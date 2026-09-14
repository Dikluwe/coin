// Sanitizer probe for SbPList's unchecked integer-index preconditions.
//
// Each mode deliberately violates one public API precondition. Run one mode
// per process because a successful ASan/UBSan diagnosis terminates the
// process. See run.sh for the complete matrix and expected result.

#include <Inventor/lists/SbPList.h>

#include <climits>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <new>

static void * volatile sink;
static bool fail_array_allocation;

void *
operator new[](std::size_t bytes)
{
  if (fail_array_allocation) throw std::bad_alloc();
  void * memory = std::malloc(bytes);
  if (!memory) throw std::bad_alloc();
  return memory;
}

void
operator delete[](void * memory) noexcept
{
  std::free(memory);
}

int
main(int argc, char ** argv)
{
  if (argc != 2) {
    std::fprintf(stderr, "usage: %s MODE\n", argv[0]);
    return 2;
  }

  int value = 42;
  SbPList list;

  if (std::strcmp(argv[1], "negative-subscript") == 0) {
    list[-1] = &value;
  }
  else if (std::strcmp(argv[1], "maximum-subscript") == 0) {
    list[INT_MAX] = &value;
  }
  else if (std::strcmp(argv[1], "negative-get") == 0) {
    sink = list.get(-1);
    std::fprintf(stderr, "negative get returned %p; array starts at %p\n",
                 sink, static_cast<void *>(list.getArrayPtr()));
  }
  else if (std::strcmp(argv[1], "negative-set") == 0) {
    list.set(-1, &value);
  }
  else if (std::strcmp(argv[1], "negative-array-start") == 0) {
    sink = *list.getArrayPtr(-1);
    std::fprintf(stderr,
                 "negative array start read %p; array starts at %p\n",
                 sink, static_cast<void *>(list.getArrayPtr()));
  }
  else if (std::strcmp(argv[1], "negative-insert") == 0) {
    list.insert(&value, -1);
  }
  else if (std::strcmp(argv[1], "negative-remove") == 0) {
    list.append(&value);
    list.remove(-1);
  }
  else if (std::strcmp(argv[1], "negative-remove-fast") == 0) {
    list.append(&value);
    list.removeFast(-1);
  }
  else if (std::strcmp(argv[1], "negative-truncate") == 0) {
    list.truncate(-1);
    list.append(&value);
  }
  else if (std::strcmp(argv[1], "oversize-truncate") == 0) {
    list.append(&value);
    list.truncate(5);
    sink = list.get(4);
  }
  else if (std::strcmp(argv[1], "failed-growth-state") == 0) {
    int values[5] = { 0, 1, 2, 3, 4 };
    for (int i = 0; i < 4; i++) list.append(&values[i]);

    bool allocationfailed = false;
    fail_array_allocation = true;
    try {
      list.append(&values[4]);
    }
    catch (const std::bad_alloc &) {
      allocationfailed = true;
    }
    fail_array_allocation = false;

    if (!allocationfailed || list.getLength() != 4) {
      std::fprintf(stderr, "growth failure did not preserve the list length\n");
      return 1;
    }

    // A failed grow must leave the old capacity in force. The current
    // implementation changes it before allocating, so this otherwise valid
    // append writes beyond builtinbuffer and ASan diagnoses the stale state.
    list.append(&values[4]);
    if (list.getLength() != 5 || list.get(4) != &values[4]) return 1;
    std::fprintf(stderr, "failed growth preserved a reusable list\n");
    return 0;
  }
  else {
    std::fprintf(stderr, "unknown mode: %s\n", argv[1]);
    return 2;
  }

  std::fprintf(stderr,
               "UNDETECTED: %s completed despite an invalid SbPList index\n",
               argv[1]);
  return 0;
}
