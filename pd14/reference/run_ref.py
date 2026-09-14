# -*- coding: utf-8 -*-
"""Headless runner for the vendored PyNEST microcircuit.

This is the baseline the C++ implementations are checked against. It is the
upstream ``run_microcircuit.py`` with the plotting removed, with the scaling,
thread count and seed exposed as options so that the C++ programs can be told to
reproduce the same run, and with NEST's own stopwatches read at the end instead
of ``time.time()`` around the calls.

``--derived`` prints the quantities the model derives from its parameters before
any node is created: the scaled neuron and synapse counts, the external
indegrees, the weight matrix and the compensating DC current. Every one of them
has to be reproduced bit for bit by a C++ implementation before there is any
point comparing spikes, so they are printed to 17 significant digits.
"""

import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import nest  # noqa: E402
import network  # noqa: E402
from network_params import net_dict  # noqa: E402
from sim_params import sim_dict  # noqa: E402
from stimulus_params import stim_dict  # noqa: E402

parser = argparse.ArgumentParser()
parser.add_argument("--threads", type=int, default=sim_dict["local_num_threads"])
parser.add_argument("--seed", type=int, default=sim_dict["rng_seed"])
parser.add_argument("--n-scaling", type=float, default=net_dict["N_scaling"])
parser.add_argument("--k-scaling", type=float, default=net_dict["K_scaling"])
parser.add_argument("--presim", type=float, default=sim_dict["t_presim"])
parser.add_argument("--sim", type=float, default=sim_dict["t_sim"])
parser.add_argument("--data-path", default=os.getcwd())
parser.add_argument("--quiet", action="store_true")
parser.add_argument("--derived", action="store_true", help="print derived quantities and exit")
args = parser.parse_args()

sim_dict["local_num_threads"] = args.threads
sim_dict["rng_seed"] = args.seed
sim_dict["t_presim"] = args.presim
sim_dict["t_sim"] = args.sim
sim_dict["data_path"] = args.data_path
sim_dict["print_time"] = not args.quiet
net_dict["N_scaling"] = args.n_scaling
net_dict["K_scaling"] = args.k_scaling

net = network.Network(sim_dict, net_dict, stim_dict)


def row(values, fmt):
    return " ".join(fmt.format(v) for v in values)


if args.derived:
    print("num_neurons:   " + row(net.num_neurons, "{0:d}"))
    print("ext_indegrees: " + row(net.ext_indegrees, "{0:d}"))
    for i, r in enumerate(net.num_synapses):
        print("num_synapses[{0}]: ".format(i) + row(r, "{0:d}"))
    print("weight_ext:    {0:.17g}".format(net.weight_ext))
    for i, r in enumerate(net.weight_matrix_mean):
        print("weight[{0}]: ".format(i) + row(r, "{0:.17g}"))
    print("DC_amp:        " + row(net.DC_amp, "{0:.17g}"))
    sys.exit(0)

net.create()
net.connect()
net.simulate(args.presim)
net.simulate(args.sim)

kernel = nest.GetKernelStatus()
events = [int(sr.n_events) for sr in net.spike_recorders]
t_sim_total = args.presim + args.sim

print("Microcircuit simulation (Python reference)")
print("Number of neurons : {0}".format(kernel["network_size"] - len(events) - net.num_pops))
print("Number of synapses: {0}".format(kernel["num_connections"]))
for i, name in enumerate(net_dict["populations"]):
    print(
        "{0:<5} : {1:6d} neurons, {2:9d} events, {3:7.3f} Hz".format(
            name, int(net.num_neurons[i]), events[i], events[i] / net.num_neurons[i] / t_sim_total * 1000.0
        )
    )
print(
    "Building time     : {0:.3f} s (create {1:.3f}, connect {2:.3f})".format(
        kernel["time_construction_create"] + kernel["time_construction_connect"],
        kernel["time_construction_create"],
        kernel["time_construction_connect"],
    )
)
print("Simulation time   : {0:.3f} s".format(kernel["time_simulate"]))
