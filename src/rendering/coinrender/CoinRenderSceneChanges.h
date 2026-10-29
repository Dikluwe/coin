#ifndef COIN_RENDER_SCENE_CHANGES_H
#define COIN_RENDER_SCENE_CHANGES_H

#include <Inventor/SoPath.h>
#include <Inventor/sensors/SoNodeSensor.h>
#include <Inventor/misc/SoNotification.h>
#include "rendering/SoRetainedUpdater.h"

// Bounded notification/path collection adapted from FreeCAD's
// SoRenderManagerRootSensor at 17a9e4b. Classification belongs to its updater.
class CoinRenderSceneChanges : public SoNodeSensor {
public:
  std::vector<SoRetainedNotification> notifications;
  // Overflow and structural notifications both require a fresh capture.
  bool requiresRecapture = false;
  ~CoinRenderSceneChanges() override { detach(); reset(); }
  void reset() {
    for (const auto & notification : notifications)
      if (notification.path) const_cast<SoPath *>(notification.path)->unref();
    notifications.clear(); requiresRecapture = false;
  }
  void notify(SoNotList * list) override {
    if (requiresRecapture) return;
    if (notifications.size() >= 4096 || !list->getLastField()) { requiresRecapture = true; return; }
    SoNotRec * first = list->getFirstRecAtNode();
    auto * node = first ? static_cast<SoNode *>(first->getBase()) : nullptr;
    const SoNotRec * record = list->getLastRec();
    while (record && !record->getBase()->isOfType(SoNode::getClassTypeId()))
      record = record->getPrevious();
    if (!node || !record) { requiresRecapture = true; return; }
    SoPath * path = new SoPath(static_cast<SoNode *>(record->getBase()));
    path->ref();
    try {
      while (record && record->getBase() != node) {
        record = record->getPrevious();
        if (record) path->append(static_cast<SoNode *>(record->getBase()));
      }
      if (!record) { path->unref(); requiresRecapture = true; return; }
      for (const auto & existing : notifications)
        if (existing.node == node && existing.field == list->getLastField() && *existing.path == *path) {
          path->unref(); return;
        }
      notifications.push_back({node, list->getLastField(), path});
    } catch (...) { path->unref(); requiresRecapture = true; throw; }
    // Rendering is caller-driven; do not enqueue a second sensor callback.
  }
};

#endif
