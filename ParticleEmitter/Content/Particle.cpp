//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#include "Particle.h"

using namespace std;
using namespace DirectX;
using namespace XUSG;

Particle::Particle(const Device& device) :
	m_device(device)
{
	m_graphicsPipelineCache.SetDevice(device);
	m_computePipelineCache.SetDevice(device);
	m_pipelineLayoutCache.SetDevice(device);
}

Particle::~Particle()
{
}

bool Particle::Init(const CommandList& commandList, uint32_t numParticles,
	shared_ptr<DescriptorTableCache> descriptorTableCache,
	Format rtFormat, Format dsFormat)
{
	m_numParticles = numParticles;
	m_descriptorTableCache = descriptorTableCache;

	// Create resources and pipelines
	N_RETURN(m_particleBuffer.Create(m_device, numParticles, sizeof(ParticleInfo),
		ResourceFlag::ALLOW_UNORDERED_ACCESS, MemoryType::DEFAULT, 1,
		nullptr, 1, nullptr, L"ParticleBuffer"), false);

	N_RETURN(createPipelineLayouts(), false);
	N_RETURN(createPipelines(rtFormat, dsFormat), false);
	N_RETURN(createDescriptorTables(), false);

	return true;
}

void Particle::UpdateFrame(double time, float timeStep, const CXMMATRIX viewProj)
{
	m_time = time;
	m_timeStep = timeStep;
	XMStoreFloat4x4(&m_viewProj, XMMatrixTranspose(viewProj));
}

void Particle::Render(const CommandList& commandList, const Descriptor& rtv,
	const Descriptor* pDsv)
{
	CBPerFrame cb;
	cb.ViewProj = m_viewProj;
	cb.TimeStep = m_timeStep;

	const DescriptorPool descriptorPools[] =
	{
		m_descriptorTableCache->GetDescriptorPool(CBV_SRV_UAV_POOL),
	};
	commandList.SetDescriptorPools(static_cast<uint32_t>(size(descriptorPools)), descriptorPools);

	commandList.OMSetRenderTargets(1, &rtv, pDsv);

	// Set pipeline state
	commandList.SetGraphicsPipelineLayout(m_pipelineLayouts[0]);
	commandList.SetPipelineState(m_pipelines[0]);

	commandList.IASetPrimitiveTopology(PrimitiveTopology::POINTLIST);

	// Set descriptor tables
	commandList.SetGraphics32BitConstants(0, SizeOfInUint32(cb), &cb);
	commandList.SetGraphicsDescriptorTable(1, m_uavTable);

	commandList.Draw(m_numParticles, 1, 0, 0);
}

const DescriptorTable& Particle::GetParticleBufferUAVTable() const
{
	return m_uavTable;
}

uint32_t Particle::GetParticleCount() const
{
	return m_numParticles;
}

bool Particle::createPipelineLayouts()
{
	// Particle rendering
	{
		Util::PipelineLayout pipelineLayout;
		pipelineLayout.SetConstants(0, SizeOfInUint32(CBPerFrame), 0, 0, Shader::Stage::VS);
		pipelineLayout.SetRange(1, DescriptorType::UAV, 1, 0, 0);
		X_RETURN(m_pipelineLayouts[0], pipelineLayout.GetPipelineLayout(m_pipelineLayoutCache,
			PipelineLayoutFlag::NONE, L"ParticleRenderLayout"), false);
	}

	return true;
}

bool Particle::createPipelines(Format rtFormat, Format dsFormat)
{
	auto vsIndex = 0u;
	auto psIndex = 0u;

	// Particle rendering
	{
		N_RETURN(m_shaderPool.CreateShader(Shader::Stage::VS, vsIndex, L"VSParticle.cso"), false);
		N_RETURN(m_shaderPool.CreateShader(Shader::Stage::PS, psIndex, L"PSConstColor.cso"), false);

		Graphics::State state;
		state.SetPipelineLayout(m_pipelineLayouts[0]);
		state.SetShader(Shader::Stage::VS, m_shaderPool.GetShader(Shader::Stage::VS, vsIndex++));
		state.SetShader(Shader::Stage::PS, m_shaderPool.GetShader(Shader::Stage::PS, psIndex++));
		state.IASetPrimitiveTopologyType(PrimitiveTopologyType::POINT);
		state.OMSetNumRenderTargets(1);
		state.OMSetRTVFormat(0, rtFormat);
		state.OMSetDSVFormat(dsFormat);
		X_RETURN(m_pipelines[0], state.GetPipeline(m_graphicsPipelineCache, L"Visualization"), false);
	}

	return true;
}

bool Particle::createDescriptorTables()
{
	// Create UAV table
	{
		Util::DescriptorTable uavTable;
		uavTable.SetDescriptors(0, 1, &m_particleBuffer.GetUAV());
		X_RETURN(m_uavTable, uavTable.GetCbvSrvUavTable(*m_descriptorTableCache), false);
	}

	return true;
}
