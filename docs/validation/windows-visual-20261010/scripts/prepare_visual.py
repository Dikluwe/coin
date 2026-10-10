import pathlib
r=pathlib.Path(__file__).parent
d=r/'visual-src';d.mkdir(exist_ok=True)
s=(r/'windows-fronts-src/main.cpp').read_text()
s=s.replace('#include <Inventor/nodes/SoCube.h>', '''#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoDirectionalLight.h>
#include <Inventor/nodes/SoSphere.h>
#include <Inventor/nodes/SoCone.h>
#include <Inventor/nodes/SoCylinder.h>
#include <Inventor/nodes/SoTexture2.h>''')
s=s.replace('constexpr int W=640,H=480;', 'constexpr int W=1280,H=960;')
s=s.replace('if(work!="static"&&work!="camera-events"&&work!="transforms"&&work!="materials"&&work!="geometry"&&work!="million")return 2;', 'if(work!="city-40000"&&work!="terrain-million"&&work!="solids")return 2;')
s=s.replace('extent=640x480','extent=1280x960')
start=s.index('auto*root=new SoSeparator;')
end=s.index('CoinRenderAction action(',start)
s=s[:start]+'''auto*root=new SoSeparator;root->ref();camera=new SoOrthographicCamera;camera->height=work=="city-40000"?140:work=="terrain-million"?90:30;camera->nearDistance=.1f;camera->farDistance=600;root->addChild(camera);
auto*light=new SoLightModel;light->model=SoLightModel::PHONG;root->addChild(light);
auto*sun=new SoDirectionalLight;sun->direction=SbVec3f(-.5f,-1,-.3f);sun->intensity=.85f;root->addChild(sun);
auto*fill=new SoDirectionalLight;fill->direction=SbVec3f(.6f,-.3f,.7f);fill->intensity=.35f;root->addChild(fill);
std::vector<SoTranslation*>moves;std::vector<SoMaterial*>materials;std::vector<SoCube*>cubes;size_t triangles=0;
const SbColor palette[]={SbColor(.12f,.42f,.70f),SbColor(.16f,.66f,.72f),SbColor(.92f,.48f,.16f),SbColor(.76f,.22f,.22f),SbColor(.45f,.25f,.70f),SbColor(.72f,.72f,.77f),SbColor(.22f,.55f,.29f),SbColor(.85f,.68f,.24f)};
auto box=[&](float x,float y,float z,float w,float h,float dep,SoMaterial*mat){auto*sep=new SoSeparator;auto*t=new SoTranslation;t->translation=SbVec3f(x,y,z);sep->addChild(t);sep->addChild(mat);auto*b=new SoCube;b->width=w;b->height=h;b->depth=dep;sep->addChild(b);root->addChild(sep);cubes.push_back(b);triangles+=12;};
if(work=="city-40000") {
SoMaterial* colors[8];for(int k=0;k<8;++k){colors[k]=new SoMaterial;colors[k]->diffuseColor=palette[k];colors[k]->shininess=.35f;}
for(int z=0;z<200;++z)for(int x=0;x<200;++x){unsigned hash=(unsigned(x)*73856093u)^(unsigned(z)*19349663u);float h=.5f+float(hash%80)*.07f;box((x-99.5f)*.52f,h*.5f,(z-99.5f)*.52f,.36f,h,.36f,colors[(x/9+z/13)%8]);}
std::cout<<"fixture buildings=40000 triangles=480000 million_instances=0 lighting=directional palette=8\\n";
} else if(work=="terrain-million") {
auto*coord=new SoCoordinate3;std::vector<SbVec3f>points;points.reserve(1001*501);
for(int z=0;z<=500;++z)for(int x=0;x<=1000;++x){float px=(x-500)*.09f,pz=(z-250)*.14f;float y=5*std::sin(px*.13f)*std::cos(pz*.11f)+2*std::sin((px+pz)*.3f);points.emplace_back(px,y,pz);}
coord->point.setValues(0,int(points.size()),points.data());root->addChild(coord);
for(int band=0;band<20;++band){auto*sep=new SoSeparator;auto*m=new SoMaterial;m->diffuseColor=palette[band%8];sep->addChild(m);std::vector<int32_t>idx;idx.reserve(25000*5);for(int z=0;z<500;++z)for(int x=band*50;x<(band+1)*50;++x){int a=z*1001+x;idx.insert(idx.end(),{a,a+1001,a+1002,a+1,-1});}auto*f=new SoIndexedFaceSet;f->coordIndex.setValues(0,int(idx.size()),idx.data());sep->addChild(f);root->addChild(sep);}
triangles=1000000;std::cout<<"fixture terrain_vertices=501501 quads=500000 triangles=1000000 million_instances=0 lighting=directional elevation=sinusoidal\\n";
} else {
for(int z=0;z<4;++z)for(int x=0;x<6;++x){auto*sep=new SoSeparator;auto*t=new SoTranslation;t->translation=SbVec3f((x-2.5f)*3.2f,1.5f,(z-1.5f)*3.7f);sep->addChild(t);auto*m=new SoMaterial;m->diffuseColor=palette[(x+z)%8];m->shininess=.5f;if(z==0)m->transparency=.35f;sep->addChild(m);
if(z==3){auto*tex=new SoTexture2;std::vector<unsigned char>pixels(63*47*3);for(int v=0;v<47;++v)for(int u=0;u<63;++u){bool b=((u/7+v/6)%2)==0;size_t p=(v*63+u)*3;pixels[p]=b?245:25;pixels[p+1]=b?245:40;pixels[p+2]=b?245:95;}tex->image.setValue(SbVec2s(63,47),3,pixels.data());sep->addChild(tex);}
if(x%4==0){auto*a=new SoSphere;a->radius=1.35f;sep->addChild(a);}else if(x%4==1){auto*a=new SoCone;a->height=3;a->bottomRadius=1.25f;sep->addChild(a);}else if(x%4==2){auto*a=new SoCylinder;a->height=3;a->radius=1.2f;sep->addChild(a);}else {auto*a=new SoCube;a->width=2.4f;a->height=3;a->depth=2.4f;sep->addChild(a);}root->addChild(sep);}
std::cout<<"fixture solids=24 translucent=6 textured=6 texture_extent=63x47 lighting=directional\\n";
}
auto view=[&](float angle){float radius=work=="solids"?40:180;float y=work=="solids"?26:120;camera->position=SbVec3f(radius*std::sin(angle),y,radius*std::cos(angle));camera->pointAt(SbVec3f(0,0,0));};view(.65f);
''' + s[end:]
# Each submitted measured frame moves the camera along an arc; final image differs from initial.
st=s.index('for(int i=-warmup;')
en=s.index('csv.flush();',st)
s=s[:st]+'''for(int i=-warmup;i<frames&&ok;++i){auto start=Clock::now();pump();auto eventDone=Clock::now();view(.65f+.35f*float(i+warmup+1)/float(frames+warmup));auto updated=Clock::now();ok=render();auto end=Clock::now();RECT rect{};GetClientRect(hwnd,&rect);ok=ok&&rect.right==W&&rect.bottom==H;auto ms=[](auto a,auto b){return std::chrono::duration<double,std::milli>(b-a).count();};csv<<i<<','<<(i<0)<<','<<ms(start,eventDone)<<','<<ms(eventDone,updated)<<','<<ms(updated,end)<<','<<ms(start,end)<<','<<target->getLastSubmissionSerial()<<'\\n';}
''' + s[en:]
(d/'main.cpp').write_text(s)
(d/'CMakeLists.txt').write_text('''cmake_minimum_required(VERSION 3.20)
project(coin_visual_campaign LANGUAGES CXX)
find_package(Coin REQUIRED)
find_package(CoinRender REQUIRED)
add_executable(coin_visual_campaign main.cpp)
target_compile_features(coin_visual_campaign PRIVATE cxx_std_17)
target_compile_definitions(coin_visual_campaign PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
target_compile_options(coin_visual_campaign PRIVATE /EHsc)
target_link_libraries(coin_visual_campaign PRIVATE CoinRender::CoinRender Coin::Coin user32 gdi32)
''')
b=(r/'build_windows_fronts.py').read_text()
b=b[b.index('e={'):].replace("windows-fronts-src","visual-src").replace('windows-fronts-','visual-')
(r/'build_visual.py').write_text('import pathlib,subprocess,os,json\nr=pathlib.Path(__file__).parent\n'+b)
runner=(r/'run_window_campaign.py').read_text().replace("['static','camera-events','transforms','materials','geometry','million']","['city-40000','terrain-million','solids']").replace("ordered=['native','portable','portable','native'] if frames>1 else ['native','portable']","ordered=['native','portable']").replace("windows-fronts-","visual-").replace('coin_win32_campaign.exe','coin_visual_campaign.exe')
(r/'run_visual.py').write_text(runner)
