#!/usr/bin/env bash
# Build the programs in this repository against NEST.
#
# Two ways, and the script takes whichever it is given:
#
#   NEST_PREFIX=/path/to/install ./build.sh
#       Against an installed NEST. Needs one with the kernel libraries, which
#       is what packaging/install-kernel-library.patch adds and what
#       nest-config --kernel-libs reports. This is how a user outside this
#       project would build, and it is the case the patch exists to enable.
#
#   NEST_BUILD=/path/to/build ./build.sh
#       Against a configured and built NEST source tree, naming the three
#       archives inside it directly. This is the fallback for a NEST without
#       the patch, and it is the default here.
#
# Usage:  ./build.sh [model ...]        default: every directory holding a .cpp
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT="${HERE}/build"

if [[ -n "${NEST_PREFIX:-}" ]]; then
  NEST_CONFIG="${NEST_CONFIG:-${NEST_PREFIX}/bin/nest-config}"
  [[ -x "${NEST_CONFIG}" ]] || { echo "missing ${NEST_CONFIG}" >&2; exit 1; }
  # shellcheck disable=SC2207
  KERNEL_LIBS=($("${NEST_CONFIG}" --kernel-libs 2>/dev/null || true))
  [[ ${#KERNEL_LIBS[@]} -gt 0 ]] || {
    echo "${NEST_CONFIG} has no --kernel-libs: that NEST does not install its" >&2
    echo "kernel libraries. Apply packaging/install-kernel-library.patch, or" >&2
    echo "build against a NEST build tree with NEST_BUILD instead." >&2
    exit 1; }
else
  NEST_BUILD="${NEST_BUILD:-/home/magnus/Projects/nest/nest_master/build}"
  NEST_CONFIG="${NEST_CONFIG:-${NEST_BUILD}/install/bin/nest-config}"
  for lib in nestkernel/libnestkernel.a models/libmodels.a libnestutil/libnestutil.a; do
    [[ -f "${NEST_BUILD}/${lib}" ]] || { echo "missing ${NEST_BUILD}/${lib}" >&2; exit 1; }
  done
  [[ -x "${NEST_CONFIG}" ]] || { echo "missing ${NEST_CONFIG}" >&2; exit 1; }
  # The three archives refer to each other, so they go in one group. No
  # --whole-archive: NEST registers its models by explicit calls, not by static
  # initialisers.
  # shellcheck disable=SC2207
  KERNEL_LIBS=(-Wl,--start-group
    "${NEST_BUILD}/nestkernel/libnestkernel.a"
    "${NEST_BUILD}/models/libmodels.a"
    "${NEST_BUILD}/libnestutil/libnestutil.a"
    -Wl,--end-group
    $("${NEST_CONFIG}" --libs))
fi

mkdir -p "${OUT}"
# include/nest is on the path so that a model includes "nest_api.h" exactly as
# it would against an installed NEST, whose headers land in include/nest.
CXXFLAGS=(-std=c++20 -fopenmp -O2 -Wall -I"${HERE}/include" -I"${HERE}/include/nest")
# shellcheck disable=SC2207
INCLUDES=($("${NEST_CONFIG}" --includes))

build_one() {
  local src="$1" model="$2"
  local name
  name="$(basename "${src}" .cpp)"
  echo "==> ${name}"
  g++ "${CXXFLAGS[@]}" "${INCLUDES[@]}" -I"${HERE}/${model}" -c "${src}" -o "${OUT}/${name}.o"
  g++ -fopenmp -o "${OUT}/${name}" "${OUT}/${name}.o" "${KERNEL_LIBS[@]}"
}

# A model is a directory holding one or more .cpp programs.
models=( "$@" )
if [[ ${#models[@]} -eq 0 ]]; then
  for dir in "${HERE}"/*/; do
    [[ -d "${dir}" ]] || continue
    compgen -G "${dir}*.cpp" > /dev/null && models+=( "$(basename "${dir}")" )
  done
fi

[[ ${#models[@]} -gt 0 ]] || { echo "no model directories found" >&2; exit 1; }

for model in "${models[@]}"; do
  for src in "${HERE}/${model}"/*.cpp; do
    build_one "${src}" "${model}"
  done
done

echo "built into ${OUT}"
