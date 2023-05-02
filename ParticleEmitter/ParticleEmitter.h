//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

#pragma once

#include "DXFramework.h"
#include "StepTimer.h"
#include "Renderer.h"
#include "Emitter.h"
#include "FluidSPH.h"
#include "FluidFH.h"

using namespace DirectX;

// Note that while ComPtr is used to manage the lifetime of resources on the CPU,
// it has no understanding of the lifetime of resources on the GPU. Apps must account
// for the GPU lifetime of resources to avoid destroying objects that may still be
// referenced by the GPU.
// An example of this can be found in the class method: OnDestroy().

class ParticleEmitter : public DXFramework
{
public:
	ParticleEmitter(uint32_t width, uint32_t height, std::wstring name);
	virtual ~ParticleEmitter();

	virtual void OnInit();
	virtual void OnUpdate();
	virtual void OnRender();
	virtual void OnDestroy();

	virtual void OnKeyUp(uint8_t /*key*/);
	virtual void OnLButtonDown(float posX, float posY);
	virtual void OnLButtonUp(float posX, float posY);
	virtual void OnMouseMove(float posX, float posY);
	virtual void OnMouseWheel(float deltaZ, float posX, float posY);
	virtual void OnMouseLeave();

	virtual void ParseCommandLineArgs(wchar_t* argv[], int argc);

private:
	enum SimulationMethod : uint8_t
	{
		NO_SIMULATION,
		SPH_SIMULATION,
		FAST_HYBRID_FLUID,

		NUM_SIMULATION_METHOD
	};

	static const uint8_t FrameCount = Emitter::FrameCount;
	static_assert(FrameCount == Renderer::FrameCount, "ParticleEmitter::FrameCount should be equal to Renderer::FrameCount");

	XUSG::DescriptorTableLib::sptr	m_descriptorTableLib;

	XUSG::SwapChain::uptr			m_swapChain;
	XUSG::CommandAllocator::uptr	m_commandAllocators[FrameCount];
	XUSG::CommandQueue::uptr		m_commandQueue;

	XUSG::Device::uptr			m_device;
	XUSG::RenderTarget::uptr	m_renderTargets[FrameCount];
	XUSG::CommandList::uptr		m_commandList;

	// App resources.
	std::unique_ptr<Renderer>	m_renderer;
	std::unique_ptr<Emitter>	m_emitter;
	std::unique_ptr<FluidSPH>	m_fluidSPH;
	std::unique_ptr<FluidFH>	m_fluidFH;
	XUSG::DepthStencil::uptr	m_depth;
	XMFLOAT4X4	m_proj;
	XMFLOAT4X4	m_view;
	XMFLOAT3	m_focusPt;
	XMFLOAT3	m_eyePt;

	bool		m_typedUAV;

	// Screen-shot helper
	XUSG::Buffer::uptr	m_readBuffer;
	uint32_t			m_rowPitch;

	// Synchronization objects.
	uint32_t	m_frameIndex;
	HANDLE		m_fenceEvent;
	XUSG::Fence::uptr m_fence;
	uint64_t	m_fenceValues[FrameCount];

	// Application state
	bool		m_showFPS;
	bool		m_isPaused;
	StepTimer	m_timer;
	SimulationMethod m_simulationMethod;
	std::wstring m_simulationMethodDesc;

	// User camera interactions
	bool m_tracking;
	XMFLOAT2 m_mousePt;

	// User external settings
	std::string m_meshFileName;
	XMFLOAT4 m_meshPosScale;

	// Screen-shot state
	uint8_t m_screenShot;

	void LoadPipeline();
	void LoadAssets();

	void PopulateCommandList();
	void WaitForGpu();
	void MoveToNextFrame();
	void SaveImage(char const* fileName, XUSG::Buffer* imageBuffer,
		uint32_t w, uint32_t h, uint32_t rowPitch, uint8_t comp = 3);
	double CalculateFrameStats(float* fTimeStep = nullptr);

	static const wchar_t* SimulationMethodDescs[];
};
