#ifndef COIN_RENDER_TEXTURE2D_STARTUP_TRACE_H
#define COIN_RENDER_TEXTURE2D_STARTUP_TRACE_H
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <iostream>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>
namespace startup {
inline bool traceCheck(bool value, const std::string &label) {
  if (!value)
    std::cerr << "texture2d_startup FAIL " << label << std::endl;
  return value;
}
struct Config {
  unsigned samples = 1;
  bool canonical = false, portable = false, baseline = false;
};
class Environment {
  std::string name, saved;
  bool present;

public:
  Environment(const char *n, const char *v) : name(n) {
    const char *p = std::getenv(n);
    present = p;
    if (p)
      saved = p;
    if (v)
      setenv(n, v, 1);
    else
      unsetenv(n);
  }
  ~Environment() {
    if (present)
      setenv(name.c_str(), saved.c_str(), 1);
    else
      unsetenv(name.c_str());
  }
};
class Trace {
  int fd = -1, saved = -1;

public:
  bool start() {
    const char *dir = std::getenv("TMPDIR");
    std::string pattern =
        std::string(dir ? dir : "/dev/shm") + "/coin-texture2d-phase-XXXXXX";
    std::vector<char> path(pattern.begin(), pattern.end());
    path.push_back(0);
    fd = mkstemp(path.data());
    if (fd < 0)
      return false;
    unlink(path.data());
    std::fflush(stderr);
    std::cerr.flush();
    saved = dup(STDERR_FILENO);
    return saved >= 0 && dup2(fd, STDERR_FILENO) >= 0;
  }
  std::string stop() {
    std::fflush(stderr);
    std::cerr.flush();
    if (saved >= 0) {
      dup2(saved, STDERR_FILENO);
      close(saved);
      saved = -1;
    }
    std::string text;
    if (fd >= 0) {
      lseek(fd, 0, SEEK_SET);
      char bytes[4096];
      ssize_t n;
      while ((n = read(fd, bytes, sizeof(bytes))) > 0)
        text.append(bytes, size_t(n));
      close(fd);
      fd = -1;
    }
    std::cerr << text;
    return text;
  }
  ~Trace() {
    if (saved >= 0 || fd >= 0)
      stop();
  }
};
inline bool route(const std::string &trace, const Config &c, bool general) {
#ifdef COIN_TEXTURE2D_STARTUP_BGFX
  if (c.samples == 1) {
    const std::string wanted =
        std::string("texture2d=") + (general ? "0" : "1");
    bool found = false;
    std::istringstream input(trace);
    std::string line;
    while (std::getline(input, line))
      if (line.find("COIN_RENDER_PHASE bgfx_base_program ") !=
          std::string::npos) {
        found = true;
        if (line.find(wanted) == std::string::npos)
          return traceCheck(
              false, "actual 1x base-program family matches authorial frame");
      }
    return traceCheck(found, "existing 1x family trace exists");
  }
  bool found = false;
  std::istringstream input(trace);
  std::string line;
  while (std::getline(input, line))
    if (line.find("COIN_RENDER_PHASE bgfx_surface_depth_program ") !=
        std::string::npos) {
      found = true;
      const std::string family =
          std::string("family=") + (general ? "general" : "texture2d");
      const bool mode = line.find("mode=sample") != std::string::npos ||
                        line.find("mode=native") != std::string::npos;
      std::istringstream fields(line);
      std::string word;
      unsigned samples = 0;
      while (fields >> word)
        if (word.find("samples=") == 0)
          samples = unsigned(std::atoi(word.c_str() + 8));
      if (!traceCheck(
              line.find(family) != std::string::npos && mode &&
                  samples == c.samples,
              "actual submitted depth program family/mode/requested samples"))
        return false;
    }
  if (!found && c.baseline) {
    std::cout << "texture2d_startup baseline_old_depth_trace_absent=1\n";
    return true;
  }
  return traceCheck(found,
                    "candidate must expose actual submitted MSAA depth family");
#else
  (void)trace;
  (void)c;
  (void)general;
  return true;
#endif
}

} // namespace startup
#endif
