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

class SbPListTestAccess : public SbPList {
public:
  explicit SbPListTestAccess(const int sizehint = 4) : SbPList(sizehint) { }
  int capacity(void) const { return this->getArraySize(); }
};

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
has_state(const SbPListTestAccess & list, void ** items, int count,
          int capacity, void ** array)
{
  if (list.getLength() != count) return false;
  if (list.capacity() != capacity) return false;
  if (list.getArrayPtr() != array) return false;
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
  SbPListTestAccess list;

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
    void * expected[4];
    for (int i = 0; i < 4; i++) list.append(&values[i]);
    for (int i = 0; i < 4; i++) expected[i] = &values[i];
    void ** oldarray = list.getArrayPtr();

    const bool allocationfailed = inject_bad_alloc([&]() { list.append(&values[4]); });
    if (!allocationfailed || !has_state(list, expected, 4, 4, oldarray)) {
      std::fprintf(stderr, "growth failure did not preserve the complete list state\n");
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
  else if (std::strcmp(argv[1], "failed-growth-append-heap") == 0) {
    int values[9];
    void * expected[8];
    for (int i = 0; i < 8; i++) {
      values[i] = i;
      list.append(&values[i]);
      expected[i] = &values[i];
    }
    values[8] = 8;
    void ** oldarray = list.getArrayPtr();
    const bool allocationfailed = inject_bad_alloc([&]() { list.append(&values[8]); });
    if (!allocationfailed || !has_state(list, expected, 8, 8, oldarray)) return 1;
    list.append(&values[8]);
    return list.getLength() == 9 && list.capacity() == 16 &&
           list.get(8) == &values[8] ? 0 : 1;
  }
  else if (std::strcmp(argv[1], "failed-growth-insert") == 0) {
    int values[5] = { 0, 1, 2, 3, 4 };
    void * expected[4];
    for (int i = 0; i < 4; i++) {
      list.append(&values[i]);
      expected[i] = &values[i];
    }
    void ** oldarray = list.getArrayPtr();
    const bool allocationfailed = inject_bad_alloc([&]() { list.insert(&values[4], 2); });
    if (!allocationfailed || !has_state(list, expected, 4, 4, oldarray)) return 1;
    list.insert(&values[4], 2);
    return list.getLength() == 5 && list.get(2) == &values[4] ? 0 : 1;
  }
  else if (std::strcmp(argv[1], "failed-growth-subscript") == 0) {
    int values[5] = { 0, 1, 2, 3, 4 };
    void * expected[4];
    for (int i = 0; i < 4; i++) {
      list.append(&values[i]);
      expected[i] = &values[i];
    }
    void ** oldarray = list.getArrayPtr();
    const bool allocationfailed = inject_bad_alloc([&]() { list[4] = &values[4]; });
    if (!allocationfailed || !has_state(list, expected, 4, 4, oldarray)) return 1;
    list[4] = &values[4];
    return list.getLength() == 5 && list.get(4) == &values[4] ? 0 : 1;
  }
  else if (std::strcmp(argv[1], "failed-growth-distant-subscript") == 0) {
    int values[3] = { 0, 1, 2 };
    void * expected[2] = { &values[0], &values[1] };
    list.append(&values[0]);
    list.append(&values[1]);
    void ** oldarray = list.getArrayPtr();
    const bool allocationfailed = inject_bad_alloc([&]() { list[1024] = &values[2]; });
    if (!allocationfailed || !has_state(list, expected, 2, 4, oldarray)) return 1;
    list[1024] = &values[2];
    if (list.getLength() != 1025 || list.capacity() != 1025) return 1;
    if (list.get(0) != &values[0] || list.get(1) != &values[1]) return 1;
    for (int i = 2; i < 1024; i++) if (list.get(i) != NULL) return 1;
    return list.get(1024) == &values[2] ? 0 : 1;
  }
  else if (std::strcmp(argv[1], "failed-assignment") == 0) {
    int sourcevalues[5] = { 0, 1, 2, 3, 4 };
    int destinationvalues[4] = { 10, 11, 12, 13 };
    SbPListTestAccess source;
    SbPListTestAccess destination;
    void * expected[4];
    for (int i = 0; i < 5; i++) source.append(&sourcevalues[i]);
    void * sourceexpected[5];
    for (int i = 0; i < 5; i++) sourceexpected[i] = &sourcevalues[i];
    void ** sourcearray = source.getArrayPtr();
    for (int i = 0; i < 4; i++) {
      destination.append(&destinationvalues[i]);
      expected[i] = &destinationvalues[i];
    }
    void ** oldarray = destination.getArrayPtr();
    const bool allocationfailed = inject_bad_alloc([&]() { destination = source; });
    if (!allocationfailed || !has_state(destination, expected, 4, 4, oldarray)) return 1;
    if (!has_state(source, sourceexpected, 5, 8, sourcearray)) return 1;
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
    void ** oldarray = list.getArrayPtr();
    const bool allocationfailed = inject_bad_alloc([&]() { list.fit(); });
    if (!allocationfailed || !has_state(list, expected, 6, 16, oldarray)) return 1;
    list.fit();
    if (list.getLength() != 6 || list.capacity() != 6) return 1;
    for (int i = 0; i < 6; i++) if (list.get(i) != expected[i]) return 1;
    list.append(&values[9]);
    return list.getLength() == 7 && list.capacity() == 12 &&
           list.get(6) == &values[9] ? 0 : 1;
  }
  else if (std::strcmp(argv[1], "failed-constructor") == 0) {
    const bool allocationfailed = inject_bad_alloc([&]() { SbPList temporary(9); });
    if (!allocationfailed) return 1;
    SbPListTestAccess retry(9);
    retry.append(&value);
    return retry.getLength() == 1 && retry.capacity() == 9 &&
           retry.get(0) == &value ? 0 : 1;
  }
  else if (std::strcmp(argv[1], "failed-copy-constructor") == 0) {
    int values[6];
    SbPListTestAccess source;
    for (int i = 0; i < 5; i++) {
      values[i] = i;
      source.append(&values[i]);
    }
    void * expected[5];
    for (int i = 0; i < 5; i++) expected[i] = &values[i];
    void ** oldarray = source.getArrayPtr();
    const bool allocationfailed = inject_bad_alloc([&]() { SbPList temporary(source); });
    if (!allocationfailed || !has_state(source, expected, 5, 8, oldarray)) return 1;
    SbPList copy(source);
    if (copy != source) return 1;
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
