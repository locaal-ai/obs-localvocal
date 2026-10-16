#!/usr/bin/env bash
# Build the obs-localvocal Flatpak extension.
#
# Usage:
#   [ACCELERATION=<value>] ./flatpak/build.sh [flatpak-builder options] <build-dir>
#
# ACCELERATION controls the GPU/CPU backend compiled into the plugin.
# Allowed values (default: generic):
#   generic  – portable CPU-only build (OpenBLAS)
#   nvidia   – CUDA-accelerated build
#   amd      – ROCm/HIP-accelerated build
#
# Examples:
#   ./flatpak/build.sh build-dir
#   ACCELERATION=nvidia ./flatpak/build.sh --install build-dir
#   ACCELERATION=amd    ./flatpak/build.sh --repo=repo build-dir

set -euo pipefail

ACCELERATION="${ACCELERATION:-generic}"

case "${ACCELERATION}" in
    generic | nvidia | amd) ;;
    *)
        printf 'Error: ACCELERATION="%s" is invalid. Allowed values: generic, nvidia, amd.\n' \
            "${ACCELERATION}" >&2
        exit 1
        ;;
esac

# Dependencies: flatpak-builder, rsync
if ! command -v rsync &>/dev/null; then
    printf 'Error: rsync is required but not found in PATH.\n' >&2
    printf 'Install it with: sudo apt install rsync  (or equivalent for your distro)\n' >&2
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
MANIFEST="${SCRIPT_DIR}/com.obsproject.Studio.Plugin.LocalVocal.yaml"

# Create a temporary directory and ensure it is removed on exit
WORK_DIR="$(mktemp -d)"
trap 'rm -rf "${WORK_DIR}"' EXIT

# Produce a patched copy of the manifest with the requested ACCELERATION value.
# Four substitutions are needed:
#   1. build-options.env entry:    ACCELERATION: generic  → ACCELERATION: <value>
#   2. CMake cache variable:       -DACCELERATION=generic → -DACCELERATION=<value>
#   3. prebuilt whisper.cpp URL:   generic → <value>
#   4. prebuilt whisper.cpp SHA256: generic hash → <value> hash

# SHA256 hashes of the prebuilt whisper.cpp Linux x86_64 tarballs (version 0.0.13)
WHISPER_PREBUILT_HASH_generic="5811798e245482597ad393fac7bab82f0df8664e6b82be6231d089af13de0656"
WHISPER_PREBUILT_HASH_nvidia="dc00f91f5ddfb8271fa177b005022dc6ccc979ab669ee8e92008a7dc694295ad"
WHISPER_PREBUILT_HASH_amd="2334e6bfc40d0fd631ee67711598ead0a7375fac3dea18529d15147770128c27"

# Select the target hash using an indirect variable reference (bash 4+)
HASH_VAR="WHISPER_PREBUILT_HASH_${ACCELERATION}"
WHISPER_HASH_NEW="${!HASH_VAR}"

if [[ -z "${WHISPER_HASH_NEW}" ]]; then
    printf 'Error: no prebuilt SHA256 defined for ACCELERATION="%s"\n' "${ACCELERATION}" >&2
    exit 1
fi

# Create a clean copy of the repo for the type:dir source.
# The working tree contains build-dir/var/run -> /run (which has sockets, device
# files, etc.) that flatpak-builder cannot copy.  rsync with --exclude handles
# this cleanly before flatpak-builder ever sees the source directory.
REPO_CLEAN="${WORK_DIR}/repo"
rsync -a --delete \
    --exclude='.git/' \
    --exclude='build-dir/' \
    --exclude='.flatpak-builder/' \
    --exclude='build/' \
    --exclude='deps/c-webvtt-in-video-stream/target/' \
    "${REPO_DIR}/" "${REPO_CLEAN}/"

PATCHED="${WORK_DIR}/$(basename "${MANIFEST}")"
sed \
    -e "s/ACCELERATION: generic/ACCELERATION: ${ACCELERATION}/" \
    -e "s/-DACCELERATION=generic/-DACCELERATION=${ACCELERATION}/" \
    -e "s/whispercpp-linux-x86_64-generic-Release/whispercpp-linux-x86_64-${ACCELERATION}-Release/g" \
    -e "s/${WHISPER_PREBUILT_HASH_generic}/${WHISPER_HASH_NEW}/" \
    -e "s|        path: \.\.|        path: ${REPO_CLEAN}|" \
    "${MANIFEST}" > "${PATCHED}"

# For non-nvidia variants, remove the CUDA runtime module (not needed).
if [[ "${ACCELERATION}" != "nvidia" ]]; then
    sed -i '/# BEGIN_CUDA_RUNTIME_MODULE/,/# END_CUDA_RUNTIME_MODULE/d' "${PATCHED}"
fi

# Copy all local files referenced by the manifest so flatpak-builder can find
# them relative to the patched manifest location in WORK_DIR.
cp "${SCRIPT_DIR}/com.obsproject.Studio.Plugin.LocalVocal.metainfo.xml" "${WORK_DIR}/"
cp "${SCRIPT_DIR}/cargo-sources.json" "${WORK_DIR}/"

# For the nvidia variant, libggml-cuda.so (from the occ-ai prebuilt tarball) is loaded
# dynamically at runtime via ggml_backend_load_all_from_path() and resolves its own CUDA
# dependencies.  obs-localvocal.so does not call CUDA APIs directly, so no build-time
# CUDA toolkit or stub module is needed.
# At runtime, libcuda.so.1 is provided by org.freedesktop.Platform.GL.nvidia, which OBS
# Studio mounts automatically when an NVIDIA driver is present on the host.

exec flatpak-builder "$@" "${PATCHED}"
