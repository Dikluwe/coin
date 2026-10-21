#ifndef COIN_RENDER_X11_WSI_H
#define COIN_RENDER_X11_WSI_H

// Private Linux Vulkan WSI admission. Native connections remain caller-owned.
#if defined(__linux__) && !defined(__ANDROID__)
#include <X11/Xlib.h>
#include <dlfcn.h>
#include <sys/uio.h>
#include <cstdint>
#include <cstdlib>
#include <string>

namespace coin_render_x11_wsi {
struct Library {
  void * handle;
  explicit Library(const char * name) : handle(dlopen(name, RTLD_NOW | RTLD_LOCAL)) {}
  ~Library() { if (handle) dlclose(handle); }
  template<typename T> T symbol(const char * name) const {
    return handle ? reinterpret_cast<T>(dlsym(handle, name)) : nullptr;
  }
};
struct Cookie { unsigned int sequence; };
struct ExtensionReply {
  uint8_t response, pad; uint16_t sequence; uint32_t length;
  uint8_t present, opcode, firstEvent, firstError;
};
struct VersionReply {
  uint8_t response, pad; uint16_t sequence; uint32_t length, major, minor;
};
struct Request {
  size_t count; void * extension; uint8_t opcode, isVoid;
};

inline bool prepare(Display * display, std::string & diagnostic)
{
  if (!display) { diagnostic = "Vulkan X11 WSI requires a live Display"; return false; }
  Library xlibxcb("libX11-xcb.so.1"), xcb("libxcb.so.1");
  const auto connection = xlibxcb.symbol<void * (*)(Display *)>("XGetXCBConnection");
  const auto query = xcb.symbol<Cookie (*)(void *, uint16_t, const char *)>("xcb_query_extension");
  const auto extensionReply = xcb.symbol<ExtensionReply * (*)(void *, Cookie, void **)>("xcb_query_extension_reply");
  const auto send = xcb.symbol<unsigned int (*)(void *, int, iovec *, const Request *)>("xcb_send_request");
  const auto wait = xcb.symbol<void * (*)(void *, unsigned int, void **)>("xcb_wait_for_reply");
  if (!connection || !query || !extensionReply || !send || !wait) {
    diagnostic = "Vulkan X11 WSI cannot load Xlib/XCB protocol support"; return false;
  }
  XFlush(display);
  void * c = connection(display);
  if (!c) { diagnostic = "Vulkan X11 WSI has no XCB connection"; return false; }
  void * error = nullptr;
  ExtensionReply * extension = extensionReply(c, query(c, 6, "XFIXES"), &error);
  if (error || !extension) {
    std::free(error); std::free(extension);
    diagnostic = "Vulkan X11 WSI failed to query XFIXES extension"; return false;
  }
  const bool present = extension->present != 0;
  const uint8_t opcode = extension->opcode;
  const bool validExtension = extension->response == 1 && extension->length == 0 &&
    (!present || opcode >= 128);
  std::free(extension);
  // CoinRender's Vulkan Xlib presentation profile admits region-capable
  // connections before handing them to hardware WSI. This is a profile
  // requirement, not a universal Vulkan requirement.
  if (!validExtension) { diagnostic = "Vulkan X11 WSI received an invalid XFIXES extension reply"; return false; }
  if (!present) {
    diagnostic = "CoinRender Vulkan Xlib presentation requires XFIXES version 2 (extension absent)";
    return false;
  }

  // fixesproto section 4 requires QueryVersion before all extension requests.
  // Send it on EVERY handoff: driver caches keyed by recycled XCB addresses
  // cannot establish protocol state on a new caller-owned connection.
  struct { uint8_t opcode, minor; uint16_t length; uint32_t major, versionMinor; }
    version = {0, 0, 0, 6, 0};
  iovec vectors[4] = {};
  vectors[2].iov_base = &version;
  vectors[2].iov_len = sizeof(version);
  const Request request = {2, nullptr, opcode, 0};
  const unsigned int sequence = send(c, 1 /* XCB_REQUEST_CHECKED */, vectors + 2, &request);
  VersionReply * reply = sequence ? static_cast<VersionReply *>(wait(c, sequence, &error)) : nullptr;
  if (error || !reply) {
    std::free(error); std::free(reply);
    diagnostic = "Vulkan X11 WSI XFIXES QueryVersion failed"; return false;
  }
  const bool validVersion = reply->response == 1 && reply->length == 0 &&
    (reply->major < 6 || (reply->major == 6 && reply->minor == 0));
  const bool regions = validVersion && reply->major >= 2;
  std::free(reply);
  if (!regions) diagnostic = validVersion ? "Vulkan X11 WSI requires XFIXES version 2 for region requests"
                                         : "Vulkan X11 WSI received an invalid XFIXES version reply";
  return regions;
}
}
#endif
#endif
