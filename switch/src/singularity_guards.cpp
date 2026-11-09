// singularity-recomp - generic guards for the recompiled code (stack cut, main-thread report, register check).
// From gears2-nx's gow2/src/gow2_guards.cpp (GPL-3.0), without its Gears of War 2 hooks and UE3 name lookups:
// objects are logged by address only.
#include "singularity_pch.h"

#include <atomic>
#include <mutex>
#include <string>
#include <unordered_set>

#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>
#include <rex/system/xthread.h>

namespace {
// Singularity's image (startup log: image=82000000-83A10000): a pointer there in an object's first word is a vtable.
constexpr uint32_t kImageBegin = 0x82000000u;
constexpr uint32_t kImageEnd = 0x83A10000u;

bool LooksLikeObject(uint8_t* base, uint32_t value) {
  if (value < 0x40000000u || value >= 0xC0000000u || (value & 3)) return false;
  const uint32_t vtable = REX_LOAD_U32(value);
  return vtable >= kImageBegin && vtable < kImageEnd;
}

std::string Describe(uint8_t* base, uint32_t value) {
  return LooksLikeObject(base, value) ? fmt::format("{:08X} (vtable {:08X})", value, REX_LOAD_U32(value))
                                      : fmt::format("{:08X}", value);
}
}  // namespace

// The generic guard (make_nx_pch.py, point 5): any recompiled function entered with the guest stack within 32 KB of
// its limit lands here and returns at once with r3 = 0, so a runaway recursion anywhere ends in a cut instead of a
// crash. Logged once per function and call site (lr).
extern "C" void singularity_stack_cut(PPCContext& ctx, uint8_t* base, const char* function) {
  static std::atomic<uint32_t> cuts{0};
  static std::mutex mutex;
  static std::unordered_set<uint64_t> seen;
  const uint32_t n = cuts.fetch_add(1) + 1;
  const uint32_t lr = uint32_t(ctx.lr), object = ctx.r3.u32;
  bool fresh;
  {
    std::lock_guard<std::mutex> lock(mutex);
    fresh = seen.size() < 4096 && seen.insert((uint64_t(std::hash<const void*>{}(function)) << 32) ^ lr).second;
  }
  if (fresh || (n & (n - 1)) == 0) {
    REXLOG_WARN("[guard] stack cut #{} in {} (lr {:08X}, r1 {:08X}), r3 {}", n, function, lr, ctx.r1.u32,
                Describe(base, object));
  }
  ctx.r3.u64 = 0;
}

// Where the main guest thread is, for the 10 s statistics (singularity_gpu.cpp): its last call site (lr), stack
// pointer and r3, r30, r31. A hang shows up as the same place repeated.
void singularity_log_main_thread() {
  auto* kernel = rex::system::kernel_state();
  if (!kernel) return;
  for (auto& thread : kernel->object_table()->GetObjectsByType<rex::system::XThread>()) {
    if (!thread->main_thread() || !thread->thread_state()) continue;
    const PPCContext& ctx = *thread->thread_state()->context();
    uint8_t* base = kernel->memory()->virtual_membase();
    REXLOG_INFO("[main] lr {:08X} r1 {:08X} r3 {} r30 {} r31 {}", uint32_t(ctx.lr), ctx.r1.u32,
                Describe(base, ctx.r3.u32), Describe(base, ctx.r30.u32), Describe(base, ctx.r31.u32));
  }
}

// The check build of gears2-nx's patch_local_regs.py (not used here yet): kept so make_nx_pch.py's declaration links.
extern "C" void singularity_regcheck_fail(const char* function, const char* callee, int site, const char* regs,
                                          const uint64_t* before, const PPCContext* ctx) {
  (void)before;
  (void)ctx;
  static std::mutex mutex;
  static std::unordered_set<int> seen;
  {
    std::lock_guard<std::mutex> lock(mutex);
    if (seen.size() >= 256 || !seen.insert(site).second) return;
  }
  REXLOG_WARN("[regcheck] site {:06X} ({}): the call to {} changed {}", site, function, callee, regs);
}
