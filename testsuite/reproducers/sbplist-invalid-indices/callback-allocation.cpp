// Verify SoCallbackList's two SbPList members remain mutually consistent when
// assignment fails between copying the function and userdata arrays.

#include <Inventor/SoDB.h>
#include <Inventor/SoInteraction.h>
#include <Inventor/lists/SoCallbackList.h>
#include <Inventor/nodes/SoSelection.h>

#include <cstdlib>
#include <cstdio>
#include <new>

static int allocations_until_failure = -1;

static void *
allocate_or_throw(std::size_t bytes)
{
  if (allocations_until_failure > 0 && --allocations_until_failure == 0) {
    throw std::bad_alloc();
  }
  void * memory = std::malloc(bytes);
  if (!memory) throw std::bad_alloc();
  return memory;
}

void *
operator new(std::size_t bytes)
{
  return allocate_or_throw(bytes);
}

void *
operator new[](std::size_t bytes)
{
  return allocate_or_throw(bytes);
}

void
operator delete(void * memory) noexcept
{
  std::free(memory);
}

void
operator delete[](void * memory) noexcept
{
  std::free(memory);
}

#if defined(__cpp_sized_deallocation)
void
operator delete(void * memory, std::size_t) noexcept
{
  std::free(memory);
}

void
operator delete[](void * memory, std::size_t) noexcept
{
  std::free(memory);
}
#endif

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
main(int argc, char ** argv)
{
  const int failureordinal = argc == 2 ? std::atoi(argv[1]) : 2;
  if (failureordinal <= 0) {
    std::fprintf(stderr, "usage: %s POSITIVE_ALLOCATION_ORDINAL\n", argv[0]);
    return 2;
  }

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
  allocations_until_failure = failureordinal;
  try {
    destination->callbacks() = source->callbacks();
  }
  catch (const std::bad_alloc &) {
    allocationfailed = true;
  }
  allocations_until_failure = -1;

  if (!allocationfailed) {
    std::fprintf(stderr, "SKIP: assignment made fewer than %d allocations\n",
                 failureordinal);
    destination->unref();
    source->unref();
    SoDB::finish();
    return 77;
  }

  // A strong guarantee leaves the four original callback/data/owner triples
  // intact regardless of which allocation in the assignment failed.
  destination->fire();
  bool preserved = destinationcalls == 4 && sourcecalls == 0 &&
                   destination->callbacks().getNumCallbacks() == 4;

  // Reuse crosses the 4 -> 5 inline/heap boundary and detects stale capacity
  // left by a failed first SbPList allocation as well as split callback state.
  destination->addChangeCallback(callback_allocation_typed, &destinationcalls);
  destination->fire();
  preserved = preserved && destinationcalls == 9 && sourcecalls == 0 &&
              destination->callbacks().getNumCallbacks() == 5;

  destination->unref();
  source->unref();
  SoDB::finish();
  if (!preserved) {
    std::fprintf(stderr,
                 "FAIL: callback assignment allocation %d lost original state\n",
                 failureordinal);
    return 1;
  }
  std::fprintf(stderr,
               "PASS: callback assignment allocation %d preserved original state\n",
               failureordinal);
  return 0;
}
