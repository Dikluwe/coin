#ifndef COIN_ENVIRONMENT_EXPORT_H
#define COIN_ENVIRONMENT_EXPORT_H
#if defined(_WIN32)
#if defined(COIN_ENVIRONMENT_INTERNAL)
#define COIN_ENVIRONMENT_DLL_API __declspec(dllexport)
#else
#define COIN_ENVIRONMENT_DLL_API __declspec(dllimport)
#endif
#else
#define COIN_ENVIRONMENT_DLL_API __attribute__((visibility("default")))
#endif
#endif
