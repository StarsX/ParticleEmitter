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
	m_cbSimulationData.VelocityCoef = mass * 15.0f / (2.0f * XM_PI * pow(m_cbSimulationData.SmoothRadius, 3.0f));
	m_cbSimulationData.Viscosity = 0.1f;
	
	// Create resources
	N_RETURN(m_grid.Create(m_device, GRID_SIZE_FHF, GRID_SIZE_FHF, GRID_SIZE_FHF,
		Format::R16G16B16A16_FLOAT, ResourceFlag::ALLOW_UNORDERED_ACCESS |
		ResourceFlag::ALLOW_SIMULTANEOUS_ACCESS, 1, MemoryType::DEFAULT,
		L"VelocityDensity"), false);
	N_RETURN(m_density.Create(m_device, GRID_SIZE_FHF, GRID_SIZE_FHF, GRID_SIZE_FHF,
		Format::R32_UINT, ResourceFlag::ALLOW_UNORDERED_ACCESS, 1,
		MemoryType::DEFAULT, L"EncodedDensity"), false);
	{
		N_RETURN(m_velocity[0].Create(m_device, GRID_SIZE_FHF, GRID_SIZE_FHF, GRID_SIZE_FHF,
			Format::R32_FLOAT, ResourceFlag::ALLOW_UNORDERED_ACCESS, 1, MemoryType::DEFAULT,
			L"VelocityX"), false);
		N_RETURN(m_velocity[1].Create(m_device, GRID_SIZE_FHF, GRID_SIZE_FHF, GRID_SIZE_FHF,
			Format::R32_FLOAT, ResourceFlag::ALLOW_UNORDERED_ACCESS, 1, MemoryType::DEFAULT,
			L"VelocityY"), false);
		N_RETURN(m_velocity[2].Create(m_device, GRID_SIZE_FHF, GRID_SIZE_FHF, GRID_SIZE_FHF,
			Format::R32_FLOAT, ResourceFlag::ALLOW_UNORDERED_ACCESS, 1, MemoryType::DEFAULT,
			L"VelocityZ"), false);
	}

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
	m_grid.SetBarrier(&barrier, ResourceState::NON_PIXEL_SHADER_RESOURCE);
}

void FluidFH::Simulate(const CommandList& commandList)
{
	ResourceBarrier barriers[5];
	auto numBarriers = m_grid.SetBarrier(barriers, ResourceState::UNORDERED_ACCESS);
	numBarriers = m_density.SetBarrier(barriers, ResourceState::UNORDERED_ACCESS, numBarriers);
	numBarriers = m_velocity[0].SetBarrier(barriers, ResourceState::UNORDERED_ACCESS, numBarriers);
	numBarriers = m_velocity[1].SetBarrier(barriers, ResourceState::UNORDERED_ACCESS, numBarriers);
	numBarriers = m_velocity[2].SetBarrier(barriers, ResourceState::UNORDERED_ACCESS, numBarriers);
	commandList.Barrier(numBarriers, barriers);

	// Set pipeline state
	commandList.SetComputePipelineLayout(m_pipelineLayout);
	commandList.SetPipelineState(m_pipeline);

	// Set descriptor tables
	commandList.SetComputeDescriptorTable(0, m_cbvUavSrvTables[CBV_UAV_SRV_TABLE_TRANSFER]);

	commandList.Dispatch(DIV_UP(GRID_SIZE_FHF, 8), DIV_UP(GRID_SIZE_FHF, 8), GRID_SIZE_FHF);
}

const DescriptorTable& FluidFH::GetDescriptorTable() const
{
	return m_cbvUavSrvTables[CBV_UAV_SRV_TABLE_PARTICLE];
}

bool FluidFH::createPipelineLayouts()
{
	// Transfer
	{
		Util::PipelineLayout pipelineLayout;
		pipelineLayout.SetRange(0, DescriptorType::UAV, 5, 0);
		X_RETURN(m_pipelineLayout, pipelineLayout.GetPipelineLayout(m_pipelineLayoutCache,
			PipelineLayoutFlag::NONE, L"TransferLayout"), false);
	}

	return true;
}

bool FluidFH::createPipelines()
{
	auto csIndex = 0u;

	// Transfer
	{
		N_RETURN(m_shaderPool.CreateShader(Shader::Stage::CS, csIndex, L"CSTransferFHF.cso"), false);

		Compute::State state;
		state.SetPipelineLayout(m_pipelineLayout);
		state.SetShader(m_shaderPool.GetShader(Shader::Stage::CS, csIndex++));
		X_RETURN(m_pipeline, state.GetPipeline(m_computePipelineCache, L"Transfer"), false);
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
			m_cbSimulation.GetCBV(),
			m_grid.GetSRV()
		};
		cbvUavSrvTable.SetDescriptors(0, static_cast<uint32_t>(size(descriptors)), descriptors);
		X_RETURN(m_cbvUavSrvTables[CBV_UAV_SRV_TABLE_PARTICLE], cbvUavSrvTable.GetCbvSrvUavTable(*m_descriptorTableCache), false);
	}

	{
		Util::DescriptorTable uavSrvTable;
		const Descriptor descriptors[] =
		{
			m_density.GetUAV(),
			m_velocity[0].GetUAV(),
			m_velocity[1].GetUAV(),
			m_velocity[2].GetUAV(),
			m_grid.GetUAV()
		};
		uavSrvTable.SetDescriptors(0, static_cast<uint32_t>(size(descriptors)), descriptors);
		X_RETURN(m_cbvUavSrvTables[CBV_UAV_SRV_TABLE_TRANSFER], uavSrvTable.GetCbvSrvUavTable(*m_descriptorTableCache), false);
	}

	return true;
}
