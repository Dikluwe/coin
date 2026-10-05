#include <Inventor/SoDB.h>
#include <Inventor/SoOffscreenRenderer.h>
#include <Inventor/actions/SoGetBoundingBoxAction.h>
#include <Inventor/nodes/SoDirectionalLight.h>
#include <Inventor/nodes/SoPerspectiveCamera.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoFont.h>
#include <Inventor/nodes/SoText2.h>
#include <Inventor/nodes/SoTranslation.h>
#include <Inventor/nodes/SoCube.h>
#include <cstdio>
#include <cstring>
static int renderFailures = 0;
static void probe(const char* name,bool ortho,float x,float z,bool cube,bool fitted,bool manual) {
 SbViewportRegion vp(640,480);
 auto* root=new SoSeparator;root->ref();
 SoCamera* camera=ortho?static_cast<SoCamera*>(new SoOrthographicCamera):static_cast<SoCamera*>(new SoPerspectiveCamera);
 root->addChild(camera);root->addChild(new SoDirectionalLight);
 if(cube)root->addChild(new SoCube);
 auto* font=new SoFont;font->name="Times-Roman";font->size=24;root->addChild(font);
 auto* translation=new SoTranslation;translation->translation.setValue(x,0,z);root->addChild(translation);
 auto* text=new SoText2;text->string="Hello World!";root->addChild(text);
 SoGetBoundingBoxAction bbox(vp);bbox.apply(root);
 const bool before=bbox.getBoundingBox().isEmpty();
 const SbVec3f oldpos=camera->position.getValue();
 if(fitted)camera->viewAll(root,vp);
 if(manual){camera->position.setValue(x,0,z+5);camera->nearDistance=1;camera->farDistance=10;}
 bbox.apply(root);
 auto pos=camera->position.getValue();
 std::printf("%s ortho=%d x=%.2f z=%.2f cube=%d fit=%d manual=%d bbox_before_empty=%d bbox_after_empty=%d camera_moved=%d position=(%.3f,%.3f,%.3f)",name,ortho,x,z,cube,fitted,manual,before,bbox.getBoundingBox().isEmpty(),pos!=oldpos,pos[0],pos[1],pos[2]);
 SoOffscreenRenderer renderer(vp);renderer.setComponents(SoOffscreenRenderer::RGB);renderer.setBackgroundColor(SbColor(0,0,0));
 if(!renderer.render(root)){++renderFailures;std::puts(" render=FAILED");root->unref();return;}
 auto* pixels=renderer.getBuffer();unsigned count=0;for(int i=0;i<640*480;i++)if(pixels[3*i]||pixels[3*i+1]||pixels[3*i+2])++count;
 std::printf(" nonblack_pixels=%u\n",count);
 char path[256];std::snprintf(path,sizeof(path),"%s.ppm",name);FILE* out=std::fopen(path,"wb");if(out){std::fprintf(out,"P6\n640 480\n255\n");std::fwrite(pixels,1,640*480*3,out);std::fclose(out);}root->unref();
}
int main(){
 SoDB::init();
 
 probe("issue417-exact-scene",false,.25f,.25f,false,true,false);
 probe("issue417-manual-control",false,.25f,.25f,false,false,true);
 probe("visible-no-fit",false,0,0,false,false,false);
 probe("visible-fit",false,0,0,false,true,false);
 probe("moderate-outside-fit",false,5,0,false,true,false);
 probe("outside-fit",false,100,0,false,true,false);
 probe("outside-manual-control",false,100,0,false,false,true);
 probe("ortho-visible-fit",true,0,0,false,true,false);
 probe("mixed-outside-fit",false,100,0,true,true,false);SoDB::finish();return renderFailures ? 2 : 0;}
