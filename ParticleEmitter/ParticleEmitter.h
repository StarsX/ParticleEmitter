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
		FAST_HYBRID_SMOKE,

		NUM_SIMULATION_METHOD
	};

	static const uint32_t FrameCount = 3;

	std::shared_ptr<XUSG::DescriptorTableCache> m_descriptorTableCache;

	XUSG::SwapChain			m_swapChain;
	XUSG::CommandAllocator	m_commandAllocators[FrameCount];
	XUSG::CommandQueue		m_commandQueue;

	XUSG::Device			m_device;
	XUSG::RenderTarget		m_renderTargets[FrameCount];
	XUSG::CommandList		m_commandList;

	// App resources.
	std::unique_ptr<Renderer> m_renderer;
	std::unique_ptr<Emitter> m_emitter;
	std::unique_ptr<FluidSPH> m_fluidSPH;
	std::unique_ptr<FluidFH> m_fluidFH;
	XUSG::DepthStencil	m_depth;
	XMFLOAT4X4	m_proj;
	XMFLOAT4X4	m_view;
	XMFLOAT3	m_focusPt;
	XMFLOAT3	m_eyePt;

	bool		m_typedUAV;

	// Synchronization objects.
	uint32_t	m_frameIndex;
	HANDLE		m_fenceEvent;
	XUSG::Fence	m_fence;
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

	void LoadPipeline();
	void LoadAssets();

	void PopulateCommandList();
	void WaitForGpu();
	void MoveToNextFrame();
	double CalculateFrameStats(float* fTimeStep = nullptr);

	static const wchar_t* SimulationMethodDescs[];
};
