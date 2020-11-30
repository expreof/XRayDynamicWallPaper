#include "d3dApp.h"
#include <assert.h>
#include <string>
#include <vector>
#include "d3dUtil.h"

using Microsoft::WRL::ComPtr;

LRESULT MainWindow::HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
	case WM_SIZE:
		mClientWidth = LOWORD(lParam);
		mClientHeight = HIWORD(lParam);
		if (mD3dDevice)
		{
			if (wParam == SIZE_MINIMIZED)
			{
				mMinimized = true;
				mMaximized = false;
			}
			else if (wParam == SIZE_MAXIMIZED)
			{
				mMinimized = false;
				mMaximized = true;
				OnResize();
			}
			else if (wParam == SIZE_RESTORED)
			{
				if (mMinimized)
				{
					mMinimized = false;
					OnResize();
				}
				else if (mMaximized)
				{
					mMaximized = false;
					OnResize();
				}
				else if (mResizing)
				{
				}
				else
					OnResize();
			}
		}
		return 0;
	case WM_ENTERSIZEMOVE:
		mResizing = true;
		return 0;
	case WM_EXITSIZEMOVE:
		mResizing = false;
		OnResize();
		return 0;
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	case WM_GETMINMAXINFO:
		reinterpret_cast<MINMAXINFO*>(lParam)->ptMinTrackSize.x = 200;
		reinterpret_cast<MINMAXINFO*>(lParam)->ptMinTrackSize.y = 200;
		return 0;
	}
	return DefWindowProc(m_hwnd, uMsg, wParam, lParam);
}

bool MainWindow::Get4xMsaaState() const
{
	return m4xMsaaState;
}

void MainWindow::Set4xMsaaState(bool value)
{
	if (m4xMsaaState != value)
	{
		m4xMsaaState = value;

		// ʹ���µĶ�����������´����������ͻ���
		CreateSwapChain();
		OnResize();
	}
}

int MainWindow::Run()
{
	MSG msg{};
	while (msg.message != WM_QUIT)
	{
		if (PeekMessage(&msg, 0, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		else
		{
			Update();
			Draw();
		}
	}
	return int(msg.wParam);
}

bool MainWindow::Initialize()
{
	if(FAILED(InitDirect3D()))
		return false;
	// Do the initial resize code.
	OnResize();

	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

	LoadTextures();
	BuildDescriptorHeaps();
	BuildConstantBuffers();
	BuildRootSignature();
	BuildShadersAndInputLayout();
	BuildTriangleGeometry();
	BuildPSO();

	// ִ�г�ʼ������
	ThrowIfFailed(mCommandList->Close());
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQuene->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);
	FlushCommandQueue();

	mVUploadBuffer = nullptr;
	mIUploadBuffer = nullptr;
	return true;
}

HRESULT MainWindow::InitDirect3D()
{
#if defined(DEBUG) || defined(_DEBUG) 
	// Enable the D3D12 debug layer.
	{
		ComPtr<ID3D12Debug> debugController;
		if (FAILED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
			return S_FALSE;
		debugController->EnableDebugLayer();
	}
#endif
	HRESULT hr;
	hr = CreateDXGIFactory1(IID_PPV_ARGS(&mDxgiFactory));
	if (FAILED(hr))		// Ҫ������ж�
		return S_FALSE;
	else
		hr = D3D12CreateDevice(
			nullptr,		// default adapter
			D3D_FEATURE_LEVEL_11_0,
			IID_PPV_ARGS(&mD3dDevice));
	if (FAILED(hr))
	{
		ComPtr<IDXGIAdapter> pWarpAdapter;
		if (FAILED(mDxgiFactory->EnumWarpAdapter(IID_PPV_ARGS(&pWarpAdapter))))
			return S_FALSE;
		if (FAILED(D3D12CreateDevice(
			pWarpAdapter.Get(),
			D3D_FEATURE_LEVEL_11_0,
			IID_PPV_ARGS(&mD3dDevice))))
			return S_FALSE;
	}

	// ����Χ������ȡ��������С
	hr = mD3dDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&mFence));
	mRtvDescriptorSize = mD3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	mDsvDescriptorSize = mD3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
	mCbvSrvUavDescriptorSize = mD3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	// ���� 4X MSAA ���������֧��
	if (SUCCEEDED(hr))
	{
		D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS msQualityLevels;
		msQualityLevels.Format = mBackBufferFormat;
		msQualityLevels.SampleCount = 4;
		msQualityLevels.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
		msQualityLevels.NumQualityLevels = 0;
		hr = mD3dDevice->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &msQualityLevels, sizeof(msQualityLevels));

		m4xMsaaQuality = msQualityLevels.NumQualityLevels;
		assert(m4xMsaaQuality > 0 && "Unexpected MSAA quality level.");
	}
	if (SUCCEEDED(hr))
	{
#ifdef _DEBUG
		LogAdapters();
#endif
		// ����������У������б����������
		hr = CreateCommandObjects();
	}
	if (SUCCEEDED(hr))
		hr = CreateSwapChain();
	if (SUCCEEDED(hr))
		hr = CreateRtvAndDsvDescriptorHeaps();
	return hr;
}

HRESULT MainWindow::CreateCommandObjects()
{
	HRESULT hr = S_OK;
	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	hr = mD3dDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&mCommandQuene));
	if (SUCCEEDED(hr))
		hr = mD3dDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(mDirectCmdListAlloc.GetAddressOf()));
	if (SUCCEEDED(hr))
		hr = mD3dDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, mDirectCmdListAlloc.Get(), nullptr, IID_PPV_ARGS(mCommandList.GetAddressOf()));
	if (SUCCEEDED(hr))
		hr = mCommandList->Close();

	return hr;
}

HRESULT MainWindow::CreateSwapChain()
{
	HRESULT hr;

	mSwapChain.Reset();

	DXGI_SWAP_CHAIN_DESC sd;
	sd.BufferDesc.Width = mClientWidth;
	sd.BufferDesc.Height = mClientHeight;
	sd.BufferDesc.RefreshRate.Numerator = 60;
	sd.BufferDesc.RefreshRate.Denominator = 1;
	sd.BufferDesc.Format = mBackBufferFormat;
	sd.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
	sd.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
	sd.SampleDesc.Count = m4xMsaaState ? 4 : 1;
	sd.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.BufferCount = SwapChainBufferCount;
	sd.OutputWindow = Window();
	sd.Windowed = true;
	sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

	hr = mDxgiFactory->CreateSwapChain(mCommandQuene.Get(), &sd, mSwapChain.GetAddressOf());
	return hr;
}

HRESULT MainWindow::CreateRtvAndDsvDescriptorHeaps()
{
	HRESULT hr;
	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc;
	rtvHeapDesc.NumDescriptors = SwapChainBufferCount;
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	rtvHeapDesc.NodeMask = 0;
	hr = mD3dDevice->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(mRtvHeap.GetAddressOf()));

	if (SUCCEEDED(hr))
	{
		D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc;
		dsvHeapDesc.NumDescriptors = 1;
		dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
		dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		dsvHeapDesc.NodeMask = 0;
		hr = mD3dDevice->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(mDsvHeap.GetAddressOf()));
	}
	return hr;
}

HRESULT MainWindow::OnResize()
{
	assert(mD3dDevice);
	assert(mSwapChain);
	assert(mDirectCmdListAlloc);
	// �ڸı��κ���Դǰˢ��
	FlushCommandQueue();

	HRESULT hr;
	hr = mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr);

	// �ͷ���ǰ����Դ���������´���
	if (SUCCEEDED(hr))
	{
		for (int i = 0; i < SwapChainBufferCount; ++i)
			mSwapChainBuffer[i].Reset();
		mDepthStencilBuffer.Reset();
	}

	// resize the swap chain
	hr = mSwapChain->ResizeBuffers(SwapChainBufferCount, mClientWidth, mClientHeight, mBackBufferFormat, DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH);
	mCurrBackBuffer = 0;

	if (SUCCEEDED(hr))
	{
		CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHeapHandle(mRtvHeap->GetCPUDescriptorHandleForHeapStart());
		for (UINT i = 0; i < SwapChainBufferCount; ++i)
		{
			if (SUCCEEDED(hr))
				hr = mSwapChain->GetBuffer(i, IID_PPV_ARGS(&mSwapChainBuffer[i]));
			mD3dDevice->CreateRenderTargetView(mSwapChainBuffer[i].Get(), nullptr, rtvHeapHandle);
			rtvHeapHandle.Offset(1, mRtvDescriptorSize);
		}
	}

	// �������/ģ�建��������ͼ
	if (SUCCEEDED(hr))
	{
		D3D12_RESOURCE_DESC depthStencilDesc;
		depthStencilDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		depthStencilDesc.Alignment = 0;
		depthStencilDesc.Width = mClientWidth;
		depthStencilDesc.Height = mClientHeight;
		depthStencilDesc.DepthOrArraySize = 1;
		depthStencilDesc.MipLevels = 1;

		// Correction 11/12/2016: SSAO chapter requires an SRV to the depth buffer to read from 
		// the depth buffer.  Therefore, because we need to create two views to the same resource:
		//   1. SRV format: DXGI_FORMAT_R24_UNORM_X8_TYPELESS
		//   2. DSV Format: DXGI_FORMAT_D24_UNORM_S8_UINT
		// we need to create the depth buffer resource with a typeless format.  
		depthStencilDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;

		depthStencilDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
		depthStencilDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
		depthStencilDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		depthStencilDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

		// ���ֵ
		D3D12_CLEAR_VALUE optClear;
		optClear.Format = mDepthStencilFormat;
		optClear.DepthStencil.Depth = 1.0f;
		optClear.DepthStencil.Stencil = 0;
		hr = mD3dDevice->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&depthStencilDesc,
			D3D12_RESOURCE_STATE_COMMON,
			&optClear,
			IID_PPV_ARGS(mDepthStencilBuffer.GetAddressOf()));
	}

	if (SUCCEEDED(hr))
	{
		D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc;
		dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
		dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
		dsvDesc.Format = mDepthStencilFormat;
		dsvDesc.Texture2D.MipSlice = 0;
		mD3dDevice->CreateDepthStencilView(mDepthStencilBuffer.Get(), &dsvDesc, DepthStencilView());

		// ת����Դ״̬
		mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mDepthStencilBuffer.Get(),
			D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_DEPTH_WRITE));

		// ִ�е�����С����
		hr = mCommandList->Close();
	}

	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQuene->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	// �ڵ�����С���ǰ�ȴ�
	FlushCommandQueue();

	// �����ӿھ����Ը��ǿͻ���
	mScreenViewPort.TopLeftX = 0;
	mScreenViewPort.TopLeftY = 0;
	mScreenViewPort.Width = static_cast<float>(mClientWidth);
	mScreenViewPort.Height = static_cast<float>(mClientHeight);
	mScreenViewPort.MinDepth = 0.0f;
	mScreenViewPort.MaxDepth = 1.0f;

	mScissorRect = { 0,0,mClientWidth,mClientHeight };

	return hr;
}

void MainWindow::Update()
{
	LARGE_INTEGER curr_time;
	QueryPerformanceCounter(&curr_time);

	static LARGE_INTEGER init_time = curr_time;

	LARGE_INTEGER countsPerSec;
	QueryPerformanceFrequency(&countsPerSec);

	ObjectConstants obj{ float(curr_time.QuadPart-init_time.QuadPart) / countsPerSec.QuadPart };
	mObjectCB->CopyData(0, obj);
}

void MainWindow::Draw()
{
	// �ظ�ʹ�ü�¼���������ڴ�
	// ֻ�е���GPU�����������б�ִ�����ʱ�����ǲ��ܽ�������
	ThrowIfFailed(mDirectCmdListAlloc->Reset());

	// ��ͨ�� ExecuteCommandList ������ĳ�������б����������к����Ǳ�������ø�
	// �����б��Դ������������б����ڴ�
	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), mPSO.Get()));

	// ����Դ��״̬����ת��������Դ�ӳ���״̬תΪ��ȾĿ��
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

	// �����ӿںͲü����Σ�������Ҫ���������б�����ö�����
	mCommandList->RSSetViewports(1, &mScreenViewPort);
	mCommandList->RSSetScissorRects(1, &mScissorRect);

	float tempColor[]{ 0.9,0.1,0.2,1 };
	// �����̨����������Ȼ�����
	mCommandList->ClearRenderTargetView(CurrentBackBufferView(), tempColor, 0, nullptr);
	mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1, 0, 0, nullptr);

	// ָ����Ҫ��Ⱦ�Ļ�����
	mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());

	/*ID3D12DescriptorHeap* descriptorHeaps[] = { mCbvHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);*/

	mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

	ID3D12DescriptorHeap* descriptorHeaps[] = { mSrvDescriptorHeap.Get() };
	mCommandList->SetDescriptorHeaps(1, descriptorHeaps);

	mCommandList->SetGraphicsRootConstantBufferView(1, mObjectCB->Resource()->GetGPUVirtualAddress());

	D3D12_VERTEX_BUFFER_VIEW vbv;
	vbv.BufferLocation = mVertexBuffer->GetGPUVirtualAddress();
	vbv.StrideInBytes = sizeof(Vertex);
	vbv.SizeInBytes = 4 * sizeof(Vertex);
	mCommandList->IASetVertexBuffers(0, 1, &vbv);

	D3D12_INDEX_BUFFER_VIEW ibv;
	ibv.BufferLocation = mIndexBuffer->GetGPUVirtualAddress();
	ibv.Format = DXGI_FORMAT_R16_UINT;
	ibv.SizeInBytes = 6 * sizeof(std::uint16_t);
	mCommandList->IASetIndexBuffer(&ibv);

	mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	//mCommandList->SetGraphicsRootDescriptorTable(0, mCbvHeap->GetGPUDescriptorHandleForHeapStart());
	mCommandList->SetGraphicsRootDescriptorTable(0, mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());

	mCommandList->DrawIndexedInstanced(6, 1, 0, 0, 0);

	// �ٴζ���Դ״̬����ת��������Դ����ȾĿ��״̬ת���س���״̬
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
		CurrentBackBuffer(), 
		D3D12_RESOURCE_STATE_RENDER_TARGET, 
		D3D12_RESOURCE_STATE_PRESENT));

	// �������ļ�¼
	mCommandList->Close();

	// ����ִ�е������б�����������
	ID3D12CommandList* cmdLists[] = { mCommandList.Get() };
	mCommandQuene->ExecuteCommandLists(_countof(cmdLists), cmdLists);

	// ������̨��������ǰ̨������
	mSwapChain->Present(0, 0);
	mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

	// �ȴ���֡������ִ����ϣ���ǰ��ʵ��û��ʲôЧ�ʣ����ڼ�
	// �����ں��潫������֯��Ⱦ���ֵĴ��룬������ÿһ֡��Ҫ�ȴ�
	FlushCommandQueue();
}

void MainWindow::LoadTextures()
{
	auto woodCrateTex = std::make_unique<Texture>();
	woodCrateTex->Name = "woodCrateTex";
	woodCrateTex->Filename = L"WoodCrate01.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(mD3dDevice.Get(), mCommandList.Get(),
		woodCrateTex->Filename.c_str(), woodCrateTex->Resource, woodCrateTex->UploadHeap));

	mTextures[woodCrateTex->Name] = std::move(woodCrateTex);
}

void MainWindow::BuildDescriptorHeaps()
{
	/*D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc;
	cbvHeapDesc.NumDescriptors = 1;
	cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	cbvHeapDesc.NodeMask = 0;
	ThrowIfFailed(mD3dDevice->CreateDescriptorHeap(&cbvHeapDesc, IID_PPV_ARGS(&mCbvHeap)));*/

	// ���� SRV ��
	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc{};

	srvHeapDesc.NumDescriptors = 1;
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	ThrowIfFailed(mD3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mSrvDescriptorHeap)));

	// ��ʵ�ʵ�����������
	CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

	auto woodCrateTex = mTextures["woodCrateTex"]->Resource;

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = woodCrateTex->GetDesc().Format;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = woodCrateTex->GetDesc().MipLevels;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

	mD3dDevice->CreateShaderResourceView(woodCrateTex.Get(), &srvDesc, hDescriptor);
}

void MainWindow::BuildConstantBuffers()
{
	mObjectCB = std::make_unique<UploadBuffer<ObjectConstants>>(mD3dDevice.Get(), 1, true);

	UINT objCBByteSize = CalcConstantBufferByteSize(sizeof(ObjectConstants));

	D3D12_GPU_VIRTUAL_ADDRESS cbAddress = mObjectCB->Resource()->GetGPUVirtualAddress();
	// ƫ�Ƶ������������� i ����������ĳ�������
	// ����ȡ i = 0
	int TriIndex = 0;
	cbAddress += TriIndex * objCBByteSize;
	
	D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
	cbvDesc.BufferLocation = cbAddress;
	cbvDesc.SizeInBytes = objCBByteSize;

	//mD3dDevice->CreateConstantBufferView(&cbvDesc, mCbvHeap->GetCPUDescriptorHandleForHeapStart());
}

//void MainWindow::BuildRootSignature()
//{
//	// ��ɫ������һ����Ҫ����Դ��Ϊ���루���糣����������������������
//	// ��ǩ����������ɫ����������ľ�����Դ
//	// �������ɫ��������һ�������������������Դ�����������ݵĲ�������
//	// ��ô������Ƶ���Ϊ��ǩ��������Ǻ���ǩ��
//
//	// �����������������������������������
//	CD3DX12_ROOT_PARAMETER slotRootRarameter[1];
//
//	// ����һ��ֻ����һ��CBV����������
//	CD3DX12_DESCRIPTOR_RANGE cbvTable;
//	cbvTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);
//	slotRootRarameter[0].InitAsDescriptorTable(1, &cbvTable);
//
//	// ��ǩ����һ�����������
//	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(1, slotRootRarameter, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
//
//	// ����һ����һ����λ�ĸ�ǩ���������λָ��һ���ɵ���������������ɵ�������range
//	ComPtr<ID3DBlob> serializedRootSig = nullptr;
//	ComPtr<ID3DBlob> errorBlob = nullptr;
//	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
//		serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());
//	if (errorBlob != nullptr)
//		::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
//	ThrowIfFailed(hr);
//	ThrowIfFailed(mD3dDevice->CreateRootSignature(
//		0,
//		serializedRootSig->GetBufferPointer(),
//		serializedRootSig->GetBufferSize(),
//		IID_PPV_ARGS(&mRootSignature)));
//}

void MainWindow::BuildRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE texTable;
	texTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

	CD3DX12_ROOT_PARAMETER slotRootParameter[2];
	slotRootParameter[0].InitAsDescriptorTable(1, &texTable, D3D12_SHADER_VISIBILITY_PIXEL);
	slotRootParameter[1].InitAsConstantBufferView(0);

	D3D12_STATIC_SAMPLER_DESC sampler = {};
	sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
	sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	sampler.MipLODBias = 0;
	sampler.MaxAnisotropy = 0;
	sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
	sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
	sampler.MinLOD = 0.0f;
	sampler.MaxLOD = D3D12_FLOAT32_MAX;
	sampler.ShaderRegister = 0;
	sampler.RegisterSpace = 0;
	sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc;
	rootSigDesc.Init(2, slotRootParameter, 1, &sampler,
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;

	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());
	if (errorBlob != nullptr)
		::OutputDebugStringA((char*)errorBlob->GetBufferPointer());

	ThrowIfFailed(hr);
	ThrowIfFailed(mD3dDevice->CreateRootSignature(
		0, serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(mRootSignature.GetAddressOf())));
}

void MainWindow::BuildShadersAndInputLayout()
{
	mvsByteCode = CompileShader(L"color.hlsl", nullptr, "VS", "vs_5_0");
	mpsByteCode = CompileShader(L"color.hlsl", nullptr, "PS", "ps_5_0");
}

void MainWindow::BuildTriangleGeometry()
{
	Vertex vertices[] =
	{
		Vertex({DirectX::XMFLOAT3(-1,1,0),DirectX::XMFLOAT2(0,0)}),
		Vertex({DirectX::XMFLOAT3(1,1,0),DirectX::XMFLOAT2(1,0)}),
		Vertex({DirectX::XMFLOAT3(1,-1,0),DirectX::XMFLOAT2(1,1)}),
		Vertex({DirectX::XMFLOAT3(-1,-1,0),DirectX::XMFLOAT2(0,1)})
	};

	std::uint16_t indices[] =
	{
		0,1,2,
		0,2,3
	};

	const UINT vbByteSize = sizeof(vertices);
	const UINT ibByteSize = sizeof(indices);

	mVertexBuffer = CreateDefaultBuffer(mD3dDevice.Get(),
		mCommandList.Get(), vertices, vbByteSize, mVUploadBuffer);
	mIndexBuffer = CreateDefaultBuffer(mD3dDevice.Get(),
		mCommandList.Get(), indices, ibByteSize, mIUploadBuffer);
}

void MainWindow::BuildPSO()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
	//ZeroMemory(&psoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	psoDesc.InputLayout = { mInputLayout,2 };
	psoDesc.pRootSignature = mRootSignature.Get();
	psoDesc.VS = { mvsByteCode->GetBufferPointer(),mvsByteCode->GetBufferSize() };
	psoDesc.PS = { mpsByteCode->GetBufferPointer(),mpsByteCode->GetBufferSize() };
	psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	psoDesc.SampleMask = UINT_MAX;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.NumRenderTargets = 1;
	psoDesc.RTVFormats[0] = mBackBufferFormat;
	psoDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
	psoDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
	psoDesc.DSVFormat = mDepthStencilFormat;
	ThrowIfFailed(mD3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&mPSO)));
}

void MainWindow::FlushCommandQueue()
{
	mCurrentFence++;
	mCommandQuene->Signal(mFence.Get(), mCurrentFence);

	if (mFence->GetCompletedValue() < mCurrentFence)
	{
		HANDLE eventHandle = CreateEventEx(nullptr, NULL, NULL, EVENT_ALL_ACCESS);

		// ��GPU���� current fence ʱ�����¼�
		mFence->SetEventOnCompletion(mCurrentFence, eventHandle);

		// ��GPU����current fence�¼�������ǰ�ȴ�
		WaitForSingleObject(eventHandle, INFINITE);
		CloseHandle(eventHandle);
	}
}

ID3D12Resource* MainWindow::CurrentBackBuffer() const
{
	return mSwapChainBuffer[mCurrBackBuffer].Get();
}

D3D12_CPU_DESCRIPTOR_HANDLE MainWindow::CurrentBackBufferView() const
{
	return CD3DX12_CPU_DESCRIPTOR_HANDLE(
	mRtvHeap->GetCPUDescriptorHandleForHeapStart(),
		mCurrBackBuffer,
		mRtvDescriptorSize);
}

D3D12_CPU_DESCRIPTOR_HANDLE MainWindow::DepthStencilView() const
{
	return mDsvHeap->GetCPUDescriptorHandleForHeapStart();
}

void MainWindow::LogAdapters()
{
	UINT i = 0;
	IDXGIAdapter* adapter = nullptr;
	std::vector<IDXGIAdapter*> adapterList;
	while (mDxgiFactory->EnumAdapters(i, &adapter) != DXGI_ERROR_NOT_FOUND)
	{
		DXGI_ADAPTER_DESC desc;
		adapter->GetDesc(&desc);

		std::wstring text = L"***Adapter: ";
		text += desc.Description;
		text += L"\n";

		OutputDebugString(text.c_str());

		adapterList.push_back(adapter);

		++i;
	}

	for (size_t i = 0; i < adapterList.size(); ++i)
	{
		LogAdapterOutputs(adapterList[i]);
		if (adapterList[i])
		{
			adapterList[i]->Release();
			adapterList[i] = nullptr;
		}
	}
}

void MainWindow::LogAdapterOutputs(IDXGIAdapter* adapter)
{
	UINT i = 0;
	IDXGIOutput* output = nullptr;
	while (adapter->EnumOutputs(i, &output) != DXGI_ERROR_NOT_FOUND)
	{
		DXGI_OUTPUT_DESC desc;
		output->GetDesc(&desc);

		std::wstring text = L"***Output: ";
		text += desc.DeviceName;
		text += L"\n";
		OutputDebugString(text.c_str());

		LogOutputDisplayModes(output, mBackBufferFormat);

		if (output)
		{
			output->Release();
			output = nullptr;
		}

		++i;
	}
}

void MainWindow::LogOutputDisplayModes(IDXGIOutput* output, DXGI_FORMAT format)
{
	UINT count = 0;
	UINT flags = 0;

	// Call with nullptr to get list count.
	output->GetDisplayModeList(format, flags, &count, nullptr);

	std::vector<DXGI_MODE_DESC> modeList(count);
	output->GetDisplayModeList(format, flags, &count, &modeList[0]);

	for (auto& x : modeList)
	{
		UINT n = x.RefreshRate.Numerator;
		UINT d = x.RefreshRate.Denominator;
		std::wstring text =
			L"Width = " + std::to_wstring(x.Width) + L" " +
			L"Height = " + std::to_wstring(x.Height) + L" " +
			L"Refresh = " + std::to_wstring(n) + L"/" + std::to_wstring(d) +
			L"\n";

		::OutputDebugString(text.c_str());
	}
}
