#include "gb/core/gba/libretro_core.hpp"

#include "gb/core/environment.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace gb::gba {

namespace {

enum : unsigned {
  RetroEnvSetPixelFormat = 10,
  RetroEnvGetSystemDirectory = 9,
  RetroEnvGetSaveDirectory = 31,
};

enum : unsigned {
  RetroPixelFormat0Rgb1555 = 0,
  RetroPixelFormatXrgb8888 = 1,
  RetroPixelFormatRgb565 = 2,
};

enum : unsigned {
  RetroDeviceJoypad = 1,
};

enum : unsigned {
  RetroJoypadB = 0,
  RetroJoypadSelect = 2,
  RetroJoypadStart = 3,
  RetroJoypadUp = 4,
  RetroJoypadDown = 5,
  RetroJoypadLeft = 6,
  RetroJoypadRight = 7,
  RetroJoypadA = 8,
  RetroJoypadL = 10,
  RetroJoypadR = 11,
};

enum : unsigned {
  RetroMemorySaveRam = 0,
  RetroMemoryRtc = 1,
};

struct RetroGameInfo {
  const char *path;
  const void *data;
  std::size_t size;
  const char *meta;
};

struct RetroSystemInfo {
  const char *library_name;
  const char *library_version;
  const char *valid_extensions;
  bool need_fullpath;
  bool block_extract;
};

struct RetroSystemAvInfo {
  struct {
    unsigned base_width;
    unsigned base_height;
    unsigned max_width;
    unsigned max_height;
    float aspect_ratio;
  } geometry;
  struct {
    double fps;
    double sample_rate;
  } timing;
};

using retro_environment_t = bool (*)(unsigned, void *);
using retro_video_refresh_t = void (*)(const void *, unsigned, unsigned,
                                       std::size_t);
using retro_audio_sample_t = void (*)(std::int16_t, std::int16_t);
using retro_audio_sample_batch_t = std::size_t (*)(const std::int16_t *,
                                                   std::size_t);
using retro_input_poll_t = void (*)();
using retro_input_state_t = std::int16_t (*)(unsigned, unsigned, unsigned,
                                             unsigned);

u16 rgb555ToRgb565(std::uint16_t pixel) {
  const u16 r5 = static_cast<u16>((pixel >> 10U) & 0x1FU);
  const u16 g5 = static_cast<u16>((pixel >> 5U) & 0x1FU);
  const u16 b5 = static_cast<u16>(pixel & 0x1FU);
  const u16 g6 = static_cast<u16>((g5 << 1U) | (g5 >> 4U));
  return static_cast<u16>((r5 << 11U) | (g6 << 5U) | b5);
}

u16 xrgb8888ToRgb565(std::uint32_t pixel) {
  const u16 r5 = static_cast<u16>(((pixel >> 16U) & 0xFFU) >> 3U);
  const u16 g6 = static_cast<u16>(((pixel >> 8U) & 0xFFU) >> 2U);
  const u16 b5 = static_cast<u16>((pixel & 0xFFU) >> 3U);
  return static_cast<u16>((r5 << 11U) | (g6 << 5U) | b5);
}

std::vector<u8> readFileBytes(const std::string &path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return {};
  }
  return std::vector<u8>(std::istreambuf_iterator<char>(in),
                         std::istreambuf_iterator<char>());
}

} // namespace

struct LibretroCore::Impl {
#if defined(_WIN32)
  HMODULE library = nullptr;
#else
  void *library = nullptr;
#endif
  using retro_init_t = void (*)();
  using retro_deinit_t = void (*)();
  using retro_api_version_t = unsigned (*)();
  using retro_get_system_info_t = void (*)(RetroSystemInfo *);
  using retro_get_system_av_info_t = void (*)(RetroSystemAvInfo *);
  using retro_set_environment_t = void (*)(retro_environment_t);
  using retro_set_video_refresh_t = void (*)(retro_video_refresh_t);
  using retro_set_audio_sample_t = void (*)(retro_audio_sample_t);
  using retro_set_audio_sample_batch_t = void (*)(retro_audio_sample_batch_t);
  using retro_set_input_poll_t = void (*)(retro_input_poll_t);
  using retro_set_input_state_t = void (*)(retro_input_state_t);
  using retro_load_game_t = bool (*)(const RetroGameInfo *);
  using retro_unload_game_t = void (*)();
  using retro_run_t = void (*)();
  using retro_get_memory_data_t = void *(*)(unsigned);
  using retro_get_memory_size_t = std::size_t (*)(unsigned);
  using retro_serialize_size_t = std::size_t (*)();
  using retro_serialize_t = bool (*)(void *, std::size_t);
  using retro_unserialize_t = bool (*)(const void *, std::size_t);

  retro_init_t retro_init = nullptr;
  retro_deinit_t retro_deinit = nullptr;
  retro_api_version_t retro_api_version = nullptr;
  retro_get_system_info_t retro_get_system_info = nullptr;
  retro_get_system_av_info_t retro_get_system_av_info = nullptr;
  retro_set_environment_t retro_set_environment = nullptr;
  retro_set_video_refresh_t retro_set_video_refresh = nullptr;
  retro_set_audio_sample_t retro_set_audio_sample = nullptr;
  retro_set_audio_sample_batch_t retro_set_audio_sample_batch = nullptr;
  retro_set_input_poll_t retro_set_input_poll = nullptr;
  retro_set_input_state_t retro_set_input_state = nullptr;
  retro_load_game_t retro_load_game = nullptr;
  retro_unload_game_t retro_unload_game = nullptr;
  retro_run_t retro_run = nullptr;
  retro_get_memory_data_t retro_get_memory_data = nullptr;
  retro_get_memory_size_t retro_get_memory_size = nullptr;
  retro_serialize_size_t retro_serialize_size = nullptr;
  retro_serialize_t retro_serialize = nullptr;
  retro_unserialize_t retro_unserialize = nullptr;

  std::array<u16, FramebufferSize> framebuffer{};
  std::vector<std::int16_t> samples;
  std::vector<u8> romData;
  InputState input{};
  std::string loadedRom;
  std::string loadedCore;
  std::string coreName = "libretro";
  std::string systemDirectory;
  std::string saveDirectory;
  unsigned pixelFormat = RetroPixelFormatRgb565;
  bool gameLoaded = false;
  bool initialized = false;

  static Impl *active;

  template <typename T> bool loadSymbol(T &out, const char *name) {
#if defined(_WIN32)
    out = reinterpret_cast<T>(GetProcAddress(library, name));
#else
    out = reinterpret_cast<T>(dlsym(library, name));
#endif
    if (out == nullptr) {
      std::cerr << "core libretro sem simbolo obrigatorio: " << name << "\n";
      return false;
    }
    return true;
  }

  template <typename T> void loadOptionalSymbol(T &out, const char *name) {
#if defined(_WIN32)
    out = reinterpret_cast<T>(GetProcAddress(library, name));
#else
    out = reinterpret_cast<T>(dlsym(library, name));
#endif
  }

  bool loadLibrary(const std::string &path) {
#if defined(_WIN32)
    library = LoadLibraryA(path.c_str());
#else
    library = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
    if (library == nullptr) {
#if defined(_WIN32)
      std::cerr << "falha ao abrir core libretro: " << path << "\n";
#else
      std::cerr << "falha ao abrir core libretro: " << path << " (" << dlerror()
                << ")\n";
#endif
      return false;
    }

    return loadSymbol(retro_init, "retro_init") &&
           loadSymbol(retro_deinit, "retro_deinit") &&
           loadSymbol(retro_api_version, "retro_api_version") &&
           loadSymbol(retro_get_system_info, "retro_get_system_info") &&
           loadSymbol(retro_get_system_av_info, "retro_get_system_av_info") &&
           loadSymbol(retro_set_environment, "retro_set_environment") &&
           loadSymbol(retro_set_video_refresh, "retro_set_video_refresh") &&
           loadSymbol(retro_set_audio_sample, "retro_set_audio_sample") &&
           loadSymbol(retro_set_audio_sample_batch,
                      "retro_set_audio_sample_batch") &&
           loadSymbol(retro_set_input_poll, "retro_set_input_poll") &&
           loadSymbol(retro_set_input_state, "retro_set_input_state") &&
           loadSymbol(retro_load_game, "retro_load_game") &&
           loadSymbol(retro_unload_game, "retro_unload_game") &&
           loadSymbol(retro_run, "retro_run") &&
           loadSymbol(retro_get_memory_data, "retro_get_memory_data") &&
           loadSymbol(retro_get_memory_size, "retro_get_memory_size");
  }

  void closeLibrary() {
    if (library == nullptr) {
      return;
    }
#if defined(_WIN32)
    FreeLibrary(library);
#else
    dlclose(library);
#endif
    library = nullptr;
  }

  static bool environment(unsigned command, void *data) {
    if (active == nullptr) {
      return false;
    }
    switch (command) {
    case RetroEnvSetPixelFormat:
      active->pixelFormat = *static_cast<unsigned *>(data);
      return active->pixelFormat == RetroPixelFormatRgb565 ||
             active->pixelFormat == RetroPixelFormatXrgb8888 ||
             active->pixelFormat == RetroPixelFormat0Rgb1555;
    case RetroEnvGetSystemDirectory:
      *static_cast<const char **>(data) = active->systemDirectory.c_str();
      return true;
    case RetroEnvGetSaveDirectory:
      *static_cast<const char **>(data) = active->saveDirectory.c_str();
      return true;
    default:
      return false;
    }
  }

  static void videoRefresh(const void *data, unsigned width, unsigned height,
                           std::size_t pitch) {
    if (active == nullptr || data == nullptr || width == 0U || height == 0U) {
      return;
    }
    const unsigned copyW = std::min<unsigned>(width, ScreenWidth);
    const unsigned copyH = std::min<unsigned>(height, ScreenHeight);
    std::fill(active->framebuffer.begin(), active->framebuffer.end(), 0);

    const auto *bytes = static_cast<const std::uint8_t *>(data);
    for (unsigned y = 0; y < copyH; ++y) {
      const std::uint8_t *row = bytes + static_cast<std::size_t>(y) * pitch;
      for (unsigned x = 0; x < copyW; ++x) {
        u16 out = 0;
        if (active->pixelFormat == RetroPixelFormatXrgb8888) {
          std::uint32_t pixel = 0;
          std::memcpy(&pixel, row + static_cast<std::size_t>(x) * 4U,
                      sizeof(pixel));
          out = xrgb8888ToRgb565(pixel);
        } else {
          std::uint16_t pixel = 0;
          std::memcpy(&pixel, row + static_cast<std::size_t>(x) * 2U,
                      sizeof(pixel));
          out = active->pixelFormat == RetroPixelFormatRgb565
                    ? pixel
                    : rgb555ToRgb565(pixel);
        }
        active->framebuffer[static_cast<std::size_t>(y) * ScreenWidth + x] =
            out;
      }
    }
  }

  static void audioSample(std::int16_t left, std::int16_t right) {
    if (active == nullptr) {
      return;
    }
    active->samples.push_back(left);
    active->samples.push_back(right);
  }

  static std::size_t audioBatch(const std::int16_t *data, std::size_t frames) {
    if (active == nullptr || data == nullptr || frames == 0U) {
      return 0;
    }
    active->samples.insert(active->samples.end(), data, data + frames * 2U);
    return frames;
  }

  static void inputPoll() {}

  static std::int16_t inputState(unsigned, unsigned device, unsigned,
                                 unsigned id) {
    if (active == nullptr || device != RetroDeviceJoypad) {
      return 0;
    }
    const InputState &in = active->input;
    switch (id) {
    case RetroJoypadA:
      return in.a ? 1 : 0;
    case RetroJoypadB:
      return in.b ? 1 : 0;
    case RetroJoypadSelect:
      return in.select ? 1 : 0;
    case RetroJoypadStart:
      return in.start ? 1 : 0;
    case RetroJoypadUp:
      return in.up ? 1 : 0;
    case RetroJoypadDown:
      return in.down ? 1 : 0;
    case RetroJoypadLeft:
      return in.left ? 1 : 0;
    case RetroJoypadRight:
      return in.right ? 1 : 0;
    case RetroJoypadL:
      return in.l ? 1 : 0;
    case RetroJoypadR:
      return in.r ? 1 : 0;
    default:
      return 0;
    }
  }
};

LibretroCore::Impl *LibretroCore::Impl::active = nullptr;

LibretroCore::LibretroCore() : impl_(new Impl()) {}

LibretroCore::~LibretroCore() {
  unload();
  delete impl_;
}

bool LibretroCore::loadCore(const std::string &corePath) {
  unload();
  if (corePath.empty() || !std::filesystem::exists(corePath)) {
    std::cerr << "core libretro nao encontrado: " << corePath << "\n";
    return false;
  }
  if (!impl_->loadLibrary(corePath)) {
    impl_->closeLibrary();
    return false;
  }
  impl_->loadOptionalSymbol(impl_->retro_serialize_size,
                            "retro_serialize_size");
  impl_->loadOptionalSymbol(impl_->retro_serialize, "retro_serialize");
  impl_->loadOptionalSymbol(impl_->retro_unserialize, "retro_unserialize");

  Impl::active = impl_;
  impl_->systemDirectory =
      std::filesystem::path(corePath).parent_path().string();
  impl_->saveDirectory = std::filesystem::current_path().string();
  impl_->retro_set_environment(&Impl::environment);
  impl_->retro_set_video_refresh(&Impl::videoRefresh);
  impl_->retro_set_audio_sample(&Impl::audioSample);
  impl_->retro_set_audio_sample_batch(&Impl::audioBatch);
  impl_->retro_set_input_poll(&Impl::inputPoll);
  impl_->retro_set_input_state(&Impl::inputState);
  impl_->retro_init();
  impl_->initialized = true;
  impl_->loadedCore = corePath;

  RetroSystemInfo info{};
  impl_->retro_get_system_info(&info);
  if (info.library_name != nullptr) {
    impl_->coreName = info.library_name;
    if (info.library_version != nullptr) {
      impl_->coreName += " ";
      impl_->coreName += info.library_version;
    }
  }
  return true;
}

bool LibretroCore::loadRomFromFile(const std::string &romPath) {
  if (impl_->library == nullptr) {
    std::cerr << "nenhum core libretro carregado\n";
    return false;
  }
  impl_->romData = readFileBytes(romPath);
  if (impl_->romData.empty()) {
    std::cerr << "falha ao ler ROM GBA: " << romPath << "\n";
    return false;
  }

  RetroGameInfo game{};
  game.path = romPath.c_str();
  game.data = impl_->romData.data();
  game.size = impl_->romData.size();
  game.meta = nullptr;
  if (!impl_->retro_load_game(&game)) {
    std::cerr << "core libretro recusou a ROM: " << romPath << "\n";
    return false;
  }

  RetroSystemAvInfo av{};
  impl_->retro_get_system_av_info(&av);
  if (av.geometry.base_width != ScreenWidth ||
      av.geometry.base_height != ScreenHeight) {
    std::cerr << "aviso: core GBA reportou video " << av.geometry.base_width
              << "x" << av.geometry.base_height
              << ", ajustando para framebuffer 240x160\n";
  }

  impl_->loadedRom = romPath;
  impl_->gameLoaded = true;
  impl_->framebuffer.fill(0);
  impl_->samples.clear();
  return true;
}

void LibretroCore::unload() {
  if (impl_ == nullptr) {
    return;
  }
  if (impl_->gameLoaded && impl_->retro_unload_game != nullptr) {
    impl_->retro_unload_game();
    impl_->gameLoaded = false;
  }
  if (impl_->initialized && impl_->retro_deinit != nullptr) {
    impl_->retro_deinit();
    impl_->initialized = false;
  }
  if (Impl::active == impl_) {
    Impl::active = nullptr;
  }
  impl_->closeLibrary();
  impl_->loadedRom.clear();
  impl_->loadedCore.clear();
  impl_->romData.clear();
  impl_->samples.clear();
  impl_->framebuffer.fill(0);
}

void LibretroCore::setInputState(const InputState &input) {
  impl_->input = input;
}

void LibretroCore::runFrame() {
  if (impl_->gameLoaded && impl_->retro_run != nullptr) {
    Impl::active = impl_;
    impl_->retro_run();
  }
}

void LibretroCore::stepInstruction() {
}

const std::array<u16, LibretroCore::FramebufferSize> &
LibretroCore::framebuffer() const {
  return impl_->framebuffer;
}

std::vector<std::int16_t> LibretroCore::takeSamples() {
  std::vector<std::int16_t> out;
  out.swap(impl_->samples);
  impl_->samples.reserve(out.capacity());
  return out;
}

bool LibretroCore::debugAvailable() const {
  return false;
}

GbaDebugSnapshot LibretroCore::debugSnapshot() const {
  return {};
}

std::optional<u8> LibretroCore::debugRead8(u32) const {
  return std::nullopt;
}

std::optional<u16> LibretroCore::debugRead16(u32) const {
  return std::nullopt;
}

std::optional<u32> LibretroCore::debugRead32(u32) const {
  return std::nullopt;
}

bool LibretroCore::debugWrite8(u32, u8) {
  return false;
}

bool LibretroCore::debugWrite16(u32, u16) {
  return false;
}

bool LibretroCore::debugWrite32(u32, u32) {
  return false;
}

bool LibretroCore::loadBackupFromFile(const std::string &path) {
  if (!impl_->gameLoaded || impl_->retro_get_memory_data == nullptr ||
      impl_->retro_get_memory_size == nullptr) {
    return false;
  }
  void *data = impl_->retro_get_memory_data(RetroMemorySaveRam);
  const std::size_t size = impl_->retro_get_memory_size(RetroMemorySaveRam);
  if (data == nullptr || size == 0U || !std::filesystem::exists(path)) {
    return false;
  }
  const auto bytes = readFileBytes(path);
  if (bytes.empty()) {
    return false;
  }
  std::memcpy(data, bytes.data(), std::min<std::size_t>(bytes.size(), size));
  return true;
}

bool LibretroCore::saveBackupToFile(const std::string &path) const {
  if (!impl_->gameLoaded || impl_->retro_get_memory_data == nullptr ||
      impl_->retro_get_memory_size == nullptr) {
    return false;
  }
  const void *data = impl_->retro_get_memory_data(RetroMemorySaveRam);
  const std::size_t size = impl_->retro_get_memory_size(RetroMemorySaveRam);
  if (data == nullptr || size == 0U) {
    return false;
  }
  std::ofstream out(path, std::ios::binary);
  if (!out) {
    return false;
  }
  out.write(static_cast<const char *>(data),
            static_cast<std::streamsize>(size));
  return static_cast<bool>(out);
}

bool LibretroCore::loadStateFromFile(const std::string &path) {
  if (!impl_->gameLoaded || impl_->retro_unserialize == nullptr ||
      impl_->retro_serialize_size == nullptr || !std::filesystem::exists(path)) {
    return false;
  }
  const std::size_t expectedSize = impl_->retro_serialize_size();
  const auto bytes = readFileBytes(path);
  if (expectedSize == 0U || bytes.size() != expectedSize) {
    return false;
  }
  Impl::active = impl_;
  const bool loaded = impl_->retro_unserialize(bytes.data(), bytes.size());
  if (loaded) {
    impl_->samples.clear();
  }
  return loaded;
}

bool LibretroCore::saveStateToFile(const std::string &path) const {
  if (!impl_->gameLoaded || impl_->retro_serialize == nullptr ||
      impl_->retro_serialize_size == nullptr) {
    return false;
  }
  const std::size_t size = impl_->retro_serialize_size();
  if (size == 0U) {
    return false;
  }
  std::vector<u8> bytes(size);
  if (!impl_->retro_serialize(bytes.data(), bytes.size())) {
    return false;
  }
  const std::filesystem::path outPath(path);
  std::error_code ec;
  if (outPath.has_parent_path()) {
    std::filesystem::create_directories(outPath.parent_path(), ec);
  }
  std::ofstream out(path, std::ios::binary);
  if (!out) {
    return false;
  }
  out.write(reinterpret_cast<const char *>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
  return static_cast<bool>(out);
}

bool LibretroCore::loaded() const { return impl_->gameLoaded; }

const std::string &LibretroCore::loadedRomPath() const {
  return impl_->loadedRom;
}

const std::string &LibretroCore::corePath() const { return impl_->loadedCore; }

const std::string &LibretroCore::coreName() const { return impl_->coreName; }

std::string resolveLibretroGbaCorePath() {
  if (const auto configured =
          gb::readEnvironmentVariable("GBEMU_GBA_LIBRETRO_CORE")) {
    if (!configured->empty()) {
      return *configured;
    }
  }

  const std::vector<std::string> candidates = {
#if defined(__APPLE__)
      "/opt/homebrew/lib/libretro/mgba_libretro.dylib",
      "/usr/local/lib/libretro/mgba_libretro.dylib",
      "/Applications/OpenEmu.app/Contents/PlugIns/mGBA.oecoreplugin/Contents/"
      "Resources/mGBA/mgba_libretro.dylib",
#elif defined(_WIN32)
      "mgba_libretro.dll",
#else
      "/usr/lib/libretro/mgba_libretro.so",
      "/usr/local/lib/libretro/mgba_libretro.so",
#endif
  };
  for (const auto &candidate : candidates) {
    if (std::filesystem::exists(candidate)) {
      return candidate;
    }
  }
  return {};
}

} // namespace gb::gba
