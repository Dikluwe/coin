#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Inventor/SoDB.h>
#include <Inventor/SoOffscreenRenderer.h>
#include <Inventor/C/glue/gl.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/elements/SoGLCacheContextElement.h>
#include <Inventor/nodes/SoCallback.h>
#include <Inventor/nodes/SoComplexity.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoTexture2.h>
#include <Inventor/nodes/SoTexture3.h>
#include <Inventor/nodes/SoTextureCoordinate3.h>
#include <Inventor/nodes/SoTextureUnit.h>
#include <Inventor/system/gl.h>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {
struct Observation {
  int unit=0;
  GLint id=0,min=0,mag=0,mipWidth=0,mipHeight=0,mipDepth=0,active=0;
  std::vector<unsigned char> mipPixels;
  GLenum error=GL_NO_ERROR;
  bool seen=false;
};
void observe(void * raw,SoAction * action) {
  if (!action->isOfType(SoGLRenderAction::getClassTypeId())) return;
  auto & value=*static_cast<Observation *>(raw);
  glGetIntegerv(GL_ACTIVE_TEXTURE,&value.active);
  glActiveTexture(GL_TEXTURE0+value.unit);
  glGetIntegerv(GL_TEXTURE_BINDING_3D,&value.id);
  if (value.id) {
    glGetTexParameteriv(GL_TEXTURE_3D,GL_TEXTURE_MIN_FILTER,&value.min);
    glGetTexParameteriv(GL_TEXTURE_3D,GL_TEXTURE_MAG_FILTER,&value.mag);
    glGetTexLevelParameteriv(GL_TEXTURE_3D,1,GL_TEXTURE_WIDTH,&value.mipWidth);
    glGetTexLevelParameteriv(GL_TEXTURE_3D,1,GL_TEXTURE_HEIGHT,&value.mipHeight);
    glGetTexLevelParameteriv(GL_TEXTURE_3D,1,GL_TEXTURE_DEPTH,&value.mipDepth);
    if(value.mipWidth==4 && value.mipHeight==4 && value.mipDepth==4) {
      GLint alignment=0,rowLength=0,imageHeight=0,skipPixels=0,skipRows=0,
            skipImages=0,packBuffer=0;
      glGetIntegerv(GL_PACK_ALIGNMENT,&alignment);
      glGetIntegerv(GL_PACK_ROW_LENGTH,&rowLength);
      glGetIntegerv(GL_PACK_IMAGE_HEIGHT,&imageHeight);
      glGetIntegerv(GL_PACK_SKIP_PIXELS,&skipPixels);
      glGetIntegerv(GL_PACK_SKIP_ROWS,&skipRows);
      glGetIntegerv(GL_PACK_SKIP_IMAGES,&skipImages);
      glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING,&packBuffer);
      const cc_glglue * glue=cc_glglue_instance(
        SoGLCacheContextElement::get(action->getState()));
      cc_glglue_glBindBuffer(glue,GL_PIXEL_PACK_BUFFER,0);
      glPixelStorei(GL_PACK_ALIGNMENT,1);
      glPixelStorei(GL_PACK_ROW_LENGTH,0);
      glPixelStorei(GL_PACK_IMAGE_HEIGHT,0);
      glPixelStorei(GL_PACK_SKIP_PIXELS,0);
      glPixelStorei(GL_PACK_SKIP_ROWS,0);
      glPixelStorei(GL_PACK_SKIP_IMAGES,0);
      value.mipPixels.resize(size_t(value.mipWidth)*value.mipHeight*value.mipDepth*4);
      glGetTexImage(GL_TEXTURE_3D,1,GL_RGBA,GL_UNSIGNED_BYTE,value.mipPixels.data());
      glPixelStorei(GL_PACK_ALIGNMENT,alignment);
      glPixelStorei(GL_PACK_ROW_LENGTH,rowLength);
      glPixelStorei(GL_PACK_IMAGE_HEIGHT,imageHeight);
      glPixelStorei(GL_PACK_SKIP_PIXELS,skipPixels);
      glPixelStorei(GL_PACK_SKIP_ROWS,skipRows);
      glPixelStorei(GL_PACK_SKIP_IMAGES,skipImages);
      cc_glglue_glBindBuffer(glue,GL_PIXEL_PACK_BUFFER,GLuint(packBuffer));
    }
  }
  value.error=glGetError();
  glActiveTexture(value.active);
  value.seen=true;
}
bool check(bool passed,const char * label) {
  if (!passed) std::cerr << "texture_quality_transition: " << label << '\n';
  return passed;
}
struct Scene {
  SoSeparator * root=new SoSeparator;
  SoComplexity * quality=new SoComplexity;
  SoTexture3 * texture=new SoTexture3;
  Observation observation;
  Scene(int unit) {
    root->ref();observation.unit=unit;
    auto * camera=new SoOrthographicCamera;
    camera->position.setValue(0,0,10);camera->height=8;
    camera->nearDistance=1;camera->farDistance=50;root->addChild(camera);
    auto * light=new SoLightModel;light->model=SoLightModel::BASE_COLOR;root->addChild(light);
    auto * material=new SoMaterial;material->diffuseColor.setValue(1,1,1);root->addChild(material);
    quality->textureQuality=.6f;root->addChild(quality);
    if (unit) {
      auto * white=new SoTexture2;
      const unsigned char texel[]={255,255,255,255};
      white->image.setValue(SbVec2s(1,1),4,texel);
      root->addChild(white);
      auto * selector=new SoTextureUnit;selector->unit=unit;root->addChild(selector);
    }
    std::vector<unsigned char> source(8*8*8*4);
    for(int z=0;z<8;++z) for(int y=0;y<8;++y) for(int x=0;x<8;++x) {
      const size_t at=size_t(z*64+y*8+x)*4;
      source[at]=8+2*x*x+y*y+z*z;
      source[at+1]=8+x*x+2*y*y+z*z;
      source[at+2]=8+x*x+y*y+2*z*z;
      source[at+3]=255;
    }
    texture->model=SoTexture3::MODULATE;
    texture->wrapS=SoTexture3::CLAMP;
    texture->wrapT=SoTexture3::CLAMP;
    texture->wrapR=SoTexture3::CLAMP;
    texture->images.setValue(SbVec3s(8,8,8),4,source.data());root->addChild(texture);
    const double rho=std::pow(2.,2.5);
    const float left=float(.425-2*rho*(4+.03125));
    const float right=float(.425+2*rho*(4-.03125));
    const SbVec3f uv[]={SbVec3f(.425f,.425f,left),SbVec3f(.425f,.425f,right),
                        SbVec3f(.425f,.425f,right),SbVec3f(.425f,.425f,left)};
    auto * texcoords=new SoTextureCoordinate3;texcoords->point.setValues(0,4,uv);root->addChild(texcoords);
    auto * coords=new SoCoordinate3;
    const SbVec3f corners[]={SbVec3f(-4,-4,0),SbVec3f(4,-4,0),
                             SbVec3f(4,4,0),SbVec3f(-4,4,0)};
    coords->point.setValues(0,4,corners);root->addChild(coords);
    auto * probe=new SoCallback;probe->setCallback(observe,&observation);root->addChild(probe);
    auto * face=new SoIndexedFaceSet;
    const int32_t indices[]={0,1,2,3,-1};face->coordIndex.setValues(0,5,indices);
    root->addChild(face);
  }
  ~Scene(){root->unref();}
};
bool runContext(int unit) {
  Scene scene(unit);
  SoOffscreenRenderer renderer(SbViewportRegion(128,128));
  renderer.setBackgroundColor(SbColor(0,0,0));
  const float qualities[]={.6f,.8f,.6f,.3f,.1f,.8f,.6f};
  const int minFilters[]={GL_NEAREST_MIPMAP_LINEAR,GL_LINEAR_MIPMAP_LINEAR,
                          GL_NEAREST_MIPMAP_LINEAR,GL_LINEAR,GL_NEAREST,
                          GL_LINEAR_MIPMAP_LINEAR,GL_NEAREST_MIPMAP_LINEAR};
  const int magFilters[]={GL_LINEAR,GL_LINEAR,GL_LINEAR,GL_LINEAR,GL_NEAREST,GL_LINEAR,GL_LINEAR};
  GLint firstId=0;
  std::vector<unsigned char> firstMip;
  bool passed=true;
  for(size_t i=0;i<sizeof(qualities)/sizeof(qualities[0]);++i) {
    scene.quality->textureQuality=qualities[i];
    scene.observation.seen=false;
    const bool rendered=renderer.render(scene.root) && renderer.getBuffer();
    const auto & o=scene.observation;
    int red=-1;
    if(rendered) red=renderer.getBuffer()[(64*128+64)*3];
    const bool state=rendered && o.seen && o.error==GL_NO_ERROR && o.id>0 &&
      o.min==minFilters[i] && o.mag==magFilters[i] &&
      (i==0 || o.id==firstId) && o.mipWidth==4 &&
      o.mipHeight==4 && o.mipDepth==4 &&
      (i==0 || o.mipPixels==firstMip);
    if (i==0) {firstId=o.id;firstMip=o.mipPixels;}
    // Nonlinear volume and R-only footprint distinguish the two mip filters.
    const bool color=red>=0 && std::abs(red-(qualities[i]==.6f?50:
      qualities[i]==.8f?70:qualities[i]==.1f?44:42))<=5;
    std::cout << "quality_transition unit=" << unit << " step=" << i
              << " quality=" << qualities[i] << " rgb_red=" << red
              << " texture_id=" << o.id << " min=" << o.min << " mag=" << o.mag
              << " mip1_width=" << o.mipWidth << " gl_error=" << o.error
              << " state=" << state << " color=" << color << '\n';
    passed &= check(state,"GL filter, mip chain, or same-context texture identity");
    passed &= check(color,"volume image did not follow current filter band");
  }
  return passed;
}
}
int main(int argc,char ** argv) {
  int unit=-1;
  for(int i=1;i<argc;++i) {
    const std::string argument(argv[i]);
    if(argument=="--unit0") unit=0;
    else if(argument=="--unit3") unit=3;
    else return 2;
  }
  if(unit<0) return 2;
  SoDB::init();
  return runContext(unit)?0:1;
}
