#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include "rendering/coinwgpu/CoinWgpuFfi.h"

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

constexpr uint32_t kSide = 16;
constexpr int kSetupFailure = 70;
constexpr int kExitOrderFailure = 71;

int g_traceFd = -1;
CoinWgpuDeviceId g_devices[2] = {};
uint64_t g_tickets[2] = {};
uint64_t g_rttTokens[2] = {};

[[noreturn]] void failAtExit(int code, const char * message) {
  std::fprintf(stderr, "CoinWgpuExitShutdownTest: %s\n", message);
  std::fflush(stderr);
  std::_Exit(code);
}

bool writeTrace(char value) {
  const char * cursor = &value;
  size_t remaining = 1;
  while (remaining != 0) {
    const ssize_t written = ::write(g_traceFd, cursor, remaining);
    if (written > 0) {
      cursor += written;
      remaining -= static_cast<size_t>(written);
      continue;
    }
    if (written < 0 && errno == EINTR) continue;
    return false;
  }
  return true;
}

bool bridgeIsEmpty() {
  uint64_t jobs = UINT64_MAX, bytes = UINT64_MAX;
  if (!coin_wgpu_readback_resource_load(&jobs, &bytes) || jobs != 0 || bytes != 0)
    return false;

  uint64_t active = UINT64_MAX, retired = UINT64_MAX;
  coin_wgpu_rtt_resource_counts(&active, &retired);
  if (active != 0 || retired != 0) return false;

  char error[256] = {};
  for (uint64_t token : g_tickets) {
    if (token == 0 || coin_wgpu_readback_query(token, error, sizeof(error)) !=
                        COIN_WGPU_INVALID_ARGUMENT)
      return false;
  }
  for (CoinWgpuDeviceId device : g_devices) {
    if (device == 0 || coin_wgpu_device_destroy(device) != COIN_WGPU_INVALID_ARGUMENT)
      return false;
  }
  return true;
}

// This handler stands in for a graphics-driver exit handler. It is registered
// before the first runtime touch, so Coin's process-shutdown hook must be
// registered later and must have removed every bridge-owned device/resource
// before control reaches this point.
void driverExitSentinel() {
  if (!bridgeIsEmpty())
    failAtExit(kExitOrderFailure, "runtime was still live at the driver exit sentinel");

  // The existing reset API remains safe after automatic shutdown, including
  // repeated calls, and must not recreate an empty runtime.
  coin_wgpu_reset_context();
  coin_wgpu_reset_context();
  if (!bridgeIsEmpty())
    failAtExit(kExitOrderFailure, "post-shutdown reset was not idempotent");
  if (!writeTrace('C'))
    failAtExit(kExitOrderFailure, "could not record the clean exit phase");
}

// Registered only after establishWorkload synchronously proves that all
// resources are live. LIFO exit order requires this marker to run before
// Coin's shutdown hook. It deliberately avoids Rust/wgpu calls after main has
// returned because the process may already have dismantled thread-local state.
void liveResourceSentinel() {
  if (!writeTrace('L'))
    failAtExit(kExitOrderFailure, "could not record the live exit phase");
}

CoinWgpuFrameView clearFrame(uint32_t passes, float red, float green, float blue) {
  CoinWgpuFrameView frame{};
  frame.abi_version = COIN_WGPU_ABI_VERSION;
  frame.struct_size = sizeof(frame);
  frame.width = kSide;
  frame.height = kSide;
  frame.clear_color[0] = red;
  frame.clear_color[1] = green;
  frame.clear_color[2] = blue;
  frame.clear_color[3] = 1.0f;
  frame.multipass_count = passes;
  frame.multipass_reserved = 0;
  frame.multipass_budget_bytes = uint64_t(256) * 1024 * 1024;
  frame.multipass_matrices = nullptr;
  frame.multipass_matrix_count = 0;
  return frame;
}

CoinWgpuTarget targetFor(CoinWgpuDeviceId device, uint32_t samples,
                         std::vector<uint8_t> * color = nullptr) {
  CoinWgpuTarget target{};
  target.width = kSide;
  target.height = kSide;
  target.device_id = device;
  target.sample_count = samples;
  if (color) {
    target.color_buffer = color->data();
    target.color_buffer_len = color->size();
  }
  return target;
}

bool submitFrame(CoinWgpuDeviceId device, uint32_t samples,
                 const CoinWgpuFrameView & frame) {
  std::vector<uint8_t> color(kSide * kSide * 4u, 0x5a);
  CoinWgpuTarget target = targetFor(device, samples, &color);
  char error[512] = {};
  const CoinWgpuStatus status =
    coin_wgpu_submit(&target, &frame, error, sizeof(error));
  if (status == COIN_WGPU_OK && target.submission_serial != 0) return true;
  std::fprintf(stderr, "CoinWgpuExitShutdownTest: submit failed: %s\n", error);
  return false;
}

bool createRtt(unsigned slot, CoinWgpuDeviceId device, uint32_t samples,
               const CoinWgpuFrameView & frame) {
  CoinWgpuTarget target = targetFor(device, samples);
  char error[512] = {};
  g_rttTokens[slot] = 0;
  const CoinWgpuStatus status = coin_wgpu_submit_texture(
    &target, &frame, &g_rttTokens[slot], error, sizeof(error));
  if (status == COIN_WGPU_OK && g_rttTokens[slot] != 0) return true;
  std::fprintf(stderr, "CoinWgpuExitShutdownTest: RTT submit failed: %s\n", error);
  return false;
}

bool createReadback(unsigned slot, CoinWgpuDeviceId device, uint32_t samples,
                    const CoinWgpuFrameView & frame) {
  CoinWgpuTarget target = targetFor(device, samples);
  CoinWgpuReadbackTicket ticket{};
  ticket.abi_version = COIN_WGPU_ABI_VERSION;
  ticket.struct_size = sizeof(ticket);
  char error[512] = {};
  const CoinWgpuStatus status = coin_wgpu_submit_async(
    &target, &frame, &ticket, error, sizeof(error));
  if (status == COIN_WGPU_OK && ticket.token != 0 &&
      ticket.submission_serial == target.submission_serial) {
    g_tickets[slot] = ticket.token;
    return true;
  }
  std::fprintf(stderr, "CoinWgpuExitShutdownTest: async submit failed: %s\n", error);
  return false;
}

bool createDevice(unsigned slot) {
  char error[512] = {};
  const CoinWgpuStatus status =
    coin_wgpu_device_create(&g_devices[slot], error, sizeof(error));
  if (status == COIN_WGPU_OK && g_devices[slot] != 0) return true;
  std::fprintf(stderr, "CoinWgpuExitShutdownTest: device create failed: %s\n", error);
  return false;
}

bool establishWorkload(bool resetAndReinitialize) {
  if (resetAndReinitialize) {
    coin_wgpu_reset_context();
    coin_wgpu_reset_context();
    if (!coin_wgpu_is_available()) return false;
  }

  if (!createDevice(0) || !createDevice(1)) return false;

  const CoinWgpuFrameView onePass = clearFrame(1, 0.25f, 0.5f, 0.75f);
  const CoinWgpuFrameView sevenPass = clearFrame(7, 0.75f, 0.5f, 0.25f);

  // Three independent targets exercise the default device and two explicit
  // devices. N=1 and the incident's MSAA4/N=7 combination remain ordinary
  // successful work before the process-lifetime probes take over.
  if (!submitFrame(0, 1, onePass) ||
      !submitFrame(g_devices[0], 4, sevenPass) ||
      !submitFrame(g_devices[1], 1, onePass))
    return false;

  if (!createRtt(0, 0, 1, onePass) ||
      !createRtt(1, g_devices[0], 4, sevenPass) ||
      !createReadback(0, 0, 1, sevenPass) ||
      !createReadback(1, g_devices[1], 1, onePass))
    return false;

  uint64_t active = 0, retired = 0, jobs = 0, bytes = 0;
  coin_wgpu_rtt_resource_counts(&active, &retired);
  return active >= 2 && coin_wgpu_readback_resource_load(&jobs, &bytes) &&
         jobs >= 2 && bytes != 0;
}

int childMain(int traceFd, bool resetAndReinitialize) {
  g_traceFd = traceFd;
  if (std::atexit(driverExitSentinel) != 0)
    return kSetupFailure;

  // Availability performs the first runtime/device initialization. A machine
  // without a usable adapter skips before asserting GPU lifecycle behavior.
  if (!coin_wgpu_is_available()) std::_Exit(77);
  if (!establishWorkload(resetAndReinitialize)) return kSetupFailure;
  if (std::atexit(liveResourceSentinel) != 0) return kSetupFailure;
  return 0;
}

int controllerMain(const char * executable, bool resetAndReinitialize) {
  int fds[2] = {-1, -1};
  if (::pipe(fds) != 0) {
    std::perror("CoinWgpuExitShutdownTest: pipe");
    return 1;
  }

  const pid_t child = ::fork();
  if (child < 0) {
    std::perror("CoinWgpuExitShutdownTest: fork");
    ::close(fds[0]);
    ::close(fds[1]);
    return 1;
  }
  if (child == 0) {
    ::close(fds[0]);
    char fd[32] = {};
    std::snprintf(fd, sizeof(fd), "%d", fds[1]);
    ::execlp(executable, executable, "--child", fd,
             resetAndReinitialize ? "reset-reinit" : "normal",
             static_cast<char *>(nullptr));
    std::perror("CoinWgpuExitShutdownTest: exec");
    std::_Exit(127);
  }

  ::close(fds[1]);
  std::string trace;
  char buffer[16];
  for (;;) {
    const ssize_t count = ::read(fds[0], buffer, sizeof(buffer));
    if (count > 0) {
      trace.append(buffer, static_cast<size_t>(count));
      continue;
    }
    if (count < 0 && errno == EINTR) continue;
    if (count < 0) std::perror("CoinWgpuExitShutdownTest: read");
    break;
  }
  ::close(fds[0]);

  int status = 0;
  while (::waitpid(child, &status, 0) < 0) {
    if (errno == EINTR) continue;
    std::perror("CoinWgpuExitShutdownTest: waitpid");
    return 1;
  }
  if (WIFEXITED(status) && WEXITSTATUS(status) == 77) return 77;
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0 || trace != "LC") {
    std::fprintf(stderr,
      "CoinWgpuExitShutdownTest: child status=%d trace='%s', expected clean LC shutdown\n",
      status, trace.c_str());
    return 1;
  }
  return 0;
}

} // namespace

int main(int argc, char ** argv) {
  if (argc == 4 && std::strcmp(argv[1], "--child") == 0) {
    char * end = nullptr;
    const long fd = std::strtol(argv[2], &end, 10);
    if (!end || *end != '\0' || fd < 0) return 2;
    return childMain(static_cast<int>(fd),
                     std::strcmp(argv[3], "reset-reinit") == 0);
  }
  const bool resetAndReinitialize =
    argc == 2 && std::strcmp(argv[1], "--reset-reinit") == 0;
  if (argc > 2 || (argc == 2 && !resetAndReinitialize)) return 2;
  return controllerMain(argv[0], resetAndReinitialize);
}
