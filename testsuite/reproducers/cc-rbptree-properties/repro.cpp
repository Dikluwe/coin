#include "base/rbptree.cpp"
#include "tidbitsp.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <functional>
#include <random>
#include <set>
#include <vector>

extern "C" void coin_atexit_func(const char *, coin_atexit_f *, coin_atexit_priorities) {}

struct Pair { void * key; void * data; };
static void collect(void * key, void * data, void * closure)
{
  static_cast<std::vector<Pair> *>(closure)->push_back({key, data});
}
static bool same(Pair a, Pair b) { return a.key == b.key && a.data == b.data; }
static std::vector<Pair> snapshot(const cc_rbptree & tree)
{
  std::vector<Pair> result;
  cc_rbptree_traverse(&tree, collect, &result);
  return result;
}
static void fail(int seed, int step, const char * what)
{
  std::fprintf(stderr, "seed=%d step=%d %s\n", seed, step, what);
  std::abort();
}
static int check_node(const cc_rbptree_node * node, const cc_rbptree_node * nil,
                      void * low, void * high, std::set<const cc_rbptree_node *> & seen,
                      int seed, int step)
{
  if (node == nil) return 1;
  if (!seen.insert(node).second) fail(seed, step, "tree cycle");
  if (!node->left || !node->right || !node->parent)
    fail(seed, step, "null tree link");
  std::less<void *> less;
  if (low && less(node->pointer, low)) fail(seed, step, "BST lower bound");
  if (high && less(high, node->pointer)) fail(seed, step, "BST upper bound");
  if (node->color > 1) fail(seed, step, "invalid color");
  if (node->color == 0 && (node->left->color == 0 || node->right->color == 0))
    fail(seed, step, "red child");
  if (node->left != nil && node->left->parent != node)
    fail(seed, step, "left parent");
  if (node->right != nil && node->right->parent != node)
    fail(seed, step, "right parent");
  const int left = check_node(node->left, nil, low, node->pointer, seen, seed, step);
  const int right = check_node(node->right, nil, node->pointer, high, seen, seed, step);
  if (left != right) fail(seed, step, "black height");
  return left + (node->color == 1);
}
static void check(const cc_rbptree & tree, const cc_rbptree_node * nil,
                  const std::vector<Pair> & model,
                  int seed, int step)
{
  if (cc_rbptree_size(&tree) != model.size()) fail(seed, step, "size");
  if (nil->color != 1) fail(seed, step, "sentinel color");
  std::set<const cc_rbptree_node *> seen;
  if (tree.root != nil) {
    if (tree.root->color != 1) fail(seed, step, "root color");
    if (tree.root->parent != nil) fail(seed, step, "root parent");
    check_node(tree.root, nil, nullptr, nullptr, seen, seed, step);
  }
  if (seen.size() != (model.size() > 2 ? model.size() - 2 : 0))
    fail(seed, step, "node count");
  const auto actual = snapshot(tree);
  if (actual.size() != model.size()) fail(seed, step, "traversal size");
  std::vector<bool> used(actual.size(), false);
  for (const Pair expected : model) {
    bool found = false;
    for (size_t i = 0; i < actual.size(); ++i) {
      if (!used[i] && same(actual[i], expected)) {
        used[i] = true;
        found = true;
        break;
      }
    }
    if (!found) fail(seed, step, "pair multiset");
  }
}

int main()
{
  int keys[13] = {}, values[19] = {};
  for (int seed = 0; seed < 64; ++seed) {
    cc_rbptree tree;
    cc_rbptree_init(&tree);
    const cc_rbptree_node * nil = tree.root;
    std::vector<Pair> model;
    std::mt19937 rng(seed);
    for (int step = 0; step < 4000; ++step) {
      const unsigned action = rng() % 10;
      void * key = &keys[rng() % 13];
      void * data = &values[rng() % 19];
      if (action < 5 && model.size() < 60) {
        cc_rbptree_insert(&tree, key, data);
        model.push_back({key, data});
      }
      else if (action < 9) {
        const auto before = snapshot(tree);
        const bool expected = std::any_of(before.begin(), before.end(),
                                          [key](Pair p) { return p.key == key; });
        const bool removed = cc_rbptree_remove(&tree, key) != FALSE;
        if (removed != expected) fail(seed, step, "remove result");
        const auto after = snapshot(tree);
        if (after.size() != before.size() - (removed ? 1 : 0))
          fail(seed, step, "remove count");
        if (removed) {
          bool reconciled = false;
          for (size_t i = 0; i < model.size(); ++i) {
            if (model[i].key != key) continue;
            const Pair old = model[i];
            model.erase(model.begin() + i);
            std::vector<Pair> expected_after = model;
            bool equal = expected_after.size() == after.size();
            for (Pair item : after) {
              const auto it = std::find_if(expected_after.begin(), expected_after.end(),
                                           [item](Pair p) { return same(p, item); });
              if (it == expected_after.end()) { equal = false; break; }
              expected_after.erase(it);
            }
            if (equal) { reconciled = true; break; }
            model.insert(model.begin() + i, old);
          }
          if (!reconciled) fail(seed, step, "remove changed wrong pair");
        }
      }
      else {
        cc_rbptree_clean(&tree);
        model.clear();
      }
      check(tree, nil, model, seed, step);
    }
    cc_rbptree_clean(&tree);
  }
  std::puts("rbptree multiset properties passed");
}
