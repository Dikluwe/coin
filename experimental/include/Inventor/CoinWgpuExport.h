#ifndef COIN_WGPU_EXPORT_H
#define COIN_WGPU_EXPORT_H

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
  #if defined(COIN_WGPU_INTERNAL)
    #define COIN_WGPU_DLL_API __declspec(dllexport)
  #else
    #define COIN_WGPU_DLL_API __declspec(dllimport)
  #endif
#else
  #if defined(COIN_WGPU_INTERNAL)
    #define COIN_WGPU_DLL_API __attribute__((visibility("default")))
  #else
    #define COIN_WGPU_DLL_API
  #endif
#endif

#endif // !COIN_WGPU_EXPORT_H
