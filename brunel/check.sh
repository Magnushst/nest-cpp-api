#!/usr/bin/env bash
# Regression check: the three Brunel implementations must agree exactly.
#
# Builds both C++ programs, runs them and the Python reference in a scratch
# directory, and compares the spike recorder files byte for byte. Any difference
# is a failure: all three build the same network in the same order and therefore
# consume the same random numbers.
#
# Usage:  brunel/check.sh [scratch-dir]
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
NEST_BUILD="${NEST_BUILD:-/home/magnus/Projects/nest/nest_master/build}"
PYNEST="${PYNEST:-${NEST_BUILD}/install/lib/python3.14/site-packages}"
PYTHON="${PYTHON:-/usr/bin/python3.14}"
MASK="${MASK:-0,1,3,6,8,10}"
WORK="${1:-$(mktemp -d)}"

# The expected value of p_rate, which fixes the Poisson stream and therefore
# every spike. Computed from the upstream parameters with scipy.special.lambertw.
EXPECT_P_RATE="17789.007714721884"

mkdir -p "${WORK}"
rm -f "${WORK}"/*.dat

echo "== building"
NEST_BUILD="${NEST_BUILD}" "${HERE}/build.sh" > "${WORK}/build.log" 2>&1 || { cat "${WORK}/build.log"; exit 1; }

run() {
  local name="$1"; shift
  echo "== running ${name}"
  ( cd "${WORK}" && taskset -c "${MASK}" "$@" ) > "${WORK}/${name}.log" 2>&1
  tr '\r' '\n' < "${WORK}/${name}.log" | grep -v "Model time" > "${WORK}/${name}.out"
}

run raw "${HERE}/build/brunel_alpha_raw"
run hpp "${HERE}/build/brunel_alpha"
PYTHONPATH="${PYNEST}" run py "${PYTHON}" "${HERE}/brunel/brunel_alpha_ref.py"

status=0

echo "== p_rate"
for name in raw hpp py; do
  got=$(grep -m1 "^p_rate" "${WORK}/${name}.out" | awk '{print $3}')
  if [[ "${got}" == "${EXPECT_P_RATE}" ]]; then
    echo "   ${name}: ${got}  ok"
  else
    echo "   ${name}: ${got}  EXPECTED ${EXPECT_P_RATE}"; status=1
  fi
done

echo "== spike trains"
for pop in ex in; do
  ref=$(ls "${WORK}"/brunel-py-"${pop}"-*.dat)
  for tag in cpp hpp; do
    got=$(ls "${WORK}"/brunel-"${tag}"-"${pop}"-*.dat)
    if diff -q "${got}" "${ref}" > /dev/null; then
      echo "   ${tag} ${pop}: identical to Python ($(grep -vc '^#' "${got}") lines)"
    else
      echo "   ${tag} ${pop}: DIFFERS from Python"; status=1
    fi
  done
done

echo "== reported counts"
for name in raw hpp py; do
  echo "   ${name}: $(grep -h "events\|synapses" "${WORK}/${name}.out" | tr '\n' ' ')"
done
counts=$(for name in raw hpp py; do grep -h "Number of synapses\|events" "${WORK}/${name}.out" | awk -F': *' '{print $2}' | tr '\n' ' '; echo; done | sort -u | wc -l)
if [[ "${counts}" -eq 1 ]]; then echo "   all three agree"; else echo "   COUNTS DISAGREE"; status=1; fi

echo "== network state"
# Identity to the Python original says the network was built the same way. It
# says nothing about whether the network behaves as the theory says. state_check
# asks that separately, from the spikes alone, and checks its own estimators
# against Poisson input first.
"${PYTHON}" "${HERE}/brunel/state_check.py" --selftest | sed 's/^/   /' || status=1
"${PYTHON}" "${HERE}/brunel/state_check.py" \
  "${WORK}"/brunel-hpp-ex-*.dat "${WORK}"/brunel-hpp-in-*.dat | sed 's/^/   /' || status=1

echo
if [[ "${status}" -eq 0 ]]; then echo "PASS  (work dir ${WORK})"; else echo "FAIL  (work dir ${WORK})"; fi
exit "${status}"
