#include <Inventor/C/basic.h>
#include <Inventor/SbName.h>
#include <Inventor/SoDB.h>
#include <Inventor/SoInput.h>
#include <Inventor/SoOutput.h>
#include <Inventor/SoPath.h>
#include <Inventor/actions/SoCallbackAction.h>
#include <Inventor/actions/SoSearchAction.h>
#include <Inventor/elements/SoLazyElement.h>
#include <Inventor/fields/SoSFEnum.h>
#include <Inventor/nodes/SoPickStyle.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/SoRenderManager.h>
#include <dlfcn.h>
#include <cstdio>
#include <cstring>
#include <stdexcept>
static_assert(COIN_MAJOR_VERSION==4,"Coin 4 consumer required");
static int checks=0;
static void require(bool value,const char* name){if(!value)throw std::runtime_error(name);++checks;}
int main(){try{
 SoDB::init();
 require(std::strcmp(SoDB::getVersion(),"SIM Coin 4.0.10")==0,"runtime Coin 4.0.10");
 SoLazyElement::LightModel model=SoLazyElement::PHONG;
 require(model==SoLazyElement::PHONG,"original enum type and value");
 SoInput input;SoInput* virtualInput=&input;
 const char data[]="0x2a 123 0.5";virtualInput->setBuffer(data,std::strlen(data));
 uint32_t hex=0;require(virtualInput->readHex(hex)&&hex==42,"old readHex virtual slot");
 int integer=0;require(virtualInput->read(integer)&&integer==123,"old read integer virtual slot");
 float real=0;require(virtualInput->read(real)&&real==.5f,"old read float virtual slot");
 SoOutput output;const SbName name("LegacyCoin4Node");
 void(SoOutput::*add)(SbName)=&SoOutput::addDEFNode;
 SbBool(SoOutput::*lookup)(SbName)=&SoOutput::lookupDEFNode;
 void(SoOutput::*remove)(SbName)=&SoOutput::removeDEFNode;
 (output.*add)(name);require((output.*lookup)(name),"old output value signature");
 (output.*remove)(name);require(!(output.*lookup)(name),"old output remove value signature");
 SoSeparator* root=new SoSeparator;root->ref();auto* pick=new SoPickStyle;root->addChild(pick);pick->setName(name);
 require(pick->style.getValue()==SoPickStyle::SHAPE,"old PickStyle layout/default");
 SoSearchAction search;void(SoSearchAction::*setName)(SbName)=&SoSearchAction::setName;
 (search.*setName)(name);search.apply(root);require(search.getPath()&&search.getPath()->getTail()==pick,"old search value signature");
 SoPath* path=new SoPath(root);path->ref();path->append(0);path->setName(SbName("LegacyPath"));
 SoPath*(*byName)(SbName)=&SoPath::getByName;
 require(byName(SbName("LegacyPath"))==path,"old path value signature");path->unref();
 SoSFEnum field;const int values[]={7};const SbName names[]={SbName("SEVEN")};field.setEnums(1,values,names);
 void(SoSFEnum::*setEnum)(SbName)=&SoSFEnum::setValue;(field.*setEnum)(names[0]);require(field.getValue()==7,"old enum value signature");
 SoCallbackAction callback;callback.apply(root);require(!callback.hasTerminated(),"old callback object layout");
 SoRenderManager manager;manager.setSceneGraph(root);require(manager.getSceneGraph()==root,"old RenderManager object layout");
 Dl_info library={};require(dladdr(reinterpret_cast<void*>(&SoDB::getVersion),&library)&&library.dli_fname,"loaded library provenance");
 std::printf("Legacy Coin 4 consumer passed checks=%d runtime=%s library=%s sizes: Input=%zu Callback=%zu PickStyle=%zu RenderManager=%zu\n",checks,SoDB::getVersion(),library.dli_fname,sizeof(SoInput),sizeof(SoCallbackAction),sizeof(SoPickStyle),sizeof(SoRenderManager));
 root->unref();return 0;
 }catch(const std::exception&e){std::fprintf(stderr,"Legacy consumer failed checks=%d: %s\n",checks,e.what());return 1;}}
