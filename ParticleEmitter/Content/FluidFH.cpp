//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#include "FluidFH.h"
#include "SharedConst.h"

using namespace std;
using namespace DirectX;
using namespace XUSG;

FluidFH::FluidFH()
{
	m_shaderLib = ShaderLib::MakeUnique();
}

FluidFH::~FluidFH()
{
}

bool FluidFH::Init(CommandList* pCommandList, uint32_t numParticles,
	const DescriptorTableLib::sptr& descriptorTableLib,
	vector<Resource::uptr>& uploaders, Format rtFormat)
{
	const auto pDevice = pCommandList->GetDevice();
	m_graphicsPipelineLib = Graphics::PipelineLib::MakeUnique(pDevice);
	m_computePipelineLib = Compute::PipelineLib::MakeUnique(pDevice);
	m_pipelineLayoutLib = PipelineLayoutLib::MakeUnique(pDevice);
	m_descriptorTableLib = descriptorTableLib;
	
	// Create resources
	m_grid = Texture3D::MakeUnique();
	XUSG_N_RETURN(m_grid->Create(pDevice, GRID_SIZE_FHF, GRID_SIZE_FHF, GRID_SIZE_FHF,
		Format::R16G16B16A16_FLOAT, ResourceFlag::ALLOW_UNORDERED_ACCESS |
		ResourceFlag::ALLOW_SIMULTANEOUS_ACCESS, 1, MemoryFlag::NONE,
		L"VelocityDensity"), false);

	m_density = Texture3D::MakeUnique();
	XUSG_N_RETURN(m_density->Create(pDevice, GRID_SIZE_FHF, GRID_SIZE_FHF, GRID_SIZE_FHF,
		Format::R16_FLOAT, ResourceFlag::ALLOW_UNORDERED_ACCESS |
		ResourceFlag::ALLOW_SIMULTANEOUS_ACCESS, 1, MemoryFlag::NONE,
		L"Density"), false);

	m_densityU = Texture3D::MakeUnique();
	XUSG_N_RETURN(m_densityU->Create(pDevice, GRID_SIZE_FHF, GRID_SIZE_FHF, GRID_SIZE_FHF,
		Format::R32_UINT, ResourceFlag::ALLOW_UNORDERED_ACCESS, 1, MemoryFlag::NONE,
		L"EncodedDensity"), false);

	for (auto& velocity : m_velocity) velocity = Texture3D::MakeUnique();
	{
		XUSG_N_RETURN(m_velocity[0]->Create(pDevice, GRID_SIZE_FHF, GRID_SIZE_FHF, GRID_SIZE_FHF,
			Format::R32_FLOAT, ResourceFlag::ALLOW_UNORDERED_ACCESS, 1, MemoryFlag::NONE,
			L"VelocityX"), false);
		XUSG_N_RETURN(m_velocity[1]->Create(pDevice, GRID_SIZE_FHF, GRID_SIZE_FHF, GRID_SIZE_FHF,
			Format::R32_FLOAT, ResourceFlag::ALLOW_UNORDERED_ACCESS, 1, MemoryFlag::NONE,
			L"VelocityY"), false);
		XUSG_N_RETURN(m_velocity[2]->Create(pDevice, GRID_SIZE_FHF, GRID_SIZE_FHF, GRID_SIZE_FHF,
			Format::R32_FLOAT, ResourceFlag::ALLOW_UNORDERED_ACCESS, 1, MemoryFlag::NONE,
			L"VelocityZ"), false);
	}

	// Create constant buffer
	m_cbSimulation = ConstantBuffer::MakeUnique();
	XUSG_N_RETURN(m_cbSimulation->Create(pDevice, sizeof(CBSimulation), 1,
		nullptr, MemoryType::DEFAULT, MemoryFlag::NONE, L"CbSimultionFHF"), false);
	uploaders.emplace_back(Resource::MakeUnique());
	CBSimulation cbSimulation;
	{
		const XMFLOAT4 boundary(BOUNDARY_FHF);
		cbSimulation.SmoothRadius = boundary.w * 2.0f / GRID_SIZE_FHF;
		cbSimulation.PressureStiffness = 200.0f;
		cbSimulation.RestDensity = 1000.0f;

		const float mass = 1310.72f / numParticles;
		cbSimulation.DensityCoef = mass * 315.0f / (64.0f * XM_PI * pow(cbSimulation.SmoothRadius, 9.0f));
		cbSimulation.VelocityCoef = mass * 15.0f / (2.0f * XM_PI * pow(cbSimulation.SmoothRadius, 3.0f));
		cbSimulation.Viscosity = 0.1f;
	}
	m_cbSimulation->Upload(pCommandList, uploaders.back().get(),
		&cbSimulation, sizeof(CBSimulation));

	// Create pipelines
	XUSG_N_RETURN(createPipelineLayouts(), false);
	XUSG_N_RETURN(createPipelines(rtFormat), false);
	XUSG_N_RETURN(createDescriptorTables(), false);

	return true;
}

void FluidFH::UpdateFrame()
{
	// Set barriers with promotions
	ResourceBarrier barrier;
	m_grid->SetBarrier(&barrier, ResourceState::NON_PIXEL_SHADER_RESOURCE);
}

void FluidFH::Simulate(CommandList* pCommandList, bool hasViscosity)
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

	const auto numGroups = XUSG_DIV_UP(GRID_SIZE_FHF, 4);
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
		XUSG_X_RETURN(m_pipelineLayouts[TRANSFER_FHF], pipelineLayout->GetPipelineLayout(m_pipelineLayoutLib.get(),
			PipelineLayoutFlag::NONE, L"TransferFHFLayout"), false);
	}

	// Resampling
	{
		const auto pipelineLayout = Util::PipelineLayout::MakeUnique();
		pipelineLayout->SetRange(0, DescriptorType::UAV, 1, 0, 0, DescriptorFlag::DATA_STATIC_WHILE_SET_AT_EXECUTE);
		pipelineLayout->SetRange(0, DescriptorType::SRV, 1, 0);
		pipelineLayout->SetRange(1, DescriptorType::SAMPLER, 1, 0);
		XUSG_X_RETURN(m_pipelineLayouts[RESAMPLE], pipelineLayout->GetPipelineLayout(m_pipelineLayoutLib.get(),
			PipelineLayoutFlag::NONE, L"ResamplingLayout"), false);
	}

	return true;
}

bool FluidFH::createPipelines(Format rtFormat)
{
	auto csIndex = 0u;

	// Transfer with viscosity
	{
		XUSG_N_RETURN(m_shaderLib->CreateShader(Shader::Stage::CS, csIndex, L"CSTransferFHF.cso"), false);

		const auto state = Compute::State::MakeUnique();
		state->SetPipelineLayout(m_pipelineLayouts[TRANSFER_FHF]);
		state->SetShader(m_shaderLib->GetShader(Shader::Stage::CS, csIndex++));
		XUSG_X_RETURN(m_pipelines[TRANSFER_FHF], state->GetPipeline(m_computePipelineLib.get(), L"TransferFHF"), false);
	}

	// Resampling
	{
		XUSG_N_RETURN(m_shaderLib->CreateShader(Shader::Stage::CS, csIndex, L"CSResample.cso"), false);

		const auto state = Compute::State::MakeUnique();
		state->SetPipelineLayout(m_pipelineLayouts[RESAMPLE]);
		state->SetShader(m_shaderLib->GetShader(Shader::Stage::CS, csIndex++));
		XUSG_X_RETURN(m_pipelines[RESAMPLE], state->GetPipeline(m_computePipelineLib.get(), L"Resampling"), false);
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
		XUSG_X_RETURN(m_cbvUavSrvTables[CBV_UAV_SRV_TABLE_PARTICLE_FHF], descriptorTable->GetCbvSrvUavTable(m_descriptorTableLib.get()), false);
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
		XUSG_X_RETURN(m_cbvUavSrvTables[CBV_UAV_SRV_TABLE_TRANSFER_FHF], descriptorTable->GetCbvSrvUavTable(m_descriptorTableLib.get()), false);
	}

	{
		const auto descriptorTable = Util::DescriptorTable::MakeUnique();
		descriptorTable->SetDescriptors(0, 1, &m_cbSimulation->GetCBV());
		XUSG_X_RETURN(m_cbvUavSrvTables[CBV_UAV_SRV_TABLE_PARTICLE_FHS], descriptorTable->GetCbvSrvUavTable(m_descriptorTableLib.get()), false);
	}

	{
		const auto descriptorTable = Util::DescriptorTable::MakeUnique();
		const Descriptor descriptors[] =
		{
			m_densityU->GetUAV(),
			m_density->GetUAV()
		};
		descriptorTable->SetDescriptors(0, static_cast<uint32_t>(size(descriptors)), descriptors);
		XUSG_X_RETURN(m_cbvUavSrvTables[CBV_UAV_SRV_TABLE_TRANSFER_FHS], descriptorTable->GetCbvSrvUavTable(m_descriptorTableLib.get()), false);
	}

	// Create the sampler
	{
		const auto descriptorTable = Util::DescriptorTable::MakeUnique();
		const auto samplerAnisoWrap = SamplerPreset::LINEAR_CLAMP;
		descriptorTable->SetSamplers(0, 1, &samplerAnisoWrap, m_descriptorTableLib.get());
		XUSG_X_RETURN(m_samplerTable, descriptorTable->GetSamplerTable(m_descriptorTableLib.get()), false);
	}

	return true;
}
