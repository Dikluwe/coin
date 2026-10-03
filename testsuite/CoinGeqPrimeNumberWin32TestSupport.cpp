// SbHash's resize()/operator=() (exercised from InternalMacroTest.cpp via
// misc/SbHash.h) call the private Coin prime helpers. On
// Windows shared-library builds this symbol is not exported from the Coin
// DLL, so linking CoinTests.exe against it fails. Compile a copy of the
// (small, self-contained) wrappers around the shared implementation into the test executable
// instead of exposing it as public API, matching the existing dict.cpp
// precedent used elsewhere in this file for the same kind of problem.
//
// tidbitsp.h declares this function inside an `extern "C" { ... }` block,
// so the definition must be included through that same header (with
// COIN_INTERNAL defined, see CMakeLists.txt) to get matching C linkage --
// a plain C++ definition here would be name-mangled differently and the
// linker would report it as unresolved anyway despite existing.
#include "tidbitsp.h"
#include "primep.h"

unsigned long
coin_geq_prime_number(unsigned long num)
{
  return coin_exact_prime_at_least(num);
}

unsigned long
coin_geq_growth_prime_number(unsigned long num)
{
  return coin_growth_prime_at_least(num);
}
