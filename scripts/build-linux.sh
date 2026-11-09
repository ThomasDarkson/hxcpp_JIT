#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
version=23.1.5
jobs=${JOBS:-2}
engine=${CONTAINER_ENGINE:-}
image=${LINUX_BUILD_IMAGE:-quay.io/pypa/manylinux_2_28_x86_64:latest}
while (($#)); do
    case "$1" in
        --llvm-version) version=${2:?Missing LLVM version}; shift 2 ;;
        --jobs) jobs=${2:?Missing job count}; shift 2 ;;
        --engine) engine=${2:?Missing container engine}; shift 2 ;;
        --image) image=${2:?Missing image}; shift 2 ;;
        -h|--help) echo 'Usage: scripts/build-linux.sh [--llvm-version 23.1.5] [--jobs 2] [--engine docker|podman] [--image IMAGE]'; exit 0 ;;
        *) echo "Unknown option: $1" >&2; exit 2 ;;
    esac
done
[[ $version =~ ^[0-9]+\.[0-9]+\.[0-9]+$ && $jobs =~ ^[1-9][0-9]*$ ]] || { echo 'Invalid version or job count' >&2; exit 2; }
if [[ -z $engine ]]; then
    for candidate in docker podman; do
        if command -v "$candidate" >/dev/null; then engine=$candidate; break; fi
    done
fi
[[ -n $engine ]] || { echo 'Install Docker or Podman for the glibc 2.28 container build.' >&2; exit 1; }
options=(--rm --platform linux/amd64 --user "$(id -u):$(id -g)")
if [[ ${engine##*/} == podman ]]; then options+=(--userns=keep-id); fi
"$engine" run "${options[@]}" -v "$root:/work:Z" -w /work \
    -e LLVM_VERSION="$version" -e JOBS="$jobs" -e BUILD_IMAGE="$image" \
    "$image" bash scripts/linux/build-inside.sh
