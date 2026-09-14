// Verify SoCallbackList's two SbPList members remain mutually consistent when
// assignment fails between copying the function and userdata arrays.

#include <Inventor/SoDB.h>
#include <Inventor/SoInteraction.h>
#include <Inventor/lists/SoCallbackList.h>
#include <Inventor/nodes/SoSelection.h>

#include <cstdlib>
#include <cstdio>
#include <new>

static int array_allocations_until_failure = -1;

void *
operator new[](std::size_t bytes)
{
  if (array_allocations_until_failure > 0 &&
      --array_allocations_until_failure == 0) {
    throw std::bad_alloc();
  }
  void * memory = std::malloc(bytes);
  if (!memory) throw std::bad_alloc();
  return memory;
}

void
operator delete[](void * memory) noexcept
{
  std::free(memory);
}

class CallbackAllocationSelection : public SoSelection {
public:
  SoCallbackList & callbacks(void) { return *this->changeCBList; }
  void fire(void) { this->changeCBList->invokeCallbacks(this); }
};

static void
callback_allocation_typed(void * userdata, SoSelection *)
{
  ++*static_cast<int *>(userdata);
}

int
main()
{
  SoDB::init();
  SoInteraction::init();
  int sourcecalls = 0;
  int destinationcalls = 0;

  CallbackAllocationSelection * source = new CallbackAllocationSelection;
  CallbackAllocationSelection * destination = new CallbackAllocationSelection;
  source->ref();
  destination->ref();
  for (int i = 0; i < 5; i++) {
    source->addChangeCallback(callback_allocation_typed, &sourcecalls);
  }
  for (int i = 0; i < 4; i++) {
    destination->addChangeCallback(callback_allocation_typed, &destinationcalls);
  }

  bool allocationfailed = false;
  array_allocations_until_failure = 2;
  try {
    destination->callbacks() = source->callbacks();
  }
  catch (const std::bad_alloc &) {
    allocationfailed = true;
  }
  array_allocations_until_failure = -1;

  if (!allocationfailed) {
    std::fprintf(stderr, "FAIL: the injected second array allocation succeeded\n");
    destination->unref();
    source->unref();
    SoDB::finish();
    return 1;
  }

  // A strong guarantee leaves the four original callback/data pairs intact.
  // The current memberwise assignment leaves five functions but four data
  // entries, so firing the list reaches a mismatched fifth registration.
  destination->fire();
  const bool preserved = destinationcalls == 4 && sourcecalls == 0 &&
                         destination->callbacks().getNumCallbacks() == 4;

  destination->unref();
  source->unref();
  SoDB::finish();
  if (!preserved) {
    std::fprintf(stderr, "FAIL: callback assignment lost pair consistency\n");
    return 1;
  }
  std::fprintf(stderr, "PASS: callback assignment preserved all original pairs\n");
  return 0;
}
