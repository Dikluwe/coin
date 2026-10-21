#define main coin_texture3_legacy_main
#include "CoinRenderTexture3ReferenceTest.cpp"
#undef main
#include "CoinRenderVolumeOracle.h"
#include <limits>
#include <thread>
#include <chrono>
#include <Inventor/nodes/SoImage.h>
#include <Inventor/nodes/SoTextureCoordinateObject.h>
#include <Inventor/nodes/SoIndexedLineSet.h>
#include <Inventor/nodes/SoPointSet.h>
namespace {
bool npotRun(CoinRenderTarget* target,const char* renderer) {
  Fixture scene;scene.volume(2);while(scene.root->getNumChildren()>5)scene.root->removeChild(5);
  auto* positions=new SoCoordinate3;const SbVec3f corners[]={SbVec3f(-4,-4,0),SbVec3f(4,-4,0),SbVec3f(4,4,0),SbVec3f(-4,4,0)};
  positions->point.setValues(0,4,corners);scene.root->addChild(positions);
  auto* coords=new SoTextureCoordinate3;scene.root->addChild(coords);
  auto* face=new SoIndexedFaceSet;const int32_t indices[]={0,1,2,3,-1};face->coordIndex.setValues(0,5,indices);scene.root->addChild(face);
  CoinRenderAction action(SbViewportRegion(side,side));action.setRenderTarget(target);target->setDepthReadbackEnabled(TRUE);bool ok=true;
  std::vector<unsigned char> pixels;std::vector<float> depth;
  std::string phase="geometry";
  auto render=[&](){auto serial=target->getLastSubmissionSerial();action.apply(scene.root);target->readbackRGBA(pixels);const bool pass=action.getLastStatus()==CoinRenderAction::SUCCESS&&target->getLastSubmissionSerial()>serial&&pixels.size()==size_t(side*side*4);if(!pass)std::cerr<<"npot render "<<renderer<<" phase="<<phase<<" status="<<action.getLastStatus()<<" serial="<<serial<<"->"<<target->getLastSubmissionSerial()<<" bytes="<<pixels.size()<<" error="<<action.getLastError().getString()<<'\n';return pass;};
  auto color=[&](double expected[3],const char* gate){bool pass=pixels.size()==size_t(side*side*4);size_t at=size_t((side-1-64)*side+64)*4;if(pass)for(unsigned c=0;c<3;++c)pass &=std::abs(double(pixels[at+c])-expected[c])<=5;if(!pass)std::cerr<<"npot "<<renderer<<' '<<gate<<" expected="<<expected[0]<<','<<expected[1]<<','<<expected[2]<<" phase="<<phase<<" actual="<<(pixels.size()>at+2?int(pixels[at]):-1)<<','<<(pixels.size()>at+2?int(pixels[at+1]):-1)<<','<<(pixels.size()>at+2?int(pixels[at+2]):-1)<<" status="<<action.getLastStatus()<<" error="<<action.getLastError().getString()<<'\n';return pass;};
  for(auto dims:{std::array<unsigned,3>{3,5,7},{7,3,5},{5,7,3},{255,1,1},{1,255,1},{1,1,255},{256,1,1},{1,1,1}}){
    auto source=volume_oracle::authored(dims[0],dims[1],dims[2]);for(size_t a=3;a<source.size();a+=4)source[a]=255;
    auto levels=volume_oracle::chain(dims[0],dims[1],dims[2],source);
    for(int channels=1;channels<=4;++channels){std::vector<uint8_t> raw(size_t(dims[0])*dims[1]*dims[2]*channels);auto expanded=source;for(size_t v=0;v<source.size()/4;++v){for(int c=0;c<channels;++c)raw[v*channels+c]=channels==2&&c==1?255:source[v*4+c];if(channels<=2)expanded[v*4+1]=expanded[v*4+2]=expanded[v*4];}
      scene.texture->images.setValue(SbVec3s(dims[0],dims[1],dims[2]),channels,raw.data());scene.complexity->textureQuality=.1f;
      for(unsigned voxel:{0u,dims[0]*dims[1]*dims[2]/2,dims[0]*dims[1]*dims[2]-1}){const SbVec3f sample((voxel%dims[0]+.5f)/dims[0],((voxel/dims[0])%dims[1]+.5f)/dims[1],(voxel/(dims[0]*dims[1])+.5f)/dims[2]);const SbVec3f values[]={sample,sample,sample,sample};coords->point.setValues(0,4,values);double expected[]={double(expanded[voxel*4]),double(expanded[voxel*4+1]),double(expanded[voxel*4+2])};ok &=check(render()&&color(expected,"authored-center"),"NPOT original dimensions and all edge voxels/channels");}
    }
    scene.texture->images.setValue(SbVec3s(dims[0],dims[1],dims[2]),4,source.data());
    // Analytic derivative at pixel (64,64), with the other two coordinates constant.
    for(int q=0;q<4;++q)for(unsigned axis=0;axis<3;++axis)for(double lod:{-.5,1.0,1.5,2.0}){
      if(dims[axis]==1)continue;scene.complexity->textureQuality=std::array<float,4>{{.1f,.3f,.6f,.8f}}[q];double rho=std::pow(2.,lod);double gradient=rho/dims[axis];const double center=.425;
      SbVec3f values[4];for(int i=0;i<4;++i){values[i]=SbVec3f(center,center,center);values[i][axis]=float(center+((i==1||i==2)?63.5:-64.5)*gradient);}coords->point.setValues(0,4,values);
      double expected[3];for(unsigned c=0;c<3;++c)expected[c]=volume_oracle::filtered(levels,center,center,center,c,q,lod);
      ok &= check(render()&&color(expected,"analytic-filter-lod"),"NPOT area mips and S/T/R dominated integer/fractional LOD");
    }
    for(bool repeat:{false,true}){phase="wrap dims="+std::to_string(dims[0])+"x"+std::to_string(dims[1])+"x"+std::to_string(dims[2])+" repeat="+std::to_string(repeat);scene.texture->wrapS=repeat?SoTexture3::REPEAT:SoTexture3::CLAMP;scene.texture->wrapT=scene.texture->wrapS.getValue();scene.texture->wrapR=scene.texture->wrapS.getValue();scene.complexity->textureQuality=.3f;const SbVec3f sample(-.17f,1.21f,1.37f),values[]={sample,sample,sample,sample};coords->point.setValues(0,4,values);double expected[3];for(unsigned c=0;c<3;++c)expected[c]=volume_oracle::sample(levels[0],sample[0],sample[1],sample[2],c,true,repeat);ok &=check(render()&&color(expected,"wrap"),"NPOT independent XYZ wrap");}
    scene.texture->wrapS=SoTexture3::CLAMP;scene.texture->wrapT=SoTexture3::CLAMP;scene.texture->wrapR=SoTexture3::CLAMP;
    const SbVec3f sample(.425f,.425f,.425f),values[]={sample,sample,sample,sample};coords->point.setValues(0,4,values);scene.complexity->textureQuality=.3f;ok &=check(render(),"NPOT stable frame");const auto stable=pixels;target->readbackDepth(depth);const auto stableDepth=depth;const auto serial=target->getLastSubmissionSerial();
    for(auto invalid:{SbVec3s(257,1,1),SbVec3s(1,257,1),SbVec3s(1,1,257)}){std::vector<unsigned char> bad(257*4,255);scene.texture->images.setValue(invalid,4,bad.data());action.apply(scene.root);target->readbackRGBA(pixels);target->readbackDepth(depth);ok &=check(action.getLastStatus()!=CoinRenderAction::SUCCESS&&target->getLastSubmissionSerial()==serial&&pixels==stable&&depth==stableDepth,"NPOT rejected extent preserves color depth serial");}
    scene.texture->images.setValue(SbVec3s(dims[0],dims[1],dims[2]),4,source.data());ok &=check(render()&&pixels==stable,"NPOT rejection recovers identical publication");
    scene.complexity->textureQuality=0;ok &=check(render(),"NPOT quality off");scene.complexity->textureQuality=.3f;ok &=check(render()&&pixels==stable,"NPOT quality on restores exact frame");
    auto changed=source;for(size_t a=0;a<changed.size();a+=4)changed[a]=255-changed[a];scene.texture->images.setValue(SbVec3s(dims[0],dims[1],dims[2]),4,changed.data());ok &=check(render()&&pixels!=stable,"NPOT image revision invalidates cache");scene.texture->images.setValue(SbVec3s(dims[0],dims[1],dims[2]),4,source.data());ok &=check(render()&&pixels==stable,"NPOT image revision recovery");
  }
  // Projective transport, two units and alpha use the same authored odd volume.
  auto source=volume_oracle::authored(3,5,7);for(size_t a=3;a<source.size();a+=4)source[a]=255;
  scene.texture->images.setValue(SbVec3s(3,5,7),4,source.data());scene.complexity->textureQuality=.1f;
  const SbVec3f selected(.5f/3,.5f/5,6.5f/7),samples[]={selected,selected,selected,selected};coords->point.setValues(0,4,samples);
  double expected[3];for(unsigned c=0;c<3;++c)expected[c]=source[size_t(6*5*3)*4+c];ok &=check(render()&&color(expected,"odd-last-slice"),"NPOT last slice original voxel");
  for(int model:{SoTexture3::MODULATE,SoTexture3::DECAL,SoTexture3::BLEND}){scene.texture->model=model;scene.texture->blendColor.setValue(0,0,0);double modelExpected[3];for(unsigned c=0;c<3;++c)modelExpected[c]=model==SoTexture3::BLEND?255-expected[c]:expected[c];ok &=check(render()&&color(modelExpected,"models"),"NPOT existing MODULATE DECAL BLEND models");}scene.texture->model=SoTexture3::MODULATE;
  auto* transform=new SoTextureMatrixTransform;scene.root->insertChild(transform,5);SbMatrix matrix=SbMatrix::identity();matrix[0][0]=matrix[1][1]=matrix[2][2]=matrix[3][3]=2;transform->matrix=matrix;
  ok &=check(render()&&color(expected,"projective-q-two"),"NPOT projective STRQ divides all three axes");
  const auto stable=pixels;target->readbackDepth(depth);const auto stableDepth=depth;const auto serial=target->getLastSubmissionSerial();
  for(int kind=0;kind<3;++kind){matrix=SbMatrix::identity();if(kind==0)matrix[3][3]=0;if(kind==1)matrix[3][2]=std::numeric_limits<float>::quiet_NaN();if(kind==2)matrix[3][3]=std::numeric_limits<float>::quiet_NaN();transform->matrix=matrix;action.apply(scene.root);target->readbackRGBA(pixels);target->readbackDepth(depth);ok &=check(action.getLastStatus()!=CoinRenderAction::SUCCESS&&pixels==stable&&depth==stableDepth&&target->getLastSubmissionSerial()==serial,"NPOT invalid R/Q rejects atomically");}
  transform->matrix=SbMatrix::identity();ok &=check(render()&&pixels==stable,"NPOT R/Q rejection recovery");scene.root->removeChild(transform);
  auto* unit1=new SoTextureUnit;unit1->unit=1;auto* gray=new SoTexture2;const unsigned char grayPixel[]={128,128,128};gray->image.setValue(SbVec2s(1,1),3,grayPixel);gray->model=SoTexture2::MODULATE;scene.root->insertChild(gray,4);scene.root->insertChild(unit1,5);
  double composed[3];for(unsigned c=0;c<3;++c)composed[c]=expected[c]*128/255.;ok &=check(render()&&color(composed,"two-units"),"NPOT volume and 2D texture both contribute");scene.root->removeChild(unit1);scene.root->removeChild(gray);ok &=check(render()&&pixels==stable,"NPOT unit scope restoration");
  auto* blend=new SoTransparencyType;blend->value=SoTransparencyType::BLEND;scene.root->insertChild(blend,5);for(size_t a=3;a<source.size();a+=4)source[a]=128;scene.texture->images.setValue(SbVec3s(3,5,7),4,source.data());
  ok &=check(render()&&color(composed,"alpha"),"NPOT transparent alpha analytic composition over black");scene.root->removeChild(blend);for(size_t a=3;a<source.size();a+=4)source[a]=255;scene.texture->images.setValue(SbVec3s(3,5,7),4,source.data());
  // Lines and points must retain R and per-unit image coordinates too.
  auto neighborhood=[&](){for(int y=61;y<=66;++y)for(int x=61;x<=66;++x){size_t at=size_t(y*side+x)*4;bool match=pixels.size()==size_t(side*side*4);if(match)for(unsigned c=0;c<3;++c)match &=std::abs(double(pixels[at+c])-expected[c])<=5;if(match)return true;}return false;};
  scene.root->removeChild(face);
  for(int topology=0;topology<2;++topology){SoNode* shape=nullptr;if(topology==0){auto* line=new SoIndexedLineSet;const int32_t indices[]={0,1,-1};line->coordIndex.setValues(0,3,indices);shape=line;const SbVec3f p[]={SbVec3f(-3,0,0),SbVec3f(3,0,0)};positions->point.setValues(0,2,p);positions->point.setNum(2);}else{auto* point=new SoPointSet;point->numPoints=1;shape=point;positions->point.set1Value(0,SbVec3f(0,0,0));positions->point.setNum(1);}scene.root->addChild(shape);
    for(unsigned unit:{0u,7u}){SoTextureUnit* upper=nullptr;if(unit){upper=new SoTextureUnit;upper->unit=unit;scene.root->insertChild(upper,4);}for(float q:{.1f,.3f,.6f,.8f}){scene.complexity->textureQuality=q;ok &=check(render()&&neighborhood(),"NPOT LINES/POINTS sample last R slice with every quality and unit seven");}const auto geometryStable=pixels;target->readbackDepth(depth);const auto geometryDepth=depth;const auto geometrySerial=target->getLastSubmissionSerial();
    coords->point.set1Value(0,SbVec3f(selected[0],selected[1],std::numeric_limits<float>::quiet_NaN()));action.apply(scene.root);target->readbackRGBA(pixels);target->readbackDepth(depth);ok &=check(action.getLastStatus()!=CoinRenderAction::SUCCESS&&pixels==geometryStable&&depth==geometryDepth&&target->getLastSubmissionSerial()==geometrySerial,"NPOT LINES/POINTS nonfinite R rejects without publication");coords->point.setValues(0,4,samples);ok &=check(render()&&pixels==geometryStable,"NPOT LINES/POINTS R recovery");
    auto* badQ=new SoTextureMatrixTransform;SbMatrix singular=SbMatrix::identity();singular[3][3]=0;badQ->matrix=singular;scene.root->insertChild(badQ,scene.root->findChild(scene.texture)+1);const auto beforeQ=target->getLastSubmissionSerial();action.apply(scene.root);target->readbackRGBA(pixels);ok &=check(action.getLastStatus()!=CoinRenderAction::SUCCESS&&target->getLastSubmissionSerial()==beforeQ&&pixels==geometryStable,"NPOT LINES/POINTS Q zero rejects");scene.root->removeChild(badQ);ok &=check(render()&&pixels==geometryStable,"NPOT LINES/POINTS Q recovery");
    if(upper)scene.root->removeChild(upper);}scene.root->removeChild(shape);
  }
  auto* rasterMatrix=new SoTextureMatrixTransform;SbMatrix raster=SbMatrix::identity();raster[0][0]=raster[1][1]=raster[2][2]=0;raster[3][0]=selected[0];raster[3][1]=selected[1];raster[3][2]=selected[2];rasterMatrix->matrix=raster;scene.root->addChild(rasterMatrix);
  auto* image=new SoImage;image->horAlignment=SoImage::CENTER;image->vertAlignment=SoImage::HALF;std::vector<unsigned char> white(16*16*3,255);image->image.setValue(SbVec2s(16,16),3,white.data());scene.root->addChild(image);scene.complexity->textureQuality=.1f;
  // A singular texture matrix cannot turn native Unknown current UV into Known.
  const auto unknownStable=pixels;target->readbackDepth(depth);const auto unknownDepth=depth;const auto unknownSerial=target->getLastSubmissionSerial();
  action.apply(scene.root);target->readbackRGBA(pixels);target->readbackDepth(depth);
  ok &=check(action.getLastStatus()==CoinRenderAction::UNSUPPORTED&&target->getLastSubmissionSerial()==unknownSerial&&pixels==unknownStable&&depth==unknownDepth,"NPOT raster Unknown after geometry rejects atomically before matrix");
  // The audited object-linear generator authors all four raster coordinates,
  // independently of persistent geometry history and RasterPos.
  auto* rasterCoordinates=new SoTextureCoordinateObject;
  rasterCoordinates->factorS=SbVec4f(0,0,0,selected[0]);rasterCoordinates->factorT=SbVec4f(0,0,0,selected[1]);rasterCoordinates->factorR=SbVec4f(0,0,0,selected[2]);rasterCoordinates->factorQ=SbVec4f(0,0,0,1);
  scene.root->insertChild(rasterCoordinates,scene.root->findChild(rasterMatrix));raster=SbMatrix::identity();rasterMatrix->matrix=raster;
  phase="inherited-raster-defined-object-linear";ok &=check(render()&&color(expected,"inherited-raster"),"NPOT inherited raster preprocess samples authored last R slice from defined generator");
  if(target->getPimpl()->backend->isGpuBackend()) {
  phase="async-raster";CoinRenderReadbackTicket ticket;action.applyAsync(scene.root,ticket);
  if(action.getLastStatus()!=CoinRenderAction::SUCCESS||!ticket.token)std::cerr<<"npot async issuance "<<renderer<<" status="<<action.getLastStatus()<<" token="<<ticket.token<<" error="<<action.getLastError().getString()<<'\n';
  const bool issued=action.getLastStatus()==CoinRenderAction::SUCCESS&&ticket.token!=0;
  const auto ticketSerial=target->getLastSubmissionSerial();const auto ticketToken=ticket.token;
  raster[3][2]=std::numeric_limits<float>::quiet_NaN();rasterMatrix->matrix=raster;action.apply(scene.root);
  ok &=check(issued&&action.getLastStatus()!=CoinRenderAction::SUCCESS&&target->getLastSubmissionSerial()==ticketSerial&&ticket.token==ticketToken,"NPOT later invalid raster input preserves prior async ticket");
  std::vector<uint8_t> ticketColor;std::vector<float> ticketDepth;auto status=CoinRenderTarget::READBACK_NOT_READY;
  for(int attempts=0;attempts<500&&status==CoinRenderTarget::READBACK_NOT_READY;++attempts){status=CoinRenderTarget::pollReadback(ticket,ticketColor,ticketDepth);if(status==CoinRenderTarget::READBACK_NOT_READY)std::this_thread::sleep_for(std::chrono::milliseconds(2));}
  ok &=check(status==CoinRenderTarget::READBACK_READY&&ticketColor.size()==size_t(side*side*4),"NPOT retained async ticket resolves after rejected raster frame");
  if(status==CoinRenderTarget::READBACK_READY){pixels=ticketColor;ok &=color(expected,"async-retained");}
  } else std::cout<<"NPOT CPU reference backend has no async submit implementation; async profile=unsupported\n";
  raster[3][2]=0;rasterMatrix->matrix=raster;phase="raster-recovered";ok &=check(render()&&color(expected,"raster-recovered"),"NPOT inherited raster recovery");
  // Raster preprocessing obeys the same three-axis repeat/linear sampler.
  const SbVec3f repeated(-.17f,1.21f,1.37f);
  for(auto dims:{std::array<unsigned,3>{7,3,5},{5,7,3}}) {
    auto repeatSource=volume_oracle::authored(dims[0],dims[1],dims[2]);for(size_t a=3;a<repeatSource.size();a+=4)repeatSource[a]=255;
    const auto repeatLevels=volume_oracle::chain(dims[0],dims[1],dims[2],repeatSource);
    scene.texture->images.setValue(SbVec3s(dims[0],dims[1],dims[2]),4,repeatSource.data());
    scene.texture->wrapS=SoTexture3::REPEAT;scene.texture->wrapT=SoTexture3::REPEAT;scene.texture->wrapR=SoTexture3::REPEAT;scene.complexity->textureQuality=.3f;
    rasterCoordinates->factorS=SbVec4f(0,0,0,repeated[0]);rasterCoordinates->factorT=SbVec4f(0,0,0,repeated[1]);rasterCoordinates->factorR=SbVec4f(0,0,0,repeated[2]);
    double repeatExpected[3];for(unsigned c=0;c<3;++c)repeatExpected[c]=volume_oracle::sample(repeatLevels[0],repeated[0],repeated[1],repeated[2],c,true,true);
    phase="inherited-raster-repeat-linear dims="+std::to_string(dims[0])+"x"+std::to_string(dims[1])+"x"+std::to_string(dims[2]);
    ok &=check(render()&&color(repeatExpected,"inherited-raster-repeat-linear"),"NPOT inherited raster REPEAT LINEAR outside STR unit cube matches independent oracle");
  }
  scene.texture->images.setValue(SbVec3s(3,5,7),4,source.data());scene.texture->wrapS=SoTexture3::CLAMP;scene.texture->wrapT=SoTexture3::CLAMP;scene.texture->wrapR=SoTexture3::CLAMP;scene.complexity->textureQuality=.1f;
  rasterCoordinates->factorS=SbVec4f(0,0,0,selected[0]);rasterCoordinates->factorT=SbVec4f(0,0,0,selected[1]);rasterCoordinates->factorR=SbVec4f(0,0,0,selected[2]);
  phase="inherited-raster-repeat-restored";ok &=check(render()&&color(expected,"inherited-raster-repeat-restored"),"NPOT inherited raster returns to original clamped authored voxel");
  action.setRenderTarget(nullptr);return ok;
}
}
int main(int argc,char**argv){SoDB::init();CoinRenderAction::initClass();configureSamplingTest(argc,argv);bool cpuOnly=false,gpuOnly=false;for(int i=1;i<argc;++i){cpuOnly |= std::string(argv[i])=="--cpu";gpuOnly |= std::string(argv[i])=="--gpu-only";}if(cpuOnly&&gpuOnly)return 2;bool ok=true;if(!gpuOnly){std::unique_ptr<CoinRenderTarget> cpu(samplingTestOffscreen(SbVec2i32(side,side)));if(!cpu)return 1;cpu->getPimpl()->backend.reset(new CoinRenderCpuReferenceBackend);ok &=npotRun(cpu.get(),"CPU");}if(!cpuOnly){CoinRenderCapabilities caps{};bool gpu=coin_render_query_capabilities(COIN_RENDER_EXPERIMENTAL_OFFSCREEN,&caps,sizeof(caps))==0&&caps.gpu_available;std::cout<<"NPOT actual backend="<<caps.backend<<" renderer="<<caps.renderer<<" adapter="<<caps.adapter_name<<" policy="<<samplingTestPolicy<<'\n';if(gpu){std::unique_ptr<CoinRenderTarget> target(samplingTestOffscreen(SbVec2i32(side,side)));ok &=target&&npotRun(target.get(),"GPU");}else if(gpuOnly||std::getenv("COIN_RENDER_REQUIRE_TEXTURE3_GPU"))ok=false;}return ok?0:1;}
