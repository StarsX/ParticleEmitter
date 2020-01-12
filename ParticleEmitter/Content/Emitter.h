//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#pragma once

#include "DXFramework.h"
#include "Core/XUSG.h"

class Emitter
{
public:
	Emitter(const XUSG::Device &device);
	virtual ~Emitter();

	bool Init(const XUSG::CommandList &commandList,
		std::shared_ptr<XUSG::DescriptorTableCache> descriptorTableCache,
		std::vector<XUSG::Resource> &uploaders, const XUSG::InputLayout& inputLayout,
		XUSG::Format rtFormat, XUSG::Format dsFormat);
	bool SetEmitterCount(const XUSG::CommandList& commandList, XUSG::RawBuffer& counter,
		XUSG::Resource* pEmitterSource);

	void UpdateFrame(double time, float timeStep);
	void Distribute(const XUSG::CommandList& commandList, const XUSG::RawBuffer& counter,
		const XUSG::VertexBuffer& vb, const XUSG::IndexBuffer& ib, uint32_t numIndices,
		float density, float scale);
	void EmitParticle(const XUSG::CommandList& commandList, uint32_t numParticles,
		const XUSG::DescriptorTable& uavTable, const DirectX::XMFLOAT4X4& world);
	void Visualize(const XUSG::CommandList& commandList, const XUSG::Descriptor& rtv,
		const XUSG::Descriptor* pDsv, const DirectX::XMFLOAT4X4& worldViewProj);
	
protected:
	enum PipelineIndex : uint8_t
	{
		DISTRIBUTE,
		EMISSION,
		VISUALIZE,

		NUM_PIPELINE
	};

	enum DescriptorPoolIndex : uint8_t
	{
		IMMUTABLE_POOL,
		TEMPORARY_POOL
	};

	enum UAVTable : uint8_t
	{
		EMITTER,
		COUNTER,

		NUM_UAV_TABLE
	};

	struct EmitterInfo
	{
		DirectX::XMUINT3 Indices;
		DirectX::XMFLOAT2 Barycoord;
	};

	struct CBEmission
	{
		DirectX::XMFLOAT4X4 World;
		DirectX::XMFLOAT4X4 WorldPrev;
		float Time;
		float TimeStep;
		uint32_t NumParticles;
		uint32_t NumEmitters;
	};

	bool createPipelineLayouts();
	bool createPipelines(const XUSG::InputLayout& inputLayout, XUSG::Format rtFormat, XUSG::Format dsFormat);
	bool createDescriptorTables();

	void distribute(const XUSG::CommandList& commandList, const XUSG::VertexBuffer& vb,
		const XUSG::IndexBuffer& ib, uint32_t numIndices, float density, float scale);
	
	XUSG::Device m_device;

	XUSG::ShaderPool				m_shaderPool;
	XUSG::Graphics::PipelineCache	m_graphicsPipelineCache;
	XUSG::Compute::PipelineCache	m_computePipelineCache;
	XUSG::PipelineLayoutCache		m_pipelineLayoutCache;
	std::shared_ptr<XUSG::DescriptorTableCache> m_descriptorTableCache;

	XUSG::PipelineLayout	m_pipelineLayouts[NUM_PIPELINE];
	XUSG::Pipeline			m_pipelines[NUM_PIPELINE];

	XUSG::DescriptorTable	m_uavTables[NUM_UAV_TABLE];
	XUSG::DescriptorTable	m_srvTable;

	XUSG::Descriptor		m_srvVertexBuffer;

	XUSG::RawBuffer			m_counter;
	XUSG::StructuredBuffer	m_emitterBuffer;

	uint32_t				m_numEmitters;

	DirectX::XMFLOAT4X4		m_worldPrev;

	double					m_time;
	float					m_timeStep;
};
