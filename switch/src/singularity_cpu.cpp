// gears2-nx - how the recompiled code's "interrupts off" regions behave (see tools/make_nx_pch.py, point 3).
//
// The game brackets its lwarx/stwcx. atomics with mtmsrd. The codegen made each region one process-wide mutex; the
// Switch PCH keeps that state per thread instead, which is enough for the atomics (host compare-and-swap) and lets the
// job scheduler's threads run side by side. singularity_msr_cerrojo_global brings the old mutex back, for comparing.

#include "singularity_pch.h"

#include <rex/cvar.h>
#include <rex/logging.h>

#include "singularity_cpu.h"

#if defined(__SWITCH__)
#include <switch.h>
#endif

// SINGULARITY_MSR_GLOBAL_DEFAULT=1 builds a variant with the codegen's global mutex by default (to compare).
#ifndef SINGULARITY_MSR_GLOBAL_DEFAULT
#define SINGULARITY_MSR_GLOBAL_DEFAULT 0
#endif
REXCVAR_DEFINE_BOOL(singularity_msr_cerrojo_global, SINGULARITY_MSR_GLOBAL_DEFAULT != 0, "Singularity",
                    "Las regiones con interrupciones apagadas del codigo recompilado (mtmsrd) como un mutex global "
                    "del proceso, como las genera el codegen. false = estado por hilo (los atomicos ya son CAS)");

namespace singularity::cpu {

void Configurar() {
  ppc_msr_cerrojo_global_() = REXCVAR_GET(singularity_msr_cerrojo_global);
  REXLOG_INFO("[cpu] regiones mtmsrd: {}", ppc_msr_cerrojo_global_() ? "mutex global (codegen)" : "estado por hilo");
}

}  // namespace singularity::cpu

namespace singularity::cpu {

void InformarSistema() {
#if defined(__SWITCH__)
  const u32 hos = hosversionGet();
  const bool aplicacion = appletGetAppletType() == AppletType_Application;
  REXLOG_INFO("gears2-nx {} (commit {}), firmware {}.{}.{}{}, {}, {}", SINGULARITY_VERSION, SINGULARITY_COMMIT, HOSVER_MAJOR(hos),
              HOSVER_MINOR(hos), HOSVER_MICRO(hos), hosversionIsAtmosphere() ? " (Atmosphere)" : "",
              aplicacion ? "title takeover" : "APPLET MODE",
              appletGetOperationMode() == AppletOperationMode_Handheld ? "handheld" : "docked");
  if (!aplicacion) {
    REXLOG_WARN("gears2-nx: started as an applet (from the album?): not enough memory for the game. Start any game while "
                "holding R and open the Homebrew Menu from there");
  }
#else
  REXLOG_INFO("gears2-nx {} (commit {})", SINGULARITY_VERSION, SINGULARITY_COMMIT);
#endif
}

}  // namespace singularity::cpu
