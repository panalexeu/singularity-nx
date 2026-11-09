# Third-party notices

This Switch port is built on the work of other projects. Their licenses apply to their code and to the
parts of this repository derived from it. This repository's own code is under GPL-3.0 (`LICENSE`).

| Project | Used for | License |
|---|---|---|
| [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk) by Tom Clay, with portions from [Xenia](https://xenia.jp) | Static recompilation (the v0.10.0 `rexglue` codegen tool, `thirdparty/rexglue-sdk`), Xbox 360 kernel and runtime | BSD-3-Clause |
| [rexglue-sdk-nx](https://github.com/nx-mod/rexglue-sdk-nx) by nx-mod | Switch fork of ReXGlue: Horizon layer, deko3d renderer, game-neutral `rex::x360` GPU layer (`thirdparty/rexglue-sdk-nx`, with `patches/rexglue-sdk-nx/`) | BSD-3-Clause |
| [gears2-nx](https://github.com/VasilkovND/gears2-nx) by VasilkovND | Unreal Engine 3 on the Switch: its SDK patches (`patches/rexglue-sdk-nx/0001`), game-side GPU and shader glue (`switch/`), CPU passes and 540p patch (`tools/switch/codegen/`), shader extractor (`tools/switch/shaders/`) | GPL-3.0; SDK changes BSD-3-Clause; shader tools MIT |
| [Gears of War 2 PC port](https://github.com/VasilkovND/gears-of-war-2-pc-port) | ARM fixes for the recompiled code (`tools/switch/codegen/patch_switch_traps.py`, `patch_memory_barriers.py`, `patch_spin_hints.py`, see `LICENSE.gears-of-war-2-pc-port`) | BSD-3-Clause |
| [ds2-nx](https://github.com/VasilkovND/ds2-nx) by VasilkovND | Origin of gears2-nx's game-side code and tools | GPL-3.0; SDK changes BSD-3-Clause |
| [NFSMW-NX](https://github.com/StevensND/nfsmw-nx) by StevensND | The native deko3d renderer in rexglue-sdk-nx, shader tools | GPL-3.0; SDK changes BSD-3-Clause; shader changes MIT |
| [XenosRecomp](https://github.com/hedge-dev/XenosRecomp) by hedge-dev and contributors | Xenos shader microcode to HLSL translator (`tools/switch/shaders/LICENSE.md`) | MIT |
| [deko3d](https://github.com/devkitPro/deko3d), [libnx](https://github.com/switchbrew/libnx), [devkitPro](https://devkitpro.org) | Switch graphics API, system library and toolchain | zlib / ISC / various |
| castlevania-nx | Switch CMake toolchain file (`tools/switch/switch-devkitA64.cmake`) | same author |

Libraries in the SDKs' third-party trees (FFmpeg and libmspack under LGPL-2.1; fmt, spdlog, SIMDe, toml++, Dear ImGui
and others under MIT, BSD or similar) keep their own licenses.

No game data, executable or recompiled game code is part of this repository. You need your own legally obtained copy
of the game.
