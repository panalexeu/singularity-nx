// singularity-nx: profile collection for profile-guided optimisation (PGO). From renut_nx_ultimate_base's
// app/src/banjo_pgo.cpp (sa2-nx's, deadpool-nx's, same project family); only the names differ.
//
// The instrumented build (tools/switch/build-native.sh with PGO=generate) counts every branch of the recompiled game,
// this app and the SDK runtime. GCC's usual way to save the counts (libgcov writing a .gcda file per object at exit,
// at the build machine's paths) does not fit the Switch: the game is often closed from the HOME menu without exiting,
// and those paths do not exist there. So the build registers each object's counts in a section
// (-fprofile-info-section=gcov_info) and this file streams them all into one file, pgo/singularity_pgo_<N>.stream next
// to the NRO (N = this session):
//   - every 2 minutes and at exit;
//   - the whole session each time (the counts only grow), replacing the previous dump through a .tmp file, so a dump
//     cut short (console switched off) leaves the previous one.
// On the PC, tools/switch/pgo_merge.sh turns the stream into .gcda files in pgo/ (gcov-tool merge-stream), and the
// build with PGO=use reads them.

#include "singularity_pgo.h"

#if defined(SINGULARITY_PGO_GENERATE)

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

extern "C" {  // GCC's gcov.h has no C++ guard of its own
#include <gcov.h>
}

#include <rex/filesystem.h>
#include <rex/logging.h>

// One pointer per instrumented object, in the section GNU ld brackets with these two symbols.
extern "C" const struct gcov_info* const __start_gcov_info[];
extern "C" const struct gcov_info* const __stop_gcov_info[];

namespace singularity::pgo {
namespace {

constexpr auto kPeriod = std::chrono::minutes(2);

std::mutex g_dump_mutex;  // one dump at a time (the thread, exit)
std::filesystem::path g_file;
std::mutex g_thread_mutex;
std::condition_variable g_wake;
bool g_stop = false;
std::thread g_thread;

struct Stream {
  std::FILE* file = nullptr;
  bool ok = true;
  std::vector<std::unique_ptr<unsigned char[]>> allocations;  // libgcov's scratch buffers for one dump
};

void Write(const void* data, unsigned length, void* arg) {
  auto* s = static_cast<Stream*>(arg);
  if (s->ok && length && std::fwrite(data, 1, length, s->file) != length) s->ok = false;
}

void Filename(const char* name, void* arg) {
  __gcov_filename_to_gcfn(name, Write, arg);
}

void* Allocate(unsigned length, void* arg) {
  auto* s = static_cast<Stream*>(arg);
  s->allocations.push_back(std::make_unique<unsigned char[]>(length ? length : 1));
  return s->allocations.back().get();
}

}  // namespace

void DumpNow() {
  std::lock_guard<std::mutex> lock(g_dump_mutex);
  if (g_file.empty()) return;
  std::filesystem::path temporary = g_file;
  temporary += ".tmp";
  Stream stream;
  stream.file = std::fopen(temporary.string().c_str(), "wb");
  if (!stream.file) return;
  static std::vector<char> buffer(1 << 20);  // one SD write per MB instead of per record
  std::setvbuf(stream.file, buffer.data(), _IOFBF, buffer.size());
  const gcov_info* const* info = __start_gcov_info;
  const gcov_info* const* const end = __stop_gcov_info;
  __asm__("" : "+r"(info));  // as in GCC's manual: the compiler must not reason about the linker's symbols
  size_t objetos = 0;
  for (; info != end && stream.ok; ++info) {
    if (*info) {
      __gcov_info_to_gcda(*info, Filename, Write, Allocate, &stream);
      ++objetos;
    }
  }
  const bool closed = std::fclose(stream.file) == 0;
  if (!stream.ok || !closed) return;  // the previous complete dump stays
  std::error_code ec;
  std::filesystem::rename(temporary, g_file, ec);
  if (ec) {  // a file system that does not replace on rename
    std::filesystem::remove(g_file, ec);
    std::filesystem::rename(temporary, g_file, ec);
  }
  REXLOG_INFO("[pgo] {} objects' counts written to {}", objetos, g_file.filename().string());
}

void Start() {
  const std::filesystem::path folder = rex::filesystem::GetExecutableFolder() / "pgo";
  std::error_code ec;
  std::filesystem::create_directories(folder, ec);
  for (int n = 1; n < 100000; ++n) {
    const std::filesystem::path candidate = folder / ("singularity_pgo_" + std::to_string(n) + ".stream");
    std::filesystem::path temporary = candidate;
    temporary += ".tmp";
    if (!std::filesystem::exists(candidate, ec) && !std::filesystem::exists(temporary, ec)) {
      g_file = candidate;
      break;
    }
  }
  if (g_file.empty()) return;
  REXLOG_INFO("[pgo] instrumented build: counts to {} every 2 minutes and at exit", g_file.filename().string());
  g_thread = std::thread([] {
    std::unique_lock<std::mutex> lock(g_thread_mutex);
    while (!g_wake.wait_for(lock, kPeriod, [] { return g_stop; })) {
      lock.unlock();
      DumpNow();
      lock.lock();
    }
  });
}

void Stop() {
  {
    std::lock_guard<std::mutex> lock(g_thread_mutex);
    g_stop = true;
  }
  g_wake.notify_all();
  if (g_thread.joinable()) g_thread.join();
  DumpNow();
}

}  // namespace singularity::pgo

#else  // not the instrumented build

namespace singularity::pgo {
void Start() {}
void DumpNow() {}
void Stop() {}
}  // namespace singularity::pgo

#endif
