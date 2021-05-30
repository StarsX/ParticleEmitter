//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#include "FluidSPH.h"
#include "SharedConst.h"

using namespace std;
using namespace DirectX;
using namespace XUSG;

const uint32_t g_gridBufferSize = GRID_SIZE_SPH * GRID_SIZE_SPH * GRID_SIZE_SPH + 1;

FluidSPH::FluidSPH(const Device::sptr& device) :
	m_device(device)
{
	m_shaderPool = ShaderPool::MakeUnique();
	m_computePipelineCache = Compute::PipelineCache::MakeUnique(device.get());
	m_pipelineLayoutCache = PipelineLayoutCache::MakeUnique(device.get());
	m_prefixSumUtil.SetDevice(device);
}

FluidSPH::~FluidSPH()
{
}

bool FluidSPH::Init(CommandList* pCommandList, uint32_t numParticles,
	const DescriptorTableCache::sptr& descriptorTableCache,
	vector<Resource::uptr>& uploaders, const StructuredBuffer::uptr* pParticleBuffers)
{
	m_numParticles = numParticles;
	m_descriptorTableCache = descriptorTableCache;
	m_pParticleBuffers = pParticleBuffers;

	// Create resources
	m_gridBuffer = TypedBuffer::MakeUnique();
	N_RETURN(m_gridBuffer->Create(m_device.get(), g_gridBufferSize,
		sizeof(uint32_t), Format::R32_UINT, ResourceFlag::ALLOW_UNORDERED_ACCESS,
		MemoryType::DEFAULT, 1, nullptr, 1, nullptr, L"GridBuffer"), false);

	m_offsetBuffer = TypedBuffer::MakeUnique();
	N_RETURN(m_offsetBuffer->Create(m_device.get(), numParticles, sizeof(uint32_t),
		Format::R32_UINT, ResourceFlag::ALLOW_UNORDERED_ACCESS, MemoryType::DEFAULT,
		1, nullptr, 1, nullptr, L"OffsetBuffer"), false);

	m_densityBuffer = TypedBuffer::MakeUnique();
	N_RETURN(m_densityBuffer->Create(m_device.get(), numParticles, sizeof(uint16_t),
		Format::R16_FLOAT, ResourceFlag::ALLOW_UNORDERED_ACCESS, MemoryType::DEFAULT,
		1, nullptr, 1, nullptr, L"DensityBuffer"), false);

	m_forceBuffer = TypedBuffer::MakeUnique();
	N_RETURN(m_forceBuffer->Create(m_device.get(), numParticles, sizeof(uint16_t[4]),
		Format::R16G16B16A16_FLOAT, ResourceFlag::ALLOW_UNORDERED_ACCESS, MemoryType::DEFAULT,
		1, nullptr, 1, nullptr, L"ForceBuffer"), false);

	// Create constant buffer
	m_cbSimulation = ConstantBuffer::MakeUnique();
	N_RETURN(m_cbSimulation->Create(m_device.get(), sizeof(CBSimulation), 1,
		nullptr, MemoryType::DEFAULT, L"CBSimulationSPH"), false);
	uploaders.emplace_back(Resource::MakeUnique());
	CBSimulation cbSimulation;
	{
		const XMFLOAT4 boundary(BOUNDARY_SPH);
		cbSimulation.SmoothRadius = boundary.w * 2.0f / GRID_SIZE_SPH;
		cbSimulation.PressureStiffness = 200.0f;
		cbSimulation.RestDensity = 1000.0f;

		const float mass = 1310.72f / numParticles;
		const float viscosity = 0.1f;
		cbSimulation.NumParticles = numParticles;
		cbSimulation.DensityCoef = mass * 315.0f / (64.0f * XM_PI * pow(cbSimulation.SmoothRadius, 9.0f));
		cbSimulation.PressureGradCoef = mass * -45.0f / (XM_PI * pow(cbSimulation.SmoothRadius, 6.0f));
		cbSimulation.ViscosityLaplaceCoef = mass * viscosity * 45.0f / (XM_PI * pow(cbSimulation.SmoothRadius, 6.0f));
	}
	m_cbSimulation->Upload(pCommandList, uploaders.back().get(),
		&cbSimulation, sizeof(CBSimulation));

	// Set prefix sum
	m_prefixSumUtil.SetPrefixSum(pCommandList, g_gridBufferSize > 4096,
		m_descriptorTableCache, m_gridBuffer.get());

	// Create pipelines
	N_RETURN(createPipelineLayouts(), false);
	N_RETURN(createPipelines(), false);
	N_RETURN(createDescriptorTables(), false);

	return true;
}

void FluidSPH::UpdateFrame()
{
	// Set barriers with promotions
	ResourceBarrier barrier;
	m_offsetBuffer->SetBarrier(&barrier, ResourceState::UNORDERED_ACCESS);
	m_forceBuffer->SetBarrier(&barrier, ResourceState::NON_PIXEL_SHADER_RESOURCE);
}

void FluidSPH::Simulate(const CommandList* pCommandList)
{
	ResourceBarrier barrier;
	auto numBarriers = m_gridBuffer->SetBarrier(&barrier, ResourceState::UNORDERED_ACCESS);
	pCommandList->Barrier(numBarriers, &barrier);

	// Prefix sum grid
	m_prefixSumUtil.PrefixSum(pCommandList, g_gridBufferSize);

	// Simulation steps
	rearrange(pCommandList); // Sort particles
	density(pCommandList);
	force(pCommandList);

	// Clear grid
	const uint32_t clear[4] = {};
	numBarriers = m_gridBuffer->SetBarrier(&barrier, ResourceState::UNORDERED_ACCESS);
	pCommandList->Barrier(numBarriers, &barrier);
	pCommandList->ClearUnorderedAccessViewUint(m_uavSrvTable, m_gridBuffer->GetUAV(),
		m_gridBuffer.get(), clear);
}

const DescriptorTable& FluidSPH::GetDescriptorTable() const
{
	return m_uavSrvTable;
}

bool FluidSPH::createPipelineLayouts()
{
	// Rearrangement
	{
		const auto pipelineLayout = Util::PipelineLayout::MakeUnique();
		pipelineLayout->SetRange(0, DescriptorType::SRV, 3, 0, 0, DescriptorFlag::DESCRIPTORS_VOLATILE);
		pipelineLayout->SetRange(1, DescriptorType::UAV, 1, 0, 0,
			DescriptorFlag::DATA_STATIC_WHILE_SET_AT_EXECUTE | DescriptorFlag::DESCRIPTORS_VOLATILE);
		X_RETURN(m_pipelineLayouts[REARRANGE], pipelineLayout->GetPipelineLayout(m_pipelineLayoutCache.get(),
			PipelineLayoutFlag::NONE, L"RearrangementLayout"), false);
	}

	// Density
	{
		const auto pipelineLayout = Util::PipelineLayout::MakeUnique();
		pipelineLayout->SetRootCBV(0, 0, 0, Shader::Stage::CS);
		pipelineLayout->SetRange(1, DescriptorType::SRV, 2, 0, 0, DescriptorFlag::DESCRIPTORS_VOLATILE);
		pipelineLayout->SetRange(2, DescriptorType::UAV, 1, 0, 0,
			DescriptorFlag::DATA_STATIC_WHILE_SET_AT_EXECUTE | DescriptorFlag::DESCRIPTORS_VOLATILE);
		X_RETURN(m_pipelineLayouts[DENSITY], pipelineLayout->GetPipelineLayout(m_pipelineLayoutCache.get(),
			PipelineLayoutFlag::NONE, L"DensityLayout"), false);
	}

	// Force
	{
		const auto pipelineLayout = Util::PipelineLayout::MakeUnique();
		pipelineLayout->SetRootCBV(0, 0, 0, Shader::Stage::CS);
		pipelineLayout->SetRange(1, DescriptorType::SRV, 3, 0, 0, DescriptorFlag::DESCRIPTORS_VOLATILE);
		pipelineLayout->SetRange(2, DescriptorType::UAV, 1, 0, 0,
			DescriptorFlag::DATA_STATIC_WHILE_SET_AT_EXECUTE | DescriptorFlag::DESCRIPTORS_VOLATILE);
		X_RETURN(m_pipelineLayouts[FORCE], pipelineLayout->GetPipelineLayout(m_pipelineLayoutCache.get(),
			PipelineLayoutFlag::NONE, L"ForceLayout"), false);
	}

	return true;
}

bool FluidSPH::createPipelines()
{
	auto csIndex = 0u;

	// Rearrangement
	{
		N_RETURN(m_shaderPool->CreateShader(Shader::Stage::CS, csIndex, L"CSRearrange.cso"), false);

		const auto state = Compute::State::MakeUnique();
		state->SetPipelineLayout(m_pipelineLayouts[REARRANGE]);
		state->SetShader(m_shaderPool->GetShader(Shader::Stage::CS, csIndex++));
		X_RETURN(m_pipelines[REARRANGE], state->GetPipeline(m_computePipelineCache.get(), L"Rearrangement"), false);
	}

	// Density
	{
		N_RETURN(m_shaderPool->CreateShader(Shader::Stage::CS, csIndex, L"CSDensitySPH.cso"), false);

		const auto state = Compute::State::MakeUnique();
		state->SetPipelineLayout(m_pipelineLayouts[DENSITY]);
		state->SetShader(m_shaderPool->GetShader(Shader::Stage::CS, csIndex++));
		X_RETURN(m_pipelines[DENSITY], state->GetPipeline(m_computePipelineCache.get(), L"Density"), false);
	}

	// Force
	{
		N_RETURN(m_shaderPool->CreateShader(Shader::Stage::CS, csIndex, L"CSForceSPH.cso"), false);

		const auto state = Compute::State::MakeUnique();
		state->SetPipelineLayout(m_pipelineLayouts[FORCE]);
		state->SetShader(m_shaderPool->GetShader(Shader::Stage::CS, csIndex++));
		X_RETURN(m_pipelines[FORCE], state->GetPipeline(m_computePipelineCache.get(), L"Force"), false);
	}

	return true;
}

bool FluidSPH::createDescriptorTables()
{
	// Create UAV and SRV table for integration
	{
		const auto descriptorTable = Util::DescriptorTable::MakeUnique();
		const Descriptor descriptors[] =
		{
			m_gridBuffer->GetUAV(),
			m_offsetBuffer->GetUAV(),
			m_pParticleBuffers[REARRANGED]->GetSRV(),
			m_forceBuffer->GetSRV()
		};
		descriptorTable->SetDescriptors(0, static_cast<uint32_t>(size(descriptors)), descriptors);
		X_RETURN(m_uavSrvTable, descriptorTable->GetCbvSrvUavTable(m_descriptorTableCache.get()), false);
	}

	// Create SRV tables
	{
		const auto descriptorTable = Util::DescriptorTable::MakeUnique();
		const Descriptor descriptors[] =
		{
			m_pParticleBuffers[INTEGRATED]->GetSRV(),
			m_gridBuffer->GetSRV(),
			m_offsetBuffer->GetSRV()
		};
		descriptorTable->SetDescriptors(0, static_cast<uint32_t>(size(descriptors)), descriptors);
		X_RETURN(m_srvTables[SRV_TABLE_REARRANGLE], descriptorTable->GetCbvSrvUavTable(m_descriptorTableCache.get()), false);
	}

	{
		const auto descriptorTable = Util::DescriptorTable::MakeUnique();
		const Descriptor descriptors[] =
		{
			m_pParticleBuffers[REARRANGED]->GetSRV(),
			m_gridBuffer->GetSRV(),
			m_densityBuffer->GetSRV()
		};
		descriptorTable->SetDescriptors(0, static_cast<uint32_t>(size(descriptors)), descriptors);
		X_RETURN(m_srvTables[SRV_TABLE_SPH], descriptorTable->GetCbvSrvUavTable(m_descriptorTableCache.get()), false);
	}

	// Create UAV tables
	{
		const auto descriptorTable = Util::DescriptorTable::MakeUnique();
		descriptorTable->SetDescriptors(0, 1, &m_pParticleBuffers[REARRANGED]->GetUAV());
		X_RETURN(m_uavTables[UAV_TABLE_PARTICLE], descriptorTable->GetCbvSrvUavTable(m_descriptorTableCache.get()), false);
	}

	{
		const auto descriptorTable = Util::DescriptorTable::MakeUnique();
		descriptorTable->SetDescriptors(0, 1, &m_densityBuffer->GetUAV());
		X_RETURN(m_uavTables[UAV_TABLE_DENSITY], descriptorTable->GetCbvSrvUavTable(m_descriptorTableCache.get()), false);
	}

	{
		const auto descriptorTable = Util::DescriptorTable::MakeUnique();
		descriptorTable->SetDescriptors(0, 1, &m_forceBuffer->GetUAV());
		X_RETURN(m_uavTables[UAV_TABLE_FORCE], descriptorTable->GetCbvSrvUavTable(m_descriptorTableCache.get()), false);
	}

	return true;
}

void FluidSPH::rearrange(const CommandList* pCommandList)
{
	// Set barriers
	ResourceBarrier barriers[4];
	auto numBarriers = m_pParticleBuffers[REARRANGED]->SetBarrier(barriers, ResourceState::UNORDERED_ACCESS);
	numBarriers = m_pParticleBuffers[INTEGRATED]->SetBarrier(barriers, ResourceState::NON_PIXEL_SHADER_RESOURCE, numBarriers);
	numBarriers = m_gridBuffer->SetBarrier(barriers, ResourceState::NON_PIXEL_SHADER_RESOURCE, numBarriers);
	numBarriers = m_offsetBuffer->SetBarrier(barriers, ResourceState::NON_PIXEL_SHADER_RESOURCE, numBarriers);
	pCommandList->Barrier(numBarriers, barriers);

	// Set pipeline state
	pCommandList->SetComputePipelineLayout(m_pipelineLayouts[REARRANGE]);
	pCommandList->SetPipelineState(m_pipelines[REARRANGE]);

	// Set descriptor tables
	pCommandList->SetComputeDescriptorTable(0, m_srvTables[SRV_TABLE_REARRANGLE]);
	pCommandList->SetComputeDescriptorTable(1, m_uavTables[UAV_TABLE_PARTICLE]);

	pCommandList->Dispatch(DIV_UP(m_numParticles, 64), 1, 1);
}

void FluidSPH::density(const CommandList* pCommandList)
{
	// Set barriers
	ResourceBarrier barriers[2];
	auto numBarriers = m_densityBuffer->SetBarrier(barriers, ResourceState::UNORDERED_ACCESS);
	numBarriers = m_pParticleBuffers[REARRANGED]->SetBarrier(barriers, ResourceState::NON_PIXEL_SHADER_RESOURCE, numBarriers);
	pCommandList->Barrier(numBarriers, barriers);

	// Set pipeline state
	pCommandList->SetComputePipelineLayout(m_pipelineLayouts[DENSITY]);
	pCommandList->SetPipelineState(m_pipelines[DENSITY]);

	// Set descriptor tables
	pCommandList->SetComputeRootConstantBufferView(0, m_cbSimulation.get());
	pCommandList->SetComputeDescriptorTable(1, m_srvTables[SRV_TABLE_SPH]);
	pCommandList->SetComputeDescriptorTable(2, m_uavTables[UAV_TABLE_DENSITY]);

	pCommandList->Dispatch(DIV_UP(m_numParticles, 64), 1, 1);
}

void FluidSPH::force(const CommandList* pCommandList)
{
	// Set barriers
	ResourceBarrier barriers[2];
	auto numBarriers = m_forceBuffer->SetBarrier(barriers, ResourceState::UNORDERED_ACCESS);
	numBarriers = m_densityBuffer->SetBarrier(barriers, ResourceState::NON_PIXEL_SHADER_RESOURCE, numBarriers);
	pCommandList->Barrier(numBarriers, barriers);

	// Set pipeline state
	pCommandList->SetComputePipelineLayout(m_pipelineLayouts[FORCE]);
	pCommandList->SetPipelineState(m_pipelines[FORCE]);

	// Set descriptor tables
	pCommandList->SetComputeRootConstantBufferView(0, m_cbSimulation.get());
	pCommandList->SetComputeDescriptorTable(1, m_srvTables[SRV_TABLE_SPH]);
	pCommandList->SetComputeDescriptorTable(2, m_uavTables[UAV_TABLE_FORCE]);

	pCommandList->Dispatch(DIV_UP(m_numParticles, 64), 1, 1);
}
