#ifndef COIN_RENDER_GUARDED_TRAVERSAL_H
#define COIN_RENDER_GUARDED_TRAVERSAL_H

#include <Inventor/actions/SoAction.h>
#include <Inventor/elements/SoCacheElement.h>
#include <Inventor/lists/SoActionMethodList.h>
#include <Inventor/misc/SoState.h>
#include <Inventor/nodes/SoNode.h>
#include <Inventor/SoPath.h>
#include <exception>
#include <utility>

// Internal native actions must finish SoAction::apply normally: its read lock,
// applied path and root references are not exception guards. Keep the native
// runtime action type and dispatch, containing an authored exception at its
// node so ancestor state/cache scopes can unwind through their normal paths.
// Rethrow only after the complete native apply has released its bookkeeping.
template <typename Action>
class CoinRenderGuardedTraversal : public Action {
public:
  template <typename... Args>
  explicit CoinRenderGuardedTraversal(Args &&... args)
    : Action(std::forward<Args>(args)...), nativeMethods(this->traversalMethods),
      guardedMethods(nullptr)
  {
    this->nativeMethods->setUp();
    this->guardedMethods.addMethod(SoNode::getClassTypeId(), dispatch);
    this->traversalMethods = &this->guardedMethods;
  }
  ~CoinRenderGuardedTraversal() override { this->traversalMethods = this->nativeMethods; }

  using Action::apply;
  void apply(SoNode * root) override {
    this->failure = nullptr;
    Action::apply(root);
    if (this->failure) std::rethrow_exception(this->failure);
  }
  void apply(SoPath * path) override {
    this->failure = nullptr;
    Action::apply(path);
    if (this->failure) std::rethrow_exception(this->failure);
  }

private:
  static void dispatch(SoAction * action, SoNode * node) {
    auto * guarded = static_cast<CoinRenderGuardedTraversal *>(action);
    if (guarded->hasTerminated()) return;
    SoState * state = guarded->getState();
    const int depth = state->getDepth();
    const int pathLength = guarded->getCurPath()->fullPath().getLength();
    try {
      const int index = SoNode::getActionMethodIndex(node->getTypeId());
      (*guarded->nativeMethods)[index](action, node);
    } catch (...) {
      guarded->failure = std::current_exception();
      if (state->isElementEnabled(SoCacheElement::getClassStackIndex()))
        SoCacheElement::invalidate(state);
      while (state->getDepth() > depth) state->pop();
      while (guarded->getCurPath()->fullPath().getLength() > pathLength)
        guarded->popCurPath();
      guarded->setTerminated(TRUE);
    }
  }
  SoActionMethodList * nativeMethods;
  SoActionMethodList guardedMethods;
  std::exception_ptr failure;
};

#endif
