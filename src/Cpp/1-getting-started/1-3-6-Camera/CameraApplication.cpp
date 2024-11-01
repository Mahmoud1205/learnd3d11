#include "CameraApplication.hpp"
#include "Camera.hpp"
#include "DeviceContext.hpp"
#include "ModelFactory.hpp"
#include "Pipeline.hpp"
#include "PipelineFactory.hpp"
#include "TextureFactory.hpp"

#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#include <d3dcompiler.h>

#include <imgui/backend/imgui_impl_dx11.h>
#include <imgui/backend/imgui_impl_glfw.h>
#include <imgui/imgui.h>

#include <iostream>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "dxguid.lib")

template <UINT TDebugNameLength>
inline void SetDebugName(_In_ ID3D11DeviceChild* deviceResource, _In_z_ const char (&debugName)[TDebugNameLength])
{
    deviceResource->SetPrivateData(WKPDID_D3DDebugObjectName, TDebugNameLength - 1, debugName);
}

CameraApplication::CameraApplication(const std::string& title)
    : ApplicationWithInput(title)
{
}

CameraApplication::~CameraApplication()
{
    _deviceContext->Flush();
    _depthDisabledDepthStencilState.Reset();
    _depthEnabledLessDepthStencilState.Reset();
    _depthEnabledLessEqualDepthStencilState.Reset();
    _depthEnabledAlwaysDepthStencilState.Reset();
    _depthEnabledNeverDepthStencilState.Reset();
    _depthEnabledEqualDepthStencilState.Reset();
    _depthEnabledNotEqualDepthStencilState.Reset();
    _depthEnabledGreaterDepthStencilState.Reset();
    _depthEnabledGreaterEqualDepthStencilState.Reset();

    _wireFrameCullBackRasterizerState.Reset();
    _wireFrameCullFrontRasterizerState.Reset();
    _wireFrameCullNoneRasterizerState.Reset();

    _solidFrameCullBackRasterizerState.Reset();
    _solidFrameCullFrontRasterizerState.Reset();
    _solidFrameCullNoneRasterizerState.Reset();

    _depthStencilView.Reset();
    _cameraConstantBuffer.Reset();
    _textureSrv.Reset();
    _pipeline.reset();
    _pipelineFactory.reset();
    _modelVertices.Reset();
    _modelIndices.Reset();
    _modelFactory.reset();
    DestroySwapchainResources();
    _swapChain.Reset();
    _dxgiFactory.Reset();
    _deviceContext.reset();
#if !defined(NDEBUG)
    _debug->ReportLiveDeviceObjects(D3D11_RLDO_FLAGS::D3D11_RLDO_DETAIL);
    _debug.Reset();
#endif
    _device.Reset();

    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext(_imGuiContext);

    ApplicationWithInput::Cleanup();
}

bool CameraApplication::Initialize()
{
    // This section initializes GLFW and creates a Window
    if (!ApplicationWithInput::Initialize())
    {
        return false;
    }

    // This section initializes DirectX's devices and SwapChain
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&_dxgiFactory))))
    {
        std::cout << "DXGI: Failed to create factory\n";
        return false;
    }

    constexpr D3D_FEATURE_LEVEL deviceFeatureLevel = D3D_FEATURE_LEVEL::D3D_FEATURE_LEVEL_11_0;
    uint32_t deviceFlags = 0;
#if !defined(NDEBUG)
    deviceFlags |= D3D11_CREATE_DEVICE_FLAG::D3D11_CREATE_DEVICE_DEBUG;
#endif

    WRL::ComPtr<ID3D11DeviceContext> deviceContext = nullptr;
    if (FAILED(D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE::D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            deviceFlags,
            &deviceFeatureLevel,
            1,
            D3D11_SDK_VERSION,
            &_device,
            nullptr,
            &deviceContext)))
    {
        std::cout << "D3D11: Failed to create Device and Device Context\n";
        return false;
    }

    if (FAILED(_device.As(&_debug)))
    {
        std::cout << "D3D11: Failed to get the debug layer from the device\n";
        return false;
    }

    InitializeImGui();

    constexpr char deviceName[] = "DEV_Main";
    _device->SetPrivateData(WKPDID_D3DDebugObjectName, sizeof(deviceName), deviceName);
    SetDebugName(deviceContext.Get(), "CTX_Main");

    _deviceContext = std::make_unique<DeviceContext>(_device, std::move(deviceContext));

    DXGI_SWAP_CHAIN_DESC1 swapChainDescriptor = {};
    swapChainDescriptor.Width = GetWindowWidth();
    swapChainDescriptor.Height = GetWindowHeight();
    swapChainDescriptor.Format = DXGI_FORMAT::DXGI_FORMAT_B8G8R8A8_UNORM;
    swapChainDescriptor.SampleDesc.Count = 1;
    swapChainDescriptor.SampleDesc.Quality = 0;
    swapChainDescriptor.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDescriptor.BufferCount = 2;
    swapChainDescriptor.SwapEffect = DXGI_SWAP_EFFECT::DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDescriptor.Scaling = DXGI_SCALING::DXGI_SCALING_STRETCH;
    swapChainDescriptor.Flags = {};

    DXGI_SWAP_CHAIN_FULLSCREEN_DESC swapChainFullscreenDescriptor = {};
    swapChainFullscreenDescriptor.Windowed = true;

    if (FAILED(_dxgiFactory->CreateSwapChainForHwnd(
            _device.Get(),
            glfwGetWin32Window(GetWindow()),
            &swapChainDescriptor,
            &swapChainFullscreenDescriptor,
            nullptr,
            &_swapChain)))
    {
        std::cout << "DXGI: Failed to create SwapChain\n";
        return false;
    }

    CreateSwapchainResources();

    _pipelineFactory = std::make_unique<PipelineFactory>(_device);
    _textureFactory = std::make_unique<TextureFactory>(_device);
    _modelFactory = std::make_unique<ModelFactory>(_device);
    _camera = std::make_unique<PerspectiveCamera>(60.0f, GetWindowWidth(), GetWindowHeight(), 0.1f, 2048.0f);

    return true;
}

bool CameraApplication::Load()
{
    PipelineDescriptor pipelineDescriptor = {};
    pipelineDescriptor.VertexFilePath = L"Assets/Shaders/Main.vs.hlsl";
    pipelineDescriptor.PixelFilePath = L"Assets/Shaders/Main.ps.hlsl";
    pipelineDescriptor.VertexType = VertexType::PositionColorUv;
    if (!_pipelineFactory->CreatePipeline(pipelineDescriptor, _pipeline))
    {
        std::cout << "PipelineFactory: Failed to create pipeline\n";
        return false;
    }

    _pipeline->SetViewport(
        0.0f,
        0.0f,
        static_cast<float>(GetWindowWidth()),
        static_cast<float>(GetWindowHeight()));

    if (!_textureFactory->CreateShaderResourceViewFromFile(L"Assets/Textures/T_Atlas.dds", _textureSrv))
    {
        return false;
    }

    _pipeline->BindTexture(0, _textureSrv.Get());

    D3D11_SAMPLER_DESC linearSamplerStateDescriptor = {};
    linearSamplerStateDescriptor.Filter = D3D11_FILTER::D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    linearSamplerStateDescriptor.AddressU = D3D11_TEXTURE_ADDRESS_MODE::D3D11_TEXTURE_ADDRESS_WRAP;
    linearSamplerStateDescriptor.AddressV = D3D11_TEXTURE_ADDRESS_MODE::D3D11_TEXTURE_ADDRESS_WRAP;
    linearSamplerStateDescriptor.AddressW = D3D11_TEXTURE_ADDRESS_MODE::D3D11_TEXTURE_ADDRESS_WRAP;
    if (FAILED(_device->CreateSamplerState(&linearSamplerStateDescriptor, &_linearSamplerState)))
    {
        std::cout << "D3D11: Failed to create linear sampler state\n";
        return false;
    }

    _pipeline->BindSampler(0, _linearSamplerState.Get());

    if (!_modelFactory->LoadModel(
            "Assets/Models/SM_Deccer_Cubes_Merged_Texture_Atlas.fbx",
            _modelVertices,
            &_modelVertexCount,
            _modelIndices,
            &_modelIndexCount))
    {
        return false;
    }

    const D3D11_BUFFER_DESC cameraConstantBufferDescriptor = CD3D11_BUFFER_DESC(
        sizeof(CameraConstants),
        D3D11_BIND_FLAG::D3D11_BIND_CONSTANT_BUFFER);

    if (FAILED(_device->CreateBuffer(&cameraConstantBufferDescriptor, nullptr, &_cameraConstantBuffer)))
    {
        std::cout << "D3D11: Failed to create CameraConstants buffer\n";
        return false;
    }
    SetDebugName(_cameraConstantBuffer.Get(), "CB_Camera");

    const D3D11_BUFFER_DESC objectConstantBufferDescriptor = CD3D11_BUFFER_DESC(
        sizeof(DirectX::XMMATRIX),
        D3D11_BIND_FLAG::D3D11_BIND_CONSTANT_BUFFER);

    if (FAILED(_device->CreateBuffer(&objectConstantBufferDescriptor, nullptr, &_objectConstantBuffer)))
    {
        std::cout << "D3D11: Failed to create ObjectConstants buffer\n";
        return false;
    }
    SetDebugName(_objectConstantBuffer.Get(), "CB_Object");

    _pipeline->BindVertexStageConstantBuffer(0, _cameraConstantBuffer.Get());
    _pipeline->BindVertexStageConstantBuffer(1, _objectConstantBuffer.Get());

    if (!CreateDepthStencilStates())
    {
        return false;
    }

    if (!CreateRasterizerStates())
    {
        return false;
    }

    _camera->SetPosition(DirectX::XMFLOAT3{ 0.0f, 50.0f, 400.0f });
    _camera->SetDirection(DirectX::XMFLOAT3{ 0.0f, 0.0f, 1.0f });
    _camera->SetUp(DirectX::XMFLOAT3{ 0.0f, 1.0f, 0.0f });

    return true;
}

bool CameraApplication::CreateSwapchainResources()
{
    WRL::ComPtr<ID3D11Texture2D> backBuffer = nullptr;
    if (FAILED(_swapChain->GetBuffer(
            0,
            IID_PPV_ARGS(&backBuffer))))
    {
        std::cout << "D3D11: Failed to get back buffer from swapchain\n";
        return false;
    }

    if (FAILED(_device->CreateRenderTargetView(
            backBuffer.Get(),
            nullptr,
            &_renderTarget)))
    {
        std::cout << "D3D11: Failed to create rendertarget view from back buffer\n";
        return false;
    }

    WRL::ComPtr<ID3D11Texture2D> depthBuffer = nullptr;

    D3D11_TEXTURE2D_DESC depthStencilBufferDescriptor = {};
    depthStencilBufferDescriptor.ArraySize = 1;
    depthStencilBufferDescriptor.BindFlags = D3D11_BIND_FLAG::D3D11_BIND_DEPTH_STENCIL;
    depthStencilBufferDescriptor.CPUAccessFlags = 0;
    depthStencilBufferDescriptor.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthStencilBufferDescriptor.Width = GetWindowWidth();
    depthStencilBufferDescriptor.Height = GetWindowHeight();
    depthStencilBufferDescriptor.MipLevels = 1;
    depthStencilBufferDescriptor.SampleDesc.Count = 1;
    depthStencilBufferDescriptor.SampleDesc.Quality = 0;
    depthStencilBufferDescriptor.Usage = D3D11_USAGE::D3D11_USAGE_DEFAULT;
    if (FAILED(_device->CreateTexture2D(
            &depthStencilBufferDescriptor,
            nullptr,
            &depthBuffer)))
    {
        std::cout << "D3D11: Failed to create depth buffer\n";
        return false;
    }

    if (FAILED(_device->CreateDepthStencilView(
            depthBuffer.Get(),
            nullptr,
            &_depthStencilView)))
    {
        std::cout << "D3D11: Failed to create shaderresource view from back buffer\n";
        return false;
    }

    return true;
}

void CameraApplication::DestroySwapchainResources()
{
    _depthStencilView.Reset();
    _renderTarget.Reset();
}

void CameraApplication::OnResize(
    const int32_t width,
    const int32_t height)
{
    Application::OnResize(width, height);
    _deviceContext->Flush();

    DestroySwapchainResources();

    if (FAILED(_swapChain->ResizeBuffers(
            0,
            width,
            height,
            DXGI_FORMAT::DXGI_FORMAT_B8G8R8A8_UNORM,
            0)))
    {
        std::cout << "D3D11: Failed to recreate swapchain buffers\n";
        return;
    }

    CreateSwapchainResources();

    _camera->Resize(width, height);

    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(
        static_cast<float>(width),
        static_cast<float>(height));
}

void CameraApplication::Update()
{
    ApplicationWithInput::Update();

    if (IsKeyDown(GLFW_KEY_ESCAPE))
    {
        Close();
    }

    float speed = 0.0f;
    if (IsKeyPressed(GLFW_KEY_W))
    {
        speed = 0.1f;
        _camera->Move(speed);
    }

    if (IsKeyPressed(GLFW_KEY_S))
    {
        speed = -0.1f;
        _camera->Move(speed);
    }

    if (IsKeyPressed(GLFW_KEY_A))
    {
        speed = -0.1f;
        _camera->Slide(speed);
    }

    if (IsKeyPressed(GLFW_KEY_D))
    {
        speed = 0.1f;
        _camera->Slide(speed);
    }

    if (IsButtonPressed(GLFW_MOUSE_BUTTON_1))
    {
        _camera->AddYaw(DeltaPosition.x * 0.1f);
        _camera->AddPitch(DeltaPosition.y * 0.1f);
    }

    static float angle = 0.0f;
    if (_toggledRotation)
    {
        angle += 90.0f * (10.0f / 60000.0f);
    }
    else
    {
        angle -= 90.0f * (10.0f / 60000.0f);
    }

    DirectX::XMMATRIX rotationMatrix = DirectX::XMMatrixRotationY(DirectX::XMConvertToRadians(angle));
    DirectX::XMStoreFloat4x4(&_worldMatrix, rotationMatrix);
    _deviceContext->UpdateSubresource(_objectConstantBuffer.Get(), &_worldMatrix);
}

void CameraApplication::Render()
{
    _camera->Update();
    CameraConstants& cameraConstants = _camera->GetCameraConstants();
    _deviceContext->UpdateSubresource(_cameraConstantBuffer.Get(), &cameraConstants);

    float clearColor[] = { 0.1f, 0.1f, 0.1f, 1.0f };

    _deviceContext->Clear(
        _renderTarget.Get(),
        clearColor,
        _depthStencilView.Get(),
        1.0f);
    _deviceContext->SetPipeline(_pipeline.get());
    _deviceContext->SetVertexBuffer(_modelVertices.Get(), 0);
    _deviceContext->SetIndexBuffer(_modelIndices.Get(), 0);

    _deviceContext->DrawIndexed();

    RenderUi();
    _swapChain->Present(1, 0);
}

void CameraApplication::RenderUi()
{
    ImGui_ImplDX11_NewFrame();
    ImGui::NewFrame();

    if (ImGui::Begin("Hello Froge"))
    {
        ImGui::Checkbox("Toggle Rotation", &_toggledRotation);

        ImGui::TextUnformatted("Depth State");
        ImGui::RadioButton("Disabled", &_selectedDepthFunction, 0);
        ImGui::RadioButton("Less", &_selectedDepthFunction, 1);
        ImGui::RadioButton("LessEqual", &_selectedDepthFunction, 2);
        ImGui::RadioButton("Greater", &_selectedDepthFunction, 3);
        ImGui::RadioButton("GreaterEqual", &_selectedDepthFunction, 4);
        ImGui::RadioButton("Equal", &_selectedDepthFunction, 5);
        ImGui::RadioButton("NotEqual", &_selectedDepthFunction, 6);
        ImGui::RadioButton("Always", &_selectedDepthFunction, 7);
        ImGui::RadioButton("Never", &_selectedDepthFunction, 8);

        switch (_selectedDepthFunction)
        {
        case 0:
            _pipeline->SetDepthStencilState(_depthDisabledDepthStencilState.Get());
            break;
        case 1:
            _pipeline->SetDepthStencilState(_depthEnabledLessDepthStencilState.Get());
            break;
        case 2:
            _pipeline->SetDepthStencilState(_depthEnabledLessEqualDepthStencilState.Get());
            break;
        case 3:
            _pipeline->SetDepthStencilState(_depthEnabledGreaterDepthStencilState.Get());
            break;
        case 4:
            _pipeline->SetDepthStencilState(_depthEnabledGreaterEqualDepthStencilState.Get());
            break;
        case 5:
            _pipeline->SetDepthStencilState(_depthEnabledEqualDepthStencilState.Get());
            break;
        case 6:
            _pipeline->SetDepthStencilState(_depthEnabledNotEqualDepthStencilState.Get());
            break;
        case 7:
            _pipeline->SetDepthStencilState(_depthEnabledAlwaysDepthStencilState.Get());
            break;
        case 8:
            _pipeline->SetDepthStencilState(_depthEnabledNeverDepthStencilState.Get());
            break;
        }

        ImGui::TextUnformatted("Rasterizer State");
        ImGui::Checkbox("Wireframe", &_isWireframe);
        ImGui::TextUnformatted("Cull");
        ImGui::RadioButton("Front", &_selectedRasterizerState, 10);
        ImGui::RadioButton("Back", &_selectedRasterizerState, 11);
        ImGui::RadioButton("None", &_selectedRasterizerState, 12);

        if (_isWireframe)
        {
            switch (_selectedRasterizerState)
            {
            case 10:
                _pipeline->SetRasterizerState(_wireFrameCullFrontRasterizerState.Get());
                break;
            case 11:
                _pipeline->SetRasterizerState(_wireFrameCullBackRasterizerState.Get());
                break;
            case 12:
                _pipeline->SetRasterizerState(_wireFrameCullNoneRasterizerState.Get());
                break;
            }
        }
        else
        {
            switch (_selectedRasterizerState)
            {
            case 10:
                _pipeline->SetRasterizerState(_solidFrameCullFrontRasterizerState.Get());
                break;
            case 11:
                _pipeline->SetRasterizerState(_solidFrameCullBackRasterizerState.Get());
                break;
            case 12:
                _pipeline->SetRasterizerState(_solidFrameCullNoneRasterizerState.Get());
                break;
            }
        }

        ImGui::End();
    }

    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

void CameraApplication::InitializeImGui()
{
    _imGuiContext = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(
        static_cast<float>(GetWindowWidth()),
        static_cast<float>(GetWindowHeight()));

    ImGui_ImplGlfw_InitForOther(GetWindow(), true);
}

bool CameraApplication::CreateDepthStencilStates()
{
    D3D11_DEPTH_STENCIL_DESC depthStencilDescriptor = {};
    depthStencilDescriptor.DepthEnable = false;
    depthStencilDescriptor.DepthWriteMask = D3D11_DEPTH_WRITE_MASK::D3D11_DEPTH_WRITE_MASK_ALL;
    depthStencilDescriptor.DepthFunc = D3D11_COMPARISON_FUNC::D3D11_COMPARISON_LESS;
    depthStencilDescriptor.StencilEnable = false;

    if (FAILED(_device->CreateDepthStencilState(&depthStencilDescriptor, &_depthDisabledDepthStencilState)))
    {
        std::cout << "D3D11: Failed to create disabled depth stencil state\n";
        return false;
    }

    depthStencilDescriptor.DepthEnable = true;
    if (FAILED(_device->CreateDepthStencilState(&depthStencilDescriptor, &_depthEnabledLessDepthStencilState)))
    {
        std::cout << "D3D11: Failed to create enabled depth stencil state\n";
        return false;
    }

    depthStencilDescriptor.DepthFunc = D3D11_COMPARISON_FUNC::D3D11_COMPARISON_LESS_EQUAL;
    if (FAILED(_device->CreateDepthStencilState(&depthStencilDescriptor, &_depthEnabledLessEqualDepthStencilState)))
    {
        std::cout << "D3D11: Failed to create enabled depth stencil state\n";
        return false;
    }

    depthStencilDescriptor.DepthFunc = D3D11_COMPARISON_FUNC::D3D11_COMPARISON_ALWAYS;
    if (FAILED(_device->CreateDepthStencilState(&depthStencilDescriptor, &_depthEnabledAlwaysDepthStencilState)))
    {
        std::cout << "D3D11: Failed to create enabled depth stencil state\n";
        return false;
    }

    depthStencilDescriptor.DepthFunc = D3D11_COMPARISON_FUNC::D3D11_COMPARISON_NEVER;
    if (FAILED(_device->CreateDepthStencilState(&depthStencilDescriptor, &_depthEnabledNeverDepthStencilState)))
    {
        std::cout << "D3D11: Failed to create enabled depth stencil state\n";
        return false;
    }

    depthStencilDescriptor.DepthFunc = D3D11_COMPARISON_FUNC::D3D11_COMPARISON_GREATER;
    if (FAILED(_device->CreateDepthStencilState(&depthStencilDescriptor, &_depthEnabledGreaterDepthStencilState)))
    {
        std::cout << "D3D11: Failed to create enabled depth stencil state\n";
        return false;
    }

    depthStencilDescriptor.DepthFunc = D3D11_COMPARISON_FUNC::D3D11_COMPARISON_GREATER_EQUAL;
    if (FAILED(_device->CreateDepthStencilState(&depthStencilDescriptor, &_depthEnabledGreaterEqualDepthStencilState)))
    {
        std::cout << "D3D11: Failed to create enabled depth stencil state\n";
        return false;
    }

    depthStencilDescriptor.DepthFunc = D3D11_COMPARISON_FUNC::D3D11_COMPARISON_EQUAL;
    if (FAILED(_device->CreateDepthStencilState(&depthStencilDescriptor, &_depthEnabledEqualDepthStencilState)))
    {
        std::cout << "D3D11: Failed to create enabled depth stencil state\n";
        return false;
    }

    depthStencilDescriptor.DepthFunc = D3D11_COMPARISON_FUNC::D3D11_COMPARISON_NOT_EQUAL;
    if (FAILED(_device->CreateDepthStencilState(&depthStencilDescriptor, &_depthEnabledNotEqualDepthStencilState)))
    {
        std::cout << "D3D11: Failed to create enabled depth stencil state\n";
        return false;
    }

    return true;
}

bool CameraApplication::CreateRasterizerStates()
{
    D3D11_RASTERIZER_DESC rasterizerStateDescriptor = {};
    rasterizerStateDescriptor.AntialiasedLineEnable = false;
    rasterizerStateDescriptor.DepthBias = 0;
    rasterizerStateDescriptor.DepthBiasClamp = 0.0f;
    rasterizerStateDescriptor.DepthClipEnable = true;
    rasterizerStateDescriptor.FrontCounterClockwise = true;
    rasterizerStateDescriptor.MultisampleEnable = false;
    rasterizerStateDescriptor.ScissorEnable = false;
    rasterizerStateDescriptor.SlopeScaledDepthBias = 0.0f;

    rasterizerStateDescriptor.FillMode = D3D11_FILL_MODE::D3D11_FILL_SOLID;

    rasterizerStateDescriptor.CullMode = D3D11_CULL_MODE::D3D11_CULL_BACK;
    if (FAILED(_device->CreateRasterizerState(&rasterizerStateDescriptor, &_solidFrameCullBackRasterizerState)))
    {
        std::cout << "D3D11: Failed to create rasterizer state\n";
        return false;
    }

    rasterizerStateDescriptor.CullMode = D3D11_CULL_MODE::D3D11_CULL_FRONT;
    if (FAILED(_device->CreateRasterizerState(&rasterizerStateDescriptor, &_solidFrameCullFrontRasterizerState)))
    {
        std::cout << "D3D11: Failed to create rasterizer state\n";
        return false;
    }

    rasterizerStateDescriptor.CullMode = D3D11_CULL_MODE::D3D11_CULL_NONE;
    if (FAILED(_device->CreateRasterizerState(&rasterizerStateDescriptor, &_solidFrameCullNoneRasterizerState)))
    {
        std::cout << "D3D11: Failed to create rasterizer state\n";
        return false;
    }

    rasterizerStateDescriptor.FillMode = D3D11_FILL_MODE::D3D11_FILL_WIREFRAME;

    rasterizerStateDescriptor.CullMode = D3D11_CULL_MODE::D3D11_CULL_BACK;
    if (FAILED(_device->CreateRasterizerState(&rasterizerStateDescriptor, &_wireFrameCullBackRasterizerState)))
    {
        std::cout << "D3D11: Failed to create rasterizer state\n";
        return false;
    }

    rasterizerStateDescriptor.CullMode = D3D11_CULL_MODE::D3D11_CULL_FRONT;
    if (FAILED(_device->CreateRasterizerState(&rasterizerStateDescriptor, &_wireFrameCullFrontRasterizerState)))
    {
        std::cout << "D3D11: Failed to create rasterizer state\n";
        return false;
    }

    rasterizerStateDescriptor.CullMode = D3D11_CULL_MODE::D3D11_CULL_NONE;
    if (FAILED(_device->CreateRasterizerState(&rasterizerStateDescriptor, &_wireFrameCullNoneRasterizerState)))
    {
        std::cout << "D3D11: Failed to create rasterizer state\n";
        return false;
    }

    return true;
}
