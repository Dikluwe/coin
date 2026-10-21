
// Short public-scene reproducer. The original historical campaign is unchanged.
#define COIN_RENDER_SHADOW_MULTITEXTURE_WINDOW_FIXTURE_ONLY
#include "CoinRenderShadowMultitextureWindowTest.cpp"
namespace {
struct Difference {
 unsigned interior=0,bad=0;int maximum=0,firstX=-1,firstY=-1,firstChannel=-1,expected=0,actual=0;
};
Difference differenceInterior(const std::vector<uint8_t>&reference,const std::vector<uint8_t>&actual,int extent){
 Difference result;if(reference.size()!=size_t(extent)*extent*4||actual.size()!=reference.size()){result.bad=UINT32_MAX;return result;}
 for(int y=2;y<extent-2;++y)for(int x=2;x<extent-2;++x){const size_t i=size_t(y*extent+x)*4;bool smooth=true;
  for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){const size_t j=size_t((y+dy)*extent+x+dx)*4;smooth &=reference[j+3]==255;for(unsigned c=0;c<3;++c)smooth &=std::abs(int(reference[i+c])-int(reference[j+c]))<=8;}
  if(!smooth)continue;++result.interior;
  for(unsigned c=0;c<4;++c){const int delta=std::abs(int(reference[i+c])-int(actual[i+c]));if(c<3)result.maximum=std::max(result.maximum,delta);if((c<3&&delta>3)||(c==3&&actual[i+c]!=255)){if(!result.bad){result.firstX=x;result.firstY=y;result.firstChannel=int(c);result.expected=reference[i+c];result.actual=actual[i+c];}++result.bad;}}
 }return result;
}
void reportDifference(const Difference&delta,const char*phase,const char*stage){
 std::cout<<"window_transition_delta phase="<<phase<<" stage="<<stage<<" interiors="<<delta.interior<<" bad_channels="<<delta.bad<<" max="<<delta.maximum<<" first="<<delta.firstX<<','<<delta.firstY<<','<<delta.firstChannel<<" expected="<<delta.expected<<" actual="<<delta.actual<<'\n';
}
bool authoredColor(const std::vector<uint8_t>&bytes,int extent,const std::array<uint8_t,3>&rgb){
 if(bytes.size()!=size_t(extent)*extent*4)return false;
 unsigned bad=0,checked=0;
 for(int y=20;y<108;++y)for(int x=20;x<108;++x){size_t offset=size_t(y*extent+x)*4;for(unsigned c=0;c<3;++c)bad+=std::abs(int(bytes[offset+c])-int(rgb[c]))>3;bad+=bytes[offset+3]!=255;++checked;}
 return check(checked==88u*88u&&bad==0,"authored constant selected texel, RGB bound3 and opaque window alpha");
}
bool observeCurrent(WindowPair&h,const std::vector<uint8_t>&control,const std::vector<uint8_t>&previous,const std::array<uint8_t,3>&rgb,const char*phase){
 const auto begin=std::chrono::steady_clock::now(),deadline=begin+std::chrono::seconds(5);unsigned attempts=0,previousMatches=0;bool matched=false;std::vector<uint8_t>shown;Difference first,last;
 do{
  shown=ximage(h.d,h.window,h.extent);++attempts;last=differenceInterior(control,shown,h.extent);if(attempts==1)first=last;
  if(!previous.empty()&&equivalent(previous,shown,h.extent,false))++previousMatches;
  if(last.interior>unsigned(h.extent*h.extent/8)&&!last.bad&&std::chrono::steady_clock::now()<=deadline){matched=true;break;}
 }while(std::chrono::steady_clock::now()<deadline);
 reportDifference(first,phase,"first");reportDifference(last,phase,"terminal");
 std::cout<<"window_transition_present phase="<<phase<<" attempts="<<attempts<<" previous_matches="<<previousMatches<<" elapsed_ms="<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count()<<" current="<<matched<<'\n';
 h.lastShown=shown;
 return check(matched,"whole XGetImage current opaque interiors match the current single-apply control within original5s without sleep or reapply")&&authoredColor(shown,h.extent,rgb);
}
bool transition(WindowPair&h,Scene&s,const std::array<uint8_t,3>&rgb,const char*phase,Frame&control){
 if(!h.ready)return false;
 const auto previous=h.lastReadback;const uint64_t oldOff=h.off->getLastSubmissionSerial(),oldWindow=h.shown->getLastSubmissionSerial();
 const unsigned offCalls=h.offProxy->submits,windowCalls=h.windowProxy->submits;
 h.offAction.apply(s.root);h.off->readbackRGBA(control.rgba);h.off->readbackDepth(control.depth);control.serial=h.off->getLastSubmissionSerial();
 const uint64_t expectedOff=h.caps.backend==COIN_RENDER_EXPERIMENTAL_RUST?std::max(oldOff,oldWindow)+1:oldOff+1;
 bool ok=check(h.offAction.getLastStatus()==CoinRenderAction::SUCCESS&&control.serial==expectedOff&&h.offProxy->submits==offCalls+1&&control.rgba.size()==size_t(h.extent)*h.extent*4&&control.depth.size()==size_t(h.extent)*h.extent,"exactly one complete offscreen control publication")&&expectedColor(control,rgb);
 const bool requested=h.shown->requestWindowReadbackRGBA();h.windowAction.apply(s.root);const uint64_t next=h.shown->getLastSubmissionSerial();std::vector<uint8_t>read;h.shown->readbackRGBA(read);
 std::cout<<"window_transition phase="<<phase<<" samples="<<windowSamples<<" status="<<h.windowAction.getLastStatus()<<" serial="<<oldWindow<<','<<next<<" root_submits="<<windowCalls<<','<<h.windowProxy->submits<<" error="<<h.windowAction.getLastError().getString()<<'\n';
 const uint64_t expectedWindow=h.caps.backend==COIN_RENDER_EXPERIMENTAL_RUST?control.serial+1:oldWindow+1;
 ok &=check(requested&&h.windowAction.getLastStatus()==CoinRenderAction::SUCCESS&&next==expectedWindow&&h.windowProxy->submits==windowCalls+1,"same apply finishes current explicit readback with one logical window publication");
 const auto delta=differenceInterior(control.rgba,read,h.extent);reportDifference(delta,phase,"immediate_readback");
 ok &=check(delta.interior>unsigned(h.extent*h.extent/8)&&!delta.bad,"current explicit readback agrees with all smooth opaque control interiors RGB3")&&authoredColor(read,h.extent,rgb);
 // Observe once per accepted submission, regardless of an earlier numeric gate,
 // so diagnostics distinguish attachment/readback from physical publication.
 const bool physical=observeCurrent(h,control.rgba,previous,rgb,phase);
 h.lastReadback=read;h.latest=next;h.publishedOff=control.serial;h.publishedWindow=next;return physical&&ok;
}
bool atomicInvalid(WindowPair&h,Scene&s,const char*phase){
 std::vector<uint8_t>offBefore,offAfter,readAfter;std::vector<float>depthBefore,depthAfter;
 h.off->readbackRGBA(offBefore);h.off->readbackDepth(depthBefore);const auto physicalBefore=h.lastShown,readBefore=h.lastReadback;
 const auto offSerial=h.off->getLastSubmissionSerial(),windowSerial=h.shown->getLastSubmissionSerial();const auto offCalls=h.offProxy->submits,windowCalls=h.windowProxy->submits,offProducers=h.offProxy->producers,windowProducers=h.windowProxy->producers;
 h.offAction.apply(s.root);h.off->readbackRGBA(offAfter);h.off->readbackDepth(depthAfter);const bool requested=h.shown->requestWindowReadbackRGBA();h.windowAction.apply(s.root);h.shown->readbackRGBA(readAfter);
 const auto physicalAfter=ximage(h.d,h.window,h.extent);
 std::cout<<"window_transition_invalid phase="<<phase<<" off_status="<<h.offAction.getLastStatus()<<" window_status="<<h.windowAction.getLastStatus()<<" error="<<h.windowAction.getLastError().getString()<<'\n';
 return check(requested&&h.offAction.getLastStatus()!=CoinRenderAction::SUCCESS&&h.windowAction.getLastStatus()!=CoinRenderAction::SUCCESS&&h.offAction.getLastError().getLength()&&h.windowAction.getLastError().getLength()&&h.off->getLastSubmissionSerial()==offSerial&&h.shown->getLastSubmissionSerial()==windowSerial&&h.offProxy->submits==offCalls&&h.windowProxy->submits==windowCalls&&h.offProxy->producers==offProducers&&h.windowProxy->producers==windowProducers&&offBefore==offAfter&&depthBefore==depthAfter&&readBefore==readAfter&&physicalBefore==physicalAfter,"malformed transition executes zero producers/submissions and preserves whole published physical/readback/color/depth/serial");
}
bool matrixTransitions(WindowPair&h,unsigned cycles,int unit){
 Scene s(8,8,1.f,false);s.white();s.pattern(unit);s.uv(unit,.125f,.125f);Frame initial,changed,restored;
 if(!transition(h,s,{{240,30,60}},"spot Phong red restored STQ",initial))return false;const auto physicalA=h.lastShown;bool ok=true;
 for(unsigned i=0;i<cycles;++i){SbMatrix translated=SbMatrix::identity();translated[3][0]=.75f;s.matrices[unit]->matrix=translated;
  ok &=transition(h,s,{{40,220,70}},"spot Phong unit matrix translation",changed)&&check(difference(initial,changed)>1000,"matrix transition is observably different from previous physical frame");
  s.matrices[unit]->matrix=SbMatrix::identity();ok &=transition(h,s,{{240,30,60}},"matrix A/B/A restored",restored)&&check(exact(initial,restored)&&h.lastShown==physicalA,"matrix physical and offscreen A/B/A exact in same environment");}
 SbMatrix invalid=SbMatrix::identity();invalid[0][0]=std::numeric_limits<float>::quiet_NaN();s.matrices[unit]->matrix=invalid;ok &=atomicInvalid(h,s,"nonfinite authored matrix");
 s.matrices[unit]->matrix=SbMatrix::identity();ok &=transition(h,s,{{240,30,60}},"matrix refusal recovery",restored)&&check(exact(initial,restored)&&h.lastShown==physicalA,"matrix recovery retains exact A");return ok;
}
bool projectionTransitions(WindowPair&h,unsigned cycles,int unit){
 Scene s(8,8,1.f,true);s.white();s.pattern(unit);s.uv(unit,.75f,.125f);SbMatrix q=SbMatrix::identity();q[3][3]=2.f;s.matrices[unit]->matrix=q;
 Frame initial,changed,restored;if(!transition(h,s,{{40,220,70}},"directional Phong DIRECT_ST Q2",initial))return false;const auto physicalA=h.lastShown;bool ok=projectionPolicy(s,h.windowProxy->plan,CoinRenderTextureProjection::DIRECT_ST);
 for(unsigned i=0;i<cycles;++i){s.receiverStyle->style=SoShadowStyle::NO_SHADOWING;
  ok &=transition(h,s,{{240,30,60}},"directional Phong unshadowed PROJECTIVE Q2",changed)&&projectionPolicy(s,h.windowProxy->plan,CoinRenderTextureProjection::PROJECTIVE)&&check(difference(initial,changed)>1000,"Q2 projection transition observably changes selected texel");
  s.receiverStyle->style=SoShadowStyle::SHADOWED;ok &=transition(h,s,{{40,220,70}},"projection A/B/A restored",restored)&&check(exact(initial,restored)&&h.lastShown==physicalA,"projection physical and offscreen A/B/A exact");}
 s.receiverStyle->style=SoShadowStyle::NO_SHADOWING;q[3][3]=0;s.matrices[unit]->matrix=q;ok &=atomicInvalid(h,s,"projective Q zero");
 q[3][3]=2;s.matrices[unit]->matrix=q;s.receiverStyle->style=SoShadowStyle::SHADOWED;ok &=transition(h,s,{{40,220,70}},"projection refusal recovery",restored)&&check(exact(initial,restored)&&h.lastShown==physicalA,"projection recovery retains exact A");return ok;
}
}
int main(int argc,char**argv){
 configureSamplingTest(argc,argv);unsigned cycles=3;int unit=-1;std::string mode="--both";
 for(int i=1;i<argc;++i){std::string arg=argv[i];if(arg=="--portable-sampling")continue;if((arg=="--samples"||arg=="--cycles"||arg=="--unit")&&i+1<argc){const char*text=argv[++i];if(!*text)return 2;unsigned n=0;for(const char*p=text;*p;++p){if(*p<'0'||*p>'9'||n>100000)return 2;n=n*10+unsigned(*p-'0');}if(arg=="--samples")windowSamples=n;else if(arg=="--cycles")cycles=n;else unit=int(n);}else if(arg=="--both"||arg=="--matrix"||arg=="--projection")mode=arg;else return 2;}
 if((windowSamples!=1&&windowSamples!=2&&windowSamples!=4)||cycles<1||cycles>100||(unit!=-1&&unit!=0&&unit!=3&&unit!=7))return 2;
 if(!XInitThreads())return 1;SoDB::init();CoinRenderAction::initClass();const auto caps=msaa_shadow_test::capabilities(COIN_RENDER_EXPERIMENTAL_XLIB_WINDOW);if(!caps.gpu_available||caps.renderer==COIN_RENDER_RENDERER_UNKNOWN)return 77;if(!check(caps.max_texture_units>=8&&caps.max_lights_per_draw>=8,"reported limits cover original maximal8/8 input"))return 1;
 std::cout<<"window_transition_config samples="<<windowSamples<<" cycles="<<cycles<<" precise="<<(std::getenv("COIN_QUADMESH_PRECISE_LIGHTING")?std::getenv("COIN_QUADMESH_PRECISE_LIGHTING"):"<unset>")<<" sampling="<<samplingTestPolicy<<" renderer="<<caps.renderer<<'\n';
 WindowPair h(caps);if(!h.ready)return 1;bool ok=true;
 if(mode!="--projection")ok &=matrixTransitions(h,cycles,unit<0?0:unit);
 if(mode!="--matrix")ok &=projectionTransitions(h,cycles,unit<0?7:unit);
 return ok?0:1;
}
