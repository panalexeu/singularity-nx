# Switch build environment: devkitA64/libnx for the game, plus what Mesa (NVK) needs to build.
#
#   docker build -t singularity-switch tools/switch
#
# Based on mesa-switch's Docker.rust. The devkitA64 image is pinned (same digest as castlevania-nx).
FROM devkitpro/devkita64@sha256:1fc388c3a0d34bd2045a6dadcb1020e069d5f876a187fd705de14b4440c00282

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        python3-pip python3-setuptools python3-wheel python3-mako python3-yaml \
        ninja-build pkg-config flex bison curl ca-certificates git cmake \
        clang libclang-dev \
        llvm-15-dev libllvmspirvlib-15-dev libclc-15-dev libclang-15-dev \
    && apt-get clean && rm -rf /var/lib/apt/lists/*

# Meson >= 1.4 (Debian 12 ships 1.0).
RUN pip3 install --break-system-packages meson

# SPIRV-Tools >= 2024.1 for Mesa's host tools (from mesa-switch's Docker.rust).
RUN cd /tmp && \
    git clone --depth 1 --branch vulkan-sdk-1.3.290.0 https://github.com/KhronosGroup/SPIRV-Tools.git && \
    git clone --depth 1 --branch vulkan-sdk-1.3.290.0 https://github.com/KhronosGroup/SPIRV-Headers.git SPIRV-Tools/external/spirv-headers && \
    cmake -S SPIRV-Tools -B SPIRV-Tools/build -G Ninja -DCMAKE_INSTALL_PREFIX=/usr/local \
        -DCMAKE_BUILD_TYPE=Release -DSPIRV_SKIP_TESTS=ON && \
    ninja -C SPIRV-Tools/build install && rm -rf /tmp/SPIRV-Tools

# Debian's Clang headers live under the full version; mesa_clc looks under the major version.
RUN [ -d /usr/lib/llvm-15/lib/clang/15/include ] || \
    ln -s /usr/lib/llvm-15/lib/clang/15.0.6/include /usr/lib/llvm-15/lib/clang/15/include

# Rust nightly for NAK (Mesa's NVIDIA shader compiler), pinned to 1.98 (what nfsmw-nx used: newer
# nightlies moved io::Error into core and NAK then fails to link), with the aarch64 std it is built against, and
# bindgen/cbindgen. Installed under /opt so any user can run it.
ENV RUSTUP_HOME=/opt/rustup CARGO_HOME=/opt/cargo PATH=/opt/cargo/bin:$PATH
RUN curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y --no-modify-path \
        --default-toolchain nightly-2026-06-01 --profile minimal && \
    rustup component add rust-src && \
    rustup target add aarch64-unknown-linux-gnu && \
    cargo install --locked bindgen-cli cbindgen && \
    chmod -R a+rX /opt/rustup /opt/cargo

# Rust's std links -ldl -lrt -lutil, which libnx does not have: empty placeholders
# (Mesa's rust_switch_stubs.c provides the symbols). From mesa-switch's build-switch.sh.
RUN cd /tmp && echo "void __mesa_switch_stub_lib(void) {}" > stub.c && \
    /opt/devkitpro/devkitA64/bin/aarch64-none-elf-gcc -c stub.c -o stub.o && \
    for L in dl rt util; do \
        /opt/devkitpro/devkitA64/bin/aarch64-none-elf-ar rcs /opt/devkitpro/portlibs/switch/lib/lib$L.a stub.o; \
    done && rm stub.c stub.o

ENV PATH=/opt/devkitpro/devkitA64/bin:/opt/devkitpro/tools/bin:$PATH
WORKDIR /work
