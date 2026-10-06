#ifndef COIN_RENDER_TEST_ENVIRONMENT_H
#define COIN_RENDER_TEST_ENVIRONMENT_H

#include <cstdlib>

inline int coinRenderTestSetEnvironment(const char * name, const char * value)
{
#ifdef _WIN32
  return _putenv_s(name, value ? value : "");
#else
  return value ? setenv(name, value, 1) : unsetenv(name);
#endif
}

#endif
