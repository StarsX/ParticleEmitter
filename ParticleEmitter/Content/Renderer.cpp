//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#include "DXFrameworkHelper.h"
#include "Optional/XUSGObjLoader.h"
#include "Renderer.h"

using namespace std;
using namespace DirectX;
using namespace XUSG;

Renderer::Renderer(const Device& device) :
	m_device(device),
	m_frameParity(0)
{
	m_graphicsPipelineCache.SetDevice(device);
	m_pipelineLayoutCache.SetDevice(device);
}

Renderer::~Renderer()
{
}

bool Renderer::Init(const CommandList& commandList, uint32_t width, uint32_t height,
	vector<Resource>& uploaders, const char* fileName,
	Format rtFormat, Format dsFormat)
{
	m_viewport = XMUINT2(width, height);

	// Load inputs
	ObjLoader objLoader;
	if (!objLoader.Import(fileName, true, true)) return false;
	N_RETURN(createVB(commandList, objLoader.GetNumVertices(), objLoader.GetVertexStride(), objLoader.GetVertices(), uploaders), false);
	N_RETURN(createIB(commandList, objLoader.GetNumIndices(), objLoader.GetIndices(), uploaders), false);

	// Create pipelines
	N_RETURN(createInputLayout(), false);
	N_RETURN(createPipelineLayouts(), false);
	N_RETURN(createPipelines(rtFormat, dsFormat), false);

	return true;
}

void Renderer::UpdateFrame(double time, float timeStep, const XMFLOAT4& posScale,
	CXMMATRIX viewProj, bool isPaused)
{
	{
		static auto angle = 0.0f;
		const auto speed = static_cast<float>(sin(time) * 0.5 + 0.5) * 700.0f + 100.0f;
		angle += !isPaused ? speed * timeStep * XM_PI / 180.0f : 0.0f;
		const auto rot = XMMatrixRotationY(angle);

		const auto world = XMMatrixScaling(posScale.w, posScale.w, posScale.w) * rot *
			XMMatrixTranslation(posScale.x, posScale.y, posScale.z);

		m_cbBasePass.WorldViewProjPrev = m_cbBasePass.WorldViewProj;
		XMStoreFloat4x4(&m_cbBasePass.WorldViewProj, XMMatrixTranspose(world * viewProj));
		XMStoreFloat4x4(&m_cbBasePass.World, XMMatrixTranspose(world));
	}

	m_frameParity = !m_frameParity;
}

void Renderer::Render(const CommandList& commandList, const Descriptor& rtv,
	const Descriptor& dsv)
{
	// Set framebuffer
	commandList.OMSetRenderTargets(1, &rtv, &dsv);

	// Set pipeline state
	commandList.SetGraphicsPipelineLayout(m_pipelineLayout);
	commandList.SetPipelineState(m_pipeline);

	// Set viewport
	Viewport viewport(0.0f, 0.0f, static_cast<float>(m_viewport.x), static_cast<float>(m_viewport.y));
	RectRange scissorRect(0, 0, m_viewport.x, m_viewport.y);
	commandList.RSSetViewports(1, &viewport);
	commandList.RSSetScissorRects(1, &scissorRect);

	commandList.IASetPrimitiveTopology(PrimitiveTopology::TRIANGLELIST);

	// Set descriptor tables
	commandList.SetGraphics32BitConstants(0, SizeOfInUint32(m_cbBasePass), &m_cbBasePass);

	commandList.IASetVertexBuffers(0, 1, &m_vertexBuffer.GetVBV());
	commandList.IASetIndexBuffer(m_indexBuffer.GetIBV());

	commandList.DrawIndexed(m_numIndices, 1, 0, 0, 0);
}

const VertexBuffer& Renderer::GetVertexBuffer() const
{
	return m_vertexBuffer;
}

const IndexBuffer& Renderer::GetIndexBuffer() const
{
	return m_indexBuffer;
}

const InputLayout& Renderer::GetInputLayout() const
{
	return m_inputLayout;
}

const DirectX::XMFLOAT4X4& Renderer::GetWorldViewProj() const
{
	return m_cbBasePass.WorldViewProj;
}

const DirectX::XMFLOAT4X4& Renderer::GetWorld() const
{
	return m_cbBasePass.World;
}

uint32_t Renderer::GetNumIndices() const
{
	return m_numIndices;
}

bool Renderer::createVB(const CommandList& commandList, uint32_t numVert,
	uint32_t stride, const uint8_t* pData, vector<Resource>& uploaders)
{
	N_RETURN(m_vertexBuffer.Create(m_device, numVert, stride, ResourceFlag::NONE,
		MemoryType::DEFAULT, 1, nullptr, 1, nullptr, 1, nullptr, L"MeshVB"), false);
	uploaders.emplace_back();

	return m_vertexBuffer.Upload(commandList, uploaders.back(), pData,
		stride * numVert, 0, ResourceState::VERTEX_AND_CONSTANT_BUFFER |
		ResourceState::NON_PIXEL_SHADER_RESOURCE);
}

bool Renderer::createIB(const CommandList& commandList, uint32_t numIndices,
	const uint32_t* pData, vector<Resource>& uploaders)
{
	m_numIndices = numIndices;

	const uint32_t byteWidth = sizeof(uint32_t) * numIndices;
	N_RETURN(m_indexBuffer.Create(m_device, byteWidth, Format::R32_UINT, ResourceFlag::NONE,
		MemoryType::DEFAULT, 1, nullptr, 1, nullptr, 1, nullptr, L"MeshIB"), false);
	uploaders.emplace_back();

	return m_indexBuffer.Upload(commandList, uploaders.back(), pData,
		byteWidth, 0, ResourceState::INDEX_BUFFER);
}

bool Renderer::createInputLayout()
{
	// Define the vertex input layout.
	InputElementTable inputElementDescs =
	{
		{ "POSITION",	0, Format::R32G32B32_FLOAT, 0, 0,								InputClassification::PER_VERTEX_DATA, 0 },
		{ "NORMAL",		0, Format::R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,	InputClassification::PER_VERTEX_DATA, 0 }
	};

	X_RETURN(m_inputLayout, m_graphicsPipelineCache.CreateInputLayout(inputElementDescs), false);

	return true;
}

bool Renderer::createPipelineLayouts()
{
	// This is a pipeline layout for base pass
	Util::PipelineLayout pipelineLayout;
	pipelineLayout.SetConstants(0, SizeOfInUint32(BasePassConstants), 0, 0, Shader::Stage::VS);
	X_RETURN(m_pipelineLayout, pipelineLayout.GetPipelineLayout(m_pipelineLayoutCache,
		PipelineLayoutFlag::ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT, L"BasePassLayout"), false);

	return true;
}

bool Renderer::createPipelines(Format rtFormat, Format dsFormat)
{
	auto vsIndex = 0u;
	auto psIndex = 0u;

	// Base pass
	N_RETURN(m_shaderPool.CreateShader(Shader::Stage::VS, vsIndex, L"VSBasePass.cso"), false);
	N_RETURN(m_shaderPool.CreateShader(Shader::Stage::PS, psIndex, L"PSBasePass.cso"), false);

	Graphics::State state;
	state.IASetInputLayout(m_inputLayout);
	state.SetPipelineLayout(m_pipelineLayout);
	state.SetShader(Shader::Stage::VS, m_shaderPool.GetShader(Shader::Stage::VS, vsIndex));
	state.SetShader(Shader::Stage::PS, m_shaderPool.GetShader(Shader::Stage::PS, psIndex));
	state.IASetPrimitiveTopologyType(PrimitiveTopologyType::TRIANGLE);
	state.OMSetNumRenderTargets(1);
	state.OMSetRTVFormat(0, rtFormat);
	state.OMSetDSVFormat(dsFormat);
	X_RETURN(m_pipeline, state.GetPipeline(m_graphicsPipelineCache, L"BasePass"), false);

	return true;
}
