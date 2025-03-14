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
#include "stb_image_write.h"

using namespace std;
using namespace XUSG;

const wchar_t* ParticleEmitter::SimulationMethodDescs[] =
{
	L"No internal force simulation",
	L"Smooth Particle Hydrodynamics",
	L"Fast particle-grid hybrid fluid",
	L"Fast particle-grid hybrid smoke"
};

const float g_FOVAngleY = XM_PIDIV4;
const float g_zNear = 1.0f;
const float g_zFar = 1000.0f;

const auto g_backBufferFormat = Format::R8G8B8A8_UNORM;

ParticleEmitter::ParticleEmitter(uint32_t width, uint32_t height, std::wstring name) :
	DXFramework(width, height, name),
	m_typedUAV(false),
	m_frameIndex(0),
	m_deviceType(DEVICE_DISCRETE),
	m_simulationMethod(SPH_SIMULATION),
	m_showFPS(true),
	m_isPaused(false),
	m_tracking(false),
	m_meshFileName("Assets/bunny.obj"),
	m_meshPosScale(0.0f, 0.0f, 0.0f, 1.0f),
	m_screenShot(0)
{
#if defined (_DEBUG)
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
	AllocConsole();
	FILE* stream;
	freopen_s(&stream, "CONIN$", "r+t", stdin);
	freopen_s(&stream, "CONOUT$", "w+t", stdout);
	freopen_s(&stream, "CONOUT$", "w+t", stderr);
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

	com_ptr<IDXGIFactory5> factory;
	ThrowIfFailed(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory)));

	DXGI_ADAPTER_DESC1 dxgiAdapterDesc;
	com_ptr<IDXGIAdapter1> dxgiAdapter;
	const auto useUMA = m_deviceType == DEVICE_UMA;
	const auto useWARP = m_deviceType == DEVICE_WARP;
	auto checkUMA = true, checkWARP = true;
	auto hr = DXGI_ERROR_NOT_FOUND;
	for (uint8_t n = 0; n < 3; ++n)
	{
		if (FAILED(hr)) hr = DXGI_ERROR_UNSUPPORTED;
		for (auto i = 0u; hr == DXGI_ERROR_UNSUPPORTED; ++i)
		{
			dxgiAdapter = nullptr;
			hr = factory->EnumAdapters1(i, &dxgiAdapter);

			if (SUCCEEDED(hr) && dxgiAdapter)
			{
				dxgiAdapter->GetDesc1(&dxgiAdapterDesc);
				if (checkWARP) hr = dxgiAdapterDesc.VendorId == 0x1414 && dxgiAdapterDesc.DeviceId == 0x8c ?
					(useWARP ? hr : DXGI_ERROR_UNSUPPORTED) : (useWARP ? DXGI_ERROR_UNSUPPORTED : hr);
			}

			if (SUCCEEDED(hr))
			{
				m_device = Device::MakeUnique();
				if (SUCCEEDED(m_device->Create(dxgiAdapter.get(), D3D_FEATURE_LEVEL_11_0)) && checkUMA)
				{
					D3D12_FEATURE_DATA_ARCHITECTURE feature = {};
					const auto pDevice = static_cast<ID3D12Device*>(m_device->GetHandle());
					if (SUCCEEDED(pDevice->CheckFeatureSupport(D3D12_FEATURE_ARCHITECTURE, &feature, sizeof(feature))))
						hr = feature.UMA ? (useUMA ? hr : DXGI_ERROR_UNSUPPORTED) : (useUMA ? DXGI_ERROR_UNSUPPORTED : hr);
				}
			}
		}

		checkUMA = false;
		if (n) checkWARP = false;
	}

	if (dxgiAdapterDesc.VendorId == 0x1414 && dxgiAdapterDesc.DeviceId == 0x8c) m_title += L" (WARP)";
	else if (dxgiAdapterDesc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) m_title += L" (Software)";
	//else m_title += wstring(L" - ") + dxgiAdapterDesc.Description;
	ThrowIfFailed(hr);

	D3D12_FEATURE_DATA_D3D12_OPTIONS featureData = {};
	const auto pDevice = static_cast<ID3D12Device*>(m_device->GetHandle());
	hr = pDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &featureData, sizeof(featureData));
	if (SUCCEEDED(hr))
	{
		// TypedUAVLoadAdditionalFormats contains a Boolean that tells you whether the feature is supported or not
		if (featureData.TypedUAVLoadAdditionalFormats)
		{
			// Can assume �all-or-nothing� subset is supported (e.g. R32G32B32A32_FLOAT)
			// Cannot assume other formats are supported, so we check:
			D3D12_FEATURE_DATA_FORMAT_SUPPORT formatSupport = { DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_FORMAT_SUPPORT1_NONE, D3D12_FORMAT_SUPPORT2_NONE };
			hr = pDevice->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &formatSupport, sizeof(formatSupport));
			if (SUCCEEDED(hr) && (formatSupport.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD))
				m_typedUAV = true;
		}
	}

	// Create the command queue.
	m_commandQueue = CommandQueue::MakeUnique();
	XUSG_N_RETURN(m_commandQueue->Create(m_device.get(), CommandListType::DIRECT, CommandQueueFlag::NONE,
		0, 0, L"CommandQueue"), ThrowIfFailed(E_FAIL));

	// Describe and create the swap chain.
	m_swapChain = SwapChain::MakeUnique();
	XUSG_N_RETURN(m_swapChain->Create(factory.get(), Win32Application::GetHwnd(), m_commandQueue->GetHandle(),
		FrameCount, m_width, m_height, g_backBufferFormat, SwapChainFlag::ALLOW_TEARING), ThrowIfFailed(E_FAIL));

	// This sample does not support fullscreen transitions.
	ThrowIfFailed(factory->MakeWindowAssociation(Win32Application::GetHwnd(), DXGI_MWA_NO_ALT_ENTER));

	m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

	m_descriptorTableLib = DescriptorTableLib::MakeShared(m_device.get(), L"DescriptorTableLib");

	// Create frame resources.
	// Create a RTV and a command allocator for each frame.
	for (uint8_t n = 0; n < FrameCount; ++n)
	{
		m_renderTargets[n] = RenderTarget::MakeUnique();
		XUSG_N_RETURN(m_renderTargets[n]->CreateFromSwapChain(m_device.get(), m_swapChain.get(), n), ThrowIfFailed(E_FAIL));

		m_commandAllocators[n] = CommandAllocator::MakeUnique();
		XUSG_N_RETURN(m_commandAllocators[n]->Create(m_device.get(), CommandListType::DIRECT,
			(L"CommandAllocator" + to_wstring(n)).c_str()), ThrowIfFailed(E_FAIL));
	}

	// Create output views
	m_depth = DepthStencil::MakeUnique();
	m_depth->Create(m_device.get(), m_width, m_height, Format::D24_UNORM_S8_UINT,
		ResourceFlag::DENY_SHADER_RESOURCE, 1, 1, 1, 1.0f, 0, false,
		MemoryFlag::NONE, L"Depth");
}

// Load the sample assets.
void ParticleEmitter::LoadAssets()
{
	const auto counter = RawBuffer::MakeUnique();
	XUSG_N_RETURN(counter->Create(m_device.get(), sizeof(uint32_t), ResourceFlag::DENY_SHADER_RESOURCE,
		MemoryType::READBACK, 0, nullptr, 0), ThrowIfFailed(E_FAIL));

	// Create the command list.
	m_commandList = CommandList::MakeUnique();
	const auto pCommandList = m_commandList.get();
	XUSG_N_RETURN(pCommandList->Create(m_device.get(), 0, CommandListType::DIRECT,
		m_commandAllocators[m_frameIndex].get(), nullptr), ThrowIfFailed(E_FAIL));

	vector<Resource::uptr> uploaders(0);
	// Create renderer
	m_renderer = make_unique<Renderer>();
	XUSG_N_RETURN(m_renderer->Init(pCommandList, m_width, m_height, uploaders, m_meshFileName.c_str(),
		g_backBufferFormat, Format::D24_UNORM_S8_UINT), ThrowIfFailed(E_FAIL));

	// Create emitter
	const auto numParticles = 1u << 16;
	m_emitter = make_unique<Emitter>();
	XUSG_N_RETURN(m_emitter->Init(pCommandList, numParticles, m_descriptorTableLib, uploaders,
		m_renderer->GetInputLayout(), g_backBufferFormat, Format::D24_UNORM_S8_UINT), ThrowIfFailed(E_FAIL));

	// Create SPH fluid simulator
	m_fluidSPH = make_unique<FluidSPH>();
	XUSG_N_RETURN(m_fluidSPH->Init(pCommandList, numParticles, m_descriptorTableLib,
		uploaders, m_emitter->GetParticleBuffers()), ThrowIfFailed(E_FAIL));

	// Create fast hybrid fluid simulator
	m_fluidFH = make_unique<FluidFH>();
	XUSG_N_RETURN(m_fluidFH->Init(pCommandList, numParticles, m_descriptorTableLib,
		uploaders, g_backBufferFormat), ThrowIfFailed(E_FAIL));

#if defined(_DEBUG)
	ComputeUtil prefixSumUtil(m_device);
	prefixSumUtil.SetPrefixSum(pCommandList, true, m_descriptorTableLib,
		nullptr, &uploaders, Format::R32_UINT, 1024 * 5 + 387);
#endif

	m_emitter->Distribute(pCommandList, counter.get(), m_renderer->GetVertexBuffer(),
		m_renderer->GetIndexBuffer(), m_renderer->GetNumIndices(), 32.0f, m_meshPosScale.w);

#if defined(_DEBUG)
	prefixSumUtil.PrefixSum(pCommandList);
#endif

	// Close the command list and execute it to begin the initial GPU setup.
	XUSG_N_RETURN(pCommandList->Close(), ThrowIfFailed(E_FAIL));
	m_commandQueue->ExecuteCommandList(pCommandList);

	// Create synchronization objects and wait until assets have been uploaded to the GPU.
	{
		if (!m_fence)
		{
			m_fence = Fence::MakeUnique();
			XUSG_N_RETURN(m_fence->Create(m_device.get(), m_fenceValues[m_frameIndex]++, FenceFlag::NONE, L"Fence"), ThrowIfFailed(E_FAIL));
		}

		// Create an event handle to use for frame synchronization.
		m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		if (!m_fenceEvent) ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));

		// Wait for the command list to execute; we are reusing the same command 
		// list in our main loop but for now, we just want to wait for setup to 
		// complete before continuing.
		WaitForGpu();
	}

#if defined(_DEBUG)
	prefixSumUtil.VerifyPrefixSum();
#endif

	// Shrink memory cost
	StructuredBuffer::uptr emitterScratch = StructuredBuffer::MakeUnique();
	XUSG_N_RETURN(m_commandAllocators[m_frameIndex]->Reset(), ThrowIfFailed(E_FAIL));
	XUSG_N_RETURN(pCommandList->Reset(m_commandAllocators[m_frameIndex].get(), nullptr), ThrowIfFailed(E_FAIL));
	XUSG_N_RETURN(m_emitter->SetEmitterCount(pCommandList, counter.get(), emitterScratch), ThrowIfFailed(E_FAIL));

	// Close the command list and execute it to begin the initial GPU setup.
	XUSG_N_RETURN(pCommandList->Close(), ThrowIfFailed(E_FAIL));
	m_commandQueue->ExecuteCommandList(pCommandList);

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
	const auto view = XMMatrixLookAtLH(eyePt, focusPt, XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));
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
	m_renderer->UpdateFrame(m_frameIndex, time, timeStep, m_meshPosScale, viewProj, m_isPaused);
	m_emitter->UpdateFrame(m_frameIndex, time, timeStep, m_renderer->GetWorld(), viewProj);
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
	m_commandQueue->ExecuteCommandList(m_commandList.get());

	// Present the frame.
	XUSG_N_RETURN(m_swapChain->Present(0, PresentFlag::ALLOW_TEARING), ThrowIfFailed(E_FAIL));

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
	case VK_SPACE:
		m_isPaused = !m_isPaused;
		break;
	case VK_F1:
		m_showFPS = !m_showFPS;
		break;
	case VK_F11:
		m_screenShot = 1;
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
	const auto str_tolower = [](wstring s)
	{
		transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return towlower(c); });

		return s;
	};

	const auto isArgMatched = [&argv, &str_tolower](int i, const wchar_t* paramName)
	{
		const auto& arg = argv[i];

		return (arg[0] == L'-' || arg[0] == L'/')
			&& str_tolower(&arg[1]) == str_tolower(paramName);
	};

	const auto hasNextArgValue = [&argv, &argc](int i)
	{
		const auto& arg = argv[i + 1];

		return i + 1 < argc && arg[0] != L'/' &&
			(arg[0] != L'-' || (arg[1] >= L'0' && arg[1] <= L'9') || arg[1] == L'.');
	};

	DXFramework::ParseCommandLineArgs(argv, argc);

	for (auto i = 1; i < argc; ++i)
	{
		if (isArgMatched(i, L"warp")) m_deviceType = DEVICE_WARP;
		else if (isArgMatched(i, L"uma")) m_deviceType = DEVICE_UMA;
		else if (isArgMatched(i, L"mesh"))
		{
			if (hasNextArgValue(i))
			{
				m_meshFileName.resize(wcslen(argv[++i]));
				for (size_t j = 0; j < m_meshFileName.size(); ++j)
					m_meshFileName[j] = static_cast<char>(argv[i][j]);
			}
			if (hasNextArgValue(i)) i += swscanf_s(argv[i + 1], L"%f", &m_meshPosScale.x);
			if (hasNextArgValue(i)) i += swscanf_s(argv[i + 1], L"%f", &m_meshPosScale.y);
			if (hasNextArgValue(i)) i += swscanf_s(argv[i + 1], L"%f", &m_meshPosScale.z);
			if (hasNextArgValue(i)) i += swscanf_s(argv[i + 1], L"%f", &m_meshPosScale.w);
		}
	}
}

void ParticleEmitter::PopulateCommandList()
{
	// Command list allocators can only be reset when the associated 
	// command lists have finished execution on the GPU; apps should use 
	// fences to determine GPU execution progress.
	const auto pCommandAllocator = m_commandAllocators[m_frameIndex].get();
	XUSG_N_RETURN(pCommandAllocator->Reset(), ThrowIfFailed(E_FAIL));

	// However, when ExecuteCommandList() is called on a particular command 
	// list, that command list can then be reset at any time and must be before 
	// re-recording.
	const auto pCommandList = m_commandList.get();
	XUSG_N_RETURN(pCommandList->Reset(pCommandAllocator, nullptr), ThrowIfFailed(E_FAIL));

	// Record commands.
	// Bind the descriptor heap.
	const auto descriptorHeap = m_descriptorTableLib->GetDescriptorHeap(CBV_SRV_UAV_HEAP);
	pCommandList->SetDescriptorHeaps(1, &descriptorHeap);

	const auto pRenderTarget = m_renderTargets[m_frameIndex].get();

	ResourceBarrier barriers[1];
	auto numBarriers = pRenderTarget->SetBarrier(barriers, ResourceState::RENDER_TARGET);
	pCommandList->Barrier(numBarriers, barriers);

	// Clear render target
	const float clearColor[4] = { 0.2f, 0.2f, 0.2f, 0.0f };
	pCommandList->ClearRenderTargetView(pRenderTarget->GetRTV(), clearColor);
	pCommandList->ClearDepthStencilView(m_depth->GetDSV(), ClearFlag::DEPTH, 1.0f);
	
	m_renderer->Render(pCommandList, m_frameIndex, pRenderTarget->GetRTV(), m_depth->GetDSV());

	// Fluid simulation
	switch (m_simulationMethod)
	{
	case SPH_SIMULATION:
		m_emitter->RenderSPH(pCommandList, m_frameIndex, pRenderTarget->GetRTV(),
			&m_depth->GetDSV(), m_fluidSPH->GetDescriptorTable());
		m_fluidSPH->Simulate(pCommandList);
		break;
	case FAST_HYBRID_FLUID:
		m_emitter->RenderFHF(pCommandList, m_frameIndex, pRenderTarget->GetRTV(),
			&m_depth->GetDSV(), m_fluidFH->GetDescriptorTable());
		m_fluidFH->Simulate(pCommandList);
		break;
	default:
		m_emitter->Render(pCommandList, m_frameIndex, pRenderTarget->GetRTV(), &m_depth->GetDSV());
	}

	// Indicate that the back buffer will now be used to present.
	numBarriers = pRenderTarget->SetBarrier(barriers, ResourceState::PRESENT);
	pCommandList->Barrier(numBarriers, barriers);

	// Screen-shot helper
	if (m_screenShot == 1)
	{
		if (!m_readBuffer) m_readBuffer = Buffer::MakeUnique();
		pRenderTarget->ReadBack(pCommandList, m_readBuffer.get(), &m_rowPitch);
		m_screenShot = 2;
	}

	XUSG_N_RETURN(pCommandList->Close(), ThrowIfFailed(E_FAIL));
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

	// Screen-shot helper
	if (m_screenShot)
	{
		if (m_screenShot > FrameCount)
		{
			char timeStr[15];
			tm dateTime;
			const auto now = time(nullptr);
			if (!localtime_s(&dateTime, &now) && strftime(timeStr, sizeof(timeStr), "%Y%m%d%H%M%S", &dateTime))
				SaveImage((string("ParticleEmitter_") + timeStr + ".png").c_str(), m_readBuffer.get(), m_width, m_height, m_rowPitch);
			m_screenShot = 0;
		}
		else ++m_screenShot;
	}
}

void ParticleEmitter::SaveImage(char const* fileName, Buffer* pImageBuffer, uint32_t w, uint32_t h, uint32_t rowPitch, uint8_t comp)
{
	assert(comp == 3 || comp == 4);
	const auto pData = static_cast<const uint8_t*>(pImageBuffer->Map(nullptr));

	//stbi_write_png_compression_level = 1024;
	vector<uint8_t> imageData(comp * w * h);
	const auto sw = rowPitch / 4; // Byte to pixel
	for (auto i = 0u; i < h; ++i)
		for (auto j = 0u; j < w; ++j)
		{
			const auto s = sw * i + j;
			const auto d = w * i + j;
			for (uint8_t k = 0; k < comp; ++k)
				imageData[comp * d + k] = pData[4 * s + k];
		}

	stbi_write_png(fileName, w, h, comp, imageData.data(), 0);

	pImageBuffer->Unmap();
}

double ParticleEmitter::CalculateFrameStats(float* pTimeStep)
{
	static auto frameCnt = 0u;
	static auto previousTime = 0.0;
	const auto totalTime = m_timer.GetTotalSeconds();
	++frameCnt;

	const auto timeStep = totalTime - previousTime;

	// Compute averages over one second period.
	if (timeStep >= 1.0)
	{
		const auto fps = static_cast<float>(frameCnt / timeStep);	// Normalize to an exact second.

		frameCnt = 0;
		previousTime = totalTime;

		wstringstream windowText;
		windowText << L"    fps: ";
		if (m_showFPS) windowText << setprecision(2) << fixed << fps;
		else windowText << L"[F1]";
		windowText << L"    [S] " << SimulationMethodDescs[m_simulationMethod];
		windowText << L"    [F11] screen shot";

		SetCustomWindowText(windowText.str().c_str());
	}

	if (pTimeStep) *pTimeStep = static_cast<float>(m_timer.GetElapsedSeconds());

	return totalTime;
}
