// Verify that adding a callback is atomic when any allocation fails.

#include <Inventor/SoDB.h>
#include <Inventor/SoInteraction.h>
#include <Inventor/lists/SoCallbackList.h>
#include <Inventor/nodes/SoSelection.h>

#include <cstdlib>
#include <cstdio>
#include <cstring>
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

void * operator new(std::size_t bytes) { return allocate_or_throw(bytes); }
void * operator new[](std::size_t bytes) { return allocate_or_throw(bytes); }
void operator delete(void * memory) noexcept { std::free(memory); }
void operator delete[](void * memory) noexcept { std::free(memory); }

#if defined(__cpp_sized_deallocation)
void operator delete(void * memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void * memory, std::size_t) noexcept { std::free(memory); }
#endif

class CallbackAddSelection : public SoSelection {
public:
  SoCallbackList & callbacks(void) { return *this->changeCBList; }
  void fire(void) { this->changeCBList->invokeCallbacks(this); }
};

static void callback_add_raw(void * userdata, void *)
{
  ++*static_cast<int *>(userdata);
}

static void callback_add_typed(void * userdata, SoSelection *)
{
  ++*static_cast<int *>(userdata);
}

static int
check_raw(const int failureordinal)
{
  int oldcalls = 0;
  int newcalls = 0;
  SoCallbackList callbacks;
  for (int i = 0; i < 4; ++i) callbacks.addCallback(callback_add_raw, &oldcalls);

  bool allocationfailed = false;
  allocations_until_failure = failureordinal;
  try { callbacks.addCallback(callback_add_raw, &newcalls); }
  catch (const std::bad_alloc &) { allocationfailed = true; }
  allocations_until_failure = -1;
  if (!allocationfailed) return 77;

  callbacks.invokeCallbacks(NULL);
  bool preserved = oldcalls == 4 && newcalls == 0 &&
                   callbacks.getNumCallbacks() == 4;
  callbacks.addCallback(callback_add_raw, &newcalls);
  callbacks.invokeCallbacks(NULL);
  return preserved && oldcalls == 8 && newcalls == 1 &&
         callbacks.getNumCallbacks() == 5 ? 0 : 1;
}

static int
check_typed(const int failureordinal)
{
  int oldcalls = 0;
  int newcalls = 0;
  CallbackAddSelection * selection = new CallbackAddSelection;
  selection->ref();
  for (int i = 0; i < 4; ++i) {
    selection->addChangeCallback(callback_add_typed, &oldcalls);
  }

  bool allocationfailed = false;
  allocations_until_failure = failureordinal;
  try { selection->addChangeCallback(callback_add_typed, &newcalls); }
  catch (const std::bad_alloc &) { allocationfailed = true; }
  allocations_until_failure = -1;
  if (!allocationfailed) {
    selection->unref();
    return 77;
  }

  selection->fire();
  bool preserved = oldcalls == 4 && newcalls == 0 &&
                   selection->callbacks().getNumCallbacks() == 4;
  selection->addChangeCallback(callback_add_typed, &newcalls);
  selection->fire();
  preserved = preserved && oldcalls == 8 && newcalls == 1 &&
              selection->callbacks().getNumCallbacks() == 5;
  selection->unref();
  return preserved ? 0 : 1;
}

int
main(int argc, char ** argv)
{
  if (argc != 3) {
    std::fprintf(stderr, "usage: %s raw|typed POSITIVE_ALLOCATION_ORDINAL\n",
                 argv[0]);
    return 2;
  }
  const bool typed = std::strcmp(argv[1], "typed") == 0;
  if (!typed && std::strcmp(argv[1], "raw") != 0) return 2;
  const int failureordinal = std::atoi(argv[2]);
  if (failureordinal <= 0) return 2;

  SoDB::init();
  SoInteraction::init();
  const int result = typed ? check_typed(failureordinal)
                           : check_raw(failureordinal);
  SoDB::finish();

  if (result == 77) {
    std::fprintf(stderr, "SKIP: %s callback add made fewer than %d allocations\n",
                 argv[1], failureordinal);
    return 77;
  }
  if (result != 0) {
    std::fprintf(stderr,
                 "FAIL: %s callback add allocation %d lost original state\n",
                 argv[1], failureordinal);
    return 1;
  }
  std::fprintf(stderr, "PASS: %s callback add allocation %d preserved state\n",
               argv[1], failureordinal);
  return 0;
}
