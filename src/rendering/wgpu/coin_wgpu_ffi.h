#ifndef COIN_WGPU_FFI_H
#define COIN_WGPU_FFI_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define COIN_WGPU_ABI_VERSION 3

typedef uint64_t CoinWgpuSurfaceId;
#define COIN_WGPU_INVALID_SURFACE_ID UINT64_C(0)

typedef enum CoinWgpuStatus {
  COIN_WGPU_OK = 0,
  COIN_WGPU_NOT_READY = 1,
  COIN_WGPU_INVALID_ARGUMENT = 2,
  COIN_WGPU_UNSUPPORTED = 3,
  COIN_WGPU_OUT_OF_MEMORY = 4,
  COIN_WGPU_DEVICE_LOST = 5,
  COIN_WGPU_BACKEND_ERROR = 6,
  COIN_WGPU_SURFACE_LOST = 7
} CoinWgpuStatus;

typedef enum CoinWgpuNativeSurfaceType {
  COIN_WGPU_NATIVE_XLIB = 1,
  COIN_WGPU_NATIVE_WAYLAND = 2,
  COIN_WGPU_NATIVE_WIN32 = 3,
  COIN_WGPU_NATIVE_APPKIT_LAYER = 4
} CoinWgpuNativeSurfaceType;

typedef struct CoinWgpuNativeSurfaceDescriptor {
  uint32_t abi_version;
  uint32_t struct_size;
  uint32_t type;
  uint32_t reserved;
  uint64_t handle_a;
  uint64_t handle_b;
} CoinWgpuNativeSurfaceDescriptor;

typedef struct CoinWgpuSurfaceCreateInfo {
  uint32_t abi_version;
  uint32_t struct_size;
  CoinWgpuNativeSurfaceDescriptor native;
  uint32_t width;
  uint32_t height;
} CoinWgpuSurfaceCreateInfo;

typedef struct CoinWgpuVertex {
  float position[3];
  float normal[3];
  float texcoord[2];
  uint32_t material_slot;
} CoinWgpuVertex;

typedef struct CoinWgpuDraw {
  uint32_t topology;
  uint32_t first_vertex;
  uint32_t vertex_count;
  uint32_t first_index;
  uint32_t index_count;
  uint32_t render_state_slot;
} CoinWgpuDraw;

typedef struct CoinWgpuMaterial {
  float ambient[4];
  float diffuse[4];
  float specular[4];
  float emission[4];
  float shininess;
  float transparency;
} CoinWgpuMaterial;

typedef struct CoinWgpuRenderState {
  float model_view[16];
  float model_view_projection[16];
  float normal_matrix[16];
  float light_direction[4];
  float light_color[4];
  float light_intensity;
  uint32_t has_light;
  uint32_t material_slot;
  uint32_t cull_mode;  /* 0=None, 1=Back, 2=Front */
  uint32_t front_face; /* 0=Ccw, 1=Cw */
} CoinWgpuRenderState;

typedef struct CoinWgpuFrameView {
  uint32_t abi_version;
  uint32_t struct_size;

  const CoinWgpuVertex * vertices;
  uint64_t vertex_count;
  const uint32_t * indices;
  uint64_t index_count;
  const CoinWgpuDraw * draws;
  uint64_t draw_count;

  const CoinWgpuMaterial * materials;
  uint64_t material_count;
  const CoinWgpuRenderState * states;
  uint64_t state_count;

  float clear_color[4];
  uint32_t width;
  uint32_t height;
} CoinWgpuFrameView;

typedef struct CoinWgpuTarget {
  uint32_t width;
  uint32_t height;
  uint8_t * color_buffer;
  uint64_t color_buffer_len;
  float * depth_buffer;
  uint64_t depth_buffer_len;
  uint64_t submission_serial;
} CoinWgpuTarget;

int32_t coin_wgpu_is_available(void);

void coin_wgpu_get_adapter_info(char * buffer, size_t buffer_len);

CoinWgpuStatus coin_wgpu_submit(
  CoinWgpuTarget * target,
  const CoinWgpuFrameView * frame,
  char * error_buf,
  size_t error_buf_len
);

CoinWgpuStatus coin_wgpu_surface_create(
  const CoinWgpuSurfaceCreateInfo * info,
  CoinWgpuSurfaceId * out_surface,
  char * error_buf,
  size_t error_buf_len
);

CoinWgpuStatus coin_wgpu_surface_resize(
  CoinWgpuSurfaceId surface,
  uint32_t width,
  uint32_t height,
  char * error_buf,
  size_t error_buf_len
);

CoinWgpuStatus coin_wgpu_surface_submit(
  CoinWgpuSurfaceId surface,
  const CoinWgpuFrameView * frame,
  char * error_buf,
  size_t error_buf_len
);

CoinWgpuStatus coin_wgpu_surface_destroy(
  CoinWgpuSurfaceId surface,
  char * error_buf,
  size_t error_buf_len
);

void coin_wgpu_inject_fault(int32_t fault_code);

void coin_wgpu_inject_async_fault(int32_t fault_code);

void coin_wgpu_reset_context(void);

#ifdef __cplusplus
}
#endif

#endif // !COIN_WGPU_FFI_H
