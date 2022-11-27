//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#include "Emitter.h"

using namespace std;
using namespace DirectX;
using namespace XUSG;

struct EmitterInfo
{
	DirectX::XMUINT3 Indices;
	DirectX::XMFLOAT2 Barycoord;
};

struct ParticleInfo
{
	DirectX::XMFLOAT3 Pos;
	DirectX::XMFLOAT3 Velocity;
	float LifeTime;
};

struct CBPerObject
{
	DirectX::XMFLOAT3X4 World;
	DirectX::XMFLOAT3X4 WorldPrev;
	float TimeStep;
	uint32_t BaseSeed;
	uint32_t NumEmitters;
	uint32_t NumParticles;
	DirectX::XMFLOAT4X4 ViewProj;
};

Emitter::Emitter() :
	m_srvTable(nullptr)
{
	m_shaderLib = ShaderLib::MakeUnique();
}

Emitter::~Emitter()
{
}

bool Emitter::Init(CommandList* pCommandList, uint32_t numParticles,
	const DescriptorTableLib::sptr& descriptorTableLib,
	vector<Resource::uptr>& uploaders, const InputLayout* pInputLayout,
	Format rtFormat, Format dsFormat)
{
	const auto pDevice = pCommandList->GetDevice();
	m_graphicsPipelineLib = Graphics::PipelineLib::MakeUnique(pDevice);
	m_computePipelineLib = Compute::PipelineLib::MakeUnique(pDevice);
	m_pipelineLayoutLib = PipelineLayoutLib::MakeUnique(pDevice);
	m_descriptorTableLib = descriptorTableLib;
	m_numParticles = numParticles;

	// Create resources and pipelines
	m_counter = RawBuffer::MakeShared();
	XUSG_N_RETURN(m_counter->Create(pDevice, sizeof(uint32_t),
		ResourceFlag::ALLOW_UNORDERED_ACCESS | ResourceFlag::DENY_SHADER_RESOURCE,
		MemoryType::DEFAULT, 0, nullptr, 1, nullptr, MemoryFlag::NONE, L"Counter"), false);

	m_emitterBuffer = StructuredBuffer::MakeUnique();
	m_emitterBuffer->SetCounter(m_counter);
	XUSG_N_RETURN(m_emitterBuffer->Create(pDevice, 1 << 24, sizeof(EmitterInfo),
		ResourceFlag::ALLOW_UNORDERED_ACCESS, MemoryType::DEFAULT, 1,
		nullptr, 1, nullptr, MemoryFlag::NONE, L"EmitterBuffer"), false);

	uint8_t particleBufferIdx = 0;
	for (auto& particleBuffer : m_particleBuffers)
	{
		particleBuffer = StructuredBuffer::MakeUnique();
		XUSG_N_RETURN(particleBuffer->Create(pDevice, numParticles, sizeof(ParticleInfo),
			ResourceFlag::ALLOW_UNORDERED_ACCESS, MemoryType::DEFAULT, 1, nullptr, 1, nullptr,
			MemoryFlag::NONE, (L"ParticleBuffer" + to_wstring(particleBufferIdx++)).c_str()), false);
	}

	vector<ParticleInfo> particles(numParticles);
	for (auto& particle : particles)
	{
		particle = {};
		particle.Pos.y = FLT_MAX;
		particle.LifeTime = rand() % numParticles / 10000.0f;
	}
	uploaders.emplace_back(Resource::MakeUnique());
	m_particleBuffers[REARRANGED]->Upload(pCommandList, uploaders.back().get(), particles.data(),
		sizeof(ParticleInfo) * numParticles);

	// Create constant buffer
	m_cbPerObject = ConstantBuffer::MakeUnique();
	XUSG_N_RETURN(m_cbPerObject->Create(pDevice, sizeof(CBPerObject[FrameCount]), FrameCount,
		nullptr, MemoryType::UPLOAD, MemoryFlag::NONE, L"CBParticle"), false);

	XUSG_N_RETURN(createPipelineLayouts(), false);
	XUSG_N_RETURN(createPipelines(pInputLayout, rtFormat, dsFormat), false);
	XUSG_N_RETURN(createDescriptorTables(), false);

	return true;
}

bool Emitter::SetEmitterCount(CommandList* pCommandList, RawBuffer* pCounter,
	XUSG::StructuredBuffer::uptr& emitterScratch)
{
	m_numEmitters = *reinterpret_cast<const uint32_t*>(pCounter->Map(nullptr));
#if defined(_DEBUG)
	cout << m_numEmitters << endl;
#endif

	if (emitterScratch)
	{
		// Set source barrier
		ResourceBarrier barriers[2];
		auto numBarriers = m_emitterBuffer->SetBarrier(barriers, ResourceState::COPY_SOURCE);

		XUSG_N_RETURN(emitterScratch->Create(pCommandList->GetDevice(), m_numEmitters, sizeof(EmitterInfo), ResourceFlag::NONE,
			MemoryType::DEFAULT, 1, nullptr, 0, nullptr, MemoryFlag::NONE, L"EmitterBuffer"), false);

		// Set barriers
		numBarriers = emitterScratch->SetBarrier(barriers, ResourceState::COPY_DEST, numBarriers);
		pCommandList->Barrier(numBarriers, barriers);

		// Copy data
		pCommandList->CopyBufferRegion(emitterScratch.get(), 0,
			m_emitterBuffer.get(), 0, sizeof(EmitterInfo) * m_numEmitters);

		// Set destination barrier
		numBarriers = emitterScratch->SetBarrier(barriers, ResourceState::NON_PIXEL_SHADER_RESOURCE);
		pCommandList->Barrier(numBarriers, barriers);

		m_emitterBuffer.swap(emitterScratch);
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
		XUSG_X_RETURN(m_srvTable, descriptorTable->GetCbvSrvUavTable(m_descriptorTableLib.get()), false);
	}

	return true;
}

void Emitter::UpdateFrame(uint8_t frameIndex, double time, float timeStep,
	const XMFLOAT3X4& world, const CXMMATRIX viewProj)
{
	m_time = time;

	const auto pCbData = reinterpret_cast<CBPerObject*>(m_cbPerObject->Map(frameIndex));
	pCbData->WorldPrev = m_world;
	pCbData->World = world;
	pCbData->TimeStep = timeStep;
	pCbData->BaseSeed = rand();
	pCbData->NumEmitters = m_numEmitters;
	pCbData->NumParticles = m_numParticles;
	XMStoreFloat4x4(&pCbData->ViewProj, XMMatrixTranspose(viewProj));
	m_world = pCbData->World;
}

void Emitter::Distribute(CommandList* pCommandList, const RawBuffer* pCounter,
	const VertexBuffer* pVB, const IndexBuffer* pIB, uint32_t numIndices,
	float density, float scale)
{
	m_srvVertexBuffer = pVB->GetSRV();

	// Bind the descriptor heap.
	const auto descriptorHeap = m_descriptorTableLib->GetDescriptorHeap(CBV_SRV_UAV_HEAP);
	pCommandList->SetDescriptorHeaps(1, &descriptorHeap);

	// Set barriers
	ResourceBarrier barriers[2];
	// Promotion
	m_emitterBuffer->SetBarrier(barriers, ResourceState::UNORDERED_ACCESS);
	m_counter->SetBarrier(barriers, ResourceState::UNORDERED_ACCESS);

	// Clear counter
	uint32_t clear[4] = {};
	pCommandList->ClearUnorderedAccessViewUint(m_uavTables[UAV_TABLE_COUNTER],
		m_counter->GetUAV(), m_counter.get(), clear);

	distribute(pCommandList, pVB, pIB, numIndices, density, scale);

	// Set barriers
	auto numBarriers = m_emitterBuffer->SetBarrier(barriers, ResourceState::NON_PIXEL_SHADER_RESOURCE);
	numBarriers = m_counter->SetBarrier(barriers, ResourceState::COPY_SOURCE);
	pCommandList->Barrier(numBarriers, barriers);

	// Copy the counter for readback
	pCommandList->CopyResource(pCounter, m_counter.get());
}

void Emitter::EmitParticle(const CommandList* pCommandList, uint8_t frameIndex,
	uint32_t numParticles, const DescriptorTable& uavTable)
{
	//if (m_cbParticle.BaseSeed <= 0.0) return;

	// Set pipeline state
	pCommandList->SetComputePipelineLayout(m_pipelineLayouts[EMISSION]);
	pCommandList->SetPipelineState(m_pipelines[EMISSION]);

	// Set descriptor tables
	pCommandList->SetComputeRootConstantBufferView(0, m_cbPerObject.get(), m_cbPerObject->GetCBVOffset(frameIndex));
	pCommandList->SetComputeDescriptorTable(1, m_srvTable);
	pCommandList->SetComputeDescriptorTable(2, uavTable);

	pCommandList->Dispatch(XUSG_DIV_UP(numParticles, 64), 1, 1);
}

void Emitter::Render(const CommandList* pCommandList, uint8_t frameIndex,
	const Descriptor& rtv, const Descriptor* pDsv)
{
	pCommandList->OMSetRenderTargets(1, &rtv, pDsv);

	// Set pipeline state
	pCommandList->SetGraphicsPipelineLayout(m_pipelineLayouts[PARTICLE]);
	pCommandList->SetPipelineState(m_pipelines[PARTICLE]);

	pCommandList->IASetPrimitiveTopology(PrimitiveTopology::POINTLIST);

	// Set descriptor tables
	pCommandList->SetGraphicsRootConstantBufferView(0, m_cbPerObject.get(), m_cbPerObject->GetCBVOffset(frameIndex));
	pCommandList->SetGraphicsDescriptorTable(1, m_srvTable);
	pCommandList->SetGraphicsDescriptorTable(2, m_uavTables[UAV_TABLE_PARTICLE]);

	pCommandList->Draw(m_numParticles, 1, 0, 0);
}

void Emitter::RenderSPH(CommandList* pCommandList, uint8_t frameIndex, const Descriptor& rtv,
	const Descriptor* pDsv, const DescriptorTable& fluidDescriptorTable)
{
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
	pCommandList->SetGraphicsRootConstantBufferView(0, m_cbPerObject.get(), m_cbPerObject->GetCBVOffset(frameIndex));
	pCommandList->SetGraphicsDescriptorTable(1, m_srvTable);
	pCommandList->SetGraphicsDescriptorTable(2, m_uavTables[UAV_TABLE_PARTICLE1]);
	pCommandList->SetGraphicsDescriptorTable(3, fluidDescriptorTable);

	pCommandList->Draw(m_numParticles, 1, 0, 0);
}

void Emitter::RenderFHF(const CommandList* pCommandList, uint8_t frameIndex, const Descriptor& rtv,
	const Descriptor* pDsv, const DescriptorTable& fluidDescriptorTable)
{
	pCommandList->OMSetRenderTargets(1, &rtv, pDsv);

	// Set pipeline state
	pCommandList->SetGraphicsPipelineLayout(m_pipelineLayouts[PARTICLE_FHF]);
	pCommandList->SetPipelineState(m_pipelines[PARTICLE_FHF]);

	pCommandList->IASetPrimitiveTopology(PrimitiveTopology::POINTLIST);

	// Set descriptor tables
	pCommandList->SetGraphicsRootConstantBufferView(0, m_cbPerObject.get(), m_cbPerObject->GetCBVOffset(frameIndex));
	pCommandList->SetGraphicsDescriptorTable(1, m_srvTable);
	pCommandList->SetGraphicsDescriptorTable(2, m_uavTables[UAV_TABLE_PARTICLE]);
	pCommandList->SetGraphicsDescriptorTable(3, fluidDescriptorTable);

	pCommandList->Draw(m_numParticles, 1, 0, 0);
}

void Emitter::Visualize(const CommandList* pCommandList, const Descriptor& rtv,
	const Descriptor* pDsv, const XMFLOAT4X4& worldViewProj)
{
	pCommandList->OMSetRenderTargets(1, &rtv, pDsv);

	// Set pipeline state
	pCommandList->SetGraphicsPipelineLayout(m_pipelineLayouts[VISUALIZE]);
	pCommandList->SetPipelineState(m_pipelines[VISUALIZE]);

	pCommandList->IASetPrimitiveTopology(PrimitiveTopology::POINTLIST);

	// Set descriptor tables
	pCommandList->SetGraphics32BitConstants(0, XUSG_UINT32_SIZE_OF(XMFLOAT4X4), &worldViewProj);
	pCommandList->SetGraphicsDescriptorTable(1, m_srvTable);

	pCommandList->Draw(m_numEmitters, 1, 0, 0);
}

const StructuredBuffer::uptr* Emitter::GetParticleBuffers() const
{
	return m_particleBuffers;
}

bool Emitter::createPipelineLayouts()
{
	// Generate uniformized distribution
	{
		const auto pipelineLayout = Util::PipelineLayout::MakeUnique();
		pipelineLayout->SetConstants(0, XUSG_UINT32_SIZE_OF(XMFLOAT3X4), 0, 0, Shader::Stage::VS);
		pipelineLayout->SetRange(1, DescriptorType::UAV, 1, 0, 0, DescriptorFlag::DATA_STATIC_WHILE_SET_AT_EXECUTE);
		pipelineLayout->SetShaderStage(1, Shader::Stage::DS);
		XUSG_X_RETURN(m_pipelineLayouts[DISTRIBUTE], pipelineLayout->GetPipelineLayout(m_pipelineLayoutLib.get(),
			PipelineLayoutFlag::ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT, L"DistributionLayout"), false);
	}

	// Particle emission and integration
	{
		const auto pipelineLayout = Util::PipelineLayout::MakeUnique();
		pipelineLayout->SetRootCBV(0, 0, 0, Shader::Stage::VS);
		pipelineLayout->SetRange(1, DescriptorType::SRV, 2, 0, 0, DescriptorFlag::DATA_STATIC);
		pipelineLayout->SetRange(2, DescriptorType::UAV, 1, 0, 0, DescriptorFlag::DATA_STATIC_WHILE_SET_AT_EXECUTE);
		pipelineLayout->SetShaderStage(1, Shader::Stage::VS);
		pipelineLayout->SetShaderStage(2, Shader::Stage::VS);
		XUSG_X_RETURN(m_pipelineLayouts[PARTICLE], pipelineLayout->GetPipelineLayout(m_pipelineLayoutLib.get(),
			PipelineLayoutFlag::NONE, L"ParticleLayout"), false);
	}

	// Particle emission and integration for SPH
	{
		const auto pipelineLayout = Util::PipelineLayout::MakeUnique();
		pipelineLayout->SetRootCBV(0, 0, 0, Shader::Stage::VS);
		pipelineLayout->SetRange(1, DescriptorType::SRV, 2, 0, 0, DescriptorFlag::DATA_STATIC);
		pipelineLayout->SetRange(2, DescriptorType::UAV, 1, 0, 0, DescriptorFlag::DATA_STATIC_WHILE_SET_AT_EXECUTE);
		pipelineLayout->SetRange(3, DescriptorType::UAV, 2, 1, 0, DescriptorFlag::DATA_STATIC_WHILE_SET_AT_EXECUTE);
		pipelineLayout->SetRange(3, DescriptorType::SRV, 2, 2);
		pipelineLayout->SetShaderStage(1, Shader::Stage::VS);
		pipelineLayout->SetShaderStage(2, Shader::Stage::VS);
		pipelineLayout->SetShaderStage(3, Shader::Stage::VS);
		XUSG_X_RETURN(m_pipelineLayouts[PARTICLE_SPH], pipelineLayout->GetPipelineLayout(m_pipelineLayoutLib.get(),
			PipelineLayoutFlag::NONE, L"ParticleSPHLayout"), false);
	}

	// Particle emission and integration for fast hybrid fluid
	{
		const auto& sampler = m_descriptorTableLib->GetSampler(SamplerPreset::LINEAR_CLAMP);

		const auto pipelineLayout = Util::PipelineLayout::MakeUnique();
		pipelineLayout->SetRootCBV(0, 0, 0, Shader::Stage::VS);
		pipelineLayout->SetRange(1, DescriptorType::SRV, 2, 0, 0, DescriptorFlag::DATA_STATIC);
		pipelineLayout->SetRange(2, DescriptorType::UAV, 1, 0, 0, DescriptorFlag::DATA_STATIC_WHILE_SET_AT_EXECUTE);
		pipelineLayout->SetRange(3, DescriptorType::CBV, 1, 1, 0, DescriptorFlag::DATA_STATIC);
		pipelineLayout->SetRange(3, DescriptorType::SRV, 1, 2);
		pipelineLayout->SetRange(3, DescriptorType::UAV, 4, 1, 0, DescriptorFlag::DATA_STATIC_WHILE_SET_AT_EXECUTE);
		pipelineLayout->SetShaderStage(1, Shader::Stage::VS);
		pipelineLayout->SetShaderStage(2, Shader::Stage::VS);
		pipelineLayout->SetShaderStage(3, Shader::Stage::VS);
		pipelineLayout->SetStaticSamplers(&sampler, 1, 0, 0, Shader::Stage::VS);
		XUSG_X_RETURN(m_pipelineLayouts[PARTICLE_FHF], pipelineLayout->GetPipelineLayout(m_pipelineLayoutLib.get(),
			PipelineLayoutFlag::NONE, L"ParticleFHFLayout"), false);
	}

	// Particle emission
	{
		const auto pipelineLayout = Util::PipelineLayout::MakeUnique();
		pipelineLayout->SetRootCBV(0, 0, 0, Shader::Stage::CS);
		pipelineLayout->SetRange(1, DescriptorType::SRV, 2, 0, 0, DescriptorFlag::DATA_STATIC);
		pipelineLayout->SetRange(2, DescriptorType::UAV, 1, 0, 0,
			DescriptorFlag::DATA_STATIC_WHILE_SET_AT_EXECUTE | DescriptorFlag::DESCRIPTORS_VOLATILE);
		XUSG_X_RETURN(m_pipelineLayouts[EMISSION], pipelineLayout->GetPipelineLayout(m_pipelineLayoutLib.get(),
			PipelineLayoutFlag::NONE, L"EmissionLayout"), false);
	}

	// Show emitters
	{
		const auto pipelineLayout = Util::PipelineLayout::MakeUnique();
		pipelineLayout->SetConstants(0, XUSG_UINT32_SIZE_OF(XMFLOAT4X4), 0, 0, Shader::Stage::VS);
		pipelineLayout->SetRange(1, DescriptorType::SRV, 2, 0, 0, DescriptorFlag::DATA_STATIC);
		XUSG_X_RETURN(m_pipelineLayouts[VISUALIZE], pipelineLayout->GetPipelineLayout(m_pipelineLayoutLib.get(),
			PipelineLayoutFlag::NONE, L"VisualizationLayout"), false);
	}

	return true;
}

bool Emitter::createPipelines(const InputLayout* pInputLayout, Format rtFormat, Format dsFormat)
{
	auto vsIndex = 0u;
	auto hsIndex = 0u;
	auto dsIndex = 0u;
	auto psIndex = 0u;
	auto csIndex = 0u;

	// Generate uniform distribution
	{
		XUSG_N_RETURN(m_shaderLib->CreateShader(Shader::Stage::VS, vsIndex, L"VSDistribute.cso"), false);
		XUSG_N_RETURN(m_shaderLib->CreateShader(Shader::Stage::HS, hsIndex, L"HSDistribute.cso"), false);
		XUSG_N_RETURN(m_shaderLib->CreateShader(Shader::Stage::DS, dsIndex, L"DSDistribute.cso"), false);

		const auto state = Graphics::State::MakeUnique();
		state->IASetInputLayout(pInputLayout);
		state->SetPipelineLayout(m_pipelineLayouts[DISTRIBUTE]);
		state->SetShader(Shader::Stage::VS, m_shaderLib->GetShader(Shader::Stage::VS, vsIndex++));
		state->SetShader(Shader::Stage::HS, m_shaderLib->GetShader(Shader::Stage::HS, hsIndex++));
		state->SetShader(Shader::Stage::DS, m_shaderLib->GetShader(Shader::Stage::DS, dsIndex++));
		state->DSSetState(Graphics::DEPTH_STENCIL_NONE, m_graphicsPipelineLib.get());
		state->IASetPrimitiveTopologyType(PrimitiveTopologyType::PATCH);
		XUSG_X_RETURN(m_pipelines[DISTRIBUTE], state->GetPipeline(m_graphicsPipelineLib.get(), L"Distribution"), false);
	}

	// Particle emission and integration
	XUSG_N_RETURN(m_shaderLib->CreateShader(Shader::Stage::PS, psIndex, L"PSConstColor.cso"), false);
	{
		XUSG_N_RETURN(m_shaderLib->CreateShader(Shader::Stage::VS, vsIndex, L"VSParticle.cso"), false);

		const auto state = Graphics::State::MakeUnique();
		state->SetPipelineLayout(m_pipelineLayouts[PARTICLE]);
		state->SetShader(Shader::Stage::VS, m_shaderLib->GetShader(Shader::Stage::VS, vsIndex++));
		state->SetShader(Shader::Stage::PS, m_shaderLib->GetShader(Shader::Stage::PS, psIndex));
		state->IASetPrimitiveTopologyType(PrimitiveTopologyType::POINT);
		state->OMSetNumRenderTargets(1);
		state->OMSetRTVFormat(0, rtFormat);
		state->OMSetDSVFormat(dsFormat);
		XUSG_X_RETURN(m_pipelines[PARTICLE], state->GetPipeline(m_graphicsPipelineLib.get(), L"Particle"), false);
	}

	// Particle emission and integration for SPH
	{
		XUSG_N_RETURN(m_shaderLib->CreateShader(Shader::Stage::VS, vsIndex, L"VSParticleSPH.cso"), false);

		const auto state = Graphics::State::MakeUnique();
		state->SetPipelineLayout(m_pipelineLayouts[PARTICLE_SPH]);
		state->SetShader(Shader::Stage::VS, m_shaderLib->GetShader(Shader::Stage::VS, vsIndex++));
		state->SetShader(Shader::Stage::PS, m_shaderLib->GetShader(Shader::Stage::PS, psIndex));
		state->IASetPrimitiveTopologyType(PrimitiveTopologyType::POINT);
		state->OMSetNumRenderTargets(1);
		state->OMSetRTVFormat(0, rtFormat);
		state->OMSetDSVFormat(dsFormat);
		XUSG_X_RETURN(m_pipelines[PARTICLE_SPH], state->GetPipeline(m_graphicsPipelineLib.get(), L"ParticleSPH"), false);
	}

	// Particle emission and integration for fast hybrid fluid
	{
		XUSG_N_RETURN(m_shaderLib->CreateShader(Shader::Stage::VS, vsIndex, L"VSParticleFHF.cso"), false);

		const auto state = Graphics::State::MakeUnique();
		state->SetPipelineLayout(m_pipelineLayouts[PARTICLE_FHF]);
		state->SetShader(Shader::Stage::VS, m_shaderLib->GetShader(Shader::Stage::VS, vsIndex++));
		state->SetShader(Shader::Stage::PS, m_shaderLib->GetShader(Shader::Stage::PS, psIndex));
		state->IASetPrimitiveTopologyType(PrimitiveTopologyType::POINT);
		state->OMSetNumRenderTargets(1);
		state->OMSetRTVFormat(0, rtFormat);
		state->OMSetDSVFormat(dsFormat);
		XUSG_X_RETURN(m_pipelines[PARTICLE_FHF], state->GetPipeline(m_graphicsPipelineLib.get(), L"ParticleFHF"), false);
	}

	// Particle emission
	{
		XUSG_N_RETURN(m_shaderLib->CreateShader(Shader::Stage::CS, csIndex, L"CSEmit.cso"), false);

		const auto state = Compute::State::MakeUnique();
		state->SetPipelineLayout(m_pipelineLayouts[EMISSION]);
		state->SetShader(m_shaderLib->GetShader(Shader::Stage::CS, csIndex++));
		XUSG_X_RETURN(m_pipelines[EMISSION], state->GetPipeline(m_computePipelineLib.get(), L"Emission"), false);
	}

	// Show emitters
	{
		XUSG_N_RETURN(m_shaderLib->CreateShader(Shader::Stage::VS, vsIndex, L"VSShowEmitter.cso"), false);

		const auto state = Graphics::State::MakeUnique();
		state->SetPipelineLayout(m_pipelineLayouts[VISUALIZE]);
		state->SetShader(Shader::Stage::VS, m_shaderLib->GetShader(Shader::Stage::VS, vsIndex++));
		state->SetShader(Shader::Stage::PS, m_shaderLib->GetShader(Shader::Stage::PS, psIndex++));
		state->IASetPrimitiveTopologyType(PrimitiveTopologyType::POINT);
		state->OMSetNumRenderTargets(1);
		state->OMSetRTVFormat(0, rtFormat);
		state->OMSetDSVFormat(dsFormat);
		XUSG_X_RETURN(m_pipelines[VISUALIZE], state->GetPipeline(m_graphicsPipelineLib.get(), L"Visualization"), false);
	}

	return true;
}

bool Emitter::createDescriptorTables()
{
	// Create UAV tables
	{
		const auto descriptorTable = Util::DescriptorTable::MakeUnique();
		descriptorTable->SetDescriptors(0, 1, &m_emitterBuffer->GetUAV());
		XUSG_X_RETURN(m_uavTables[UAV_TABLE_EMITTER], descriptorTable->GetCbvSrvUavTable(m_descriptorTableLib.get()), false);
	}

	{
		const auto descriptorTable = Util::DescriptorTable::MakeUnique();
		descriptorTable->SetDescriptors(0, 1, &m_counter->GetUAV());
		XUSG_X_RETURN(m_uavTables[UAV_TABLE_COUNTER], descriptorTable->GetCbvSrvUavTable(m_descriptorTableLib.get()), false);
	}

	for (uint8_t i = 0; i < 2; ++i)
	{
		const auto descriptorTable = Util::DescriptorTable::MakeUnique();
		descriptorTable->SetDescriptors(0, 1, &m_particleBuffers[i]->GetUAV());
		XUSG_X_RETURN(m_uavTables[UAV_TABLE_PARTICLE + i], descriptorTable->GetCbvSrvUavTable(m_descriptorTableLib.get()), false);
	}

	return true;
}

void Emitter::distribute(const CommandList* pCommandList, const VertexBuffer* pVB,
	const IndexBuffer* pIB, uint32_t numIndices, float density, float scale)
{
	// Set pipeline state
	pCommandList->SetGraphicsPipelineLayout(m_pipelineLayouts[DISTRIBUTE]);
	pCommandList->SetPipelineState(m_pipelines[DISTRIBUTE]);

	pCommandList->IASetPrimitiveTopology(PrimitiveTopology::CONTROL_POINT3_PATCHLIST);

	// Set descriptor tables
	scale *= density;
	XMFLOAT3X4 transform;
	XMStoreFloat3x4(&transform, XMMatrixScaling(scale, scale, scale));
	pCommandList->SetGraphics32BitConstants(0, XUSG_UINT32_SIZE_OF(XMFLOAT3X4), &transform);
	pCommandList->SetGraphicsDescriptorTable(1, m_uavTables[UAV_TABLE_EMITTER]);

	pCommandList->IASetVertexBuffers(0, 1, &pVB->GetVBV());
	pCommandList->IASetIndexBuffer(pIB->GetIBV());

	pCommandList->DrawIndexed(numIndices, 1, 0, 0, 0);
}
