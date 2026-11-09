// singularity-recomp - Singularity (Xbox 360, title 415607EF) on the Nintendo Switch, native deko3d renderer.
// From gears2-nx's gow2/src/gow2_app.h (GPL-3.0).
//
// Where the game is, next to the NRO (sdmc:/switch/singularity/ on the console):
//   singularity-extract/   the extracted game (default.xex and RvGame/)
//   singularity_shaders.dslib, shadercache_dksh.bin   the shader library (tools/switch/shaders/build_library.sh)

#pragma once

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/rex_app.h>
#include <rex/runtime.h>
#include <rex/system.h>
#include <rex/system/xmemory.h>
#include <rex/system/kernel_state.h>
#include <rex/ui/flags.h>
#include <rex/x360/xenos.h>

#include <algorithm>
#include <cstdlib>
#include <vector>
#include <iterator>
#include <fstream>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>

#if defined(__SWITCH__)
#include <switch.h>
#include <rex/ui/switch_apm.h>
#endif

#include "singularity_cpu.h"
#include "singularity_gpu.h"

REXCVAR_DECLARE(int32_t, singularity_resolution);

REXCVAR_DEFINE_STRING(singularity_game, "", "Singularity",
                      "Game folder (default.xex and RvGame/). Empty: singularity-extract/ next to the executable.");

// Handheld GPU speed requested from the system (0 = leave it, 384, 460). Official Horizon performance modes, not an
// overclock; homebrew starts at 307.2 MHz. Uses more battery. The profiler's "relojes" line shows the real clock.
REXCVAR_DEFINE_INT32(singularity_switch_gpu_mhz, 460, "Singularity",
                     "Handheld GPU MHz requested from the system (0 = leave it, 384, 460)")
    .allowed({"0", "384", "460"});

// Handheld CPU clock (0 = leave it, 1224, 1581, 1785). Set through clkrst, as sys-clk does, and set again every
// second if the system puts it back (apm reapplies its configuration). 1785 MHz is the console's own boost clock
// (loading screens of official games), the ceiling here: no higher, no voltage change. Uses more battery.
REXCVAR_DEFINE_INT32(singularity_switch_cpu_mhz, 0, "Singularity",
                     "Handheld CPU MHz kept by the NRO (0 = leave it at 1020, 1224, 1581, 1785)")
    .allowed({"0", "1224", "1581", "1785"});

class SingularityApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<SingularityApp>(new SingularityApp(ctx, "singularity", PPCImageConfig));
  }

 protected:
  // No Xenos emulation: singularity_gpu.cpp reads the game's command ring and draws with the deko3d renderer.
  void OnPreSetup(rex::RuntimeConfig& config) override {
    if (!config.graphics) config.graphics = singularity::gpu::CreateGraphicsSystem();
#if defined(__SWITCH__)
    // libnx's BSD sockets: Unreal Engine 3 opens its network layer even for single player (Gears of War 2 needed
    // them to load the campaign).
    const Result rc = socketInitializeDefault();
    if (R_FAILED(rc)) {
      REXLOG_WARN("singularity: socketInitializeDefault failed: 0x{:X}", rc);
    } else {
      REXLOG_INFO("singularity: sockets ready (libnx BSD)");
    }
#endif
  }

  // game_data_root is the folder with default.xex (ConstructRuntime checks it).
  void OnConfigurePaths(rex::PathConfig& paths) override {
    if (!paths.game_data_root.empty()) return;
    const std::string flag = REXCVAR_GET(singularity_game);
    if (!flag.empty()) {
      paths.game_data_root = flag;
      return;
    }
    const auto folder = rex::filesystem::GetExecutableFolder();
    if (!folder.empty()) paths.game_data_root = folder / "singularity-extract";
  }

  void OnPostInitLogging() override { singularity::cpu::InformarSistema(); }

  void OnLoadXexImage(std::string& xex_image) override {
    std::error_code ec;
    const auto root = game_data_root();
    if (!std::filesystem::is_regular_file(root / "default.xex", ec) ||
        !std::filesystem::is_directory(root / "RvGame", ec)) {
      Fail(fmt::format("Singularity: default.xex or RvGame/ is missing from {}. Put the extracted game in "
                       "singularity-extract/ next to the NRO.",
                       root.string()));
    }
    REXLOG_INFO("singularity: game at {}; loading {}", root.string(), xex_image);
  }

  void OnPreLaunchModule() override {
    // The 540p scene needs the video mode at 960x540 too, before the game asks for it (gears2-nx 1.1.0): the UI and
    // back buffer are sized from it, the scene from game patch 3 (singularity_game_patches.cpp).
    if (REXCVAR_GET(singularity_resolution) == 540) {
      REXCVAR_SET(video_mode_width, 960);
      REXCVAR_SET(video_mode_height, 540);
      // The renderer tells the scene from other targets by its width (GPU time categories, the deferred sky).
      rex::x360::g_ancho_escena.store(960, std::memory_order_relaxed);
    }
    REXLOG_INFO("singularity: scene {}p", REXCVAR_GET(singularity_resolution));
    singularity::cpu::Configurar();
    RehashConfigFiles();
  }

  void OnPostSetup() override {
#if defined(__SWITCH__)
    RexSwitchApmPedirGpuMhz(REXCVAR_GET(singularity_switch_gpu_mhz));
    RexSwitchApmAplicar();
    const int32_t cpu_mhz = REXCVAR_GET(singularity_switch_cpu_mhz);
    if (cpu_mhz > 0) {
      // A libnx thread: std::thread's detach is not implemented by devkitA64's libstdc++ (it ended the program).
      static uint32_t hz;
      static Thread hilo;
      hz = uint32_t(cpu_mhz) * 1000000u;
      const Result rc = threadCreate(
          &hilo, [](void*) { MantenerRelojCpu(hz); }, nullptr, nullptr, 0x4000, 0x2C, -2);
      if (R_SUCCEEDED(rc)) {
        threadStart(&hilo);
      } else {
        REXLOG_WARN("singularity: CPU clock thread not created: 0x{:X}", rc);
      }
    }
#endif
  }

 private:
  // Keeps the CPU at hz: set it through clkrst when it is lower (apm reapplies its configuration), once a second.
  static void MantenerRelojCpu(uint32_t hz) {
    uint32_t corrections = 0;
    for (;;) {
      if (R_SUCCEEDED(clkrstInitialize())) {
        ClkrstSession session;
        if (R_SUCCEEDED(clkrstOpenSession(&session, PcvModuleId_CpuBus, 3))) {
          uint32_t now = 0;
          clkrstGetClockRate(&session, &now);
          if (now + 1000000u < hz) {
            const Result rc = clkrstSetClockRate(&session, hz);
            uint32_t after = 0;
            clkrstGetClockRate(&session, &after);
            if (corrections++ < 8) {
              REXLOG_INFO("singularity: CPU clock {} MHz -> asked {} MHz: rc 0x{:X}, now {} MHz", now / 1000000,
                          hz / 1000000, rc, after / 1000000);
            }
          }
          clkrstCloseSession(&session);
        }
        clkrstExit();
      }
      svcSleepThread(1'000'000'000LL);
    }
  }

  // The game checks its config files (RvGame/CookedXenon/Coalesced_<LANG>.bin) against SHA-1 hashes in its own
  // image: a table of "coalesced_int.bin\0" + 20 bytes near its end (0x83A02E80 for coalesced_int.bin). An edited
  // file gave "Disc Read Error". Each entry's hash is replaced by the hash of the file on the SD card before the game
  // starts, so the config can be edited (e.g. [SystemSettings] ScreenPercentage).
  void RehashConfigFiles() {
#if defined(__SWITCH__)
    auto* memory = rex::system::kernel_memory();
    if (!memory) return;
    constexpr uint32_t kBegin = 0x839F0000u, kEnd = 0x83A10000u;
    uint8_t* image = memory->TranslateVirtual<uint8_t*>(kBegin);
    const size_t size = kEnd - kBegin;
    static constexpr char kPrefix[] = "coalesced_";
    for (size_t i = 0; i + sizeof(kPrefix) < size; ++i) {
      if (std::memcmp(image + i, kPrefix, sizeof(kPrefix) - 1) != 0) continue;
      const char* name = reinterpret_cast<const char*>(image + i);
      const size_t len = strnlen(name, std::min<size_t>(64, size - i));
      if (len < 14 || len >= 64 || std::strcmp(name + len - 4, ".bin") != 0 || i + len + 1 + 20 > size) continue;
      uint8_t* stored = image + i + len + 1;
      const auto path = game_data_root() / "RvGame" / "CookedXenon" / std::string(name, len);
      std::ifstream file(path, std::ios::binary);
      if (!file) continue;
      std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
      uint8_t hash[SHA1_HASH_SIZE];
      sha1CalculateHash(hash, data.data(), data.size());
      if (std::memcmp(hash, stored, SHA1_HASH_SIZE) != 0) {
        std::memcpy(stored, hash, SHA1_HASH_SIZE);
        REXLOG_INFO("singularity: {} edited ({} bytes): its hash in the image (at {:08X}) updated", name, data.size(),
                    uint32_t(kBegin + i + len + 1));
      }
      i += len;
    }
#endif
  }

  [[noreturn]] static void Fail(const std::string& msg) {
    REXLOG_ERROR("{}", msg);
    rex::ShowSimpleMessageBox(rex::SimpleMessageBoxType::Error, msg);
    rex::FlushLogging();
    std::_Exit(1);
  }
};
