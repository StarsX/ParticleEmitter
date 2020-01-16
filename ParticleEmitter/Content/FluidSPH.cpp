//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#include "FluidSPH.h"

#define GRID_SIZE 32

using namespace std;
using namespace XUSG;

FluidSPH::FluidSPH(const Device& device) :
	m_device(device)
{
	m_computePipelineCache.SetDevice(device);
	m_pipelineLayoutCache.SetDevice(device);
	m_prefixSumUtil.SetDevice(device);
}

FluidSPH::~FluidSPH()
{
}

bool FluidSPH::Init(const CommandList& commandList, uint32_t numParticles,
	shared_ptr<DescriptorTableCache> descriptorTableCache,
	const StructuredBuffer& sortedParticles, const Descriptor& particleSRV)
{
	m_cbSimulation.NumParticles = numParticles;
	m_descriptorTableCache = descriptorTableCache;

	// Create resources
	N_RETURN(m_gridBuffer.Create(m_device, GRID_SIZE * GRID_SIZE * GRID_SIZE,
		sizeof(uint32_t), Format::R32_UINT, ResourceFlag::ALLOW_UNORDERED_ACCESS,
		MemoryType::DEFAULT, 1, nullptr, 1, nullptr, L"GridBuffer"), false);

	N_RETURN(m_offsetBuffer.Create(m_device, numParticles, sizeof(uint32_t),
		Format::R32_UINT, ResourceFlag::ALLOW_UNORDERED_ACCESS, MemoryType::DEFAULT,
		1, nullptr, 1, nullptr, L"OffsetBuffer"), false);
	N_RETURN(m_densityBuffer.Create(m_device, numParticles, sizeof(uint32_t),
		Format::R32_UINT, ResourceFlag::ALLOW_UNORDERED_ACCESS, MemoryType::DEFAULT,
		1, nullptr, 1, nullptr, L"DensityBuffer"), false);
	N_RETURN(m_forceBuffer.Create(m_device, numParticles, sizeof(uint32_t),
		Format::R32_UINT, ResourceFlag::ALLOW_UNORDERED_ACCESS, MemoryType::DEFAULT,
		1, nullptr, 1, nullptr, L"ForceBuffer"), false);

	// Set prefix sum
	m_prefixSumUtil.SetPrefixSum(commandList, m_descriptorTableCache, &m_gridBuffer.GetUAV());

	// Create pipelines
	N_RETURN(createPipelineLayouts(), false);
	N_RETURN(createPipelines(), false);
	N_RETURN(createDescriptorTables(sortedParticles, particleSRV), false);

	return true;
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
		pipelineLayout.SetRange(0, DescriptorType::SRV, 3, 0, 0, DescriptorRangeFlag::DATA_STATIC);
		pipelineLayout.SetRange(1, DescriptorType::UAV, 1, 0, 0);
		X_RETURN(m_pipelineLayouts[REARRANGE], pipelineLayout.GetPipelineLayout(m_pipelineLayoutCache,
			PipelineLayoutFlag::NONE, L"RearrangementLayout"), false);
	}

	// Density
	{
		Util::PipelineLayout pipelineLayout;
		pipelineLayout.SetConstants(0, SizeOfInUint32(CBSimulation), 0, 0, Shader::Stage::CS);
		pipelineLayout.SetRange(1, DescriptorType::SRV, 2, 0, 0, DescriptorRangeFlag::DATA_STATIC);
		pipelineLayout.SetRange(2, DescriptorType::UAV, 1, 0, 0);
		X_RETURN(m_pipelineLayouts[DENSITY], pipelineLayout.GetPipelineLayout(m_pipelineLayoutCache,
			PipelineLayoutFlag::NONE, L"DensityLayout"), false);
	}

	// Force
	{
		Util::PipelineLayout pipelineLayout;
		pipelineLayout.SetConstants(0, SizeOfInUint32(CBSimulation), 0, 0, Shader::Stage::CS);
		pipelineLayout.SetRange(1, DescriptorType::SRV, 2, 0, 0, DescriptorRangeFlag::DATA_STATIC);
		pipelineLayout.SetRange(2, DescriptorType::SRV, 1, 2, 0, DescriptorRangeFlag::DATA_STATIC);
		pipelineLayout.SetRange(3, DescriptorType::UAV, 1, 0, 0);
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

bool FluidSPH::createDescriptorTables(const StructuredBuffer& sortedParticles,
	const Descriptor& particleSRV)
{
	// Create UAV and SRV table for integration
	{
		Util::DescriptorTable uavSrvTable;
		const Descriptor descriptors[] =
		{
			m_gridBuffer.GetUAV(),
			m_offsetBuffer.GetUAV(),
			m_gridBuffer.GetSRV(),
			sortedParticles.GetSRV(),
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
			particleSRV,
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
			sortedParticles.GetSRV(),
			m_gridBuffer.GetSRV(),
			m_densityBuffer.GetSRV()
		};
		srvTable.SetDescriptors(0, static_cast<uint32_t>(size(srvs)), srvs);
		X_RETURN(m_srvTables[SRV_TABLE_SPH], srvTable.GetCbvSrvUavTable(*m_descriptorTableCache), false);
	}

	// Create UAV tables
	{
		Util::DescriptorTable uavTable;
		uavTable.SetDescriptors(0, 1, &sortedParticles.GetUAV());
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
