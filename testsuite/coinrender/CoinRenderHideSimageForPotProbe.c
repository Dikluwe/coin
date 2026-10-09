/* LD_PRELOAD helper for the POT probe. Build with:
 * cc -shared -fPIC -o /tmp/coin-hide-simage.so \
 *   testsuite/coinrender/CoinRenderHideSimageForPotProbe.c -ldl
 * Set COIN_TEST_HIDE_GLU=1 to exercise CoinGL's nearest fallback too.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>

void *dlopen(const char *name, int flags) {
  static void *(*real_dlopen)(const char *, int);
  if (!real_dlopen) real_dlopen = dlsym(RTLD_NEXT, "dlopen");
  if (name && strstr(name, "simage")) return NULL;
  if (name && getenv("COIN_TEST_HIDE_GLU") &&
      (strstr(name, "GLU") || strstr(name, "glu"))) return NULL;
  return real_dlopen(name, flags);
}
