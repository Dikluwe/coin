#include "CoinTest.h"
#include "tidbitsp.h"
#include "misc/SbHash.h"

#include <climits>
#include "base/dict.h"
#include "base/dictp.h"

BOOST_AUTO_TEST_CASE(coin_prime_boundary_has_no_composite_fallback)
{
  const unsigned long largest = 4294967291UL;
  BOOST_CHECK_EQUAL(coin_geq_prime_number(0), 2UL);
  BOOST_CHECK_EQUAL(coin_geq_prime_number(3), 3UL);
  BOOST_CHECK_EQUAL(coin_geq_prime_number(18), 19UL);
  BOOST_CHECK_EQUAL(coin_geq_prime_number(20), 23UL);
  BOOST_CHECK_EQUAL(coin_geq_prime_number(largest - 1), largest);
  BOOST_CHECK_EQUAL(coin_geq_prime_number(largest), largest);
  BOOST_CHECK_EQUAL(coin_geq_prime_number(largest + 1), 0UL);
  BOOST_CHECK_EQUAL(coin_geq_prime_number(ULONG_MAX), 0UL);
  BOOST_CHECK_EQUAL(coin_geq_growth_prime_number(18), 37UL);
  BOOST_CHECK_EQUAL(coin_geq_growth_prime_number(largest + 1), 0UL);

  cc_dict * dict = cc_dict_construct(18, 0.75f);
  BOOST_REQUIRE(dict != NULL);
  BOOST_CHECK_EQUAL(dict->size, 19U);
  for (uintptr_t key = 0; key < 15; ++key) {
    cc_dict_put(dict, key, NULL);
  }
  BOOST_CHECK_EQUAL(dict->size, 37U);
  cc_dict_destruct(dict);

  BOOST_CHECK(cc_dict_construct(UINT_MAX, 0.75f) == NULL);
  bool rejected = false;
  try { SbHash<int, int> hash(UINT_MAX); }
  catch (const std::bad_alloc &) { rejected = true; }
  BOOST_CHECK(rejected);
}
