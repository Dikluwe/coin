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

#if !defined(SBPLIST_NO_ALLOCATION_INTERPOSE)
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
#endif

template <typename Operation>
static bool
inject_bad_alloc(Operation operation)
{
  fail_array_allocation = true;
  try {
    operation();
  }
  catch (const std::bad_alloc &) {
    fail_array_allocation = false;
    return true;
  }
  catch (...) {
    fail_array_allocation = false;
    throw;
  }
  fail_array_allocation = false;
  return false;
}

static bool
has_items(const SbPList & list, void ** items, int count)
{
  if (list.getLength() != count) return false;
  for (int i = 0; i < count; i++) {
    if (list.get(i) != items[i]) return false;
  }
  return true;
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
  else if (std::strcmp(argv[1], "past-end-get") == 0) {
    list.append(&value);
    sink = list.get(1);
    if (sink == &value) std::fprintf(stderr, "unexpected repeated value\n");
  }
  else if (std::strcmp(argv[1], "past-end-set") == 0) {
    list.append(&value);
    list.set(1, &value);
  }
  else if (std::strcmp(argv[1], "negative-array-start") == 0) {
    sink = *list.getArrayPtr(-1);
    std::fprintf(stderr,
                 "negative array start read %p; array starts at %p\n",
                 sink, static_cast<void *>(list.getArrayPtr()));
  }
  else if (std::strcmp(argv[1], "past-end-array-start") == 0) {
    list.append(&value);
    sink = *list.getArrayPtr(1);
    if (sink == &value) std::fprintf(stderr, "unexpected repeated value\n");
  }
  else if (std::strcmp(argv[1], "negative-insert") == 0) {
    list.insert(&value, -1);
  }
  else if (std::strcmp(argv[1], "past-end-insert") == 0) {
    list.insert(&value, 1);
    sink = list.get(0);
    std::fprintf(stderr, "past-end insert exposed slot zero as %p\n", sink);
  }
  else if (std::strcmp(argv[1], "maximum-insert") == 0) {
    list.insert(&value, INT_MAX);
  }
  else if (std::strcmp(argv[1], "negative-remove") == 0) {
    list.append(&value);
    list.remove(-1);
  }
  else if (std::strcmp(argv[1], "negative-remove-fast") == 0) {
    list.append(&value);
    list.removeFast(-1);
  }
  else if (std::strcmp(argv[1], "past-end-remove") == 0) {
    list.append(&value);
    list.remove(1);
  }
  else if (std::strcmp(argv[1], "past-end-remove-fast") == 0) {
    list.append(&value);
    list.removeFast(1);
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
  else if (std::strcmp(argv[1], "oversize-truncate-within-capacity") == 0) {
    SbPList reserved(8);
    reserved.append(&value);
    reserved.truncate(5);
    sink = reserved.get(4);
    std::fprintf(stderr, "oversize truncate exposed slot four as %p\n", sink);
  }
  else if (std::strcmp(argv[1], "minimum-truncate") == 0) {
    list.truncate(INT_MIN);
    list.append(&value);
  }
  else if (std::strcmp(argv[1], "maximum-truncate") == 0) {
    list.truncate(INT_MAX);
    sink = list.get(INT_MAX - 1);
  }
  else if (std::strcmp(argv[1], "oversize-truncate-fit") == 0) {
    list.append(&value);
    list.truncate(5, TRUE);
    sink = list.get(4);
  }
  else if (std::strcmp(argv[1], "failed-growth-append") == 0) {
    int values[5] = { 0, 1, 2, 3, 4 };
    for (int i = 0; i < 4; i++) list.append(&values[i]);

    const bool allocationfailed = inject_bad_alloc([&]() { list.append(&values[4]); });
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
  else if (std::strcmp(argv[1], "failed-growth-insert") == 0) {
    int values[5] = { 0, 1, 2, 3, 4 };
    for (int i = 0; i < 4; i++) list.append(&values[i]);
    const bool allocationfailed = inject_bad_alloc([&]() { list.insert(&values[4], 2); });
    if (!allocationfailed || list.getLength() != 4) return 1;
    list.insert(&values[4], 2);
    return list.getLength() == 5 && list.get(2) == &values[4] ? 0 : 1;
  }
  else if (std::strcmp(argv[1], "failed-growth-subscript") == 0) {
    int values[5] = { 0, 1, 2, 3, 4 };
    for (int i = 0; i < 4; i++) list.append(&values[i]);
    const bool allocationfailed = inject_bad_alloc([&]() { list[4] = &values[4]; });
    if (!allocationfailed || list.getLength() != 4) return 1;
    list[4] = &values[4];
    return list.getLength() == 5 && list.get(4) == &values[4] ? 0 : 1;
  }
  else if (std::strcmp(argv[1], "failed-assignment") == 0) {
    int sourcevalues[5] = { 0, 1, 2, 3, 4 };
    int destinationvalues[4] = { 10, 11, 12, 13 };
    SbPList source;
    SbPList destination;
    void * expected[4];
    for (int i = 0; i < 5; i++) source.append(&sourcevalues[i]);
    for (int i = 0; i < 4; i++) {
      destination.append(&destinationvalues[i]);
      expected[i] = &destinationvalues[i];
    }
    const bool allocationfailed = inject_bad_alloc([&]() { destination = source; });
    if (!allocationfailed || !has_items(destination, expected, 4)) return 1;
    destination = source;
    return destination == source ? 0 : 1;
  }
  else if (std::strcmp(argv[1], "failed-fit") == 0) {
    int values[10];
    for (int i = 0; i < 9; i++) {
      values[i] = i;
      list.append(&values[i]);
    }
    list.truncate(6);
    void * expected[6];
    for (int i = 0; i < 6; i++) expected[i] = &values[i];
    const bool allocationfailed = inject_bad_alloc([&]() { list.fit(); });
    if (!allocationfailed || !has_items(list, expected, 6)) return 1;
    list.append(&values[9]);
    return list.getLength() == 7 && list.get(6) == &values[9] ? 0 : 1;
  }
  else if (std::strcmp(argv[1], "failed-constructor") == 0) {
    const bool allocationfailed = inject_bad_alloc([&]() { SbPList temporary(9); });
    if (!allocationfailed) return 1;
    list.append(&value);
    return list.getLength() == 1 && list.get(0) == &value ? 0 : 1;
  }
  else if (std::strcmp(argv[1], "failed-copy-constructor") == 0) {
    int values[6];
    SbPList source;
    for (int i = 0; i < 5; i++) {
      values[i] = i;
      source.append(&values[i]);
    }
    const bool allocationfailed = inject_bad_alloc([&]() { SbPList temporary(source); });
    if (!allocationfailed) return 1;
    source.append(&values[5]);
    return source.getLength() == 6 && source.get(5) == &values[5] ? 0 : 1;
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
