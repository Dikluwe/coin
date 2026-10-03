#include "CoinTest.h"
#include "tidbitsp.h"
#include "misc/SbHash.h"

#include <climits>
#include "base/dict.h"

BOOST_AUTO_TEST_CASE(coin_prime_boundary_has_no_composite_fallback)
{
  const unsigned long largest = 4294967291UL;
  BOOST_CHECK_EQUAL(coin_geq_prime_number(0), 2UL);
  BOOST_CHECK_EQUAL(coin_geq_prime_number(largest), largest);
  BOOST_CHECK_EQUAL(coin_geq_prime_number(largest + 1), 0UL);
  BOOST_CHECK_EQUAL(coin_geq_prime_number(ULONG_MAX), 0UL);
  BOOST_CHECK(cc_dict_construct(UINT_MAX, 0.75f) == NULL);
  bool rejected = false;
  try { SbHash<int, int> hash(UINT_MAX); }
  catch (const std::bad_alloc &) { rejected = true; }
  BOOST_CHECK(rejected);
}
