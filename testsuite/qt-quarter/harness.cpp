// Tests the production QuarterWidget/SoNaviCube from the supplied FreeCAD build.
// No presenter, request queue, traversal or backend implementation is duplicated.
#include <FCConfig.h>
#include <Base/Type.h>
#include <Quarter/Quarter.h>
#include <Quarter/QuarterWidget.h>
#include <Inventor/SoDB.h>
#include <Inventor/SoOffscreenRenderer.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/actions/SoCallbackAction.h>
#include <Inventor/nodes/SoSubNode.h>
#include <Inventor/fields/SoSFBool.h>
#include <Inventor/misc/SoContextHandler.h>
#include <Inventor/rendering/CoinRenderCapabilities.h>
#include <Inventor/rendering/CoinRenderSceneManager.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoPerspectiveCamera.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoTransform.h>
#include <Inventor/nodes/SoAnnotation.h>
#include <Inventor/nodes/SoDepthBuffer.h>
#include <Inventor/nodes/SoPolygonOffset.h>
#include <Inventor/SoType.h>
#include <Inventor/SoPath.h>
#include <Inventor/nodes/SoShape.h>
#include <Gui/Inventor/SoNaviCube.h>
#include <Gui/SoFCColorBar.h>
#include <Gui/SoLabelNodes.h>
#include <Mod/Part/Gui/SoFCShapeObject.h>
#include <Mod/Mesh/Gui/SoFCMeshObject.h>
#include <Mod/Mesh/Gui/SoPolygon.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoComplexity.h>
#include <Inventor/nodes/SoSceneTexture2.h>
#include <Inventor/nodes/SoTexture2.h>
#include <Inventor/nodes/SoTextureCoordinate2.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
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
#include <QOpenGLWidget>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <limits>

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
  void dprNotify(qreal ratio) { Q_EMIT devicePixelRatioChanged(ratio); }
};
class NativeAdmissionProbe final : public SoNode {
  SO_NODE_HEADER(NativeAdmissionProbe);
public:
  static void initClass() { SO_NODE_INIT_CLASS(NativeAdmissionProbe,SoNode,"Node"); }
  NativeAdmissionProbe() {
    SO_NODE_CONSTRUCTOR(NativeAdmissionProbe);
    SO_NODE_ADD_FIELD(reject,(TRUE));
  }
  SoSFBool reject;
  unsigned callbacks=0, glCalls=0;
  void callback(SoCallbackAction * action) override {
    ++callbacks;
    if(reject.getValue() && action->isOfType(CoinRenderAction::getClassTypeId()))
      static_cast<CoinRenderAction *>(action)->rejectUnsupported("R20_NATIVE_ADMISSION_PROBE");
  }
  void GLRender(SoGLRenderAction *) override { ++glCalls; }
protected:
  ~NativeAdmissionProbe() override = default;
};
SO_NODE_SOURCE(NativeAdmissionProbe);
template<class Action> bool explicitForegroundAvailable() {
  return requires(Action & action) { action.beginForegroundPass(TRUE); };
}
template<class Action> bool beginExplicitForeground(Action * action) {
  if constexpr(requires(Action & value) { value.beginForegroundPass(TRUE); }) {
    action->beginForegroundPass(TRUE);
    return true;
  }
  return false;
}
class PublicForegroundBoundary final : public SoNode {
  SO_NODE_HEADER(PublicForegroundBoundary);
public:
  static void initClass() { SO_NODE_INIT_CLASS(PublicForegroundBoundary,SoNode,"Node"); }
  PublicForegroundBoundary() {
    SO_NODE_CONSTRUCTOR(PublicForegroundBoundary);
    SO_NODE_ADD_FIELD(opening,(TRUE)); SO_NODE_ADD_FIELD(clear,(FALSE));
  }
  SoSFBool opening,clear;
  void callback(SoCallbackAction * action) override {
    if(!action->isOfType(CoinRenderAction::getClassTypeId())) return;
    auto * render=static_cast<CoinRenderAction *>(action);
    if(!opening.getValue()) render->endForegroundPass();
    else if(!clear.getValue()) render->beginForegroundPass();
    else if(!beginExplicitForeground(render)) render->rejectUnsupported("explicit foreground clear API unavailable");
  }
protected:
  ~PublicForegroundBoundary() override = default;
};
SO_NODE_SOURCE(PublicForegroundBoundary);
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
void rttWindow() {
  require(qEnvironmentVariableIntValue("COIN_RENDER_RTT_GPU_DIRECT")==1,"window RTT test requires GPU-direct mode");
  QMainWindow window; auto * view=new View; window.setCentralWidget(view);
  SoMaterial * consumerMaterial=nullptr, *producerMaterial=nullptr;
  auto * root=scene(nullptr,&consumerMaterial); root->ref();
  consumerMaterial->diffuseColor=SbColor(1,1,1);
  auto * texture=new SoSceneTexture2;
  texture->scene=scene(nullptr,&producerMaterial); texture->size=SbVec2s(64,32);
  texture->model=SoSceneTexture2::REPLACE;
  texture->backgroundColor=SbVec4f(.05f,.1f,.2f,1);
  auto * quality=new SoComplexity; quality->textureQuality=.3f;
  root->insertChild(quality,4); root->insertChild(texture,5);
  view->setSceneGraph(root); window.resize(480,360); window.show(); rendered(view,"rtt-first");
  const auto before=capture(view->surface(),"rtt-before");
  producerMaterial->diffuseColor=SbColor(0,1,0);
  spin(350); const auto changed=capture(view->surface(),"rtt-green");
  require(difference(before,changed)>.5,"producer mutation did not reach window pixels");
  const auto count=view->frameCount(); texture->size=SbVec2s(128,64); window.resize(530,390);
  require(until([&]{return view->frameCount()>count;}),"RTT/window resize did not submit");
  spin(250); capture(view->surface(),"rtt-resized");
  // An invalid producer must not silently stage or publish a partially updated window.
  const auto valid=capture(view->surface(),"rtt-valid");
  const auto beforeReject=view->frameCount();
  const auto serialBeforeReject=view->property("wgpuFrameSerial").toULongLong();
  require(serialBeforeReject>0,"valid RTT publication has no submission serial");
  texture->size=SbVec2s(0,32);
  require(until([&]{return view->property("wgpuFrameStatus").toInt()==CoinRenderAction::INVALID_SCENE;}),"invalid RTT size was not rejected");
  require(view->frameCount()==beforeReject,"rejected candidate counted as a publication"); spin(250);
  require(view->frameCount()==beforeReject,"rejected RTT later changed the published frame count");
  require(view->property("wgpuFrameSerial").toULongLong()==serialBeforeReject,"rejected RTT changed the published submission serial");
  require(view->surface()!=nullptr && view->property("wgpuFallbackReason").toString().isEmpty(),"RTT admission caused legacy fallback");
  const auto rejected=capture(view->surface(),"rtt-rejected");
  require(valid==rejected,"RTT rejection changed actual window publication");
  texture->size=SbVec2s(128,64); producerMaterial->diffuseColor=SbColor(1,.05f,.02f);
  errors.clear(); rendered(view,"rtt-recovered");
  require(view->frameCount()>beforeReject && view->property("wgpuFrameSerial").toULongLong()>serialBeforeReject,
          "valid RTT recovery did not publish a fresh frame and serial");
  root->unref(); report["direct_rtt_window"]=true;
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
void nativeAdmission() {
  NativeAdmissionProbe::initClass();
  QMainWindow window;
  auto * view=new View;
  SoTransform * transform=nullptr;
  auto * root=scene(&transform);
  auto * probe=new NativeAdmissionProbe;
  root->insertChild(probe,0);
  view->setSceneGraph(root);
  window.setCentralWidget(view); window.resize(480,360); window.show();
  auto serial=[&]() -> qulonglong {
    const auto value=view->property("wgpuFrameSerial");
    require(value.isValid(),"Quarter did not expose its actual target publication serial");
    return value.toULongLong();
  };
  auto rejected=[&] {
    const auto status=view->property("wgpuFrameStatus");
    return status.isValid() && status.toInt()==int(CoinRenderAction::UNSUPPORTED);
  };
  auto nativeOnly=[&] {
    require(view->property("wgpuFallbackReason").toString().isEmpty(),"admission rejection switched Quarter to GL");
    require(qobject_cast<QOpenGLWidget *>(view->viewport())==nullptr,"admission rejection installed a GL viewport");
    require(probe->glCalls==0,"Quarter executed GLRender during native rejection/recovery");
    require(errors.empty(),"Quarter emitted a native rendering failure during admission recovery");
  };
  require(until([&]{return probe->callbacks>0 && rejected();}),"cold native scene did not report UNSUPPORTED");
  nativeOnly();
  require(view->frameCount()==0 && serial()==0,"cold rejection published a frame");
  require(view->property("wgpuFrameError").toString().contains("R20_NATIVE_ADMISSION_PROBE"),
          "cold rejection lost the author's unsupported reason");
  QJsonObject cold{{"status","UNSUPPORTED"},{"frames",qint64(view->frameCount())},
    {"serial",qint64(serial())},{"gl_calls",int(probe->glCalls)},{"callbacks",int(probe->callbacks)}};
  // Only authored graph edits request subsequent frames through normal sensors.
  // Do not call requestWgpuFrame/renderWgpuFrameNow or post an explicit update.
  probe->reject=FALSE;
  rendered(view,"admission-first-valid"); nativeOnly();
  require(view->property("wgpuFrameStatus").toInt()==int(CoinRenderAction::SUCCESS),"cold rejection did not recover natively");
  require(serial()>0,"native recovery did not publish a target serial");
  const QImage previous=capture(view->surface(),"admission-previous");
  const auto previousFrames=view->frameCount(),previousSerial=serial();
  QJsonObject first{{"status","SUCCESS"},{"frames",qint64(previousFrames)},{"serial",qint64(previousSerial)}};
  const unsigned callbacksBefore=probe->callbacks;
  probe->reject=TRUE;
  transform->translation=SbVec3f(0.7f,0,0);
  require(until([&]{return probe->callbacks>callbacksBefore && rejected();}),"late native scene did not report UNSUPPORTED");
  nativeOnly();
  require(view->frameCount()==previousFrames && serial()==previousSerial,"late rejection changed publication count/serial");
  const QImage retained=capture(view->surface(),"admission-retained");
  require(previous==retained,"late rejection changed the visible previous publication");
  QJsonObject late{{"status","UNSUPPORTED"},{"frames",qint64(view->frameCount())},
    {"serial",qint64(serial())},{"gl_calls",int(probe->glCalls)},{"unchanged_pixels",true}};
  probe->reject=FALSE;
  require(until([&]{return view->frameCount()>previousFrames;}),"authored correction did not schedule a new native frame");
  rendered(view,"admission-recovered"); nativeOnly();
  require(serial()>previousSerial,"late recovery did not advance target serial");
  require(view->property("wgpuFrameStatus").toInt()==int(CoinRenderAction::SUCCESS),"late recovery retained UNSUPPORTED status");
  require(view->property("wgpuFrameError").toString().isEmpty(),"successful recovery retained a stale rejection reason");
  const QImage recovered=capture(view->surface(),"admission-recovered-visible");
  require(previous.size()==recovered.size(),"admission recovery changed viewport dimensions");
  int changed=0;
  for(int y=0;y<previous.height();++y) for(int x=0;x<previous.width();++x)
    changed+=previous.pixel(x,y)!=recovered.pixel(x,y);
  require(changed>=100,"native recovery reused the stale image instead of the authored edit");
  QJsonObject recovery{{"status","SUCCESS"},{"frames",qint64(view->frameCount())},
    {"serial",qint64(serial())},{"gl_calls",int(probe->glCalls)},{"changed_pixels",changed}};
  const auto idleFrames=view->frameCount(),idleSerial=serial();
  const unsigned idleCallbacks=probe->callbacks;
  spin(450); nativeOnly();
  require(view->frameCount()==idleFrames && serial()==idleSerial && probe->callbacks==idleCallbacks,
          "native admission recovery continuously retried or redrew while idle");
  report["native_admission"]=QJsonObject{{"cold",cold},{"first",first},{"late",late},
    {"recovered",recovery},{"idle_frames",0}};
}
struct NaviCubePrune {
  SoAction * action;
  SoNode * cube;
  int leaves=0;
  bool skip() {
    const auto * path=action->getCurPath();
    if(path && path->containsNode(cube) && path->getTail()!=cube &&
       path->getTail()->isOfType(SoShape::getClassTypeId())) {
      ++leaves;
      return true;
    }
    return false;
  }
};
QImage readWgpu(SoNode * root,SoNode * emptyCube=nullptr,int * prunedLeaves=nullptr) {
  if(emptyCube) {
    std::unique_ptr<CoinRenderTarget> target(CoinRenderTarget::createOffscreen(SbVec2i32(256,256)));
    require(target!=nullptr,"could not create target for pruned NaviCube");
    target->setDepthReadbackEnabled(FALSE);
    CoinRenderAction action(SbViewportRegion(256,256));
    action.setRenderTarget(target.get()); action.setBackgroundColor(SbColor4f(0.12f,0.16f,0.2f,1));
    NaviCubePrune prune{&action,emptyCube};
    action.setAbortCallback([](void * data) {
      return static_cast<NaviCubePrune *>(data)->skip()?CoinRenderAction::PRUNE:CoinRenderAction::CONTINUE;
    },&prune);
    action.apply(root);
    require(action.getLastStatus()==CoinRenderAction::SUCCESS,action.getLastError().getString());
    if(prunedLeaves) *prunedLeaves=prune.leaves;
    std::vector<uint8_t> pixels; target->readbackRGBA(pixels);
    require(pixels.size()==256*256*4,"pruned NaviCube frame did not publish RGBA");
    return QImage(pixels.data(),256,256,256*4,QImage::Format_RGBA8888).copy();
  }
  CoinRenderSceneManager manager(SbVec2i32(256,256));
  manager.getRenderTarget()->setDepthReadbackEnabled(FALSE);
  manager.setBackgroundColor(SbColor4f(0.12f,0.16f,0.2f,1));
  manager.setSceneGraph(root);
  auto status=manager.render();
  require(status==CoinRenderAction::SUCCESS, manager.getLastError().getString());
  std::vector<uint8_t> pixels; manager.getRenderTarget()->readbackRGBA(pixels);
  require(pixels.size()==256*256*4,"successful GPU frame did not publish RGBA");
  return QImage(pixels.data(),256,256,256*4,QImage::Format_RGBA8888).copy();
}
QImage readGl(SoNode * root,SoNode * emptyCube=nullptr,int * prunedLeaves=nullptr) {
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
    NaviCubePrune prune{&action,emptyCube};
    if(emptyCube) action.setAbortCallback([](void * data) {
      return static_cast<NaviCubePrune *>(data)->skip()?SoGLRenderAction::PRUNE:SoGLRenderAction::CONTINUE;
    },&prune);
    action.apply(root); gl->glFinish();
    if(prunedLeaves) *prunedLeaves=prune.leaves;
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
void foregroundScope() {
  const bool available=explicitForegroundAvailable<CoinRenderAction>();
  report["explicit_foreground_clear_available"]=available;
  require(available,"baseline lacks required beginForegroundPass(TRUE) capability");
  PublicForegroundBoundary::initClass();
  auto base=[] {
    auto * root=new SoSeparator;
    auto * camera=new SoOrthographicCamera; camera->position=SbVec3f(0,0,5);
    camera->height=4; camera->nearDistance=1; camera->farDistance=10; root->addChild(camera);
    auto * lighting=new SoLightModel; lighting->model=SoLightModel::BASE_COLOR; root->addChild(lighting);
    return root;
  };
  auto boundary=[](bool opening,bool clear=false) {
    auto * node=new PublicForegroundBoundary; node->opening=opening; node->clear=clear; return node;
  };
  auto geometry=[](const SbColor & color,bool zeroDepth,const SbVec3f & position,float size) {
    auto * group=new SoSeparator;
    auto * depth=new SoDepthBuffer;
    if(zeroDepth) { depth->function=SoDepthBuffer::ALWAYS; depth->range.setValue(0,0); }
    group->addChild(depth);
    auto * transform=new SoTransform; transform->translation=position; group->addChild(transform);
    auto * material=new SoMaterial; material->diffuseColor=color; group->addChild(material);
    auto * shape=new SoCube; shape->width=size; shape->height=size; shape->depth=.1f;
    group->addChild(shape); return group;
  };
  auto * control=base(); control->ref();
  auto * nearRed=geometry(SbColor(1,0,0),true,SbVec3f(0,0,0),10);
  auto * farGreen=geometry(SbColor(0,1,0),false,SbVec3f(0,0,-1),1);
  auto * blue=geometry(SbColor(0,0,1),true,SbVec3f(-1.25f,1.25f,0),.5f);
  control->addChild(nearRed); control->addChild(farGreen); control->addChild(blue);
  const QImage controlImage=readWgpu(control);
  const QColor controlCenter=controlImage.pixelColor(128,128);
  require(controlCenter.red()>180 && controlCenter.green()<100,
          "foreground scope control did not discriminate preserved scene depth");
  auto * nested=base(); nested->ref();
  nested->addChild(boundary(true,true));
  nested->addChild(boundary(true));
  nested->addChild(nearRed);
  nested->addChild(boundary(false));
  // The child used ordinary preserved depth. Its end must restore the parent's
  // still-pending explicit clear, rather than the base foreground scope.
  nested->addChild(farGreen);
  nested->addChild(boundary(false));
  const QImage nestedImage=readWgpu(nested);
  nestedImage.save(artifacts+"/foreground-explicit-nested.png");
  const QColor nestedCenter=nestedImage.pixelColor(128,128);
  require(nestedCenter.green()>180 && nestedCenter.red()<100,
          "ending a nested foreground scope lost the parent's explicit depth clear");
  // There are literally no drawable nodes between this begin/end pair. The
  // next ordinary green cube must remain behind red depth zero, as in control.
  control->insertChild(boundary(true,true),3);
  control->insertChild(boundary(false),4);
  const QImage emptyImage=readWgpu(control);
  controlImage.save(artifacts+"/foreground-empty-control.png");
  emptyImage.save(artifacts+"/foreground-empty-actual.png");
  require(emptyImage==controlImage,"empty explicit foreground scope leaked its depth clear into the next external draw");
  // A following foreground that requests preservation must also inherit the
  // original scene depth, rather than a clear abandoned by the empty scope.
  auto * preserved=base(); preserved->ref();
  preserved->addChild(nearRed);
  preserved->addChild(boundary(true));
  preserved->addChild(farGreen);
  preserved->addChild(boundary(false));
  preserved->addChild(blue);
  const QImage preservedControl=readWgpu(preserved);
  const QColor preservedCenter=preservedControl.pixelColor(128,128);
  require(preservedCenter.red()>180 && preservedCenter.green()<100,
          "preserving foreground control did not discriminate existing scene depth");
  preserved->insertChild(boundary(true,true),3);
  preserved->insertChild(boundary(false),4);
  const QImage preservedActual=readWgpu(preserved);
  preservedControl.save(artifacts+"/foreground-empty-preserved-control.png");
  preservedActual.save(artifacts+"/foreground-empty-preserved-actual.png");
  require(preservedActual==preservedControl,
          "empty explicit foreground scope leaked its depth clear into a later preserving foreground");
  report["foreground_scope"]=QJsonObject{{"parent_clear_restored",true},
    {"empty_scope_preserved_pixels",true},{"empty_scope_preserved_later_foreground",true},
    {"depth_discriminating_control",true}};
  preserved->unref(); nested->unref(); control->unref();
}
void perspectivePolygonBias() {
  QJsonArray observations;
  // All author coordinates are finite. A triangle crossing a clip plane is a
  // valid scene, including a vertex on or behind the eye plane (clip w<=0).
  for(float apexZ : {4.5f,5.0f,6.0f}) {
    auto * root=new SoSeparator; root->ref();
    auto * camera=new SoPerspectiveCamera;
    camera->position=SbVec3f(0,0,5); camera->nearDistance=1; camera->farDistance=20;
    camera->heightAngle=.9f; root->addChild(camera);
    auto * lighting=new SoLightModel; lighting->model=SoLightModel::BASE_COLOR;
    root->addChild(lighting);
    auto * transform=new SoTransform;
    transform->translation=SbVec3f(.2f,-.1f,0);
    transform->rotation.setValue(SbVec3f(0,0,1),.54f); root->addChild(transform);
    auto * material=new SoMaterial; material->diffuseColor=SbColor(1,.05f,.02f);
    root->addChild(material);
    auto * offset=new SoPolygonOffset; offset->on=TRUE; offset->factor=1; offset->units=1;
    root->addChild(offset);
    auto * coordinates=new SoCoordinate3;
    const SbVec3f points[]={SbVec3f(-2,-2,0),SbVec3f(2,-2,0),SbVec3f(0,1,apexZ)};
    coordinates->point.setValues(0,3,points); root->addChild(coordinates);
    auto * faces=new SoIndexedFaceSet;
    const int32_t indices[]={0,1,2,-1}; faces->coordIndex.setValues(0,4,indices);
    root->addChild(faces);
    const QImage native=readWgpu(root),reference=readGl(root);
    int nativeContent=0,referenceContent=0;
    for(int y=0;y<256;++y) for(int x=0;x<256;++x) {
      const QColor a=native.pixelColor(x,y),b=reference.pixelColor(x,y);
      nativeContent+=a.red()>180 && a.green()<100 && a.blue()<100;
      referenceContent+=b.red()>180 && b.green()<100 && b.blue()<100;
    }
    const double mae=difference(reference,native);
    native.save(artifacts+QString("/bias-perspective-%1-actual.png").arg(apexZ));
    reference.save(artifacts+QString("/bias-perspective-%1-reference.png").arg(apexZ));
    require(nativeContent>=100 && referenceContent>=100,"finite clipped perspective bias lost triangle content");
    require(mae<3,"transformed perspective polygon bias differs from the clipped CoinGL oracle");
    QJsonObject observation{{"apex_z",apexZ},{"rgb_mae",mae},
      {"native_pixels",nativeContent},{"reference_pixels",referenceContent}};
    if(apexZ==4.5f) {
      CoinRenderSceneManager manager(SbVec2i32(256,256));
      manager.getRenderTarget()->setDepthReadbackEnabled(FALSE);
      manager.setSceneGraph(root);
      require(manager.render()==CoinRenderAction::SUCCESS,manager.getLastError().getString());
      QJsonArray refusals;
      for(int invalid=0;invalid<4;++invalid) {
        const uint64_t serial=manager.getRenderTarget()->getLastSubmissionSerial();
        std::vector<uint8_t> before,after;
        manager.getRenderTarget()->readbackRGBA(before);
        require(serial>0 && before.size()==256*256*4,"perspective bias control has no publication");
        const float nan=std::numeric_limits<float>::quiet_NaN();
        const float infinity=std::numeric_limits<float>::infinity();
        if(invalid==0) offset->factor=nan;
        else if(invalid==1) offset->units=infinity;
        else if(invalid==2) coordinates->point.set1Value(0,SbVec3f(nan,-2,0));
        else coordinates->point.set1Value(2,SbVec3f(0,1,-infinity));
        const auto status=manager.render();
        require(status==CoinRenderAction::INVALID_SCENE,"non-finite polygon bias input was not rejected as INVALID_SCENE");
        require(manager.getRenderTarget()->getLastSubmissionSerial()==serial,"non-finite bias input published a serial");
        manager.getRenderTarget()->readbackRGBA(after);
        require(after==before,"non-finite bias input changed the previous publication");
        offset->factor=1; offset->units=1; coordinates->point.setValues(0,3,points);
        require(manager.render()==CoinRenderAction::SUCCESS,manager.getLastError().getString());
        require(manager.getRenderTarget()->getLastSubmissionSerial()>serial,"valid bias recovery did not submit");
        refusals.append(QJsonObject{{"input",invalid},{"invalid_scene",true},
          {"preserved_serial",true},{"preserved_pixels",true},{"recovered",true}});
      }
      observation["nonfinite_refusals"]=refusals;
    }
    observations.append(observation); root->unref();
  }
  report["perspective_polygon_bias"]=observations;
}
void visual(const QString & test) {
  if(test=="foreground-scope") { foregroundScope(); return; }
  if(test=="axis-cross" || test=="rubber-band") {
    report["status"]="UNSUPPORTED"; report["expected_failure"]=true;
    report["reason"]="legacy GL viewer overlay lacks an exported WGPU scene node; requires full FreeCAD viewer probe";
    return;
  }
  if(test=="retained-rejection") {
    MeshGui::SoFCMeshGridNode::initClass(); MeshGui::SoPolygon::initClass(); PartGui::SoFCControlPoints::initClass();
    auto* root=scene(); root->ref();
    auto* coordinates=new SoCoordinate3;
    const SbVec3f points[]={{-1,-1,0},{1,-1,0},{1,1,0},{-1,1,0}};
    coordinates->point.setValues(0,4,points); root->addChild(coordinates);
    auto* grid=new MeshGui::SoFCMeshGridNode; grid->maxGrid=SbVec3f(1,1,1); grid->lenGrid=SbVec3s(2,2,2); root->addChild(grid);
    auto* polygon=new MeshGui::SoPolygon; polygon->numVertices=4; root->addChild(polygon);
    auto* control=new PartGui::SoFCControlPoints; control->numPolesU=2; control->numPolesV=2; root->addChild(control);
    CoinRenderSceneManager manager(SbVec2i32(256,256)); manager.setSceneGraph(root);
    require(manager.render()==CoinRenderAction::SUCCESS,"retained valid setup failed");
    for(int testCase=0;testCase<5;++testCase) {
      std::vector<uint8_t> previous, after; manager.getRenderTarget()->readbackRGBA(previous);
      if(testCase==0) grid->lenGrid=SbVec3s(129,2,2);
      if(testCase==1) polygon->startIndex=5;
      if(testCase==2) control->numPolesU=5;
      if(testCase==3) { control->numPolesU=65536; control->numPolesV=65536; }
      if(testCase==4) { control->numPolesU=0; control->numPolesV=65536; }
      require(manager.render()==CoinRenderAction::UNSUPPORTED,"retained invalid fields did not reject");
      manager.getRenderTarget()->readbackRGBA(after);
      require(previous==after,"retained rejection changed publication");
      grid->lenGrid=SbVec3s(2,2,2); polygon->startIndex=0; control->numPolesU=2; control->numPolesV=2;
      require(manager.render()==CoinRenderAction::SUCCESS,"retained recovery failed");
    }
    report["rejections"]=5; report["recoveries"]=5; root->unref();
  } else if(test=="colorbar") {
    Gui::SoColorBarLabel::initClass(); Gui::SoFCColorBar::initClass();
    SoOrthographicCamera* camera=nullptr; auto* root=scene(nullptr,nullptr,&camera); root->ref();
    camera->height=10; camera->nearDistance=0; camera->farDistance=10;
    auto* bar=new Gui::SoFCColorBar; root->addChild(bar);
    QJsonArray states; double worst=0;
    QImage previous;
    for(int i=0;i<3;++i) {
      if(i==1) bar->setRange(-100.0f,100.0f,2);
      if(i==2) bar->setRange(1.0f,1000.0f,1);
      // GPU callback must prepare cold state before the independent GL render.
      const QImage actual=readWgpu(root), reference=readGl(root);
      const double mae=difference(reference,actual); worst=std::max(worst,mae);
      actual.save(artifacts+QString("/colorbar-%1-actual.png").arg(i));
      reference.save(artifacts+QString("/colorbar-%1-reference.png").arg(i));
      require(mae<6.0,"ColorBar cold callback differs from GL");
      int glyphs=0;
      for(int y=0;y<actual.height();++y) for(int x=0;x<actual.width();++x) {
        const auto pixel=actual.pixelColor(x,y);
        if(pixel.red()>220 && pixel.green()>220 && pixel.blue()>220) ++glyphs;
      }
      require(glyphs>20,"ColorBar labels missing (gradient does not count)");
      if(!previous.isNull()) require(difference(previous,actual)>0.001,"ColorBar setRange did not update pixels");
      states.append(QJsonObject{{"range_case",i},{"rgb_mae",mae},{"glyph_pixels",glyphs}}); previous=actual;
    }
    report["ranges"]=states; report["rgb_mae_max"]=worst; root->unref();
  } else if(test=="navicube") {
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
      QImage label(qEnvironmentVariableIntValue("COIN_TEST_NPOT_LABELS") ? 127 : 128,
                   qEnvironmentVariableIntValue("COIN_TEST_NPOT_LABELS") ? 61 : 64,QImage::Format_RGBA8888); label.fill(Qt::transparent);
      QPainter painter(&label); painter.setPen(Qt::white);
      QFont font("DejaVu Sans",18); font.setBold(true); painter.setFont(font);
      painter.drawText(label.rect(),Qt::AlignCenter,labels[face]); painter.end();
      labelImages.push_back(label);
    }
    auto installLabels=[&] { for(int face=0;face<6;++face)
      cube->setLabelImage(ids[face],SbVec2s(labelImages[face].width(),labelImages[face].height()),4,labelImages[face].constBits()); };
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
      // The native node must prepare its own labels/geometry on its first use.
      const QImage actual=readWgpu(root), reference=readGl(root);
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
    // A cube on an empty depth buffer cannot witness overlay isolation. Fill
    // depth with zero, then draw an ordinary far sentinel after the overlay.
    // Clearing the whole target makes that sentinel visible outside the cube;
    // omitting the clear hides the cube; disabling depth leaks its rear labels.
    auto * composed=new SoSeparator; composed->ref();
    auto * camera=new SoOrthographicCamera;
    camera->position=SbVec3f(0,0,5); camera->height=4;
    camera->nearDistance=1; camera->farDistance=10;
    composed->addChild(camera);
    auto * lighting=new SoLightModel; lighting->model=SoLightModel::BASE_COLOR;
    composed->addChild(lighting);
    auto * backdrop=new SoSeparator;
    auto * zeroDepth=new SoDepthBuffer;
    zeroDepth->function=SoDepthBuffer::ALWAYS; zeroDepth->range.setValue(0,0);
    backdrop->addChild(zeroDepth);
    auto * red=new SoMaterial; red->diffuseColor=SbColor(1,0.05f,0.02f);
    backdrop->addChild(red);
    auto * plate=new SoCube; plate->width=10; plate->height=10; plate->depth=0.1f;
    backdrop->addChild(plate); composed->addChild(backdrop);
    auto marker=[](const SbVec3f & position,const SbColor & color) {
      auto * group=new SoSeparator;
      auto * translate=new SoTransform; translate->translation=position; group->addChild(translate);
      auto * material=new SoMaterial; material->diffuseColor=color; group->addChild(material);
      auto * shape=new SoCube; shape->width=0.5f; shape->height=0.5f; shape->depth=0.1f;
      group->addChild(shape); return group;
    };
    auto * hidden=marker(SbVec3f(-1.25f,-1.25f,-1),SbColor(0,1,0));
    composed->addChild(hidden);
    const QImage backdropActual=readWgpu(composed);
    const QImage backdropReference=readGl(composed);
    require(difference(backdropReference,backdropActual)<6.0,"NaviCube depth adversary baseline differs from GL");
    cube->viewportRect=SbVec4f(128,128,128,128);
    cube->cameraOrientation=orientations[6]; installLabels();
    composed->insertChild(cube,3);
    const QImage composedActual=readWgpu(composed);
    const QImage composedReference=readGl(composed);
    composedActual.save(artifacts+"/navicube-depth-composed-actual.png");
    composedReference.save(artifacts+"/navicube-depth-composed-reference.png");
    int insideChanges=0,outsideChanges=0;
    for(int y=0;y<256;++y) for(int x=0;x<256;++x) {
      const bool inOverlay=x>=128 && y<128;
      if(composedActual.pixel(x,y)!=backdropActual.pixel(x,y)) {
        if(inOverlay) ++insideChanges; else ++outsideChanges;
      }
    }
    require(insideChanges>40,"NaviCube disappeared behind previously written scene depth");
    require(outsideChanges==0,"NaviCube clear/viewport leaked outside its overlay");
    cube->clearLabelTextures();
    const QImage composedPlainActual=readWgpu(composed);
    const QImage composedPlainReference=readGl(composed);
    int expectedComposedLabels=0,actualComposedLabels=0,mismatchedComposedLabels=0;
    labelsVisible(composedReference,composedActual,composedPlainReference,composedPlainActual,
                  expectedComposedLabels,actualComposedLabels,mismatchedComposedLabels);
    require(difference(composedReference,composedActual)<6.0,"NaviCube scene-depth composition differs from GL");
    // Use the public abort traversal in both actions to prune every renderable
    // descendant, rather than assuming alpha zero means no GL draw. A visible
    // external sentinel also witnesses restoration of viewport/scissor state.
    auto * visibleOutside=marker(SbVec3f(-1.25f,1.25f,-1),SbColor(0,0,1));
    auto * always=new SoDepthBuffer; always->function=SoDepthBuffer::ALWAYS;
    visibleOutside->insertChild(always,0); composed->addChild(visibleOutside);
    composed->removeChild(cube);
    const QImage emptyOverlayControl=readWgpu(composed);
    composed->insertChild(cube,3);
    int nativePruned=0,glPruned=0;
    const QImage emptyOverlayActual=readWgpu(composed,cube,&nativePruned);
    const QImage emptyOverlayReference=readGl(composed,cube,&glPruned);
    emptyOverlayActual.save(artifacts+"/navicube-empty-overlay-actual.png");
    emptyOverlayReference.save(artifacts+"/navicube-empty-overlay-reference.png");
    int emptyOverlayChanges=0;
    for(int y=0;y<256;++y) for(int x=0;x<256;++x)
      if(!(x>=128 && y<128))
        emptyOverlayChanges+=emptyOverlayActual.pixel(x,y)!=emptyOverlayControl.pixel(x,y);
    require(nativePruned>0 && glPruned>0,"empty NaviCube did not actually prune its renderable descendants");
    require(emptyOverlayChanges==0,"empty NaviCube leaked its pending clear/viewport to an external draw");
    require(difference(emptyOverlayReference,emptyOverlayActual)<6.0,"pruned NaviCube differs from the same CoinGL traversal");
    const QColor emptyBlue=emptyOverlayActual.pixelColor(48,48),emptyHidden=emptyOverlayActual.pixelColor(48,208);
    require(emptyBlue.blue()>180 && emptyBlue.red()<100 && emptyBlue.green()<100,
            "empty NaviCube clipped or lost the following visible external sentinel");
    require(emptyHidden.red()>180 && emptyHidden.green()<100,"empty NaviCube cleared depth outside its viewport");
    composed->removeChild(visibleOutside);
    // Preserve annotation depth/viewport policy on both sides of a nested cube.
    composed->removeChild(cube);
    auto * outer=new SoAnnotation;
    auto * inner=new SoAnnotation; inner->addChild(cube); outer->addChild(inner);
    outer->addChild(marker(SbVec3f(-1.25f,-1.25f,-1),SbColor(0,1,0)));
    auto * sibling=new SoAnnotation;
    sibling->addChild(marker(SbVec3f(-1.25f,1.25f,-1),SbColor(0,0,1)));
    outer->addChild(sibling); composed->addChild(outer);
    installLabels();
    const QImage nestedActual=readWgpu(composed),nestedReference=readGl(composed);
    nestedActual.save(artifacts+"/navicube-depth-nested-actual.png");
    nestedReference.save(artifacts+"/navicube-depth-nested-reference.png");
    require(difference(nestedReference,nestedActual)<6.0,"NaviCube failed nested annotation viewport/depth restoration");
    const QColor green=nestedActual.pixelColor(48,208),blue=nestedActual.pixelColor(48,48);
    require(green.green()>180 && green.red()<100 && green.blue()<100,
            "foreground after nested NaviCube did not retain annotation depth/viewport policy");
    require(blue.blue()>180 && blue.red()<100 && blue.green()<100,
            "nested annotation sibling after NaviCube lost its viewport/depth policy");
    report["scene_depth_overlay"]=QJsonObject{{"inside_changes",insideChanges},
      {"outside_changes",outsideChanges},{"expected_label_pixels",expectedComposedLabels},
      {"actual_label_pixels",actualComposedLabels},{"mismatched_label_pixels",mismatchedComposedLabels},
      {"empty_overlay_changes",emptyOverlayChanges},{"native_pruned_leaves",nativePruned},
      {"gl_pruned_leaves",glPruned},{"nested_foreground",true}};
    composed->unref();
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
    if(test=="polygon-offset") perspectivePolygonBias();
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
    CoinRenderAction::initClass();
    CoinRenderCapabilities caps{};
    require(coin_render_query_capabilities(COIN_RENDER_EXPERIMENTAL_OFFSCREEN,&caps,sizeof(caps))==0,"GPU capability query failed");
    report["adapter"]=QString::fromUtf8(caps.adapter_name);
    report["gpu_available"]=int(caps.gpu_available);
    // Some BGFX builds intentionally report zero for a side-effect-free query.
    // Only an actual frame proves availability; never skip on this query alone.
    if(QStringList{"retained-rejection","colorbar","navicube","foreground-scope","depth","polygon-offset","annotation","foregroundroot","decorationroot","axis-cross","rubber-band"}.contains(test)) {
      visual(test);
    } else {
      if(test=="rtt-window") rttWindow();
      else if(test=="native-admission") nativeAdmission();
      else lifecycle(test);
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
