//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#pragma once

#include "DXFramework.h"
#include "Core/XUSG.h"

class Particle
{
public:
	Particle(const XUSG::Device& device);
	virtual ~Particle();

	bool Init(const XUSG::CommandList& commandList, uint32_t numParticles,
		std::shared_ptr<XUSG::DescriptorTableCache> descriptorTableCache,
		XUSG::Format rtFormat, XUSG::Format dsFormat);
	
	void UpdateFrame(double time, float timeStep, const DirectX::CXMMATRIX viewProj);
	void Render(const XUSG::CommandList& commandList, const XUSG::Descriptor& rtv,
		const XUSG::Descriptor* pDsv);

	const XUSG::DescriptorTable& GetParticleBufferUAVTable() const;
	uint32_t GetParticleCount() const;

protected:
	struct ParticleInfo
	{
		DirectX::XMUINT3 Indices;
		DirectX::XMFLOAT2 Barycoord;
	};

	struct CBPerFrame
	{
		DirectX::XMFLOAT4X4 ViewProj;
		float TimeStep;
	};

	bool createPipelineLayouts();
	bool createPipelines(XUSG::Format rtFormat, XUSG::Format dsFormat);
	bool createDescriptorTables();

	XUSG::Device m_device;

	XUSG::ShaderPool				m_shaderPool;
	XUSG::Graphics::PipelineCache	m_graphicsPipelineCache;
	XUSG::Compute::PipelineCache	m_computePipelineCache;
	XUSG::PipelineLayoutCache		m_pipelineLayoutCache;
	std::shared_ptr<XUSG::DescriptorTableCache> m_descriptorTableCache;

	XUSG::PipelineLayout	m_pipelineLayouts[1];
	XUSG::Pipeline			m_pipelines[1];

	XUSG::DescriptorTable	m_uavTable;

	XUSG::StructuredBuffer	m_particleBuffer;

	DirectX::XMFLOAT4X4		m_viewProj;

	uint32_t				m_numParticles;

	double					m_time;
	float					m_timeStep;
};
