#include <Inventor/SoDB.h>
#include <Inventor/actions/SoGetBoundingBoxAction.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoPerspectiveCamera.h>
#include <Inventor/nodes/SoFont.h>
#include <Inventor/nodes/SoText2.h>
#include <Inventor/nodes/SoTranslation.h>
#include <string>
#include <cstdio>
#include <cmath>
static void probe(const char *tag,int chars,int lines,float spacing,float x,float z,int width=640,int height=480) {
 auto *root=new SoSeparator;root->ref();auto *camera=new SoPerspectiveCamera;camera->position.setValue(0,0,5);camera->nearDistance=1;camera->farDistance=10;root->addChild(camera);
 auto *font=new SoFont;font->name="Times-Roman";font->size=12;root->addChild(font);
 auto *tr=new SoTranslation;tr->translation.setValue(x,0,z);root->addChild(tr);
 auto *text=new SoText2;text->spacing=spacing;std::string line(chars,'M');for(int i=0;i<lines;++i)text->string.set1Value(i,line.c_str());root->addChild(text);
 SoGetBoundingBoxAction action(SbViewportRegion(width,height));action.apply(root);const auto box=action.getBoundingBox();float sx=0,sy=0,sz=0;auto center=SbVec3f(0,0,0);if(!box.isEmpty()){box.getSize(sx,sy,sz);center=box.getCenter();}
 printf("%s chars=%d lines=%d spacing=%g anchor=(%g,0,%g) viewport=%dx%d empty=%d size=(%.9g,%.9g,%.9g) center=(%.9g,%.9g,%.9g)\n",tag,chars,lines,spacing,x,z,width,height,box.isEmpty(),sx,sy,sz,center[0],center[1],center[2]);root->unref();
}
int main(){SoDB::init();for(int n:{1000,2000,3000,4000,6000})probe("wide",n,1,1,0,0);for(int n:{1000,2000,3000,4000})probe("tall",1,n,1,0,0);for(float s:{0.f,-1.f,0.1f,10000.f})probe("spacing",1,3,s,0,0);for(float z:{4.9999f,5.f,5.0001f,6.f})probe("camera-plane",12,1,1,0,z);for(float x:{1000.f,1000000.f,1000000000.f})probe("far",12,1,1,x,0);probe("zero-width",12,1,1,0,0,0,480);probe("large-viewport",12,1,1,0,0,40000,480);SoDB::finish();}
