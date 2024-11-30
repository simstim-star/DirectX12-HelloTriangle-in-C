#define COBJMACROS
#include <dxgi1_6.h>

#include "sample.h"
#include "sample_commons.h"
#include "macros.h"
#include "window.h"
#include "d3dcompiler.h"
#include "barrier_helpers.h"
#include "core_helpers.h"


/*************************************************************************************
 Forward declarations of private functions
**************************************************************************************/

static void LoadPipeline(DXSample* const sample);
static void LoadAssets(DXSample* const sample);
static void WaitForPreviousFrame(DXSample* const sample);
static void PopulateCommandList(DXSample* const sample);
static void ReleaseAll(DXSample* const sample);

/*************************************************************************************
 Public functions
**************************************************************************************/

void Sample_Init(DXSample* const sample) {
	sample->aspectRatio = (float)(sample->width) / (float)(sample->height);
	sample->frameIndex = 0;
	sample->viewport = (D3D12_VIEWPORT){ 0.0f, 0.0f, (float)(sample->width), (float)(sample->height) };
	sample->scissorRect = (D3D12_RECT){ 0, 0, (LONG)(sample->width), (LONG)(sample->height) };
	sample->rtvDescriptorSize = 0;
	GetCurrentPath(sample->assetsPath, _countof(sample->assetsPath));

	LoadPipeline(sample);
	LoadAssets(sample);
}

void Sample_Destroy(DXSample* const sample)
{
	WaitForPreviousFrame(sample);
	CloseHandle(sample->fenceEvent);
	ReleaseAll(sample);
}

void Sample_Update(DXSample* const sample) {}

void Sample_Render(DXSample* const sample)
{
	PopulateCommandList(sample);
	ID3D12CommandList* asCommandList = NULL;
	CAST(sample->commandList, asCommandList);
	ID3D12CommandList* ppCommandLists[] = { asCommandList };
	ID3D12CommandQueue_ExecuteCommandLists(sample->commandQueue, _countof(ppCommandLists), ppCommandLists);
	RELEASE(asCommandList);
	HRESULT hr = IDXGISwapChain3_Present(sample->swapChain, 1, 0);
	if(FAILED(hr)) LogErrAndExit(IDXGISwapChain3_Present(sample->swapChain, 1, 0));
	WaitForPreviousFrame(sample);
}

/*************************************************************************************
 Private functions
**************************************************************************************/

static void LoadPipeline(DXSample* const sample)
{
	int isDebugFactory = 0;

#if defined(_DEBUG)
	// Enable the debug layer (requires the Graphics Tools "optional feature").
	ID3D12Debug* debugController = NULL;
	if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
	{
		ID3D12Debug_EnableDebugLayer(debugController);
		isDebugFactory |= DXGI_CREATE_FACTORY_DEBUG;
		RELEASE(debugController);
	}
#endif

	IDXGIFactory4* factory = NULL;
	HRESULT hr = CreateDXGIFactory2(isDebugFactory, IID_PPV_ARGS(&factory));
	if (FAILED(hr)) LogErrAndExit(hr);

	/* Create device */

	IDXGIAdapter1* hardwareAdapter = NULL;
	IDXGIFactory1* factoryAsFactory1 = NULL;

	hr = CAST(factory, factoryAsFactory1);
	if (FAILED(hr)) LogErrAndExit(hr);

	GetHardwareAdapter(factoryAsFactory1, &hardwareAdapter, FALSE);
	RELEASE(factoryAsFactory1);

	IUnknown* hardwareAdapterAsUnknown = NULL;

	hr = CAST(hardwareAdapter, hardwareAdapterAsUnknown);
	if (FAILED(hr)) LogErrAndExit(hr);

	hr = D3D12CreateDevice(hardwareAdapterAsUnknown, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&sample->device));
	if (FAILED(hr)) LogErrAndExit(hr);

	RELEASE(hardwareAdapterAsUnknown);
	RELEASE(hardwareAdapter);

	/* Create command queue */

	D3D12_COMMAND_QUEUE_DESC queueDesc = { .Flags = D3D12_COMMAND_QUEUE_FLAG_NONE, .Type = D3D12_COMMAND_LIST_TYPE_DIRECT };
	ID3D12CommandQueue* commandQueue = NULL;
	hr = ID3D12Device_CreateCommandQueue(sample->device, &queueDesc, IID(&sample->commandQueue), &sample->commandQueue);
	if (FAILED(hr)) LogErrAndExit(hr);

	/* Create swap chain */

	DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {
		.BufferCount = 2,
		.Width = 1280,
		.Height = 720,
		.Format = DXGI_FORMAT_R8G8B8A8_UNORM,
		.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
		.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
		.SampleDesc.Count = 1,
	};

	IUnknown* commandQueueAsIUnknown = NULL;
	hr = CAST(sample->commandQueue, commandQueueAsIUnknown);
	if (FAILED(hr)) LogErrAndExit(hr);

	IDXGISwapChain1* swapChainAsSwapChain1 = NULL;
	hr = IDXGIFactory4_CreateSwapChainForHwnd(
		factory,
		commandQueueAsIUnknown,        // Swap chain needs the queue so that it can force a flush on it
		G_HWND,
		&swapChainDesc,
		NULL,
		NULL,
		&swapChainAsSwapChain1);
	if (FAILED(hr)) LogErrAndExit(hr);

	RELEASE(commandQueueAsIUnknown);

	hr = CAST(swapChainAsSwapChain1, sample->swapChain);
	if (FAILED(hr)) LogErrAndExit(hr);
	RELEASE(swapChainAsSwapChain1);

	hr = IDXGIFactory4_MakeWindowAssociation(factory, G_HWND, DXGI_MWA_NO_ALT_ENTER);
	if (FAILED(hr)) LogErrAndExit(hr);

	sample->frameIndex = IDXGISwapChain3_GetCurrentBackBufferIndex(sample->swapChain);

	/* Create descriptor heaps (only 2 RTVs in this example) */
	{
		D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {
			.NumDescriptors = FrameCount,
			.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
			.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
		};
		hr = ID3D12Device_CreateDescriptorHeap(sample->device, &rtvHeapDesc, IID(&sample->rtvHeap), &sample->rtvHeap);
		if (FAILED(hr)) LogErrAndExit(hr);
		sample->rtvDescriptorSize = ID3D12Device_GetDescriptorHandleIncrementSize(sample->device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	}

	/* Create frame resources on the descriptor heaps above */
	{
		D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle;
		ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(sample->rtvHeap, &rtvHandle);
		// Create a RTV for each frame.
		for (UINT nthSwapChainBuffer = 0; nthSwapChainBuffer < FrameCount; nthSwapChainBuffer++)
		{
			// set resource at renderTargets[nthSwapChainBuffer] as the nth-SwapChainBuffer
			hr = IDXGISwapChain3_GetBuffer(sample->swapChain, nthSwapChainBuffer, IID(&sample->renderTargets[nthSwapChainBuffer]), &sample->renderTargets[nthSwapChainBuffer]);
			if (FAILED(hr)) LogErrAndExit(hr);
			// create a RTV on the heap related to the handle
			ID3D12Device_CreateRenderTargetView(sample->device, sample->renderTargets[nthSwapChainBuffer], NULL, rtvHandle);
			// walk an offset equivalent to one descriptor to go to next space to store the next RTV
			rtvHandle.ptr = (SIZE_T)((INT64)(rtvHandle.ptr) + (INT64)(sample->rtvDescriptorSize));
		}
	}
	hr = ID3D12Device_CreateCommandAllocator(sample->device, D3D12_COMMAND_LIST_TYPE_DIRECT, IID(&sample->commandAllocator), &sample->commandAllocator);
	if (FAILED(hr)) LogErrAndExit(hr);
	RELEASE(factory);
}

static void LoadAssets(DXSample* const sample)
{
	/* Create root signature */
	{
		const D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = {
			.NumParameters = 0,
			.pParameters = NULL,
			.NumStaticSamplers = 0,
			.pStaticSamplers = NULL,
			.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
		};

		// Note: Blobs don't seem to be actually RefCounted. We can call Release on them even after the count has reached zero
		// and they also don't show up when calling ReportLiveObjects. Therefore, there is no need to release them.
		ID3DBlob* signature = NULL;
		ID3DBlob* error = NULL;
		HRESULT hr = D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error);
		if (FAILED(hr)) LogErrAndExit(hr);

		const LPVOID bufferPointer = ID3D10Blob_GetBufferPointer(signature);
		const SIZE_T bufferSize = ID3D10Blob_GetBufferSize(signature);
		hr = ID3D12Device_CreateRootSignature(sample->device, 0, bufferPointer, bufferSize, IID(&sample->rootSignature), &sample->rootSignature);
		if (FAILED(hr)) LogErrAndExit(hr);
	}
	
	/* Create the pipeline state, which includes compiling and loading shaders */
	{
		ID3DBlob* vertexShader = NULL;
		ID3DBlob* pixelShader = NULL;

#if defined(_DEBUG)
		// Enable better shader debugging with the graphics debugging tools.
		const UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
		const UINT compileFlags = 0;
#endif

		const wchar_t* shadersPath = wcscat(sample->assetsPath, L"shaders/shaders.hlsl");
		
		HRESULT hr =  D3DCompileFromFile(shadersPath, NULL, NULL, "VSMain", "vs_5_0", compileFlags, 0, &vertexShader, NULL);
		if (FAILED(hr)) LogErrAndExit(hr);

		hr = D3DCompileFromFile(shadersPath, NULL, NULL, "PSMain", "ps_5_0", compileFlags, 0, &pixelShader, NULL);
		if (FAILED(hr)) LogErrAndExit(hr);

		// Define the vertex input layout
		const D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
		{
			{.SemanticName = "POSITION", .SemanticIndex = 0, .Format = DXGI_FORMAT_R32G32B32_FLOAT, .InputSlot = 0,
			 .AlignedByteOffset = 0, .InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, .InstanceDataStepRate = 0
			},
			{.SemanticName = "COLOR", .SemanticIndex = 0, .Format = DXGI_FORMAT_R32G32B32A32_FLOAT, .InputSlot = 0,
			 .AlignedByteOffset = 12, .InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, .InstanceDataStepRate = 0
			}
		};

		/* Create PSO */

		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {
			.pRootSignature = sample->rootSignature,
			.InputLayout = (D3D12_INPUT_LAYOUT_DESC){
				.pInputElementDescs = inputElementDescs,
				.NumElements = _countof(inputElementDescs)
			},
			.VS = (D3D12_SHADER_BYTECODE){
				.pShaderBytecode = ID3D10Blob_GetBufferPointer(vertexShader),
				.BytecodeLength = ID3D10Blob_GetBufferSize(vertexShader),
			},
			.PS = (D3D12_SHADER_BYTECODE){
				.pShaderBytecode = ID3D10Blob_GetBufferPointer(pixelShader),
				.BytecodeLength = ID3D10Blob_GetBufferSize(pixelShader),
			},
			.RasterizerState = CD3DX12_DEFAULT_RASTERIZER_DESC(),
			.BlendState = CD3DX12_DEFAULT_BLEND_DESC(),
			.DepthStencilState.DepthEnable = FALSE,
			.DepthStencilState.StencilEnable = FALSE,
			.SampleMask = UINT_MAX,
			.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
			.NumRenderTargets = 1,
			.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM,
			.SampleDesc.Count = 1,
		};
		hr = ID3D12Device_CreateGraphicsPipelineState(sample->device, &psoDesc, IID(&sample->pipelineState), &sample->pipelineState);
		if (FAILED(hr)) LogErrAndExit(hr);
	}

	/* Create the command list */

	HRESULT hr = ID3D12Device_CreateCommandList(sample->device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, sample->commandAllocator, sample->pipelineState, IID(&sample->commandList), &sample->commandList);
	if (FAILED(hr)) LogErrAndExit(hr);
	// Command lists are created in the recording state, but there is nothing
	// to record yet. The main loop expects it to be closed, so close it now
	hr = ID3D12GraphicsCommandList_Close(sample->commandList);
	if (FAILED(hr)) LogErrAndExit(hr);

	/* Create the vertex buffer, populate it and set a view to it */
	{
		// Coordinates are in relation to the screen center, left-handed (+z to screen inside, +y up, +x right)
		const Vertex triangleVertices[] =
		{
			{ { 0.0f, 0.25f * sample->aspectRatio, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f }},
			{ { 0.25f, -0.25f * sample->aspectRatio, 0.0f }, { 0.0f, 1.0f, 0.0f, 1.0f } },
			{ { -0.25f, -0.25f * sample->aspectRatio, 0.0f }, { 0.0f, 0.0f, 1.0f, 1.0f }},
		};
		const UINT vertexBufferSize = sizeof(triangleVertices);
		const D3D12_HEAP_PROPERTIES heapPropertyUpload = (D3D12_HEAP_PROPERTIES){
			.Type = D3D12_HEAP_TYPE_UPLOAD,
			.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
			.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN,
			.CreationNodeMask = 1,
			.VisibleNodeMask = 1,
		};
		const D3D12_RESOURCE_DESC bufferResource = CD3DX12_RESOURCE_DESC_BUFFER(vertexBufferSize, D3D12_RESOURCE_FLAG_NONE, 0);

		// Note: using upload heaps to transfer static data like vert buffers is not 
		// recommended. Every time the GPU needs it, the upload heap will be marshalled 
		// over. Please read up on Default Heap usage. An upload heap is used here for 
		// code simplicity and because there are very few verts to actually transfer
		hr = ID3D12Device_CreateCommittedResource(sample->device,
			&heapPropertyUpload,
			D3D12_HEAP_FLAG_NONE,
			&bufferResource,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			NULL,
			IID(&sample->vertexBuffer),
			&sample->vertexBuffer);
		if (FAILED(hr)) LogErrAndExit(hr);

		// We will open the vertexBuffer memory (that is in the GPU) for the CPU to write the triangle data 
		// in it. To do that, we use the Map() function, which enables the CPU to read from or write 
		// to the vertex buffer's memory directly
		UINT8* pVertexDataBegin = NULL; // UINT8 to represent byte-level manipulation
		// We do not intend to read from this resource on the CPU, only write
		const D3D12_RANGE readRange = (D3D12_RANGE){ .Begin = 0, .End = 0 };
		hr = ID3D12Resource_Map(sample->vertexBuffer, 0, &readRange, (void**)(&pVertexDataBegin));
		if (FAILED(hr)) LogErrAndExit(hr);
		memcpy(pVertexDataBegin, triangleVertices, sizeof(triangleVertices));
		// While mapped, the GPU cannot access the buffer, so it's important to minimize the time 
		// the buffer is mapped.
		ID3D12Resource_Unmap(sample->vertexBuffer, 0, NULL);

		// Initialize the vertex buffer view
		sample->vertexBufferView.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(sample->vertexBuffer);
		sample->vertexBufferView.StrideInBytes = sizeof(Vertex);
		sample->vertexBufferView.SizeInBytes = vertexBufferSize;
	}

	// Create synchronization objects and wait until assets have been uploaded to the GPU
	{
		hr = ID3D12Device_CreateFence(sample->device, 0, D3D12_FENCE_FLAG_NONE, IID(&sample->fence), &sample->fence);
		if (FAILED(hr)) LogErrAndExit(hr);
		sample->fenceValue = 1;

		// Create an event handle to use for frame synchronization
		sample->fenceEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
		if (sample->fenceEvent == NULL)
		{
			hr = HRESULT_FROM_WIN32(GetLastError());
			if (FAILED(hr)) LogErrAndExit(hr);
		}

		// Wait for the command list to execute; we are reusing the same command 
		// list in our main loop, but for now, we just want to wait for setup to 
		// complete before continuing
		WaitForPreviousFrame(sample);
	}
}

// WAITING FOR THE FRAME TO COMPLETE BEFORE CONTINUING IS NOT BEST PRACTICE.
// It's implemented as such for simplicity, but doesn't really make good use of the frame
// buffers, as we always wait for all work to be done before starting work on the other buffer.
// In the D3D12HelloFrameBuffering example, we keep a fenceValue for each buffer to keep track if
// the work for this buffer is already done and it's ready to receive more.
static void WaitForPreviousFrame(DXSample* const sample)
{
	// Signal to the fence the current fenceValue
	HRESULT hr = ID3D12CommandQueue_Signal(sample->commandQueue, sample->fence, sample->fenceValue);
	if(FAILED(hr)) LogErrAndExit(hr);

	// Wait until the frame is finished (ie. reached the signal sent right above) 
	if (ID3D12Fence_GetCompletedValue(sample->fence) < sample->fenceValue)
	{
		hr = ID3D12Fence_SetEventOnCompletion(sample->fence, sample->fenceValue, sample->fenceEvent);
		if (FAILED(hr)) LogErrAndExit(hr);
		WaitForSingleObject(sample->fenceEvent, INFINITE);
	}

	sample->fenceValue++;
	sample->frameIndex = IDXGISwapChain3_GetCurrentBackBufferIndex(sample->swapChain);
}

// Record all the commands we need to render the scene into the command list
static void PopulateCommandList(DXSample* const sample)
{
	// Command list allocators can only be reset when the associated 
	// command lists have finished execution on the GPU; apps should use 
	// fences to determine GPU execution progress
	HRESULT hr = ID3D12CommandAllocator_Reset(sample->commandAllocator);
	if (FAILED(hr)) LogErrAndExit(hr);

	// However, when ExecuteCommandList() is called on a particular command 
	// list, that command list can then be reset at any time and must be before 
	// re-recording
	hr = ID3D12GraphicsCommandList_Reset(sample->commandList, sample->commandAllocator, sample->pipelineState);
	if (FAILED(hr)) LogErrAndExit(hr);
	// Set necessary state
	ID3D12GraphicsCommandList_SetGraphicsRootSignature(sample->commandList, sample->rootSignature);
	ID3D12GraphicsCommandList_RSSetViewports(sample->commandList, 1, &sample->viewport);
	ID3D12GraphicsCommandList_RSSetScissorRects(sample->commandList, 1, &sample->scissorRect);

	// Indicate that the back buffer will be used as a render target
	const D3D12_RESOURCE_BARRIER transitionBarrierRT = CD3DX12_Transition(sample->renderTargets[sample->frameIndex],
		D3D12_RESOURCE_STATE_PRESENT,
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
		D3D12_RESOURCE_BARRIER_FLAG_NONE);
	ID3D12GraphicsCommandList_ResourceBarrier(sample->commandList, 1, &transitionBarrierRT);

	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle;
	ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(sample->rtvHeap, &rtvHandle);
	const INT64 CurrentRtvOffset = sample->frameIndex * sample->rtvDescriptorSize;
	rtvHandle.ptr = (SIZE_T)((INT64)(rtvHandle.ptr) + CurrentRtvOffset);
	ID3D12GraphicsCommandList_OMSetRenderTargets(sample->commandList, 1, &rtvHandle, FALSE, NULL);

	// Record commands
	const float clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
	ID3D12GraphicsCommandList_ClearRenderTargetView(sample->commandList, rtvHandle, clearColor, 0, NULL);
	ID3D12GraphicsCommandList_IASetVertexBuffers(sample->commandList, 0, 1, &sample->vertexBufferView);
	ID3D12GraphicsCommandList_IASetPrimitiveTopology(sample->commandList, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	ID3D12GraphicsCommandList_DrawInstanced(sample->commandList, 3, 1, 0, 0);

	D3D12_RESOURCE_BARRIER transitionBarrierPresent = CD3DX12_Transition(sample->renderTargets[sample->frameIndex],
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		D3D12_RESOURCE_STATE_PRESENT,
		D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
		D3D12_RESOURCE_BARRIER_FLAG_NONE);
	
	// Indicate that the back buffer will now be used to present
	ID3D12GraphicsCommandList_ResourceBarrier(sample->commandList, 1, &transitionBarrierPresent);
	hr = ID3D12GraphicsCommandList_Close(sample->commandList);
	if (FAILED(hr)) LogErrAndExit(hr);
}

void ReleaseAll(DXSample* const sample)
{
	RELEASE(sample->swapChain);
	RELEASE(sample->device);
	for (int i = 0; i < FrameCount; ++i) {
		RELEASE(sample->renderTargets[i]);
	}
	RELEASE(sample->commandAllocator);
	RELEASE(sample->commandQueue);
	RELEASE(sample->rootSignature);
	RELEASE(sample->rtvHeap);
	RELEASE(sample->pipelineState);
	RELEASE(sample->commandList);
	RELEASE(sample->vertexBuffer);
	RELEASE(sample->fence);
}
