#!/usr/bin/env bash
# Regression check: the C++ microcircuit must agree exactly with the upstream
# PyNEST one.
#
# Two comparisons, in the order they have to succeed:
#
#   1. The derived network. Population sizes, the 8x8 synapse count matrix, the
#      external indegrees, the weights and the compensating DC currents, printed
#      to 17 significant digits by both implementations. These are rounded to
#      integers that decide how many random numbers the kernel draws, so they
#      have to agree before comparing spikes means anything.
#   2. The output files, byte for byte. Each implementation writes into its own
#      directory, since both use the upstream file names. That covers the spike
#      trains of all eight populations on every virtual process, and the node ID
#      bounds of each population.
#
# Both run with the upstream defaults, which is 500 ms of presimulation and
# 1000 ms of simulation at one tenth of the neurons and one tenth of the
# indegrees, on four threads. Override with PRESIM and SIM for a quicker pass,
# and with RANKS to run under MPI: RANKS=2 THREADS=1 checks that both are rank
# agnostic, since which virtual process owns a node decides what it draws.
#
# Usage:  pd14/check.sh [scratch-dir]
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
NEST_BUILD="${NEST_BUILD:-/home/magnus/Projects/nest/nest_master/build}"
PYNEST="${PYNEST:-${NEST_BUILD}/install/lib/python3.14/site-packages}"
PYTHON="${PYTHON:-/usr/bin/python3.14}"
MASK="${MASK:-0,1,3,6,8,10}"
THREADS="${THREADS:-4}"
RANKS="${RANKS:-1}"
PRESIM="${PRESIM:-500}"
SIM="${SIM:-1000}"
WORK="${1:-$(mktemp -d)}"

mkdir -p "${WORK}/py" "${WORK}/cpp"
rm -f "${WORK}"/py/*.dat "${WORK}"/cpp/*.dat

echo "== building"
NEST_BUILD="${NEST_BUILD}" "${HERE}/build.sh" pd14 > "${WORK}/build.log" 2>&1 || { cat "${WORK}/build.log"; exit 1; }

status=0

echo "== derived network"
# The two print the same quantities in the same order; only the column padding
# differs, so compare with whitespace collapsed.
"${HERE}/build/microcircuit" --derived --threads="${THREADS}" \
  | sed 's/[[:space:]]\+/ /g' > "${WORK}/derived.cpp"
PYTHONPATH="${PYNEST}" "${PYTHON}" "${HERE}/pd14/reference/run_ref.py" --derived --data-path "${WORK}/py" \
  | grep -E "num_|ext_|weight|DC_" | sed 's/[[:space:]]\+/ /g' > "${WORK}/derived.py"
if diff -q "${WORK}/derived.py" "${WORK}/derived.cpp" > /dev/null; then
  echo "   identical: $(grep -c . "${WORK}/derived.py") lines, 64 synapse counts and 64 weights among them"
else
  echo "   DIFFERS"; diff "${WORK}/derived.py" "${WORK}/derived.cpp" | head -10; status=1
fi

echo "== running, ${PRESIM} ms presimulation and ${SIM} ms simulation on ${RANKS} rank(s) of ${THREADS} thread(s)"
if [[ "${RANKS}" -gt 1 ]]; then
  launch=( mpirun -np "${RANKS}" --oversubscribe )
else
  launch=( taskset -c "${MASK}" )
fi
( cd "${WORK}/py" && PYTHONPATH="${PYNEST}" "${launch[@]}" "${PYTHON}" "${HERE}/pd14/reference/run_ref.py" \
    --quiet --threads "${THREADS}" --presim "${PRESIM}" --sim "${SIM}" --data-path "${WORK}/py" ) > "${WORK}/py.log" 2>&1
( cd "${WORK}/cpp" && "${launch[@]}" "${HERE}/build/microcircuit" \
    --quiet --threads="${THREADS}" --presim="${PRESIM}" --sim="${SIM}" --data-path="${WORK}/cpp" ) > "${WORK}/cpp.log" 2>&1

echo "== output files"
if diff -r "${WORK}/py" "${WORK}/cpp" > "${WORK}/diff.log" 2>&1; then
  spikes=$(cat "${WORK}"/py/spike_recorder-*.dat | grep -vc '^#')
  echo "   identical: $(ls "${WORK}/py" | wc -l) files, ${spikes} spikes"
else
  echo "   DIFFERS"; head -10 "${WORK}/diff.log"; status=1
fi

echo "== reported counts"
for name in py cpp; do
  grep -E "^Number of (neurons|synapses)" "${WORK}/${name}.log" | tr '\n' ' ' | sed "s/^/   ${name}: /"
  echo
done
if [[ "$(grep -hE '^Number of' "${WORK}/py.log" | md5sum)" == "$(grep -hE '^Number of' "${WORK}/cpp.log" | md5sum)" ]]; then
  echo "   both agree"
else
  echo "   COUNTS DISAGREE"; status=1
fi

echo "== firing rates"
paste <(grep -E "^L[0-9]" "${WORK}/py.log") <(grep -E "^L[0-9]" "${WORK}/cpp.log" | awk -F, '{print $NF}') \
  | sed 's/^/   /'

echo
if [[ "${status}" -eq 0 ]]; then echo "PASS  (work dir ${WORK})"; else echo "FAIL  (work dir ${WORK})"; fi
exit "${status}"
