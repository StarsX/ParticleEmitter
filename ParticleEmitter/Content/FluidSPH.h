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
		const XUSG::Descriptor& particleUAV);
	
	void UpdateFrame(double time, float timeStep, const DirectX::CXMMATRIX viewProj);
	void Simulate(const XUSG::CommandList& commandList);

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
	bool createDescriptorTables(const XUSG::Descriptor& particleUAV);

	XUSG::Device m_device;

	XUSG::ShaderPool				m_shaderPool;
	XUSG::Compute::PipelineCache	m_computePipelineCache;
	XUSG::PipelineLayoutCache		m_pipelineLayoutCache;
	std::shared_ptr<XUSG::DescriptorTableCache> m_descriptorTableCache;

	XUSG::PipelineLayout	m_pipelineLayouts[NUM_PIPELINE];
	XUSG::Pipeline			m_pipelines[NUM_PIPELINE];

	XUSG::DescriptorTable	m_uavTables[NUM_UAV_TABLE];
	XUSG::DescriptorTable	m_srvTables;

	XUSG::TypedBuffer		m_gridBuffer;
	XUSG::TypedBuffer		m_offsetBuffer;
	XUSG::TypedBuffer		m_densityBuffer;
	XUSG::TypedBuffer		m_forceBuffer;

	CBSimulation			m_cbSimulation;
	double					m_time;
};
