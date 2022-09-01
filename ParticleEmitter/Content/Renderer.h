//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#pragma once

#include "Core/XUSG.h"

class Renderer
{
public:
	Renderer();
	virtual ~Renderer();

	bool Init(XUSG::CommandList* pCommandList, uint32_t width, uint32_t height,
		std::vector<XUSG::Resource::uptr>& uploaders, const char* fileName,
		XUSG::Format rtFormat, XUSG::Format dsFormat);

	void UpdateFrame(uint8_t frameIndex, double time, float timeStep,
		const DirectX::XMFLOAT4& posScale, DirectX::CXMMATRIX viewProj, bool isPaused);
	void Render(const XUSG::CommandList* pCommandList, uint8_t frameIndex,
		const XUSG::Descriptor& rtv, const XUSG::Descriptor& dsv);

	const XUSG::VertexBuffer* GetVertexBuffer() const;
	const XUSG::IndexBuffer* GetIndexBuffer() const;
	const XUSG::InputLayout* GetInputLayout() const;
	const DirectX::XMFLOAT3X4& GetWorld() const;
	uint32_t GetNumIndices() const;

	static const uint8_t FrameCount = 3;
	
protected:
	bool createVB(XUSG::CommandList* pCommandList, uint32_t numVert, uint32_t stride,
		const uint8_t* pData, std::vector<XUSG::Resource::uptr>& uploaders);
	bool createIB(XUSG::CommandList* pCommandList, uint32_t numIndices,
		const uint32_t* pData, std::vector<XUSG::Resource::uptr>& uploaders);
	bool createInputLayout();
	bool createPipelineLayouts();
	bool createPipelines(XUSG::Format rtFormat, XUSG::Format dsFormat);

	uint32_t	m_numIndices;
	uint8_t		m_frameParity;

	DirectX::XMUINT2	m_viewport;
	DirectX::XMFLOAT3X4	m_world;
	DirectX::XMFLOAT4X4	m_worldViewProj;

	const XUSG::InputLayout* m_pInputLayout;
	XUSG::PipelineLayout	m_pipelineLayout;
	XUSG::Pipeline			m_pipeline;

	XUSG::DescriptorTable	m_cbvTable;
	XUSG::Framebuffer		m_framebuffer;

	XUSG::VertexBuffer::uptr	m_vertexBuffer;
	XUSG::IndexBuffer::uptr		m_indexBuffer;

	XUSG::ConstantBuffer::uptr	m_cbBasePass;

	XUSG::ShaderPool::uptr				m_shaderPool;
	XUSG::Graphics::PipelineCache::uptr	m_graphicsPipelineCache;
	XUSG::PipelineLayoutCache::uptr		m_pipelineLayoutCache;
};
