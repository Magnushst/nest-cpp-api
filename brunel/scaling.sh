#!/usr/bin/env bash
# Measure how the Brunel run scales with OpenMP threads.
#
# This measures NEST, not the interface: the driver issues the same dozen calls at
# every thread count and all the work happens inside libnestkernel. The only
# lever the driver actually holds is local_num_threads, so that is what this
# varies.
#
# Note that thread count changes which virtual process owns which node and which
# random number stream it draws from, so results at more than one thread are
# statistically equivalent to the one-thread run but are NOT the same spikes.
# brunel/check.sh pins the one-thread case; this script does not check
# identity.
#
# Usage:  brunel/scaling.sh [scratch-dir]
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MASK="${MASK:-0,1,3,6,8,10}"
THREADS="${THREADS:-1 2 4 6}"
REPEATS="${REPEATS:-3}"
WORK="${1:-$(mktemp -d)}"
mkdir -p "${WORK}"

printf '%-8s %-10s %-12s %-12s %-10s %-8s %-8s\n' \
  threads repeat build_s simulate_s rate_ex_Hz ex_ev in_ev

for t in ${THREADS}; do
  for r in $(seq 1 "${REPEATS}"); do
    out="${WORK}/t${t}_r${r}.out"
    ( cd "${WORK}" && taskset -c "${MASK}" "${HERE}/build/brunel_alpha" "--threads=${t}" --quiet ) \
      > "${out}" 2>&1
    # NEST's own banner also prints a line starting "Simulation time (ms):",
    # so match the report lines by their full fixed prefix, not by a keyword.
    field() { grep -m1 -F "$1" "${out}" | sed 's/.*: *//' | awk '{print $1}'; }
    build=$(field "Building time     :")
    sim=$(field "Simulation time   :")
    rate=$(field "Excitatory rate   :")
    exev=$(field "Excitatory events :")
    inev=$(field "Inhibitory events :")
    printf '%-8s %-10s %-12s %-12s %-10s %-8s %-8s\n' "${t}" "${r}" "${build}" "${sim}" "${rate}" "${exev}" "${inev}"
  done
done

echo
echo "work dir ${WORK}"
