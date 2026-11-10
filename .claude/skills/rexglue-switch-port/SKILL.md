---
name: rexglue-switch-port
description: How to port an Xbox 360 game (especially Unreal Engine 3) to the Nintendo Switch with ReXGlue static recompilation and the native deko3d renderer (rexglue-sdk-nx), and how to debug rendering, performance and audio problems on the console. Use when building, running, debugging or optimizing such a port (this repo, gears2-nx-style ports), reading frame dumps or rex_perfil.log, or deciding what to change next.
---

# Porting an Xbox 360 game to the Switch with ReXGlue + deko3d

Learned on Singularity (UE3, title 415607EF), built on the gears2-nx stack. Everything here was measured on a console.
Each worked example names the symptom, the mechanism and how it was proven.

## 1. Ground rules (how to work)

- **Mechanism before fix.** Read the screenshot or dump, then write down the one sequence of events that produces
  exactly that image or behaviour. Change code only after a measurement confirms it. Guesses shipped as fixes, several
  per build, cost a whole day here (Step 17 of LOG.md) and once made the user ask "do you actually understand why?".
- **One change per test, with a stated prediction.** "If X is the cause, Y happens." A test that can't fail is useless.
- **The user's test runs are expensive.** Before asking for one, check whether the logs you already have answer the
  question. Ask for exactly what is needed (e.g. "a frame dump while the gun is missing, standing still"), and say what
  each outcome would mean. Diagnostics that cost nothing in normal play (only active during a dump) are fine to ship.
- **If you are not sure, say so.** List what is known, what is not, and what single measurement separates the hypotheses.
- **Reference ports first.** Before writing anything, audit *every* file of the closest working port (gears2-nx for
  UE3) and list what you port and what you skip, with a reason. Missing gears2-nx's `gow2_perf_hooks.cpp` (free,
  already proven) cost several test rounds.
- **Never commit** game data, the extracted game, or generated code (`out/codegen-nx/`). No `sudo`. Commit locally; the
  user pushes. Explain every change: what it does, why, what it should fix.
- **Be concise** with the user; they read on a phone sometimes. Give paths in full.

## 2. Pipeline and order of work

1. **Reference behaviour:** run the game in Xenia Canary first (clean log, what the screen should look like).
2. **PC build** (rexglue codegen + Vulkan Xenos plugin): find missing functions (`overrides.toml`), fix codegen.
   Rule: wrong or missing generated code → `overrides.toml`; right code, wrong behaviour → hooks in `switch/src/`.
3. **Switch with the emulated GPU** only as a smoke test: Singularity got 0.1–0.2 fps. Emulating Xenos on the
   Switch is a dead end for 3D games.
4. **Native deko3d renderer** (rexglue-sdk-nx at gears2-nx's pin + its 49 SDK patches): a ring reader
   (`switch/src/singularity_gpu.cpp`) turns PM4 draws into deko3d draws. Shaders are extracted from the cooked
   packages and compiled ahead of time (XenosRecomp → HLSL → DXC → SPIR-V → uam → DKSH, `tools/switch/shaders.sh`).
   UE3 package versions differ per game (Singularity 584, Gears 2 575): check the summary fields.
5. **CPU passes** on the generated code (`tools/switch/codegen.sh`): ARM fixes (switch traps, memory barriers, spin
   hints), then gears2-nx's leaf prologue, stack access, inline calls, local registers (25–30% less CPU per frame).

Layout here: `switch/src/` (app, GPU ring, hooks), `tools/switch/codegen/` (game patches as codegen passes),
`patches/rexglue-sdk-nx/000N-*.patch` (SDK changes; the working SDK is a separate checkout on branch `singularity`,
each fix committed there and exported with `git format-patch -1 --start-number N -o patches/rexglue-sdk-nx/`).
`LOG.md` (gitignored) is the full history: read its "Dead ends" tables before trying anything.

## 3. Build, stage, run

```bash
SINGULARITY_SDK_NX=$HOME/projects/ports/rexglue-sdk-nx tools/switch/build-native.sh   # incremental
cp out/build/switch-native/singularity.nro out/sd-native/
cp out/build/switch-native/singularity out/sd-native-elf/singularity-final.elf          # keep the ELF of each staged NRO
md5sum out/sd-native/singularity.nro                                                    # tell the user the md5
```

- After an SDK edit, check the object really rebuilt (compare the `.obj` mtime with the source).
- The console is reached over **DBI MTP** at `/run/user/1000/gvfs/mtp:host=*/1: SD Card/switch/<game>/`. Reading works;
  **writing from the command line leaves 0-byte files**: the user copies files with the file manager. Always compare
  the md5 of the NRO on the SD with the staged one before reading a log.
- Screenshots: `.../8: Album/<title-takeover game>/` (newest by name).
- The game must run in **title takeover** (hold R on a game). From the Album it starts as an applet and quits
  (not enough memory): such a log is useless.
- After changing a codegen pass: `rexglue codegen --ignore-stamp …` (the stamp ignores the generator).

## 4. Diagnostics toolbox

| Tool | How | What it answers |
|---|---|---|
| Game log | `logs/<game>_NNN.log` (newest number) | everything below, plus `[esperas]`, `clear shape`, `skipped without VS` |
| Per-thread CPU | `logs/rex/rex_perfil.log` (overwritten each run: copy it) | which thread is busy; audio `solicitudes sin datos` = missing sound blocks |
| Stack samples | ZL+ZR+R, then `aarch64-none-elf-addr2line -f -i -C -e out/sd-native-elf/*.elf 0x…` | where a busy thread spends time (guest `sub_` addresses are stable across builds; host offsets are not) |
| **Frame dump** | ZL+ZR+D-pad **Left** (needs `switch_atajos_depuracion = true`) | full draw trace of the next frame in the log + every resolved texture as BMP in `volcado_NN/` |
| FPS counter | ZL+ZR+D-pad Up, or `switch_contador_fps = true` | fps / frame time on screen |
| GPU profile | `nfsmw_dk_perfil_gpu_s = N` | real per-draw GPU time ("GPU ms per frame" alone includes GPU idle) |

Reading a frame dump:
- Each draw line: `prim n vs ps c0 <edram>/f<format> z <edram> mask blend0 depthctl stencil mode pitch msaa -> fate`.
  `fate` is `drawn`, `Direct3D clear, emulated`, `no VS` (skipped!) or `REJECTED`. Below it: viewport, vertex fetches,
  textures (`fmt`, `swz`, `signs`, `tiled`), PS constants, and `copy:` lines for resolves.
- depthctl: bit1 z test, bit2 z write, bits 4–6 zfunc (1 less, 3 lequal, 4 greater, 6 gequal, 7 always). UE3 uses
  reversed Z (far = 0, GEQUAL). blend0 `00010008` = multiply into the target (dst × src).
- Texture swizzle: 3 bits per channel, 0–3 = R/G/B/A, 4 = 0, 5 = 1 (`A00` = R,R,R,1; `60A` = B,G,R,A).
- **Volcado numbering restarts each session** (`volcado_00` is overwritten): match by time and the log's
  `volcado: … escriben en …` line. BMP → PNG with a 20-line pure-Python converter (no PIL); compute per-channel means.
  A channel that is 0 over the whole image is a strong clue.
- `ps N` / `vs N` are indexes in `singularity_shaders.dslib`. Decompile one: link a tiny program against
  `out/build_dksh` objects (`ds_shader_library.cpp.o`, `glsl.cpp.o`, spirv-cross libs), use
  `BibliotecaShaders::CargarIndice` + `LeerSpirv(n)` + `rex::deko3d::SpirvAGlsl` (light on memory; `Cargar` reads all).
- Direct3D's own internal shaders (clears) are not in the library: log their microcode during a dump and decode by
  hand (CF word: exec; ALU word2 bits 24–28 vector opcode, MAXv a,a = mov; vfetch: dst bits 12–17, format bits 16–21 of
  word1, stride/offset in word2).

## 5. Symptom → mechanism (worked examples)

| Symptom | Mechanism | How it was proven / fix |
|---|---|---|
| Bottom of the screen red | Scene drawn in 2 MSAA predicated tiles; renderer drew one | `GUseTilingMode` found from the `TILING` console command; codegen patch stores 0 |
| Whole world red | Depth prepass and base pass use separately compiled VS; last-bit difference, GEQUAL fails | Skip the depth-only prepass (`singularity_sin_prepaso_z`) |
| Objects show a "staircase" of old frames | Same, for the alpha-tested prepass (colour+depth mode, colour masked) | Read from the screenshot (ragged alpha edge, old frames inside); `[estado]` histogram; skip that prepass too |
| Effects missing (vortex, water, beams) on PC | Shader-written depth (oDepth) not scaled to the float24 mapping | Magenta shader → depth test off → visible; fix in translator |
| Letters missing for a moment | Run-time written linear texture (font atlas) rechecked only every N frames | Frame dump showed the atlas stale; check linear textures every frame (SDK 0006) |
| Projector slides red | (a) D3D clear rejected: colour is in the rectangle's **vertices**, not PS c0; (b) colour-through-depth clear: `1.0f*16777215+0.5f` rounds to 2^24 in float, `<<8` overflows → `000000FF` | Dumped buffer had G=B=0 everywhere, even where the shader outputs grey; logged the clear's vertices and raw bits; compute in double |
| Parts of the weapon cut away by world objects, changing as you walk | Renderer's "skip repeated clear" remembered a **stencil-only** clear as a depth clear, so the full depth clear before the foreground pass was skipped | Trace: stencil-only clear, copy (not a draw), full clear with the same value; only full clears are remembered (SDK 0007) |
| Objects blink at edges | Occlusion answered with the previous frame's result | `singularity_oclusion_anterior = false` |
| Sound crackles | Game's audio thread starved (missing blocks), not a decoder bug | `rex_perfil.log`: audio thread %, missing blocks per 10 s; fixes in §6 |
| Freeze after Start | A blocking wait missed a signal path | Reverted to polling; keep waits as polls |

Questions that separated hypotheses fast:
- Is the draw issued at all (trace line present)? Skipped (`no VS`)? Drawn but rejected by depth (try depth test off)?
- Whole-image channel statistics of the dumped target before and after the suspect draw.
- What does the draw multiply into (blend)? What cleared that target, and with which value?
- Does it depend on position (depth/occlusion), time (streaming, timing) or both?
- Rule out cheaply with existing settings first: e.g. `singularity_oclusion_real = false` (no culling) in the toml,
  no rebuild.

## 6. Optimizations (measured on Singularity, Switch Lite)

Ranked by effect:
1. **CPU clock 1785 MHz** (NRO sets it via clkrst every second; GPU 460.8 MHz handheld mode). Clean sound, heavy
   scenes playable. ~1.8–2.1 h battery vs ~2.5–3 h.
2. **540p scene** (codegen patch on the hard-coded 720/1280 in the scene-size function, plus `video_mode` 960×540):
   ~56% of the pixel work. ScreenPercentage and Coalesced ResX/ResY do not work on the console path.
3. **CPU codegen passes** (gears2-nx): −25–30% CPU per frame.
4. **Perf hooks** (`switch/src/*_perf_hooks.cpp`): D3D "GPU done yet?" and query GetData spins sleep 100 µs instead of
   spinning (they stole a core from the audio thread); native memcpy/memset; native audio state-variable filter (26%
   of the audio thread). Find the addresses by function shape (same XDK/CRT code as Gears 2).
5. **Waits:** multi-object waits poll every 100 µs (1 ms made the audio thread late for every 5.33 ms block).
6. **No shader compiles while playing** (`nfsmw_dk_compilar_variantes = false`): a runtime compile took a core 30+ s.
   Collect the variants the console asks for into `dksh_variants.txt` and rebuild the cache instead.
7. **Occlusion results read as written** (SDK 0002): render-thread spin 15% → 2%.
8. **Game settings** via `tools/switch/coalesced_settings.py` (NRO re-hashes Coalesced at startup):
   `LightEnvironmentShadows=False` +2–3 fps in fights.

Fights stay ~27–28 fps: bound by the game's render thread (~74%, flat profile). Don't chase it with GPU tricks.

## 7. Dead ends (don't repeat without new evidence)

Colour-resolve swaps / scene-sized targets, ZCULL off, distortion off, bigger upload ring, texture retries, the
NFS-MW renderer shortcuts off, blocking (event) waits, compiler thread priority 0x3C (refused: fall back to 0x3B),
"previous frame" occlusion answers, geometry small-range rechecks for the text bug, MangoHud fps on PC
(it counted repaints, not frames).

## 8. Gotchas

- `std::thread::detach` is not implemented on Horizon (crashes): use libnx `threadCreate`.
- Float precision: anything that turns a [0,1] float into 24-bit integers must be done in double and clamped.
- A "copy" (resolve) is not a draw: logic keyed on "draws since" treats copies as nothing happened.
- D3D clears come in several shapes (colour rectangle, colour through the depth path on an MSAA alias, stencil-only).
  Check which targets the internal pixel shader really exports before clearing aliases.
- Horizon refuses some thread priorities; guest threads share 0x3B (time-sliced only among themselves).
- Keep `switch_atajos_depuracion = true` while debugging; tell the user to remove it for normal play.
