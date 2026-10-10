#include <windows.h>
#include <Inventor/SoDB.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <Inventor/rendering/CoinRenderNativeSurface.h>
#include <Inventor/rendering/CoinRenderCapabilities.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoTranslation.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <chrono>
#include <fstream>
#include <string>
#include <iostream>
#include <vector>
#include <memory>
#include <cmath>
#include <algorithm>
using Clock=std::chrono::steady_clock;
constexpr int W=640,H=480;
static unsigned events=0;static SoOrthographicCamera* camera=nullptr;
LRESULT CALLBACK wnd(HWND h,UINT m,WPARAM w,LPARAM l){if(m==WM_MOUSEMOVE && w==MK_LBUTTON && camera){++events;camera->position=SbVec3f(.01f*int(LOWORD(l)),0,180);return 0;}return DefWindowProcW(h,m,w,l);}
static void pump(){MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}}
static void ppm(const std::string&p,const std::vector<unsigned char>&v){if(v.size()!=W*H*4)return;std::ofstream f(p,std::ios::binary);f<<"P6\n"<<W<<' '<<H<<"\n255\n";for(size_t i=0;i<v.size();i+=4)f.write((const char*)&v[i],3);}
static bool capture(HWND h,std::vector<unsigned char>&v){pump();Sleep(50);HDC dc=GetDC(h),mem=CreateCompatibleDC(dc);BITMAPINFO b{};b.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);b.bmiHeader.biWidth=W;b.bmiHeader.biHeight=-H;b.bmiHeader.biPlanes=1;b.bmiHeader.biBitCount=32;void*d=nullptr;HBITMAP bmp=CreateDIBSection(dc,&b,DIB_RGB_COLORS,&d,nullptr,0);auto old=SelectObject(mem,bmp);bool ok=BitBlt(mem,0,0,W,H,dc,0,0,SRCCOPY|CAPTUREBLT);GdiFlush();if(ok){v.resize(W*H*4);auto*x=(unsigned char*)d;for(size_t i=0;i<v.size();i+=4){v[i]=x[i+2];v[i+1]=x[i+1];v[i+2]=x[i];v[i+3]=255;}}SelectObject(mem,old);DeleteObject(bmp);DeleteDC(mem);ReleaseDC(h,dc);return ok;}
int main(int argc,char**argv){std::cout<<std::unitbuf;if(argc!=7){std::cerr<<"api policy workload frames warmup output-prefix\n";return 2;}
std::string api=argv[1],policy=argv[2],work=argv[3],prefix=argv[6];int frames=std::stoi(argv[4]),warmup=std::stoi(argv[5]);if(frames<1||warmup<0)return 2;
CoinRenderRenderer renderer=api=="d3d12"?COIN_RENDER_RENDERER_D3D12:api=="vulkan"?COIN_RENDER_RENDERER_VULKAN:api=="opengl"?COIN_RENDER_RENDERER_OPENGL:COIN_RENDER_RENDERER_UNKNOWN;
if(renderer==COIN_RENDER_RENDERER_UNKNOWN || (policy!="native"&&policy!="portable"))return 2;
if(work!="static"&&work!="camera-events"&&work!="transforms"&&work!="materials"&&work!="geometry"&&work!="million")return 2;
SoDB::init();for(const char* dll : {"Coin4.dll","CoinRender4.dll"}) { char modulePath[MAX_PATH]{}; HMODULE module=GetModuleHandleA(dll); if(!module || !GetModuleFileNameA(module,modulePath,MAX_PATH)) return 2; std::cout << "loaded_module name=" << dll << " path=" << modulePath << std::endl; }CoinRenderAction::initClass();SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);WNDCLASSW c{};c.lpfnWndProc=wnd;c.hInstance=GetModuleHandleW(nullptr);c.lpszClassName=L"CoinWin32Campaign";RegisterClassW(&c);HWND hwnd=CreateWindowW(c.lpszClassName,L"Coin Win32 campaign",WS_POPUP,30,80,W,H,nullptr,nullptr,c.hInstance,nullptr);if(!hwnd)return 77;SetWindowPos(hwnd,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);ShowWindow(hwnd,SW_SHOWNOACTIVATE);pump();
CoinRenderCapabilities caps{};if(coin_render_query_capabilities_for_renderer(COIN_RENDER_EXPERIMENTAL_WIN32_WINDOW,renderer,&caps,sizeof(caps)) || caps.renderer!=renderer || !caps.gpu_available || caps.vendor_id!=0x10de){std::cerr<<"requested physical API unavailable\n";return 77;}
std::cout<<"receipt renderer="<<caps.renderer<<" vendor="<<std::hex<<caps.vendor_id<<" device="<<caps.device_id<<std::dec<<" adapter="<<caps.adapter_name<<" workload="<<work<<" policy="<<policy<<" extent=640x480 dpi="<<GetDpiForWindow(hwnd)<<'\n';
CoinRenderOptions options;options.renderer=renderer;options.textureSamplingPolicy=policy=="native"?COIN_RENDER_SAMPLING_NATIVE:COIN_RENDER_SAMPLING_PORTABLE;CoinRenderNativeSurfaceDescriptor desc{};desc.abiVersion=COIN_RENDER_NATIVE_SURFACE_ABI_VERSION;desc.structSize=sizeof(desc);desc.type=COIN_RENDER_SURFACE_WIN32;desc.native.win32.hinstance=c.hInstance;desc.native.win32.hwnd=hwnd;
std::unique_ptr<CoinRenderTarget>target(CoinRenderTarget::createWindow(desc,SbVec2i32(W,H),options));if(!target||target->getStatus()!=CoinRenderTarget::TARGET_READY){std::cerr<<"target not ready\n";return 1;}
auto*root=new SoSeparator;root->ref();camera=new SoOrthographicCamera;camera->height=58;camera->position=SbVec3f(0,0,180);camera->nearDistance=.1f;camera->farDistance=300;root->addChild(camera);auto*light=new SoLightModel;light->model=SoLightModel::BASE_COLOR;root->addChild(light);
std::vector<SoTranslation*>moves;std::vector<SoMaterial*>materials;std::vector<SoCube*>cubes;size_t triangles=0;
if(work=="million") {auto*m=new SoMaterial;m->diffuseColor=SbColor(.3f,.6f,.4f);root->addChild(m);auto*coord=new SoCoordinate3;std::vector<SbVec3f>points;std::vector<int32_t>indices;points.reserve(1001*501);indices.reserve(500000*5);for(int y=0;y<=500;++y)for(int x=0;x<=1000;++x)points.emplace_back((x-500)*.045f,(y-250)*.09f,0);for(int y=0;y<500;++y)for(int x=0;x<1000;++x){int a=y*1001+x;indices.insert(indices.end(),{a,a+1,a+1002,a+1001,-1});}coord->point.setValues(0,int(points.size()),points.data());root->addChild(coord);auto*f=new SoIndexedFaceSet;f->coordIndex.setValues(0,int(indices.size()),indices.data());root->addChild(f);triangles=1000000;}
else for(int y=0;y<50;++y)for(int x=0;x<50;++x){auto*s=new SoSeparator;auto*t=new SoTranslation;t->translation=SbVec3f(x-24.5f,y-24.5f,0);s->addChild(t);moves.push_back(t);auto*m=new SoMaterial;m->diffuseColor=SbColor(.3f,.6f,.4f);s->addChild(m);materials.push_back(m);auto*b=new SoCube;b->width=.7f;b->height=.7f;b->depth=.5f;s->addChild(b);cubes.push_back(b);root->addChild(s);triangles+=12;}
CoinRenderAction action(SbViewportRegion(W,H));action.setRenderTarget(target.get());auto render=[&](){auto old=target->getLastSubmissionSerial();action.apply(root);if(action.getLastStatus()!=CoinRenderAction::SUCCESS){std::cerr<<"render failed status="<<action.getLastStatus()<<" diagnostic="<<action.getLastError().getString()<<'\n';return false;}return target->getLastSubmissionSerial()>old;};
const bool externalCapture=api=="opengl" && std::string(caps.adapter_name).find("BGFX")==std::string::npos;auto oracle=[&](const char*stage){if(!externalCapture)target->requestWindowReadbackRGBA();if(!render())return false;if(externalCapture && !render())return false;std::vector<unsigned char>a,b;target->readbackRGBA(a);bool gdi=a.empty();if(gdi && !capture(hwnd,a))return false;std::unique_ptr<CoinRenderTarget>off(CoinRenderTarget::createOffscreen(SbVec2i32(W,H),options));CoinRenderAction ref(SbViewportRegion(W,H));ref.setRenderTarget(off.get());ref.apply(root);off->readbackRGBA(b);bool ok=ref.getLastStatus()==CoinRenderAction::SUCCESS && a.size()==W*H*4 && a.size()==b.size();int maximum=0;if(ok)for(size_t i=0;i<a.size();++i)if(i%4!=3)maximum=std::max(maximum,std::abs(int(a[i])-int(b[i])));size_t coverage=0;if(a.size()==W*H*4)for(size_t i=0;i<a.size();i+=4)if(a[i]||a[i+1]||a[i+2])++coverage;ok=ok&&maximum==0&&coverage>size_t(W*H/10);std::cout<<"visible_coverage="<<coverage<<" of="<<W*H<<'\n';ppm(prefix+"-"+stage+"-window.ppm",a);ppm(prefix+"-"+stage+"-offscreen.ppm",b);std::cout<<"surface_oracle stage="<<stage<<" rgb_max="<<maximum<<" exact="<<ok<<" gdi="<<gdi<<" outside_measurement=1\n";ref.setRenderTarget(nullptr);return ok;};
std::cout<<"scene nodes="<<cubes.size()<<" triangles="<<triangles<<" textured=0\n";if(!oracle("initial"))return 1;
std::ofstream csv(prefix+".csv");csv<<"frame,warmup,event_ms,update_ms,render_present_ms,total_ms,serial\n";bool ok=true;
for(int i=-warmup;i<frames&&ok;++i){auto start=Clock::now();if(work=="camera-events")PostMessageW(hwnd,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM((i+warmup)%80,50));pump();auto eventDone=Clock::now();for(size_t j=0;j<cubes.size();j+=10){float v=std::sin(float(i)*.05f+j*.01f);if(work=="transforms"){auto p=moves[j]->translation.getValue();p[2]=.15f*v;moves[j]->translation=p;}else if(work=="materials")materials[j]->diffuseColor=SbColor(.3f+.1f*v,.6f,.4f);else if(work=="geometry")cubes[j]->width=.7f+.1f*v;}auto updated=Clock::now();ok=render();auto end=Clock::now();RECT rect{};GetClientRect(hwnd,&rect);ok=ok&&rect.right==W&&rect.bottom==H;auto ms=[](auto a,auto b){return std::chrono::duration<double,std::milli>(b-a).count();};csv<<i<<','<<(i<0)<<','<<ms(start,eventDone)<<','<<ms(eventDone,updated)<<','<<ms(updated,end)<<','<<ms(start,end)<<','<<target->getLastSubmissionSerial()<<'\n';}
csv.flush();ok=ok&&bool(csv);if(ok)ok=oracle("final");std::cout<<"window_campaign result="<<ok<<" frames="<<frames<<" warmup="<<warmup<<" readback_during_measurement=none metric=CPU_render_present_return display_latency=0 gpu_duration=0 event_source=PostMessage event_count="<<events<<'\n';action.setRenderTarget(nullptr);target.reset();camera=nullptr;root->unref();DestroyWindow(hwnd);return ok?0:1;}
