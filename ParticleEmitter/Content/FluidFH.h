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
		const XUSG::DescriptorTableLib::sptr& descriptorTableLib,
		std::vector<XUSG::Resource::uptr>& uploaders, XUSG::Format rtFormat);

	void UpdateFrame();
	void Simulate(XUSG::CommandList* pCommandList, bool hasViscosity = true);

	const XUSG::DescriptorTable& GetDescriptorTable(bool hasViscosity = true) const;

protected:
	enum PipelineIndex : uint8_t
	{
		TRANSFER_FHF,
		BLIT_3D,

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

	XUSG::ShaderLib::uptr				m_shaderLib;
	XUSG::Graphics::PipelineLib::uptr	m_graphicsPipelineLib;
	XUSG::Compute::PipelineLib::uptr	m_computePipelineLib;
	XUSG::PipelineLayoutLib::uptr		m_pipelineLayoutLib;
	XUSG::DescriptorTableLib::sptr		m_descriptorTableLib;

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
