#include "CoinBgfxShaderCompiler.h"
#include "CoinBgfxShaderCompilerConfig.h"
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <vector>
#ifndef _WIN32
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {
std::string configured(const char *variable, const char *fallback) {
  const char *value = std::getenv(variable);
  return value && *value ? value : fallback;
}
bool fileExists(const std::string &path) {
  std::ifstream stream(path.c_str(), std::ios::binary);
  return stream.good();
}
#ifndef _WIN32
struct TemporaryFiles {
  std::string directory;
  ~TemporaryFiles() {
    if (directory.empty()) return;
    for (const char *file : {"fragment.sc", "coin_portable_module.sh", "fragment.bin", "compiler.log"})
      ::unlink((directory + "/" + file).c_str());
    ::rmdir(directory.c_str());
  }
};
bool writeFile(const std::string &path, const std::string &contents) {
  std::ofstream out(path.c_str(), std::ios::binary);
  out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
  out.close();
  return !out.fail();
}
#endif
}

bool coin_bgfx_compile_portable_fragment(const CoinRenderShaderIr &ir,
  const std::string &fragmentTemplate, const std::string &profile,
  const std::string &defines, std::vector<uint8_t> &output, std::string &error)
{
#ifdef _WIN32
  error = "Portable BGFX runtime compiler process support is unavailable on this platform";
  return false;
#else
  // Template and compiler profile come from backend enums, never author input.
  if (fragmentTemplate.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_./") != std::string::npos ||
      fragmentTemplate.find("..") != std::string::npos ||
      (profile != "spirv" && profile != "330" && profile != "430")) {
    error = "Invalid portable BGFX compiler profile";
    return false;
  }
  std::string assets = configured("COIN_RENDER_BGFX_SHADER_ASSETS", COIN_RENDER_BGFX_SHADER_ASSETS);
  if (!std::getenv("COIN_RENDER_BGFX_SHADER_ASSETS") && !fileExists(assets + "/" + fragmentTemplate))
    assets = COIN_RENDER_BGFX_BUILD_SHADER_ASSETS;
  std::string includes = configured("COIN_RENDER_BGFX_SHADER_INCLUDES", COIN_RENDER_BGFX_SHADER_INCLUDES);
  if (!std::getenv("COIN_RENDER_BGFX_SHADER_INCLUDES") && !fileExists(includes + "/bgfx_shader.sh"))
    includes = COIN_RENDER_BGFX_BUILD_SHADER_INCLUDES;
  if (!fileExists(assets + "/" + fragmentTemplate) || !fileExists(includes + "/bgfx_shader.sh")) {
    error = "Portable BGFX shader templates or SDK includes are unavailable";
    return false;
  }
  std::string compiler = configured("COIN_RENDER_BGFX_SHADERC", COIN_RENDER_BGFX_SHADERC);
  if (!std::getenv("COIN_RENDER_BGFX_SHADERC") && !fileExists(compiler))
    compiler = COIN_RENDER_BGFX_BUILD_SHADERC;
  std::string tempRoot = configured("TMPDIR", "/tmp") + "/coin-portable-shader-XXXXXX";
  std::vector<char> temporary(tempRoot.begin(), tempRoot.end());
  temporary.push_back(0);
  const char *created = ::mkdtemp(temporary.data());
  if (!created) { error = "Cannot create private portable shader compiler directory"; return false; }
  TemporaryFiles files;
  files.directory = created;
  std::ostringstream helpers;
  helpers << "uniform vec4 u_portable0[128];\nuniform vec4 u_portable1[128];\n"
             "float coinPortableUniform(int i) { return i < 512 ? u_portable0[i / 4][i % 4] : u_portable1[(i - 512) / 4][i % 4]; }\n";
  for (unsigned unit = 0; unit < 8; ++unit) {
    if (!(ir.textureMask & (1u << unit))) continue;
    helpers << "vec4 coinPortableSample" << unit << "(vec4 coord) { return coinSurfaceSample(s_texColor";
    if (unit) helpers << unit;
    helpers << ", coord.xyz, coord.w, u_texParams[" << unit << "], " << unit << "); }\n";
  }
  std::string function = coin_render_shader_function(ir, false);
  const size_t body = function.find("{\n");
  if (body == std::string::npos) { error = "Invalid portable Core function"; return false; }
  function.insert(body + 2,
    "if (u_portableProbe.x > 0.5) return vec4(0.0, 0.0, 0.0, u_portableProbe.y);\n");
  helpers << "uniform vec4 u_portableProbe;\n" << function;
  if (!writeFile(files.directory + "/coin_portable_module.sh", helpers.str()) ||
      !writeFile(files.directory + "/fragment.sc", "#define COIN_PORTABLE_MODULE 1\n#include \"" + fragmentTemplate + "\"\n")) {
    error = "Cannot write private portable shader compiler inputs";
    return false;
  }
  const std::string includePaths = files.directory + ";" + assets + ";" + includes;
  std::vector<std::string> arguments = {compiler, "-f", files.directory + "/fragment.sc",
    "-o", files.directory + "/fragment.bin", "--type", "fragment", "--platform", "linux",
    "--profile", profile, "--varyingdef", assets + "/varying.def.sc", "-i", includePaths};
  if (!defines.empty()) { arguments.push_back("--define"); arguments.push_back(defines); }
  std::vector<char *> argv;
  for (auto &argument : arguments) argv.push_back(&argument[0]);
  argv.push_back(nullptr);
  const std::string logPath = files.directory + "/compiler.log";
  const pid_t child = ::fork();
  if (child == 0) {
    const int log = ::open(logPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (log < 0) _exit(126);
    ::dup2(log, STDOUT_FILENO);
    ::dup2(log, STDERR_FILENO);
    ::close(log);
    ::execvp(argv[0], argv.data());
    _exit(127);
  }
  if (child < 0) { error = "Cannot start portable BGFX shader compiler"; return false; }
  int status = 0;
  pid_t waited;
  do { waited = ::waitpid(child, &status, 0); } while (waited < 0 && errno == EINTR);
  if (waited != child || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    error = "Portable BGFX shader compilation failed";
    // Keep diagnostics bounded; no authored source text is copied into errors.
    if (WIFEXITED(status) && WEXITSTATUS(status) == 127)
      error += ": configured shaderc executable is unavailable";
    return false;
  }
  std::ifstream binary((files.directory + "/fragment.bin").c_str(), std::ios::binary | std::ios::ate);
  const std::streamoff size = binary.tellg();
  if (!binary || size <= 0 || size > 16 * 1024 * 1024) {
    error = "Portable BGFX compiler produced an invalid shader binary";
    return false;
  }
  std::vector<uint8_t> bytes(static_cast<size_t>(size));
  binary.seekg(0);
  binary.read(reinterpret_cast<char *>(bytes.data()), size);
  if (!binary) { error = "Cannot read portable BGFX shader binary"; return false; }
  output.swap(bytes);
  error.clear();
  return true;
#endif
}
