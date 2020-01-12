//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#pragma once

#include "DXFramework.h"
#include "Core/XUSG.h"

class Renderer
{
public:
	Renderer(const XUSG::Device& device);
	virtual ~Renderer();

	bool Init(const XUSG::CommandList& commandList, uint32_t width, uint32_t height,
		std::vector<XUSG::Resource>& uploaders, const char* fileName,
		XUSG::Format rtFormat, XUSG::Format dsFormat);

	void UpdateFrame(double time, float timeStep, const DirectX::XMFLOAT4& posScale,
		DirectX::CXMMATRIX viewProj, bool isPaused);
	void Render(const XUSG::CommandList& commandList, const XUSG::Descriptor& rtv,
		const XUSG::Descriptor& dsv);

	const XUSG::VertexBuffer& GetVertexBuffer() const;
	const XUSG::IndexBuffer& GetIndexBuffer() const;
	const XUSG::InputLayout& GetInputLayout() const;
	const DirectX::XMFLOAT4X4& GetWorldViewProj() const;
	const DirectX::XMFLOAT4X4& GetWorld() const;
	uint32_t GetNumIndices() const;
	
protected:
	struct BasePassConstants
	{
		DirectX::XMFLOAT4X4	WorldViewProj;
		DirectX::XMFLOAT4X4	WorldViewProjPrev;
		DirectX::XMFLOAT4X4	World;
	};

	bool createVB(const XUSG::CommandList& commandList, uint32_t numVert,
		uint32_t stride, const uint8_t* pData, std::vector<XUSG::Resource>& uploaders);
	bool createIB(const XUSG::CommandList& commandList, uint32_t numIndices,
		const uint32_t* pData, std::vector<XUSG::Resource>& uploaders);
	bool createInputLayout();
	bool createPipelineLayouts();
	bool createPipelines(XUSG::Format rtFormat, XUSG::Format dsFormat);

	XUSG::Device m_device;

	uint32_t	m_numIndices;
	uint8_t		m_frameParity;

	DirectX::XMUINT2	m_viewport;
	BasePassConstants	m_cbBasePass;

	XUSG::InputLayout		m_inputLayout;
	XUSG::PipelineLayout	m_pipelineLayout;
	XUSG::Pipeline			m_pipeline;

	XUSG::DescriptorTable	m_cbvTable;
	XUSG::Framebuffer		m_framebuffer;

	XUSG::VertexBuffer		m_vertexBuffer;
	XUSG::IndexBuffer		m_indexBuffer;

	XUSG::ShaderPool				m_shaderPool;
	XUSG::Graphics::PipelineCache	m_graphicsPipelineCache;
	XUSG::PipelineLayoutCache		m_pipelineLayoutCache;
};
