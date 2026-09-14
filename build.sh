#!/usr/bin/env bash
# Build the model programs against a NEST build tree.
#
# An installed NEST is not enough: it ships the C++ headers but no linkable
# kernel library (see docs/01_state_of_the_kernel.md). NEST_BUILD must therefore
# point at a configured and built NEST source tree, not at an install prefix.
#
# Usage:  ./build.sh [model ...]        default: every model directory
set -euo pipefail

NEST_BUILD="${NEST_BUILD:-/home/magnus/Projects/nest/nest_master/build}"
NEST_CONFIG="${NEST_CONFIG:-${NEST_BUILD}/install/bin/nest-config}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT="${HERE}/build"

for lib in nestkernel/libnestkernel.a models/libmodels.a libnestutil/libnestutil.a; do
  [[ -f "${NEST_BUILD}/${lib}" ]] || { echo "missing ${NEST_BUILD}/${lib}" >&2; exit 1; }
done
[[ -x "${NEST_CONFIG}" ]] || { echo "missing ${NEST_CONFIG}" >&2; exit 1; }

mkdir -p "${OUT}"
CXXFLAGS=(-std=c++20 -fopenmp -O2 -Wall -I"${HERE}/include")
# shellcheck disable=SC2207
INCLUDES=($("${NEST_CONFIG}" --includes))
# shellcheck disable=SC2207
EXTLIBS=($("${NEST_CONFIG}" --libs))

build_one() {
  local src="$1"
  local name
  name="$(basename "${src}" .cpp)"
  echo "==> ${name}"
  g++ "${CXXFLAGS[@]}" "${INCLUDES[@]}" -c "${src}" -o "${OUT}/${name}.o"
  # The three archives refer to each other, so they go in one group. No
  # --whole-archive: NEST registers its models by explicit calls, not by static
  # initialisers.
  g++ -fopenmp -o "${OUT}/${name}" "${OUT}/${name}.o" \
    -Wl,--start-group \
      "${NEST_BUILD}/nestkernel/libnestkernel.a" \
      "${NEST_BUILD}/models/libmodels.a" \
      "${NEST_BUILD}/libnestutil/libnestutil.a" \
    -Wl,--end-group \
    "${EXTLIBS[@]}"
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
    build_one "${src}"
  done
done

echo "built into ${OUT}"
