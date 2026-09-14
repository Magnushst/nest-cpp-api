#!/usr/bin/env bash
# Measure how the Brunel run scales with OpenMP threads.
#
# This measures NEST, not the interface: the driver issues the same dozen calls
# at every thread count and all the work happens inside libnestkernel. The only
# lever the driver actually holds is local_num_threads, so that is what this
# varies.
#
# One warm-up run per thread count is discarded, and the summary reports the
# minimum of the timed runs rather than the mean or the median. The minimum is
# the run least contaminated by other work on the machine, and with a warm-up
# discarded it is the closest thing to the cost of the simulation itself.
#
# Note that thread count changes which virtual process owns which node and which
# random number stream it draws from, so results at more than one thread are
# statistically equivalent to the one-thread run but are NOT the same spikes.
# brunel/check.sh pins the one-thread case; this script does not check identity.
#
# Usage:  brunel/scaling.sh [scratch-dir]
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MASK="${MASK:-0,1,3,6,8,10}"
THREADS="${THREADS:-1 2 4 6}"
REPEATS="${REPEATS:-3}"
WORK="${1:-$(mktemp -d)}"
mkdir -p "${WORK}"

run_once() {  # thread-count tag -> writes ${WORK}/<tag>.out
  local t="$1" tag="$2"
  ( cd "${WORK}" && taskset -c "${MASK}" "${HERE}/build/brunel_alpha" "--threads=${t}" --quiet ) \
    > "${WORK}/${tag}.out" 2>&1
}

# NEST's own banner also prints a line starting "Simulation time (ms):", so
# match the report lines by their full fixed prefix, not by a keyword.
field() { grep -m1 -F "$2" "$1" | sed 's/.*: *//' | awk '{print $1}'; }

declare -A best_build best_sim rate_of exev_of inev_of

printf '%-8s %-10s %-12s %-12s\n' threads repeat build_s simulate_s
for t in ${THREADS}; do
  run_once "${t}" "t${t}_warmup"        # discarded
  for r in $(seq 1 "${REPEATS}"); do
    out="${WORK}/t${t}_r${r}.out"
    run_once "${t}" "t${t}_r${r}"
    build=$(field "${out}" "Building time     :")
    sim=$(field "${out}" "Simulation time   :")
    printf '%-8s %-10s %-12s %-12s\n' "${t}" "${r}" "${build}" "${sim}"

    if [[ -z "${best_build[$t]:-}" ]] || awk "BEGIN{exit !(${build} < ${best_build[$t]})}"; then
      best_build[$t]="${build}"
    fi
    if [[ -z "${best_sim[$t]:-}" ]] || awk "BEGIN{exit !(${sim} < ${best_sim[$t]})}"; then
      best_sim[$t]="${sim}"
    fi
    rate_of[$t]=$(field "${out}" "Excitatory rate   :")
    exev_of[$t]=$(field "${out}" "Excitatory events :")
    inev_of[$t]=$(field "${out}" "Inhibitory events :")
  done
done

base=""
echo
printf '%-8s %-12s %-12s %-10s %-12s %-8s %-8s\n' \
  threads build_s simulate_s speedup rate_ex_Hz ex_ev in_ev
for t in ${THREADS}; do
  [[ -z "${base}" ]] && base="${best_sim[$t]}"
  speedup=$(awk "BEGIN{printf \"%.2f\", ${base}/${best_sim[$t]}}")
  printf '%-8s %-12s %-12s %-10s %-12s %-8s %-8s\n' \
    "${t}" "${best_build[$t]}" "${best_sim[$t]}" "${speedup}" \
    "${rate_of[$t]}" "${exev_of[$t]}" "${inev_of[$t]}"
done
echo "(minimum of ${REPEATS} timed runs, one warm-up discarded, mask ${MASK})"
echo "work dir ${WORK}"
