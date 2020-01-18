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

	const XMFLOAT4 boundary(BOUNDARY_FHF);
	m_cbSimulationData.SmoothRadius = boundary.w * 2.0f / GRID_SIZE_FHF;
	m_cbSimulationData.PressureStiffness = 200.0f;
	m_cbSimulationData.RestDensity = 1000.0f;
}

FluidFH::~FluidFH()
{
}

bool FluidFH::Init(const CommandList& commandList, uint32_t numParticles,
	shared_ptr<DescriptorTableCache> descriptorTableCache,
	vector<Resource>& uploaders)
{
	m_descriptorTableCache = descriptorTableCache;

	const float mass = 1310.72f / numParticles;
	m_cbSimulationData.DensityCoef = mass * 315.0f / (64.0f * XM_PI * pow(m_cbSimulationData.SmoothRadius, 9.0f));
	
	// Create resources
	N_RETURN(m_grid.Create(m_device, GRID_SIZE_FHF, GRID_SIZE_FHF, GRID_SIZE_FHF,
		Format::R32_UINT, ResourceFlag::ALLOW_UNORDERED_ACCESS, 1,
		MemoryType::DEFAULT, L"Grid3D"), false);
	N_RETURN(m_density.Create(m_device, GRID_SIZE_FHF, GRID_SIZE_FHF, GRID_SIZE_FHF,
		Format::R16_FLOAT, ResourceFlag::ALLOW_UNORDERED_ACCESS |
		ResourceFlag::ALLOW_SIMULTANEOUS_ACCESS, 1,
		MemoryType::DEFAULT, L"Density3D"), false);

	N_RETURN(m_cbSimulation.Create(m_device, sizeof(CBSimulation), 1,
		nullptr, MemoryType::DEFAULT, L"CbSimultionFHF"), false);
	uploaders.emplace_back();
	m_cbSimulation.Upload(commandList, uploaders.back(),
		&m_cbSimulationData, sizeof(CBSimulation));

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
	commandList.SetComputeDescriptorTable(0, m_cbvUavSrvTables[CBV_UAV_SRV_TABLE_DENSITY]);

	commandList.Dispatch(DIV_UP(GRID_SIZE_FHF, 8), DIV_UP(GRID_SIZE_FHF, 8), GRID_SIZE_FHF);

	// Clear grid
	const uint32_t clear[4] = {};
	numBarriers = m_grid.SetBarrier(barriers, ResourceState::UNORDERED_ACCESS);
	commandList.Barrier(numBarriers, barriers);
	commandList.ClearUnorderedAccessViewUint(m_cbvUavSrvTables[CBV_UAV_SRV_TABLE_PARTICLE], m_grid.GetUAV(),
		m_grid.GetResource(), clear);
}

const DescriptorTable& FluidFH::GetDescriptorTable() const
{
	return m_cbvUavSrvTables[CBV_UAV_SRV_TABLE_PARTICLE];
}

bool FluidFH::createPipelineLayouts()
{
	// Density
	{
		Util::PipelineLayout pipelineLayout;
		pipelineLayout.SetRange(0, DescriptorType::SRV, 1, 0);
		pipelineLayout.SetRange(0, DescriptorType::UAV, 1, 0);
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
		Util::DescriptorTable cbvUavSrvTable;
		const Descriptor descriptors[] =
		{
			m_grid.GetUAV(),
			m_density.GetSRV(),
			m_cbSimulation.GetCBV()
		};
		cbvUavSrvTable.SetDescriptors(0, static_cast<uint32_t>(size(descriptors)), descriptors);
		X_RETURN(m_cbvUavSrvTables[CBV_UAV_SRV_TABLE_PARTICLE], cbvUavSrvTable.GetCbvSrvUavTable(*m_descriptorTableCache), false);
	}

	{
		Util::DescriptorTable uavSrvTable;
		const Descriptor descriptors[] =
		{
			m_grid.GetSRV(),
			m_density.GetUAV()
		};
		uavSrvTable.SetDescriptors(0, static_cast<uint32_t>(size(descriptors)), descriptors);
		X_RETURN(m_cbvUavSrvTables[CBV_UAV_SRV_TABLE_DENSITY], uavSrvTable.GetCbvSrvUavTable(*m_descriptorTableCache), false);
	}

	return true;
}
