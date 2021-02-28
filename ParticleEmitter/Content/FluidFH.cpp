//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#include "FluidFH.h"
#include "SharedConst.h"

using namespace std;
using namespace DirectX;
using namespace XUSG;

struct CBPerObject
{
	DirectX::XMVECTOR LocalSpaceLightPt;
	DirectX::XMVECTOR LocalSpaceEyePt;
	DirectX::XMMATRIX ScreenToLocal;
	DirectX::XMMATRIX WorldViewProj;
};

FluidFH::FluidFH(const Device& device) :
	m_device(device)
{
	m_shaderPool = ShaderPool::MakeUnique();
	m_graphicsPipelineCache = Graphics::PipelineCache::MakeUnique(device);
	m_computePipelineCache = Compute::PipelineCache::MakeUnique(device);
	m_pipelineLayoutCache = PipelineLayoutCache::MakeUnique(device);
	m_prefixSumUtil.SetDevice(device);

	const XMFLOAT4 boundary(BOUNDARY_FHF);
	m_cbSimulationData.SmoothRadius = boundary.w * 2.0f / GRID_SIZE_FHF;
	m_cbSimulationData.PressureStiffness = 200.0f;
	m_cbSimulationData.RestDensity = 1000.0f;
}

FluidFH::~FluidFH()
{
}

bool FluidFH::Init(CommandList* pCommandList, uint32_t numParticles,
	shared_ptr<DescriptorTableCache> descriptorTableCache,
	vector<Resource>& uploaders, Format rtFormat)
{
	m_descriptorTableCache = descriptorTableCache;

	const float mass = 1310.72f / numParticles;
	m_cbSimulationData.DensityCoef = mass * 315.0f / (64.0f * XM_PI * pow(m_cbSimulationData.SmoothRadius, 9.0f));
	m_cbSimulationData.VelocityCoef = mass * 15.0f / (2.0f * XM_PI * pow(m_cbSimulationData.SmoothRadius, 3.0f));
	m_cbSimulationData.Viscosity = 0.1f;
	
	// Create resources
	m_grid = Texture3D::MakeUnique();
	N_RETURN(m_grid->Create(m_device, GRID_SIZE_FHF, GRID_SIZE_FHF, GRID_SIZE_FHF,
		Format::R16G16B16A16_FLOAT, ResourceFlag::ALLOW_UNORDERED_ACCESS |
		ResourceFlag::ALLOW_SIMULTANEOUS_ACCESS, 1, MemoryType::DEFAULT,
		L"VelocityDensity"), false);

	m_density = Texture3D::MakeUnique();
	N_RETURN(m_density->Create(m_device, GRID_SIZE_FHF, GRID_SIZE_FHF, GRID_SIZE_FHF,
		Format::R16_FLOAT, ResourceFlag::ALLOW_UNORDERED_ACCESS |
		ResourceFlag::ALLOW_SIMULTANEOUS_ACCESS, 1, MemoryType::DEFAULT,
		L"Density"), false);

	m_densityU = Texture3D::MakeUnique();
	N_RETURN(m_densityU->Create(m_device, GRID_SIZE_FHF, GRID_SIZE_FHF, GRID_SIZE_FHF,
		Format::R32_UINT, ResourceFlag::ALLOW_UNORDERED_ACCESS, 1,
		MemoryType::DEFAULT, L"EncodedDensity"), false);

	for (auto& velocity : m_velocity) velocity = Texture3D::MakeUnique();
	{
		N_RETURN(m_velocity[0]->Create(m_device, GRID_SIZE_FHF, GRID_SIZE_FHF, GRID_SIZE_FHF,
			Format::R32_FLOAT, ResourceFlag::ALLOW_UNORDERED_ACCESS, 1, MemoryType::DEFAULT,
			L"VelocityX"), false);
		N_RETURN(m_velocity[1]->Create(m_device, GRID_SIZE_FHF, GRID_SIZE_FHF, GRID_SIZE_FHF,
			Format::R32_FLOAT, ResourceFlag::ALLOW_UNORDERED_ACCESS, 1, MemoryType::DEFAULT,
			L"VelocityY"), false);
		N_RETURN(m_velocity[2]->Create(m_device, GRID_SIZE_FHF, GRID_SIZE_FHF, GRID_SIZE_FHF,
			Format::R32_FLOAT, ResourceFlag::ALLOW_UNORDERED_ACCESS, 1, MemoryType::DEFAULT,
			L"VelocityZ"), false);
	}

	m_cbSimulation = ConstantBuffer::MakeUnique();
	N_RETURN(m_cbSimulation->Create(m_device, sizeof(CBSimulation), 1,
		nullptr, MemoryType::DEFAULT, L"CbSimultionFHF"), false);
	uploaders.emplace_back();
	m_cbSimulation->Upload(pCommandList, uploaders.back(),
		&m_cbSimulationData, sizeof(CBSimulation));

	// Create pipelines
	N_RETURN(createPipelineLayouts(), false);
	N_RETURN(createPipelines(rtFormat), false);
	N_RETURN(createDescriptorTables(), false);

	return true;
}

void FluidFH::UpdateFrame()
{
	// Set barriers with promotions
	ResourceBarrier barrier;
	m_grid->SetBarrier(&barrier, ResourceState::NON_PIXEL_SHADER_RESOURCE);
}

void FluidFH::Simulate(const CommandList* pCommandList, bool hasViscosity)
{
	ResourceBarrier barriers[5];
	auto numBarriers = m_grid->SetBarrier(barriers, ResourceState::UNORDERED_ACCESS);
	numBarriers = m_densityU->SetBarrier(barriers, ResourceState::UNORDERED_ACCESS, numBarriers);
	numBarriers = m_velocity[0]->SetBarrier(barriers, ResourceState::UNORDERED_ACCESS, numBarriers);
	numBarriers = m_velocity[1]->SetBarrier(barriers, ResourceState::UNORDERED_ACCESS, numBarriers);
	numBarriers = m_velocity[2]->SetBarrier(barriers, ResourceState::UNORDERED_ACCESS, numBarriers);
	pCommandList->Barrier(numBarriers, barriers);

	// Set pipeline state
	pCommandList->SetComputePipelineLayout(m_pipelineLayouts[TRANSFER_FHF]);
	pCommandList->SetPipelineState(m_pipelines[TRANSFER_FHF]);

	// Set descriptor tables
	pCommandList->SetComputeDescriptorTable(0, m_cbvUavSrvTables[hasViscosity ?
		CBV_UAV_SRV_TABLE_TRANSFER_FHF : CBV_UAV_SRV_TABLE_TRANSFER_FHS]);

	const auto numGroups = DIV_UP(GRID_SIZE_FHF, 4);
	pCommandList->Dispatch(numGroups, numGroups, numGroups);
}

const DescriptorTable& FluidFH::GetDescriptorTable(bool hasViscosity) const
{
	return m_cbvUavSrvTables[hasViscosity ? CBV_UAV_SRV_TABLE_PARTICLE_FHF : CBV_UAV_SRV_TABLE_PARTICLE_FHS];
}

bool FluidFH::createPipelineLayouts()
{
	// Transfer with viscosity
	{
		const auto pipelineLayout = Util::PipelineLayout::MakeUnique();
		pipelineLayout->SetRange(0, DescriptorType::UAV, 5, 0, 0, DescriptorFlag::DATA_STATIC_WHILE_SET_AT_EXECUTE);
		X_RETURN(m_pipelineLayouts[TRANSFER_FHF], pipelineLayout->GetPipelineLayout(*m_pipelineLayoutCache,
			PipelineLayoutFlag::NONE, L"TransferFHFLayout"), false);
	}

	// Resampling
	{
		const auto pipelineLayout = Util::PipelineLayout::MakeUnique();
		pipelineLayout->SetRange(0, DescriptorType::UAV, 1, 0, 0, DescriptorFlag::DATA_STATIC_WHILE_SET_AT_EXECUTE);
		pipelineLayout->SetRange(0, DescriptorType::SRV, 1, 0);
		pipelineLayout->SetRange(1, DescriptorType::SAMPLER, 1, 0);
		X_RETURN(m_pipelineLayouts[RESAMPLE], pipelineLayout->GetPipelineLayout(*m_pipelineLayoutCache,
			PipelineLayoutFlag::NONE, L"ResamplingLayout"), false);
	}

	return true;
}

bool FluidFH::createPipelines(Format rtFormat)
{
	auto csIndex = 0u;

	// Transfer with viscosity
	{
		N_RETURN(m_shaderPool->CreateShader(Shader::Stage::CS, csIndex, L"CSTransferFHF.cso"), false);

		const auto state = Compute::State::MakeUnique();
		state->SetPipelineLayout(m_pipelineLayouts[TRANSFER_FHF]);
		state->SetShader(m_shaderPool->GetShader(Shader::Stage::CS, csIndex++));
		X_RETURN(m_pipelines[TRANSFER_FHF], state->GetPipeline(*m_computePipelineCache, L"TransferFHF"), false);
	}

	// Resampling
	{
		N_RETURN(m_shaderPool->CreateShader(Shader::Stage::CS, csIndex, L"CSResample.cso"), false);

		const auto state = Compute::State::MakeUnique();
		state->SetPipelineLayout(m_pipelineLayouts[RESAMPLE]);
		state->SetShader(m_shaderPool->GetShader(Shader::Stage::CS, csIndex++));
		X_RETURN(m_pipelines[RESAMPLE], state->GetPipeline(*m_computePipelineCache, L"Resampling"), false);
	}

	return true;
}

bool FluidFH::createDescriptorTables()
{
	// Create UAV and SRV tables
	{
		const auto descriptorTable = Util::DescriptorTable::MakeUnique();
		const Descriptor descriptors[] =
		{
			m_cbSimulation->GetCBV(),
			m_grid->GetSRV()
		};
		descriptorTable->SetDescriptors(0, static_cast<uint32_t>(size(descriptors)), descriptors);
		X_RETURN(m_cbvUavSrvTables[CBV_UAV_SRV_TABLE_PARTICLE_FHF], descriptorTable->GetCbvSrvUavTable(*m_descriptorTableCache), false);
	}

	{
		const auto descriptorTable = Util::DescriptorTable::MakeUnique();
		const Descriptor descriptors[] =
		{
			m_densityU->GetUAV(),
			m_velocity[0]->GetUAV(),
			m_velocity[1]->GetUAV(),
			m_velocity[2]->GetUAV(),
			m_grid->GetUAV()
		};
		descriptorTable->SetDescriptors(0, static_cast<uint32_t>(size(descriptors)), descriptors);
		X_RETURN(m_cbvUavSrvTables[CBV_UAV_SRV_TABLE_TRANSFER_FHF], descriptorTable->GetCbvSrvUavTable(*m_descriptorTableCache), false);
	}

	{
		const auto descriptorTable = Util::DescriptorTable::MakeUnique();
		descriptorTable->SetDescriptors(0, 1, &m_cbSimulation->GetCBV());
		X_RETURN(m_cbvUavSrvTables[CBV_UAV_SRV_TABLE_PARTICLE_FHS], descriptorTable->GetCbvSrvUavTable(*m_descriptorTableCache), false);
	}

	{
		const auto descriptorTable = Util::DescriptorTable::MakeUnique();
		const Descriptor descriptors[] =
		{
			m_densityU->GetUAV(),
			m_density->GetUAV()
		};
		descriptorTable->SetDescriptors(0, static_cast<uint32_t>(size(descriptors)), descriptors);
		X_RETURN(m_cbvUavSrvTables[CBV_UAV_SRV_TABLE_TRANSFER_FHS], descriptorTable->GetCbvSrvUavTable(*m_descriptorTableCache), false);
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
