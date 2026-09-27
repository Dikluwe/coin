// Tests the production QuarterWidget/SoNaviCube from the supplied FreeCAD build.
// No presenter, request queue, traversal or backend implementation is duplicated.
#include <Quarter/Quarter.h>
#include <Quarter/QuarterWidget.h>
#include <Inventor/SoDB.h>
#include <Inventor/SoOffscreenRenderer.h>
#include <Inventor/actions/SoWgpuRenderAction.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/misc/SoContextHandler.h>
#include <Inventor/rendering/SoWgpuCapabilities.h>
#include <Inventor/rendering/SoWgpuSceneManager.h>
#include <Inventor/rendering/SoWgpuRenderTarget.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoTransform.h>
#include <Inventor/nodes/SoAnnotation.h>
#include <Inventor/nodes/SoDepthBuffer.h>
#include <Inventor/nodes/SoPolygonOffset.h>
#include <Inventor/SoType.h>
#include <Gui/Inventor/SoNaviCube.h>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QMainWindow>
#include <QDockWidget>
#include <QLabel>
#include <QScreen>
#include <QPainter>
#include <QTest>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QOffscreenSurface>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>

using QuarterWidget = SIM::Coin3D::Quarter::QuarterWidget;
namespace {
QString artifacts;
QJsonObject report;
QStringList errors;
void messages(QtMsgType type, const QMessageLogContext &, const QString & message) {
  std::cerr << "QT " << int(type) << " " << message.toStdString() << '\n';
  if (message.contains("WGPU", Qt::CaseInsensitive) && message.contains("failed", Qt::CaseInsensitive))
    errors.append(message);
}
void require(bool condition, const char * message) {
  if (!condition) throw std::runtime_error(message);
}
void spin(int ms) { QTest::qWait(ms); }
bool until(const std::function<bool()> & predicate, int ms=4000) {
  QElapsedTimer timer; timer.start();
  while (!predicate() && timer.elapsed() < ms) spin(20);
  return predicate();
}
class View final : public QuarterWidget {
public:
  explicit View(QWidget * parent=nullptr) : QuarterWidget(parent) {
    setBackgroundColor(QColor(24, 42, 64));
    setContextMenuEnabled(false);
    setNavigationModeFile();
  }
  bool request() { return requestWgpuFrame(); }
  qulonglong frameCount() const { return property("wgpuFrameCount").toULongLong(); }
  QWidget * surface() const {
    for (auto * child : findChildren<QWidget *>())
      if (child->testAttribute(Qt::WA_PaintOnScreen) && child->testAttribute(Qt::WA_NativeWindow))
        return child;
    return nullptr;
  }
  void dprNotify(qreal ratio) { emit devicePixelRatioChanged(ratio); }
};
QImage capture(QWidget * widget, const QString & name, bool save=true) {
  const QPoint p = widget->mapToGlobal(QPoint(0,0));
  QImage image = widget->screen()->grabWindow(0, p.x(), p.y(), widget->width(), widget->height()).toImage().convertToFormat(QImage::Format_RGBA8888);
  require(!image.isNull(), "native window capture returned no pixels");
  require(!save || image.save(artifacts+"/"+name+".png"), "could not save image artifact");
  return image;
}
int redPixels(const QImage & image) {
  int count=0;
  for (int y=0; y<image.height(); ++y) for (int x=0; x<image.width(); ++x) {
    QColor c=image.pixelColor(x,y);
    if (c.red()>120 && c.red()>2*c.green() && c.red()>2*c.blue()) ++count;
  }
  return count;
}
double difference(const QImage & a, const QImage & b) {
  if (a.size()!=b.size()) return 255;
  double sum=0;
  for (int y=0; y<a.height(); ++y) for (int x=0; x<a.width(); ++x) {
    auto ca=a.pixelColor(x,y), cb=b.pixelColor(x,y);
    sum+=std::abs(ca.red()-cb.red())+std::abs(ca.green()-cb.green())+std::abs(ca.blue()-cb.blue());
  }
  return sum/(a.width()*a.height()*3.0);
}
void labelsVisible(const QImage & reference, const QImage & actual,
                   const QImage & plainReference, const QImage & plainActual,
                   int & expected, int & actualCount, int & mismatch) {
  expected=0; actualCount=0; mismatch=0;
  for(int y=0;y<reference.height();++y) for(int x=0;x<reference.width();++x) {
    auto delta=[](QColor a,QColor b){return std::abs(a.red()-b.red())+
      std::abs(a.green()-b.green())+std::abs(a.blue()-b.blue())>20;};
    bool a=delta(reference.pixelColor(x,y),plainReference.pixelColor(x,y));
    bool b=delta(actual.pixelColor(x,y),plainActual.pixelColor(x,y));
    expected+=a; actualCount+=b; mismatch+=(a!=b);
  }
  require(expected>40,"GL NaviCube oracle has no legible front label; comparison would be vacuous");
  require(mismatch<expected*0.35,"front text missing/incomplete or rear text leaked through cube");
}
SoSeparator * scene(SoTransform ** transform=nullptr, SoMaterial ** material=nullptr,
                    SoOrthographicCamera ** cameraOut=nullptr) {
  auto * root=new SoSeparator;
  auto * camera=new SoOrthographicCamera;
  camera->position=SbVec3f(0,0,5); camera->height=4;
  root->addChild(camera);
  auto * light=new SoLightModel; light->model=SoLightModel::BASE_COLOR; root->addChild(light);
  auto * tr=new SoTransform; root->addChild(tr);
  auto * mat=new SoMaterial; mat->diffuseColor=SbColor(1,0.05f,0.02f); root->addChild(mat);
  auto * cube=new SoCube; cube->width=1.5f; cube->height=1.2f; root->addChild(cube);
  if(transform) *transform=tr; if(material) *material=mat;
  if(cameraOut) *cameraOut=camera;
  return root;
}
void rendered(View * view, const QString & name) {
  require(until([&]{return view->surface() && view->surface()->isVisible() && view->frameCount()>0;}), "Quarter never exposed a visible native WGPU surface");
  spin(180);
  require(errors.empty(), "Quarter reported WGPU failure");
  // The production counter proves submission, not GPU completion. Software
  // shaders may need longer; wait boundedly for the unchanged pixel assertion,
  // without requesting another frame or accepting a black surface.
  QImage visible;
  const bool hasGeometry=until([&]{
    require(view->surface()!=nullptr && view->property("wgpuFallbackReason").toString().isEmpty(),
            "viewport fell back to GL while awaiting GPU presentation");
    visible=capture(view->surface(),name,false);
    return redPixels(visible)>100;
  },5000);
  require(visible.save(artifacts+"/"+name+".png"), "could not save presented image");
  require(hasGeometry, "exposed surface is black/empty or contains no rendered geometry after GPU visibility timeout");
}
void lifecycle(const QString & test) {
  QMainWindow window;
  auto * view=new View;
  SoTransform * transform=nullptr;
  SoOrthographicCamera * camera=nullptr;
  SoMaterial * firstMaterial=nullptr;
  view->setSceneGraph(scene(&transform,&firstMaterial,&camera));
  if(qEnvironmentVariableIntValue("COIN_TEST_PRIMARY_TRANSPARENT")==1)
    firstMaterial->transparency=0.25f;
  window.setCentralWidget(view);
  window.resize(480,360); window.show();
  rendered(view, "first-expose");
  report["native_surface"]=QString::number(view->surface()->winId());
  report["actual_dpr"]=view->devicePixelRatioF();
  const qulonglong initialFrames=view->frameCount();
  if(test=="first-expose") return;
  if(test=="frame-coalescing" || test=="idle") {
    spin(250);
    const qulonglong before=view->frameCount();
    if(test=="frame-coalescing") {
      for(int i=0;i<100;++i) require(view->request(), "frame request rejected after first expose");
      require(until([&]{return view->frameCount()>before;}), "accepted frame requests never ran");
      spin(250);
      require(view->frameCount()-before<=3, "100 requests did not coalesce into a bounded number of frames");
    }
    spin(300);
    const qulonglong idle=view->frameCount(); spin(450);
    report["idle_frames"]=qint64(view->frameCount()-idle);
    require(view->frameCount()==idle, "static viewport continuously schedules frames");
  } else if(test=="resize") {
    for(int i=0;i<24;++i) { window.resize(430+i*5,330+i*3); spin(25); }
    rendered(view,"resize-final");
    require(view->frameCount()>initialFrames,"resize retained old pixels without a new frame");
    require(view->surface()->size()==view->viewport()->size(), "native surface did not follow viewport resize");
  } else if(test=="maximize") {
    window.showMaximized(); spin(400); rendered(view,"maximized");
    require(window.isMaximized(), "window manager did not maximize window");
    window.showNormal(); spin(400); rendered(view,"restored");
    require(view->frameCount()>initialFrames,"maximize/restore did not submit a fresh frame");
  } else if(test=="minimize") {
    window.showMinimized(); spin(300);
    require(window.isMinimized(), "window manager did not minimize window");
    window.showNormal(); spin(400); rendered(view,"restored");
    require(view->frameCount()>initialFrames,"minimize/restore did not submit a fresh frame");
  } else if(test=="panel") {
    auto * panel=new QDockWidget("bottom panel",&window);
    panel->setWidget(new QLabel("Report / Python console panel"));
    window.addDockWidget(Qt::BottomDockWidgetArea,panel);
    panel->setMinimumHeight(110);
    spin(300); rendered(view,"panel-open");
    require(view->surface()->size()==view->viewport()->size(), "bottom panel left stale surface dimensions");
    panel->hide(); spin(300); rendered(view,"panel-closed");
    require(view->frameCount()>initialFrames,"panel layout retained old pixels without a fresh frame");
  } else if(test=="dpr") {
    // Notification-only regression plus real initial DPR runs supplied by run.py.
    // Changing a signal is not claimed as a real monitor migration.
    view->dprNotify(2.0); spin(200); rendered(view,"dpr-notify");
    require(view->frameCount()>initialFrames,"DPR notification did not trigger a fresh frame");
    report["dpr_transition"]="notification-only; real 1x/2x tested in separate processes";
  } else if(test=="recreate") {
    QWidget * old=window.takeCentralWidget(); delete old; spin(100);
    view=new View; view->setSceneGraph(scene()); window.setCentralWidget(view);
    spin(300); rendered(view,"recreated");
  } else if(test=="wheel-rotation") {
    const QImage before=capture(view->surface(),"input-before");
    // Actual Quarter event handler must modify camera/scene; no direct scene edit.
    auto * input=view->viewport();
    QPointF pos(input->rect().center()), global=input->mapToGlobal(pos.toPoint());
    QWheelEvent wheel(pos,global,QPoint(),QPoint(0,120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
    QApplication::sendEvent(input,&wheel);
    QTest::mousePress(input,Qt::LeftButton,Qt::NoModifier,pos.toPoint());
    QTest::mouseMove(input,pos.toPoint()+QPoint(65,35),50);
    QTest::mouseRelease(input,Qt::LeftButton,Qt::NoModifier,pos.toPoint()+QPoint(65,35));
    spin(500);
    const auto after=capture(view->surface(),"input-after");
    require(difference(before,after)>0.3, "wheel/rotation did not update the presented pixels");
  } else if(test=="two-viewports") {
    QMainWindow other;
    SoTransform * secondTransform=nullptr;
    SoOrthographicCamera * secondCamera=nullptr;
    SoMaterial * secondMaterial=nullptr;
    auto * second=new View; second->setSceneGraph(scene(&secondTransform,&secondMaterial,&secondCamera));
    secondMaterial->transparency=0.25f;
    secondTransform->rotation.setValue(SbVec3f(0,1,0),0.7f);
    other.setCentralWidget(second); other.resize(320,280); other.move(700,50); other.show();
    rendered(second,"second-viewport");
    require(view->surface()->winId()!=second->surface()->winId(), "viewports share a native surface");
    require(!view->property("wgpuOpenGLViewport").toBool() &&
            !second->property("wgpuOpenGLViewport").toBool(), "multi-viewport silently fell back to GL");
    require(redPixels(capture(view->surface(),"first-with-second"))>100, "second viewport corrupted the first");
    const QImage secondBefore=capture(second->surface(),"second-independent-before");
    const QImage firstBefore=capture(view->surface(),"first-independent-before");
    const auto firstFrames=view->frameCount();
    camera->position=SbVec3f(0,4,5);
    camera->pointAt(SbVec3f(0,0,0));
    require(view->request() && until([&]{return view->frameCount()>firstFrames;}), "first viewport stopped submitting");
    spin(200);
    require(difference(firstBefore,capture(view->surface(),"first-independent-after"))>0.3,
            "first viewport scene update was not presented");
    require(difference(secondBefore,capture(second->surface(),"second-independent-after"))<0.1,
            "first viewport update changed second viewport pixels");
    const auto secondFrames=second->frameCount();
    secondCamera->position=SbVec3f(4,3,5);
    secondCamera->pointAt(SbVec3f(0,0,0));
    require(second->request() && until([&]{return second->frameCount()>secondFrames;}), "second viewport stopped submitting");
    spin(200);
    require(difference(secondBefore,capture(second->surface(),"second-updated"))>0.3,
            "second independent scene update was not presented");
    // Destroy the initialization owner while a second native target remains.
    delete window.takeCentralWidget(); view=nullptr;
    spin(100); other.resize(410,330);
    const auto survivingFrames=second->frameCount();
    require(second->request() && until([&]{return second->frameCount()>survivingFrames;}),
            "destroying first viewport stopped the second target");
    rendered(second,"second-after-first-destroy");
    require(second->surface()->size()==second->viewport()->size(), "surviving target resize is stale");
    view=new View; view->setSceneGraph(scene()); window.setCentralWidget(view);
    rendered(view,"first-reopened"); rendered(second,"second-after-first-reopen");
    delete other.takeCentralWidget();
    const auto reopenedFrames=view->frameCount();
    require(view->request() && until([&]{return view->frameCount()>reopenedFrames;}),
            "destroying second viewport stopped the reopened target");
    rendered(view,"reopened-after-second-destroy");
    report["independent_targets"]=true;
    report["destroy_initial_owner"]=true;
  }
  report["wgpu_frames"]=qint64(view->frameCount());
  if(view->surface()) capture(view->surface(),"final");
}
QImage readWgpu(SoNode * root) {
  SoWgpuSceneManager manager(SbVec2i32(256,256));
  manager.getRenderTarget()->setDepthReadbackEnabled(FALSE);
  manager.setBackgroundColor(SbColor4f(0.12f,0.16f,0.2f,1));
  manager.setSceneGraph(root);
  auto status=manager.render();
  require(status==SoWgpuRenderAction::SUCCESS, manager.getLastError().getString());
  std::vector<uint8_t> pixels; manager.getRenderTarget()->readbackRGBA(pixels);
  require(pixels.size()==256*256*4,"successful GPU frame did not publish RGBA");
  return QImage(pixels.data(),256,256,256*4,QImage::Format_RGBA8888).copy();
}
QImage readGl(SoNode * root) {
  // Qt creates a direct compatibility context: legacy Coin GLX pixmaps use
  // indirect rendering, which modern Xvfb/Mesa intentionally disables.
  QSurfaceFormat format; format.setVersion(2,1); format.setDepthBufferSize(24);
  QOpenGLContext context; context.setFormat(format);
  require(context.create(),"GL oracle context creation failed");
  QOffscreenSurface surface; surface.setFormat(context.format()); surface.create();
  require(context.makeCurrent(&surface),"GL oracle makeCurrent failed");
  QImage image;
  {
    QOpenGLFramebufferObject fbo(256,256,QOpenGLFramebufferObject::CombinedDepthStencil);
    require(fbo.isValid() && fbo.bind(),"GL oracle framebuffer creation failed");
    auto * gl=context.functions(); gl->glViewport(0,0,256,256);
    gl->glClearColor(0.12f,0.16f,0.2f,1); gl->glEnable(GL_DEPTH_TEST);
    gl->glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    static uint32_t contextId=50000;
    SoGLRenderAction action(SbViewportRegion(256,256)); action.setCacheContext(++contextId);
    action.apply(root); gl->glFinish();
    // QOpenGLFramebufferObject::toImage() returns a premultiplied QImage when
    // the FBO has alpha. Converting it to RGBA8888 would unpremultiply RGB and
    // manufacture a translucent-only difference against raw BGFX readback.
    image=QImage(256,256,QImage::Format_RGBA8888);
    gl->glReadPixels(0,0,256,256,GL_RGBA,GL_UNSIGNED_BYTE,image.bits());
    image=image.mirrored();
    SoContextHandler::destructingContext(contextId);
  }
  context.doneCurrent();
  require(!image.isNull(),"GL oracle returned no pixels");
  return image;
}
void visual(const QString & test) {
  if(test=="axis-cross" || test=="rubber-band") {
    report["status"]="UNSUPPORTED"; report["expected_failure"]=true;
    report["reason"]="legacy GL viewer overlay lacks an exported WGPU scene node; requires full FreeCAD viewer probe";
    return;
  }
  if(test=="navicube") {
    Gui::SoNaviCube::initClass();
    auto * root=new SoSeparator; root->ref();
    auto * cube=new Gui::SoNaviCube; root->addChild(cube);
    cube->viewportRect=SbVec4f(0,0,256,256);
    cube->size=100;
    cube->showCoordinateSystem=FALSE;
    cube->opacity=1;
    cube->baseAlpha=qEnvironmentVariable("COIN_TEST_ALPHA")=="opaque"?1.0f:0.55f;
    using Id=Gui::SoNaviCube::PickId;
    const Id ids[]={Id::Front,Id::Rear,Id::Top,Id::Bottom,Id::Right,Id::Left};
    const char * labels[]={"FRONT","REAR","TOP","BOTTOM","RIGHT","LEFT"};
    std::vector<QImage> labelImages;
    for(int face=0;face<6;++face) {
      QImage label(128,64,QImage::Format_RGBA8888); label.fill(Qt::transparent);
      QPainter painter(&label); painter.setPen(Qt::white);
      QFont font("DejaVu Sans",18); font.setBold(true); painter.setFont(font);
      painter.drawText(label.rect(),Qt::AlignCenter,labels[face]); painter.end();
      labelImages.push_back(label);
    }
    auto installLabels=[&] { for(int face=0;face<6;++face)
      cube->setLabelImage(ids[face],SbVec2s(128,64),4,labelImages[face].constBits()); };
    const SbRotation orientations[]={
      SbRotation(SbVec3f(0,0,1),SbVec3f(0,0,1)),
      SbRotation(SbVec3f(0,0,1),SbVec3f(0,0,-1)),
      SbRotation(SbVec3f(0,0,1),SbVec3f(0,1,0)),
      SbRotation(SbVec3f(0,0,1),SbVec3f(0,-1,0)),
      SbRotation(SbVec3f(0,0,1),SbVec3f(1,0,0)),
      SbRotation(SbVec3f(0,0,1),SbVec3f(-1,0,0)),
      SbRotation(SbVec3f(1,1,0),0.65f)};
    constexpr int orientationCount=7;
    double worst=0; bool labelsMatch=true;
    QJsonArray labelMaskResults;
    for(int orientation=0;orientation<orientationCount;++orientation) {
      cube->cameraOrientation=orientations[orientation];
      installLabels();
      const QImage reference=readGl(root), actual=readWgpu(root);
      reference.save(artifacts+QString("/navicube-%1-gl.png").arg(orientation));
      actual.save(artifacts+QString("/navicube-%1-wgpu.png").arg(orientation));
      const double mae=difference(reference,actual); worst=std::max(worst,mae);
      cube->clearLabelTextures();
      const auto plainReference=readGl(root), plainActual=readWgpu(root);
      plainReference.save(artifacts+QString("/navicube-%1-gl-unlabelled.png").arg(orientation));
      plainActual.save(artifacts+QString("/navicube-%1-wgpu-unlabelled.png").arg(orientation));
      int expectedLabelPixels=0, actualLabelPixels=0, mismatchedLabelPixels=0;
      try { labelsVisible(reference,actual,plainReference,plainActual,
                          expectedLabelPixels,actualLabelPixels,mismatchedLabelPixels); }
      catch(const std::exception &) { labelsMatch=false; }
      labelMaskResults.append(QJsonObject{{"orientation",orientation},
        {"expected_pixels",expectedLabelPixels},{"actual_pixels",actualLabelPixels},
        {"mismatched_pixels",mismatchedLabelPixels}});
      std::cout<<"VISUAL orientation="<<orientation<<" mae="<<mae<<'\n';
      // Use independent production GL traversal, not WGPU-generated goldens.
    }
    report["orientations"]=orientationCount; report["rgb_mae_max"]=worst;
    report["label_masks_match"]=labelsMatch;
    report["label_mask_results"]=labelMaskResults;
    cube->clearLabelTextures(); root->unref();
    require(worst<6.0,"NaviCube differs from GL oracle: complete/front labels, rear occlusion or depth composition regression");
    require(labelsMatch,"NaviCube label mask differs from GL oracle or reference lacks front labels");
  } else if(test=="depth" || test=="polygon-offset") {
    double worst=0;
    QJsonArray states;
    const int functions[]={SoDepthBuffer::LESS,SoDepthBuffer::LEQUAL,SoDepthBuffer::NEVER,
      SoDepthBuffer::ALWAYS,SoDepthBuffer::GREATER,SoDepthBuffer::GEQUAL,
      SoDepthBuffer::EQUAL,SoDepthBuffer::NOTEQUAL};
    const int count=test=="depth"?10:2;
    for(int i=0;i<count;++i) {
      auto * root=scene(); root->ref();
      auto * depth=new SoDepthBuffer;
      depth->test=i!=8; depth->write=i!=9;
      depth->function=functions[std::min(i,7)];
      if(i>=8 || test=="polygon-offset") depth->function=SoDepthBuffer::LEQUAL;
      root->insertChild(depth,2);
      auto * second=new SoSeparator; root->addChild(second);
      auto * tr=new SoTransform; tr->translation=SbVec3f(0,0,test=="depth"?-0.4f:0);
      second->addChild(tr);
      auto * material=new SoMaterial; material->diffuseColor=SbColor(0,1,0); second->addChild(material);
      if(test=="polygon-offset") {
        auto * offset=new SoPolygonOffset; offset->factor=1; offset->units=1; offset->on=i==1;
        second->addChild(offset);
      }
      auto * cube=new SoCube; cube->width=1.5f; cube->height=1.2f; second->addChild(cube);
      QImage gl=readGl(root), wgpu=readWgpu(root);
      gl.save(artifacts+QString("/state-%1-reference.png").arg(i));
      wgpu.save(artifacts+QString("/state-%1-actual.png").arg(i));
      const double mae=difference(gl,wgpu);
      states.append(QJsonObject{{"index",i},{"depth_test",bool(depth->test.getValue())},
        {"depth_write",bool(depth->write.getValue())},{"depth_function",depth->function.getValue()},
        {"polygon_offset",test=="polygon-offset" && i==1},{"rgb_mae",mae}});
      worst=std::max(worst,mae); root->unref();
    }
    report["states"]=count; report["rgb_mae_max"]=worst;
    report["state_results"]=states;
    require(worst<3,"depth test/write/function or polygon-offset differs from GL reference");
  } else if(test=="annotation" || test=="foregroundroot" || test=="decorationroot") {
    auto * root=scene(); root->ref();
    // A named root alone cannot verify the FreeCAD viewer's wiring.
    if(test!="annotation") {
      root->unref();
      report["status"]="UNSUPPORTED";
      report["reason"]="private FreeCAD viewer root wiring is not exposed by Quarter; full viewer instrumentation required";
      return;
    }
    auto * annotation=new SoAnnotation; root->addChild(annotation);
    auto * material=new SoMaterial; material->diffuseColor=SbColor(0,1,0);
    auto * behind=new SoTransform; behind->translation=SbVec3f(0,0,-2);
    annotation->addChild(behind);
    annotation->addChild(material); annotation->addChild(new SoCube);
    auto gl=readGl(root), wgpu=readWgpu(root);
    gl.save(artifacts+"/reference.png"); wgpu.save(artifacts+"/actual.png");
    require(difference(gl,wgpu)<3,"SoAnnotation ordering differs from GL oracle");
    root->unref();
  }
}
} // namespace
int main(int argc,char **argv) {
  QApplication app(argc,argv);
  qInstallMessageHandler(messages);
  QString test=argc>1?QString::fromLocal8Bit(argv[1]):"first-expose";
  artifacts=qEnvironmentVariable("COIN_TEST_ARTIFACTS");
  if(artifacts.isEmpty()) artifacts="artifacts/"+test;
  QDir().mkpath(artifacts);
  report["test"]=test; report["status"]="PASS";
  report["renderer"]=qEnvironmentVariable("COIN_BGFX_RENDERER");
  try {
    require(qEnvironmentVariableIntValue("FREECAD_COIN_WGPU")==1,"FREECAD_COIN_WGPU=1 is mandatory");
    require(app.platformName()=="xcb","native X11/xcb platform is mandatory");
    SIM::Coin3D::Quarter::Quarter::init();
    SoWgpuRenderAction::initClass();
    CoinWgpuExperimentalCapabilities caps{};
    require(coin_wgpu_experimental_query_capabilities(COIN_WGPU_EXPERIMENTAL_OFFSCREEN,&caps,sizeof(caps))==0,"GPU capability query failed");
    report["adapter"]=QString::fromUtf8(caps.adapter_name);
    report["gpu_available"]=int(caps.gpu_available);
    // Some BGFX builds intentionally report zero for a side-effect-free query.
    // Only an actual frame proves availability; never skip on this query alone.
    if(QStringList{"navicube","depth","polygon-offset","annotation","foregroundroot","decorationroot","axis-cross","rubber-band"}.contains(test)) {
      visual(test);
    } else {
      lifecycle(test);
    }
    require(errors.empty(),"Qt reported a WGPU rendering failure");
  } catch(const std::exception & error) {
    report["status"]="FAIL"; report["reason"]=QString::fromUtf8(error.what());
    // Capture every top-level window before a runner timeout/crash artifact is lost.
    if(app.primaryScreen()) app.primaryScreen()->grabWindow(0).save(artifacts+"/failure-screen.png");
  }
  std::cout<<"RESULT "<<QJsonDocument(report).toJson(QJsonDocument::Compact).toStdString()<<std::endl;
  auto status=report["status"].toString();
  // BGFX unloads Vulkan while Qt may retain Xlib close-display callbacks.
  // Avoid QApplication teardown after all owned viewports have left their scopes.
  std::_Exit(status=="PASS"?0:status=="SKIP"?77:status=="UNSUPPORTED"?78:1);
}
