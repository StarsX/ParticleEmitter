//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#include "Optional/XUSGObjLoader.h"
#include "Renderer.h"

using namespace std;
using namespace DirectX;
using namespace XUSG;

struct CBBasePass
{
	DirectX::XMFLOAT4X4	WorldViewProj;
	DirectX::XMFLOAT4X4	WorldViewProjPrev;
	DirectX::XMFLOAT3X4	World;
};

Renderer::Renderer() :
	m_frameParity(0)
{
	m_shaderLib = ShaderLib::MakeUnique();
	
}

Renderer::~Renderer()
{
}

bool Renderer::Init(CommandList* pCommandList, uint32_t width, uint32_t height,
	vector<Resource::uptr>& uploaders, const char* fileName,
	Format rtFormat, Format dsFormat)
{
	const auto pDevice = pCommandList->GetDevice();
	m_graphicsPipelineLib = Graphics::PipelineLib::MakeUnique(pDevice);
	m_pipelineLayoutLib = PipelineLayoutLib::MakeUnique(pDevice);

	m_viewport = XMUINT2(width, height);

	// Load inputs
	ObjLoader objLoader;
	if (!objLoader.Import(fileName, true, true)) return false;
	XUSG_N_RETURN(createVB(pCommandList, objLoader.GetNumVertices(), objLoader.GetVertexStride(), objLoader.GetVertices(), uploaders), false);
	XUSG_N_RETURN(createIB(pCommandList, objLoader.GetNumIndices(), objLoader.GetIndices(), uploaders), false);

	// Create constant buffer
	m_cbBasePass = ConstantBuffer::MakeUnique();
	XUSG_N_RETURN(m_cbBasePass->Create(pDevice, sizeof(CBBasePass[FrameCount]), FrameCount,
		nullptr, MemoryType::UPLOAD, MemoryFlag::NONE, L"CBBasePass"), false);

	// Create pipelines
	XUSG_N_RETURN(createInputLayout(), false);
	XUSG_N_RETURN(createPipelineLayouts(), false);
	XUSG_N_RETURN(createPipelines(rtFormat, dsFormat), false);

	return true;
}

void Renderer::UpdateFrame(uint8_t frameIndex, double time, float timeStep,
	const XMFLOAT4& posScale, CXMMATRIX viewProj, bool isPaused)
{
	{
		static auto angle = 0.0f;
		const auto speed = static_cast<float>(sin(time) * 0.5 + 0.5) * 700.0f + 100.0f;
		angle += !isPaused ? speed * timeStep * XM_PI / 180.0f : 0.0f;
		const auto rot = XMMatrixRotationY(angle);

		auto movSpeed = XM_PI * 0.25f;
		movSpeed = (min)(movSpeed / sqrt(speed), movSpeed);
		XMFLOAT3 pos(posScale.x, posScale.y, posScale.z);
		pos.x += static_cast<float>(cos(time * movSpeed)) * 4.0f;
		pos.z += static_cast<float>(sin(time * movSpeed)) * 4.0f;
		pos.y += static_cast<float>(sin(time * movSpeed) * 0.5 + 0.5) * 4.0f;

		const auto world = XMMatrixScaling(posScale.w, posScale.w, posScale.w) * rot *
			XMMatrixTranslation(pos.x, pos.y, pos.z);

		const auto pCbData = reinterpret_cast<CBBasePass*>(m_cbBasePass->Map(frameIndex));
		pCbData->WorldViewProjPrev = m_worldViewProj;
		XMStoreFloat4x4(&pCbData->WorldViewProj, XMMatrixTranspose(world * viewProj));
		XMStoreFloat3x4(&pCbData->World, world);
		m_worldViewProj = pCbData->WorldViewProj;
		m_world = pCbData->World;
	}

	m_frameParity = !m_frameParity;
}

void Renderer::Render(const CommandList* pCommandList, uint8_t frameIndex,
	const Descriptor& rtv, const Descriptor& dsv)
{
	// Set framebuffer
	pCommandList->OMSetRenderTargets(1, &rtv, &dsv);

	// Set pipeline state
	pCommandList->SetGraphicsPipelineLayout(m_pipelineLayout);
	pCommandList->SetPipelineState(m_pipeline);

	// Set viewport
	Viewport viewport(0.0f, 0.0f, static_cast<float>(m_viewport.x), static_cast<float>(m_viewport.y));
	RectRange scissorRect(0, 0, m_viewport.x, m_viewport.y);
	pCommandList->RSSetViewports(1, &viewport);
	pCommandList->RSSetScissorRects(1, &scissorRect);

	pCommandList->IASetPrimitiveTopology(PrimitiveTopology::TRIANGLELIST);

	// Set descriptor tables
	pCommandList->SetGraphicsRootConstantBufferView(0, m_cbBasePass.get(), m_cbBasePass->GetCBVOffset(frameIndex));

	pCommandList->IASetVertexBuffers(0, 1, &m_vertexBuffer->GetVBV());
	pCommandList->IASetIndexBuffer(m_indexBuffer->GetIBV());

	pCommandList->DrawIndexed(m_numIndices, 1, 0, 0, 0);
}

const VertexBuffer* Renderer::GetVertexBuffer() const
{
	return m_vertexBuffer.get();
}

const IndexBuffer* Renderer::GetIndexBuffer() const
{
	return m_indexBuffer.get();
}

const InputLayout* Renderer::GetInputLayout() const
{
	return m_pInputLayout;
}

const DirectX::XMFLOAT3X4& Renderer::GetWorld() const
{
	return m_world;
}

uint32_t Renderer::GetNumIndices() const
{
	return m_numIndices;
}

bool Renderer::createVB(CommandList* pCommandList, uint32_t numVert, uint32_t stride,
	const uint8_t* pData, vector<Resource::uptr>& uploaders)
{
	m_vertexBuffer = VertexBuffer::MakeUnique();
	XUSG_N_RETURN(m_vertexBuffer->Create(pCommandList->GetDevice(), numVert, stride, ResourceFlag::NONE,
		MemoryType::DEFAULT, 1, nullptr, 1, nullptr, 1, nullptr, MemoryFlag::NONE, L"MeshVB"), false);
	uploaders.emplace_back(Resource::MakeUnique());

	return m_vertexBuffer->Upload(pCommandList, uploaders.back().get(), pData,
		stride * numVert, 0, ResourceState::VERTEX_AND_CONSTANT_BUFFER |
		ResourceState::NON_PIXEL_SHADER_RESOURCE);
}

bool Renderer::createIB(CommandList* pCommandList, uint32_t numIndices,
	const uint32_t* pData, vector<Resource::uptr>& uploaders)
{
	m_numIndices = numIndices;

	const uint32_t byteWidth = sizeof(uint32_t) * numIndices;
	m_indexBuffer = IndexBuffer::MakeUnique();
	XUSG_N_RETURN(m_indexBuffer->Create(pCommandList->GetDevice(), byteWidth, Format::R32_UINT, ResourceFlag::NONE,
		MemoryType::DEFAULT, 1, nullptr, 1, nullptr, 1, nullptr, MemoryFlag::NONE, L"MeshIB"), false);
	uploaders.emplace_back(Resource::MakeUnique());

	return m_indexBuffer->Upload(pCommandList, uploaders.back().get(), pData,
		byteWidth, 0, ResourceState::INDEX_BUFFER);
}

bool Renderer::createInputLayout()
{
	// Define the vertex input layout.
	const InputElement inputElements[] =
	{
		{ "POSITION",	0, Format::R32G32B32_FLOAT, 0, 0,								InputClassification::PER_VERTEX_DATA, 0 },
		{ "NORMAL",		0, Format::R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,	InputClassification::PER_VERTEX_DATA, 0 }
	};

	XUSG_X_RETURN(m_pInputLayout, m_graphicsPipelineLib->CreateInputLayout(inputElements, static_cast<uint32_t>(size(inputElements))), false);

	return true;
}

bool Renderer::createPipelineLayouts()
{
	// This is a pipeline layout for base pass
	const auto pipelineLayout = Util::PipelineLayout::MakeUnique();
	pipelineLayout->SetRootCBV(0, 0, 0, Shader::Stage::VS);
	XUSG_X_RETURN(m_pipelineLayout, pipelineLayout->GetPipelineLayout(m_pipelineLayoutLib.get(),
		PipelineLayoutFlag::ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT, L"BasePassLayout"), false);

	return true;
}

bool Renderer::createPipelines(Format rtFormat, Format dsFormat)
{
	auto vsIndex = 0u;
	auto psIndex = 0u;

	// Base pass
	XUSG_N_RETURN(m_shaderLib->CreateShader(Shader::Stage::VS, vsIndex, L"VSBasePass.cso"), false);
	XUSG_N_RETURN(m_shaderLib->CreateShader(Shader::Stage::PS, psIndex, L"PSBasePass.cso"), false);

	const auto state = Graphics::State::MakeUnique();
	state->IASetInputLayout(m_pInputLayout);
	state->SetPipelineLayout(m_pipelineLayout);
	state->SetShader(Shader::Stage::VS, m_shaderLib->GetShader(Shader::Stage::VS, vsIndex));
	state->SetShader(Shader::Stage::PS, m_shaderLib->GetShader(Shader::Stage::PS, psIndex));
	state->IASetPrimitiveTopologyType(PrimitiveTopologyType::TRIANGLE);
	state->OMSetNumRenderTargets(1);
	state->OMSetRTVFormat(0, rtFormat);
	state->OMSetDSVFormat(dsFormat);
	XUSG_X_RETURN(m_pipeline, state->GetPipeline(m_graphicsPipelineLib.get(), L"BasePass"), false);

	return true;
}
