//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#pragma once

#include "DXFramework.h"
#include "Core/XUSG.h"
#include "Optional/XUSGComputeUtil.h"

class FluidFH
{
public:
	FluidFH(const XUSG::Device& device);
	virtual ~FluidFH();

	bool Init(const XUSG::CommandList& commandList, uint32_t numParticles,
		std::shared_ptr<XUSG::DescriptorTableCache> descriptorTableCache,
		std::vector<XUSG::Resource>& uploaders, XUSG::Format rtFormat);

	void UpdateFrame();
	void Simulate(const XUSG::CommandList& commandList, bool hasViscosity = true);
	void SimulateSmoke(const XUSG::CommandList& commandList);
	void RayCast(const XUSG::CommandList& commandList, uint32_t width, uint32_t height,
		DirectX::CXMVECTOR eyePt, DirectX::CXMMATRIX viewProj);

	const XUSG::DescriptorTable& GetDescriptorTable(bool hasViscosity = true) const;

protected:
	enum PipelineIndex : uint8_t
	{
		TRANSFER_FHF,
		TRANSFER_FHS,
		RESAMPLE,
		RAY_CAST,

		NUM_PIPELINE
	};

	enum CbvUavSrvTable : uint8_t
	{
		CBV_UAV_SRV_TABLE_PARTICLE_FHF,
		CBV_UAV_SRV_TABLE_TRANSFER_FHF,
		CBV_UAV_SRV_TABLE_PARTICLE_FHS,
		CBV_UAV_SRV_TABLE_RAYCAST,
		CBV_UAV_SRV_TABLE_TRANSFER_FHS,

		NUM_CBV_UAV_SRV_TABLE
	};

	struct CBSimulation
	{
		float SmoothRadius;
		float PressureStiffness;
		float RestDensity;
		float Viscosity;
		float DensityCoef;
		float VelocityCoef;
	};

	bool createPipelineLayouts();
	bool createPipelines(XUSG::Format rtFormat);
	bool createDescriptorTables();

	XUSG::Device m_device;

	XUSG::ShaderPool				m_shaderPool;
	XUSG::Graphics::PipelineCache	m_graphicsPipelineCache;
	XUSG::Compute::PipelineCache	m_computePipelineCache;
	XUSG::PipelineLayoutCache		m_pipelineLayoutCache;
	std::shared_ptr<XUSG::DescriptorTableCache> m_descriptorTableCache;

	XUSG::ComputeUtil		m_prefixSumUtil;

	XUSG::PipelineLayout	m_pipelineLayouts[NUM_PIPELINE];
	XUSG::Pipeline			m_pipelines[NUM_PIPELINE];

	std::vector<XUSG::DescriptorTable> m_uavSrvTables;
	XUSG::DescriptorTable	m_cbvUavSrvTables[NUM_CBV_UAV_SRV_TABLE];
	XUSG::DescriptorTable	m_samplerTable;

	XUSG::Texture3D			m_grid;
	XUSG::Texture3D			m_density;
	XUSG::Texture3D			m_densityU;
	XUSG::Texture3D			m_velocity[3];
	XUSG::ConstantBuffer	m_cbSimulation;

	CBSimulation			m_cbSimulationData;
};
