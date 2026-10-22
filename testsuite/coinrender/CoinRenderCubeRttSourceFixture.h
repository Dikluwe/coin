#ifndef COIN_RENDER_CUBE_RTT_SOURCE_FIXTURE_H
#define COIN_RENDER_CUBE_RTT_SOURCE_FIXTURE_H
#include "CoinRenderCubeRttFixture.h"
#include <Inventor/nodes/SoTexture3.h>
#include <Inventor/nodes/SoTextureCubeMap.h>
namespace cube_rtt_source {
using namespace cube_rtt_test;
// Independent encoded texels, transfer function, sample weights and integer mip
// reduction. No renderer/Core sampler is used to calculate expected colors.
inline double decode(double x){x/=255.;return x<=.04045?x/12.92:std::pow((x+.055)/1.055,2.4);}
inline uint8_t byte(double x){return uint8_t(std::max(0.,std::min(255.,std::floor(x+.5))));}
inline RGBA texel(unsigned face,unsigned x,unsigned y,unsigned z,unsigned epoch){return {{double(19+(face*37+x*137+y*71+z*103+epoch*43)%218),double(27+(face*29+x*61+y*149+z*89+epoch*67)%210),double(15+(face*43+x*107+y*47+z*157+epoch*31)%224),double(63+(face*17+x*73+y*41+z*97+epoch*23)%181)}};}
struct Walls {
 SoSeparator*root=new SoSeparator;SoPerspectiveCamera*camera=new SoPerspectiveCamera;SoTextureMatrixTransform*matrix=new SoTextureMatrixTransform;
 std::array<SoTexture2*,6>images{};std::array<SoTexture3*,6>volumes{};std::array<SoTextureCubeMap*,6>cubes{};
 unsigned family,unit,epoch=0;bool srgb,blend;const RGBA material{{.7,.6,.8,.65}};
 Walls(unsigned kind,unsigned selected,bool encoded,bool blending):family(kind),unit(selected),srgb(encoded),blend(blending){
  root->ref();camera->position.setValue(0,0,0);camera->heightAngle=float(1.5707963267948966);camera->nearDistance=.1f;camera->farDistance=20;root->addChild(camera);
  auto*lighting=new SoLightModel;lighting->model=SoLightModel::BASE_COLOR;root->addChild(lighting);
  auto*policy=new SoTransparencyType;policy->value=blend?SoTransparencyType::SORTED_OBJECT_BLEND:SoTransparencyType::NONE;root->addChild(policy);
  auto*quality=new SoComplexity;quality->textureQuality=.3f;root->addChild(quality);
  auto*m=new SoMaterial;m->diffuseColor.setValue(float(material[0]),float(material[1]),float(material[2]));m->transparency=float(1-material[3]);root->addChild(m);
  for(unsigned f=0;f<6;++f){auto*wall=new SoSeparator;root->addChild(wall);auto*select=new SoTextureUnit;select->unit=unit;wall->addChild(select);
   if(family==0){auto*t=new SoTexture2;images[f]=t;t->model=SoTexture2::MODULATE;t->wrapS=SoTexture2::CLAMP;t->wrapT=SoTexture2::CLAMP;wall->addChild(t);}
   else if(family==1){auto*t=new SoTexture3;volumes[f]=t;t->model=SoTexture3::MODULATE;t->wrapS=SoTexture3::CLAMP;t->wrapT=SoTexture3::CLAMP;t->wrapR=SoTexture3::CLAMP;wall->addChild(t);}
   else{auto*t=new SoTextureCubeMap;cubes[f]=t;t->model=SoTextureCubeMap::MODULATE;wall->addChild(t);}
   wall->addChild(matrix);
   for(unsigned q=0;q<4;++q){auto*piece=new SoSeparator;wall->addChild(piece);auto*uv=new SoTextureCoordinate3;const double s=q&1?.5:.25,t=q&2?.5:.25;const SbVec3f coordinate=family==2?ray(f,s,t):SbVec3f(float(s),float(t),.5f);for(unsigned v=0;v<4;++v)uv->point.set1Value(v,coordinate);piece->addChild(uv);auto*restore=new SoTextureUnit;restore->unit=0;piece->addChild(restore);double x=(q&1)*.5,y=(q>>1)*.5;const SbVec3f p[]={ray(f,x,y)*4,ray(f,x+.5,y)*4,ray(f,x+.5,y+.5)*4,ray(f,x,y+.5)*4};quad(piece,p);}
  }paint(0);
 }
 ~Walls(){root->unref();}
 void paint(unsigned e){epoch=e;for(unsigned f=0;f<6;++f){std::vector<uint8_t>data;for(unsigned z=0;z<(family==1?2u:1u);++z)for(unsigned y=0;y<2;++y)for(unsigned x=0;x<2;++x)for(double c:texel(f,x,y,z,e))data.push_back(byte(c));if(family==0)images[f]->image.setValue(SbVec2s(2,2),4,data.data());else if(family==1)volumes[f]->images.setValue(SbVec3s(2,2,2),4,data.data());else{SoSFImage*faces[]={&cubes[f]->imagePosX,&cubes[f]->imageNegX,&cubes[f]->imagePosY,&cubes[f]->imageNegY,&cubes[f]->imagePosZ,&cubes[f]->imageNegZ};for(unsigned sf=0;sf<6;++sf){data.clear();for(unsigned y=0;y<2;++y)for(unsigned x=0;x<2;++x)for(double c:texel(sf,x,y,0,e))data.push_back(byte(c));faces[sf]->setValue(SbVec2s(2,2),4,data.data());}}}}
 RGBA color(unsigned f,unsigned q)const{
  const unsigned nx=q&1?2:1,ny=q&2?2:1,nz=family==1?2:1;RGBA out{{0,0,0,0}};
  for(unsigned z=0;z<nz;++z)for(unsigned y=0;y<ny;++y)for(unsigned x=0;x<nx;++x){const auto p=texel(f,x,y,z,epoch);for(unsigned c=0;c<4;++c)out[c]+=(c<3&&srgb?decode(p[c]):p[c]/255.)/(nx*ny*nz);}
  for(unsigned c=0;c<4;++c)out[c]*=material[c];
  if(blend){const double alpha=out[3];const double clear[]={.02,.03,.04};for(unsigned c=0;c<3;++c)out[c]=out[c]*alpha+clear[c]*(1-alpha);out[3]=alpha*alpha+1-alpha;}
  for(auto&c:out)c=byte(c*255);return out;
 }
};
struct Oracle {
 unsigned n;std::vector<std::array<std::vector<RGBA>,6>>levels;
 Oracle(const Walls&w,unsigned extent):n(extent){levels.resize(1);for(unsigned f=0;f<6;++f){auto&face=levels[0][f];face.resize(size_t(n)*n);for(unsigned y=0;y<n;++y)for(unsigned x=0;x<n;++x)face[size_t(y)*n+x]=w.color(f,(x>=n/2?1:0)+(y>=n/2?2:0));}
  for(unsigned size=n;size>1;size/=2){const unsigned next=size/2;std::array<std::vector<RGBA>,6>level;for(unsigned f=0;f<6;++f){level[f].resize(size_t(next)*next);for(unsigned y=0;y<next;++y)for(unsigned x=0;x<next;++x){RGBA sum{{0,0,0,0}};for(unsigned dy=0;dy<2;++dy)for(unsigned dx=0;dx<2;++dx)for(unsigned c=0;c<4;++c)sum[c]+=levels.back()[f][size_t(y*2+dy)*size+x*2+dx][c];for(auto&v:sum)v=std::floor((v+2)/4);level[f][size_t(y)*next+x]=sum;}}levels.push_back(std::move(level));}
 }
 RGBA sample(const SbVec3f&direction,double lod,unsigned filter)const{
  unsigned face;double s,t;if(!project(direction,face,s,t))return {{0,0,0,0}};
  auto at=[&](unsigned level,bool linear){const unsigned extent=n>>level;RGBA result{{0,0,0,0}};const double x=s*extent-(linear?.5:0),y=t*extent-(linear?.5:0);int ix=int(std::floor(x)),iy=int(std::floor(y));const double fx=linear?x-ix:0,fy=linear?y-iy:0;for(unsigned dy=0;dy<2;++dy)for(unsigned dx=0;dx<2;++dx){const unsigned a=unsigned(std::max(0,std::min(int(extent)-1,ix+int(dx)))),b=unsigned(std::max(0,std::min(int(extent)-1,iy+int(dy))));const double weight=(dx?fx:1-fx)*(dy?fy:1-fy);for(unsigned c=0;c<4;++c)result[c]+=weight*levels[level][face][size_t(b)*extent+a][c];}return result;};
  if(filter<2)return at(0,filter==1);lod=std::max(0.,std::min(double(levels.size()-1),lod));unsigned a=unsigned(std::floor(lod)),b=std::min(a+1,unsigned(levels.size()-1));bool linear=filter==3||lod<=0;RGBA result=at(a,linear),other=at(b,linear);for(unsigned c=0;c<4;++c)result[c]=result[c]*(1-(lod-a))+other[c]*(lod-a);return result;
 }
};
inline bool panels(const Frame&frame,const Scene&scene,const Oracle&oracle,unsigned extent,unsigned filter=0,double lod=0,bool variable=false,bool window=false){
 if(!check(frame.rgba.size()==size_t(extent)*extent*4,"source crossing root readback extent"))return false;unsigned bad=0;int maximum=0;
 for(unsigned cell=0;cell<54;++cell){const double cx=-3.2+.8*(cell%9),cy=2.-.8*(cell/9);const int px=int(std::lround(extent*.5+cx*extent/8-.5)),py=int(std::lround(extent*.5-cy*extent/8-.5));SbVec3f d=scene.coordinates[cell]->point[0];if(variable){const double wx=(px+.5)*8/extent-4,wy=4-(py+.5)*8/extent;const double slope=double(extent)/8*std::pow(2.,lod)/oracle.n;d=ray(cell/9,.5+(wx-cx)*slope,.5+(wy-cy)*slope);}auto expected=oracle.sample(d,lod,filter);if(window)expected[3]=255;const size_t p=(size_t(py)*extent+unsigned(px))*4;for(unsigned c=0;c<4;++c){int delta=std::abs(int(frame.rgba[p+c])-int(std::lround(expected[c])));maximum=std::max(maximum,delta);bad+=delta>3;if(delta>3&&bad<=6)std::cerr<<"source_cross cell="<<cell<<" face="<<cell/9<<" channel="<<c<<" expected="<<expected[c]<<" actual="<<unsigned(frame.rgba[p+c])<<'\n';}}
 std::cout<<"cube_source_oracle bad="<<bad<<" maximum="<<maximum<<" filter="<<filter<<" lod="<<lod<<'\n';return check(bad==0,"independent decoded-source/filter/material-alpha/linear-output oracle across six faces");
}
inline void lodCoordinates(Scene&scene,unsigned extent,unsigned n,double lod){const double slope=double(extent)/8*std::pow(2.,lod)/n;for(unsigned cell=0;cell<54;++cell){const unsigned f=cell/9;const SbVec3f uv[]={ray(f,.5-.32*slope,.5-.32*slope),ray(f,.5+.32*slope,.5-.32*slope),ray(f,.5+.32*slope,.5+.32*slope),ray(f,.5-.32*slope,.5+.32*slope)};scene.coordinates[cell]->point.setValues(0,4,uv);}}
inline bool atlas(const CoinRenderFramePlan&frame,const Oracle&oracle,bool mips,bool direct){unsigned found=0,bad=0;bool ok=true;for(const auto&image:frame.textures)if(image.cubeFaceSize){++found;uint64_t texels=0;for(unsigned size=oracle.n;;size/=2){texels+=6*uint64_t(size)*size;if(!mips||size==1)break;}const uint64_t width=2*oracle.n,height=(texels+width-1)/width;
 ok &=check(image.format==CoinRenderTextureFormat::RGBA8_LINEAR&&!image.producerId&&image.cubeFaceSize==oracle.n&&image.cubeSourceComponents==4&&image.cubeMipmapped==mips&&!image.mipmapped&&image.width==width&&image.height==height,"RTT is resolved RGBA8_LINEAR with complete face/mip dimensions under stored sRGB policy");
 if(direct){ok &=check(image.gpuToken&&!image.pixelsRgba.size()&&image.mipmapsRgba.empty(),"DIRECT source crossing retains GPU-only resolved cube");continue;}
 ok &=check(!image.gpuToken&&image.pixelsRgba.size()==width*height*4,"STAGED source crossing exposes complete internal atlas to root submission observer");size_t offset=0;for(unsigned l=0;l<(mips?oracle.levels.size():1);++l)for(unsigned f=0;f<6;++f)for(const auto&pixel:oracle.levels[l][f])for(unsigned c=0;c<4;++c){if(offset>=image.pixelsRgba.size()){++bad;continue;}bad+=std::abs(int(image.pixelsRgba[offset++])-int(pixel[c]))>3;}for(;offset<image.pixelsRgba.size();++offset)bad+=image.pixelsRgba[offset]!=0;
 std::vector<CoinRenderCubeTextureCore::Subresource>descriptions;ok &=check(CoinRenderCubeTextureCore::subresources(oracle.n,mips,descriptions),"resolved cube subresources describable");size_t index=0;uint64_t bytes=0;for(unsigned l=0;l<(mips?oracle.levels.size():1);++l){unsigned size=oracle.n>>l;for(unsigned f=0;f<6;++f){if(index>=descriptions.size()){ok=false;continue;}const auto&d=descriptions[index++];ok &=check(d.face==f&&d.level==l&&d.width==size&&d.height==size&&d.rowPitch==size*4&&d.linearByteOffset==bytes&&d.originBottomLeft,"subresource face/level/offset/pitch/origin agrees with actual resolved STAGED atlas");bytes+=uint64_t(size)*size*4;}}ok &=check(index==descriptions.size(),"all subresources accounted exactly once");}
 CoinWgpuFfiFrame ffi;std::string diagnostic;if(!check(ffi.prepare(frame,side,side,diagnostic,false),"resolved source crossing packs current private ingress: "+diagnostic))return false;const auto&view=ffi.getView();unsigned packets=0;for(uint64_t i=0;i<view.texture_count;++i)if(view.textures[i].format==12){++packets;const auto&t=view.textures[i];ok &=check(!(t.reserved&512u)&&bool(t.reserved&256u)==direct,"Current ABI resolved RTT packet keeps stored-source sRGB bit clear and exact GPU token flag");if(direct)ok &=check(t.content_digest&&t.pixels==nullptr&&t.pixel_bytes_len==0,"Current ABI DIRECT token lives in digest and never substitutes CPU bytes");}ok &=check(view.abi_version==COIN_WGPU_ABI_VERSION&&packets==1,"source crossings retain the current private ABI without broadening public export");
 std::cout<<"cube_source_atlas count="<<found<<" bad="<<bad<<'\n';return check(found==1&&bad==0,"source patterns produce face-local linear rendered mips without second gamma decode")&&ok;
}
}
#endif
