# Provenance of these files

`network.py`, `network_params.py`, `sim_params.py`, `stimulus_params.py` and
`helpers.py` are the PyNEST microcircuit example from the NEST simulator, taken
from `pynest/examples/Potjans_2014/` at commit `e0a0745546eb75f7d50a7e433c32d0c2b7376513`
of `nest/nest-simulator`. That is the parent of `09c1a3ccf`, "remove Potjans from
examples", so it is the last state of the example inside the NEST tree. The
example was removed after NEST 3.9 and now lives in its own repository; the
kernel this project links against is 3.10.0 at `acca9704d`.

Two changes were made, both subtractions:

* `helpers.py` keeps only the four functions used to build the network
  (`num_synapses_from_conn_probs`, `postsynaptic_potential_to_current`,
  `dc_input_compensating_poisson` and
  `adjust_weights_and_input_to_synapse_scaling`). The plotting and evaluation
  functions and the `matplotlib` import are gone.
* `network.py` loses its `evaluate` method, which only plotted.

No parameter and no line of network construction was altered, so this remains
the upstream model rather than a translation of it.

`run_ref.py` is the only new file. It replaces the upstream
`run_microcircuit.py`: same sequence of calls, no plotting, the scaling factors
and thread count exposed as options, and NEST's own stopwatches read at the end.
