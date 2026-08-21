#!/bin/bash

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/../.." && pwd)"

sdl_version="${SDL_VERSION:-3.4.14}"
zstd_version="${ZSTD_VERSION:-1.5.7}"
ffmpeg_version="${FFMPEG_VERSION:-8.0.1}"
fluidsynth_version="${FLUIDSYNTH_VERSION:-2.4.8}"
speexdsp_version="${SPEEXDSP_VERSION:-1.2.1}"
shadercross_commit="${SHADERCROSS_COMMIT:-6b06e55c7c5d7e7a09a8a14f76e866dcfad5ab99}"
deployment_target="${TIE_MACOS_DEPLOYMENT_TARGET:-13.0}"
architecture="${TIE_MACOS_ARCHITECTURE:-$(uname -m)}"
application_version="${TIE_VERSION:-0.0.0-dev}"
build_version="${TIE_MACOS_BUILD_VERSION:-1}"
build_type="${TIE_BUILD_TYPE:-Release}"
bundle_identifier="${TIE_MACOS_BUNDLE_IDENTIFIER:-org.totallyopen.opentie}"
sign_identity="${TIE_MACOS_SIGN_IDENTITY:--}"
notary_profile="${TIE_MACOS_NOTARY_PROFILE:-}"
notary_apple_id="${TIE_MACOS_NOTARY_APPLE_ID:-}"
notary_team_id="${TIE_MACOS_NOTARY_TEAM_ID:-}"
notary_password="${TIE_MACOS_NOTARY_PASSWORD:-}"

# The Debug variant enables the in-game debug UI and packages an OpenTIE.dSYM
# debug-symbol bundle beside the application. Dependencies are always built in
# Release mode; only OpenTIE changes configuration, so both variants share the
# dependency and shader-tool build trees.
case "${build_type}" in
    Release) build_variant=release; debug_ui=OFF ;;
    Debug) build_variant=debug; debug_ui=ON ;;
    *) echo "TIE_BUILD_TYPE must be Release or Debug" >&2; exit 2 ;;
esac

work_root="${TIE_MACOS_BUILD_ROOT:-${repo_root}/build/cache/macos-${architecture}}"
artifact_dir="${TIE_MACOS_ARTIFACT_DIR:-${repo_root}/build/artifacts}"
source_root="${work_root}/sources"
dependency_prefix="${work_root}/dependencies"
tool_prefix="${work_root}/tools"
build_root="${work_root}/build"
stage_root="${work_root}/stage-${build_variant}"
application="${stage_root}/OpenTIE.app"
dmg="${artifact_dir}/opentie-${application_version}-macos-${architecture}-${build_variant}.dmg"
dmg_volume_name="OpenTIE"
dmg_extras="${script_dir}/dmg"
dsym_bundle=""

jobs=""

# Shared signing, notarization, and DMG helpers.
source "${script_dir}/common.sh"

clone_tag() {
    local repository="$1"
    local tag="$2"
    local destination="$3"

    if [[ ! -d "${destination}/.git" ]]; then
        if [[ -e "${destination}" ]]; then
            echo "Source path exists but is not a Git checkout: ${destination}" >&2
            exit 1
        fi
        git clone --branch "${tag}" --depth 1 "${repository}" "${destination}"
    fi
}

prepare_shadercross() {
    local destination="${source_root}/SDL_shadercross"
    local patch="${repo_root}/packaging/common/shadercross-fsr3.patch"
    local checkout

    if [[ ! -d "${destination}/.git" ]]; then
        if [[ -e "${destination}" ]]; then
            echo "Source path exists but is not a Git checkout: ${destination}" >&2
            exit 1
        fi
        git clone https://github.com/libsdl-org/SDL_shadercross.git "${destination}"
        git -C "${destination}" checkout "${shadercross_commit}"
    fi

    checkout="$(git -C "${destination}" rev-parse HEAD)"
    if [[ "${checkout}" != "${shadercross_commit}" ]]; then
        echo "SDL_shadercross checkout is ${checkout}, expected ${shadercross_commit}" >&2
        exit 1
    fi

    if git -C "${destination}" apply --check "${patch}" >/dev/null 2>&1; then
        git -C "${destination}" apply "${patch}"
    elif ! git -C "${destination}" apply --reverse --check "${patch}" >/dev/null 2>&1; then
        echo "SDL_shadercross patch cannot be applied cleanly" >&2
        exit 1
    fi

    git -C "${destination}" submodule update --init --recursive
}

build_dependencies() {
    local common_cmake=(
        -G Ninja
        -DCMAKE_BUILD_TYPE=Release
        "-DCMAKE_OSX_ARCHITECTURES=${architecture}"
        "-DCMAKE_OSX_DEPLOYMENT_TARGET=${deployment_target}"
    )

    clone_tag https://github.com/libsdl-org/SDL.git \
        "release-${sdl_version}" "${source_root}/SDL-${sdl_version}"
    clone_tag https://github.com/facebook/zstd.git \
        "v${zstd_version}" "${source_root}/zstd-${zstd_version}"
    clone_tag https://github.com/FFmpeg/FFmpeg.git \
        "n${ffmpeg_version}" "${source_root}/FFmpeg-${ffmpeg_version}"
    clone_tag https://github.com/xiph/speexdsp.git \
        "SpeexDSP-${speexdsp_version}" "${source_root}/speexdsp-${speexdsp_version}"
    clone_tag https://github.com/FluidSynth/fluidsynth.git \
        "v${fluidsynth_version}" "${source_root}/fluidsynth-${fluidsynth_version}"

    cmake -S "${source_root}/SDL-${sdl_version}" \
        -B "${build_root}/sdl" \
        "${common_cmake[@]}" \
        "-DCMAKE_INSTALL_PREFIX=${dependency_prefix}" \
        -DSDL_HIDAPI_LIBUSB=OFF \
        -DSDL_INSTALL_DOCS=OFF \
        -DSDL_SHARED=ON \
        -DSDL_STATIC=OFF \
        -DSDL_TESTS=OFF
    cmake --build "${build_root}/sdl" --parallel "${jobs}"
    cmake --install "${build_root}/sdl"

    cmake -S "${source_root}/zstd-${zstd_version}/build/cmake" \
        -B "${build_root}/zstd" \
        "${common_cmake[@]}" \
        "-DCMAKE_INSTALL_PREFIX=${dependency_prefix}" \
        -DZSTD_BUILD_PROGRAMS=OFF \
        -DZSTD_BUILD_SHARED=ON \
        -DZSTD_BUILD_STATIC=OFF \
        -DZSTD_BUILD_TESTS=OFF
    cmake --build "${build_root}/zstd" --parallel "${jobs}"
    cmake --install "${build_root}/zstd"

    (
        cd "${source_root}/speexdsp-${speexdsp_version}"
        ./autogen.sh
    )
	mkdir -p "${build_root}/speexdsp"
	(
		cd "${build_root}/speexdsp"
		CFLAGS="-arch ${architecture} -mmacosx-version-min=${deployment_target}" \
			"${source_root}/speexdsp-${speexdsp_version}/configure" \
			"--prefix=${dependency_prefix}" \
			--disable-static \
			--enable-shared
        make -j"${jobs}"
        make install
    )

    cmake -S "${source_root}/fluidsynth-${fluidsynth_version}" \
        -B "${build_root}/fluidsynth" \
        "${common_cmake[@]}" \
        "-DCMAKE_INSTALL_PREFIX=${dependency_prefix}" \
        "-DCMAKE_PREFIX_PATH=${dependency_prefix}" \
        -DBUILD_SHARED_LIBS=ON \
        -Denable-aufile=OFF \
        -Denable-coreaudio=OFF \
        -Denable-coremidi=OFF \
        -Denable-dbus=OFF \
        -Denable-framework=OFF \
        -Denable-ipv6=OFF \
        -Denable-jack=OFF \
        -Denable-ladspa=OFF \
        -Denable-libinstpatch=OFF \
        -Denable-libsndfile=OFF \
        -Denable-midishare=OFF \
        -Denable-network=OFF \
        -Denable-openmp=OFF \
        -Denable-pipewire=OFF \
        -Denable-pulseaudio=OFF \
        -Denable-readline=OFF \
        -Denable-sdl2=OFF \
        -Denable-sdl3=OFF
    cmake --build "${build_root}/fluidsynth" --parallel "${jobs}"
    cmake --install "${build_root}/fluidsynth"

    mkdir -p "${build_root}/ffmpeg"
    (
        cd "${build_root}/ffmpeg"
        "${source_root}/FFmpeg-${ffmpeg_version}/configure" \
            "--prefix=${dependency_prefix}" \
            --target-os=darwin \
            "--arch=${architecture}" \
            --cc=clang \
            --cxx=clang++ \
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
            "--extra-cflags=-arch ${architecture} -mmacosx-version-min=${deployment_target}" \
            "--extra-ldflags=-arch ${architecture} -mmacosx-version-min=${deployment_target}"
        make -j"${jobs}"
        make install
    )
}

build_shadercross() {
    prepare_shadercross

    # LLVM_APPEND_VC_REV makes the vendored DXC configure run git describe,
    # which can fail transiently (5-second timeout) and change the generated
    # version headers, forcing a full DXC rebuild despite warm build trees.
    cmake -S "${source_root}/SDL_shadercross" \
        -B "${build_root}/shadercross" \
        -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        "-DCMAKE_INSTALL_PREFIX=${tool_prefix}" \
        "-DCMAKE_PREFIX_PATH=${dependency_prefix}" \
        "-DCMAKE_OSX_ARCHITECTURES=${architecture}" \
        "-DCMAKE_OSX_DEPLOYMENT_TARGET=${deployment_target}" \
        -DLLVM_APPEND_VC_REV=OFF \
        -DSDLSHADERCROSS_CLI=ON \
        -DSDLSHADERCROSS_DXC=ON \
        -DSDLSHADERCROSS_INSTALL=ON \
        -DSDLSHADERCROSS_INSTALL_RUNTIME=OFF \
        -DSDLSHADERCROSS_SHARED=ON \
        -DSDLSHADERCROSS_SPIRVCROSS_SHARED=ON \
        -DSDLSHADERCROSS_STATIC=OFF \
        -DSDLSHADERCROSS_VENDORED=ON
    cmake --build "${build_root}/shadercross" --parallel "${jobs}"
    cmake --install "${build_root}/shadercross"
}

build_application() {
    export PKG_CONFIG_PATH="${dependency_prefix}/lib/pkgconfig"

    cmake -S "${repo_root}" \
        -B "${build_root}/tie-${build_variant}" \
        -G Ninja \
        "-DCMAKE_BUILD_TYPE=${build_type}" \
        "-DCMAKE_INSTALL_PREFIX=${stage_root}" \
        "-DCMAKE_PREFIX_PATH=${dependency_prefix}" \
        "-DCMAKE_OSX_ARCHITECTURES=${architecture}" \
        "-DCMAKE_OSX_DEPLOYMENT_TARGET=${deployment_target}" \
        "-DAERON_SHADERCROSS_EXECUTABLE=${build_root}/shadercross/shadercross" \
        -DENABLE_ASAN=OFF \
        -DENABLE_FLUIDSYNTH=ON \
        -DENABLE_NUKED_SC55=ON \
        -DTIE_BUILD_TOOLS=OFF \
        "-DTIE_ENABLE_DEBUG_UI=${debug_ui}" \
        "-DTIE_MACOS_BUILD_VERSION=${build_version}" \
        "-DTIE_MACOS_BUNDLE_IDENTIFIER=${bundle_identifier}" \
        "-DTIE_VERSION=${application_version}"
    cmake --build "${build_root}/tie-${build_variant}" --target tie \
        --parallel "${jobs}"

    cmake -E remove_directory "${stage_root}"
    cmake --install "${build_root}/tie-${build_variant}"

    # On Apple platforms DWARF stays in the object files; a distributable
    # debug package needs the linked debug info extracted into a dSYM bundle.
    if [[ "${build_type}" == Debug ]]; then
        dsym_bundle="${stage_root}/OpenTIE.dSYM"
        dsymutil "${application}/Contents/MacOS/OpenTIE" -o "${dsym_bundle}"
    fi
}

stage_licenses() {
    local license_dir="${application}/Contents/Resources/licenses"

    mkdir -p "${license_dir}"
    cp "${source_root}/SDL-${sdl_version}/LICENSE.txt" "${license_dir}/SDL3.txt"
    cp "${source_root}/zstd-${zstd_version}/LICENSE" "${license_dir}/zstd.txt"
    cp "${source_root}/FFmpeg-${ffmpeg_version}/COPYING.LGPLv2.1" \
        "${license_dir}/FFmpeg-LGPL-2.1.txt"
    cp "${source_root}/fluidsynth-${fluidsynth_version}/LICENSE" \
        "${license_dir}/FluidSynth-LGPL-2.1.txt"
    cp "${source_root}/speexdsp-${speexdsp_version}/COPYING" \
        "${license_dir}/SpeexDSP-BSD-3-Clause.txt"
    cp "${repo_root}/aeron/third_party/bc7enc/LICENSE" "${license_dir}/bc7enc.txt"
    cp "${repo_root}/aeron/third_party/fidelityfx-fsr3/LICENSE.txt" \
        "${license_dir}/FidelityFX-FSR3.txt"
    cp "${repo_root}/aeron/third_party/libyaml/License" "${license_dir}/libyaml.txt"
    cp "${repo_root}/packaging/macos/PACKAGE-README.md" \
        "${application}/Contents/Resources/README.md"
}

main() {
    local host_architecture

    for command in clang cmake codesign ditto file git hdiutil make ninja \
            pkg-config sysctl xcrun; do
        require_command "${command}"
    done
    if [[ "${build_type}" == Debug ]]; then
        require_command dsymutil
    fi

    resolve_notary_arguments

    host_architecture="$(uname -m)"
    if [[ "${architecture}" != "${host_architecture}" ]]; then
        echo "This package script builds native host tools and target binaries together." >&2
        echo "Run it on a ${architecture} Mac instead of ${host_architecture}." >&2
        exit 1
    fi
    jobs="$(sysctl -n hw.logicalcpu)"

    mkdir -p "${source_root}" "${dependency_prefix}" "${tool_prefix}" \
        "${build_root}" "${artifact_dir}"

    build_dependencies
    build_shadercross
    build_application
    stage_licenses

    sign_bundle

    if [[ "${notarize_enabled}" -eq 1 ]]; then
        notarize_application
    fi

    create_dmg

    if [[ "${notarize_enabled}" -eq 1 ]]; then
        notarize_dmg
    fi

    echo "Created ${dmg}"
    echo "Application bundle: ${application}"
}

main "$@"
