# -*- coding: utf-8 -*-
"""
Headless reference run of the Brunel network.

This is pynest/examples/brunel_alpha_nest.py from NEST 3.10.0 with the
plotting and the wall-clock reporting removed, so that its spike counts can be
compared against ../reference/brunel_alpha_raw.cpp. Nothing that touches the
network or the random number streams has been changed.

Run it against the same NEST build the C++ drafts are linked to:

    PYTHONPATH=<prefix>/lib/pythonX.Y/site-packages python brunel_alpha_ref.py
"""

import math

import nest
import numpy as np

try:
    import scipy.special as sp

    def LambertWm1(x):
        return sp.lambertw(x, k=-1 if x < 0 else 0).real

except ModuleNotFoundError:
    # The NEST build under test may ship with a Python that has no SciPy.
    # Fall back to the same Halley iteration the C++ drafts use. For the one
    # argument this script evaluates, x = -exp(-1/40)/40, it agrees with
    # scipy.special.lambertw(x, k=-1) to all 16 printed digits
    # (-5.400341679701187).
    def LambertWm1(x):
        w = math.log(-x) - math.log(-math.log(-x))
        for _ in range(100):
            e = math.exp(w)
            f = w * e - x
            denom = e * (w + 1.0) - (w + 2.0) * f / (2.0 * w + 2.0)
            dw = f / denom
            w -= dw
            if abs(dw) < 1e-15 * (1.0 + abs(w)):
                break
        return w


def ComputePSPnorm(tauMem, CMem, tauSyn):
    a = tauMem / tauSyn
    b = 1.0 / tauSyn - 1.0 / tauMem
    t_max = 1.0 / b * (-LambertWm1(-np.exp(-1.0 / a) / a) - 1.0 / a)
    return (
        np.exp(1.0)
        / (tauSyn * CMem * b)
        * ((np.exp(-t_max / tauMem) - np.exp(-t_max / tauSyn)) / b - t_max * np.exp(-t_max / tauSyn))
    )


nest.ResetKernel()

dt = 0.1
simtime = 1000.0
delay = 1.5

g = 5.0
eta = 2.0
epsilon = 0.1

order = 2500
NE = 4 * order
NI = 1 * order
N_neurons = NE + NI
N_rec = 50

CE = int(epsilon * NE)
CI = int(epsilon * NI)

tauSyn = 0.5
tauMem = 20.0
CMem = 250.0
theta = 20.0
neuron_params = {
    "C_m": CMem,
    "tau_m": tauMem,
    "tau_syn_ex": tauSyn,
    "tau_syn_in": tauSyn,
    "t_ref": 2.0,
    "E_L": 0.0,
    "V_reset": 0.0,
    "V_m": 0.0,
    "V_th": theta,
}
J = 0.1
J_unit = ComputePSPnorm(tauMem, CMem, tauSyn)
J_ex = J / J_unit
J_in = -g * J_ex

nu_th = (theta * CMem) / (J_ex * CE * np.exp(1) * tauMem * tauSyn)
nu_ex = eta * nu_th
p_rate = 1000.0 * nu_ex * CE

print("J_unit = %.17g" % J_unit)
print("J_ex   = %.17g" % J_ex)
print("p_rate = %.17g" % p_rate)

nest.resolution = dt
nest.print_time = False
nest.overwrite_files = True

nodes_ex = nest.Create("iaf_psc_alpha", NE, params=neuron_params)
nodes_in = nest.Create("iaf_psc_alpha", NI, params=neuron_params)
noise = nest.Create("poisson_generator", params={"rate": p_rate})
espikes = nest.Create("spike_recorder")
ispikes = nest.Create("spike_recorder")

espikes.set(label="brunel-py-ex", record_to="ascii")
ispikes.set(label="brunel-py-in", record_to="ascii")

nest.CopyModel("static_synapse", "excitatory", {"weight": J_ex, "delay": delay})
nest.CopyModel("static_synapse", "inhibitory", {"weight": J_in, "delay": delay})

nest.Connect(noise, nodes_ex, syn_spec="excitatory")
nest.Connect(noise, nodes_in, syn_spec="excitatory")

nest.Connect(nodes_ex[:N_rec], espikes, syn_spec="excitatory")
nest.Connect(nodes_in[:N_rec], ispikes, syn_spec="excitatory")

conn_params_ex = {"rule": "fixed_indegree", "indegree": CE}
nest.Connect(nodes_ex, nodes_ex + nodes_in, conn_params_ex, "excitatory")

conn_params_in = {"rule": "fixed_indegree", "indegree": CI}
nest.Connect(nodes_in, nodes_ex + nodes_in, conn_params_in, "inhibitory")

nest.Simulate(simtime)

events_ex = espikes.n_events
events_in = ispikes.n_events
rate_ex = events_ex / simtime * 1000.0 / N_rec
rate_in = events_in / simtime * 1000.0 / N_rec

num_synapses = nest.GetDefaults("excitatory")["num_connections"] + nest.GetDefaults("inhibitory")["num_connections"]

print("Brunel network simulation (Python reference)")
print("Number of neurons : {0}".format(N_neurons))
print("Number of synapses: {0}".format(num_synapses))
print("Excitatory events : {0}".format(events_ex))
print("Inhibitory events : {0}".format(events_in))
print("Excitatory rate   : {0:.2f} Hz".format(rate_ex))
print("Inhibitory rate   : {0:.2f} Hz".format(rate_in))
