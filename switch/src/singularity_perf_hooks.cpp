// singularity-recomp - CPU time the console has no use for. From gears2-nx's gow2/src/gow2_perf_hooks.cpp (GPL-3.0),
// with Singularity's addresses (the same Xbox 360 SDK and C runtime code, found by its shape).
//
// The game has three cores on the Switch, and in busy scenes all three were full while Unreal's rendering thread spent
// up to 41% of its time in one wait loop. On the Xbox 360 these spins cost a hardware thread out of six; here they take a
// core from the threads they wait for (the game's audio thread, the GPU ring). While they answer "still waiting" the
// thread now sleeps instead.
//
//   sub_826845D8  Direct3D's "is the GPU there yet" check: 4 x 8 db16cyc delays, then a look at the GPU's progress.
//                 Answers 1 while still waiting; its callers simply call it again. (Gears of War 2: sub_82977688.)
//   sub_8267CCB8  Direct3D's query GetData: 1 (S_FALSE) while the GPU has not written the result, and Unreal's
//                 rendering thread polls it in a loop (sub_82DB5C90). (Gears of War 2: sub_829783A8.)
//
// Real work done natively:
//   sub_829E2330  the C runtime's memcpy (r3 destination, r4 source, r5 bytes; answers r3), 1,899 call sites.
//   sub_829E2290  the C runtime's memset (r3 destination, r4 byte, r5 bytes), 1,571 call sites.
#include "singularity_pch.h"

#include <atomic>
#include <chrono>
#include <bit>
#include <cmath>
#include <cstring>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/memory/utils.h>

#if defined(__SWITCH__)
#include <switch.h>
#endif

REXCVAR_DEFINE_INT32(singularity_espera_us, 100, "Singularity",
                     "Microseconds the game's busy waits (GPU progress, occlusion results) sleep each time the answer is "
                     "still 'not yet'; 0 = spin as on the Xbox 360");
REXCVAR_DEFINE_BOOL(singularity_cpu_nativo, true, "Singularity",
                    "The game's memcpy (sub_829E2330) and memset (sub_829E2290) as native code");

extern "C" void SingularityOcclusionWait();  // singularity_gpu.cpp: the game waits, answer it now

namespace {
enum Espera { kEsperaGpu, kEsperaConsulta, kEsperas };
std::atomic<uint64_t> g_esperas[kEsperas], g_esperas_ns[kEsperas];
std::atomic<uint64_t> g_informe_ns{0};

uint64_t AhoraNs() {
  return uint64_t(
      std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

void Dormir(Espera tipo) {
#if defined(__SWITCH__)
  const uint64_t inicio = AhoraNs();
  const int32_t us = REXCVAR_GET(singularity_espera_us);
  svcSleepThread(us > 0 ? int64_t(us) * 1000 : 0);
  const uint64_t fin = AhoraNs();
  g_esperas[tipo].fetch_add(1, std::memory_order_relaxed);
  g_esperas_ns[tipo].fetch_add(fin - inicio, std::memory_order_relaxed);
  uint64_t antes = g_informe_ns.load(std::memory_order_relaxed);
  if (fin - antes >= 10'000'000'000ull && g_informe_ns.compare_exchange_strong(antes, fin)) {
    REXLOG_INFO("[esperas] 10 s: GPU {} ({:.0f} ms), consultas {} ({:.0f} ms)", g_esperas[kEsperaGpu].exchange(0),
                double(g_esperas_ns[kEsperaGpu].exchange(0)) / 1e6, g_esperas[kEsperaConsulta].exchange(0),
                double(g_esperas_ns[kEsperaConsulta].exchange(0)) / 1e6);
  }
#else
  (void)tipo;
#endif
}
}  // namespace

REX_EXTERN(__imp__sub_826845D8);
REX_HOOK_RAW(sub_826845D8) {
  __imp__sub_826845D8(ctx, base);
  if (ctx.r3.u32 == 1) Dormir(kEsperaGpu);
}

REX_EXTERN(__imp__sub_8267CCB8);
REX_HOOK_RAW(sub_8267CCB8) {
  __imp__sub_8267CCB8(ctx, base);
  if (ctx.r3.u32 == 1) {  // S_FALSE: not there yet
    SingularityOcclusionWait();
    Dormir(kEsperaConsulta);
  }
}

REX_EXTERN(__imp__sub_829E2330);
REX_HOOK_RAW(sub_829E2330) {
  if (!REXCVAR_GET(singularity_cpu_nativo)) {
    __imp__sub_829E2330(ctx, base);
    return;
  }
  if (const uint32_t n = ctx.r5.u32) std::memmove(base + ctx.r3.u32, base + ctx.r4.u32, n);
}

REX_EXTERN(__imp__sub_829E2290);
REX_HOOK_RAW(sub_829E2290) {
  if (!REXCVAR_GET(singularity_cpu_nativo)) {
    __imp__sub_829E2290(ctx, base);
    return;
  }
  if (const uint32_t n = ctx.r5.u32) std::memset(base + ctx.r3.u32, int(ctx.r4.u32 & 0xFF), n);
}

// sub_828EDF78  a state-variable filter of the game's audio mixer, ~20% of its audio thread in busy scenes. Its
//               parameters at r3: +0 input samples, +4 output samples, +8 / +12 pointers to the low and band states,
//               +16 sample count, +24 mode (0 low-pass, 1 band-pass, 2 high-pass, else notch = low + high), +28 the
//               frequency coefficient, +32 the damping. Per sample, as the PowerPC code (each fused multiply-add
//               rounded once to single precision): high = (x - low) - damping * band; band += high * f; low += band * f.
//               Called only through a table (no direct call sites), so no inlining hides it.
REXCVAR_DEFINE_BOOL(singularity_audio_filtro_nativo, true, "Singularity",
                    "The game's audio state-variable filter (sub_828EDF78) as native code");

REX_EXTERN(__imp__sub_828EDF78);
REX_HOOK_RAW(sub_828EDF78) {
  if (!REXCVAR_GET(singularity_audio_filtro_nativo)) {
    __imp__sub_828EDF78(ctx, base);
    return;
  }
  auto u32 = [&](uint32_t a) { return rex::memory::load_and_swap<uint32_t>(base + a); };
  auto lf = [&](uint32_t a) { return std::bit_cast<float>(u32(a)); };
  auto sf = [&](uint32_t a, float v) { rex::memory::store_and_swap<uint32_t>(base + a, std::bit_cast<uint32_t>(v)); };
  const uint32_t p = ctx.r3.u32;
  const uint32_t src = u32(p + 0), dst = u32(p + 4), p_low = u32(p + 8), p_band = u32(p + 12), n = u32(p + 16);
  const uint32_t mode = u32(p + 24);
  const float f = lf(p + 28), q = lf(p + 32);
  float low = lf(p_low), band = lf(p_band);
  for (uint32_t i = 0; i < n; ++i) {
    const float x = lf(src + i * 4);
    const float d = x - low;                       // fsubs
    const float high = std::fmaf(-q, band, d);     // fnmsubs: -(q * band - d)
    band = std::fmaf(high, f, band);               // fmadds
    low = std::fmaf(band, f, low);                 // fmadds
    const float out = mode == 0 ? low : mode == 1 ? band : mode == 2 ? high : low + high;
    sf(dst + i * 4, out);
  }
  sf(p_low, low);
  sf(p_band, band);
}

// ---------------------------------------------------------------------------------------------------------------------
// singularity_audio_fin_por_duracion: sounds that never report finished (the audio tapes' speech never started).
//
// FXAudio2SoundSource::IsFinished (sub_82DC70A0, source vtable 0x822FFE90 slot 28; source+8 = FWaveInstance, +16 =
// paused) ends a sound when its XAudio2 voice has no buffer queued. On the Switch ~20 % of sounds keep 1 buffer queued
// after their audio has played out, so they never finish; a sound cue that chains sounds (a concatenator) then stops at
// the stuck one: the audio tape cue (tape noise start -> speech -> tape noise stop) never reached the speech. Safety net
// until the decoder hands that last buffer back: a non-looping sound (wave instance+32, LoopingMode, 0) that has played
// (paused time excluded) for longer than 1.5 x its wave's Duration (wave+108, USoundNodeWave::GetDuration, wave vtable
// slot 288) + 0.5 s finishes as the original does on its own finished path: WaveInstance->NotifyFinished
// (sub_833AC1D8), then "finished". Measured: 1,831 sounds ended this way in 27 min of play, the tapes speak.
#include <mutex>
#include <unordered_map>

REXCVAR_DEFINE_BOOL(singularity_audio_fin_por_duracion, true, "Singularity",
                    "Finish non-looping sounds that have played for 1.5 x their length + 0.5 s without finishing "
                    "(audio tapes' speech)");

REX_EXTERN(__imp__sub_82DC70A0);
REX_HOOK_RAW(sub_82DC70A0) {
  const uint32_t fuente = ctx.r3.u32;
  __imp__sub_82DC70A0(ctx, base);
  if (!REXCVAR_GET(singularity_audio_fin_por_duracion)) return;
  struct Seguimiento {
    uint32_t instancia = 0;
    uint64_t ultima_ns = 0;   // last IsFinished call for this source
    uint64_t sonando_ns = 0;  // time played: the gaps between calls while not paused, each capped at 100 ms
  };
  static std::mutex m;
  static std::unordered_map<uint32_t, Seguimiento> fuentes;
  const uint32_t instancia = ctx.r3.u32 ? 0 : REX_LOAD_U32(fuente + 8);
  if (!instancia) {
    std::lock_guard<std::mutex> l(m);
    fuentes.erase(fuente);
    return;
  }
  if (REX_LOAD_U32(instancia + 32) != 0) return;  // looping
  const uint32_t onda = REX_LOAD_U32(instancia);
  if (!onda) return;
  const float duracion = std::bit_cast<float>(REX_LOAD_U32(onda + 108));
  if (!(duracion > 0.0f && duracion < 1000.0f)) return;
  const uint64_t ahora = AhoraNs();
  {
    std::lock_guard<std::mutex> l(m);
    Seguimiento& s = fuentes[fuente];
    if (s.instancia != instancia) s = Seguimiento{instancia, ahora, 0};
    if (!REX_LOAD_U32(fuente + 16)) s.sonando_ns += std::min<uint64_t>(ahora - s.ultima_ns, 100'000'000ull);
    s.ultima_ns = ahora;
    if (double(s.sonando_ns) / 1e9 <= double(duracion) * 1.5 + 0.5) return;
    fuentes.erase(fuente);
  }
  // As the original's own finished path: WaveInstance->NotifyFinished(), then "finished".
  const PPCContext guardado = ctx;
  ctx.r3.u64 = instancia;
  REX_CALL_INDIRECT_FUNC(0x833AC1D8u);
  ctx = guardado;
  ctx.r3.u64 = 1;
}
