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
	m_graphicsPipelineCache.SetDevice(device);
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
	vector<Resource>& uploaders, Format rtFormat)
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
		Format::R16_FLOAT, ResourceFlag::ALLOW_UNORDERED_ACCESS |
		ResourceFlag::ALLOW_SIMULTANEOUS_ACCESS, 1, MemoryType::DEFAULT,
		L"Density"), false);
	N_RETURN(m_densityU.Create(m_device, GRID_SIZE_FHF, GRID_SIZE_FHF, GRID_SIZE_FHF,
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
	N_RETURN(createPipelines(rtFormat), false);
	N_RETURN(createDescriptorTables(), false);

	return true;
}

void FluidFH::UpdateFrame()
{
	// Set barriers with promotions
	ResourceBarrier barrier;
	m_grid.SetBarrier(&barrier, ResourceState::NON_PIXEL_SHADER_RESOURCE);
}

void FluidFH::Simulate(const CommandList& commandList, bool hasViscosity)
{
	ResourceBarrier barriers[5];
	auto numBarriers = m_grid.SetBarrier(barriers, ResourceState::UNORDERED_ACCESS);
	numBarriers = m_densityU.SetBarrier(barriers, ResourceState::UNORDERED_ACCESS, numBarriers);
	numBarriers = m_velocity[0].SetBarrier(barriers, ResourceState::UNORDERED_ACCESS, numBarriers);
	numBarriers = m_velocity[1].SetBarrier(barriers, ResourceState::UNORDERED_ACCESS, numBarriers);
	numBarriers = m_velocity[2].SetBarrier(barriers, ResourceState::UNORDERED_ACCESS, numBarriers);
	commandList.Barrier(numBarriers, barriers);

	// Set pipeline state
	const auto pipelineIndex = hasViscosity ? TRANSFER_FHF : TRANSFER_FHS;
	commandList.SetComputePipelineLayout(m_pipelineLayouts[pipelineIndex]);
	commandList.SetPipelineState(m_pipelines[pipelineIndex]);

	// Set descriptor tables
	commandList.SetComputeDescriptorTable(0, m_cbvUavSrvTables[hasViscosity ?
		CBV_UAV_SRV_TABLE_TRANSFER_FHF : CBV_UAV_SRV_TABLE_TRANSFER_FHS]);

	commandList.Dispatch(DIV_UP(GRID_SIZE_FHF, 8), DIV_UP(GRID_SIZE_FHF, 8), GRID_SIZE_FHF);
}

void FluidFH::RayCast(const CommandList& commandList, uint32_t width,
	uint32_t height, CXMVECTOR eyePt, CXMMATRIX viewProj)
{
	// General matrices
	const XMFLOAT4 boundary(BOUNDARY_FHF);
	const auto world = XMMatrixScaling(boundary.w, boundary.w, boundary.w) *
		XMMatrixTranslation(boundary.x, boundary.y, boundary.z) *
		XMMatrixScaling(10.0f, 10.0f, 10.0f);
	const auto worldI = XMMatrixInverse(nullptr, world);
	const auto worldViewProj = world * viewProj;

	// Screen space matrices
	CBPerObject cbPerObject;
	cbPerObject.LocalSpaceLightPt = XMVector3TransformCoord(XMVectorSet(75.0f, 75.0f, -75.0f, 0.0f), worldI);
	cbPerObject.LocalSpaceEyePt = XMVector3TransformCoord(eyePt, worldI);

	const auto mToScreen = XMMATRIX
	(
		0.5f * width, 0.0f, 0.0f, 0.0f,
		0.0f, -0.5f * height, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.5f * width, 0.5f * height, 0.0f, 1.0f
	);
	const auto localToScreen = XMMatrixMultiply(worldViewProj, mToScreen);
	const auto screenToLocal = XMMatrixInverse(nullptr, localToScreen);
	cbPerObject.ScreenToLocal = XMMatrixTranspose(screenToLocal);
	cbPerObject.WorldViewProj = XMMatrixTranspose(worldViewProj);

	// Set pipeline state
	commandList.SetGraphicsPipelineLayout(m_pipelineLayouts[RAY_CAST]);
	commandList.SetPipelineState(m_pipelines[RAY_CAST]);

	commandList.IASetPrimitiveTopology(PrimitiveTopology::TRIANGLELIST);

	// Set descriptor tables
	commandList.SetGraphics32BitConstants(0, SizeOfInUint32(cbPerObject), &cbPerObject);
	commandList.SetGraphicsDescriptorTable(1, m_cbvUavSrvTables[CBV_UAV_SRV_TABLE_RAYCAST]);
	commandList.SetGraphicsDescriptorTable(2, m_samplerTable);

	commandList.Draw(3, 1, 0, 0);
}

const DescriptorTable& FluidFH::GetDescriptorTable(bool hasViscosity) const
{
	return m_cbvUavSrvTables[hasViscosity ? CBV_UAV_SRV_TABLE_PARTICLE_FHF : CBV_UAV_SRV_TABLE_PARTICLE_FHS];
}

bool FluidFH::createPipelineLayouts()
{
	// Transfer with viscosity
	{
		Util::PipelineLayout pipelineLayout;
		pipelineLayout.SetRange(0, DescriptorType::UAV, 5, 0);
		X_RETURN(m_pipelineLayouts[TRANSFER_FHF], pipelineLayout.GetPipelineLayout(m_pipelineLayoutCache,
			PipelineLayoutFlag::NONE, L"TransferFHFLayout"), false);
	}

	// Transfer without viscosity
	{
		Util::PipelineLayout pipelineLayout;
		pipelineLayout.SetRange(0, DescriptorType::UAV, 2, 0);
		X_RETURN(m_pipelineLayouts[TRANSFER_FHS], pipelineLayout.GetPipelineLayout(m_pipelineLayoutCache,
			PipelineLayoutFlag::NONE, L"TransferFHSLayout"), false);
	}

	// Resampling
	{
		Util::PipelineLayout pipelineLayout;
		pipelineLayout.SetRange(0, DescriptorType::UAV, 1, 0);
		pipelineLayout.SetRange(0, DescriptorType::SRV, 1, 0);
		pipelineLayout.SetRange(1, DescriptorType::SAMPLER, 1, 0);
		X_RETURN(m_pipelineLayouts[RESAMPLE], pipelineLayout.GetPipelineLayout(m_pipelineLayoutCache,
			PipelineLayoutFlag::NONE, L"ResamplingLayout"), false);
	}

	// Ray casting
	{
		Util::PipelineLayout pipelineLayout;
		pipelineLayout.SetConstants(0, SizeOfInUint32(CBPerObject), 0, 0, Shader::Stage::PS);
		pipelineLayout.SetRange(1, DescriptorType::SRV, 1, 0);
		pipelineLayout.SetRange(2, DescriptorType::SAMPLER, 1, 0);
		pipelineLayout.SetShaderStage(0, Shader::Stage::PS);
		pipelineLayout.SetShaderStage(1, Shader::Stage::PS);
		pipelineLayout.SetShaderStage(2, Shader::Stage::PS);
		X_RETURN(m_pipelineLayouts[RAY_CAST], pipelineLayout.GetPipelineLayout(m_pipelineLayoutCache,
			PipelineLayoutFlag::NONE, L"RayCastLayout"), false);
	}

	return true;
}

bool FluidFH::createPipelines(Format rtFormat)
{
	auto csIndex = 0u;

	// Transfer with viscosity
	{
		N_RETURN(m_shaderPool.CreateShader(Shader::Stage::CS, csIndex, L"CSTransferFHF.cso"), false);

		Compute::State state;
		state.SetPipelineLayout(m_pipelineLayouts[TRANSFER_FHF]);
		state.SetShader(m_shaderPool.GetShader(Shader::Stage::CS, csIndex++));
		X_RETURN(m_pipelines[TRANSFER_FHF], state.GetPipeline(m_computePipelineCache, L"TransferFHF"), false);
	}

	// Transfer without viscosity
	{
		N_RETURN(m_shaderPool.CreateShader(Shader::Stage::CS, csIndex, L"CSTransferFHS.cso"), false);

		Compute::State state;
		state.SetPipelineLayout(m_pipelineLayouts[TRANSFER_FHS]);
		state.SetShader(m_shaderPool.GetShader(Shader::Stage::CS, csIndex++));
		X_RETURN(m_pipelines[TRANSFER_FHS], state.GetPipeline(m_computePipelineCache, L"TransferFHS"), false);
	}

	// Resampling
	{
		N_RETURN(m_shaderPool.CreateShader(Shader::Stage::CS, csIndex, L"CSResample.cso"), false);

		Compute::State state;
		state.SetPipelineLayout(m_pipelineLayouts[RESAMPLE]);
		state.SetShader(m_shaderPool.GetShader(Shader::Stage::CS, csIndex++));
		X_RETURN(m_pipelines[RESAMPLE], state.GetPipeline(m_computePipelineCache, L"Resampling"), false);
	}

	// Ray casting
	{
		N_RETURN(m_shaderPool.CreateShader(Shader::Stage::VS, 0, L"VSScreenQuad.cso"), false);
		N_RETURN(m_shaderPool.CreateShader(Shader::Stage::PS, 0, L"PSRayCast.cso"), false);

		Graphics::State state;
		state.SetPipelineLayout(m_pipelineLayouts[RAY_CAST]);
		state.SetShader(Shader::Stage::VS, m_shaderPool.GetShader(Shader::Stage::VS, 0));
		state.SetShader(Shader::Stage::PS, m_shaderPool.GetShader(Shader::Stage::PS, 0));
		state.IASetPrimitiveTopologyType(PrimitiveTopologyType::TRIANGLE);
		state.DSSetState(Graphics::DEPTH_STENCIL_NONE, m_graphicsPipelineCache);
		state.OMSetBlendState(Graphics::NON_PRE_MUL, m_graphicsPipelineCache);
		state.OMSetRTVFormats(&rtFormat, 1);
		X_RETURN(m_pipelines[RAY_CAST], state.GetPipeline(m_graphicsPipelineCache, L"RayCast"), false);
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
		X_RETURN(m_cbvUavSrvTables[CBV_UAV_SRV_TABLE_PARTICLE_FHF], cbvUavSrvTable.GetCbvSrvUavTable(*m_descriptorTableCache), false);
	}

	{
		Util::DescriptorTable uavTable;
		const Descriptor descriptors[] =
		{
			m_densityU.GetUAV(),
			m_velocity[0].GetUAV(),
			m_velocity[1].GetUAV(),
			m_velocity[2].GetUAV(),
			m_grid.GetUAV()
		};
		uavTable.SetDescriptors(0, static_cast<uint32_t>(size(descriptors)), descriptors);
		X_RETURN(m_cbvUavSrvTables[CBV_UAV_SRV_TABLE_TRANSFER_FHF], uavTable.GetCbvSrvUavTable(*m_descriptorTableCache), false);
	}

	{
		Util::DescriptorTable cbvUavSrvTable;
		cbvUavSrvTable.SetDescriptors(0, 1, &m_cbSimulation.GetCBV());
		X_RETURN(m_cbvUavSrvTables[CBV_UAV_SRV_TABLE_PARTICLE_FHS], cbvUavSrvTable.GetCbvSrvUavTable(*m_descriptorTableCache), false);
	}

	{
		Util::DescriptorTable srvTable;
		const Descriptor descriptors[] =
		{
			m_density.GetSRV()
		};
		srvTable.SetDescriptors(0, static_cast<uint32_t>(size(descriptors)), descriptors);
		X_RETURN(m_cbvUavSrvTables[CBV_UAV_SRV_TABLE_RAYCAST], srvTable.GetCbvSrvUavTable(*m_descriptorTableCache), false);
	}

	{
		Util::DescriptorTable uavTable;
		const Descriptor descriptors[] =
		{
			m_densityU.GetUAV(),
			m_density.GetUAV()
		};
		uavTable.SetDescriptors(0, static_cast<uint32_t>(size(descriptors)), descriptors);
		X_RETURN(m_cbvUavSrvTables[CBV_UAV_SRV_TABLE_TRANSFER_FHS], uavTable.GetCbvSrvUavTable(*m_descriptorTableCache), false);
	}

	// Create the sampler
	{
		Util::DescriptorTable samplerTable;
		const auto samplerAnisoWrap = SamplerPreset::LINEAR_CLAMP;
		samplerTable.SetSamplers(0, 1, &samplerAnisoWrap, *m_descriptorTableCache);
		X_RETURN(m_samplerTable, samplerTable.GetSamplerTable(*m_descriptorTableCache), false);
	}

	return true;
}
