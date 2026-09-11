#ifndef COIN_SOCOMPILEDSCENECACHE_H
#define COIN_SOCOMPILEDSCENECACHE_H

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
 * Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * Neither the name of the copyright holder nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
\**************************************************************************/

#ifndef COIN_INTERNAL
#error this is a private header file
#endif // !COIN_INTERNAL

#include <atomic>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include <Inventor/misc/SoChildList.h>
#include <Inventor/nodes/SoNode.h>
#include <Inventor/sensors/SoNodeSensor.h>

/* Immutable, internal snapshot of scene-graph topology.

   The unique-node and edge arrays are the shared "green" representation:
   a node referenced through multiple DEF/USE-style graph edges is stored
   once. A compact traversal stack creates the "red" projection on demand,
   so repeated USE-style occurrences do not consume persistent cache memory.

   Node fields deliberately remain in the original scene graph. An immediate
   SoNodeSensor invalidates the snapshot after any field or topology change;
   rebuilding creates a new immutable cache instead of patching this one. */
class SoCompiledSceneCache {
public:
  enum : uint32_t { INVALID_INDEX = UINT32_MAX };

  explicit SoCompiledSceneCache(SoNode * root)
    : valid(true), cyclic(false), sensor(SoCompiledSceneCache::changedCB, this)
  {
    if (root == NULL) return;

    NodeIndexMap indices;
    ChildLists childlists;
    std::vector<unsigned char> states;
    const uint32_t rootidx = this->compileNode(root, indices, childlists, states);
    this->makeEdges(childlists);
    if (!this->cyclic) {
      std::vector<uint64_t> counts(this->nodes.size(), 0);
      this->occurrencecount = this->countOccurrences(rootidx, counts);
    }

    this->sensor.setPriority(0);
    this->sensor.attach(root);
  }

  ~SoCompiledSceneCache(void)
  {
    this->sensor.detach();
    for (std::vector<SoNode *>::reverse_iterator it = this->nodes.rbegin();
         it != this->nodes.rend(); ++it) {
      (*it)->unref();
    }
  }

  SbBool isValid(void) const
  {
    return this->valid.load(std::memory_order_acquire) ? TRUE : FALSE;
  }

  SbBool hasCycle(void) const { return this->cyclic ? TRUE : FALSE; }

  uint32_t getNumUniqueNodes(void) const
  {
    return static_cast<uint32_t>(this->nodes.size());
  }

  uint32_t getNumEdges(void) const
  {
    return static_cast<uint32_t>(this->edges.size());
  }

  uint64_t getNumOccurrences(void) const
  {
    return this->occurrencecount;
  }

  SoNode * getUniqueNode(uint32_t index) const
  {
    return index < this->nodes.size() ? this->nodes[index] : NULL;
  }

  void getChildren(uint32_t index, const uint32_t * & children,
                   uint32_t & count) const
  {
    if (index >= this->nodes.size()) {
      children = NULL;
      count = 0;
      return;
    }
    const uint32_t begin = this->childoffsets[index];
    count = this->childoffsets[index + 1] - begin;
    children = count != 0 ? &this->edges[begin] : NULL;
  }

  template <typename Visitor>
  SbBool forEachOccurrence(Visitor visitor) const
  {
    if (!this->isValid() || this->cyclic || this->nodes.empty()) return FALSE;

    struct Frame {
      Frame(uint32_t nodearg, uint32_t edgearg)
        : node(nodearg), nextedge(edgearg) { }
      uint32_t node;
      uint32_t nextedge;
    };
    std::vector<Frame> stack;
    visitor(this->nodes[0], static_cast<uint32_t>(0));
    stack.push_back(Frame(0, this->childoffsets[0]));
    while (!stack.empty()) {
      Frame & frame = stack.back();
      if (frame.nextedge == this->childoffsets[frame.node + 1]) {
        stack.pop_back();
        continue;
      }
      const uint32_t childidx = this->edges[frame.nextedge++];
      visitor(this->nodes[childidx], static_cast<uint32_t>(stack.size()));
      stack.push_back(Frame(childidx, this->childoffsets[childidx]));
    }
    return TRUE;
  }

  size_t getCompiledBytes(void) const
  {
    return this->nodes.size() * sizeof(SoNode *) +
      this->childoffsets.size() * sizeof(uint32_t) +
      this->edges.size() * sizeof(uint32_t);
  }

private:
  typedef std::unordered_map<SoNode *, uint32_t> NodeIndexMap;
  typedef std::vector<std::vector<uint32_t> > ChildLists;

  static void changedCB(void * data, SoSensor *)
  {
    static_cast<SoCompiledSceneCache *>(data)->valid.store(
      false, std::memory_order_release);
  }

  uint32_t compileNode(SoNode * node, NodeIndexMap & indices,
                       ChildLists & childlists,
                       std::vector<unsigned char> & states)
  {
    const NodeIndexMap::const_iterator found = indices.find(node);
    if (found != indices.end()) {
      if (states[found->second] == 1) this->cyclic = true;
      return found->second;
    }

    const uint32_t index = static_cast<uint32_t>(this->nodes.size());
    indices.insert(std::make_pair(node, index));
    node->ref();
    this->nodes.push_back(node);
    childlists.push_back(std::vector<uint32_t>());
    states.push_back(1);

    SoChildList * children = node->getChildren();
    if (children != NULL) {
      const int count = children->getLength();
      childlists[index].reserve(static_cast<size_t>(count));
      for (int child = 0; child < count; ++child) {
        const uint32_t childidx =
          this->compileNode((*children)[child], indices, childlists, states);
        childlists[index].push_back(childidx);
      }
    }
    states[index] = 2;
    return index;
  }

  void makeEdges(const ChildLists & childlists)
  {
    this->childoffsets.reserve(childlists.size() + 1);
    this->childoffsets.push_back(0);
    for (ChildLists::const_iterator list = childlists.begin();
         list != childlists.end(); ++list) {
      this->edges.insert(this->edges.end(), list->begin(), list->end());
      this->childoffsets.push_back(static_cast<uint32_t>(this->edges.size()));
    }
  }

  uint64_t countOccurrences(uint32_t nodeidx, std::vector<uint64_t> & counts)
  {
    if (counts[nodeidx] != 0) return counts[nodeidx];
    uint64_t count = 1;
    for (uint32_t edge = this->childoffsets[nodeidx];
         edge < this->childoffsets[nodeidx + 1]; ++edge) {
      const uint64_t childcount = this->countOccurrences(this->edges[edge], counts);
      if (UINT64_MAX - count < childcount) count = UINT64_MAX;
      else count += childcount;
    }
    counts[nodeidx] = count;
    return count;
  }

  SoCompiledSceneCache(const SoCompiledSceneCache &);
  SoCompiledSceneCache & operator=(const SoCompiledSceneCache &);

  std::atomic<bool> valid;
  bool cyclic;
  std::vector<SoNode *> nodes;
  std::vector<uint32_t> childoffsets;
  std::vector<uint32_t> edges;
  uint64_t occurrencecount = 0;
  SoNodeSensor sensor;
};

#endif // !COIN_SOCOMPILEDSCENECACHE_H
