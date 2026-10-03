#include <Inventor/C/base/hash.h>

#include <cstdio>

struct ApplyState {
  cc_hash * hash;
  unsigned int visits;
  bool failed;
};

static cc_hash_key collide(cc_hash_key)
{
  return 1;
}

static void remove_current(cc_hash_key key, void *, void * closure)
{
  ApplyState * state = static_cast<ApplyState *>(closure);
  ++state->visits;
  if (!cc_hash_remove(state->hash, key)) state->failed = true;
}

int main()
{
  cc_hash * hash = cc_hash_construct(17, 1.0f);
  if (hash == NULL) return 1;
  const cc_hash_key keys[] = { 0, 17, 34 };
  for (unsigned int i = 0; i < 3; ++i) {
    if (!cc_hash_put(hash, keys[i], NULL)) return 2;
  }
  cc_hash_set_hash_func(hash, collide);
  for (unsigned int i = 0; i < 3; ++i) {
    void * value = NULL;
    if (!cc_hash_get(hash, keys[i], &value)) return 3;
  }

  ApplyState state = { hash, 0, false };
  cc_hash_apply(hash, remove_current, &state);
  const bool okay = !state.failed && state.visits == 3 &&
    cc_hash_get_num_elements(hash) == 0;
  cc_hash_destruct(hash);
  if (!okay) std::fputs("cc_hash_apply failed to remove current entries\n", stderr);
  return okay ? 0 : 4;
}
