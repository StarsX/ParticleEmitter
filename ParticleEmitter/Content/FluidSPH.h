//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#pragma once

#include "DXFramework.h"
#include "Core/XUSG.h"
#include "Optional/XUSGComputeUtil.h"

class FluidSPH
{
public:
	FluidSPH(const XUSG::Device& device);
	virtual ~FluidSPH();

	bool Init(const XUSG::CommandList& commandList, uint32_t numParticles,
		std::shared_ptr<XUSG::DescriptorTableCache> descriptorTableCache,
		const XUSG::StructuredBuffer& sortedParticles,
		const XUSG::Descriptor& particleSRV);
	
	void UpdateFrame(double time, float timeStep, const DirectX::CXMMATRIX viewProj);
	void Simulate(const XUSG::CommandList& commandList);

	const XUSG::DescriptorTable& GetBuildGridDescriptorTable() const;

protected:
	enum PipelineIndex : uint8_t
	{
		REARRANGE,
		DENSITY,
		FORCE,

		NUM_PIPELINE
	};

	enum DescriptorPoolIndex : uint8_t
	{
		IMMUTABLE_POOL,
		TEMPORARY_POOL
	};

	enum UAVTable : uint8_t
	{
		UAV_TABLE_PARTICLE,
		UAV_TABLE_DENSITY,
		UAV_TABLE_FORCE,

		NUM_UAV_TABLE
	};

	enum SRVTable : uint8_t
	{
		SRV_TABLE_REARRANGLE,
		SRV_TABLE_SPH,

		NUM_SRV_TABLE
	};

	struct CBSimulation
	{
		uint32_t NumParticles;
		float SmoothRadius;
		float PressureStiffness;
		float RestDensity;
		float DensityCoef;
		float PressureGradCoef;
		float ViscosityLaplaceCoef;
	};

	bool createPipelineLayouts();
	bool createPipelines();
	bool createDescriptorTables(const XUSG::StructuredBuffer& sortedParticles,
		const XUSG::Descriptor& particleSRV);

	XUSG::Device m_device;

	XUSG::ShaderPool				m_shaderPool;
	XUSG::Compute::PipelineCache	m_computePipelineCache;
	XUSG::PipelineLayoutCache		m_pipelineLayoutCache;
	std::shared_ptr<XUSG::DescriptorTableCache> m_descriptorTableCache;

	XUSG::ComputeUtil		m_prefixSumUtil;

	XUSG::PipelineLayout	m_pipelineLayouts[NUM_PIPELINE];
	XUSG::Pipeline			m_pipelines[NUM_PIPELINE];

	XUSG::DescriptorTable	m_uavSrvTable;
	XUSG::DescriptorTable	m_uavTables[NUM_UAV_TABLE];
	XUSG::DescriptorTable	m_srvTables[NUM_SRV_TABLE];

	XUSG::TypedBuffer		m_gridBuffer;
	XUSG::TypedBuffer		m_offsetBuffer;
	XUSG::TypedBuffer		m_densityBuffer;
	XUSG::TypedBuffer		m_forceBuffer;

	CBSimulation			m_cbSimulation;
	double					m_time;
};
