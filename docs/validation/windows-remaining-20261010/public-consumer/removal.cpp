#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <iostream>
using Microsoft::WRL::ComPtr;
int main(){ComPtr<IDXGIFactory4> factory;HRESULT h=CreateDXGIFactory1(IID_PPV_ARGS(&factory));if(FAILED(h))return 77;
for(UINT i=0;;++i){ComPtr<IDXGIAdapter1>a;if(factory->EnumAdapters1(i,&a)==DXGI_ERROR_NOT_FOUND)break;DXGI_ADAPTER_DESC1 d{};a->GetDesc1(&d);if(d.VendorId!=0x10de || (d.Flags&DXGI_ADAPTER_FLAG_SOFTWARE))continue;
ComPtr<ID3D12Device> device;h=D3D12CreateDevice(a.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device));if(FAILED(h))return 77;ComPtr<ID3D12Device5>five;h=device.As(&five);std::wcout<<L"adapter="<<d.Description<<L" vendor="<<std::hex<<d.VendorId<<L" device="<<d.DeviceId<<L" device5_query_hr="<<h<<L" removed_before="<<device->GetDeviceRemovedReason()<<L" removal_invoked=0\n";return SUCCEEDED(h)?0:77;}return 77;}
