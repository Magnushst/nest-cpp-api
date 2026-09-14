#!/usr/bin/env bash
# Compare C++ and PyNEST cost on the same Brunel network.
#
# All three implementations issue the same dozen calls into the same
# libnestkernel, so the kernel phases should cost the same and any difference
# must sit in the driver. Two clocks are reported for exactly that reason:
#
#   create / connect / simulate  NEST's own kernel stopwatches, read out of the
#                                kernel status at the end of the run. These
#                                cover the work the kernel does and exclude
#                                everything the driver does around it.
#   total                        wall clock for the whole process, including
#                                interpreter start-up and module imports for
#                                Python and dynamic linking for C++.
#
# A third figure, "outside", is the whole-process time less the three phase
# timers: process start-up, kernel start-up and model registration, prepare and
# cleanup, flushing the spike files, and for Python the interpreter and its
# imports. That is the only part of the run the choice of driver can touch.
#
# Every implementation runs single threaded, because the Python reference does
# and a thread count change would also change which random stream each node
# draws from. One warm-up run per implementation is discarded and the minimum of
# the timed runs is reported, for the reasons given in brunel/scaling.sh.
#
# The timed runs are interleaved, one round of all three implementations at a
# time, rather than grouped by implementation, and the order within a round is
# reversed on every second round. Grouping confounds the comparison with
# anything that drifts over the session: in a grouped run the three arrived in
# decreasing order of time with decreasing spread, which is a machine settling
# and not a property of the drivers. Interleaving in a fixed order leaves a
# smaller version of the same problem, so the order alternates and every
# implementation runs first as often as it runs last.
#
# Usage:  brunel/parity.sh [scratch-dir]
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
NEST_BUILD="${NEST_BUILD:-/home/magnus/Projects/nest/nest_master/build}"
PYNEST="${PYNEST:-${NEST_BUILD}/install/lib/python3.14/site-packages}"
PYTHON="${PYTHON:-/usr/bin/python3.14}"
MASK="${MASK:-0,1,3,6,8,10}"
REPEATS="${REPEATS:-4}"   # kept even, so each order runs the same number of times
WORK="${1:-$(mktemp -d)}"
mkdir -p "${WORK}"

echo "== building"
NEST_BUILD="${NEST_BUILD}" "${HERE}/build.sh" > "${WORK}/build.log" 2>&1 || { cat "${WORK}/build.log"; exit 1; }

run_once() {  # impl tag -> writes ${WORK}/<tag>.out and ${WORK}/<tag>.wall
  local impl="$1" tag="$2" start stop
  start=$(date +%s.%N)
  case "${impl}" in
    raw) ( cd "${WORK}" && taskset -c "${MASK}" "${HERE}/build/brunel_alpha_raw" --quiet ) > "${WORK}/${tag}.out" 2>&1 ;;
    hpp) ( cd "${WORK}" && taskset -c "${MASK}" "${HERE}/build/brunel_alpha"     --quiet ) > "${WORK}/${tag}.out" 2>&1 ;;
    py)  ( cd "${WORK}" && PYTHONPATH="${PYNEST}" taskset -c "${MASK}" "${PYTHON}" "${HERE}/brunel/brunel_alpha_ref.py" ) > "${WORK}/${tag}.out" 2>&1 ;;
  esac
  stop=$(date +%s.%N)
  awk "BEGIN{printf \"%.3f\", ${stop} - ${start}}" > "${WORK}/${tag}.wall"
}

# NEST's banner also prints a line starting "Simulation time (ms):", so match
# the report lines by their full fixed prefix rather than by a keyword.
field() { grep -m1 -F "$2" "$1" | sed 's/.*: *//' | awk '{print $1}'; }
# "Building time     : 1.234 s (create 0.5, connect 0.7)"
paren() { grep -m1 -F "Building time" "$1" | sed 's/.*(//; s/)//' | awk -v k="$2" '{for(i=1;i<NF;i++) if($i==k) print $(i+1)}' | tr -d ','; }

declare -A c_min n_min s_min w_min o_min

for impl in py raw hpp; do run_once "${impl}" "${impl}_warmup"; done   # discarded

printf '%-8s %-6s %-10s %-10s %-11s %-10s %-10s\n' round impl create_s connect_s simulate_s outside_s total_s
for r in $(seq 1 "${REPEATS}"); do
  if (( r % 2 )); then order="py raw hpp"; else order="hpp raw py"; fi
  for impl in ${order}; do
    out="${WORK}/${impl}_r${r}.out"
    run_once "${impl}" "${impl}_r${r}"
    create=$(paren "${out}" create)
    connect=$(paren "${out}" connect)
    sim=$(field "${out}" "Simulation time   :")
    wall=$(cat "${WORK}/${impl}_r${r}.wall")
    outside=$(awk "BEGIN{printf \"%.3f\", ${wall} - ${create} - ${connect} - ${sim}}")
    printf '%-8s %-6s %-10s %-10s %-11s %-10s %-10s\n' "${r}" "${impl}" "${create}" "${connect}" "${sim}" "${outside}" "${wall}"
    for pair in "c_min ${create}" "n_min ${connect}" "s_min ${sim}" "w_min ${wall}" "o_min ${outside}"; do
      set -- ${pair}
      declare -n tgt="$1"
      if [[ -z "${tgt[$impl]:-}" ]] || awk "BEGIN{exit !($2 < ${tgt[$impl]})}"; then tgt[$impl]="$2"; fi
    done
  done
done

echo
printf '%-6s %-10s %-10s %-11s %-10s %-10s %-14s\n' impl create_s connect_s simulate_s outside_s total_s total_vs_py
for impl in py raw hpp; do
  rel=$(awk "BEGIN{printf \"%.2fx\", ${w_min[$impl]}/${w_min[py]}}")
  printf '%-6s %-10s %-10s %-11s %-10s %-10s %-14s\n' \
    "${impl}" "${c_min[$impl]}" "${n_min[$impl]}" "${s_min[$impl]}" "${o_min[$impl]}" "${w_min[$impl]}" "${rel}"
done
echo "(minimum of ${REPEATS} interleaved timed runs in alternating order, one warm-up each discarded, single threaded, mask ${MASK})"
echo "work dir ${WORK}"
