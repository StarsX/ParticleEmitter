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

	bool Init(const XUSG::CommandList &commandList, uint32_t numParticles,
		std::shared_ptr<XUSG::DescriptorTableCache> descriptorTableCache,
		std::vector<XUSG::Resource> &uploaders, const XUSG::InputLayout& inputLayout,
		XUSG::Format rtFormat, XUSG::Format dsFormat);
	bool SetEmitterCount(const XUSG::CommandList& commandList, XUSG::RawBuffer& counter,
		XUSG::Resource* pEmitterSource);

	void UpdateFrame(double time, float timeStep, const DirectX::CXMMATRIX viewProj);
	void Distribute(const XUSG::CommandList& commandList, const XUSG::RawBuffer& counter,
		const XUSG::VertexBuffer& vb, const XUSG::IndexBuffer& ib, uint32_t numIndices,
		float density, float scale);
	void EmitParticle(const XUSG::CommandList& commandList, uint32_t numParticles,
		const XUSG::DescriptorTable& uavTable, const DirectX::XMFLOAT4X4& world);
	void Render(const XUSG::CommandList& commandList, const XUSG::Descriptor& rtv,
		const XUSG::Descriptor* pDsv, const DirectX::XMFLOAT4X4& world);
	void RenderSPH(const XUSG::CommandList& commandList, const XUSG::Descriptor& rtv,
		const XUSG::Descriptor* pDsv, const XUSG::DescriptorTable& fluidDescriptorTable,
		const DirectX::XMFLOAT4X4& world);
	void RenderFHF(const XUSG::CommandList& commandList, const XUSG::Descriptor& rtv,
		const XUSG::Descriptor* pDsv, const XUSG::DescriptorTable& fluidDescriptorTable,
		const DirectX::XMFLOAT4X4& world);
	void ParticleFHS(const XUSG::CommandList& commandList,
		const XUSG::DescriptorTable& fluidDescriptorTable,
		const DirectX::XMFLOAT4X4& world);
	void Visualize(const XUSG::CommandList& commandList, const XUSG::Descriptor& rtv,
		const XUSG::Descriptor* pDsv, const DirectX::XMFLOAT4X4& worldViewProj);

	XUSG::StructuredBuffer* GetParticleBuffers();
	
protected:
	enum ParticleBufferIndex : uint8_t
	{
		REARRANGED,
		INTEGRATED,

		NUM_PARTICLE_BUFFER
	};

	enum PipelineIndex : uint8_t
	{
		DISTRIBUTE,
		PARTICLE,
		PARTICLE_SPH,
		PARTICLE_FHF,
		PARTICLE_FHS,
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
		UAV_TABLE_EMITTER,
		UAV_TABLE_COUNTER,
		UAV_TABLE_PARTICLE,
		UAV_TABLE_PARTICLE1,

		NUM_UAV_TABLE
	};

	struct EmitterInfo
	{
		DirectX::XMUINT3 Indices;
		DirectX::XMFLOAT2 Barycoord;
	};

	struct ParticleInfo
	{
		DirectX::XMFLOAT3 Pos;
		DirectX::XMFLOAT3 Velocity;
		float LifeTime;
	};

	struct CBEmission
	{
		DirectX::XMFLOAT4X4 World;
		DirectX::XMFLOAT4X4 WorldPrev;
		float TimeStep;
		uint32_t BaseSeed;
		uint32_t NumEmitters;
	};

	struct CBParticle : public CBEmission
	{
		uint32_t NumParticles;
		DirectX::XMFLOAT4X4 ViewProj;
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
	XUSG::DescriptorTable	m_samplerTable;

	XUSG::Descriptor		m_srvVertexBuffer;

	XUSG::RawBuffer			m_counter;
	XUSG::StructuredBuffer	m_emitterBuffer;
	XUSG::StructuredBuffer	m_particleBuffers[NUM_PARTICLE_BUFFER];

	CBParticle				m_cbParticle;
	double					m_time;
};
