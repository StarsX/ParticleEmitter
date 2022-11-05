//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#pragma once

#include "Core/XUSG.h"
#include "Optional/XUSGComputeUtil.h"

class FluidSPH
{
public:
	FluidSPH();
	virtual ~FluidSPH();

	bool Init(XUSG::CommandList* pCommandList, uint32_t numParticles,
		const XUSG::DescriptorTableLib::sptr& descriptorTableLib,
		std::vector<XUSG::Resource::uptr>& uploaders,
		const XUSG::StructuredBuffer::uptr* pParticleBuffers);

	void UpdateFrame();
	void Simulate(XUSG::CommandList* pCommandList);

	const XUSG::DescriptorTable& GetDescriptorTable() const;

protected:
	enum ParticleBufferIndex : uint8_t
	{
		REARRANGED,
		INTEGRATED,

		NUM_PARTICLE_BUFFER
	};

	enum PipelineIndex : uint8_t
	{
		REARRANGE,
		DENSITY,
		FORCE,

		NUM_PIPELINE
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
	bool createDescriptorTables();

	void rearrange(XUSG::CommandList* pCommandList);
	void density(XUSG::CommandList* pCommandList);
	void force(XUSG::CommandList* pCommandList);

	XUSG::ShaderLib::uptr				m_shaderLib;
	XUSG::Compute::PipelineLib::uptr	m_computePipelineLib;
	XUSG::PipelineLayoutLib::uptr		m_pipelineLayoutLib;
	XUSG::DescriptorTableLib::sptr		m_descriptorTableLib;

	XUSG::ComputeUtil		m_prefixSumUtil;

	XUSG::PipelineLayout	m_pipelineLayouts[NUM_PIPELINE];
	XUSG::Pipeline			m_pipelines[NUM_PIPELINE];

	XUSG::DescriptorTable	m_uavSrvTable;
	XUSG::DescriptorTable	m_uavTables[NUM_UAV_TABLE];
	XUSG::DescriptorTable	m_srvTables[NUM_SRV_TABLE];

	XUSG::TypedBuffer::uptr	m_gridBuffer;
	XUSG::TypedBuffer::uptr	m_offsetBuffer;
	XUSG::TypedBuffer::uptr	m_densityBuffer;
	XUSG::TypedBuffer::uptr	m_forceBuffer;

	const XUSG::StructuredBuffer::uptr* m_pParticleBuffers;

	XUSG::ConstantBuffer::uptr m_cbSimulation;

	uint32_t m_numParticles;
};
