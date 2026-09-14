#!/usr/bin/env bash
# Build the drafts against a NEST *build tree*.
#
# An installed NEST is not enough: it ships the C++ headers but no linkable
# kernel library (see docs/01_state_of_the_kernel.md). NEST_BUILD must therefore
# point at a configured and built NEST source tree, not at an install prefix.
set -euo pipefail

NEST_BUILD="${NEST_BUILD:-/home/magnus/Projects/nest/nest_master/build}"
NEST_CONFIG="${NEST_CONFIG:-${NEST_BUILD}/install/bin/nest-config}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT="${HERE}/build"

for lib in nestkernel/libnestkernel.a models/libmodels.a libnestutil/libnestutil.a; do
  [[ -f "${NEST_BUILD}/${lib}" ]] || { echo "missing ${NEST_BUILD}/${lib}" >&2; exit 1; }
done

mkdir -p "${OUT}"
CXXFLAGS=(-std=c++20 -fopenmp -O2 -Wall)
# shellcheck disable=SC2207
INCLUDES=($("${NEST_CONFIG}" --includes))
# shellcheck disable=SC2207
EXTLIBS=($("${NEST_CONFIG}" --libs))

build_one() {
  local src="$1" name="$2"; shift 2
  echo "==> ${name}"
  g++ "${CXXFLAGS[@]}" "${INCLUDES[@]}" "$@" -c "${src}" -o "${OUT}/${name}.o"
  g++ -fopenmp -o "${OUT}/${name}" "${OUT}/${name}.o" \
    -Wl,--start-group \
      "${NEST_BUILD}/nestkernel/libnestkernel.a" \
      "${NEST_BUILD}/models/libmodels.a" \
      "${NEST_BUILD}/libnestutil/libnestutil.a" \
    -Wl,--end-group \
    "${EXTLIBS[@]}"
}

build_one "${HERE}/reference/brunel_alpha_raw.cpp" brunel_alpha_raw
build_one "${HERE}/draft/examples/brunel_alpha.cpp" brunel_alpha -I"${HERE}/draft/include"

echo "built: ${OUT}/brunel_alpha_raw ${OUT}/brunel_alpha"
