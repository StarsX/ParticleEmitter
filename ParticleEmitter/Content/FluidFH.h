//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#pragma once

#include "Core/XUSG.h"

class FluidFH
{
public:
	FluidFH();
	virtual ~FluidFH();

	bool Init(XUSG::CommandList* pCommandList, uint32_t numParticles,
		const XUSG::DescriptorTableCache::sptr& descriptorTableCache,
		std::vector<XUSG::Resource::uptr>& uploaders, XUSG::Format rtFormat);

	void UpdateFrame();
	void Simulate(XUSG::CommandList* pCommandList, bool hasViscosity = true);

	const XUSG::DescriptorTable& GetDescriptorTable(bool hasViscosity = true) const;

protected:
	enum PipelineIndex : uint8_t
	{
		TRANSFER_FHF,
		RESAMPLE,

		NUM_PIPELINE
	};

	enum CbvUavSrvTable : uint8_t
	{
		CBV_UAV_SRV_TABLE_PARTICLE_FHF,
		CBV_UAV_SRV_TABLE_TRANSFER_FHF,
		CBV_UAV_SRV_TABLE_PARTICLE_FHS,
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

	XUSG::ShaderPool::uptr				m_shaderPool;
	XUSG::Graphics::PipelineCache::uptr	m_graphicsPipelineCache;
	XUSG::Compute::PipelineCache::uptr	m_computePipelineCache;
	XUSG::PipelineLayoutCache::uptr		m_pipelineLayoutCache;
	XUSG::DescriptorTableCache::sptr	m_descriptorTableCache;

	XUSG::PipelineLayout	m_pipelineLayouts[NUM_PIPELINE];
	XUSG::Pipeline			m_pipelines[NUM_PIPELINE];

	std::vector<XUSG::DescriptorTable> m_uavSrvTables;
	XUSG::DescriptorTable	m_cbvUavSrvTables[NUM_CBV_UAV_SRV_TABLE];
	XUSG::DescriptorTable	m_samplerTable;

	XUSG::Texture3D::uptr	m_grid;
	XUSG::Texture3D::uptr	m_density;
	XUSG::Texture3D::uptr	m_densityU;
	XUSG::Texture3D::uptr	m_velocity[3];

	XUSG::ConstantBuffer::uptr m_cbSimulation;
};
