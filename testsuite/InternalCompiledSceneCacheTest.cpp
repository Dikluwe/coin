/**************************************************************************\
 * Copyright (c) 2026 FreeCAD contributors
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES ARE DISCLAIMED.
\**************************************************************************/

#include "caches/SoCompiledSceneCache.h"
#include "CoinTest.h"

#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoGroup.h>
#include <Inventor/nodes/SoSeparator.h>

BOOST_AUTO_TEST_CASE(SoCompiledSceneCache_shares_nodes_and_retains_occurrences)
{
  SoSeparator * root = new SoSeparator;
  root->ref();
  SoGroup * left = new SoGroup;
  SoGroup * right = new SoGroup;
  SoCube * shared = new SoCube;
  left->addChild(shared);
  right->addChild(shared);
  root->addChild(left);
  root->addChild(right);

  SoCompiledSceneCache cache(root);
  BOOST_CHECK(cache.isValid());
  BOOST_CHECK(!cache.hasCycle());
  BOOST_CHECK_EQUAL(cache.getNumUniqueNodes(), 4U);
  BOOST_CHECK_EQUAL(cache.getNumEdges(), 4U);
  BOOST_CHECK_EQUAL(cache.getNumOccurrences(), UINT64_C(5));

  std::vector<SoNode *> occurrences;
  std::vector<uint32_t> depths;
  BOOST_REQUIRE(cache.forEachOccurrence(
    [&occurrences, &depths](SoNode * node, uint32_t depth) {
      occurrences.push_back(node);
      depths.push_back(depth);
    }));
  BOOST_REQUIRE_EQUAL(occurrences.size(), static_cast<size_t>(5));
  BOOST_CHECK(occurrences[2] == shared);
  BOOST_CHECK(occurrences[4] == shared);
  BOOST_CHECK_EQUAL(depths[0], 0U);
  BOOST_CHECK_EQUAL(depths[2], 2U);
  BOOST_CHECK_EQUAL(depths[4], 2U);

  const uint32_t * children = NULL;
  uint32_t count = 0;
  cache.getChildren(0, children, count);
  BOOST_REQUIRE_EQUAL(count, 2U);
  BOOST_CHECK(cache.getUniqueNode(children[0]) == left);
  BOOST_CHECK(cache.getUniqueNode(children[1]) == right);
  root->unref();
}

BOOST_AUTO_TEST_CASE(SoCompiledSceneCache_invalidates_without_mutating_snapshot)
{
  SoSeparator * root = new SoSeparator;
  root->ref();
  SoCube * cube = new SoCube;
  root->addChild(cube);

  SoCompiledSceneCache * cache = new SoCompiledSceneCache(root);
  BOOST_REQUIRE(cache->isValid());
  BOOST_CHECK_EQUAL(cache->getNumUniqueNodes(), 2U);
  BOOST_CHECK_EQUAL(cache->getNumOccurrences(), UINT64_C(2));

  cube->width = 2.0f;
  BOOST_CHECK(!cache->isValid());
  BOOST_CHECK_EQUAL(cache->getNumUniqueNodes(), 2U);
  BOOST_CHECK_EQUAL(cache->getNumOccurrences(), UINT64_C(2));
  BOOST_CHECK(!cache->forEachOccurrence([](SoNode *, uint32_t) { }));

  root->addChild(new SoCube);
  BOOST_CHECK_EQUAL(cache->getNumUniqueNodes(), 2U);
  SoCompiledSceneCache rebuilt(root);
  BOOST_CHECK(rebuilt.isValid());
  BOOST_CHECK_EQUAL(rebuilt.getNumUniqueNodes(), 3U);
  BOOST_CHECK_EQUAL(rebuilt.getNumOccurrences(), UINT64_C(3));

  delete cache;
  root->unref();
}

BOOST_AUTO_TEST_CASE(SoCompiledSceneCache_detects_cycles)
{
  SoGroup * first = new SoGroup;
  SoGroup * second = new SoGroup;
  first->ref();
  second->ref();
  first->addChild(second);
  second->addChild(first);

  {
    SoCompiledSceneCache cache(first);
    BOOST_CHECK(cache.isValid());
    BOOST_CHECK(cache.hasCycle());
    BOOST_CHECK_EQUAL(cache.getNumUniqueNodes(), 2U);
    BOOST_CHECK_EQUAL(cache.getNumEdges(), 2U);
    BOOST_CHECK_EQUAL(cache.getNumOccurrences(), UINT64_C(0));
    BOOST_CHECK(!cache.forEachOccurrence([](SoNode *, uint32_t) { }));
  }

  second->removeAllChildren();
  first->removeAllChildren();
  second->unref();
  first->unref();
}
