# -*- coding: utf-8 -*-
"""
Independent scientific check on the network state, from the spike files alone.

Matching the Python reference byte for byte (validation/check.sh) proves the C++
programs build the same network. It says nothing about whether that network
behaves as the theory says it should. This script asks that separately, and it
answers it two ways that do not depend on each other:

1.  **Measured, from the spikes.** Firing rate, the coefficient of variation of
    the interspike intervals (how irregular each neuron is; a Poisson process
    gives 1), the mean correlation between pairs of neurons' binned spike counts
    (how synchronous the population is; independent neurons give 0), and the
    Fano factor of the population count per bin.

2.  **Predicted, from the parameters.** The mean input each neuron receives,
    relative to threshold, follows from the connectivity and the drive alone:

        mu / theta = eta - (g * gamma - 1) * nu / nu_thr

    with gamma = C_I / C_E. If that is above 1 the neuron is driven over
    threshold by the mean input and fires almost periodically, at the rate a
    noiseless leaky integrate and fire neuron would:

        nu = 1 / (t_ref + tau_m * ln(mu / (mu - theta)))

    Solving those two together gives a firing rate with no free parameters, to
    compare against the measured one.

**What this finds for the upstream parameters.** The NEST example uses g = 5 and
eta = 2, which puts mu at about 1.22 times threshold. The network is therefore
mean driven: asynchronous, but *regular*, not the asynchronous irregular state
usually quoted for Brunel networks. The measured coefficient of variation near
0.19 and the close agreement between the measured and predicted rate both say
so. This is a property of the upstream example's parameters, not of the C++
translation: the Python original produces the identical spikes.

Usage:
    python state_check.py <spike-file> [<spike-file> ...]
    python state_check.py --selftest        # check the estimators on Poisson input
"""

import math
import sys

import numpy as np

BIN_MS = 2.0
MAX_PAIRS = 400
RNG_SEED = 0

# The upstream example's parameters. Keep in step with brunel_alpha_ref.py.
THETA = 20.0  # mV, firing threshold above rest
TAU_M = 20.0  # ms, membrane time constant
T_REF = 2.0  # ms, refractory period
ETA = 2.0  # external rate relative to threshold rate
G = 5.0  # ratio of inhibitory to excitatory weight
C_E = 1000.0  # excitatory indegree
C_I = 250.0  # inhibitory indegree
NU_THR_HZ = 8.894503857360942  # Hz, external rate that alone reaches threshold

# Tolerances. Loose enough to test the state rather than the seed.
RATE_TOLERANCE = 0.15
ASYNC_LIMIT = 0.05
FANO_BAND = (0.3, 3.0)
REGULAR_CV_MAX = 0.5
IRREGULAR_CV_BAND = (0.5, 1.5)


def mean_input_over_threshold(rate_hz):
    """mu / theta for a population firing at rate_hz."""
    gamma = C_I / C_E
    return ETA - (G * gamma - 1.0) * (rate_hz / NU_THR_HZ)


def predicted_rate_hz():
    """
    Self-consistent firing rate in the mean driven regime, or None if the mean
    input stays below threshold and the network is fluctuation driven instead.
    """

    def rate_from(nu):
        ratio = mean_input_over_threshold(nu)
        if ratio <= 1.0:
            return None
        return 1000.0 / (T_REF + TAU_M * math.log(ratio / (ratio - 1.0)))

    if rate_from(1e-6) is None:
        return None  # sub-threshold even at vanishing recurrent activity

    lo, hi = 1e-6, 1000.0
    for _ in range(200):
        mid = 0.5 * (lo + hi)
        value = rate_from(mid)
        if value is None or value < mid:
            hi = mid
        else:
            lo = mid
    return 0.5 * (lo + hi)


def read_spikes(path):
    """Return (senders, times_ms) from a NEST ascii spike file."""
    senders, times = [], []
    with open(path) as handle:
        for line in handle:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            fields = line.split()
            try:
                sender, time_ms = int(fields[0]), float(fields[1])
            except (ValueError, IndexError):
                continue  # the column header line
            senders.append(sender)
            times.append(time_ms)
    return np.array(senders, dtype=np.int64), np.array(times, dtype=float)


def cv_isi(senders, times):
    """Mean coefficient of variation of the interspike intervals."""
    values = []
    for neuron in np.unique(senders):
        spike_times = np.sort(times[senders == neuron])
        if spike_times.size < 3:
            continue
        intervals = np.diff(spike_times)
        mean = intervals.mean()
        if mean > 0:
            values.append(intervals.std(ddof=1) / mean)
    return (float(np.mean(values)) if values else float("nan")), len(values)


def binned_counts(senders, times, duration_ms):
    """Spike counts per neuron per bin, shape (n_neurons, n_bins)."""
    edges = np.arange(0.0, duration_ms + BIN_MS, BIN_MS)
    neurons = np.unique(senders)
    counts = np.empty((neurons.size, edges.size - 1), dtype=float)
    for row, neuron in enumerate(neurons):
        counts[row], _ = np.histogram(times[senders == neuron], bins=edges)
    return counts


def mean_pairwise_correlation(counts):
    """Mean Pearson correlation over a random sample of neuron pairs."""
    active = counts[counts.std(axis=1) > 0]
    if active.shape[0] < 2:
        return float("nan"), 0
    rng = np.random.default_rng(RNG_SEED)
    n = active.shape[0]
    pairs = [(i, j) for i in range(n) for j in range(i + 1, n)]
    if len(pairs) > MAX_PAIRS:
        pairs = [pairs[k] for k in rng.choice(len(pairs), size=MAX_PAIRS, replace=False)]
    values = [np.corrcoef(active[i], active[j])[0, 1] for i, j in pairs]
    return float(np.mean(values)), len(values)


def fano_factor(counts):
    """Variance over mean of the population spike count per bin."""
    population = counts.sum(axis=0)
    mean = population.mean()
    return float(population.var(ddof=1) / mean) if mean > 0 else float("nan")


def measure(path):
    senders, times = read_spikes(path)
    if senders.size == 0:
        return None
    duration_ms = float(np.ceil(times.max() / BIN_MS) * BIN_MS)
    n_neurons = int(np.unique(senders).size)
    counts = binned_counts(senders, times, duration_ms)
    cv, n_cv = cv_isi(senders, times)
    corr, n_pairs = mean_pairwise_correlation(counts)
    return {
        "neurons": n_neurons,
        "spikes": int(senders.size),
        "rate": senders.size / (duration_ms / 1000.0) / n_neurons,
        "cv": cv,
        "n_cv": n_cv,
        "corr": corr,
        "n_pairs": n_pairs,
        "fano": fano_factor(counts),
    }


def report(path):
    m = measure(path)
    if m is None:
        print("{0}: no spikes".format(path))
        return False

    predicted = predicted_rate_hz()
    mu_ratio = mean_input_over_threshold(m["rate"])

    print(path)
    print("  neurons recorded      {0}".format(m["neurons"]))
    print("  spikes                {0}".format(m["spikes"]))
    print("  mean rate             {0:.2f} Hz".format(m["rate"]))
    print("  mean CV of ISI        {0:.3f}   (over {1} neurons, Poisson = 1)".format(m["cv"], m["n_cv"]))
    print("  mean pair correlation {0:.4f}  (over {1} pairs, independent = 0)".format(m["corr"], m["n_pairs"]))
    print("  population Fano       {0:.3f}   (Poisson = 1)".format(m["fano"]))
    print("  mean input mu/theta   {0:.3f}   (above 1 means mean driven)".format(mu_ratio))

    ok = True

    if predicted is None:
        print("  predicted regime      fluctuation driven, no closed form rate")
        if not IRREGULAR_CV_BAND[0] <= m["cv"] <= IRREGULAR_CV_BAND[1]:
            print("  FAIL: sub-threshold mean input should give irregular firing, CV near 1")
            ok = False
    else:
        error = abs(m["rate"] - predicted) / predicted
        print("  predicted rate        {0:.2f} Hz  (noiseless LIF, no free parameters)".format(predicted))
        print("  rate error            {0:.1%}".format(error))
        if error > RATE_TOLERANCE:
            print("  FAIL: measured rate more than {0:.0%} from the prediction".format(RATE_TOLERANCE))
            ok = False
        if m["cv"] > REGULAR_CV_MAX:
            print("  FAIL: mean input is above threshold, so firing should be regular, CV well below 1")
            ok = False

    if abs(m["corr"]) > ASYNC_LIMIT:
        print("  FAIL: pair correlation above {0}; the population is not asynchronous".format(ASYNC_LIMIT))
        ok = False
    if not FANO_BAND[0] <= m["fano"] <= FANO_BAND[1]:
        print("  FAIL: population Fano factor outside {0}".format(FANO_BAND))
        ok = False

    if ok:
        regime = "regular" if predicted is not None else "irregular"
        print("  asynchronous {0}, as the parameters predict".format(regime))
    return ok


def selftest():
    """
    Check the estimators against input whose answer is known: independent
    Poisson trains at the same rate and count as the simulation produces should
    give a CV near 1, no pair correlation, and a Fano factor near 1.
    """
    rng = np.random.default_rng(1)
    rate_hz, n_neurons, duration_ms = 28.5, 50, 1000.0
    senders, times = [], []
    for neuron in range(1, n_neurons + 1):
        t = 0.0
        while True:
            t += rng.exponential(1000.0 / rate_hz)
            if t > duration_ms:
                break
            senders.append(neuron)
            times.append(t)
    senders = np.array(senders, dtype=np.int64)
    times = np.array(times, dtype=float)

    counts = binned_counts(senders, times, duration_ms)
    cv, _ = cv_isi(senders, times)
    corr, _ = mean_pairwise_correlation(counts)
    fano = fano_factor(counts)

    print("selftest on {0} independent Poisson trains at {1} Hz".format(n_neurons, rate_hz))
    print("  mean CV of ISI        {0:.3f}   (expected near 1)".format(cv))
    print("  mean pair correlation {0:.4f}  (expected near 0)".format(corr))
    print("  population Fano       {0:.3f}   (expected near 1)".format(fano))

    ok = 0.85 <= cv <= 1.15 and abs(corr) <= 0.02 and 0.8 <= fano <= 1.2
    print("  estimators {0}".format("behave as expected" if ok else "FAIL"))
    return ok


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(2)
    if sys.argv[1] == "--selftest":
        sys.exit(0 if selftest() else 1)
    sys.exit(0 if all([report(p) for p in sys.argv[1:]]) else 1)
