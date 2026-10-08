#include "base/rbptree.cpp"
#include "tidbitsp.h"
#include <cstdio>

extern "C" void coin_atexit_func(const char *, coin_atexit_f *, coin_atexit_priorities) {}

struct State {
  cc_rbptree * tree;
  void * first;
  void * repeated;
  void * repeated_data;
  int repeated_calls;
};

static void visit(void * key, void *, void * closure)
{
  State * state = static_cast<State *>(closure);
  if (key == state->first) {
    cc_rbptree_remove(state->tree, state->repeated);
    cc_rbptree_insert(state->tree, state->repeated, state->repeated_data);
  }
  if (key == state->repeated) ++state->repeated_calls;
}

int main()
{
  cc_rbptree tree;
  cc_rbptree_init(&tree);
  int keys[3], data[3];
  cc_rbptree_insert(&tree, &keys[0], &data[0]);
  cc_rbptree_insert(&tree, &keys[1], &data[1]);
  cc_rbptree_insert(&tree, &keys[2], &data[2]);
  State state = { &tree, &keys[0], &keys[1], &data[1], 0 };
  cc_rbptree_traverse(&tree, visit, &state);
  std::printf("reinserted_same_pair_calls=%d\n", state.repeated_calls);
  cc_rbptree_clean(&tree);
  return state.repeated_calls == 1 ? 0 : 1;
}
