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
	m_shaderPool = ShaderPool::MakeUnique();
	m_graphicsPipelineCache = Graphics::PipelineCache::MakeUnique(device);
	m_computePipelineCache = Compute::PipelineCache::MakeUnique(device);
	m_pipelineLayoutCache = PipelineLayoutCache::MakeUnique(device);
}

Emitter::~Emitter()
{
}

bool Emitter::Init(CommandList* pCommandList, uint32_t numParticles,
	shared_ptr<DescriptorTableCache> descriptorTableCache,
	vector<Resource>& uploaders, const InputLayout& inputLayout,
	Format rtFormat, Format dsFormat)
{
	m_cbParticle.NumParticles = numParticles;
	m_descriptorTableCache = descriptorTableCache;

	// Create resources and pipelines
	m_counter = RawBuffer::MakeUnique();
	N_RETURN(m_counter->Create(m_device, sizeof(uint32_t),
		ResourceFlag::ALLOW_UNORDERED_ACCESS | ResourceFlag::DENY_SHADER_RESOURCE,
		MemoryType::DEFAULT, 0, nullptr, 1, nullptr, L"Counter"), false);

	m_emitterBuffer = StructuredBuffer::MakeUnique();
	m_emitterBuffer->SetCounter(m_counter->GetResource());
	N_RETURN(m_emitterBuffer->Create(m_device, 1 << 24, sizeof(EmitterInfo),
		ResourceFlag::ALLOW_UNORDERED_ACCESS, MemoryType::DEFAULT, 1,
		nullptr, 1, nullptr, L"EmitterBuffer"), false);

	auto particleBufferIdx = 0ui8;
	for (auto& particleBuffer : m_particleBuffers)
	{
		particleBuffer = StructuredBuffer::MakeUnique();
		N_RETURN(particleBuffer->Create(m_device, numParticles, sizeof(ParticleInfo),
			ResourceFlag::ALLOW_UNORDERED_ACCESS, MemoryType::DEFAULT, 1, nullptr, 1,
			nullptr, (L"ParticleBuffer" + to_wstring(particleBufferIdx++)).c_str()), false);
	}

	vector<ParticleInfo> particles(numParticles);
	for (auto& particle : particles)
	{
		particle = {};
		particle.Pos.y = FLT_MAX;
		particle.LifeTime = rand() % numParticles / 10000.0f;
	}
	uploaders.emplace_back();
	m_particleBuffers[REARRANGED]->Upload(pCommandList, uploaders.back(), particles.data(),
		sizeof(ParticleInfo) * numParticles);

	N_RETURN(createPipelineLayouts(), false);
	N_RETURN(createPipelines(inputLayout, rtFormat, dsFormat), false);
	N_RETURN(createDescriptorTables(), false);

	return true;
}

bool Emitter::SetEmitterCount(const CommandList* pCommandList, RawBuffer& counter,
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
		auto numBarriers = m_emitterBuffer->SetBarrier(barriers, ResourceState::COPY_SOURCE);

		*pEmitterSource = m_emitterBuffer->GetResource();

		m_emitterBuffer = StructuredBuffer::MakeUnique();
		N_RETURN(m_emitterBuffer->Create(m_device, m_cbParticle.NumEmitters, sizeof(EmitterInfo), ResourceFlag::NONE,
			MemoryType::DEFAULT, 1, nullptr, 0, nullptr, L"EmitterBuffer"), false);

		// Set barriers
		numBarriers = m_emitterBuffer->SetBarrier(barriers, ResourceState::COPY_DEST, numBarriers);
		pCommandList->Barrier(numBarriers, barriers);

		// Copy data
		pCommandList->CopyBufferRegion(m_emitterBuffer->GetResource(), 0,
			*pEmitterSource, 0, sizeof(EmitterInfo) * m_cbParticle.NumEmitters);

		// Set destination barrier
		numBarriers = m_emitterBuffer->SetBarrier(barriers, ResourceState::NON_PIXEL_SHADER_RESOURCE);
		pCommandList->Barrier(numBarriers, barriers);
	}

	if (!m_srvTable)
	{
		// Create SRV table
		const auto descriptorTable = Util::DescriptorTable::MakeUnique();
		const Descriptor descriptors[] =
		{
			m_emitterBuffer->GetSRV(),
			m_srvVertexBuffer
		};
		descriptorTable->SetDescriptors(0, static_cast<uint32_t>(size(descriptors)), descriptors);
		X_RETURN(m_srvTable, descriptorTable->GetCbvSrvUavTable(*m_descriptorTableCache), false);
	}

	return true;
}

void Emitter::UpdateFrame(double time, float timeStep, const CXMMATRIX viewProj)
{
	m_time = time;
	m_cbParticle.TimeStep = timeStep;
	XMStoreFloat4x4(&m_cbParticle.ViewProj, XMMatrixTranspose(viewProj));
}

void Emitter::Distribute(const CommandList* pCommandList, const RawBuffer& counter,
	const VertexBuffer& vb, const IndexBuffer& ib, uint32_t numIndices,
	float density, float scale)
{
	m_srvVertexBuffer = vb.GetSRV();

	const DescriptorPool descriptorPools[] =
	{
		m_descriptorTableCache->GetDescriptorPool(CBV_SRV_UAV_POOL, TEMPORARY_POOL),
	};
	pCommandList->SetDescriptorPools(static_cast<uint32_t>(size(descriptorPools)), descriptorPools);

	// Set barriers
	ResourceBarrier barriers[2];
	// Promotion
	m_emitterBuffer->SetBarrier(barriers, ResourceState::UNORDERED_ACCESS);
	m_counter->SetBarrier(barriers, ResourceState::UNORDERED_ACCESS);

	// Clear counter
	uint32_t clear[4] = {};
	pCommandList->ClearUnorderedAccessViewUint(m_uavTables[UAV_TABLE_COUNTER],
		m_counter->GetUAV(), m_counter->GetResource(), clear);

	distribute(pCommandList, vb, ib, numIndices, density, scale);

	// Set barriers
	auto numBarriers = m_emitterBuffer->SetBarrier(barriers, ResourceState::NON_PIXEL_SHADER_RESOURCE);
	numBarriers = m_counter->SetBarrier(barriers, ResourceState::COPY_SOURCE);
	pCommandList->Barrier(numBarriers, barriers);

	// Copy the counter for readback
	pCommandList->CopyResource(counter.GetResource(), m_counter->GetResource());
}

void Emitter::EmitParticle(const CommandList* pCommandList, uint32_t numParticles,
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
	pCommandList->SetDescriptorPools(static_cast<uint32_t>(size(descriptorPools)), descriptorPools);

	// Set pipeline state
	pCommandList->SetComputePipelineLayout(m_pipelineLayouts[EMISSION]);
	pCommandList->SetPipelineState(m_pipelines[EMISSION]);

	// Set descriptor tables
	pCommandList->SetCompute32BitConstants(0, SizeOfInUint32(CBEmission), &m_cbParticle);
	pCommandList->SetComputeDescriptorTable(1, m_srvTable);
	pCommandList->SetComputeDescriptorTable(2, uavTable);

	pCommandList->Dispatch(DIV_UP(numParticles, 64), 1, 1);
}

void Emitter::Render(const CommandList* pCommandList, const Descriptor& rtv,
	const Descriptor* pDsv, const XMFLOAT4X4& world)
{
	m_cbParticle.WorldPrev = m_cbParticle.World;
	m_cbParticle.World = world;
	m_cbParticle.BaseSeed = rand();

	const DescriptorPool descriptorPools[] =
	{
		m_descriptorTableCache->GetDescriptorPool(CBV_SRV_UAV_POOL),
	};
	pCommandList->SetDescriptorPools(static_cast<uint32_t>(size(descriptorPools)), descriptorPools);

	pCommandList->OMSetRenderTargets(1, &rtv, pDsv);

	// Set pipeline state
	pCommandList->SetGraphicsPipelineLayout(m_pipelineLayouts[PARTICLE]);
	pCommandList->SetPipelineState(m_pipelines[PARTICLE]);

	pCommandList->IASetPrimitiveTopology(PrimitiveTopology::POINTLIST);

	// Set descriptor tables
	pCommandList->SetGraphics32BitConstants(0, SizeOfInUint32(m_cbParticle), &m_cbParticle);
	pCommandList->SetGraphicsDescriptorTable(1, m_srvTable);
	pCommandList->SetGraphicsDescriptorTable(2, m_uavTables[UAV_TABLE_PARTICLE]);

	pCommandList->Draw(m_cbParticle.NumParticles, 1, 0, 0);
}

void Emitter::RenderSPH(const CommandList* pCommandList, const Descriptor& rtv,
	const Descriptor* pDsv, const DescriptorTable& fluidDescriptorTable,
	const XMFLOAT4X4& world)
{
	m_cbParticle.WorldPrev = m_cbParticle.World;
	m_cbParticle.World = world;
	m_cbParticle.BaseSeed = rand();

	const DescriptorPool descriptorPools[] =
	{
		m_descriptorTableCache->GetDescriptorPool(CBV_SRV_UAV_POOL),
	};
	pCommandList->SetDescriptorPools(static_cast<uint32_t>(size(descriptorPools)), descriptorPools);

	// Set barrier with promotion
	ResourceBarrier barrier;
	m_particleBuffers[INTEGRATED]->SetBarrier(&barrier, ResourceState::UNORDERED_ACCESS);
	const auto numBarriers = m_particleBuffers[REARRANGED]->SetBarrier(&barrier, ResourceState::NON_PIXEL_SHADER_RESOURCE);
	pCommandList->Barrier(numBarriers, &barrier);

	pCommandList->OMSetRenderTargets(1, &rtv, pDsv);

	// Set pipeline state
	pCommandList->SetGraphicsPipelineLayout(m_pipelineLayouts[PARTICLE_SPH]);
	pCommandList->SetPipelineState(m_pipelines[PARTICLE_SPH]);

	pCommandList->IASetPrimitiveTopology(PrimitiveTopology::POINTLIST);

	// Set descriptor tables
	pCommandList->SetGraphics32BitConstants(0, SizeOfInUint32(m_cbParticle), &m_cbParticle);
	pCommandList->SetGraphicsDescriptorTable(1, m_srvTable);
	pCommandList->SetGraphicsDescriptorTable(2, m_uavTables[UAV_TABLE_PARTICLE1]);
	pCommandList->SetGraphicsDescriptorTable(3, fluidDescriptorTable);

	pCommandList->Draw(m_cbParticle.NumParticles, 1, 0, 0);
}

void Emitter::RenderFHF(const CommandList* pCommandList, const Descriptor& rtv,
	const Descriptor* pDsv, const DescriptorTable& fluidDescriptorTable,
	const XMFLOAT4X4& world)
{
	m_cbParticle.WorldPrev = m_cbParticle.World;
	m_cbParticle.World = world;
	m_cbParticle.BaseSeed = rand();

	const DescriptorPool descriptorPools[] =
	{
		m_descriptorTableCache->GetDescriptorPool(CBV_SRV_UAV_POOL),
		m_descriptorTableCache->GetDescriptorPool(SAMPLER_POOL)
	};
	pCommandList->SetDescriptorPools(static_cast<uint32_t>(size(descriptorPools)), descriptorPools);

	pCommandList->OMSetRenderTargets(1, &rtv, pDsv);

	// Set pipeline state
	pCommandList->SetGraphicsPipelineLayout(m_pipelineLayouts[PARTICLE_FHF]);
	pCommandList->SetPipelineState(m_pipelines[PARTICLE_FHF]);

	pCommandList->IASetPrimitiveTopology(PrimitiveTopology::POINTLIST);

	// Set descriptor tables
	pCommandList->SetGraphics32BitConstants(0, SizeOfInUint32(m_cbParticle), &m_cbParticle);
	pCommandList->SetGraphicsDescriptorTable(1, m_srvTable);
	pCommandList->SetGraphicsDescriptorTable(2, m_uavTables[UAV_TABLE_PARTICLE]);
	pCommandList->SetGraphicsDescriptorTable(3, fluidDescriptorTable);
	pCommandList->SetGraphicsDescriptorTable(4, m_samplerTable);

	pCommandList->Draw(m_cbParticle.NumParticles, 1, 0, 0);
}

void Emitter::ParticleFHS(const CommandList* pCommandList,
	const DescriptorTable& fluidDescriptorTable,
	const XMFLOAT4X4& world)
{
	m_cbParticle.WorldPrev = m_cbParticle.World;
	m_cbParticle.World = world;
	m_cbParticle.BaseSeed = rand();

	const DescriptorPool descriptorPools[] =
	{
		m_descriptorTableCache->GetDescriptorPool(CBV_SRV_UAV_POOL),
		m_descriptorTableCache->GetDescriptorPool(SAMPLER_POOL)
	};
	pCommandList->SetDescriptorPools(static_cast<uint32_t>(size(descriptorPools)), descriptorPools);

	// Set pipeline state
	pCommandList->SetComputePipelineLayout(m_pipelineLayouts[PARTICLE_FHS]);
	pCommandList->SetPipelineState(m_pipelines[PARTICLE_FHS]);

	// Set descriptor tables
	pCommandList->SetCompute32BitConstants(0, SizeOfInUint32(m_cbParticle), &m_cbParticle);
	pCommandList->SetComputeDescriptorTable(1, m_srvTable);
	pCommandList->SetComputeDescriptorTable(2, m_uavTables[UAV_TABLE_PARTICLE]);
	pCommandList->SetComputeDescriptorTable(3, fluidDescriptorTable);
	pCommandList->SetComputeDescriptorTable(4, m_samplerTable);

	pCommandList->Dispatch(DIV_UP(m_cbParticle.NumParticles, 64), 1, 1);
}

void Emitter::Visualize(const CommandList* pCommandList, const Descriptor& rtv,
	const Descriptor* pDsv, const XMFLOAT4X4& worldViewProj)
{
	const DescriptorPool descriptorPools[] =
	{
		m_descriptorTableCache->GetDescriptorPool(CBV_SRV_UAV_POOL),
	};
	pCommandList->SetDescriptorPools(static_cast<uint32_t>(size(descriptorPools)), descriptorPools);

	pCommandList->OMSetRenderTargets(1, &rtv, pDsv);

	// Set pipeline state
	pCommandList->SetGraphicsPipelineLayout(m_pipelineLayouts[VISUALIZE]);
	pCommandList->SetPipelineState(m_pipelines[VISUALIZE]);

	pCommandList->IASetPrimitiveTopology(PrimitiveTopology::POINTLIST);

	// Set descriptor tables
	pCommandList->SetGraphics32BitConstants(0, SizeOfInUint32(XMFLOAT4X4), &worldViewProj);
	pCommandList->SetGraphicsDescriptorTable(1, m_srvTable);

	pCommandList->Draw(m_cbParticle.NumEmitters, 1, 0, 0);
}

StructuredBuffer::uptr* Emitter::GetParticleBuffers()
{
	return m_particleBuffers;
}

bool Emitter::createPipelineLayouts()
{
	// Generate uniformized distribution
	{
		const auto pipelineLayout = Util::PipelineLayout::MakeUnique();
		pipelineLayout->SetConstants(0, SizeOfInUint32(XMFLOAT4X4), 0, 0, Shader::Stage::VS);
		pipelineLayout->SetRange(1, DescriptorType::UAV, 1, 0);
		pipelineLayout->SetShaderStage(1, Shader::Stage::DS);
		X_RETURN(m_pipelineLayouts[DISTRIBUTE], pipelineLayout->GetPipelineLayout(*m_pipelineLayoutCache,
			PipelineLayoutFlag::ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT, L"DistributionLayout"), false);
	}

	// Particle emission and integration
	{
		const auto pipelineLayout = Util::PipelineLayout::MakeUnique();
		pipelineLayout->SetConstants(0, SizeOfInUint32(CBParticle), 0, 0, Shader::Stage::VS);
		pipelineLayout->SetRange(1, DescriptorType::SRV, 2, 0, 0, DescriptorFlag::DATA_STATIC);
		pipelineLayout->SetRange(2, DescriptorType::UAV, 1, 0);
		pipelineLayout->SetShaderStage(1, Shader::Stage::VS);
		pipelineLayout->SetShaderStage(2, Shader::Stage::VS);
		X_RETURN(m_pipelineLayouts[PARTICLE], pipelineLayout->GetPipelineLayout(*m_pipelineLayoutCache,
			PipelineLayoutFlag::NONE, L"ParticleLayout"), false);
	}

	// Particle emission and integration for SPH
	{
		const auto pipelineLayout = Util::PipelineLayout::MakeUnique();
		pipelineLayout->SetConstants(0, SizeOfInUint32(CBParticle), 0, 0, Shader::Stage::VS);
		pipelineLayout->SetRange(1, DescriptorType::SRV, 2, 0, 0, DescriptorFlag::DATA_STATIC);
		pipelineLayout->SetRange(2, DescriptorType::UAV, 1, 0);
		pipelineLayout->SetRange(3, DescriptorType::UAV, 2, 1);
		pipelineLayout->SetRange(3, DescriptorType::SRV, 2, 2);
		pipelineLayout->SetShaderStage(1, Shader::Stage::VS);
		pipelineLayout->SetShaderStage(2, Shader::Stage::VS);
		pipelineLayout->SetShaderStage(3, Shader::Stage::VS);
		X_RETURN(m_pipelineLayouts[PARTICLE_SPH], pipelineLayout->GetPipelineLayout(*m_pipelineLayoutCache,
			PipelineLayoutFlag::NONE, L"ParticleSPHLayout"), false);
	}

	// Particle emission and integration for fast hybrid fluid
	{
		const auto pipelineLayout = Util::PipelineLayout::MakeUnique();
		pipelineLayout->SetConstants(0, SizeOfInUint32(CBParticle), 0, 0, Shader::Stage::VS);
		pipelineLayout->SetRange(1, DescriptorType::SRV, 2, 0, 0, DescriptorFlag::DATA_STATIC);
		pipelineLayout->SetRange(2, DescriptorType::UAV, 1, 0);
		pipelineLayout->SetRange(3, DescriptorType::CBV, 1, 1);
		pipelineLayout->SetRange(3, DescriptorType::SRV, 1, 2);
		pipelineLayout->SetRange(3, DescriptorType::UAV, 4, 1);
		pipelineLayout->SetRange(4, DescriptorType::SAMPLER, 1, 0);
		pipelineLayout->SetShaderStage(1, Shader::Stage::VS);
		pipelineLayout->SetShaderStage(2, Shader::Stage::VS);
		pipelineLayout->SetShaderStage(3, Shader::Stage::VS);
		pipelineLayout->SetShaderStage(4, Shader::Stage::VS);
		X_RETURN(m_pipelineLayouts[PARTICLE_FHF], pipelineLayout->GetPipelineLayout(*m_pipelineLayoutCache,
			PipelineLayoutFlag::NONE, L"ParticleFHFLayout"), false);
	}

	// Particle emission and integration for fast hybrid smoke
	{
		const auto pipelineLayout = Util::PipelineLayout::MakeUnique();
		pipelineLayout->SetConstants(0, SizeOfInUint32(CBParticle), 0, 0, Shader::Stage::CS);
		pipelineLayout->SetRange(1, DescriptorType::SRV, 2, 0, 0, DescriptorFlag::DATA_STATIC);
		pipelineLayout->SetRange(2, DescriptorType::UAV, 1, 0);
		pipelineLayout->SetRange(3, DescriptorType::CBV, 1, 1);
		pipelineLayout->SetRange(3, DescriptorType::SRV, 1, 2);
		pipelineLayout->SetRange(3, DescriptorType::UAV, 1, 1);
		pipelineLayout->SetRange(4, DescriptorType::SAMPLER, 1, 0);
		X_RETURN(m_pipelineLayouts[PARTICLE_FHS], pipelineLayout->GetPipelineLayout(*m_pipelineLayoutCache,
			PipelineLayoutFlag::NONE, L"ParticleFHSLayout"), false);
	}

	// Particle emission
	{
		const auto pipelineLayout = Util::PipelineLayout::MakeUnique();
		pipelineLayout->SetConstants(0, SizeOfInUint32(CBEmission), 0, 0, Shader::Stage::CS);
		pipelineLayout->SetRange(1, DescriptorType::SRV, 2, 0, 0, DescriptorFlag::DATA_STATIC);
		pipelineLayout->SetRange(2, DescriptorType::UAV, 1, 0);
		X_RETURN(m_pipelineLayouts[EMISSION], pipelineLayout->GetPipelineLayout(*m_pipelineLayoutCache,
			PipelineLayoutFlag::NONE, L"EmissionLayout"), false);
	}

	// Show emitters
	{
		const auto pipelineLayout = Util::PipelineLayout::MakeUnique();
		pipelineLayout->SetConstants(0, SizeOfInUint32(XMFLOAT4X4), 0, 0, Shader::Stage::VS);
		pipelineLayout->SetRange(1, DescriptorType::SRV, 2, 0, 0, DescriptorFlag::DATA_STATIC);
		X_RETURN(m_pipelineLayouts[VISUALIZE], pipelineLayout->GetPipelineLayout(*m_pipelineLayoutCache,
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
		N_RETURN(m_shaderPool->CreateShader(Shader::Stage::VS, vsIndex, L"VSDistribute.cso"), false);
		N_RETURN(m_shaderPool->CreateShader(Shader::Stage::HS, hsIndex, L"HSDistribute.cso"), false);
		N_RETURN(m_shaderPool->CreateShader(Shader::Stage::DS, dsIndex, L"DSDistribute.cso"), false);

		const auto state = Graphics::State::MakeUnique();
		state->IASetInputLayout(inputLayout);
		state->SetPipelineLayout(m_pipelineLayouts[DISTRIBUTE]);
		state->SetShader(Shader::Stage::VS, m_shaderPool->GetShader(Shader::Stage::VS, vsIndex++));
		state->SetShader(Shader::Stage::HS, m_shaderPool->GetShader(Shader::Stage::HS, hsIndex++));
		state->SetShader(Shader::Stage::DS, m_shaderPool->GetShader(Shader::Stage::DS, dsIndex++));
		state->DSSetState(Graphics::DEPTH_STENCIL_NONE, *m_graphicsPipelineCache);
		state->IASetPrimitiveTopologyType(PrimitiveTopologyType::PATCH);
		X_RETURN(m_pipelines[DISTRIBUTE], state->GetPipeline(*m_graphicsPipelineCache, L"Distribution"), false);
	}

	// Particle emission and integration
	N_RETURN(m_shaderPool->CreateShader(Shader::Stage::PS, psIndex, L"PSConstColor.cso"), false);
	{
		N_RETURN(m_shaderPool->CreateShader(Shader::Stage::VS, vsIndex, L"VSParticle.cso"), false);

		const auto state = Graphics::State::MakeUnique();
		state->SetPipelineLayout(m_pipelineLayouts[PARTICLE]);
		state->SetShader(Shader::Stage::VS, m_shaderPool->GetShader(Shader::Stage::VS, vsIndex++));
		state->SetShader(Shader::Stage::PS, m_shaderPool->GetShader(Shader::Stage::PS, psIndex));
		state->IASetPrimitiveTopologyType(PrimitiveTopologyType::POINT);
		state->OMSetNumRenderTargets(1);
		state->OMSetRTVFormat(0, rtFormat);
		state->OMSetDSVFormat(dsFormat);
		X_RETURN(m_pipelines[PARTICLE], state->GetPipeline(*m_graphicsPipelineCache, L"Particle"), false);
	}

	// Particle emission and integration for SPH
	{
		N_RETURN(m_shaderPool->CreateShader(Shader::Stage::VS, vsIndex, L"VSParticleSPH.cso"), false);

		const auto state = Graphics::State::MakeUnique();
		state->SetPipelineLayout(m_pipelineLayouts[PARTICLE_SPH]);
		state->SetShader(Shader::Stage::VS, m_shaderPool->GetShader(Shader::Stage::VS, vsIndex++));
		state->SetShader(Shader::Stage::PS, m_shaderPool->GetShader(Shader::Stage::PS, psIndex));
		state->IASetPrimitiveTopologyType(PrimitiveTopologyType::POINT);
		state->OMSetNumRenderTargets(1);
		state->OMSetRTVFormat(0, rtFormat);
		state->OMSetDSVFormat(dsFormat);
		X_RETURN(m_pipelines[PARTICLE_SPH], state->GetPipeline(*m_graphicsPipelineCache, L"ParticleSPH"), false);
	}

	// Particle emission and integration for fast hybrid fluid
	{
		N_RETURN(m_shaderPool->CreateShader(Shader::Stage::VS, vsIndex, L"VSParticleFHF.cso"), false);

		const auto state = Graphics::State::MakeUnique();
		state->SetPipelineLayout(m_pipelineLayouts[PARTICLE_FHF]);
		state->SetShader(Shader::Stage::VS, m_shaderPool->GetShader(Shader::Stage::VS, vsIndex++));
		state->SetShader(Shader::Stage::PS, m_shaderPool->GetShader(Shader::Stage::PS, psIndex));
		state->IASetPrimitiveTopologyType(PrimitiveTopologyType::POINT);
		state->OMSetNumRenderTargets(1);
		state->OMSetRTVFormat(0, rtFormat);
		state->OMSetDSVFormat(dsFormat);
		X_RETURN(m_pipelines[PARTICLE_FHF], state->GetPipeline(*m_graphicsPipelineCache, L"ParticleFHF"), false);
	}

	// Particle emission and integration for fast hybrid smoke
	{
		N_RETURN(m_shaderPool->CreateShader(Shader::Stage::CS, csIndex, L"CSParticleFHS.cso"), false);

		const auto state = Compute::State::MakeUnique();
		state->SetPipelineLayout(m_pipelineLayouts[PARTICLE_FHS]);
		state->SetShader(m_shaderPool->GetShader(Shader::Stage::CS, csIndex++));
		X_RETURN(m_pipelines[PARTICLE_FHS], state->GetPipeline(*m_computePipelineCache, L"ParticleFHS"), false);
	}

	// Particle emission
	{
		N_RETURN(m_shaderPool->CreateShader(Shader::Stage::CS, csIndex, L"CSEmit.cso"), false);

		const auto state = Compute::State::MakeUnique();
		state->SetPipelineLayout(m_pipelineLayouts[EMISSION]);
		state->SetShader(m_shaderPool->GetShader(Shader::Stage::CS, csIndex++));
		X_RETURN(m_pipelines[EMISSION], state->GetPipeline(*m_computePipelineCache, L"Emission"), false);
	}

	// Show emitters
	{
		N_RETURN(m_shaderPool->CreateShader(Shader::Stage::VS, vsIndex, L"VSShowEmitter.cso"), false);

		const auto state = Graphics::State::MakeUnique();
		state->SetPipelineLayout(m_pipelineLayouts[VISUALIZE]);
		state->SetShader(Shader::Stage::VS, m_shaderPool->GetShader(Shader::Stage::VS, vsIndex++));
		state->SetShader(Shader::Stage::PS, m_shaderPool->GetShader(Shader::Stage::PS, psIndex++));
		state->IASetPrimitiveTopologyType(PrimitiveTopologyType::POINT);
		state->OMSetNumRenderTargets(1);
		state->OMSetRTVFormat(0, rtFormat);
		state->OMSetDSVFormat(dsFormat);
		X_RETURN(m_pipelines[VISUALIZE], state->GetPipeline(*m_graphicsPipelineCache, L"Visualization"), false);
	}

	return true;
}

bool Emitter::createDescriptorTables()
{
	// Create UAV tables
	{
		const auto descriptorTable = Util::DescriptorTable::MakeUnique();
		descriptorTable->SetDescriptors(0, 1, &m_emitterBuffer->GetUAV(), TEMPORARY_POOL);
		X_RETURN(m_uavTables[UAV_TABLE_EMITTER], descriptorTable->GetCbvSrvUavTable(*m_descriptorTableCache), false);
	}

	{
		const auto descriptorTable = Util::DescriptorTable::MakeUnique();
		descriptorTable->SetDescriptors(0, 1, &m_counter->GetUAV(), TEMPORARY_POOL);
		X_RETURN(m_uavTables[UAV_TABLE_COUNTER], descriptorTable->GetCbvSrvUavTable(*m_descriptorTableCache), false);
	}

	for (auto i = 0ui8; i < 2; ++i)
	{
		const auto descriptorTable = Util::DescriptorTable::MakeUnique();
		descriptorTable->SetDescriptors(0, 1, &m_particleBuffers[i]->GetUAV());
		X_RETURN(m_uavTables[UAV_TABLE_PARTICLE + i], descriptorTable->GetCbvSrvUavTable(*m_descriptorTableCache), false);
	}

	// Create the sampler
	{
		const auto descriptorTable = Util::DescriptorTable::MakeUnique();
		const auto samplerAnisoWrap = SamplerPreset::LINEAR_CLAMP;
		descriptorTable->SetSamplers(0, 1, &samplerAnisoWrap, *m_descriptorTableCache);
		X_RETURN(m_samplerTable, descriptorTable->GetSamplerTable(*m_descriptorTableCache), false);
	}

	return true;
}

void Emitter::distribute(const CommandList* pCommandList, const VertexBuffer& vb,
	const IndexBuffer& ib, uint32_t numIndices, float density, float scale)
{
	// Set pipeline state
	pCommandList->SetGraphicsPipelineLayout(m_pipelineLayouts[DISTRIBUTE]);
	pCommandList->SetPipelineState(m_pipelines[DISTRIBUTE]);

	pCommandList->IASetPrimitiveTopology(PrimitiveTopology::CONTROL_POINT3_PATCHLIST);

	// Set descriptor tables
	scale *= density;
	const auto transform = XMMatrixTranspose(XMMatrixScaling(scale, scale, scale));
	pCommandList->SetGraphics32BitConstants(0, SizeOfInUint32(XMFLOAT4X4), &transform);
	pCommandList->SetGraphicsDescriptorTable(1, m_uavTables[UAV_TABLE_EMITTER]);

	pCommandList->IASetVertexBuffers(0, 1, &vb.GetVBV());
	pCommandList->IASetIndexBuffer(ib.GetIBV());

	pCommandList->DrawIndexed(numIndices, 1, 0, 0, 0);
}
