#include <Inventor/SoDB.h>
#include <Inventor/SoInput.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/nodes/SoSeparator.h>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <new>
#include <cstdint>

static thread_local bool counting = false;
static thread_local uint64_t allocations = 0, requested = 0;
void * operator new(std::size_t size) {
  void * p = std::malloc(size ? size : 1);
  if (!p) throw std::bad_alloc();
  if (counting) { ++allocations; requested += size; }
  return p;
}
void * operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void * p) noexcept { std::free(p); }
void operator delete[](void * p) noexcept { std::free(p); }
void operator delete(void * p, std::size_t) noexcept { std::free(p); }
void operator delete[](void * p, std::size_t) noexcept { std::free(p); }
int main(int argc, char ** argv) {
  if (argc != 2) return 2;
  SoDB::init(); CoinRenderAction::initClass();
  SoInput input;
  if (!input.openFile(argv[1])) return 3;
  SoSeparator * root = SoDB::readAll(&input);
  if (!root) return 4;
  root->ref();
  for (int sample = -2; sample < 8; ++sample) {
    CoinRenderAction action(SbViewportRegion(1024,1024));
    allocations = requested = 0;
    counting = true;
    const auto start = std::chrono::steady_clock::now();
    action.apply(root);
    const auto end = std::chrono::steady_clock::now();
    counting = false;
    if (action.getLastStatus() != CoinRenderAction::SUCCESS) {
      std::cerr << action.getLastError().getString() << '\n'; return 5;
    }
    // Recording is lazy. Hash it after the timed/allocation-counted apply.
    uint64_t hash = UINT64_C(14695981039346656037);
    const unsigned char * log = reinterpret_cast<const unsigned char *>(action.getRecordingLog().getString());
    for (; *log; ++log) { hash ^= *log; hash *= UINT64_C(1099511628211); }
    if (sample >= 0) std::cout << "sample=" << sample << " apply_ms="
      << std::chrono::duration<double,std::milli>(end-start).count()
      << " cpp_allocations=" << allocations << " requested_bytes=" << requested
      << " recording_fnv64=" << std::hex << hash << std::dec << '\n';
  }
  root->unref();
}
