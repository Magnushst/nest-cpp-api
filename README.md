# A C++ API for NEST, drafted against the Brunel network

NEST is driven from Python. The NEST team would like it to be drivable from C++
as well. This repository is draft work towards that, built by taking one small,
well known network and writing it three ways: in the upstream Python, in C++
against the kernel as it stands today, and in C++ against a proposed interface.
All three are run and compared, so every claim below is a measurement rather
than a design opinion.

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

## What is here

| Path | What it is |
| --- | --- |
| `validation/brunel_alpha_ref.py` | The upstream `brunel_alpha_nest.py`, headless. The baseline. |
| `reference/brunel_alpha_raw.cpp` | The same network in C++ against `nestkernel/nest.h` as it is today. |
| `draft/include/nest_cpp/nest.hpp` | The proposed C++ interface, header only. |
| `draft/examples/brunel_alpha.cpp` | The same network again, against that interface. |
| `validation/check.sh` | Builds and runs all three and fails on any difference. |
| `validation/scaling.sh` | Sweeps thread count and reports NEST's own timers. |
| `build.sh` | Builds both C++ programs against a NEST build tree. |

## The evidence that the translation is right

`validation/check.sh` passes. It checks three things:

1. `p_rate`, the Poisson drive rate that fixes every spike, is
   `17789.007714721884` in all three, to all 17 printed digits.
2. The spike recorder files written by the two C++ programs are **byte
   identical** to the one written by the Python original: 1428 and 1432 lines.
3. The reported totals agree: 12,500 neurons, 15,637,600 synapses, 1427
   excitatory and 1431 inhibitory recorded events.

Identical spikes is a stronger statement than "the results look similar". The
three programs build the same network in the same order, so they draw the same
random numbers in the same sequence, so the simulation is the same simulation.

## What the drafted interface changes

The proposed interface adds no simulation behaviour. Every call forwards to the
kernel. What it adds is the part PyNEST adds on the Python side and nobody has
added on the C++ side. Each item below exists because writing Brunel without it
hurt, and three of them are run time failures that the compiler does not catch:

| Kernel API today | In the draft |
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
* **Failures are NEST exceptions.** The drafted header throws
  `nest::BadParameter`, `nest::TypeMismatch`, `nest::DimensionMismatch` and
  `nest::KeyError`, so a caller catches one hierarchy.
* **Timing comes from NEST's own stopwatches.** The programs read
  `time_construction_create`, `time_construction_connect` and `time_simulate`
  out of the kernel status rather than wrapping calls in a clock of their own.
  The upstream Python example uses `time.time()`.

The one thing not taken from NEST is the Lambert W function used to calibrate
the synaptic weights, because NEST does not have one and does not require GSL.
It is solved here by Halley iteration, and agrees with SciPy to all 16
significant digits at the single argument the model evaluates.

## Speed

All the work is inside `libnestkernel.a`. The driver issues about a dozen calls
and then waits, so the driver's own compiler flags are irrelevant to runtime.
The one lever the driver actually holds is the thread count, and it is worth
having: `validation/scaling.sh`, on six performance cores of an Intel Core Ultra
7 155H under `taskset -c 0,1,3,6,8,10`, three repeats, median of NEST's own
timers:

| Threads | Build (s) | Simulate (s) | Simulate range | Speedup | Excitatory rate |
| --- | --- | --- | --- | --- | --- |
| 1 | 1.62 | 14.53 | 14.03 to 16.78 | 1.00 | 28.54 Hz |
| 2 | 0.94 | 6.90 | 6.88 to 7.13 | 2.11 | 28.40 Hz |
| 4 | 0.56 | 4.09 | 3.59 to 4.15 | 3.55 | 28.86 Hz |
| 6 | 0.47 | 3.14 | 2.81 to 3.31 | 4.63 | 28.66 Hz |

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

## Running it

See [docs/03_build_and_run.md](docs/03_build_and_run.md). In short:

```sh
NEST_BUILD=/path/to/nest/build ./build.sh
validation/check.sh
validation/scaling.sh
```

`NEST_BUILD` must be a configured and built NEST source tree, not an install
prefix, for the reason at the top of this page.

## Why Brunel, and what comes next

Brunel (2000) is the smallest network that is still a real network: two
populations, one Poisson drive, two recorders, random fixed indegree
connectivity, about a dozen API calls. It is the standard NEST example and it
exercises exactly the surface a first C++ API has to get right.

The Potjans and Diesmann (2014) microcircuit is the natural second target. It
has eight populations, an eight by eight connectivity matrix, per population
drive and recorders, and scaling logic, so it needs several times this API
surface. It is the right test of whether the draft generalises, and it is not
the right place to start.

## Version

NEST 3.10.0 at commit `acca9704d`. Built with GCC 16.2.1 on Linux.
