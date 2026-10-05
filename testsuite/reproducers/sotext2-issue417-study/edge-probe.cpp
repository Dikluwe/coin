#include <initializer_list>
#include <Inventor/SoDB.h>
#include <Inventor/actions/SoGetBoundingBoxAction.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoText2.h>
#include <Inventor/nodes/SoTranslation.h>
#include <Inventor/elements/SoViewVolumeElement.h>
#include <cstdio>
class WidenedText : public SoText2 {
protected:
 void computeBBox(SoAction* action,SbBox3f& box,SbVec3f& center) override {
  SoText2::computeBBox(action,box,center);
  if(!box.hasVolume()) { auto vv=SoViewVolumeElement::get(action->getState());vv.scaleWidth(1.02f);SoViewVolumeElement::set(action->getState(),this,vv);SoText2::computeBBox(action,box,center); }
 }
};
int main(){SoDB::init();for(bool widened:{false,true}) {auto* root=new SoSeparator;root->ref();auto* camera=new SoOrthographicCamera;camera->position.setValue(0,0,5);camera->nearDistance=1;camera->farDistance=10;root->addChild(camera);auto* t=new SoTranslation;t->translation.setValue(1.34f,0,0);root->addChild(t);SoText2* text=widened?new WidenedText:new SoText2;text->string="Label";root->addChild(text);SoGetBoundingBoxAction action(SbViewportRegion(640,480));action.apply(root);auto b=action.getBoundingBox();std::printf("widened=%d empty=%d",widened,b.isEmpty());if(!b.isEmpty())std::printf(" minx=%.6f maxx=%.6f",b.getMin()[0],b.getMax()[0]);std::puts("");root->unref();}SoDB::finish();}
