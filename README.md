<div align=center>
<img src="imgs/theme.jpeg" alt="singularity" width="100%">
</div>

---

> **Status:** the port is pretty much done and in a good playable state: the complete game has been played from start to end on a Switch. Contributions are very welcome if you know how to make it run even better.

Nintendo Switch port of the Xbox 360 version of Singularity (2010), built by static recompilation with rexglue-sdk and drawn by a native deko3d renderer.

The project is fully [vibecoded](https://share.google/jggfMaZ3mu2HW2Zd0).

It reuses the Unreal Engine 3 on Switch stack of [gears2-nx](https://github.com/VasilkovND/gears2-nx) (Gears of War 2), adapted to Singularity. The PC version is [singularity-recomp](https://github.com/panalexeu/singularity-recomp). It needs your **legally owned and purchased** Xbox 360 copy: no game data or recompiled code is part of this repository.

On a Switch Lite it runs at 960x540 with the CPU at 1785 MHz (the console's boost clock) and the GPU at 460.8 MHz: clean sound, heavy scenes playable, about 2 hours of battery. At the stock 1020 MHz CPU busy scenes drop below 20 fps and the sound crackles.

Known issues: entering a new area can still hitch for up to ~0.8 s (the game's own loading work). Lightning strike hitches are mitigated by preloading captured shaders at boot, though occasional stalls can still occur when uncaptured shader permutations are first encountered in new areas.

### Instructions

Requirements: a Switch with Atmosphere running homebrew in title takeover mode (hold R while starting a game); Linux with git, Python 3, CMake 3.25+, Ninja, clang-20, Docker; `dxc` and `spirv-val` from the Vulkan SDK.

1. Clone and set up the SDKs: rexglue-sdk-nx pinned to `f961303` in `thirdparty/rexglue-sdk-nx` plus `patches/rexglue-sdk-nx/`, upstream rexglue-sdk `v0.10.0` in `thirdparty/rexglue-sdk` (its `rexglue` codegen tool), and the Docker image with the Switch toolchain:

   ```bash
   git clone <this repo's URL> singularity-nx
   cd singularity-nx
   tools/setup.sh
   ```

2. Extract your copy of the game (`default.xex` and `RvGame/`) into `singularity-extract/` (gitignored). Only the v1.0 `default.xex` with SHA-256 `d55636b840319ca8b6b9b6e8c4fc06981803787af61c331ea56eacfb7ad5dac5` is supported.

3. Recompile the game code into `out/codegen-nx/` (never share it):

   ```bash
   tools/switch/codegen.sh
   ```

4. Build the shaders once (21,767 shaders, ~152,000 DKSH variants and each shader's input shapes, takes hours):

   ```bash
   DXC=<path to dxc> VAL=<path to spirv-val> tools/switch/shaders.sh
   ```

5. Build the NRO (about an hour the first time; use `tools/switch/build-native.sh 2` on an 8 GB PC):

   ```bash
   tools/switch/build-native.sh
   ```

   **Profile-Guided Optimization (PGO):**
   - **Step 1:** Build the instrumented binary:
     ```bash
     PGO=generate tools/switch/build-native.sh
     ```
     Copy `out/build/switch-pgo-generate/singularity.nro` to `sdmc:/switch/singularity/`.
   - **Step 2:** Play the game through combat, exploration, and menus. The NRO automatically dumps branch execution counters to `sdmc:/switch/singularity/pgo/singularity_pgo_<N>.stream` every 2 minutes, when navigating to the HOME menu, and on exit.
   - **Step 3:** Copy the `pgo/` folder from the SD card to the PC and merge the profile streams:
     ```bash
     tools/switch/pgo_merge.sh /path/to/pgo
     ```
     This runs `gcov-tool merge-stream` inside Docker and updates `.gcda` execution summaries with `tools/switch/pgo_summary.py`.
   - **Step 4:** Build the final PGO-optimized binary:
     ```bash
     PGO=use tools/switch/build-native.sh
     ```
     Deploy `out/build/switch-pgo-use/singularity.nro` to your Switch.

6. Optional, about +3-5 ms GPU relief in heavy combat: apply the recommended graphics profile (lowered shadow map resolution to 256, simplified PCF shadow filtering, and disabled distortion and dynamic decals) to the game's config:

   ```bash
   python3 tools/switch/coalesced_settings.py singularity-extract/RvGame/CookedXenon/Coalesced_INT.bin \
     out/Coalesced_INT.bin \
     MaxShadowResolution=256 bEnableBranchingPCFShadows=FALSE Distortion=False DropParticleDistortion=True DynamicDecals=False DecalCullDistanceScale=0.5
   ```

   Copy `out/Coalesced_INT.bin` over `singularity-extract/RvGame/CookedXenon/Coalesced_INT.bin` on the SD card.

7. Copy to `sdmc:/switch/singularity/`: `out/build/switch-native/singularity.nro`, `switch/singularity.toml`, `out/shaders/singularity_shaders.dslib`, `out/shaders/shadercache_dksh.bin`, `out/shaders/info_shaders.bin` (into a `cache/` folder there) and your `singularity-extract/` folder. Launch the NRO from the homebrew menu in title takeover mode; logs go to `logs/` next to it.

### Changes

Settings (`singularity.toml`):

| Setting | Effect in game |
|---|---|
| `singularity_switch_cpu_mhz = 1785` | CPU at the boost clock (0 = stock 1020): clean sound, smooth heavy scenes |
| `singularity_switch_gpu_mhz = 460` | The official 460.8 MHz handheld GPU mode |
| `singularity_resolution = 540` | 960x540 instead of 1280x720, about 56% of the pixel work |
| `singularity_patch_no_tiling = true` | Without it the bottom of the screen is red |
| `singularity_sin_prepaso_z`, `singularity_sin_prepaso_enmascarado` | Without them the world turns red, and windows, rubbish and debris show a staircase of old frames |
| `singularity_oclusion_anterior = false` | Answering pending occlusion queries with the previous frame's result immediately (off by default: with FASTARGS + PGO, CPU throughput is high enough that real queries resolve without stalling) |
| `nfsmw_dk_compilar_variantes = false` | Never compiles a shader while playing (the general variant draws the same image); a runtime compile took a CPU core for 30+ s and the sound crackled |
| `input_gyro = 1` | Gyroscopic motion aiming: 0 = off, 1 = active while aiming down sights with ZL, 2 = always on |
| `input_gyro_sensitivity = 1.8` | Gyro aiming speed (0.1 to 5.0) |
| `input_gyro_deadzone = 0.3` | Deadzone in degrees/second to filter hand tremors |
| `input_gyro_min_stick = 11000` | Starting stick offset past deadzone to bypass UE3's internal stick curve |

Game patches (applied by `tools/switch/codegen.sh`):

| Patch | What it changes | Effect in game |
|---|---|---|
| 1 `patch_singularity.py` | `GUseTilingMode` (`0x83917A88`) set to 0 in `sub_82DAD680` | The scene is drawn once, not in two MSAA tiles (red bottom fixed) |
| 3 `patch_scene_resolution.py` | `sub_82DBFA80`'s hard-coded 720 height and 1280 width cap become 540 / 960 (from gears2-nx 1.1.0) | The 540p mode |
| Fast register entries (`patch_local_regs.py`, from deadpool-nx) | Functions pass parameters directly by value in host registers (`r3`–`r10`, `lr`, `f1`–`f13`) and return `r3` directly in host registers (`FASTARGS`) | Eliminates redundant `ctx` register spilling/reloading across 80,000+ functions; raises combat minimum FPS floor (~23.6 → 25.8+ fps) |
| Profile-Guided Optimization (PGO, from deadpool-nx) | Instrumented runtime streams execution branch counters to `pgo/` every 2 minutes / HOME / exit; `PGO=use` guides function layout, inlining, branch prediction, and block placement | Boosts CPU execution speed by ~5-10% in heavy combat scenes |
| ARM fixes, CPU passes | From the Gears of War 2 PC port and gears2-nx | Correct on ARM's memory model, 25-30% less CPU per frame |
| Clear emulation (`switch/src/singularity_gpu.cpp`) | Direct3D's clear rectangle takes its color from its vertices; the color written through the depth path is computed without float overflow | Projector slides in their real colors (they were red) |
| Perf hooks (`switch/src/singularity_perf_hooks.cpp`, from gears2-nx) | Direct3D's GPU and occlusion-query waits sleep instead of spinning; memcpy, memset and the audio mixer's state-variable filter (`sub_828EDF78`) run as native code | The spins took a core from the audio thread (crackling); the filter was 26% of the audio thread |

rexglue-sdk-nx patches (`patches/rexglue-sdk-nx/`):

| Patch | What was wrong | Effect in game |
|---|---|---|
| `0001` | gears2-nx's 49 SDK patches for Unreal Engine 3 on deko3d | Without them the game does not run or draws a broken image |
| `0002` | Occlusion results were read only after the frame's whole GPU work finished | The render thread waited ~15% of its time for them; now ~2% |
| `0003` | The background shader compiler ran pinned to one core | It may now use any core (and runs below the game where the system allows it) |
| `0004` | Waits on several objects polled every 1 ms | The game's audio thread woke up to 1 ms late for every 5.33 ms block of sound and missed blocks (crackling) |
| `0005` | A shader variant missing from the cache was compiled while playing | Optional now (`nfsmw_dk_compilar_variantes`) |
| `0006` | Textures the game writes at run time (the font atlas) were rechecked only every few frames | Letters were missing for a moment when text first appeared |
| `0007` | A stencil-only clear was remembered as a depth clear, so the full depth clear after it was skipped | World objects in front of you cut away parts of your gun |
| `0008` | The texture descriptor table had 8,192 entries and filled up during play (~8,000 small textures alive): every new texture then got the empty one | Characters, notes and whole levels after a transition were drawn black; now 32,768 entries, unused textures released above three quarters |
| `0009` | The decoder reported a sound's input used up in the same pass that wrote its last audio; the game's XAudio2 ran its one-time end-of-buffer step before reading that audio and kept the buffer queued forever (~20 % of sounds never finished) | Audio tapes' speech plays (it is chained after the tape noise, which never finished); sounds end on time |
| `0010` | The depth of field reads a scene depth resolved before the first-person weapon is drawn: the weapon's pixels took the depth behind them, and in front of the sky (past the far plane) half of each was replaced by the blurred sky. The renderer can now merge the depth drawn after a resolve into the resolved texture (nearer wins); the port does it before the blur and composite passes (`singularity_dof_arma`) | The weapon no longer turns see-through in front of the sky or windows |
| `0011` | The renderer bound its texture tables once per frame but submits the frame in pieces; while an achievement popup is up the presenter paints from the interface thread between two pieces and leaves its own small tables bound, so the rest of the frame read the wrong textures. Each piece now binds the renderer's tables again | No black flashing (and no GPU fault) while an achievement popup is on screen |
| `0012` | The achievement popup's dialog lived for the rest of the session, keeping the presenter painting from the interface thread | After a popup fades, frames are presented from the game thread again and the FPS counter is right |
| `0013` | The emulator's own threads (GPU ring, XMA decoder, audio worker) outrank the game's and preempted its rendering thread on every core; they can now be kept on one (`singularity_nucleo_emulador`, core 2) | More CPU for the game's rendering thread in dense scenes |
| `0014` | Vertex buffers read by the shader itself were copied whole for every draw (12-18 MB a frame in dense scenes); now cached like vertex streams (`nfsmw_dk_manuales_en_cache`) | Uploads 0.3-1.4 MB a frame, GPU ring thread ~60 % → 22-35 % of a core; dense areas 23-25 → 27-30 fps |
| `0015` | The audio threads follow a core setting of their own (`singularity_nucleo_audio`, any core): on the ring's core, with more priority, the XMA decoder left the ring no room | The GPU ring has core 2 to itself |
| `0016` | A pixel shader's first draw converted its whole SPIR-V to GLSL just to learn its inputs, every session (5-10 ms each, most of the lightning freeze). The results are kept in `cache/info_shaders.bin`; `tools/switch/shaders/dksh/ds_info` writes that file for the whole library on the PC (3.7 s), copy it to `cache/` next to the NRO | No shader conversion on the console at all; the lightning freeze went from ~1.6 s to ~0.3 s |
| `0017` | Lightning flashes and dynamic lights request new shader combinations on-demand, causing frame stalls (~100–280 ms). Added two-tier preload support (`cache/preload_shaders.txt`): reads bytecode sequentially from SD and prepares `DkShader` instances at boot. Capturing specific IDs smooths known spots (e.g. initial thunder strikes run at locked 30 fps), though stalls can still occur as new uncaptured material/light permutations appear deeper in levels. The same patch adds Switch hardware SixAxis gyroscopic aiming to `SwitchInputDriver`, additively combined with right-stick analog aiming | Mitigates lightning stalls where shaders are preloaded; work in progress; smooth, responsive gyro-assisted aiming down sights |
| `0018` | The game saw a local-only profile with no network: XAM now reports the profile signed in to Xbox Live (LIVE_ENABLED flag), grants every privilege (multiplayer, user content, chat), and reports an active Ethernet link and an online title address (loopback) | For multiplayer: the game no longer sees an offline, unprivileged profile |
| `0019` | Patch 0010's merge went into the texture of the *last* depth resolve, which a later depth resolve (a shadow map) could take over. The merge now goes into the depth texture the depth-of-field pass actually samples; ZCULL is invalidated around it | Correctness only: on the console the last resolve was always the right texture, so this alone did not fix the see-through gun (0020 did) |
| `0020` | The weapon's own modulated shadow (PS n1312, drawn after the weapon) rebuilt each pixel's position from the scene depth resolved *before* the weapon: on the gun it used the world behind it, and rails and posts were multiplied onto the gun. The renderer can now merge a depth target into its texture again after a full clear and new draws; the port does it before any pass that samples that depth (`singularity_sombra_arma`) | The gun no longer looks see-through near shadow-casting lights (confirmed on the console) |
| `0021` | With the Live spoof (0018) Quick/Public Match first reads every player's skill stats; `XUserReadStats` answered success without writing results and the game read a null view pointer. Stats reads now return one empty row per player (the game uses its default skill) and every session search returns a valid 0-result header | Quick/Public Match no longer crash (no sessions are found); Private Match is unchanged |

`overrides.toml` declares 277 function entry points the codegen analyzer misses (see singularity-recomp).

### License

GPL-3.0 (game-side code from gears2-nx); SDK patches BSD-3-Clause, shader tools MIT. See `THIRD_PARTY_NOTICES.md`.
