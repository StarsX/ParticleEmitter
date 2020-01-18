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

#include "ParticleEmitter.h"

using namespace std;
using namespace XUSG;

const wchar_t* ParticleEmitter::SimulationMethodDescs[] =
{
	L"No internal force simulation",
	L"Smooth Particle Hydrodynamics",
	L"Fast particle-grid hybrid fluid"
};

const float g_FOVAngleY = XM_PIDIV4;
const float g_zNear = 1.0f;
const float g_zFar = 1000.0f;

ParticleEmitter::ParticleEmitter(uint32_t width, uint32_t height, std::wstring name) :
	DXFramework(width, height, name),
	m_typedUAV(false),
	m_frameIndex(0),
	m_showFPS(true),
	m_isPaused(false),
	m_simulationMethod(SPH_SIMULATION),
	m_tracking(false),
	m_meshFileName("Media/bunny.obj"),
	m_meshPosScale(0.0f, 0.0f, 0.0f, 1.0f)
{
#if defined (_DEBUG)
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
	AllocConsole();
	FILE* stream;
	freopen_s(&stream, "CONOUT$", "w+t", stdout);
	freopen_s(&stream, "CONIN$", "r+t", stdin);
#endif
}

ParticleEmitter::~ParticleEmitter()
{
#if defined (_DEBUG)
	FreeConsole();
#endif
}

void ParticleEmitter::OnInit()
{
	LoadPipeline();
	LoadAssets();
}

// Load the rendering pipeline dependencies.
void ParticleEmitter::LoadPipeline()
{
	auto dxgiFactoryFlags = 0u;

#if defined(_DEBUG)
	// Enable the debug layer (requires the Graphics Tools "optional feature").
	// NOTE: Enabling the debug layer after device creation will invalidate the active device.
	{
		com_ptr<ID3D12Debug1> debugController;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
		{
			debugController->EnableDebugLayer();
			//debugController->SetEnableGPUBasedValidation(TRUE);

			// Enable additional debug layers.
			dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
		}
	}
#endif

	com_ptr<IDXGIFactory4> factory;
	ThrowIfFailed(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory)));

	DXGI_ADAPTER_DESC1 dxgiAdapterDesc;
	com_ptr<IDXGIAdapter1> dxgiAdapter;
	auto hr = DXGI_ERROR_UNSUPPORTED;
	for (auto i = 0u; hr == DXGI_ERROR_UNSUPPORTED; ++i)
	{
		dxgiAdapter = nullptr;
		ThrowIfFailed(factory->EnumAdapters1(i, &dxgiAdapter));
		hr = D3D12CreateDevice(dxgiAdapter.get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device));
	}

	dxgiAdapter->GetDesc1(&dxgiAdapterDesc);
	if (dxgiAdapterDesc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
		m_title += dxgiAdapterDesc.VendorId == 0x1414 && dxgiAdapterDesc.DeviceId == 0x8c ? L" (WARP)" : L" (Software)";
	ThrowIfFailed(hr);

	D3D12_FEATURE_DATA_D3D12_OPTIONS featureData = {};
	hr = m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &featureData, sizeof(featureData));
	if (SUCCEEDED(hr))
	{
		// TypedUAVLoadAdditionalFormats contains a Boolean that tells you whether the feature is supported or not
		if (featureData.TypedUAVLoadAdditionalFormats)
		{
			// Can assume “all-or-nothing” subset is supported (e.g. R32G32B32A32_FLOAT)
			// Cannot assume other formats are supported, so we check:
			D3D12_FEATURE_DATA_FORMAT_SUPPORT formatSupport = { DXGI_FORMAT_B8G8R8A8_UNORM, D3D12_FORMAT_SUPPORT1_NONE, D3D12_FORMAT_SUPPORT2_NONE };
			hr = m_device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &formatSupport, sizeof(formatSupport));
			if (SUCCEEDED(hr) && (formatSupport.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD))
				m_typedUAV = true;
		}
	}

	// Create the command queue.
	N_RETURN(m_device->GetCommandQueue(m_commandQueue, CommandListType::DIRECT, CommandQueueFlags::NONE), ThrowIfFailed(E_FAIL));

	// Describe and create the swap chain.
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
	swapChainDesc.BufferCount = FrameCount;
	swapChainDesc.Width = m_width;
	swapChainDesc.Height = m_height;
	swapChainDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapChainDesc.SampleDesc.Count = 1;

	com_ptr<IDXGISwapChain1> swapChain;
	ThrowIfFailed(factory->CreateSwapChainForHwnd(
		m_commandQueue.get(),		// Swap chain needs the queue so that it can force a flush on it.
		Win32Application::GetHwnd(),
		&swapChainDesc,
		nullptr,
		nullptr,
		&swapChain
	));

	// This sample does not support fullscreen transitions.
	ThrowIfFailed(factory->MakeWindowAssociation(Win32Application::GetHwnd(), DXGI_MWA_NO_ALT_ENTER));

	ThrowIfFailed(swapChain.As(&m_swapChain));
	m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

	m_descriptorTableCache = make_shared<DescriptorTableCache>(m_device, L"DescriptorTableCache");

	// Create frame resources.
	// Create a RTV and a command allocator for each frame.
	for (auto n = 0u; n < FrameCount; n++)
	{
		N_RETURN(m_renderTargets[n].CreateFromSwapChain(m_device, m_swapChain, n), ThrowIfFailed(E_FAIL));
		N_RETURN(m_device->GetCommandAllocator(m_commandAllocators[n], CommandListType::DIRECT), ThrowIfFailed(E_FAIL));
	}

	// Create output views
	m_depth.Create(m_device, m_width, m_height, Format::D24_UNORM_S8_UINT,
		ResourceFlag::DENY_SHADER_RESOURCE, 1, 1, 1, 1.0f, 0, false, L"Depth");
}

// Load the sample assets.
void ParticleEmitter::LoadAssets()
{
	RawBuffer counter;
	N_RETURN(counter.Create(m_device, sizeof(uint32_t), ResourceFlag::DENY_SHADER_RESOURCE,
		MemoryType::READBACK, 0, nullptr, 0), ThrowIfFailed(E_FAIL));

	// Create the command list.
	N_RETURN(m_device->GetCommandList(m_commandList.GetCommandList(), 0, CommandListType::DIRECT,
		m_commandAllocators[m_frameIndex], nullptr), ThrowIfFailed(E_FAIL));

	vector<Resource> uploaders(0);
	// Create renderer
	m_renderer = make_unique<Renderer>(m_device);
	if (!m_renderer) ThrowIfFailed(E_FAIL);
	if (!m_renderer->Init(m_commandList, m_width, m_height, uploaders, m_meshFileName.c_str(),
		Format::B8G8R8A8_UNORM, Format::D24_UNORM_S8_UINT))
		ThrowIfFailed(E_FAIL);

	// Create emitter
	const auto numParticles = 1u << 16;
	m_emitter = make_unique<Emitter>(m_device);
	if (!m_emitter) ThrowIfFailed(E_FAIL);
	if (!m_emitter->Init(m_commandList, numParticles, m_descriptorTableCache, uploaders,
		m_renderer->GetInputLayout(), Format::B8G8R8A8_UNORM, Format::D24_UNORM_S8_UINT))
		ThrowIfFailed(E_FAIL);

	// Create SPH fluid simulator
	m_fluidSPH = make_unique<FluidSPH>(m_device);
	if (!m_fluidSPH) ThrowIfFailed(E_FAIL);
	if (!m_fluidSPH->Init(m_commandList, numParticles, m_descriptorTableCache, m_emitter->GetParticleBuffers()))
		ThrowIfFailed(E_FAIL);

	// Create fast hybrid fluid simulator
	m_fluidFH = make_unique<FluidFH>(m_device);
	if (!m_fluidFH) ThrowIfFailed(E_FAIL);
	if (!m_fluidFH->Init(m_commandList, numParticles, m_descriptorTableCache, uploaders))
		ThrowIfFailed(E_FAIL);

#if defined(_DEBUG)
	ComputeUtil prefixSumUtil(m_device);
	prefixSumUtil.SetPrefixSum(m_commandList, true, m_descriptorTableCache,
		nullptr, &uploaders, Format::R32_UINT, 1024 * 5 + 387);
#endif

	m_emitter->Distribute(m_commandList, counter, m_renderer->GetVertexBuffer(),
		m_renderer->GetIndexBuffer(), m_renderer->GetNumIndices(), 32.0f, m_meshPosScale.w);

#if defined(_DEBUG)
	prefixSumUtil.PrefixSum(m_commandList);
#endif

	// Close the command list and execute it to begin the initial GPU setup.
	ThrowIfFailed(m_commandList.Close());
	BaseCommandList* ppCommandLists[] = { m_commandList.GetCommandList().get() };
	m_commandQueue->ExecuteCommandLists(static_cast<uint32_t>(size(ppCommandLists)), ppCommandLists);

	// Create synchronization objects and wait until assets have been uploaded to the GPU.
	{
		N_RETURN(m_device->GetFence(m_fence, m_fenceValues[m_frameIndex]++, FenceFlag::NONE), ThrowIfFailed(E_FAIL));

		// Create an event handle to use for frame synchronization.
		m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		if (m_fenceEvent == nullptr)
		{
			ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
		}

		// Wait for the command list to execute; we are reusing the same command 
		// list in our main loop but for now, we just want to wait for setup to 
		// complete before continuing.
		WaitForGpu();
	}

#if defined(_DEBUG)
	prefixSumUtil.VerifyPrefixSum();
#endif

	// Shrink memory cost
	Resource pEmitterSource[1];
	ThrowIfFailed(m_commandAllocators[m_frameIndex]->Reset());
	ThrowIfFailed(m_commandList.Reset(m_commandAllocators[m_frameIndex], nullptr));
	N_RETURN(m_emitter->SetEmitterCount(m_commandList, counter, pEmitterSource), ThrowIfFailed(E_FAIL));

	// Close the command list and execute it to begin the initial GPU setup.
	ThrowIfFailed(m_commandList.Close());
	*ppCommandLists = m_commandList.GetCommandList().get();
	m_commandQueue->ExecuteCommandLists(static_cast<uint32_t>(size(ppCommandLists)), ppCommandLists);

	// Create synchronization objects and wait until assets have been uploaded to the GPU.
	WaitForGpu();

	// Projection
	const auto aspectRatio = m_width / static_cast<float>(m_height);
	const auto proj = XMMatrixPerspectiveFovLH(g_FOVAngleY, aspectRatio, g_zNear, g_zFar);
	XMStoreFloat4x4(&m_proj, proj);

	// View initialization
	m_focusPt = XMFLOAT3(0.0f, 4.0f, 0.0f);
	m_eyePt = XMFLOAT3(4.0f, 16.0f, -40.0f);
	const auto focusPt = XMLoadFloat3(&m_focusPt);
	const auto eyePt = XMLoadFloat3(&m_eyePt);
	const auto view = XMMatrixLookAtLH(eyePt, focusPt, XMVectorSet(0.0f, 1.0f, 0.0f, 1.0f));
	XMStoreFloat4x4(&m_view, view);
}

// Update frame-based values.
void ParticleEmitter::OnUpdate()
{
	// Timer
	static auto time = 0.0, pauseTime = 0.0;

	m_timer.Tick();
	float timeStep;
	const auto totalTime = CalculateFrameStats(&timeStep);
	pauseTime = m_isPaused ? totalTime - time : pauseTime;
	timeStep = m_isPaused ? 0.0f : timeStep;
	time = totalTime - pauseTime;

	// View
	const auto eyePt = XMLoadFloat3(&m_eyePt);
	const auto view = XMLoadFloat4x4(&m_view);
	const auto proj = XMLoadFloat4x4(&m_proj);
	const auto viewProj = view * proj;
	m_renderer->UpdateFrame(time, timeStep, m_meshPosScale, viewProj, m_isPaused);
	m_emitter->UpdateFrame(time, timeStep, viewProj);
	switch (m_simulationMethod)
	{
	case SPH_SIMULATION:
		m_fluidSPH->UpdateFrame();
		break;
	case FAST_HYBRID_FLUID:
		m_fluidFH->UpdateFrame();
		break;
	}
}

// Render the scene.
void ParticleEmitter::OnRender()
{
	// Record all the commands we need to render the scene into the command list.
	PopulateCommandList();

	// Execute the command list.
	BaseCommandList* const ppCommandLists[] = { m_commandList.GetCommandList().get() };
	m_commandQueue->ExecuteCommandLists(static_cast<uint32_t>(size(ppCommandLists)), ppCommandLists);

	// Present the frame.
	ThrowIfFailed(m_swapChain->Present(0, 0));

	MoveToNextFrame();
}

void ParticleEmitter::OnDestroy()
{
	// Ensure that the GPU is no longer referencing resources that are about to be
	// cleaned up by the destructor.
	WaitForGpu();

	CloseHandle(m_fenceEvent);
}

// User hot-key interactions.
void ParticleEmitter::OnKeyUp(uint8_t key)
{
	switch (key)
	{
	case 0x20:	// case VK_SPACE:
		m_isPaused = !m_isPaused;
		break;
	case 0x70:	//case VK_F1:
		m_showFPS = !m_showFPS;
		break;
	case 'S':
		m_simulationMethod = static_cast<SimulationMethod>((m_simulationMethod + 1) % NUM_SIMULATION_METHOD);
		break;
	}
}

// User camera interactions.
void ParticleEmitter::OnLButtonDown(float posX, float posY)
{
	m_tracking = true;
	m_mousePt = XMFLOAT2(posX, posY);
}

void ParticleEmitter::OnLButtonUp(float posX, float posY)
{
	m_tracking = false;
}

void ParticleEmitter::OnMouseMove(float posX, float posY)
{
	if (m_tracking)
	{
		const auto dPos = XMFLOAT2(m_mousePt.x - posX, m_mousePt.y - posY);

		XMFLOAT2 radians;
		radians.x = XM_2PI * dPos.y / m_height;
		radians.y = XM_2PI * dPos.x / m_width;

		const auto focusPt = XMLoadFloat3(&m_focusPt);
		auto eyePt = XMLoadFloat3(&m_eyePt);

		const auto len = XMVectorGetX(XMVector3Length(focusPt - eyePt));
		auto transform = XMMatrixTranslation(0.0f, 0.0f, -len);
		transform *= XMMatrixRotationRollPitchYaw(radians.x, radians.y, 0.0f);
		transform *= XMMatrixTranslation(0.0f, 0.0f, len);

		const auto view = XMLoadFloat4x4(&m_view) * transform;
		const auto viewInv = XMMatrixInverse(nullptr, view);
		eyePt = viewInv.r[3];

		XMStoreFloat3(&m_eyePt, eyePt);
		XMStoreFloat4x4(&m_view, view);

		m_mousePt = XMFLOAT2(posX, posY);
	}
}

void ParticleEmitter::OnMouseWheel(float deltaZ, float posX, float posY)
{
	const auto focusPt = XMLoadFloat3(&m_focusPt);
	auto eyePt = XMLoadFloat3(&m_eyePt);

	const auto len = XMVectorGetX(XMVector3Length(focusPt - eyePt));
	const auto transform = XMMatrixTranslation(0.0f, 0.0f, -len * deltaZ / 16.0f);

	const auto view = XMLoadFloat4x4(&m_view) * transform;
	const auto viewInv = XMMatrixInverse(nullptr, view);
	eyePt = viewInv.r[3];

	XMStoreFloat3(&m_eyePt, eyePt);
	XMStoreFloat4x4(&m_view, view);
}

void ParticleEmitter::OnMouseLeave()
{
	m_tracking = false;
}

void ParticleEmitter::ParseCommandLineArgs(wchar_t* argv[], int argc)
{
	wstring_convert<codecvt_utf8<wchar_t>> converter;
	DXFramework::ParseCommandLineArgs(argv, argc);

	for (auto i = 1; i < argc; ++i)
	{
		if (_wcsnicmp(argv[i], L"-mesh", wcslen(argv[i])) == 0 ||
			_wcsnicmp(argv[i], L"/mesh", wcslen(argv[i])) == 0)
		{
			if (i + 1 < argc) m_meshFileName = converter.to_bytes(argv[i + 1]);
			m_meshPosScale.x = i + 2 < argc ? static_cast<float>(_wtof(argv[i + 2])) : m_meshPosScale.x;
			m_meshPosScale.y = i + 3 < argc ? static_cast<float>(_wtof(argv[i + 3])) : m_meshPosScale.y;
			m_meshPosScale.z = i + 4 < argc ? static_cast<float>(_wtof(argv[i + 4])) : m_meshPosScale.z;
			m_meshPosScale.w = i + 5 < argc ? static_cast<float>(_wtof(argv[i + 5])) : m_meshPosScale.w;
		}
	}
}

void ParticleEmitter::PopulateCommandList()
{
	// Command list allocators can only be reset when the associated 
	// command lists have finished execution on the GPU; apps should use 
	// fences to determine GPU execution progress.
	ThrowIfFailed(m_commandAllocators[m_frameIndex]->Reset());

	// However, when ExecuteCommandList() is called on a particular command 
	// list, that command list can then be reset at any time and must be before 
	// re-recording.
	ThrowIfFailed(m_commandList.Reset(m_commandAllocators[m_frameIndex], nullptr));

	// Record commands.
	ResourceBarrier barriers[1];
	auto numBarriers = m_renderTargets[m_frameIndex].SetBarrier(barriers, ResourceState::RENDER_TARGET);
	m_commandList.Barrier(numBarriers, barriers);

	// Clear render target
	const float clearColor[4] = { 0.2f, 0.2f, 0.2f, 0.0f };
	m_commandList.ClearRenderTargetView(m_renderTargets[m_frameIndex].GetRTV(), clearColor);
	m_commandList.ClearDepthStencilView(m_depth.GetDSV(), ClearFlag::DEPTH, 1.0f);
	
	m_renderer->Render(m_commandList, m_renderTargets[m_frameIndex].GetRTV(), m_depth.GetDSV());

	// Fluid simulation
	switch (m_simulationMethod)
	{
	case SPH_SIMULATION:
		m_emitter->RenderSPH(m_commandList, m_renderTargets[m_frameIndex].GetRTV(), &m_depth.GetDSV(),
			m_fluidSPH->GetDescriptorTable(), m_renderer->GetWorld());
		m_fluidSPH->Simulate(m_commandList);
		break;
	case FAST_HYBRID_FLUID:
		m_emitter->RenderFHF(m_commandList, m_renderTargets[m_frameIndex].GetRTV(), &m_depth.GetDSV(),
			m_fluidFH->GetDescriptorTable(), m_renderer->GetWorld());
		m_fluidFH->Simulate(m_commandList);
		break;
	default:
		m_emitter->Render(m_commandList, m_renderTargets[m_frameIndex].GetRTV(),
			&m_depth.GetDSV(), m_renderer->GetWorld());
	}

	// Indicate that the back buffer will now be used to present.
	numBarriers = m_renderTargets[m_frameIndex].SetBarrier(barriers, ResourceState::PRESENT);
	m_commandList.Barrier(numBarriers, barriers);

	ThrowIfFailed(m_commandList.Close());
}

// Wait for pending GPU work to complete.
void ParticleEmitter::WaitForGpu()
{
	// Schedule a Signal command in the queue.
	ThrowIfFailed(m_commandQueue->Signal(m_fence.get(), m_fenceValues[m_frameIndex]));

	// Wait until the fence has been processed.
	ThrowIfFailed(m_fence->SetEventOnCompletion(m_fenceValues[m_frameIndex], m_fenceEvent));
	WaitForSingleObjectEx(m_fenceEvent, INFINITE, FALSE);

	// Increment the fence value for the current frame.
	m_fenceValues[m_frameIndex]++;
}

// Prepare to render the next frame.
void ParticleEmitter::MoveToNextFrame()
{
	// Schedule a Signal command in the queue.
	const auto currentFenceValue = m_fenceValues[m_frameIndex];
	ThrowIfFailed(m_commandQueue->Signal(m_fence.get(), currentFenceValue));

	// Update the frame index.
	m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

	// If the next frame is not ready to be rendered yet, wait until it is ready.
	if (m_fence->GetCompletedValue() < m_fenceValues[m_frameIndex])
	{
		ThrowIfFailed(m_fence->SetEventOnCompletion(m_fenceValues[m_frameIndex], m_fenceEvent));
		WaitForSingleObjectEx(m_fenceEvent, INFINITE, FALSE);
	}

	// Set the fence value for the next frame.
	m_fenceValues[m_frameIndex] = currentFenceValue + 1;
}

double ParticleEmitter::CalculateFrameStats(float* pTimeStep)
{
	static int frameCnt = 0;
	static double elapsedTime = 0.0;
	static double previousTime = 0.0;
	const auto totalTime = m_timer.GetTotalSeconds();
	++frameCnt;

	const auto timeStep = static_cast<float>(totalTime - elapsedTime);

	// Compute averages over one second period.
	if ((totalTime - elapsedTime) >= 1.0f)
	{
		float fps = static_cast<float>(frameCnt) / timeStep;	// Normalize to an exact second.

		frameCnt = 0;
		elapsedTime = totalTime;

		wstringstream windowText;
		windowText << L"    fps: ";
		if (m_showFPS) windowText << setprecision(2) << fixed << fps;
		else windowText << L"[F1]";
		windowText << L"    [S] " << SimulationMethodDescs[m_simulationMethod];
		SetCustomWindowText(windowText.str().c_str());
	}

	if (pTimeStep)* pTimeStep = static_cast<float>(totalTime - previousTime);
	previousTime = totalTime;

	return totalTime;
}
