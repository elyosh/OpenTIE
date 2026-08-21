# Microsoft's Linux DXC release binaries are available for x86-64 only.
ARG TIE_BUILD_PLATFORM=linux/amd64

FROM --platform=${TIE_BUILD_PLATFORM} debian:bookworm-slim AS build-base

RUN apt-get update && apt-get install -y --no-install-recommends \
        binutils-mingw-w64-x86-64 \
        build-essential \
        ca-certificates \
        cmake \
        curl \
        g++-mingw-w64-x86-64-posix \
        gcc-mingw-w64-x86-64-posix \
        git \
        libncurses-dev \
        libxml2-dev \
        ninja-build \
        nasm \
        pkg-config \
        python3 \
        unzip \
        zip \
        zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

ARG SDL_VERSION=3.4.14
ARG ZSTD_VERSION=1.5.7
ARG FFMPEG_VERSION=8.0.1
ARG SHADERCROSS_COMMIT=6b06e55c7c5d7e7a09a8a14f76e866dcfad5ab99

FROM build-base AS host-tools

RUN git clone --branch "release-${SDL_VERSION}" --depth 1 \
        https://github.com/libsdl-org/SDL.git /deps/SDL \
    && cmake -S /deps/SDL -B /build/sdl-host -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/opt/tie-host \
        -DSDL_SHARED=ON \
        -DSDL_STATIC=OFF \
        -DSDL_TESTS=OFF \
        -DSDL_UNIX_CONSOLE_BUILD=ON \
    && cmake --build /build/sdl-host --parallel \
    && cmake --install /build/sdl-host

COPY packaging/common/shadercross-fsr3.patch /tmp/shadercross-fsr3.patch

# DXC 1.9 requires glibc 2.38; keep the Debian 12-compatible 1.8 release.
ARG DXC_VERSION=1.8.2502
ARG DXC_LINUX_X64_ARCHIVE=linux_dxc_2025_02_20.x86_64.tar.gz
ARG DXC_LINUX_X64_SHA256=e0580d90dbf6053a783ddd8d5153285f0606e5deaad17a7a6452f03acdf88c71

ADD --checksum=sha256:${DXC_LINUX_X64_SHA256} \
    https://github.com/microsoft/DirectXShaderCompiler/releases/download/v${DXC_VERSION}/${DXC_LINUX_X64_ARCHIVE} \
    /tmp/dxc.tar.gz

RUN git clone https://github.com/libsdl-org/SDL_shadercross.git /deps/SDL_shadercross \
    && git -C /deps/SDL_shadercross checkout "${SHADERCROSS_COMMIT}" \
    && git -C /deps/SDL_shadercross apply /tmp/shadercross-fsr3.patch \
    && git -C /deps/SDL_shadercross submodule update --init --depth 1 \
        external/SPIRV-Cross \
    && mkdir -p \
        /deps/SDL_shadercross/external/DirectXShaderCompiler-binaries/linux \
    && tar -xzf /tmp/dxc.tar.gz \
        -C /deps/SDL_shadercross/external/DirectXShaderCompiler-binaries/linux \
    && cmake -S /deps/SDL_shadercross/external/SPIRV-Cross \
        -B /build/spirv-cross -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/opt/tie-host \
        -DSPIRV_CROSS_CLI=OFF \
        -DSPIRV_CROSS_ENABLE_TESTS=OFF \
        -DSPIRV_CROSS_SHARED=ON \
        -DSPIRV_CROSS_STATIC=OFF \
    && cmake --build /build/spirv-cross --parallel \
    && cmake --install /build/spirv-cross \
    && cmake -S /deps/SDL_shadercross -B /build/shadercross -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/opt/tie-host \
        -DCMAKE_PREFIX_PATH="/opt/tie-host" \
        -DDirectXShaderCompiler_ROOT=/deps/SDL_shadercross/external/DirectXShaderCompiler-binaries \
        -DSDLSHADERCROSS_CLI=ON \
        -DSDLSHADERCROSS_DXC=ON \
        -DSDLSHADERCROSS_INSTALL=ON \
        -DSDLSHADERCROSS_INSTALL_RUNTIME=OFF \
        -DSDLSHADERCROSS_SHARED=ON \
        -DSDLSHADERCROSS_STATIC=OFF \
        -DSDLSHADERCROSS_SPIRVCROSS_SHARED=ON \
        -DSDLSHADERCROSS_VENDORED=OFF \
    && cmake --build /build/shadercross --parallel \
    && cmake --install /build/shadercross \
    && cp /deps/SDL_shadercross/external/DirectXShaderCompiler-binaries/linux/lib/libdxcompiler.so \
        /deps/SDL_shadercross/external/DirectXShaderCompiler-binaries/linux/lib/libdxil.so \
        /opt/tie-host/lib/

# The Windows target dependencies build as independent stages so that a
# version bump only rebuilds the affected dependency and so that cold builds
# run concurrently.

FROM build-base AS sdl-windows

COPY cmake/toolchains/mingw-w64-x86_64.cmake /opt/tie-toolchain.cmake
COPY packaging/common/sdl-gpu-d3d12-hdr.patch /tmp/sdl-gpu-d3d12-hdr.patch

# SDL's D3D12 SDL_GPU backend queries HDR swapchain composition support against
# the live SDR swapchain, which DXGI can never report scRGB as presentable on,
# so HDR output is unreachable on that backend. Only the Windows target build
# has a D3D12 backend, so the host SDL above is left unpatched.
RUN git clone --branch "release-${SDL_VERSION}" --depth 1 \
        https://github.com/libsdl-org/SDL.git /deps/SDL \
    && git -C /deps/SDL apply /tmp/sdl-gpu-d3d12-hdr.patch \
    && cmake -S /deps/SDL -B /build/sdl-windows -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/opt/tie-windows \
        -DCMAKE_TOOLCHAIN_FILE=/opt/tie-toolchain.cmake \
        -DSDL_SHARED=ON \
        -DSDL_STATIC=OFF \
        -DSDL_TESTS=OFF \
    && cmake --build /build/sdl-windows --parallel \
    && cmake --install /build/sdl-windows

FROM build-base AS zstd-windows

COPY cmake/toolchains/mingw-w64-x86_64.cmake /opt/tie-toolchain.cmake

RUN git clone --branch "v${ZSTD_VERSION}" --depth 1 \
        https://github.com/facebook/zstd.git /deps/zstd \
    && cmake -S /deps/zstd/build/cmake -B /build/zstd-windows -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/opt/tie-windows \
        -DCMAKE_TOOLCHAIN_FILE=/opt/tie-toolchain.cmake \
        -DZSTD_BUILD_PROGRAMS=OFF \
        -DZSTD_BUILD_SHARED=ON \
        -DZSTD_BUILD_STATIC=OFF \
        -DZSTD_BUILD_TESTS=OFF \
    && cmake --build /build/zstd-windows --parallel \
    && cmake --install /build/zstd-windows

FROM build-base AS ffmpeg-windows

RUN git clone --branch "n${FFMPEG_VERSION}" --depth 1 \
        https://github.com/FFmpeg/FFmpeg.git /deps/FFmpeg \
    && cd /deps/FFmpeg \
    && ./configure \
        --prefix=/opt/tie-windows \
        --target-os=mingw32 \
        --arch=x86_64 \
        --cross-prefix=x86_64-w64-mingw32- \
        --disable-autodetect \
        --disable-avdevice \
        --disable-avfilter \
        --disable-debug \
        --disable-doc \
        --disable-everything \
        --disable-network \
        --disable-programs \
        --disable-static \
        --enable-shared \
        --enable-decoder=vorbis \
        --enable-demuxer=ogg \
        --enable-w32threads \
    && make -j"$(nproc)" \
    && make install

FROM build-base AS midi-windows

ARG VCPKG_REF=2025.10.17

COPY packaging/common/fluidsynth-library-only.patch /tmp/fluidsynth-library-only.patch
COPY packaging/common/vcpkg-fluidsynth-library-only.patch /tmp/vcpkg-fluidsynth-library-only.patch

# The registry tag pins FluidSynth, SpeexDSP, GLib, and their transitive
# MinGW dependency graph as one reproducible unit.
# vcpkg's SpeexDSP MinGW recipe doubles the lib prefix even though its import
# library records libspeexdsp.dll as the runtime name.
RUN git clone --branch "${VCPKG_REF}" --depth 1 \
        https://github.com/microsoft/vcpkg.git /opt/vcpkg \
    && cp /tmp/fluidsynth-library-only.patch \
        /opt/vcpkg/ports/fluidsynth/fluidsynth-library-only.patch \
    && git -C /opt/vcpkg apply /tmp/vcpkg-fluidsynth-library-only.patch \
    && /opt/vcpkg/bootstrap-vcpkg.sh -disableMetrics \
    && /opt/vcpkg/vcpkg install fluidsynth speexdsp \
        --triplet x64-mingw-dynamic \
        --clean-after-build \
    && if [ -e /opt/vcpkg/installed/x64-mingw-dynamic/bin/liblibspeexdsp.dll ]; then \
        if [ -e /opt/vcpkg/installed/x64-mingw-dynamic/bin/libspeexdsp.dll ]; then \
            echo "Both SpeexDSP DLL names exist; refusing to overwrite libspeexdsp.dll" >&2; \
            exit 1; \
        fi; \
        mv /opt/vcpkg/installed/x64-mingw-dynamic/bin/liblibspeexdsp.dll \
            /opt/vcpkg/installed/x64-mingw-dynamic/bin/libspeexdsp.dll; \
    fi \
    && cmake -E copy_directory \
        /opt/vcpkg/installed/x64-mingw-dynamic /opt/tie-windows

FROM build-base AS winpix-windows

ARG WINPIX_VERSION=1.0.240308001
ARG WINPIX_NUPKG_SHA256=726acc93d6968e2146261a1e415521747d50ad69894c2b42b5d0d4c29fd66ec4

# SDL's D3D12 backend emits SDL_Push/PopGPUDebugGroup and
# SDL_InsertGPUDebugLabel through WinPixEventRuntime.dll, loaded at runtime.
# MIT-licensed since 1.0.240308001. Shipped with Debug packages only: Release
# builds compile the label calls out (AERON_GPU_DEBUG_LABELS), so the DLL is
# staged outside /opt/tie-windows to keep it clear of the wildcard DLL copy.
ADD --checksum=sha256:${WINPIX_NUPKG_SHA256} \
    https://globalcdn.nuget.org/packages/winpixeventruntime.${WINPIX_VERSION}.nupkg \
    /tmp/winpix.nupkg

RUN mkdir -p /deps/winpix /opt/winpix/bin \
    && unzip -q /tmp/winpix.nupkg -d /deps/winpix \
    && install -m 644 /deps/winpix/bin/x64/WinPixEventRuntime.dll \
        /opt/winpix/bin/

FROM build-base AS tie-host-tools

COPY --from=host-tools /opt/tie-host /opt/tie-host
COPY aeron /src/aeron

ENV LD_LIBRARY_PATH=/opt/tie-host/lib

RUN cmake -S /src/aeron -B /build/aeron-host -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/opt/tie-host \
        -DCMAKE_PREFIX_PATH=/opt/tie-host \
        -DAERON_HOST_TOOLS_ONLY=ON \
    && cmake --build /build/aeron-host --parallel \
    && cmake --install /build/aeron-host

FROM build-base AS tie-build

ARG TIE_BUILD_TYPE=Release
ARG TIE_VERSION=0.0.0-dev

COPY --from=tie-host-tools /opt/tie-host /opt/tie-host
COPY --from=sdl-windows /opt/tie-windows /opt/tie-windows
COPY --from=zstd-windows /opt/tie-windows /opt/tie-windows
COPY --from=ffmpeg-windows /opt/tie-windows /opt/tie-windows
COPY --from=midi-windows /opt/tie-windows /opt/tie-windows
COPY --from=sdl-windows /deps/SDL/LICENSE.txt /opt/tie-licenses/SDL3.txt
COPY --from=zstd-windows /deps/zstd/LICENSE /opt/tie-licenses/zstd.txt
COPY --from=ffmpeg-windows /deps/FFmpeg/COPYING.LGPLv2.1 /opt/tie-licenses/FFmpeg-LGPL-2.1.txt
COPY --from=midi-windows /opt/vcpkg/installed/x64-mingw-dynamic/share/fluidsynth/copyright \
    /opt/tie-licenses/FluidSynth-LGPL-2.1.txt
COPY --from=midi-windows /opt/vcpkg/installed/x64-mingw-dynamic/share/speexdsp/copyright \
    /opt/tie-licenses/SpeexDSP-BSD-3-Clause.txt
COPY --from=winpix-windows /opt/winpix /opt/winpix
COPY --from=winpix-windows /deps/winpix/license.txt /opt/tie-licenses/WinPixEventRuntime.txt
COPY --from=winpix-windows /deps/winpix/ThirdPartyNotices.txt /opt/tie-licenses/WinPixEventRuntime-ThirdPartyNotices.txt
COPY cmake/toolchains/mingw-w64-x86_64.cmake /opt/tie-toolchain.cmake
COPY CMakeLists.txt /src/CMakeLists.txt
COPY README.md /src/README.md
COPY aeron /src/aeron
COPY cmake /src/cmake
COPY resources /src/resources
COPY shaders /src/shaders
COPY src /src/src

ENV LD_LIBRARY_PATH=/opt/tie-host/lib

RUN case "${TIE_BUILD_TYPE}" in \
        Release) tie_debug_ui=OFF ;; \
        Debug) tie_debug_ui=ON ;; \
        *) echo "TIE_BUILD_TYPE must be Release or Debug" >&2; exit 2 ;; \
    esac \
    && cmake -S /src -B /build/tie -G Ninja \
        -DCMAKE_BUILD_TYPE="${TIE_BUILD_TYPE}" \
        -DCMAKE_TOOLCHAIN_FILE=/opt/tie-toolchain.cmake \
        -DAERON_FSR3_SHADERGEN_EXECUTABLE=/opt/tie-host/bin/aeron_fsr3_shadergen \
        -DAERON_SHADERCROSS_EXECUTABLE=/opt/tie-host/bin/shadercross \
        -DENABLE_ASAN=OFF \
        -DENABLE_FLUIDSYNTH=ON \
        -DENABLE_NUKED_SC55=ON \
        -DTIE_BUILD_TOOLS=OFF \
        -DTIE_ENABLE_DEBUG_UI="${tie_debug_ui}" \
        -DTIE_VERSION="${TIE_VERSION}" \
    && cmake --build /build/tie --target tie --parallel

RUN mkdir -p /build/package/tie-windows-x86_64/licenses \
        /build/package/tie-windows-x86_64/shaders \
    && cp /build/tie/OpenTIE.exe /build/package/tie-windows-x86_64/ \
    && cp /build/tie/shaders/*.dxil /build/tie/shaders/*.spv \
        /build/package/tie-windows-x86_64/shaders/ \
    && cp -a /build/tie/resources /build/package/tie-windows-x86_64/ \
    && cp /src/README.md \
        /build/package/tie-windows-x86_64/README.md \
    && cp /opt/tie-licenses/SDL3.txt \
        /build/package/tie-windows-x86_64/licenses/SDL3.txt \
    && cp /opt/tie-licenses/zstd.txt \
        /build/package/tie-windows-x86_64/licenses/zstd.txt \
    && cp /opt/tie-licenses/FFmpeg-LGPL-2.1.txt \
        /build/package/tie-windows-x86_64/licenses/FFmpeg-LGPL-2.1.txt \
    && cp /opt/tie-licenses/FluidSynth-LGPL-2.1.txt \
        /build/package/tie-windows-x86_64/licenses/FluidSynth-LGPL-2.1.txt \
    && cp /opt/tie-licenses/SpeexDSP-BSD-3-Clause.txt \
        /build/package/tie-windows-x86_64/licenses/SpeexDSP-BSD-3-Clause.txt \
    && cp -L /usr/share/doc/gcc-mingw-w64-x86-64-posix-runtime/copyright \
        /build/package/tie-windows-x86_64/licenses/GCC-MinGW-w64.txt \
    && cp -L /usr/share/doc/mingw-w64-x86-64-dev/copyright \
        /build/package/tie-windows-x86_64/licenses/MinGW-w64.txt \
    && cp /opt/tie-windows/bin/*.dll /build/package/tie-windows-x86_64/ \
    && cp "$(x86_64-w64-mingw32-g++ -print-file-name=libgcc_s_seh-1.dll)" \
        /build/package/tie-windows-x86_64/ \
    && cp "$(x86_64-w64-mingw32-g++ -print-file-name=libstdc++-6.dll)" \
        /build/package/tie-windows-x86_64/ \
    && cp "$(x86_64-w64-mingw32-g++ -print-file-name=libwinpthread-1.dll)" \
        /build/package/tie-windows-x86_64/ \
    && if [ "${TIE_BUILD_TYPE}" = Debug ]; then \
        cp /opt/winpix/bin/WinPixEventRuntime.dll \
            /build/package/tie-windows-x86_64/ \
        && cp /opt/tie-licenses/WinPixEventRuntime.txt \
            /build/package/tie-windows-x86_64/licenses/WinPixEventRuntime.txt \
        && cp /opt/tie-licenses/WinPixEventRuntime-ThirdPartyNotices.txt \
            /build/package/tie-windows-x86_64/licenses/WinPixEventRuntime-ThirdPartyNotices.txt; \
    fi

RUN if [ "${TIE_BUILD_TYPE}" = Debug ]; then \
        artifact_variant=debug; \
    else \
        artifact_variant=release; \
    fi \
    && package_name="opentie-${TIE_VERSION}-windows-x86_64-${artifact_variant}" \
    && mv /build/package/tie-windows-x86_64 "/build/package/${package_name}" \
    && find "/build/package/${package_name}" \
        -exec touch -h -d "@315532800" {} + \
    && mkdir -p /build/artifacts \
    && cd /build/package \
    && find "${package_name}" -print > /tmp/tie-windows-files \
    && LC_ALL=C sort -o /tmp/tie-windows-files /tmp/tie-windows-files \
    && TZ=UTC zip -X -9 -q "/build/artifacts/${package_name}.zip" \
        -@ < /tmp/tie-windows-files

FROM scratch AS artifact
COPY --from=tie-build /build/artifacts/ /
