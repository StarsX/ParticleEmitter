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
		std::shared_ptr<XUSG::DescriptorTableCache> descriptorTableCache);

	void UpdateFrame();
	void Simulate(const XUSG::CommandList& commandList);

	const XUSG::DescriptorTable& GetDescriptorTable() const;

protected:
	enum UavSrvTable : uint8_t
	{
		UAV_SRV_TABLE_PARTICLE,
		UAV_SRV_TABLE_DENSITY,

		NUM_UAV_TABLE
	};

	bool createPipelineLayouts();
	bool createPipelines();
	bool createDescriptorTables();

	XUSG::Device m_device;

	XUSG::ShaderPool				m_shaderPool;
	XUSG::Compute::PipelineCache	m_computePipelineCache;
	XUSG::PipelineLayoutCache		m_pipelineLayoutCache;
	std::shared_ptr<XUSG::DescriptorTableCache> m_descriptorTableCache;

	XUSG::ComputeUtil		m_prefixSumUtil;

	XUSG::PipelineLayout	m_pipelineLayout;
	XUSG::Pipeline			m_pipeline;

	XUSG::DescriptorTable	m_uavSrvTables[NUM_UAV_TABLE];

	XUSG::Texture3D			m_grid;
	XUSG::Texture3D			m_density;

	float					m_densityCoef;
};
