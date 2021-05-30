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
	FluidSPH(const XUSG::Device::sptr& device);
	virtual ~FluidSPH();

	bool Init(XUSG::CommandList* pCommandList, uint32_t numParticles,
		const XUSG::DescriptorTableCache::sptr& descriptorTableCache,
		std::vector<XUSG::Resource::uptr>& uploaders,
		const XUSG::StructuredBuffer::uptr* pParticleBuffers);

	void UpdateFrame();
	void Simulate(const XUSG::CommandList* pCommandList);

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

	void rearrange(const XUSG::CommandList* pCommandList);
	void density(const XUSG::CommandList* pCommandList);
	void force(const XUSG::CommandList* pCommandList);

	XUSG::Device::sptr m_device;

	XUSG::ShaderPool::uptr				m_shaderPool;
	XUSG::Compute::PipelineCache::uptr	m_computePipelineCache;
	XUSG::PipelineLayoutCache::uptr		m_pipelineLayoutCache;
	XUSG::DescriptorTableCache::sptr	m_descriptorTableCache;

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
