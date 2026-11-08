#ifndef COIN_SOCAMERA_P_H
#define COIN_SOCAMERA_P_H

#include <Inventor/nodes/SoCamera.h>
#include <memory>
#include <vector>

// Opt-in publication for non-field stereo state. It deliberately does not
// touch the authored graph or enqueue conventional viewer redraw sensors.
// Delivery and registration obey the camera's existing producer-thread rule.
class SoCameraStereoObserver {
public:
  virtual ~SoCameraStereoObserver() {}
  virtual bool isActive() const = 0;
  virtual void stereoChanged() = 0;
};
class COIN_DLL_API SoCameraP {
public:
  static void observeStereo(SoCamera *, const std::shared_ptr<SoCameraStereoObserver> &);
  void publishStereo();
private:
  std::vector<std::weak_ptr<SoCameraStereoObserver>> observers;
};
#endif
