#ifndef COIN_RENDER_MANAGER_LAYER_PURPOSE_H
#define COIN_RENDER_MANAGER_LAYER_PURPOSE_H

// Private manager boundary purpose, separate from authored transparency values.
// These identities belong to generated composition boundaries, not nodes in
// the authored scene or inferred material/texture combinations.
static const int COIN_RENDER_MANAGER_OWNER = -4;
static const int COIN_RENDER_MANAGER_BASE_LAYER = -2;
static const int COIN_RENDER_MANAGER_HIGHLIGHT_LAYER = -3;

#endif
