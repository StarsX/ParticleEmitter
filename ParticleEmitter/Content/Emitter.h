//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#pragma once

#include "Core/XUSG.h"

class Emitter
{
public:
	Emitter();
	virtual ~Emitter();

	bool Init(XUSG::CommandList* pCommandList, uint32_t numParticles,
		const XUSG::DescriptorTableLib::sptr& descriptorTableLib,
		std::vector<XUSG::Resource::uptr> &uploaders, const XUSG::InputLayout* pInputLayout,
		XUSG::Format rtFormat, XUSG::Format dsFormat);
	bool SetEmitterCount(XUSG::CommandList* pCommandList, XUSG::RawBuffer* pCounter,
		XUSG::StructuredBuffer::uptr& emitterScratch);

	void UpdateFrame(uint8_t frameIndex, double time, float timeStep,
		const DirectX::XMFLOAT3X4& world, const DirectX::CXMMATRIX viewProj);
	void Distribute(XUSG::CommandList* pCommandList, const XUSG::RawBuffer* pCounter,
		const XUSG::VertexBuffer* pVB, const XUSG::IndexBuffer* pIB, uint32_t numIndices,
		float density, float scale);
	void EmitParticle(const XUSG::CommandList* pCommandList, uint8_t frameIndex,
		uint32_t numParticles, const XUSG::DescriptorTable& uavTable);
	void Render(const XUSG::CommandList* pCommandList, uint8_t frameIndex,
		const XUSG::Descriptor& rtv, const XUSG::Descriptor* pDsv);
	void RenderSPH(XUSG::CommandList* pCommandList, uint8_t frameIndex, const XUSG::Descriptor& rtv,
		const XUSG::Descriptor* pDsv, const XUSG::DescriptorTable& fluidDescriptorTable);
	void RenderFHF(const XUSG::CommandList* pCommandList, uint8_t frameIndex, const XUSG::Descriptor& rtv,
		const XUSG::Descriptor* pDsv, const XUSG::DescriptorTable& fluidDescriptorTable);
	void Visualize(const XUSG::CommandList* pCommandList, const XUSG::Descriptor& rtv,
		const XUSG::Descriptor* pDsv, const DirectX::XMFLOAT4X4& worldViewProj);

	const XUSG::StructuredBuffer::uptr* GetParticleBuffers() const;

	static const uint8_t FrameCount = 3;
	
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
		EMISSION,
		VISUALIZE,

		NUM_PIPELINE
	};

	enum UAVTable : uint8_t
	{
		UAV_TABLE_EMITTER,
		UAV_TABLE_COUNTER,
		UAV_TABLE_PARTICLE,
		UAV_TABLE_PARTICLE1,

		NUM_UAV_TABLE
	};

	bool createPipelineLayouts();
	bool createPipelines(const XUSG::InputLayout* pInputLayout, XUSG::Format rtFormat, XUSG::Format dsFormat);
	bool createDescriptorTables();

	void distribute(const XUSG::CommandList* pCommandList, const XUSG::VertexBuffer* pVB,
		const XUSG::IndexBuffer* pIB, uint32_t numIndices, float density, float scale);

	XUSG::ShaderLib::uptr				m_shaderLib;
	XUSG::Graphics::PipelineLib::uptr	m_graphicsPipelineLib;
	XUSG::Compute::PipelineLib::uptr	m_computePipelineLib;
	XUSG::PipelineLayoutLib::uptr		m_pipelineLayoutLib;
	XUSG::DescriptorTableLib::sptr		m_descriptorTableLib;

	XUSG::PipelineLayout	m_pipelineLayouts[NUM_PIPELINE];
	XUSG::Pipeline			m_pipelines[NUM_PIPELINE];

	XUSG::DescriptorTable	m_uavTables[NUM_UAV_TABLE];
	XUSG::DescriptorTable	m_srvTable;

	XUSG::Descriptor		m_srvVertexBuffer;

	XUSG::RawBuffer::sptr	m_counter;
	XUSG::StructuredBuffer::uptr m_emitterBuffer;
	XUSG::StructuredBuffer::uptr m_particleBuffers[NUM_PARTICLE_BUFFER];

	XUSG::ConstantBuffer::uptr m_cbPerObject;

	double					m_time;
	uint32_t				m_numParticles;
	uint32_t				m_numEmitters;
	DirectX::XMFLOAT3X4		m_world;
};
