# Microsoft's Linux DXC release binaries are available for x86-64 only.
ARG TIE_BUILD_PLATFORM=linux/amd64

FROM --platform=${TIE_BUILD_PLATFORM} debian:bookworm-slim AS build-base

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        ca-certificates \
        cmake \
        autoconf \
        automake \
        file \
        git \
        libasound2-dev \
        libdbus-1-dev \
        libdecor-0-dev \
        libdrm-dev \
        libegl1-mesa-dev \
        libgbm-dev \
        libglib2.0-dev \
        libgl-dev \
        libibus-1.0-dev \
        libtool \
        libncurses-dev \
        libpipewire-0.3-dev \
        libpulse-dev \
        libudev-dev \
        liburing-dev \
        libvulkan-dev \
        libwayland-dev \
        libx11-dev \
        libxcursor-dev \
        libxext-dev \
        libxfixes-dev \
        libxi-dev \
        libxkbcommon-dev \
        libxml2-dev \
        libxrandr-dev \
        libxss-dev \
        libxtst-dev \
        nasm \
        ninja-build \
        patchelf \
        pkg-config \
        python3 \
        wayland-protocols \
        xz-utils \
        zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

ARG SDL_VERSION=3.4.14
ARG ZSTD_VERSION=1.5.7
ARG FFMPEG_VERSION=8.0.1
ARG FLUIDSYNTH_VERSION=2.4.8
ARG SPEEXDSP_VERSION=1.2.1
ARG SHADERCROSS_COMMIT=6b06e55c7c5d7e7a09a8a14f76e866dcfad5ab99

# The dependencies build as independent stages so that a version bump only
# rebuilds the affected dependency and so that cold builds run concurrently.

FROM build-base AS sdl-build

RUN git clone --branch "release-${SDL_VERSION}" --depth 1 \
        https://github.com/libsdl-org/SDL.git /deps/SDL \
    && cmake -S /deps/SDL -B /build/sdl -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/opt/tie-linux \
        -DSDL_INSTALL_DOCS=OFF \
        -DSDL_SHARED=ON \
        -DSDL_STATIC=OFF \
        -DSDL_TESTS=OFF \
    && cmake --build /build/sdl --parallel \
    && cmake --install /build/sdl

FROM build-base AS zstd-build

RUN git clone --branch "v${ZSTD_VERSION}" --depth 1 \
        https://github.com/facebook/zstd.git /deps/zstd \
    && cmake -S /deps/zstd/build/cmake -B /build/zstd -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/opt/tie-linux \
        -DZSTD_BUILD_PROGRAMS=OFF \
        -DZSTD_BUILD_SHARED=ON \
        -DZSTD_BUILD_STATIC=OFF \
        -DZSTD_BUILD_TESTS=OFF \
    && cmake --build /build/zstd --parallel \
    && cmake --install /build/zstd

FROM build-base AS ffmpeg-build

RUN git clone --branch "n${FFMPEG_VERSION}" --depth 1 \
        https://github.com/FFmpeg/FFmpeg.git /deps/FFmpeg \
    && cd /deps/FFmpeg \
    && ./configure \
        --prefix=/opt/tie-linux \
        --disable-autodetect \
        --disable-avdevice \
        --disable-avfilter \
        --disable-debug \
        --disable-doc \
        --disable-everything \
        --disable-network \
        --disable-programs \
        --disable-static \
        --enable-decoder=vorbis \
        --enable-demuxer=ogg \
        --enable-pic \
        --enable-pthreads \
        --enable-shared \
    && make -j"$(nproc)" \
    && make install

FROM build-base AS speexdsp-build

RUN git clone --branch "SpeexDSP-${SPEEXDSP_VERSION}" --depth 1 \
        https://github.com/xiph/speexdsp.git /deps/speexdsp \
    && cd /deps/speexdsp \
    && ./autogen.sh \
    && ./configure \
        --prefix=/opt/tie-linux \
        --disable-static \
        --enable-shared \
    && make -j"$(nproc)" \
    && make install

FROM build-base AS fluidsynth-build

RUN git clone --branch "v${FLUIDSYNTH_VERSION}" --depth 1 \
        https://github.com/FluidSynth/fluidsynth.git /deps/fluidsynth \
    && cmake -S /deps/fluidsynth -B /build/fluidsynth -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/opt/tie-linux \
        -DBUILD_SHARED_LIBS=ON \
        -Denable-alsa=OFF \
        -Denable-aufile=OFF \
        -Denable-dbus=OFF \
        -Denable-ipv6=OFF \
        -Denable-jack=OFF \
        -Denable-ladspa=OFF \
        -Denable-libinstpatch=OFF \
        -Denable-libsndfile=OFF \
        -Denable-midishare=OFF \
        -Denable-network=OFF \
        -Denable-openmp=OFF \
        -Denable-oss=OFF \
        -Denable-pipewire=OFF \
        -Denable-pulseaudio=OFF \
        -Denable-readline=OFF \
        -Denable-sdl2=OFF \
        -Denable-sdl3=OFF \
        -Denable-systemd=OFF \
    && cmake --build /build/fluidsynth --parallel \
    && cmake --install /build/fluidsynth

FROM build-base AS shader-tools

COPY packaging/common/shadercross-fsr3.patch /tmp/shadercross-fsr3.patch

# DXC 1.9 requires glibc 2.38; keep the Debian 12-compatible 1.8 release.
ARG DXC_VERSION=1.8.2502
ARG DXC_LINUX_X64_ARCHIVE=linux_dxc_2025_02_20.x86_64.tar.gz
ARG DXC_LINUX_X64_SHA256=e0580d90dbf6053a783ddd8d5153285f0606e5deaad17a7a6452f03acdf88c71

ADD --checksum=sha256:${DXC_LINUX_X64_SHA256} \
    https://github.com/microsoft/DirectXShaderCompiler/releases/download/v${DXC_VERSION}/${DXC_LINUX_X64_ARCHIVE} \
    /tmp/dxc.tar.gz

# shadercross links SDL but needs neither zstd nor FFmpeg.
COPY --from=sdl-build /opt/tie-linux /opt/tie-linux

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
        -DCMAKE_INSTALL_PREFIX=/opt/tie-tools \
        -DSPIRV_CROSS_CLI=OFF \
        -DSPIRV_CROSS_ENABLE_TESTS=OFF \
        -DSPIRV_CROSS_SHARED=ON \
        -DSPIRV_CROSS_STATIC=OFF \
    && cmake --build /build/spirv-cross --parallel \
    && cmake --install /build/spirv-cross \
    && cmake -S /deps/SDL_shadercross -B /build/shadercross -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/opt/tie-tools \
        -DCMAKE_PREFIX_PATH="/opt/tie-linux;/opt/tie-tools" \
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
        /opt/tie-tools/lib/

FROM build-base AS tie-build

ARG TIE_BUILD_TYPE=Release
ARG TIE_VERSION=0.0.0-dev

COPY --from=sdl-build /opt/tie-linux /opt/tie-linux
COPY --from=zstd-build /opt/tie-linux /opt/tie-linux
COPY --from=ffmpeg-build /opt/tie-linux /opt/tie-linux
COPY --from=fluidsynth-build /opt/tie-linux /opt/tie-linux
COPY --from=speexdsp-build /opt/tie-linux /opt/tie-linux
COPY --from=sdl-build /deps/SDL/LICENSE.txt /opt/tie-licenses/SDL3.txt
COPY --from=zstd-build /deps/zstd/LICENSE /opt/tie-licenses/zstd.txt
COPY --from=ffmpeg-build /deps/FFmpeg/COPYING.LGPLv2.1 /opt/tie-licenses/FFmpeg-LGPL-2.1.txt
COPY --from=fluidsynth-build /deps/fluidsynth/LICENSE /opt/tie-licenses/FluidSynth-LGPL-2.1.txt
COPY --from=speexdsp-build /deps/speexdsp/COPYING /opt/tie-licenses/SpeexDSP-BSD-3-Clause.txt
COPY --from=shader-tools /opt/tie-tools /opt/tie-tools
COPY CMakeLists.txt /src/CMakeLists.txt
COPY README.md /src/README.md
COPY aeron /src/aeron
COPY cmake /src/cmake
COPY resources /src/resources
COPY shaders /src/shaders
COPY src /src/src

ENV LD_LIBRARY_PATH=/opt/tie-tools/lib:/opt/tie-linux/lib
ENV PKG_CONFIG_PATH=/opt/tie-linux/lib/pkgconfig

RUN case "${TIE_BUILD_TYPE}" in \
        Release) tie_debug_ui=OFF ;; \
        Debug) tie_debug_ui=ON ;; \
        *) echo "TIE_BUILD_TYPE must be Release or Debug" >&2; exit 2 ;; \
    esac \
    && cmake -S /src -B /build/tie -G Ninja \
        -DCMAKE_BUILD_TYPE="${TIE_BUILD_TYPE}" \
        -DCMAKE_PREFIX_PATH=/opt/tie-linux \
        -DAERON_SHADERCROSS_EXECUTABLE=/opt/tie-tools/bin/shadercross \
        -DENABLE_ASAN=OFF \
        -DENABLE_FLUIDSYNTH=ON \
        -DENABLE_NUKED_SC55=ON \
        -DTIE_BUILD_TOOLS=OFF \
        -DTIE_ENABLE_DEBUG_UI="${tie_debug_ui}" \
        -DTIE_VERSION="${TIE_VERSION}" \
    && cmake --build /build/tie --target tie --parallel

RUN mkdir -p /build/package/tie-linux-x86_64/licenses \
        /build/package/tie-linux-x86_64/lib \
        /build/package/tie-linux-x86_64/shaders \
    && cp /build/tie/OpenTIE /build/package/tie-linux-x86_64/ \
    && cp /build/tie/shaders/*.spv \
        /build/package/tie-linux-x86_64/shaders/ \
    && cp -a /build/tie/resources /build/package/tie-linux-x86_64/ \
    && cp /src/README.md \
        /build/package/tie-linux-x86_64/README.md \
    && cp /opt/tie-licenses/SDL3.txt \
        /build/package/tie-linux-x86_64/licenses/SDL3.txt \
    && cp /opt/tie-licenses/zstd.txt \
        /build/package/tie-linux-x86_64/licenses/zstd.txt \
    && cp /opt/tie-licenses/FFmpeg-LGPL-2.1.txt \
        /build/package/tie-linux-x86_64/licenses/FFmpeg-LGPL-2.1.txt \
    && cp /opt/tie-licenses/FluidSynth-LGPL-2.1.txt \
        /build/package/tie-linux-x86_64/licenses/FluidSynth-LGPL-2.1.txt \
    && cp /opt/tie-licenses/SpeexDSP-BSD-3-Clause.txt \
        /build/package/tie-linux-x86_64/licenses/SpeexDSP-BSD-3-Clause.txt \
    && cp -L /opt/tie-linux/lib/libSDL3.so.0 \
        /build/package/tie-linux-x86_64/lib/ \
    && cp -L /opt/tie-linux/lib/libavcodec.so.62 \
        /build/package/tie-linux-x86_64/lib/ \
    && cp -L /opt/tie-linux/lib/libavformat.so.62 \
        /build/package/tie-linux-x86_64/lib/ \
    && cp -L /opt/tie-linux/lib/libavutil.so.60 \
        /build/package/tie-linux-x86_64/lib/ \
    && cp -L /opt/tie-linux/lib/libswresample.so.6 \
        /build/package/tie-linux-x86_64/lib/ \
    && cp -L /opt/tie-linux/lib/libswscale.so.9 \
        /build/package/tie-linux-x86_64/lib/ \
    && cp -L /opt/tie-linux/lib/libzstd.so.1 \
        /build/package/tie-linux-x86_64/lib/ \
    && cp -L /opt/tie-linux/lib/libfluidsynth.so.3 \
        /build/package/tie-linux-x86_64/lib/ \
    && cp -L /opt/tie-linux/lib/libspeexdsp.so.1 \
        /build/package/tie-linux-x86_64/lib/ \
    && patchelf --set-rpath '$ORIGIN/lib' \
        /build/package/tie-linux-x86_64/OpenTIE \
    && for library in /build/package/tie-linux-x86_64/lib/*.so*; do \
        patchelf --set-rpath '$ORIGIN' "$library"; \
    done

RUN if [ "${TIE_BUILD_TYPE}" = Debug ]; then \
        artifact_variant=debug; \
    else \
        artifact_variant=release; \
    fi \
    && package_name="opentie-${TIE_VERSION}-linux-x86_64-${artifact_variant}" \
    && mv /build/package/tie-linux-x86_64 "/build/package/${package_name}" \
    && mkdir -p /build/artifacts \
    && tar \
        --sort=name \
        --mtime="@0" \
        --owner=0 \
        --group=0 \
        --numeric-owner \
        -C /build/package \
        -cJf "/build/artifacts/${package_name}.tar.xz" \
        "${package_name}"

FROM scratch AS artifact
COPY --from=tie-build /build/artifacts/ /
