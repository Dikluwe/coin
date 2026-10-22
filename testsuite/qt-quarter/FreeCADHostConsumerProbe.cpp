// Test observer only. Never call Python from a dragger's Qt/Coin event callback.
#include <Inventor/draggers/SoDragger.h>
#include <Inventor/nodes/SoNode.h>
#include <Inventor/nodes/SoCamera.h>
#include <Inventor/SoEventManager.h>
#include <Inventor/actions/SoHandleEventAction.h>
#include <Inventor/actions/SoSearchAction.h>
#include <Inventor/SoPath.h>
#include <Inventor/nodes/SoCallback.h>
#include <Gui/SoFCColorBar.h>
#include <Gui/View3DInventorViewer.h>
#include <Gui/NaviCube.h>
#include <Gui/Inventor/SoNaviCube.h>
#include <Quarter/QuarterWidget.h>
#include <QApplication>
#include <QGraphicsView>
#include <QMainWindow>
#include <QDockWidget>
#include <QMouseEvent>
#include <QPointer>
#include <QVariant>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <cstdint>
#include <set>

namespace {
struct DragProbe {
  SoDragger * dragger;
  uint64_t start=0,motion=0,finish=0;
  static void started(void * data,SoDragger *) { ++static_cast<DragProbe *>(data)->start; }
  static void moved(void * data,SoDragger *) { ++static_cast<DragProbe *>(data)->motion; }
  static void finished(void * data,SoDragger *) { ++static_cast<DragProbe *>(data)->finish; }
};
struct InputProbe final : QObject {
  QPointer<QGraphicsView> viewer;
  uint64_t counts[4][3]{};
  double motion[5]{};
  double input[3][7]{};
  uintptr_t objects[3]{};
  bool suppressTooltips;
  InputProbe(QGraphicsView * value,bool suppress)
    : QObject(qApp),viewer(value),suppressTooltips(suppress) {}
  bool eventFilter(QObject * watched,QEvent * event) override {
    QWidget * viewport=viewer?viewer->viewport():nullptr;
    auto * widget=qobject_cast<QWidget *>(watched);
    const bool accepted=viewport && widget && viewer->isAncestorOf(viewport) &&
      (widget==viewport || viewport->isAncestorOf(widget));
    const int phase=event->type()==QEvent::MouseMove?0:
      event->type()==QEvent::MouseButtonPress?1:event->type()==QEvent::MouseButtonRelease?2:-1;
    if(phase>=0) {
      ++counts[2][phase];
      objects[0]=reinterpret_cast<uintptr_t>(watched);
      objects[1]=reinterpret_cast<uintptr_t>(viewer.data());
      objects[2]=reinterpret_cast<uintptr_t>(viewport);
      if(accepted) {
        ++counts[0][phase]; counts[1][phase]+=event->spontaneous();
        const auto * mouse=static_cast<QMouseEvent *>(event);
        input[phase][0]=mouse->position().x(); input[phase][1]=mouse->position().y();
        input[phase][2]=mouse->globalPosition().x(); input[phase][3]=mouse->globalPosition().y();
        input[phase][4]=int(mouse->buttons()); input[phase][5]=int(mouse->button());
        input[phase][6]=double(mouse->timestamp());
        if(phase==0) {
          motion[0]=mouse->position().x(); motion[1]=mouse->position().y();
          motion[2]=mouse->globalPosition().x(); motion[3]=mouse->globalPosition().y();
          motion[4]=int(mouse->buttons());
        }
      } else ++counts[3][phase];
    }
    return accepted && suppressTooltips && event->type()==QEvent::ToolTip;
  }
};
}
extern "C" const char * host_consumer_navicube_metadata_search(void * pointer) {
  static thread_local QByteArray snapshot;
  auto * viewer=qobject_cast<Gui::View3DInventorViewer *>(static_cast<QWidget *>(pointer));
  auto * cube=viewer?viewer->getNaviCube():nullptr;
  auto * root=cube?cube->getCoinNode():nullptr;
  auto * camera=viewer?viewer->getSoRenderManager()->getCamera():nullptr;
  QJsonObject state{{"viewer_present",viewer!=nullptr},{"overlay_root_present",root!=nullptr}};
  if(root && camera) {
    const auto rootId=root->getNodeId(),cameraId=camera->getNodeId();
    SoSearchAction search;
    search.setType(SoNode::getClassTypeId(),TRUE);
    search.setInterest(SoSearchAction::ALL); search.setSearchingAll(TRUE);
    search.apply(root);
    const auto & paths=search.getPaths();
    int cubes=0,callbacks=0;
    for(int i=0;i<paths.getLength();++i) {
      auto * node=paths[i]->getTail();
      cubes+=node->isOfType(Gui::SoNaviCube::getClassTypeId());
      callbacks+=node->isOfType(SoCallback::getClassTypeId());
    }
    state["overlay_root_name"]=QString::fromUtf8(root->getName().getString());
    state["paths"]=paths.getLength(); state["navicubes"]=cubes; state["callbacks"]=callbacks;
    state["root_id_before"]=QString::number(rootId);
    state["root_id_after"]=QString::number(root->getNodeId());
    state["camera_id_before"]=QString::number(cameraId);
    state["camera_id_after"]=QString::number(camera->getNodeId());
    state["state_unchanged"]=rootId==root->getNodeId() && cameraId==camera->getNodeId();
  }
  snapshot=QJsonDocument(state).toJson(QJsonDocument::Compact);
  return snapshot.constData();
}
extern "C" const char * host_consumer_prepare_edit_ui(void * pointer) {
  static thread_local QByteArray snapshot;
  auto * main=qobject_cast<QMainWindow *>(static_cast<QWidget *>(pointer));
  QJsonArray authored;
  if(main) {
    // Qt owns and can replace these objects while a task dialog changes.
    // QPointer keeps a mutation of one dock from leaving dangling list entries.
    QList<QPointer<QDockWidget>> docks;
    for(auto * dock : main->findChildren<QDockWidget *>()) docks.append(dock);
    for(auto dock : docks) {
      if(!dock) continue;
      const QString name=dock->objectName();
      dock->setFloating(true);
      if(dock) dock->hide();
      authored.append(QJsonObject{{"name",name},{"alive",!dock.isNull()},
        {"floating",dock?dock->isFloating():false},{"visible",dock?dock->isVisible():false}});
    }
  }
  snapshot=QJsonDocument(QJsonObject{{"main_window_present",main!=nullptr},
    {"dock_count",int(authored.size())},{"docks",authored}}).toJson(QJsonDocument::Compact);
  return snapshot.constData();
}
extern "C" void * host_consumer_input_probe_create(void * pointer,int suppressTooltips) {
  auto * viewer=qobject_cast<QGraphicsView *>(static_cast<QWidget *>(pointer));
  if(!qApp || !viewer) return nullptr;
  auto * probe=new InputProbe(viewer,suppressTooltips!=0);
  qApp->installEventFilter(probe);
  return probe;
}
extern "C" uint64_t host_consumer_input_probe_count(void * pointer,int phase,int kind) {
  const auto * probe=static_cast<InputProbe *>(pointer);
  return probe && phase>=0 && phase<3 && kind>=0 && kind<4?probe->counts[kind][phase]:0;
}
extern "C" double host_consumer_input_probe_motion(void * pointer,int component) {
  const auto * probe=static_cast<InputProbe *>(pointer);
  return probe && component>=0 && component<5?probe->motion[component]:0;
}
extern "C" double host_consumer_input_probe_event(void * pointer,int phase,int component) {
  const auto * probe=static_cast<InputProbe *>(pointer);
  return probe && phase>=0 && phase<3 && component>=0 && component<7?probe->input[phase][component]:0;
}
extern "C" uintptr_t host_consumer_input_probe_object(void * pointer,int component) {
  const auto * probe=static_cast<InputProbe *>(pointer);
  return probe && component>=0 && component<3?probe->objects[component]:0;
}
extern "C" const char * host_consumer_input_probe_publication(void * pointer) {
  static thread_local QByteArray snapshot;
  const auto * probe=static_cast<InputProbe *>(pointer);
  auto * viewer=probe?probe->viewer.data():nullptr;
  QJsonObject state{{"viewer_alive",viewer!=nullptr}};
  if(viewer) {
    state["counter_properties_present"]=viewer->property("wgpuFrameCount").isValid() &&
      viewer->property("wgpuFrameSerial").isValid() && viewer->property("wgpuFrameStatus").isValid();
    state["frames"]=QString::number(viewer->property("wgpuFrameCount").toULongLong());
    state["serial"]=QString::number(viewer->property("wgpuFrameSerial").toULongLong());
    state["status"]=viewer->property("wgpuFrameStatus").toInt();
    state["fallback_reason"]=viewer->property("wgpuFallbackReason").toString();
    state["opengl_viewport"]=viewer->property("wgpuOpenGLViewport").toBool();
  }
  snapshot=QJsonDocument(state).toJson(QJsonDocument::Compact);
  return snapshot.constData();
}
extern "C" const char * host_consumer_input_probe_coin_state(void * pointer) {
  static thread_local QByteArray snapshot;
  const auto * probe=static_cast<InputProbe *>(pointer);
  using QuarterWidget=SIM::Coin3D::Quarter::QuarterWidget;
  auto * viewer=probe?qobject_cast<QuarterWidget *>(probe->viewer.data()):nullptr;
  auto * manager=viewer?viewer->getSoEventManager():nullptr;
  auto * action=manager?manager->getHandleEventAction():nullptr;
  auto * grabber=action?action->getGrabber():nullptr;
  QJsonObject state{{"viewer_alive",viewer!=nullptr},{"manager_present",manager!=nullptr},
    {"action_present",action!=nullptr},{"grabber_present",grabber!=nullptr},
    {"manager_pointer",QString::number(reinterpret_cast<quintptr>(manager))},
    {"action_pointer",QString::number(reinterpret_cast<quintptr>(action))},
    {"grabber_pointer",QString::number(reinterpret_cast<quintptr>(grabber))}};
  if(grabber) {
    state["grabber_type"]=QString::fromUtf8(grabber->getTypeId().getName().getString());
    state["grabber_name"]=QString::fromUtf8(grabber->getName().getString());
    const bool dragger=grabber->isOfType(SoDragger::getClassTypeId());
    state["grabber_is_dragger"]=dragger;
    if(dragger) state["dragger_active"]=bool(static_cast<SoDragger *>(grabber)->isActive.getValue());
  }
  QJsonArray draggers;
  int activeDraggers=0;
  if(manager && manager->getSceneGraph()) {
    SoSearchAction search;
    search.setType(SoDragger::getClassTypeId(),TRUE);
    search.setInterest(SoSearchAction::ALL); search.setSearchingAll(TRUE);
    search.apply(manager->getSceneGraph());
    const auto & paths=search.getPaths();
    std::set<SoNode *> seen;
    for(int i=0;i<paths.getLength();++i) {
      auto * node=paths[i]->getTail();
      if(!seen.insert(node).second) continue;
      auto * dragger=static_cast<SoDragger *>(node);
      const bool active=dragger->isActive.getValue();
      activeDraggers+=active;
      draggers.append(QJsonObject{
        {"type",QString::fromUtf8(node->getTypeId().getName().getString())},
        {"name",QString::fromUtf8(node->getName().getString())},
        {"pointer",QString::number(reinterpret_cast<quintptr>(node))},
        {"active",active},{"is_grabber",node==grabber}});
    }
  }
  state["scene_dragger_count"]=draggers.size();
  state["scene_active_dragger_count"]=activeDraggers;
  state["scene_draggers"]=draggers;
  snapshot=QJsonDocument(state).toJson(QJsonDocument::Compact);
  return snapshot.constData();
}
extern "C" void host_consumer_input_probe_destroy(void * pointer) {
  auto * probe=static_cast<InputProbe *>(pointer);
  if(probe) { qApp->removeEventFilter(probe); delete probe; }
}
extern "C" void * host_consumer_drag_probe_create(void * pointer) {
  auto * node=static_cast<SoNode *>(pointer);
  if(!node || !node->isOfType(SoDragger::getClassTypeId())) return nullptr;
  auto * dragger=static_cast<SoDragger *>(node);
  auto * probe=new DragProbe{dragger};
  dragger->ref();
  dragger->addStartCallback(DragProbe::started,probe);
  dragger->addMotionCallback(DragProbe::moved,probe);
  dragger->addFinishCallback(DragProbe::finished,probe);
  return probe;
}
extern "C" uint64_t host_consumer_drag_probe_count(void * pointer,int phase) {
  const auto * probe=static_cast<DragProbe *>(pointer);
  if(!probe) return 0;
  return phase==0?probe->start:phase==1?probe->motion:phase==2?probe->finish:0;
}
extern "C" void host_consumer_drag_probe_destroy(void * pointer) {
  auto * probe=static_cast<DragProbe *>(pointer);
  if(!probe) return;
  auto * dragger=probe->dragger;
  dragger->removeStartCallback(DragProbe::started,probe);
  dragger->removeMotionCallback(DragProbe::moved,probe);
  dragger->removeFinishCallback(DragProbe::finished,probe);
  dragger->unref();
  delete probe;
}
// Invoke the real host's public range API; never edit derived label strings.
extern "C" int host_consumer_colorbar_set_range(void * pointer,float minimum,float maximum,int precision) {
  auto * node=static_cast<SoNode *>(pointer);
  if(!node || !node->isOfType(Gui::SoFCColorBar::getClassTypeId())) return 0;
  static_cast<Gui::SoFCColorBar *>(node)->setRange(minimum,maximum,precision);
  return 1;
}
