import pathlib
root=pathlib.Path(r'C:\Users\Diklu\.codex\worktrees\sampling-win-20261010\coin')
p=root/'testsuite/coinrender/CoinRenderSamplingPolicyTest.cpp';s=p.read_text(encoding='utf-8')
s=s.replace('#include <chrono>','#include <chrono>\n#include <fstream>\n#include <cstdlib>')
helper=r'''
static void saveEvidence(const std::vector<uint8_t>& pixels,int side,const char* stage) {
  const char* prefix=std::getenv("COIN_RENDER_SAMPLING_PIXEL_PREFIX");
  if(!prefix || pixels.size()!=size_t(side*side*4))return;
  std::ofstream out(std::string(prefix)+"-"+stage+".ppm",std::ios::binary);
  out << "P6\n" << side << ' ' << side << "\n255\n";
  for(size_t i=0;i<pixels.size();i+=4)out.write(reinterpret_cast<const char*>(pixels.data()+i),3);
}
'''
s=s.replace('static bool selection(bool gpu, bool window = false) {',helper+'\nstatic bool selection(bool gpu, bool window = false) {',1)
s=s.replace('ok &= render(native.get());readPixels(native.get(),nativeFirst);','ok &= render(native.get());readPixels(native.get(),nativeFirst);saveEvidence(nativeFirst,64,"native-first");',1)
s=s.replace('if(portablePixels.size()!=64*64*4){ok=false;break;}','if(portablePixels.size()!=64*64*4){ok=false;break;}\n    saveEvidence(portablePixels,64,("portable-unit-"+std::to_string(textureUnit)).c_str());',1)
s=s.replace('  if (portablePixels.size()==128*128*4) {','  saveEvidence(portablePixels,128,"portable-resized");\n  if (portablePixels.size()==128*128*4) {',1)
bench=r'''
#ifdef _WIN32
  if(window && argc==5 && std::string(argv[2])=="--benchmark-no-readback") {
    const std::string policy=argv[3];
    ok &= check(policy=="native" || policy=="portable","benchmark explicit policy");
    windows.size(windows.portable,64);
    nativeAction.setRenderTarget(nullptr);native.reset();
    std::unique_ptr<CoinRenderTarget> measured(createTarget(policy=="portable"));
    CoinRenderAction timed(SbViewportRegion(64,64));timed.setRenderTarget(measured.get());
    std::ofstream csv(argv[4]);csv << "frame,warmup,render_present_ms,serial\n";
    bool admitted=measured && measured->getStatus()==CoinRenderTarget::TARGET_READY;
    for(int i=-60;admitted && i<600;++i) {
      SamplingWindows::pump();const auto before=measured->getLastSubmissionSerial();
      const auto start=std::chrono::steady_clock::now();timed.apply(root);const auto finish=std::chrono::steady_clock::now();
      admitted=timed.getLastStatus()==CoinRenderAction::SUCCESS && measured->getLastSubmissionSerial()>before;
      csv << i << ',' << (i<0) << ',' << std::chrono::duration<double,std::milli>(finish-start).count()
          << ',' << measured->getLastSubmissionSerial() << '\n';
    }
    csv.flush();ok &= check(admitted && bool(csv),"window timing frames publish with no readback calls");
    std::cout << "window_timing policy=" << policy << " frames=600 warmup=60 readback=none extent=64x64"
              << " metric=CPU_render_present_return includes_backpressure=1 gpu_completion=0 display_latency=0 result=" << admitted << '\n';
    timed.setRenderTarget(nullptr);
  }
#endif
'''
s=s.replace('  action.setRenderTarget(nullptr);nativeAction.setRenderTarget(nullptr);root->unref();',bench+'\n  action.setRenderTarget(nullptr);nativeAction.setRenderTarget(nullptr);root->unref();')
p.write_text(s,encoding='utf-8',newline='\n')
p=root/'testsuite/coinrender/CoinRenderShadowReferenceTest.cpp';s=p.read_text(encoding='utf-8')
# Preserve the five functional NPOT stages, outside the timed loops.
snippet=r'''
  if (const char* prefix=std::getenv("COIN_RENDER_NPOT_PIXEL_PREFIX")) {
    const std::vector<unsigned char>* stages[]={&shadowed,&clear,&opaque,&retained,&recovered};
    const char* names[]={"shadowed","clear","opaque","retained","recovered"};
    for(int i=0;i<5;++i)if(stages[i]->size()==size_t(side*side*4)) {
      std::ofstream out(std::string(prefix)+"-"+names[i]+".ppm",std::ios::binary);
      out << "P6\n" << side << ' ' << side << "\n255\n";
      for(size_t j=0;j<stages[i]->size();j+=4)out.write(reinterpret_cast<const char*>(stages[i]->data()+j),3);
    }
  }
'''
if '#include <fstream>' not in s:s=s.replace('#include <iostream>','#include <iostream>\n#include <fstream>')
s=s.replace('  if (qualified && benchmark) {',snippet+'\n  if (qualified && benchmark) {',1)
p.write_text(s,encoding='utf-8',newline='\n')
