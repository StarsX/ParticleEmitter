//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#include "Emitter.h"

using namespace std;
using namespace DirectX;
using namespace XUSG;

Emitter::Emitter(const Device& device) :
	m_device(device),
	m_srvTable(nullptr)
{
	m_graphicsPipelineCache.SetDevice(device);
	m_computePipelineCache.SetDevice(device);
	m_pipelineLayoutCache.SetDevice(device);
}

Emitter::~Emitter()
{
}

bool Emitter::Init(const CommandList& commandList, uint32_t numParticles,
	shared_ptr<DescriptorTableCache> descriptorTableCache,
	vector<Resource>& uploaders, const InputLayout& inputLayout,
	Format rtFormat, Format dsFormat)
{
	m_cbParticle.NumParticles = numParticles;
	m_descriptorTableCache = descriptorTableCache;

	// Create resources and pipelines
	N_RETURN(m_counter.Create(m_device, sizeof(uint32_t),
		ResourceFlag::ALLOW_UNORDERED_ACCESS | ResourceFlag::DENY_SHADER_RESOURCE,
		MemoryType::DEFAULT, 0, nullptr, 1, nullptr, L"Counter"), false);

	m_emitterBuffer.SetCounter(m_counter.GetResource());
	N_RETURN(m_emitterBuffer.Create(m_device, 1 << 24, sizeof(EmitterInfo),
		ResourceFlag::ALLOW_UNORDERED_ACCESS, MemoryType::DEFAULT, 1,
		nullptr, 1, nullptr, L"EmitterBuffer"), false);

	auto particleBufferIdx = 0ui8;
	for (auto& particleBuffer : m_particleBuffers)
		N_RETURN(particleBuffer.Create(m_device, numParticles, sizeof(ParticleInfo),
			ResourceFlag::ALLOW_UNORDERED_ACCESS, MemoryType::DEFAULT, 1, nullptr, 1,
			nullptr, (L"ParticleBuffer" + to_wstring(particleBufferIdx++)).c_str()), false);

	vector<ParticleInfo> particles(numParticles);
	for (auto& particle : particles)
	{
		particle = {};
		particle.LifeTime = rand() % numParticles / 10000.0f;
	}
	uploaders.emplace_back();
	m_particleBuffers[REARRANGED].Upload(commandList, uploaders.back(), particles.data(),
		sizeof(ParticleInfo) * numParticles);

	N_RETURN(createPipelineLayouts(), false);
	N_RETURN(createPipelines(inputLayout, rtFormat, dsFormat), false);
	N_RETURN(createDescriptorTables(), false);

	return true;
}

bool Emitter::SetEmitterCount(const CommandList& commandList, RawBuffer& counter,
	Resource* pEmitterSource)
{
	m_cbParticle.NumEmitters = *reinterpret_cast<const uint32_t*>(counter.Map(nullptr));
#if defined(_DEBUG)
	cout << m_cbParticle.NumEmitters << endl;
#endif

	if (pEmitterSource)
	{
		// Set source barrier
		ResourceBarrier barriers[2];
		auto numBarriers = m_emitterBuffer.SetBarrier(barriers, ResourceState::COPY_SOURCE);

		*pEmitterSource = m_emitterBuffer.GetResource();

		m_emitterBuffer = StructuredBuffer();
		N_RETURN(m_emitterBuffer.Create(m_device, m_cbParticle.NumEmitters, sizeof(EmitterInfo), ResourceFlag::NONE,
			MemoryType::DEFAULT, 1, nullptr, 0, nullptr, L"EmitterBuffer"), false);

		// Set barriers
		numBarriers = m_emitterBuffer.SetBarrier(barriers, ResourceState::COPY_DEST, numBarriers);
		commandList.Barrier(numBarriers, barriers);

		// Copy data
		commandList.CopyBufferRegion(m_emitterBuffer.GetResource(), 0,
			*pEmitterSource, 0, sizeof(EmitterInfo) * m_cbParticle.NumEmitters);

		// Set destination barrier
		numBarriers = m_emitterBuffer.SetBarrier(barriers, ResourceState::NON_PIXEL_SHADER_RESOURCE);
		commandList.Barrier(numBarriers, barriers);
	}

	if (!m_srvTable)
	{
		// Create SRV table
		Util::DescriptorTable srvTable;
		const Descriptor srvs[] =
		{
			m_emitterBuffer.GetSRV(),
			m_srvVertexBuffer
		};
		srvTable.SetDescriptors(0, static_cast<uint32_t>(size(srvs)), srvs);
		X_RETURN(m_srvTable, srvTable.GetCbvSrvUavTable(*m_descriptorTableCache), false);
	}

	return true;
}

void Emitter::UpdateFrame(double time, float timeStep, const CXMMATRIX viewProj)
{
	m_time = time;
	m_cbParticle.TimeStep = timeStep;
	XMStoreFloat4x4(&m_cbParticle.ViewProj, XMMatrixTranspose(viewProj));
}

void Emitter::Distribute(const CommandList& commandList, const RawBuffer& counter,
	const VertexBuffer& vb, const IndexBuffer& ib, uint32_t numIndices,
	float density, float scale)
{
	m_srvVertexBuffer = vb.GetSRV();

	const DescriptorPool descriptorPools[] =
	{
		m_descriptorTableCache->GetDescriptorPool(CBV_SRV_UAV_POOL, TEMPORARY_POOL),
	};
	commandList.SetDescriptorPools(static_cast<uint32_t>(size(descriptorPools)), descriptorPools);

	// Set barriers
	ResourceBarrier barriers[2];
	// Promotion
	m_emitterBuffer.SetBarrier(barriers, ResourceState::UNORDERED_ACCESS);
	m_counter.SetBarrier(barriers, ResourceState::UNORDERED_ACCESS);

	// Clear counter
	uint32_t clear[4] = {};
	commandList.ClearUnorderedAccessViewUint(m_uavTables[UAV_TABLE_COUNTER],
		m_counter.GetUAV(), m_counter.GetResource(), clear);

	distribute(commandList, vb, ib, numIndices, density, scale);

	// Set barriers
	auto numBarriers = m_emitterBuffer.SetBarrier(barriers, ResourceState::NON_PIXEL_SHADER_RESOURCE);
	numBarriers = m_counter.SetBarrier(barriers, ResourceState::COPY_SOURCE);
	commandList.Barrier(numBarriers, barriers);

	// Copy the counter for readback
	commandList.CopyResource(counter.GetResource(), m_counter.GetResource());
}

void Emitter::EmitParticle(const CommandList& commandList, uint32_t numParticles,
	const DescriptorTable& uavTable, const XMFLOAT4X4& world)
{
	m_cbParticle.WorldPrev = m_cbParticle.World;
	m_cbParticle.World = world;
	m_cbParticle.BaseSeed = rand();

	if (m_cbParticle.BaseSeed <= 0.0) return;

	const DescriptorPool descriptorPools[] =
	{
		m_descriptorTableCache->GetDescriptorPool(CBV_SRV_UAV_POOL),
	};
	commandList.SetDescriptorPools(static_cast<uint32_t>(size(descriptorPools)), descriptorPools);

	// Set pipeline state
	commandList.SetComputePipelineLayout(m_pipelineLayouts[EMISSION]);
	commandList.SetPipelineState(m_pipelines[EMISSION]);

	// Set descriptor tables
	commandList.SetCompute32BitConstants(0, SizeOfInUint32(CBEmission), &m_cbParticle);
	commandList.SetComputeDescriptorTable(1, m_srvTable);
	commandList.SetComputeDescriptorTable(2, uavTable);

	commandList.Dispatch(DIV_UP(numParticles, 64), 1, 1);
}

void Emitter::Render(const CommandList& commandList, const Descriptor& rtv,
	const Descriptor* pDsv, const XMFLOAT4X4& world)
{
	m_cbParticle.WorldPrev = m_cbParticle.World;
	m_cbParticle.World = world;
	m_cbParticle.BaseSeed = rand();

	const DescriptorPool descriptorPools[] =
	{
		m_descriptorTableCache->GetDescriptorPool(CBV_SRV_UAV_POOL),
	};
	commandList.SetDescriptorPools(static_cast<uint32_t>(size(descriptorPools)), descriptorPools);

	commandList.OMSetRenderTargets(1, &rtv, pDsv);

	// Set pipeline state
	commandList.SetGraphicsPipelineLayout(m_pipelineLayouts[PARTICLE]);
	commandList.SetPipelineState(m_pipelines[PARTICLE]);

	commandList.IASetPrimitiveTopology(PrimitiveTopology::POINTLIST);

	// Set descriptor tables
	commandList.SetGraphics32BitConstants(0, SizeOfInUint32(m_cbParticle), &m_cbParticle);
	commandList.SetGraphicsDescriptorTable(1, m_srvTable);
	commandList.SetGraphicsDescriptorTable(2, m_uavTables[UAV_TABLE_PARTICLE]);

	commandList.Draw(m_cbParticle.NumParticles, 1, 0, 0);
}

void Emitter::RenderSPH(const CommandList& commandList, const Descriptor& rtv,
	const Descriptor* pDsv, const DescriptorTable& buildGridDescriptorTable,
	const XMFLOAT4X4& world)
{
	m_cbParticle.WorldPrev = m_cbParticle.World;
	m_cbParticle.World = world;
	m_cbParticle.BaseSeed = rand();

	const DescriptorPool descriptorPools[] =
	{
		m_descriptorTableCache->GetDescriptorPool(CBV_SRV_UAV_POOL),
	};
	commandList.SetDescriptorPools(static_cast<uint32_t>(size(descriptorPools)), descriptorPools);

	// Set barrier with promotion
	ResourceBarrier barrier;
	m_particleBuffers[INTEGRATED].SetBarrier(&barrier, ResourceState::UNORDERED_ACCESS);

	commandList.OMSetRenderTargets(1, &rtv, pDsv);

	// Set pipeline state
	commandList.SetGraphicsPipelineLayout(m_pipelineLayouts[PARTICLE_SPH]);
	commandList.SetPipelineState(m_pipelines[PARTICLE_SPH]);

	commandList.IASetPrimitiveTopology(PrimitiveTopology::POINTLIST);

	// Set descriptor tables
	commandList.SetGraphics32BitConstants(0, SizeOfInUint32(m_cbParticle), &m_cbParticle);
	commandList.SetGraphicsDescriptorTable(1, m_srvTable);
	commandList.SetGraphicsDescriptorTable(2, m_uavTables[UAV_TABLE_PARTICLE1]);
	commandList.SetGraphicsDescriptorTable(3, buildGridDescriptorTable);

	commandList.Draw(m_cbParticle.NumParticles, 1, 0, 0);
}

void Emitter::Visualize(const CommandList& commandList, const Descriptor& rtv,
	const Descriptor* pDsv, const XMFLOAT4X4& worldViewProj)
{
	const DescriptorPool descriptorPools[] =
	{
		m_descriptorTableCache->GetDescriptorPool(CBV_SRV_UAV_POOL),
	};
	commandList.SetDescriptorPools(static_cast<uint32_t>(size(descriptorPools)), descriptorPools);

	commandList.OMSetRenderTargets(1, &rtv, pDsv);

	// Set pipeline state
	commandList.SetGraphicsPipelineLayout(m_pipelineLayouts[VISUALIZE]);
	commandList.SetPipelineState(m_pipelines[VISUALIZE]);

	commandList.IASetPrimitiveTopology(PrimitiveTopology::POINTLIST);

	// Set descriptor tables
	commandList.SetGraphics32BitConstants(0, SizeOfInUint32(XMFLOAT4X4), &worldViewProj);
	commandList.SetGraphicsDescriptorTable(1, m_srvTable);

	commandList.Draw(m_cbParticle.NumEmitters, 1, 0, 0);
}

StructuredBuffer* Emitter::GetParticleBuffers()
{
	return m_particleBuffers;
}

bool Emitter::createPipelineLayouts()
{
	// Generate uniformized distribution
	{
		Util::PipelineLayout pipelineLayout;
		pipelineLayout.SetConstants(0, SizeOfInUint32(XMFLOAT4X4), 0, 0, Shader::Stage::VS);
		pipelineLayout.SetRange(1, DescriptorType::UAV, 1, 0);
		pipelineLayout.SetShaderStage(1, Shader::Stage::DS);
		X_RETURN(m_pipelineLayouts[DISTRIBUTE], pipelineLayout.GetPipelineLayout(m_pipelineLayoutCache,
			PipelineLayoutFlag::ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT, L"DistributionLayout"), false);
	}

	// Particle emission and integration
	{
		Util::PipelineLayout pipelineLayout;
		pipelineLayout.SetConstants(0, SizeOfInUint32(CBParticle), 0, 0, Shader::Stage::VS);
		pipelineLayout.SetRange(1, DescriptorType::SRV, 2, 0, 0, DescriptorRangeFlag::DATA_STATIC);
		pipelineLayout.SetRange(2, DescriptorType::UAV, 1, 0);
		pipelineLayout.SetShaderStage(1, Shader::Stage::VS);
		pipelineLayout.SetShaderStage(2, Shader::Stage::VS);
		X_RETURN(m_pipelineLayouts[PARTICLE], pipelineLayout.GetPipelineLayout(m_pipelineLayoutCache,
			PipelineLayoutFlag::NONE, L"ParticleLayout"), false);
	}

	// Particle emission and integration for SPH
	{
		Util::PipelineLayout pipelineLayout;
		pipelineLayout.SetConstants(0, SizeOfInUint32(CBParticle), 0, 0, Shader::Stage::VS);
		pipelineLayout.SetRange(1, DescriptorType::SRV, 2, 0, 0, DescriptorRangeFlag::DATA_STATIC);
		pipelineLayout.SetRange(2, DescriptorType::UAV, 1, 0);
		pipelineLayout.SetRange(3, DescriptorType::UAV, 2, 1);
		pipelineLayout.SetRange(3, DescriptorType::SRV, 2, 2);
		pipelineLayout.SetShaderStage(1, Shader::Stage::VS);
		pipelineLayout.SetShaderStage(2, Shader::Stage::VS);
		pipelineLayout.SetShaderStage(3, Shader::Stage::VS);
		X_RETURN(m_pipelineLayouts[PARTICLE_SPH], pipelineLayout.GetPipelineLayout(m_pipelineLayoutCache,
			PipelineLayoutFlag::NONE, L"ParticleSPHLayout"), false);
	}

	// Particle emission
	{
		Util::PipelineLayout pipelineLayout;
		pipelineLayout.SetConstants(0, SizeOfInUint32(CBEmission), 0, 0, Shader::Stage::CS);
		pipelineLayout.SetRange(1, DescriptorType::SRV, 2, 0, 0, DescriptorRangeFlag::DATA_STATIC);
		pipelineLayout.SetRange(2, DescriptorType::UAV, 1, 0);
		X_RETURN(m_pipelineLayouts[EMISSION], pipelineLayout.GetPipelineLayout(m_pipelineLayoutCache,
			PipelineLayoutFlag::NONE, L"EmissionLayout"), false);
	}

	// Show emitters
	{
		Util::PipelineLayout pipelineLayout;
		pipelineLayout.SetConstants(0, SizeOfInUint32(XMFLOAT4X4), 0, 0, Shader::Stage::VS);
		pipelineLayout.SetRange(1, DescriptorType::SRV, 2, 0, 0, DescriptorRangeFlag::DATA_STATIC);
		X_RETURN(m_pipelineLayouts[VISUALIZE], pipelineLayout.GetPipelineLayout(m_pipelineLayoutCache,
			PipelineLayoutFlag::NONE, L"VisualizationLayout"), false);
	}

	return true;
}

bool Emitter::createPipelines(const InputLayout& inputLayout, Format rtFormat, Format dsFormat)
{
	auto vsIndex = 0u;
	auto hsIndex = 0u;
	auto dsIndex = 0u;
	auto psIndex = 0u;
	auto csIndex = 0u;

	// Generate uniform distribution
	{
		N_RETURN(m_shaderPool.CreateShader(Shader::Stage::VS, vsIndex, L"VSDistribute.cso"), false);
		N_RETURN(m_shaderPool.CreateShader(Shader::Stage::HS, hsIndex, L"HSDistribute.cso"), false);
		N_RETURN(m_shaderPool.CreateShader(Shader::Stage::DS, dsIndex, L"DSDistribute.cso"), false);

		Graphics::State state;
		state.IASetInputLayout(inputLayout);
		state.SetPipelineLayout(m_pipelineLayouts[DISTRIBUTE]);
		state.SetShader(Shader::Stage::VS, m_shaderPool.GetShader(Shader::Stage::VS, vsIndex++));
		state.SetShader(Shader::Stage::HS, m_shaderPool.GetShader(Shader::Stage::HS, hsIndex++));
		state.SetShader(Shader::Stage::DS, m_shaderPool.GetShader(Shader::Stage::DS, dsIndex++));
		state.DSSetState(Graphics::DEPTH_STENCIL_NONE, m_graphicsPipelineCache);
		state.IASetPrimitiveTopologyType(PrimitiveTopologyType::PATCH);
		X_RETURN(m_pipelines[DISTRIBUTE], state.GetPipeline(m_graphicsPipelineCache, L"Distribution"), false);
	}

	// Particle emission and integration
	N_RETURN(m_shaderPool.CreateShader(Shader::Stage::PS, psIndex, L"PSConstColor.cso"), false);
	{
		N_RETURN(m_shaderPool.CreateShader(Shader::Stage::VS, vsIndex, L"VSParticle.cso"), false);

		Graphics::State state;
		state.SetPipelineLayout(m_pipelineLayouts[PARTICLE]);
		state.SetShader(Shader::Stage::VS, m_shaderPool.GetShader(Shader::Stage::VS, vsIndex++));
		state.SetShader(Shader::Stage::PS, m_shaderPool.GetShader(Shader::Stage::PS, psIndex));
		state.IASetPrimitiveTopologyType(PrimitiveTopologyType::POINT);
		state.OMSetNumRenderTargets(1);
		state.OMSetRTVFormat(0, rtFormat);
		state.OMSetDSVFormat(dsFormat);
		X_RETURN(m_pipelines[PARTICLE], state.GetPipeline(m_graphicsPipelineCache, L"Particle"), false);
	}

	// Particle emission and integration for SPH
	{
		N_RETURN(m_shaderPool.CreateShader(Shader::Stage::VS, vsIndex, L"VSParticleSPH.cso"), false);

		Graphics::State state;
		state.SetPipelineLayout(m_pipelineLayouts[PARTICLE_SPH]);
		state.SetShader(Shader::Stage::VS, m_shaderPool.GetShader(Shader::Stage::VS, vsIndex++));
		state.SetShader(Shader::Stage::PS, m_shaderPool.GetShader(Shader::Stage::PS, psIndex));
		state.IASetPrimitiveTopologyType(PrimitiveTopologyType::POINT);
		state.OMSetNumRenderTargets(1);
		state.OMSetRTVFormat(0, rtFormat);
		state.OMSetDSVFormat(dsFormat);
		X_RETURN(m_pipelines[PARTICLE_SPH], state.GetPipeline(m_graphicsPipelineCache, L"ParticleSPH"), false);
	}

	// Particle emission
	{
		N_RETURN(m_shaderPool.CreateShader(Shader::Stage::CS, csIndex, L"CSEmit.cso"), false);

		Compute::State state;
		state.SetPipelineLayout(m_pipelineLayouts[EMISSION]);
		state.SetShader(m_shaderPool.GetShader(Shader::Stage::CS, csIndex++));
		X_RETURN(m_pipelines[EMISSION], state.GetPipeline(m_computePipelineCache, L"Emission"), false);
	}

	// Show emitters
	{
		N_RETURN(m_shaderPool.CreateShader(Shader::Stage::VS, vsIndex, L"VSShowEmitter.cso"), false);

		Graphics::State state;
		state.SetPipelineLayout(m_pipelineLayouts[VISUALIZE]);
		state.SetShader(Shader::Stage::VS, m_shaderPool.GetShader(Shader::Stage::VS, vsIndex++));
		state.SetShader(Shader::Stage::PS, m_shaderPool.GetShader(Shader::Stage::PS, psIndex++));
		state.IASetPrimitiveTopologyType(PrimitiveTopologyType::POINT);
		state.OMSetNumRenderTargets(1);
		state.OMSetRTVFormat(0, rtFormat);
		state.OMSetDSVFormat(dsFormat);
		X_RETURN(m_pipelines[VISUALIZE], state.GetPipeline(m_graphicsPipelineCache, L"Visualization"), false);
	}

	return true;
}

bool Emitter::createDescriptorTables()
{
	// Create UAV tables
	{
		Util::DescriptorTable uavTable;
		uavTable.SetDescriptors(0, 1, &m_emitterBuffer.GetUAV(), TEMPORARY_POOL);
		X_RETURN(m_uavTables[UAV_TABLE_EMITTER], uavTable.GetCbvSrvUavTable(*m_descriptorTableCache), false);
	}

	{
		Util::DescriptorTable uavTable;
		uavTable.SetDescriptors(0, 1, &m_counter.GetUAV(), TEMPORARY_POOL);
		X_RETURN(m_uavTables[UAV_TABLE_COUNTER], uavTable.GetCbvSrvUavTable(*m_descriptorTableCache), false);
	}

	for (auto i = 0ui8; i < 2; ++i)
	{
		Util::DescriptorTable uavTable;
		uavTable.SetDescriptors(0, 1, &m_particleBuffers[i].GetUAV());
		X_RETURN(m_uavTables[UAV_TABLE_PARTICLE + i], uavTable.GetCbvSrvUavTable(*m_descriptorTableCache), false);
	}

	return true;
}

void Emitter::distribute(const CommandList& commandList, const VertexBuffer& vb,
	const IndexBuffer& ib, uint32_t numIndices, float density, float scale)
{
	// Set pipeline state
	commandList.SetGraphicsPipelineLayout(m_pipelineLayouts[DISTRIBUTE]);
	commandList.SetPipelineState(m_pipelines[DISTRIBUTE]);

	commandList.IASetPrimitiveTopology(PrimitiveTopology::CONTROL_POINT3_PATCHLIST);

	// Set descriptor tables
	scale *= density;
	const auto transform = XMMatrixTranspose(XMMatrixScaling(scale, scale, scale));
	commandList.SetGraphics32BitConstants(0, SizeOfInUint32(XMFLOAT4X4), &transform);
	commandList.SetGraphicsDescriptorTable(1, m_uavTables[UAV_TABLE_EMITTER]);

	commandList.IASetVertexBuffers(0, 1, &vb.GetVBV());
	commandList.IASetIndexBuffer(ib.GetIBV());

	commandList.DrawIndexed(numIndices, 1, 0, 0, 0);
}
