#ifndef COIN_RENDER_CUBE_RTT_PUBLIC_FIXTURE_H
#define COIN_RENDER_CUBE_RTT_PUBLIC_FIXTURE_H
#include <Inventor/SoDB.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <Inventor/rendering/CoinRenderCapabilities.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoSwitch.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoPerspectiveCamera.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoComplexity.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoTextureCoordinate3.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoSceneTextureCubeMap.h>
#include <Inventor/nodes/SoTextureUnit.h>
#include <Inventor/nodes/SoTextureMatrixTransform.h>
#include <array>
#include <vector>
#include <string>
#include <memory>
#include <iostream>
#include <cmath>
#include <cstdlib>
namespace cube_rtt_test {
inline bool unsignedOption(const char*text,unsigned&value){if(!text||!*text)return false;uint64_t parsed=0;for(const char*p=text;*p;++p){if(*p<'0'||*p>'9')return false;parsed=parsed*10+unsigned(*p-'0');if(parsed>UINT32_MAX)return false;}value=unsigned(parsed);return true;}
constexpr unsigned side=192;
using RGBA=std::array<double,4>;
inline bool check(bool v,const std::string&s){if(!v)std::cerr<<"cube_rtt FAIL "<<s<<'\n';return v;}
inline SbVec3f ray(unsigned face,double s,double t){const float a=float(2*s-1),b=float(2*t-1);switch(face){case 0:return SbVec3f(1,-b,-a);case 1:return SbVec3f(-1,-b,a);case 2:return SbVec3f(a,1,b);case 3:return SbVec3f(a,-1,-b);case 4:return SbVec3f(a,-b,1);default:return SbVec3f(-a,-b,-1);}}
inline RGBA tileColor(unsigned face,unsigned x,unsigned y,unsigned epoch=0){return {{double(30+(face*31+x*53+y*17+epoch*29)%200),double(35+(face*19+x*11+y*61+epoch*47)%195),double(25+(face*43+x*29+y*7+epoch*37)%205),255}};}
inline SoIndexedFaceSet*quad(SoSeparator*root,const SbVec3f*p,SoCoordinate3**out=nullptr){auto*positions=new SoCoordinate3;if(out)*out=positions;positions->point.setValues(0,4,p);root->addChild(positions);auto*mesh=new SoIndexedFaceSet;const int32_t ix[]={0,1,2,-1,0,2,3,-1};mesh->coordIndex.setValues(0,8,ix);mesh->textureCoordIndex.setValues(0,8,ix);root->addChild(mesh);return mesh;}
struct Producer{
 SoSeparator*root=new SoSeparator;std::array<SoMaterial*,54>materials{};unsigned epoch=0;
 Producer(){root->ref();auto*lm=new SoLightModel;lm->model=SoLightModel::BASE_COLOR;root->addChild(lm);for(unsigned face=0;face<6;++face)for(unsigned y=0;y<3;++y)for(unsigned x=0;x<3;++x){auto*piece=new SoSeparator;auto*m=new SoMaterial;materials[(face*3+y)*3+x]=m;piece->addChild(m);SbVec3f p[]={ray(face,x/3.,y/3.)*4,ray(face,(x+1)/3.,y/3.)*4,ray(face,(x+1)/3.,(y+1)/3.)*4,ray(face,x/3.,(y+1)/3.)*4};quad(piece,p);root->addChild(piece);}paint(0);}
 ~Producer(){root->unref();}Producer(const Producer&)=delete;
 void paint(unsigned e){epoch=e;for(unsigned f=0;f<6;++f)for(unsigned y=0;y<3;++y)for(unsigned x=0;x<3;++x){const auto c=tileColor(f,x,y,e);materials[(f*3+y)*3+x]->diffuseColor.setValue(float(c[0]/255),float(c[1]/255),float(c[2]/255));}}
};
struct Scene{
 Producer producer;SoSeparator*root=new SoSeparator;SoSceneTextureCubeMap*cube=new SoSceneTextureCubeMap;SoSwitch*enabled=new SoSwitch;SoComplexity*quality=new SoComplexity;SoMaterial*material=new SoMaterial;SoTextureMatrixTransform*matrix=new SoTextureMatrixTransform;unsigned unit=0;std::array<SoTextureCoordinate3*,54>coordinates{};std::array<SoCoordinate3*,54>vertices{};std::array<SoSeparator*,54>panels{};
 explicit Scene(unsigned selectedUnit=0):unit(selectedUnit){root->ref();auto*camera=new SoOrthographicCamera;camera->position.setValue(0,0,10);camera->height=8;camera->nearDistance=1;camera->farDistance=30;root->addChild(camera);auto*lm=new SoLightModel;lm->model=SoLightModel::BASE_COLOR;root->addChild(lm);material->diffuseColor.setValue(1,1,1);root->addChild(material);quality->textureQuality=.1f;root->addChild(quality);cube->size.setValue(32,32);cube->scene=producer.root;cube->model=SoSceneTextureCubeMap::REPLACE;cube->transparencyFunction=SoSceneTextureCubeMap::NONE;cube->backgroundColor.setValue(.02f,.03f,.04f);enabled->whichChild=SO_SWITCH_NONE;auto*select=new SoTextureUnit;select->unit=unit;enabled->addChild(select);enabled->addChild(cube);enabled->addChild(matrix);root->addChild(enabled);auto*restore=new SoTextureUnit;restore->unit=0;root->addChild(restore);for(unsigned cell=0;cell<54;++cell){unsigned face=cell/9,x=cell%3,y=cell/3%3,col=cell%9,row=cell/9;float cx=-3.2f+.8f*col,cy=2.f-.8f*row;auto*panel=new SoSeparator;panels[cell]=panel;auto*selectPanel=new SoTextureUnit;selectPanel->unit=unit;panel->addChild(selectPanel);auto*uv=new SoTextureCoordinate3;coordinates[cell]=uv;const auto direction=ray(face,(x+.5)/3,(y+.5)/3);for(int i=0;i<4;++i)uv->point.set1Value(i,direction);panel->addChild(uv);auto*restorePanel=new SoTextureUnit;restorePanel->unit=0;panel->addChild(restorePanel);const SbVec3f p[]={SbVec3f(cx-.32f,cy-.32f,0),SbVec3f(cx+.32f,cy-.32f,0),SbVec3f(cx+.32f,cy+.32f,0),SbVec3f(cx-.32f,cy+.32f,0)};quad(panel,p,&vertices[cell]);root->addChild(panel);}}
 ~Scene(){root->unref();}Scene(const Scene&)=delete;
};
struct Frame{std::vector<uint8_t>rgba;std::vector<float>depth;uint64_t serial=0;};
inline bool centers(const Frame&f,const Scene&s,bool textured,const std::string&label,unsigned width=side,unsigned height=side){if(!check(f.rgba.size()==size_t(width)*height*4,"complete public cube consumer buffer"))return false;unsigned bad=0,count=0;int maxError=0;for(unsigned cell=0;cell<54;++cell){unsigned face=cell/9,x=cell%3,y=cell/3%3,col=cell%9,row=cell/9;const double cx=-3.2+.8*col,cy=2.-.8*row;const int px=int(std::lround(width*.5+cx*height/8-.5)),py=int(std::lround(height*.5-cy*height/8-.5));RGBA e=textured?tileColor(face,x,y,s.producer.epoch):RGBA{{255,255,255,255}};for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){size_t p=(size_t(py+dy)*width+unsigned(px+dx))*4;for(unsigned c=0;c<4;++c){int delta=std::abs(int(f.rgba[p+c])-int(std::lround(e[c])));maxError=std::max(maxError,delta);bad+=delta>3;}++count;}}
 std::cout<<"cube_rtt_oracle phase="<<label<<" face_count=6 probes=54 tested="<<count<<" wrong_channels="<<bad<<" max_delta="<<maxError<<'\n';return check(count==486&&bad==0,label+" six independently authored asymmetric walls/canonical cube rays");}
inline bool exact(const Frame&a,const Frame&b){return check(a.rgba==b.rgba&&a.depth==b.depth,"local exact cube RTT A/B/A RGBA/depth");}
}
#endif
