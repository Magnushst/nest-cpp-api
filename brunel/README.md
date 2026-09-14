# The Brunel network

The balanced random network of Brunel (2000): one excitatory and one inhibitory
population of leaky integrate and fire neurons, wired to each other and to
themselves at random with a fixed number of incoming connections per neuron, and
driven by an external Poisson process. It is the standard small NEST example and
the first model in this repository.

This directory is self contained. Everything specific to the network lives here;
the C++ interface it uses lives in `../include/nest_cpp/nest.hpp` and is shared
with the models that will sit beside this one.

## The three programs

| File | What it is |
| --- | --- |
| `brunel_alpha_ref.py` | The upstream `pynest/examples/brunel_alpha_nest.py`, with the plotting removed. The baseline. |
| `brunel_alpha_raw.cpp` | The same network in C++ against `nestkernel/nest.h` as it is today. The control. |
| `brunel_alpha.cpp` | The same network in C++ against `nest_cpp`. |

All three build the same network in the same order, so they draw the same random
numbers and produce the same spikes. That is checked, not assumed.

## The network

| Quantity | Value |
| --- | --- |
| excitatory neurons | 10,000 |
| inhibitory neurons | 2,500 |
| excitatory indegree `C_E` | 1,000 |
| inhibitory indegree `C_I` | 250 |
| synapses built | 15,637,600 |
| neuron model | `iaf_psc_alpha`, threshold 20 mV over rest, membrane time constant 20 ms, refractory 2 ms |
| synaptic time constant | 0.5 ms |
| synaptic delay | 1.5 ms |
| PSP amplitude `J` | 0.1 mV excitatory, −0.5 mV inhibitory (`g` = 5) |
| external drive | one `poisson_generator` at 17,789 Hz per neuron (`eta` = 2) |
| resolution | 0.1 ms |
| simulated time | 1,000 ms |
| recorded from | the first 50 neurons of each population |

The weights are given as a postsynaptic *potential* of 0.1 mV rather than as a
current. Converting one to the other needs the peak of the alpha shaped
postsynaptic potential for a unit current, which involves the lower branch of the
Lambert W function. The Python example takes that from SciPy; NEST has no Lambert
W and does not require GSL, so the C++ programs solve it by Halley iteration.
They return bit identical values to `scipy.special.lambertw` at the argument the
model evaluates.

## Running

Build from the repository root first:

```sh
NEST_BUILD=/path/to/nest/build ./build.sh brunel
```

Then, from a disposable directory because the programs write spike files:

```sh
mkdir -p /tmp/brunel && cd /tmp/brunel
/path/to/nest-cpp-api/build/brunel_alpha --quiet
```

Both C++ programs take `--threads=N` (default 1), `--seed=N` (default is NEST's
own, 143202461) and `--quiet`. All three defaults reproduce the upstream
example's behaviour exactly.

## The three checks

```sh
brunel/check.sh      # do the three programs agree?
brunel/scaling.sh    # what does thread count buy?
brunel/parity.sh     # is C++ any cheaper than PyNEST?
```

`check.sh` builds and runs all three and fails unless the Poisson drive rate
matches to all 17 printed digits, the spike recorder files are byte identical,
and the reported counts agree. It then runs `state_check.py`.

`state_check.py` asks the separate question of whether the network behaves as the
theory says. It measures the firing rate, the coefficient of variation of the
interspike intervals, the mean correlation between pairs of neurons and the
population Fano factor, and compares the rate against a prediction with no free
parameters. Run `python state_check.py --selftest` to see it check its own
estimators against synthetic Poisson input.

`parity.sh` times all three single threaded and reports NEST's own phase timers
next to the wall clock of the whole process. The phases agree, because all three
run the same `libnestkernel.a` over the same network; the time spent outside the
kernel shows PyNEST costing about 0.15 s more out of 16 s, which is interpreter
start-up and imports, paid once. The numbers are in the speed section of the [top level
README](../README.md). C++ is at parity and no faster, which is the honest
answer: this model issues about a dozen calls into the kernel, so there is no
interpreter overhead to recover.

## What it finds, and why it is worth saying

At `g` = 5 and `eta` = 2 the mean input to a neuron sits at 1.22 times threshold.
The neuron is therefore carried over threshold by the mean input alone rather
than by fluctuations, and fires nearly periodically. Measured:

| | Excitatory | Inhibitory | Poisson would give |
| --- | --- | --- | --- |
| firing rate | 28.54 Hz | 28.62 Hz | |
| CV of interspike intervals | 0.191 | 0.188 | 1 |
| mean pairwise correlation | 0.0070 | 0.0103 | 0 |
| population Fano factor | 1.285 | 1.491 | 1 |

Solving the mean input and the noiseless firing rate together,

```
mu / theta = eta - (g * gamma - 1) * nu / nu_thr        gamma = C_I / C_E
nu         = 1 / (t_ref + tau_m * ln(mu / (mu - theta)))
```

gives 27.68 Hz with nothing fitted, against 28.54 Hz measured: 3.1% error.

So this parameter set is **asynchronous regular**. Neurons are close to
independent yet fire close to periodically. That is a property of the upstream
example's parameters and not of the translation, since the Python original
produces the identical spikes. It is written down here because a reference
implementation should state the regime it is in rather than let a reader assume
the asynchronous irregular one that Brunel networks are usually associated with.
Where `g` = 5, `eta` = 2 sits on Brunel's own phase diagram is a separate
question, and this repository has not checked it.

## Source

`pynest/examples/brunel_alpha_nest.py` from NEST 3.10.0, commit `acca9704d`.
Every model parameter above is taken from that file unchanged.

Brunel, N. (2000). Dynamics of sparsely connected networks of excitatory and
inhibitory spiking neurons. *Journal of Computational Neuroscience* 8, 183-208.
