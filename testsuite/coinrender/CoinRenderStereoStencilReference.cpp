#define GL_GLEXT_PROTOTYPES 1
#include "CoinRenderStereoStencilReference.h"
#include <GL/gl.h>
#include <GL/glext.h>
#include <dlfcn.h>
#include <cstdlib>
#include <exception>
#include <iostream>

namespace stereo_stencil_reference {
static thread_local Scope *active=nullptr;
Scope::Scope():previous(active){active=this;}
Scope::~Scope(){active=previous;}
using DrawPixels=void(GLAPIENTRY*)(GLsizei,GLsizei,GLenum,GLenum,const GLvoid*);
static DrawPixels realDrawPixels(){
  static DrawPixels call=reinterpret_cast<DrawPixels>(dlsym(RTLD_NEXT,"glDrawPixels"));
  if(!call){std::cerr<<"stereo_stencil_reference FAIL missing real glDrawPixels"<<std::endl;std::abort();}
  return call;
}
static void fail(const std::string &message){if(active&&active->error.empty())active->error=message;
  std::cerr<<"stereo_stencil_reference FAIL "<<message<<std::endl;}
static void draw(GLsizei width,GLsizei height,GLenum format,GLenum type,const GLvoid *pixels){
  const auto real=realDrawPixels();
  const bool image=active&&active->imageUploads&&active->clipControlAvailable&&type==GL_UNSIGNED_BYTE&&
    (format==GL_LUMINANCE||format==GL_LUMINANCE_ALPHA||format==GL_RGB||format==GL_RGBA);
  if(image){GLint origin=GL_LOWER_LEFT;glGetIntegerv(GL_CLIP_ORIGIN,&origin);
    if(origin==GL_UPPER_LEFT){
      GLfloat raster[4]={},zoomX=0,zoomY=0;GLboolean valid=GL_FALSE;
      glGetFloatv(GL_CURRENT_RASTER_POSITION,raster);glGetBooleanv(GL_CURRENT_RASTER_POSITION_VALID,&valid);
      glGetFloatv(GL_ZOOM_X,&zoomX);glGetFloatv(GL_ZOOM_Y,&zoomY);
      const auto placement=upperLeftImageRaster(raster[0],raster[1],zoomX,zoomY);
      // Unlike initStencil, SoImage already uses the reflected action
      // viewport. Its raster anchor is correct. Reflect only row advance;
      // retain byte order, skips, transfer, color mask, depth and samples.
      glPushAttrib(GL_CURRENT_BIT|GL_PIXEL_MODE_BIT);glPixelZoom(placement.zoomX,placement.zoomY);
      real(width,height,format,type,pixels);glPopAttrib();
      GLfloat after[4]={},afterX=0,afterY=0;GLboolean afterValid=GL_FALSE;
      glGetFloatv(GL_CURRENT_RASTER_POSITION,after);glGetBooleanv(GL_CURRENT_RASTER_POSITION_VALID,&afterValid);
      glGetFloatv(GL_ZOOM_X,&afterX);glGetFloatv(GL_ZOOM_Y,&afterY);
      bool restored=valid==afterValid&&zoomX==afterX&&zoomY==afterY;
      for(unsigned c=0;c<4;++c)restored &= raster[c]==after[c];
      if(!restored)fail("SoImage reference did not restore raster position/validity/zoom");
      ++active->imageUploadCount;
      std::cout<<"manager_stereo_gl_image_orientation size="<<width<<"x"<<height<<" format="<<format
        <<" raster="<<raster[0]<<","<<raster[1]<<" zoom="<<zoomX<<","<<zoomY<<" reference_zoom_y="<<placement.zoomY
        <<" native_anchor=1 source_rows_unchanged=1 restored="<<restored<<std::endl;
      return;
    }
  }
  if(!active||format!=GL_STENCIL_INDEX||type!=GL_BITMAP){real(width,height,format,type,pixels);return;}
  GLint buffer=0;glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&buffer);
  if(buffer){fail("native manager bitmap must use a client pointer, not an unpack PBO");return;}
  GLfloat raster[4]={},zoomX=0,zoomY=0;GLboolean rasterValid=GL_FALSE;GLint viewport[4]={},clipOrigin=GL_LOWER_LEFT,clipDepth=GL_NEGATIVE_ONE_TO_ONE;
  glGetFloatv(GL_CURRENT_RASTER_POSITION,raster);glGetBooleanv(GL_CURRENT_RASTER_POSITION_VALID,&rasterValid);
  glGetFloatv(GL_ZOOM_X,&zoomX);glGetFloatv(GL_ZOOM_Y,&zoomY);glGetIntegerv(GL_VIEWPORT,viewport);
  if(active->clipControlAvailable){glGetIntegerv(GL_CLIP_ORIGIN,&clipOrigin);glGetIntegerv(GL_CLIP_DEPTH_MODE,&clipDepth);}
  std::cout<<"manager_stereo_gl_stencil_raster valid="<<int(rasterValid)<<" position="<<raster[0]<<","<<raster[1]<<","<<raster[2]<<","<<raster[3]
    <<" viewport="<<viewport[0]<<","<<viewport[1]<<","<<viewport[2]<<","<<viewport[3]<<" zoom="<<zoomX<<","<<zoomY
    <<" clip_origin="<<(clipOrigin==GL_UPPER_LEFT?"upper_left":"lower_left")<<" clip_depth="<<clipDepth<<std::endl;
  Layout layout;GLint lsb=0;
  glGetIntegerv(GL_UNPACK_ALIGNMENT,&layout.alignment);glGetIntegerv(GL_UNPACK_ROW_LENGTH,&layout.rowLength);
  glGetIntegerv(GL_UNPACK_SKIP_PIXELS,&layout.skipPixels);glGetIntegerv(GL_UNPACK_SKIP_ROWS,&layout.skipRows);
  glGetIntegerv(GL_UNPACK_LSB_FIRST,&lsb);layout.lsbFirst=lsb!=0;
  std::vector<uint8_t> bytes;std::string error;
  if(!expand(width,height,layout,static_cast<const uint8_t*>(pixels),std::numeric_limits<size_t>::max(),bytes,error)){
    fail(error);return;
  }
  // The GL oracle changes clip origin to match the API's MSAA sample
  // origin. Legacy glDrawPixels still advances in window +Y. Keep native
  // bottom-origin bitmap rows, but map them down from H-bottomOrigin so
  // odd extents and nonzero viewport origins retain their exact phase.
  const bool upper=clipOrigin==GL_UPPER_LEFT;
  if(upper){
    if(!rasterValid||active->framebufferHeight<=0){fail("upper-left stencil reference needs a valid raster and framebuffer height");return;}
    const auto position=upperLeftRaster(active->framebufferHeight,viewport[0],viewport[1],zoomX,zoomY);
    glPushAttrib(GL_CURRENT_BIT|GL_PIXEL_MODE_BIT);
    glWindowPos3f(position.x,position.y,raster[2]);glPixelZoom(position.zoomX,position.zoomY);
    std::cout<<"manager_stereo_gl_stencil_orientation framebuffer_height="<<active->framebufferHeight
      <<" raster="<<position.x<<","<<position.y<<" zoom="<<position.zoomX<<","<<position.zoomY
      <<" source_row_order=bottom_native phase_changed=0"<<std::endl;
  }
  // Index transfer/map, stencil tests, write masks and all sample state
  // remain native. Unpack changes only describe the equivalent byte array.
  glPixelStorei(GL_UNPACK_ALIGNMENT,1);glPixelStorei(GL_UNPACK_ROW_LENGTH,0);
  glPixelStorei(GL_UNPACK_SKIP_PIXELS,0);glPixelStorei(GL_UNPACK_SKIP_ROWS,0);
  real(width,height,GL_STENCIL_INDEX,GL_UNSIGNED_BYTE,bytes.data());
  glPixelStorei(GL_UNPACK_ALIGNMENT,layout.alignment);glPixelStorei(GL_UNPACK_ROW_LENGTH,layout.rowLength);
  glPixelStorei(GL_UNPACK_SKIP_PIXELS,layout.skipPixels);glPixelStorei(GL_UNPACK_SKIP_ROWS,layout.skipRows);
  if(upper){
    glPopAttrib();GLfloat restored[4]={},restoredX=0,restoredY=0;GLboolean valid=GL_FALSE;
    glGetFloatv(GL_CURRENT_RASTER_POSITION,restored);glGetBooleanv(GL_CURRENT_RASTER_POSITION_VALID,&valid);
    glGetFloatv(GL_ZOOM_X,&restoredX);glGetFloatv(GL_ZOOM_Y,&restoredY);
    bool same=valid==rasterValid&&restoredX==zoomX&&restoredY==zoomY;
    for(unsigned c=0;c<4;++c)same &= restored[c]==raster[c];
    if(!same)fail("native raster position/validity/zoom was not restored after the stencil reference upload");
  }
  ++active->uploads;
  std::cout<<"manager_stereo_gl_stencil_reference source_type=GL_BITMAP reference_type=GL_UNSIGNED_BYTE size="<<width<<"x"<<height
    <<" unpack_alignment="<<layout.alignment<<" row_length="<<layout.rowLength<<" skip_pixels="<<layout.skipPixels
    <<" skip_rows="<<layout.skipRows<<" lsb_first="<<layout.lsbFirst<<" legacy_bitmap_api_qualified=0"<<std::endl;
}
}
extern "C"
#if defined(__GNUC__) || defined(__clang__)
__attribute__((visibility("default")))
#endif
void GLAPIENTRY glDrawPixels(GLsizei width,GLsizei height,GLenum format,GLenum type,const GLvoid *pixels){
  try{stereo_stencil_reference::draw(width,height,format,type,pixels);}
  catch(const std::exception &error){stereo_stencil_reference::fail(error.what());}
  catch(...){stereo_stencil_reference::fail("unknown native-oracle bitmap expansion exception");}
}

extern "C"
#if defined(__GNUC__) || defined(__clang__)
__attribute__((visibility("default")))
#endif
void GLAPIENTRY glBitmap(GLsizei width,GLsizei height,GLfloat xOrigin,GLfloat yOrigin,
                        GLfloat xMove,GLfloat yMove,const GLubyte *bitmap){
  using Bitmap=void(GLAPIENTRY*)(GLsizei,GLsizei,GLfloat,GLfloat,GLfloat,GLfloat,const GLubyte*);
  static Bitmap real=reinterpret_cast<Bitmap>(dlsym(RTLD_NEXT,"glBitmap"));
  if(!real){std::cerr<<"stereo_image_reference FAIL missing real glBitmap"<<std::endl;std::abort();}
  auto *scope=stereo_stencil_reference::active;
  if(scope&&scope->imageUploads&&scope->clipControlAvailable&&!width&&!height&&!bitmap){
    GLint origin=GL_LOWER_LEFT;glGetIntegerv(GL_CLIP_ORIGIN,&origin);
    if(origin==GL_UPPER_LEFT){++scope->bitmapMoveCount;
      real(width,height,xOrigin,yOrigin,xMove,stereo_stencil_reference::upperLeftBitmapYMove(yMove),bitmap);return;}
  }
  real(width,height,xOrigin,yOrigin,xMove,yMove,bitmap);
}

extern "C"
#if defined(__GNUC__) || defined(__clang__)
__attribute__((visibility("default")))
#endif
void GLAPIENTRY glPolygonStipple(const GLubyte *mask){
  using Stipple=void(GLAPIENTRY*)(const GLubyte*);
  static Stipple real=reinterpret_cast<Stipple>(dlsym(RTLD_NEXT,"glPolygonStipple"));
  if(!real){std::cerr<<"stereo_stipple_reference FAIL missing real glPolygonStipple"<<std::endl;std::abort();}
  auto *scope=stereo_stencil_reference::active;
  if(!scope||!scope->clipControlAvailable){real(mask);return;}
  GLint origin=GL_LOWER_LEFT;glGetIntegerv(GL_CLIP_ORIGIN,&origin);
  if(origin!=GL_UPPER_LEFT){real(mask);return;}
  try{
    GLint unpackBuffer=0,packBuffer=0;glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&unpackBuffer);glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING,&packBuffer);
    if(unpackBuffer||packBuffer){stereo_stencil_reference::fail("finite stipple oracle requires client unpack/pack pointers");return;}
    stereo_stencil_reference::Layout unpack;GLint lsb=0;
    glGetIntegerv(GL_UNPACK_ALIGNMENT,&unpack.alignment);glGetIntegerv(GL_UNPACK_ROW_LENGTH,&unpack.rowLength);
    glGetIntegerv(GL_UNPACK_SKIP_PIXELS,&unpack.skipPixels);glGetIntegerv(GL_UNPACK_SKIP_ROWS,&unpack.skipRows);
    glGetIntegerv(GL_UNPACK_LSB_FIRST,&lsb);unpack.lsbFirst=lsb!=0;
    std::vector<uint8_t> normalized;std::string error;
    if(!stereo_stencil_reference::reflectStipple(scope->framebufferHeight,unpack,mask,std::numeric_limits<size_t>::max(),normalized,error)){
      stereo_stencil_reference::fail(error);return;
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT,1);glPixelStorei(GL_UNPACK_ROW_LENGTH,0);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS,0);glPixelStorei(GL_UNPACK_SKIP_ROWS,0);glPixelStorei(GL_UNPACK_LSB_FIRST,GL_FALSE);
    real(normalized.data());
    glPixelStorei(GL_UNPACK_ALIGNMENT,unpack.alignment);glPixelStorei(GL_UNPACK_ROW_LENGTH,unpack.rowLength);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS,unpack.skipPixels);glPixelStorei(GL_UNPACK_SKIP_ROWS,unpack.skipRows);glPixelStorei(GL_UNPACK_LSB_FIRST,lsb);
    // Read the actual GL stipple state back with explicit packing. Neither
    // its enable flag nor sample/alpha/color/depth state is changed here.
    const GLenum packNames[]={GL_PACK_ALIGNMENT,GL_PACK_ROW_LENGTH,GL_PACK_SKIP_PIXELS,GL_PACK_SKIP_ROWS,GL_PACK_LSB_FIRST};
    GLint pack[5]={};for(unsigned i=0;i<5;++i)glGetIntegerv(packNames[i],&pack[i]);
    glPixelStorei(GL_PACK_ALIGNMENT,1);glPixelStorei(GL_PACK_ROW_LENGTH,0);glPixelStorei(GL_PACK_SKIP_PIXELS,0);
    glPixelStorei(GL_PACK_SKIP_ROWS,0);glPixelStorei(GL_PACK_LSB_FIRST,GL_FALSE);
    GLubyte observed[128]={};glGetPolygonStipple(observed);
    for(unsigned i=0;i<5;++i)glPixelStorei(packNames[i],pack[i]);
    bool correct=true;for(unsigned i=0;i<128;++i)correct &= observed[i]==normalized[i];
    const GLenum unpackNames[]={GL_UNPACK_ALIGNMENT,GL_UNPACK_ROW_LENGTH,GL_UNPACK_SKIP_PIXELS,GL_UNPACK_SKIP_ROWS,GL_UNPACK_LSB_FIRST};
    const GLint wanted[]={unpack.alignment,unpack.rowLength,unpack.skipPixels,unpack.skipRows,lsb};
    for(unsigned i=0;i<5;++i){GLint actual=0;glGetIntegerv(unpackNames[i],&actual);correct &= actual==wanted[i];}
    for(unsigned i=0;i<5;++i){GLint actual=0;glGetIntegerv(packNames[i],&actual);correct &= actual==pack[i];}
    if(!correct)stereo_stencil_reference::fail("real stipple bytes or restored pixel-store state differ from independent upper-origin oracle");
    ++scope->stippleUploadCount;
    std::cout<<"manager_stereo_gl_stipple_orientation framebuffer_height="<<scope->framebufferHeight
      <<" source_row=(H-1-row)%32 source_bits_unchanged=1 real_gl_state_verified="<<correct<<" unpack_pack_restored="<<correct<<std::endl;
  }catch(const std::exception &error){stereo_stencil_reference::fail(error.what());}
  catch(...){stereo_stencil_reference::fail("unknown stipple reference normalization exception");}
}
