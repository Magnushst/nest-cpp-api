# A C++ API for NEST, worked out on the Brunel network

NEST is driven from Python. The NEST team would like it to be drivable from C++
as well. This repository is that interface, built by taking one small, well known
network and writing it three ways: in the upstream Python, in C++ against the
kernel as it stands today, and in C++ against the interface proposed here. All
three are run and compared, so every claim below is a measurement rather than a
design opinion.

Brunel (2000) is the first model. More models will sit beside it in the same
layout, and the interface grows to cover what they need.

Nothing here modifies NEST. The NEST checkout is read only from this project.

## The finding that shapes the work

**NEST already has a C++ API. What it does not have is a way to use it.**

`nestkernel/nest.h` declares `create`, `connect`, `simulate`, `copy_model`,
`set_kernel_status` and the rest as ordinary C++ free functions, and
`libnestutil/dictionary.h:204` says in as many words that `Dictionary` is the
"Dictionary class for interface to Python and C++ API". None of it is reachable
from an installed NEST:

* `nestkernel/CMakeLists.txt:145` builds the kernel as a **static** library.
* `nestkernel/CMakeLists.txt:172` installs **headers only**.
* An installed NEST therefore contains 295 C++ headers under `include/nest` and
  no library to link them against. Its `lib` directory holds only the Python
  site-packages tree.
* `nest-config --libs` prints `-L<prefix>/lib/nest`, a directory that is never
  created, and names no kernel library at all.

The only thing in the tree that consumes the C++ API is the Cython module
`pynest/nestkernel_api.pyx`. There is no C++ example anywhere in NEST
(`examples/` contains only MUSIC) and no documentation for C++ use.

So this project links against a NEST **build tree** rather than an install
prefix. Making an installed NEST linkable is a packaging change for the NEST
team, and it is the single most valuable thing in this repository for them.
See [docs/01_state_of_the_kernel.md](docs/01_state_of_the_kernel.md).

## Layout

```
include/nest_cpp/nest.hpp    the interface, header only, namespace nestpp
brunel/                      everything specific to the Brunel network
docs/                        the audit, the interface, how to build and run
build.sh                     builds every model directory
```

| Path | What it is |
| --- | --- |
| `include/nest_cpp/nest.hpp` | The C++ interface. Header only, no build step. |
| `brunel/brunel_alpha.cpp` | The Brunel network written against that interface. |
| `brunel/brunel_alpha_raw.cpp` | The same network against `nestkernel/nest.h` as it is today. The control. |
| `brunel/brunel_alpha_ref.py` | The upstream `brunel_alpha_nest.py`, headless. The baseline. |
| `brunel/check.sh` | Builds and runs all three and fails on any difference. |
| `brunel/state_check.py` | Asks whether the network behaves as the theory says. |
| `brunel/scaling.sh` | Sweeps thread count and reports NEST's own timers. |
| `brunel/parity.sh` | Times all three against each other, C++ versus PyNEST. |

A second model is a new directory beside `brunel/`. `build.sh` picks it up with
no change.

## The evidence that the translation is right

`brunel/check.sh` passes. It checks three things:

1. `p_rate`, the Poisson drive rate that fixes every spike, is
   `17789.007714721884` in all three, to all 17 printed digits.
2. The spike recorder files written by the two C++ programs are **byte
   identical** to the one written by the Python original: 1428 and 1432 lines.
3. The reported totals agree: 12,500 neurons, 15,637,600 synapses, 1427
   excitatory and 1431 inhibitory recorded events.

Identical spikes is a stronger statement than "the results look similar". The
three programs build the same network in the same order, so they draw the same
random numbers in the same sequence, so the simulation is the same simulation.

## The evidence that the network is right

Matching Python proves the translation. It says nothing about whether the
network does what the theory says it should, so `brunel/state_check.py` asks that
separately, from the spike files alone, and checks its own estimators against
Poisson input first.

| Measured | Excitatory | Inhibitory | Poisson would give |
| --- | --- | --- | --- |
| firing rate | 28.54 Hz | 28.62 Hz | |
| CV of interspike intervals | 0.191 | 0.188 | 1 |
| mean pairwise correlation | 0.0070 | 0.0103 | 0 |
| population Fano factor | 1.285 | 1.491 | 1 |

Against that, a prediction from the parameters with no free parameters at all.
The mean input relative to threshold follows from the connectivity and the
drive,

    mu / theta = eta - (g * gamma - 1) * nu / nu_thr,   gamma = C_I / C_E,

and if that exceeds 1 the neuron is carried over threshold by the mean input
alone and fires at the rate a noiseless leaky integrate and fire neuron would,
`nu = 1 / (t_ref + tau_m * ln(mu / (mu - theta)))`. Solving the two together
gives **27.68 Hz at mu = 1.22 theta**, against 28.54 Hz measured: **3.1% error**.

So these parameters give an **asynchronous but regular** network. Neurons are
nearly independent (correlation 0.007) yet fire nearly periodically (CV 0.19),
because at `g = 5` and `eta = 2` the mean input sits a fifth above threshold. The
close agreement between the closed form rate and the simulation is the
confirmation.

This is a property of the upstream example's parameters, not of the translation:
the Python original produces the identical spikes. It is recorded here because a
reference implementation should say what state it is in rather than leave the
reader to assume the asynchronous irregular one that Brunel networks are usually
associated with. Where this point sits on Brunel's own phase diagram in `(g,
eta)` is a separate question that this repository has not checked.

## What the interface changes

The interface adds no simulation behaviour. Every call forwards to the kernel.
What it adds is the part PyNEST adds on the Python side and nobody has added on
the C++ side. Each row below exists because writing Brunel without it hurt, and
three of them are run time failures that the compiler does not catch:

| Kernel API today | In `nest_cpp` |
| --- | --- |
| `init_nest` and `shutdown_nest` called by hand; shutdown must be reached on every path | `nestpp::Kernel` is a scoped object |
| `create` takes no parameters, so every population is created and then configured | `create(model, n, params)` |
| `set_nc_status` takes a non const vector reference, so the vector cannot be a temporary | hidden inside `NodeCollection::set` |
| `slice_nc` takes a 1 based start and an inclusive stop, undocumented; the natural call throws `BadParameter` | `slice(start, stop)` and `first(n)` with Python semantics |
| node collection status arrives as `AnyVector`, a vector of variants; asking for `std::vector<long>` throws `TypeMismatch` | `nc.get<long>(names::n_events)` |
| the connectivity rule must be named on every call | `ConnSpec` defaults to `all_to_all` |
| the synapse specification must be a dictionary inside a vector | `SynSpec` is constructible from a model name |
| `operator+` on node collections lives in `node_collection.h`, not `nest.h` | re-exported |

The example shrinks from 271 lines to 161 and, more to the point, three of the
eight rows above are mistakes that compile cleanly and fail at run time.

## NEST's own conventions are used throughout

Both C++ programs follow the conventions the kernel already has, so that the
comparison is between two interfaces rather than between good and bad style:

* **Dictionary keys come from `nest::names`.** Every key the kernel understands
  is a named constant in `nestkernel/nest_names.h`, so `names::tau_syn_ex` is
  checked by the compiler where `"tau_syn_ex"` is checked by nobody.
* **Constants come from `numerics`.** `numerics::e` is the value NEST's own
  models use. Substituting it for `std::exp(1.0)` left `p_rate` unchanged in the
  17th digit and the spike files byte identical, which is how we know it was a
  safe swap and not a silent perturbation.
* **Failures are NEST exceptions.** The `nest_cpp` header throws
  `nest::BadParameter`, `nest::TypeMismatch`, `nest::DimensionMismatch` and
  `nest::KeyError`, so a caller catches one hierarchy.
* **Timing comes from NEST's own stopwatches.** The programs read
  `time_construction_create`, `time_construction_connect` and `time_simulate`
  out of the kernel status rather than wrapping calls in a clock of their own.
  The upstream Python example uses `time.time()`.

The one thing not taken from NEST is the Lambert W function used to calibrate
the synaptic weights, because NEST does not have one and does not require GSL.
It is solved here by Halley iteration, and returns bit identical values to
`scipy.special.lambertw` at the argument the model evaluates
(`-5.4003416797011869`, residual 3e-18).

## Speed

All the work is inside `libnestkernel.a`. The driver issues about a dozen calls
and then waits, so the driver's own compiler flags are irrelevant to runtime.
The one lever the driver actually holds is the thread count, and it is worth
having: `brunel/scaling.sh`, on six performance cores of an Intel Core Ultra
7 155H under `taskset -c 0,1,3,6,8,10`. One warm-up run is discarded and the
minimum of three timed runs is reported, from NEST's own timers:

| Threads | Build (s) | Simulate (s) | Speedup | Efficiency | Excitatory rate |
| --- | --- | --- | --- | --- | --- |
| 1 | 1.22 | 11.58 | 1.00 | 100% | 28.54 Hz |
| 2 | 0.82 | 5.87 | 1.97 | 99% | 28.40 Hz |
| 4 | 0.49 | 3.47 | 3.34 | 83% | 28.86 Hz |
| 6 | 0.39 | 2.54 | 4.56 | 76% | 28.66 Hz |

Two caveats, both real:

* **More than one thread changes the spikes.** Thread count decides which
  virtual process owns which node and which random stream it draws from. The
  firing rate stays near 28.5 Hz, so the runs are statistically equivalent, but
  they are not the same run. The default is one thread precisely so that
  `check.sh` can demand byte identity.
* **The NEST build dominates.** The library these numbers were measured against
  was compiled `-Wall -fopenmp -O2 -g -std=c++20`, with no `-DNDEBUG`, so
  assertions are live inside the kernel. That is a property of the NEST build
  and not something this project can change or should work around.

### Against PyNEST

`brunel/parity.sh` runs the same network, single threaded, through all three
implementations and reports NEST's own phase timers, the wall clock of the whole
process, and the difference between them. Four rounds of all three, order
reversed on alternate rounds so that each runs first as often as last, one
warm-up each discarded, minimum of the timed runs, same machine and mask as
above:

| | Create (s) | Connect (s) | Simulate (s) | Outside the kernel (s) | Whole process (s) |
| --- | --- | --- | --- | --- | --- |
| Python | 0.006 | 1.330 | 12.617 | 2.656 | 16.773 |
| C++, kernel API | 0.007 | 1.273 | 12.184 | 2.467 | 15.953 |
| C++, `nest_cpp` | 0.007 | 1.250 | 12.124 | 2.469 | 15.980 |

**The kernel phases agree, and the simulate phase does not resolve a
difference.** The minima suggest C++ is half a second ahead, but the ranges
overlap and the slowest single simulate of the whole session, 13.33 s, was a C++
run. Nothing else was expected: all three processes execute the same
`libnestkernel.a` over the same network in the same order, so the driver cannot
influence this phase and the spread is the machine, not the language.

**Outside the kernel the difference is real but small.** Python's best round,
2.656 s, is slower than every C++ round measured, the worst of which was
2.627 s. Two independent measurements agree on the size of it. Subtracting the
phase timers, as in the table, gives about 0.19 s. Timing the bare start-up of
each instead gives 0.14 s: `python -c "import nest"` costs 1.276 s against
1.132 s for the C++ equivalent, which is

```cpp
int main( int argc, char* argv[] )
{
  nest::init_nest( &argc, &argv );
  nest::shutdown_nest( 0 );
}
```

Both are minima of five runs under the same mask. So the whole of PyNEST's
overhead on this model is roughly 0.15 s of interpreter and import, about 1% of
the run, and it is paid once at start-up rather than per call.

**C++ is therefore at parity, and parity is the ceiling.** PyNEST is a thin
driver over the same kernel, and a model of this shape issues about a dozen
calls into it, so there is no interpreter overhead to recover. The case for a
C++ API is that C++ callers can link against NEST at all, not that the
simulation runs faster. A model that called into the kernel in a loop, or one
that interleaved simulation with analysis, would have more to gain, and this
measurement says nothing about those.

A profiler was not needed here and was not used. The two processes run the same
object code through the same phases; the phase timers already separate the
kernel work from the driver, and a single scratch binary separated start-up from
everything else. `perf` would have re-measured the same kernel twice.

Absolute times shift by a few percent between sessions, so compare within a
table and not across the two above.

## Running it

See [docs/03_build_and_run.md](docs/03_build_and_run.md). In short:

```sh
NEST_BUILD=/path/to/nest/build ./build.sh
brunel/check.sh
brunel/scaling.sh
brunel/parity.sh
```

`NEST_BUILD` must be a configured and built NEST source tree, not an install
prefix, for the reason at the top of this page.

## Why Brunel first, and what comes next

Brunel (2000) is the smallest network that is still a real network: two
populations, one Poisson drive, two recorders, random fixed indegree
connectivity, about a dozen API calls. It is the standard NEST example and it
exercises exactly the surface a first C++ API has to get right.

The Potjans and Diesmann (2014) microcircuit is the natural second target. It
has eight populations, an eight by eight connectivity matrix, per population
drive and recorders, and scaling logic, so it needs several times this API
surface, including the `Parameter` system for randomised initial states. It is
the right test of whether the interface generalises, and it is not the right
place to start.

## Version

NEST 3.10.0 at commit `acca9704d`. Built with GCC 16.2.1 on Linux.
