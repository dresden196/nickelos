// NickelOS: a Direct3D 11 program for Windows, for testing how Windows games
// draw on NickelOS (Wine with DXVK, as Proton does: D3D11 on Vulkan, in an X11
// window through Xwayland). It opens a 1280x720 window, draws a spinning
// triangle with HLSL shaders, and prints the adapter Direct3D sees, then the
// frames presented per second, first without vsync for SECONDS, then with
// vsync for SECONDS:
//   adapter NAME (VENDOR:DEVICE)
//   nosync FPS
//   vsync FPS
//   d3d11-test.exe [SECONDS]
// Build (mingw-w64): x86_64-w64-mingw32-g++ -O2 -static -o d3d11-test.exe
//   d3d11-test.cpp -ld3d11 -ldxgi -ld3dcompiler_47 -lgdi32
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>

static const char kShaders[] =
    "cbuffer Spin : register(b0) { float angle; float3 pad; };\n"
    "struct V { float4 pos : SV_Position; float3 color : COLOR; };\n"
    "V vs(uint id : SV_VertexID) {\n"
    "  float2 p[3] = { float2(0, 0.6), float2(0.52, -0.3), "
    "float2(-0.52, -0.3) };\n"
    "  float3 c[3] = { float3(1, 0, 0), float3(0, 1, 0), float3(0, 0, 1) };\n"
    "  float s = sin(angle), k = cos(angle);\n"
    "  V v;\n"
    "  v.pos = float4(p[id].x * k - p[id].y * s, p[id].x * s + p[id].y * k, "
    "0, 1);\n"
    "  v.color = c[id];\n"
    "  return v;\n"
    "}\n"
    "float4 ps(V v) : SV_Target { return float4(v.color, 1); }\n";

static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
  if (msg == WM_DESTROY) {
    PostQuitMessage(0);
    return 0;
  }
  return DefWindowProcA(hwnd, msg, w, l);
}

static ID3DBlob* Compile(const char* entry, const char* target) {
  ID3DBlob* code = nullptr;
  ID3DBlob* errors = nullptr;
  if (FAILED(D3DCompile(kShaders, sizeof(kShaders) - 1, "shaders", nullptr,
                        nullptr, entry, target, 0, 0, &code, &errors))) {
    printf("shader %s: %s\n", entry,
           errors ? (const char*)errors->GetBufferPointer() : "failed");
    exit(1);
  }
  return code;
}

int main(int argc, char** argv) {
  double seconds = argc > 1 ? atof(argv[1]) : 5;
  WNDCLASSA wc = {};
  wc.lpfnWndProc = WindowProc;
  wc.hInstance = GetModuleHandleA(nullptr);
  wc.lpszClassName = "NickelD3D11Test";
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  RegisterClassA(&wc);
  RECT rect = {0, 0, 1280, 720};
  AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
  HWND hwnd = CreateWindowA("NickelD3D11Test", "D3D11 test",
                            WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT,
                            CW_USEDEFAULT, rect.right - rect.left,
                            rect.bottom - rect.top, nullptr, nullptr,
                            wc.hInstance, nullptr);

  DXGI_SWAP_CHAIN_DESC desc = {};
  desc.BufferDesc.Width = 1280;
  desc.BufferDesc.Height = 720;
  desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  desc.BufferCount = 2;
  desc.OutputWindow = hwnd;
  desc.Windowed = TRUE;
  desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  IDXGISwapChain* swapchain = nullptr;
  ID3D11Device* device = nullptr;
  ID3D11DeviceContext* context = nullptr;
  D3D_FEATURE_LEVEL level;
  HRESULT hr = D3D11CreateDeviceAndSwapChain(
      nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
      D3D11_SDK_VERSION, &desc, &swapchain, &device, &level, &context);
  if (FAILED(hr)) {
    printf("D3D11CreateDeviceAndSwapChain failed: 0x%08lx\n", hr);
    return 1;
  }
  IDXGIDevice* dxgi_device = nullptr;
  IDXGIAdapter* adapter = nullptr;
  DXGI_ADAPTER_DESC adapter_desc = {};
  device->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgi_device);
  dxgi_device->GetAdapter(&adapter);
  adapter->GetDesc(&adapter_desc);
  char name[128];
  WideCharToMultiByte(CP_UTF8, 0, adapter_desc.Description, -1, name,
                      sizeof(name), nullptr, nullptr);
  printf("adapter %s (%04x:%04x), feature level %x\n", name,
         adapter_desc.VendorId, adapter_desc.DeviceId, level);
  fflush(stdout);

  ID3D11Texture2D* back = nullptr;
  ID3D11RenderTargetView* target = nullptr;
  swapchain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&back);
  device->CreateRenderTargetView(back, nullptr, &target);
  ID3DBlob* vs_code = Compile("vs", "vs_5_0");
  ID3DBlob* ps_code = Compile("ps", "ps_5_0");
  ID3D11VertexShader* vs = nullptr;
  ID3D11PixelShader* ps = nullptr;
  device->CreateVertexShader(vs_code->GetBufferPointer(),
                             vs_code->GetBufferSize(), nullptr, &vs);
  device->CreatePixelShader(ps_code->GetBufferPointer(),
                            ps_code->GetBufferSize(), nullptr, &ps);
  D3D11_BUFFER_DESC cb_desc = {};
  cb_desc.ByteWidth = 16;
  cb_desc.Usage = D3D11_USAGE_DYNAMIC;
  cb_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  cb_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  ID3D11Buffer* spin = nullptr;
  device->CreateBuffer(&cb_desc, nullptr, &spin);
  D3D11_VIEWPORT viewport = {0, 0, 1280, 720, 0, 1};

  LARGE_INTEGER frequency, start, now;
  QueryPerformanceFrequency(&frequency);
  for (int sync = 0; sync <= 1; ++sync) {
    long frames = 0;
    QueryPerformanceCounter(&start);
    for (;;) {
      MSG msg;
      while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
          return 0;
        }
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
      }
      QueryPerformanceCounter(&now);
      double elapsed =
          double(now.QuadPart - start.QuadPart) / double(frequency.QuadPart);
      if (elapsed >= seconds) {
        printf("%s %.1f\n", sync ? "vsync" : "nosync", frames / elapsed);
        fflush(stdout);
        break;
      }
      D3D11_MAPPED_SUBRESOURCE mapped;
      context->Map(spin, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
      *(float*)mapped.pData = float(elapsed * 2.0);
      context->Unmap(spin, 0);
      const float clear[4] = {0.05f, 0.05f, 0.1f, 1.0f};
      context->OMSetRenderTargets(1, &target, nullptr);
      context->ClearRenderTargetView(target, clear);
      context->RSSetViewports(1, &viewport);
      context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
      context->VSSetShader(vs, nullptr, 0);
      context->VSSetConstantBuffers(0, 1, &spin);
      context->PSSetShader(ps, nullptr, 0);
      context->Draw(3, 0);
      swapchain->Present(sync, 0);
      ++frames;
    }
  }
  return 0;
}
