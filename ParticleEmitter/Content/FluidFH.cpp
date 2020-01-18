//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#include "FluidFH.h"
#include "SharedConst.h"

using namespace std;
using namespace DirectX;
using namespace XUSG;

FluidFH::FluidFH(const Device& device) :
	m_device(device)
{
	m_computePipelineCache.SetDevice(device);
	m_pipelineLayoutCache.SetDevice(device);
	m_prefixSumUtil.SetDevice(device);
}

FluidFH::~FluidFH()
{
}

bool FluidFH::Init(const CommandList& commandList, uint32_t numParticles,
	shared_ptr<DescriptorTableCache> descriptorTableCache)
{
	m_descriptorTableCache = descriptorTableCache;

	const XMFLOAT4 boundary(BOUNDARY_FHF);
	const float mass = 5243.0f / numParticles;
	const float cellSize = boundary.w * 2.0f / GRID_SIZE_FHF;
	const float cellVolume = cellSize * cellSize * cellSize;
	m_densityCoef = mass / cellVolume;
	
	// Create resources
	N_RETURN(m_grid.Create(m_device, GRID_SIZE_FHF, GRID_SIZE_FHF, GRID_SIZE_FHF,
		Format::R32_UINT, ResourceFlag::ALLOW_UNORDERED_ACCESS, 1,
		MemoryType::DEFAULT, L"Grid3D"), false);
	N_RETURN(m_density.Create(m_device, GRID_SIZE_FHF, GRID_SIZE_FHF, GRID_SIZE_FHF,
		Format::R16_FLOAT, ResourceFlag::ALLOW_UNORDERED_ACCESS |
		ResourceFlag::ALLOW_SIMULTANEOUS_ACCESS, 1,
		MemoryType::DEFAULT, L"Density3D"), false);

	// Create pipelines
	N_RETURN(createPipelineLayouts(), false);
	N_RETURN(createPipelines(), false);
	N_RETURN(createDescriptorTables(), false);

	return true;
}

void FluidFH::UpdateFrame()
{
	// Set barriers with promotions
	ResourceBarrier barrier;
	m_density.SetBarrier(&barrier, ResourceState::NON_PIXEL_SHADER_RESOURCE);
}

void FluidFH::Simulate(const CommandList& commandList)
{
	ResourceBarrier barriers[2];
	auto numBarriers = m_grid.SetBarrier(barriers, ResourceState::NON_PIXEL_SHADER_RESOURCE);
	numBarriers = m_density.SetBarrier(barriers, ResourceState::UNORDERED_ACCESS, numBarriers);
	commandList.Barrier(numBarriers, barriers);

	// Set pipeline state
	commandList.SetComputePipelineLayout(m_pipelineLayout);
	commandList.SetPipelineState(m_pipeline);

	// Set descriptor tables
	commandList.SetCompute32BitConstants(0, SizeOfInUint32(m_densityCoef), &m_densityCoef);
	commandList.SetComputeDescriptorTable(1, m_uavSrvTables[UAV_SRV_TABLE_DENSITY]);

	commandList.Dispatch(DIV_UP(GRID_SIZE_FHF, 8), DIV_UP(GRID_SIZE_FHF, 8), GRID_SIZE_FHF);

	// Clear grid
	const uint32_t clear[4] = {};
	numBarriers = m_grid.SetBarrier(barriers, ResourceState::UNORDERED_ACCESS);
	commandList.Barrier(numBarriers, barriers);
	commandList.ClearUnorderedAccessViewUint(m_uavSrvTables[UAV_SRV_TABLE_PARTICLE], m_grid.GetUAV(),
		m_grid.GetResource(), clear);
}

const DescriptorTable& FluidFH::GetDescriptorTable() const
{
	return m_uavSrvTables[UAV_SRV_TABLE_PARTICLE];
}

bool FluidFH::createPipelineLayouts()
{
	// Density
	{
		Util::PipelineLayout pipelineLayout;
		pipelineLayout.SetConstants(0, SizeOfInUint32(float), 0, 0, Shader::Stage::CS);
		pipelineLayout.SetRange(1, DescriptorType::SRV, 1, 0);
		pipelineLayout.SetRange(1, DescriptorType::UAV, 1, 0);
		X_RETURN(m_pipelineLayout, pipelineLayout.GetPipelineLayout(m_pipelineLayoutCache,
			PipelineLayoutFlag::NONE, L"DensityFastHybridLayout"), false);
	}

	return true;
}

bool FluidFH::createPipelines()
{
	auto csIndex = 0u;

	// Density
	{
		N_RETURN(m_shaderPool.CreateShader(Shader::Stage::CS, csIndex, L"CSDensityFHF.cso"), false);

		Compute::State state;
		state.SetPipelineLayout(m_pipelineLayout);
		state.SetShader(m_shaderPool.GetShader(Shader::Stage::CS, csIndex++));
		X_RETURN(m_pipeline, state.GetPipeline(m_computePipelineCache, L"Density"), false);
	}

	return true;
}

bool FluidFH::createDescriptorTables()
{
	// Create UAV and SRV tables
	{
		Util::DescriptorTable uavSrvTable;
		const Descriptor descriptors[] =
		{
			m_grid.GetUAV(),
			m_density.GetSRV()
		};
		uavSrvTable.SetDescriptors(0, static_cast<uint32_t>(size(descriptors)), descriptors);
		X_RETURN(m_uavSrvTables[UAV_SRV_TABLE_PARTICLE], uavSrvTable.GetCbvSrvUavTable(*m_descriptorTableCache), false);
	}

	{
		Util::DescriptorTable uavSrvTable;
		const Descriptor descriptors[] =
		{
			m_grid.GetSRV(),
			m_density.GetUAV()
		};
		uavSrvTable.SetDescriptors(0, static_cast<uint32_t>(size(descriptors)), descriptors);
		X_RETURN(m_uavSrvTables[UAV_SRV_TABLE_DENSITY], uavSrvTable.GetCbvSrvUavTable(*m_descriptorTableCache), false);
	}

	return true;
}
