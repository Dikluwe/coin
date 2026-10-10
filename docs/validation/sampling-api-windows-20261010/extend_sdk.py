import pathlib
root=pathlib.Path(r'C:\Users\Diklu\.codex\worktrees\sampling-win-20261010\coin')
p=root/'examples/coinrender/sdk-consumer/main.cpp'
s=p.read_text(encoding='utf-8')
s=s.replace('#include <Inventor/nodes/SoCube.h>', '#include <Inventor/nodes/SoCube.h>\n#include <Inventor/nodes/SoComplexity.h>\n#include <Inventor/nodes/SoCoordinate3.h>\n#include <Inventor/nodes/SoIndexedFaceSet.h>\n#include <Inventor/nodes/SoTexture2.h>\n#include <Inventor/nodes/SoTextureCoordinate2.h>')
s=s.replace('#include <vector>', '#include <vector>\n#include <fstream>\n#include <string>')
helper=r'''
static void savePixels(const std::string & path, const std::vector<uint8_t> & pixels, int side) {
  if (path.empty() || pixels.size()!=size_t(side*side*4)) return;
  std::ofstream out(path, std::ios::binary);
  out << "P6\n" << side << ' ' << side << "\n255\n";
  for(size_t i=0;i<pixels.size();i+=4) out.write(reinterpret_cast<const char*>(pixels.data()+i),3);
}

static bool samplingOracle(CoinRenderOptions options, const std::string & prefix) {
  std::unique_ptr<CoinRenderTarget> target(CoinRenderTarget::createOffscreen(SbVec2i32(64,64),options));
  if(!target || target->getStatus()!=CoinRenderTarget::TARGET_READY) return false;
  auto* root=new SoSeparator; root->ref();
  auto* camera=new SoOrthographicCamera; camera->height=2; camera->position=SbVec3f(0,0,3); root->addChild(camera);
  auto* light=new SoLightModel; light->model=SoLightModel::BASE_COLOR; root->addChild(light);
  auto* quality=new SoComplexity; quality->textureQuality=.5f; root->addChild(quality);
  auto* texture=new SoTexture2; texture->model=SoTexture2::REPLACE;
  std::vector<unsigned char> bytes(128*128*4,255);
  for(int y=0;y<128;++y)for(int x=0;x<128;++x)for(int c=0;c<3;++c)bytes[(y*128+x)*4+c]=x<64?40:160;
  texture->image.setValue(SbVec2s(128,128),4,bytes.data()); root->addChild(texture);
  auto* coords=new SoCoordinate3; const SbVec3f positions[]={{-1,-1,0},{1,-1,0},{1,1,0},{-1,1,0}};
  coords->point.setValues(0,4,positions); root->addChild(coords);
  auto* uv=new SoTextureCoordinate2; const SbVec2f st[]={{-1.46875f,.53125f},{2.53125f,.53125f},{2.53125f,.53125f},{-1.46875f,.53125f}};
  uv->point.setValues(0,4,st); root->addChild(uv);
  auto* face=new SoIndexedFaceSet; const int32_t indices[]={0,1,2,3,-1};face->coordIndex.setValues(0,5,indices);root->addChild(face);
  CoinRenderAction action(SbViewportRegion(64,64)); action.setRenderTarget(target.get());action.apply(root);
  std::vector<uint8_t> pixels;target->readbackRGBA(pixels);
  bool ok=action.getLastStatus()==CoinRenderAction::SUCCESS && pixels.size()==64*64*4;
  int maximum=0;
  if(ok && options.textureSamplingPolicy==COIN_RENDER_SAMPLING_PORTABLE) {
    for(int y=8;y<56;++y)for(int x:{30,31})for(int c=0;c<3;++c)
      maximum=std::max(maximum,std::abs(int(pixels[(y*64+x)*4+c])-(x==30?40:160)));
    ok=maximum<=1;
  }
  if(!prefix.empty())savePixels(prefix+"-mip.ppm",pixels,64);
  std::cout << "sdk_sampling policy=" << options.textureSamplingPolicy << " serial=" << target->getLastSubmissionSerial()
            << " floor_oracle=" << (options.textureSamplingPolicy==COIN_RENDER_SAMPLING_PORTABLE)
            << " maximumRGB=" << maximum << " result=" << ok << '\n';
  if(!ok)std::cerr << action.getLastError().getString() << '\n';
  action.setRenderTarget(nullptr);root->unref();return ok;
}
'''
s=s.replace('int main(int argc, char ** argv) {',helper+'\nint main(int argc, char ** argv) {')
s=s.replace('int main(int argc, char ** argv) {','int main(int argc, char ** argv) {\n  std::cout << std::unitbuf;',1)
s=s.replace('if (argc != 2)', 'if (argc < 2 || argc > 4)')
s=s.replace('d3d12|vulkan|opengl\\n','d3d12|vulkan|opengl [native|portable] [pixel-prefix]\\n')
s=s.replace('CoinRenderOptions options; options.renderer=renderer;', '''CoinRenderOptions options; options.renderer=renderer;
  if(argc>=3) {
    if(!std::strcmp(argv[2],"portable")) options.textureSamplingPolicy=COIN_RENDER_SAMPLING_PORTABLE;
    else if(std::strcmp(argv[2],"native")) return 2;
  }
  const std::string prefix=argc==4 ? argv[3] : "";
  const auto selection=coin_render_select_sampling_policy(&caps,options.textureSamplingPolicy,0);
  const auto strict=coin_render_select_sampling_policy(&caps,options.textureSamplingPolicy,1);
  std::cout << "sdk_policy=" << options.textureSamplingPolicy << " available_selection=" << selection.reason
            << " qualified_selection=" << strict.reason << '\\n';
  if(selection.reason!=COIN_RENDER_SELECTION_SUPPORTED)return 3;''')
s=s.replace('action.setRenderTarget(nullptr);\n  root->unref();\n  return ok ? 0 : 5;', '''if(!prefix.empty())savePixels(prefix+"-cube.ppm",pixels,32);
  action.setRenderTarget(nullptr);
  root->unref();
  ok=samplingOracle(options,prefix) && ok;
  return ok ? 0 : 5;''')
p.write_text(s,encoding='utf-8',newline='\n')
