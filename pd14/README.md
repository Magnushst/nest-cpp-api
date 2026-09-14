# The cortical microcircuit

The model of Potjans and Diesmann (2014): four cortical layers, each with an
excitatory and an inhibitory population of leaky integrate and fire neurons,
connected by an eight by eight matrix of connection probabilities and driven by a
layer specific external Poisson input. It is the second case study in this
repository, and it is the one that decides whether the C++ interface generalises
past a two population network.

Status: the parameters and the derivation are in place and verified against the
upstream Python. The C++ network is not yet written.

## The three implementations

| File | What it is |
| --- | --- |
| `reference/` | The upstream PyNEST microcircuit, plotting removed. The baseline. See `reference/PROVENANCE.md`. |
| `microcircuit_params.hpp` | The parameters and the derived network, in C++. Arithmetic only, no kernel. |

## The arithmetic has to match before anything else can

The model derives an eight by eight matrix of synapse counts, the population
sizes and the external indegrees from its parameters, and rounds all of them to
integers. Those integers decide how many random numbers the kernel draws. One
synapse more or less in any of the 64 entries changes every spike that follows,
so the derivation has to agree with the Python bit for bit before comparing
spikes means anything.

It does. `microcircuit_params.hpp` reproduces every derived quantity exactly, at
the default scaling and at four others, checked against
`reference/run_ref.py --derived` at 17 significant digits: 64 synapse counts, 8
population sizes, 8 external indegrees, the 64 mean weights, the external weight
and the 8 compensating DC currents.

Getting there needed two things that a direct translation gets wrong, both of
which change the network at the default scaling of 0.1:

* **`np.round` rounds halves to even; `std::round` rounds them away from zero.**
  Population L5I has 1065 neurons unscaled, and 1065 × 0.1 is exactly 106.5.
  NumPy gives 106, `std::round` gives 107. `std::nearbyint` under the default
  rounding mode gives 106.
* **`np.sum` over eight contiguous elements is a balanced tree, not a left to
  right sum.** NumPy's pairwise summation keeps eight partial accumulators and
  combines them pairwise. Summing left to right instead moves the compensating
  DC current in its last two bits, for example 29.035009174000606 pA against
  29.035009174000663 pA for L2/3E.

Neither is a rounding subtlety that can be waved away: the first changes the
number of neurons in a population, and the second changes a current that every
neuron in the population receives.

## Source

`pynest/examples/Potjans_2014/` from the NEST simulator at commit
`e0a0745546eb75f7d50a7e433c32d0c2b7376513`, the last state of the example before
it was removed from the tree. See `reference/PROVENANCE.md`.

Potjans, T. C. and Diesmann, M. (2014). The cell-type specific cortical
microcircuit: relating structure and activity in a full-scale spiking network
model. *Cerebral Cortex* 24, 785-806.
