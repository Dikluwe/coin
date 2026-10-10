#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <thread>
#undef Status
#include <cstdlib>
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
#include <Inventor/nodes/SoDirectionalLight.h>
#include <Inventor/nodes/SoSphere.h>
#include <Inventor/nodes/SoCone.h>
#include <Inventor/nodes/SoCylinder.h>
#include <Inventor/nodes/SoTexture2.h>
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
constexpr int W=1280,H=960;
static unsigned events=0;static SoOrthographicCamera* camera=nullptr;
static void pump(Display* dpy){while(XPending(dpy)){XEvent e{};XNextEvent(dpy,&e);++events;}}

static void ppm(const std::string&p,const std::vector<unsigned char>&v){if(v.size()!=W*H*4)return;std::ofstream f(p,std::ios::binary);f<<"P6\n"<<W<<' '<<H<<"\n255\n";for(size_t i=0;i<v.size();i+=4)f.write((const char*)&v[i],3);}
static bool capture(Display* dpy,Window win,std::vector<unsigned char>&v){pump(dpy);XSync(dpy,False);std::this_thread::sleep_for(std::chrono::milliseconds(50));XImage* im=XGetImage(dpy,win,0,0,W,H,AllPlanes,ZPixmap);if(!im)return false;auto chan=[](unsigned long pixel,unsigned long mask){if(!mask)return 0u;unsigned shift=0;while(((mask>>shift)&1ul)==0ul)++shift;unsigned long bits=mask>>shift;unsigned long value=(pixel&mask)>>shift;return unsigned((value*255ul+bits/2ul)/bits);};v.resize(size_t(W)*H*4);for(int y=0;y<H;++y)for(int x=0;x<W;++x){auto p=XGetPixel(im,x,y);size_t i=(size_t(y)*W+x)*4;v[i]=chan(p,im->red_mask);v[i+1]=chan(p,im->green_mask);v[i+2]=chan(p,im->blue_mask);v[i+3]=255;}XDestroyImage(im);return true;}
int main(int argc,char**argv){std::cout<<std::unitbuf;if(argc!=7){std::cerr<<"api policy workload frames warmup output-prefix\n";return 2;}
std::string api=argv[1],policy=argv[2],work=argv[3],prefix=argv[6];int frames=std::stoi(argv[4]),warmup=std::stoi(argv[5]);if(frames<1||warmup<0)return 2;
CoinRenderRenderer renderer=api=="d3d12"?COIN_RENDER_RENDERER_D3D12:api=="vulkan"?COIN_RENDER_RENDERER_VULKAN:api=="opengl"?COIN_RENDER_RENDERER_OPENGL:COIN_RENDER_RENDERER_UNKNOWN;
if(renderer==COIN_RENDER_RENDERER_UNKNOWN || (policy!="native"&&policy!="portable"))return 2;
if(work!="city-40000"&&work!="city-million"&&work!="terrain-million"&&work!="solids")return 2;
SoDB::init();CoinRenderAction::initClass();Display* dpy=XOpenDisplay(nullptr);if(!dpy){std::cerr<<"cannot open X11 display\n";return 77;}int screen=DefaultScreen(dpy);Window win=XCreateSimpleWindow(dpy,RootWindow(dpy,screen),40,40,W,H,0,BlackPixel(dpy,screen),BlackPixel(dpy,screen));XStoreName(dpy,win,"Coin Linux visual campaign");XSelectInput(dpy,win,StructureNotifyMask);XMapRaised(dpy,win);XFlush(dpy);for(int n=0;n<100;++n){XWindowAttributes a{};XGetWindowAttributes(dpy,win,&a);if(a.map_state==IsViewable)break;std::this_thread::sleep_for(std::chrono::milliseconds(20));}pump(dpy);
CoinRenderCapabilities caps{};if(coin_render_query_capabilities_for_renderer(COIN_RENDER_EXPERIMENTAL_XLIB_WINDOW,renderer,&caps,sizeof(caps)) || caps.renderer!=renderer || !caps.gpu_available || (caps.vendor_id!=0x1002 && caps.vendor_id!=0x10de && !(renderer==COIN_RENDER_RENDERER_OPENGL && caps.vendor_id==0))){std::cerr<<"requested physical API unavailable vendor="<<std::hex<<caps.vendor_id<<std::dec<<" diagnostic="<<caps.diagnostic<<"\n";return 77;}
std::cout<<"receipt renderer="<<caps.renderer<<" vendor="<<std::hex<<caps.vendor_id<<" device="<<caps.device_id<<std::dec<<" adapter="<<caps.adapter_name<<" workload="<<work<<" policy="<<policy<<" extent=1280x960\n";
CoinRenderOptions options;options.renderer=renderer;options.textureSamplingPolicy=policy=="native"?COIN_RENDER_SAMPLING_NATIVE:COIN_RENDER_SAMPLING_PORTABLE;CoinRenderNativeSurfaceDescriptor desc{};desc.abiVersion=COIN_RENDER_NATIVE_SURFACE_ABI_VERSION;desc.structSize=sizeof(desc);desc.type=COIN_RENDER_SURFACE_XLIB;desc.native.xlib.display=dpy;desc.native.xlib.window=win;
std::unique_ptr<CoinRenderTarget>target(CoinRenderTarget::createWindow(desc,SbVec2i32(W,H),options));if(!target||target->getStatus()!=CoinRenderTarget::TARGET_READY){std::cerr<<"target not ready\n";return 1;}
auto*root=new SoSeparator;root->ref();camera=new SoOrthographicCamera;camera->height=work=="city-40000"?(std::getenv("COIN_VISUAL_CITY_HEIGHT_155")?155:140):work=="city-million"?155:work=="terrain-million"?90:30;camera->nearDistance=.1f;camera->farDistance=600;root->addChild(camera);
auto*light=new SoLightModel;light->model=SoLightModel::PHONG;root->addChild(light);
auto*sun=new SoDirectionalLight;sun->direction=SbVec3f(-.5f,-1,-.3f);sun->intensity=.85f;root->addChild(sun);
auto*fill=new SoDirectionalLight;fill->direction=SbVec3f(.6f,-.3f,.7f);fill->intensity=.35f;root->addChild(fill);
std::vector<SoTranslation*>moves;std::vector<SoMaterial*>materials;std::vector<SoCube*>cubes;size_t triangles=0;
const SbColor palette[]={SbColor(.12f,.42f,.70f),SbColor(.16f,.66f,.72f),SbColor(.92f,.48f,.16f),SbColor(.76f,.22f,.22f),SbColor(.45f,.25f,.70f),SbColor(.72f,.72f,.77f),SbColor(.22f,.55f,.29f),SbColor(.85f,.68f,.24f)};
auto box=[&](float x,float y,float z,float w,float h,float dep,SoMaterial*mat){auto*sep=new SoSeparator;auto*t=new SoTranslation;t->translation=SbVec3f(x,y,z);sep->addChild(t);sep->addChild(mat);auto*b=new SoCube;b->width=w;b->height=h;b->depth=dep;sep->addChild(b);root->addChild(sep);cubes.push_back(b);triangles+=12;};
if(work=="city-million") {
SoMaterial*colors[8];SoCube* shapes[8];for(int k=0;k<8;++k){colors[k]=new SoMaterial;colors[k]->diffuseColor=palette[k];shapes[k]=new SoCube;shapes[k]->width=.085f;shapes[k]->depth=.085f;shapes[k]->height=.4f+k*.5f;}
for(int z=0;z<1000;++z)for(int x=0;x<1000;++x){unsigned hash=(unsigned(x)*73856093u)^(unsigned(z)*19349663u);int k=hash%8;auto*sep=new SoSeparator;auto*t=new SoTranslation;t->translation=SbVec3f((x-499.5f)*.12f,(.4f+k*.5f)*.5f,(z-499.5f)*.12f);sep->addChild(t);sep->addChild(colors[(x/39+z/57)%8]);sep->addChild(shapes[k]);root->addChild(sep);}
triangles=12000000;std::cout<<"fixture buildings=1000000 triangles=12000000 million_instances=1 shared_shape_nodes=8 lighting=directional palette=8\n";
} else if(work=="city-40000") {
SoMaterial* colors[8];for(int k=0;k<8;++k){colors[k]=new SoMaterial;colors[k]->diffuseColor=palette[k];colors[k]->shininess=.35f;}
for(int z=0;z<200;++z)for(int x=0;x<200;++x){unsigned hash=(unsigned(x)*73856093u)^(unsigned(z)*19349663u);float h=.5f+float(hash%80)*.07f;box((x-99.5f)*.52f,h*.5f,(z-99.5f)*.52f,.36f,h,.36f,colors[(x/9+z/13)%8]);}
std::cout<<"fixture buildings=40000 triangles=480000 million_instances=0 lighting=directional palette=8\n";
} else if(work=="terrain-million") {
auto*coord=new SoCoordinate3;std::vector<SbVec3f>points;points.reserve(1001*501);
for(int z=0;z<=500;++z)for(int x=0;x<=1000;++x){float px=(x-500)*.09f,pz=(z-250)*.14f;float y=5*std::sin(px*.13f)*std::cos(pz*.11f)+2*std::sin((px+pz)*.3f);points.emplace_back(px,y,pz);}
coord->point.setValues(0,int(points.size()),points.data());root->addChild(coord);
for(int band=0;band<20;++band){auto*sep=new SoSeparator;auto*m=new SoMaterial;m->diffuseColor=palette[band%8];sep->addChild(m);std::vector<int32_t>idx;idx.reserve(25000*5);for(int z=0;z<500;++z)for(int x=band*50;x<(band+1)*50;++x){int a=z*1001+x;idx.insert(idx.end(),{a,a+1001,a+1002,a+1,-1});}auto*f=new SoIndexedFaceSet;f->coordIndex.setValues(0,int(idx.size()),idx.data());sep->addChild(f);root->addChild(sep);}
triangles=1000000;std::cout<<"fixture terrain_vertices=501501 quads=500000 triangles=1000000 million_instances=0 lighting=directional elevation=sinusoidal\n";
} else {
for(int z=0;z<4;++z)for(int x=0;x<6;++x){auto*sep=new SoSeparator;auto*t=new SoTranslation;t->translation=SbVec3f((x-2.5f)*3.2f,1.5f,(z-1.5f)*3.7f);sep->addChild(t);auto*m=new SoMaterial;m->diffuseColor=palette[(x+z)%8];m->shininess=.5f;if(z==0)m->transparency=.35f;sep->addChild(m);
if(z==3){auto*tex=new SoTexture2;std::vector<unsigned char>pixels(63*47*3);for(int v=0;v<47;++v)for(int u=0;u<63;++u){bool b=((u/7+v/6)%2)==0;size_t p=(v*63+u)*3;pixels[p]=b?245:25;pixels[p+1]=b?245:40;pixels[p+2]=b?245:95;}tex->image.setValue(SbVec2s(63,47),3,pixels.data());sep->addChild(tex);}
if(x%4==0){auto*a=new SoSphere;a->radius=1.35f;sep->addChild(a);}else if(x%4==1){auto*a=new SoCone;a->height=3;a->bottomRadius=1.25f;sep->addChild(a);}else if(x%4==2){auto*a=new SoCylinder;a->height=3;a->radius=1.2f;sep->addChild(a);}else {auto*a=new SoCube;a->width=2.4f;a->height=3;a->depth=2.4f;sep->addChild(a);}root->addChild(sep);}
std::cout<<"fixture solids=24 translucent=6 textured=6 texture_extent=63x47 lighting=directional\n";
}
auto view=[&](float angle){float radius=work=="solids"?40:180;float y=work=="solids"?26:120;camera->position=SbVec3f(radius*std::sin(angle),y,radius*std::cos(angle));camera->pointAt(SbVec3f(0,0,0));};view(.65f);
CoinRenderAction action(SbViewportRegion(W,H));action.setRenderTarget(target.get());auto render=[&](){auto old=target->getLastSubmissionSerial();action.apply(root);if(action.getLastStatus()!=CoinRenderAction::SUCCESS){std::cerr<<"render failed status="<<action.getLastStatus()<<" diagnostic="<<action.getLastError().getString()<<'\n';return false;}return target->getLastSubmissionSerial()>old;};
const bool externalCapture=api=="opengl" && std::string(caps.adapter_name).find("BGFX")==std::string::npos;auto oracle=[&](const char*stage){if(!externalCapture)target->requestWindowReadbackRGBA();if(!render())return false;if(externalCapture && !render())return false;std::vector<unsigned char>a,b;target->readbackRGBA(a);bool gdi=a.empty();if(gdi && !capture(dpy,win,a))return false;std::unique_ptr<CoinRenderTarget>off(CoinRenderTarget::createOffscreen(SbVec2i32(W,H),options));CoinRenderAction ref(SbViewportRegion(W,H));ref.setRenderTarget(off.get());ref.apply(root);off->readbackRGBA(b);bool ok=ref.getLastStatus()==CoinRenderAction::SUCCESS && a.size()==W*H*4 && a.size()==b.size();int maximum=0;if(ok)for(size_t i=0;i<a.size();++i)if(i%4!=3)maximum=std::max(maximum,std::abs(int(a[i])-int(b[i])));size_t coverage=0;if(a.size()==W*H*4)for(size_t i=0;i<a.size();i+=4)if(a[i]||a[i+1]||a[i+2])++coverage;ok=ok&&maximum==0&&coverage>size_t(W*H/10);std::cout<<"visible_coverage="<<coverage<<" of="<<W*H<<'\n';ppm(prefix+"-"+stage+"-window.ppm",a);ppm(prefix+"-"+stage+"-offscreen.ppm",b);std::cout<<"surface_oracle stage="<<stage<<" rgb_max="<<maximum<<" exact="<<ok<<" xgetimage="<<gdi<<" outside_measurement=1\n";ref.setRenderTarget(nullptr);return ok;};
std::cout<<"scene cube_instances="<<(work=="city-million"?1000000:cubes.size())<<" counted_triangles="<<triangles<<" primitive_triangle_count_known="<<(work!="solids")<<" textured="<<(work=="solids")<<" camera_static="<<(std::getenv("COIN_VISUAL_STATIC_CAMERA")!=nullptr)<<"\n";bool initialOK=oracle("initial");
std::ofstream csv(prefix+".csv");csv<<"frame,warmup,event_ms,update_ms,render_present_ms,total_ms,serial\n";bool ok=true;
for(int i=-warmup;i<frames&&ok;++i){auto start=Clock::now();pump(dpy);auto eventDone=Clock::now();if(!std::getenv("COIN_VISUAL_STATIC_CAMERA"))view(.65f+.35f*float(i+warmup+1)/float(frames+warmup));auto updated=Clock::now();ok=render();auto end=Clock::now();XWindowAttributes attr{};XGetWindowAttributes(dpy,win,&attr);ok=ok&&attr.width==W&&attr.height==H;auto ms=[](auto a,auto b){return std::chrono::duration<double,std::milli>(b-a).count();};csv<<i<<','<<(i<0)<<','<<ms(start,eventDone)<<','<<ms(eventDone,updated)<<','<<ms(updated,end)<<','<<ms(start,end)<<','<<target->getLastSubmissionSerial()<<'\n';}
csv.flush();ok=ok&&bool(csv);if(ok)ok=oracle("final")&&initialOK;std::cout<<"window_campaign result="<<ok<<" frames="<<frames<<" warmup="<<warmup<<" readback_during_measurement=none metric=CPU_render_present_return display_latency=0 gpu_duration=0 event_source=X11 event_count="<<events<<'\n';action.setRenderTarget(nullptr);target.reset();camera=nullptr;root->unref();XDestroyWindow(dpy,win);return ok?0:1;}
