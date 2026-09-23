#ifndef CC_WPOOL_TEST_CONFIG_H
#define CC_WPOOL_TEST_CONFIG_H

#ifndef COIN_INTERNAL
#define COIN_INTERNAL
#define COIN_WPOOL_TEST_UNDEF_COIN_INTERNAL
#endif

#include <config.h>
#include <Inventor/C/threads/condvar.h>
#include <Inventor/C/threads/mutex.h>
#include "threads/wpoolp.h"

#ifdef COIN_WPOOL_TEST_UNDEF_COIN_INTERNAL
#undef COIN_INTERNAL
#undef COIN_WPOOL_TEST_UNDEF_COIN_INTERNAL
#endif

#endif /* ! CC_WPOOL_TEST_CONFIG_H */
