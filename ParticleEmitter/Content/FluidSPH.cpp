//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#include "FluidSPH.h"
#include "SharedConst.h"

using namespace std;
using namespace DirectX;
using namespace XUSG;

const uint32_t g_gridBufferSize = GRID_SIZE * GRID_SIZE * GRID_SIZE + 1;

FluidSPH::FluidSPH(const Device& device) :
	m_device(device)
{
	m_computePipelineCache.SetDevice(device);
	m_pipelineLayoutCache.SetDevice(device);
	m_prefixSumUtil.SetDevice(device);

	const XMFLOAT4 boundary(BOUNDARY);
	m_cbSimulation.SmoothRadius = 1.0f / boundary.w;
	m_cbSimulation.PressureStiffness = 200.0f;
	m_cbSimulation.RestDensity = 1000.0f;
}

FluidSPH::~FluidSPH()
{
}

bool FluidSPH::Init(const CommandList& commandList, uint32_t numParticles,
	shared_ptr<DescriptorTableCache> descriptorTableCache,
	StructuredBuffer* pParticleBuffers)
{
	m_cbSimulation.NumParticles = numParticles;
	m_descriptorTableCache = descriptorTableCache;
	m_pParticleBuffers = pParticleBuffers;

	const float mass = 5243.0f / numParticles;
	const float viscosity = 0.1f;
	m_cbSimulation.DensityCoef = mass * 315.0f / (64.0f * XM_PI * pow(m_cbSimulation.SmoothRadius, 9.0f));
	m_cbSimulation.PressureGradCoef = mass * -45.0f / (XM_PI * pow(m_cbSimulation.SmoothRadius, 6.0f));
	m_cbSimulation.ViscosityLaplaceCoef = mass * viscosity * 45.0f / (XM_PI * pow(m_cbSimulation.SmoothRadius, 6.0f));

	// Create resources
	N_RETURN(m_gridBuffer.Create(m_device, g_gridBufferSize,
		sizeof(uint32_t), Format::R32_UINT, ResourceFlag::ALLOW_UNORDERED_ACCESS,
		MemoryType::DEFAULT, 1, nullptr, 1, nullptr, L"GridBuffer"), false);

	N_RETURN(m_offsetBuffer.Create(m_device, numParticles, sizeof(uint32_t),
		Format::R32_UINT, ResourceFlag::ALLOW_UNORDERED_ACCESS, MemoryType::DEFAULT,
		1, nullptr, 1, nullptr, L"OffsetBuffer"), false);
	N_RETURN(m_densityBuffer.Create(m_device, numParticles, sizeof(uint16_t),
		Format::R16_FLOAT, ResourceFlag::ALLOW_UNORDERED_ACCESS, MemoryType::DEFAULT,
		1, nullptr, 1, nullptr, L"DensityBuffer"), false);
	N_RETURN(m_forceBuffer.Create(m_device, numParticles, sizeof(uint16_t[4]),
		Format::R16G16B16A16_FLOAT, ResourceFlag::ALLOW_UNORDERED_ACCESS, MemoryType::DEFAULT,
		1, nullptr, 1, nullptr, L"ForceBuffer"), false);

	// Set prefix sum
	m_prefixSumUtil.SetPrefixSum(commandList, g_gridBufferSize > 4096,
		m_descriptorTableCache, &m_gridBuffer);

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
	m_offsetBuffer.SetBarrier(&barrier, ResourceState::UNORDERED_ACCESS);
	m_forceBuffer.SetBarrier(&barrier, ResourceState::NON_PIXEL_SHADER_RESOURCE);
}

void FluidSPH::Simulate(const CommandList& commandList)
{
	ResourceBarrier barrier;
	auto numBarriers = m_gridBuffer.SetBarrier(&barrier, ResourceState::UNORDERED_ACCESS);
	commandList.Barrier(numBarriers, &barrier);

	// Prefix sum grid
	m_prefixSumUtil.PrefixSum(commandList, g_gridBufferSize);

	// Simulation steps
	rearrange(commandList); // Sort particles
	density(commandList);
	force(commandList);

	// Clear grid
	const uint32_t clear[4] = {};
	numBarriers = m_gridBuffer.SetBarrier(&barrier, ResourceState::UNORDERED_ACCESS);
	commandList.Barrier(numBarriers, &barrier);
	commandList.ClearUnorderedAccessViewUint(m_uavSrvTable, m_gridBuffer.GetUAV(),
		m_gridBuffer.GetResource(), clear);
}

const DescriptorTable& FluidSPH::GetBuildGridDescriptorTable() const
{
	return m_uavSrvTable;
}

bool FluidSPH::createPipelineLayouts()
{
	// Rearrangement
	{
		Util::PipelineLayout pipelineLayout;
		pipelineLayout.SetRange(0, DescriptorType::SRV, 3, 0);
		pipelineLayout.SetRange(1, DescriptorType::UAV, 1, 0);
		X_RETURN(m_pipelineLayouts[REARRANGE], pipelineLayout.GetPipelineLayout(m_pipelineLayoutCache,
			PipelineLayoutFlag::NONE, L"RearrangementLayout"), false);
	}

	// Density
	{
		Util::PipelineLayout pipelineLayout;
		pipelineLayout.SetConstants(0, SizeOfInUint32(CBSimulation), 0, 0, Shader::Stage::CS);
		pipelineLayout.SetRange(1, DescriptorType::SRV, 2, 0);
		pipelineLayout.SetRange(2, DescriptorType::UAV, 1, 0);
		X_RETURN(m_pipelineLayouts[DENSITY], pipelineLayout.GetPipelineLayout(m_pipelineLayoutCache,
			PipelineLayoutFlag::NONE, L"DensityLayout"), false);
	}

	// Force
	{
		Util::PipelineLayout pipelineLayout;
		pipelineLayout.SetConstants(0, SizeOfInUint32(CBSimulation), 0, 0, Shader::Stage::CS);
		pipelineLayout.SetRange(1, DescriptorType::SRV, 3, 0);
		pipelineLayout.SetRange(2, DescriptorType::UAV, 1, 0);
		X_RETURN(m_pipelineLayouts[FORCE], pipelineLayout.GetPipelineLayout(m_pipelineLayoutCache,
			PipelineLayoutFlag::NONE, L"ForceLayout"), false);
	}

	return true;
}

bool FluidSPH::createPipelines()
{
	auto csIndex = 0u;

	// Rearrangement
	{
		N_RETURN(m_shaderPool.CreateShader(Shader::Stage::CS, csIndex, L"CSRearrange.cso"), false);

		Compute::State state;
		state.SetPipelineLayout(m_pipelineLayouts[REARRANGE]);
		state.SetShader(m_shaderPool.GetShader(Shader::Stage::CS, csIndex++));
		X_RETURN(m_pipelines[REARRANGE], state.GetPipeline(m_computePipelineCache, L"Rearrangement"), false);
	}

	// Density
	{
		N_RETURN(m_shaderPool.CreateShader(Shader::Stage::CS, csIndex, L"CSDensitySPH.cso"), false);

		Compute::State state;
		state.SetPipelineLayout(m_pipelineLayouts[DENSITY]);
		state.SetShader(m_shaderPool.GetShader(Shader::Stage::CS, csIndex++));
		X_RETURN(m_pipelines[DENSITY], state.GetPipeline(m_computePipelineCache, L"Density"), false);
	}

	// Force
	{
		N_RETURN(m_shaderPool.CreateShader(Shader::Stage::CS, csIndex, L"CSForceSPH.cso"), false);

		Compute::State state;
		state.SetPipelineLayout(m_pipelineLayouts[FORCE]);
		state.SetShader(m_shaderPool.GetShader(Shader::Stage::CS, csIndex++));
		X_RETURN(m_pipelines[FORCE], state.GetPipeline(m_computePipelineCache, L"Force"), false);
	}

	return true;
}

bool FluidSPH::createDescriptorTables()
{
	// Create UAV and SRV table for integration
	{
		Util::DescriptorTable uavSrvTable;
		const Descriptor descriptors[] =
		{
			m_gridBuffer.GetUAV(),
			m_offsetBuffer.GetUAV(),
			m_pParticleBuffers[REARRANGED].GetSRV(),
			m_forceBuffer.GetSRV()
		};
		uavSrvTable.SetDescriptors(0, static_cast<uint32_t>(size(descriptors)), descriptors);
		X_RETURN(m_uavSrvTable, uavSrvTable.GetCbvSrvUavTable(*m_descriptorTableCache), false);
	}

	// Create SRV tables
	{
		Util::DescriptorTable srvTable;
		const Descriptor srvs[] =
		{
			m_pParticleBuffers[INTEGRATED].GetSRV(),
			m_gridBuffer.GetSRV(),
			m_offsetBuffer.GetSRV()
		};
		srvTable.SetDescriptors(0, static_cast<uint32_t>(size(srvs)), srvs);
		X_RETURN(m_srvTables[SRV_TABLE_REARRANGLE], srvTable.GetCbvSrvUavTable(*m_descriptorTableCache), false);
	}

	{
		Util::DescriptorTable srvTable;
		const Descriptor srvs[] =
		{
			m_pParticleBuffers[REARRANGED].GetSRV(),
			m_gridBuffer.GetSRV(),
			m_densityBuffer.GetSRV()
		};
		srvTable.SetDescriptors(0, static_cast<uint32_t>(size(srvs)), srvs);
		X_RETURN(m_srvTables[SRV_TABLE_SPH], srvTable.GetCbvSrvUavTable(*m_descriptorTableCache), false);
	}

	// Create UAV tables
	{
		Util::DescriptorTable uavTable;
		uavTable.SetDescriptors(0, 1, &m_pParticleBuffers[REARRANGED].GetUAV());
		X_RETURN(m_uavTables[UAV_TABLE_PARTICLE], uavTable.GetCbvSrvUavTable(*m_descriptorTableCache), false);
	}

	{
		Util::DescriptorTable uavTable;
		uavTable.SetDescriptors(0, 1, &m_densityBuffer.GetUAV());
		X_RETURN(m_uavTables[UAV_TABLE_DENSITY], uavTable.GetCbvSrvUavTable(*m_descriptorTableCache), false);
	}

	{
		Util::DescriptorTable uavTable;
		uavTable.SetDescriptors(0, 1, &m_forceBuffer.GetUAV());
		X_RETURN(m_uavTables[UAV_TABLE_FORCE], uavTable.GetCbvSrvUavTable(*m_descriptorTableCache), false);
	}

	return true;
}

void FluidSPH::rearrange(const CommandList& commandList)
{
	// Set barriers
	ResourceBarrier barriers[4];
	auto numBarriers = m_pParticleBuffers[REARRANGED].SetBarrier(barriers, ResourceState::UNORDERED_ACCESS);
	numBarriers = m_pParticleBuffers[INTEGRATED].SetBarrier(barriers, ResourceState::NON_PIXEL_SHADER_RESOURCE, numBarriers);
	numBarriers = m_gridBuffer.SetBarrier(barriers, ResourceState::NON_PIXEL_SHADER_RESOURCE, numBarriers);
	numBarriers = m_offsetBuffer.SetBarrier(barriers, ResourceState::NON_PIXEL_SHADER_RESOURCE, numBarriers);
	commandList.Barrier(numBarriers, barriers);

	// Set pipeline state
	commandList.SetComputePipelineLayout(m_pipelineLayouts[REARRANGE]);
	commandList.SetPipelineState(m_pipelines[REARRANGE]);

	// Set descriptor tables
	commandList.SetComputeDescriptorTable(0, m_srvTables[SRV_TABLE_REARRANGLE]);
	commandList.SetComputeDescriptorTable(1, m_uavTables[UAV_TABLE_PARTICLE]);

	commandList.Dispatch(DIV_UP(m_cbSimulation.NumParticles, 64), 1, 1);
}

void FluidSPH::density(const CommandList& commandList)
{
	// Set barriers
	ResourceBarrier barriers[2];
	auto numBarriers = m_densityBuffer.SetBarrier(barriers, ResourceState::UNORDERED_ACCESS);
	numBarriers = m_pParticleBuffers[REARRANGED].SetBarrier(barriers, ResourceState::NON_PIXEL_SHADER_RESOURCE, numBarriers);
	commandList.Barrier(numBarriers, barriers);

	// Set pipeline state
	commandList.SetComputePipelineLayout(m_pipelineLayouts[DENSITY]);
	commandList.SetPipelineState(m_pipelines[DENSITY]);

	// Set descriptor tables
	commandList.SetCompute32BitConstants(0, SizeOfInUint32(m_cbSimulation), &m_cbSimulation);
	commandList.SetComputeDescriptorTable(1, m_srvTables[SRV_TABLE_SPH]);
	commandList.SetComputeDescriptorTable(2, m_uavTables[UAV_TABLE_DENSITY]);

	commandList.Dispatch(DIV_UP(m_cbSimulation.NumParticles, 64), 1, 1);
}

void FluidSPH::force(const CommandList& commandList)
{
	// Set barriers
	ResourceBarrier barriers[2];
	auto numBarriers = m_forceBuffer.SetBarrier(barriers, ResourceState::UNORDERED_ACCESS);
	numBarriers = m_densityBuffer.SetBarrier(barriers, ResourceState::NON_PIXEL_SHADER_RESOURCE, numBarriers);
	commandList.Barrier(numBarriers, barriers);

	// Set pipeline state
	commandList.SetComputePipelineLayout(m_pipelineLayouts[FORCE]);
	commandList.SetPipelineState(m_pipelines[FORCE]);

	// Set descriptor tables
	commandList.SetCompute32BitConstants(0, SizeOfInUint32(m_cbSimulation), &m_cbSimulation);
	commandList.SetComputeDescriptorTable(1, m_srvTables[SRV_TABLE_SPH]);
	commandList.SetComputeDescriptorTable(2, m_uavTables[UAV_TABLE_FORCE]);

	commandList.Dispatch(DIV_UP(m_cbSimulation.NumParticles, 64), 1, 1);
}
