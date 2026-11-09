// singularity-recomp - game patches in the generated code (tools/switch/codegen/patch_singularity.py, run by
// tools/switch/codegen.sh). Each patched site asks SingularityGamePatch(id) whether it is on: a cvar each, so
// they can be compared from singularity.toml without a rebuild. Modelled on gears2-nx's gow2_game_patches.cpp.
//
//   1  GUseTilingMode = FALSE: the game draws the scene at once instead of in predicated tiles. Singularity uses
//      2x MSAA in two tiles (rows 0-511 and 512-719); the native renderer drew only the first.
//   3  The 540p scene (tools/switch/codegen/patch_scene_resolution.py, from gears2-nx 1.1.0's game patch 8):
//      sub_82DBFA80's hard-coded height 720 and 1280 width cap become 540 / 960. singularity_app.h sets the SDK's
//      video_mode_width / video_mode_height to 960x540 with it, for the UI and back buffer.
#include <rex/cvar.h>

REXCVAR_DEFINE_BOOL(singularity_patch_no_tiling, true, "Singularity",
                    "GUseTilingMode = FALSE: draw the scene at once, without predicated tiling (patch 1)");

REXCVAR_DEFINE_INT32(singularity_resolution, 720, "Singularity",
                     "Scene resolution at startup: 720 (1280x720, the game's) or 540 (960x540, about 56% of the "
                     "pixel work; game patch 3 plus a 960x540 video mode)")
    .allowed({"720", "540"});

extern "C" int SingularityGamePatch(int id) {
  switch (id) {
    case 1: return REXCVAR_GET(singularity_patch_no_tiling);
    case 3: return REXCVAR_GET(singularity_resolution) == 540;
    default: return 0;
  }
}
