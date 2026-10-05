#include <Inventor/SoDB.h>
#include <Inventor/SoPickedPoint.h>
#include <Inventor/details/SoTextDetail.h>
#include <Inventor/actions/SoGetBoundingBoxAction.h>
#include <Inventor/actions/SoRayPickAction.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoFont.h>
#include <Inventor/nodes/SoText2.h>
#include <cstdio>
#include <initializer_list>
int main(){SoDB::init();for(const char *value:{"A","AA","\xc3\xa9","\xc3\xa9" "A","A" "\xc3\xa9","\xe4\xb8\x96"}){
 auto *root=new SoSeparator;root->ref();auto *camera=new SoOrthographicCamera;camera->position.setValue(0,0,5);camera->nearDistance=1;camera->farDistance=10;root->addChild(camera);auto *font=new SoFont;font->size=24;root->addChild(font);auto *text=new SoText2;text->string=value;root->addChild(text);SbViewportRegion vp(640,480);SoGetBoundingBoxAction action(vp);action.apply(root);auto box=action.getBoundingBox();auto mn=box.getMin(),mx=box.getMax();printf("string=%s bytes=%d",value,text->string[0].getLength());for(float t:{.1f,.3f,.5f,.7f,.9f}){SbVec3f ray(mn[0]+(mx[0]-mn[0])*t,(mn[1]+mx[1])*.5f,3);SoRayPickAction pick(vp);pick.setRay(ray,SbVec3f(0,0,-1),0,10);pick.apply(root);int index=-1;auto *point=pick.getPickedPoint();if(point){auto *detail=static_cast<const SoTextDetail*>(point->getDetail(text));if(detail)index=detail->getCharacterIndex();}printf(" %.1f:%d",t,index);}puts("");root->unref();}SoDB::finish();}
