# A C++ API for NEST

A header-only C++ interface to the NEST simulation kernel, together with the
network models used to specify and verify it.

NEST 3.10 is driven from Python. Its kernel exposes a C++ API, but no C++ caller
can currently reach it, because an installed NEST ships the headers and not the
libraries. This repository fixes that and builds an interface over the API,
validated against models taken from the upstream PyNEST examples: each model is
written in C++ against the interface and run alongside the Python original, and
the two are compared spike for spike. Every performance and correctness claim
below is a measurement taken from those runs.

The parts that belong in NEST are prepared as a pull request against
`nest-simulator`: run [`upstream/assemble.sh`](upstream/README.md) to build the
branch and write the patch series. No existing NEST checkout is touched; the
script clones one of its own.

## What the kernel offers today

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
prefix, and it ships the fix: `packaging/install-kernel-library.patch` installs
the three kernel archives beside their headers and adds
`nest-config --kernel-libs`. With it applied, the microcircuit builds and links
against an install prefix alone and produces the same spikes. See
[docs/04_packaging.md](docs/04_packaging.md) for the change and the evidence,
and [docs/01_state_of_the_kernel.md](docs/01_state_of_the_kernel.md) for the
audit it came from.

## Layout

```
include/nest/nest_api.h      the interface, header only, namespace nest::api
tests/                       the interface's own test, against a running kernel
packaging/                   the change NEST needs before any of this links
upstream/                    the same work, shaped as a pull request to NEST
docs/                        the audit, the interface, packaging, how to run
<model>/                     one directory per case study, self documenting
build.sh                     builds every directory that holds a .cpp
```

| Path | What it is |
| --- | --- |
| `include/nest/nest_api.h` | The C++ interface. Header only, no build step. |
| `tests/api_test.cpp` | 70 checks over every entry point, including the failures. |
| `packaging/install-kernel-library.patch` | Makes an installed NEST linkable. Verified end to end. |
| `upstream/assemble.sh` | Builds the pull request branch and writes the patch series. |
| `docs/01_state_of_the_kernel.md` | What the kernel API offers today, and what surprises a C++ caller. |
| `docs/02_the_api.md` | What the interface does about each of those, and why. |
| `docs/03_build_and_run.md` | Building against a NEST build tree, and running the checks. |
| `docs/04_packaging.md` | The packaging change, and the evidence that it works. |
| `brunel/` | Case study 1, the Brunel (2000) balanced random network. |
| `pd14/` | Case study 2, the Potjans and Diesmann (2014) microcircuit. |

A new model is a new directory. `build.sh` picks it up with no change.

## The case studies

| Model | Directory | Where it stands |
| --- | --- | --- |
| Brunel (2000), balanced random network | [`brunel/`](brunel/README.md) | Complete. Spikes byte identical to the upstream Python example, network state checked against theory, speed measured against PyNEST. |
| Potjans and Diesmann (2014), cortical microcircuit | [`pd14/`](pd14/README.md) | Complete. Byte identical to the upstream Python on one rank, on four threads and on two MPI ranks, and reproduces the reference data the NEST team committed with the example. |

Brunel is the smallest network that is still a real network: two populations,
one Poisson drive, two recorders, random fixed indegree connectivity, about a
dozen API calls. It fixes the shape of the interface. The microcircuit tests
whether that shape generalises: eight populations, an eight by eight
connectivity matrix, per population drive and recorders, randomised initial
states, weights and delays drawn from distributions, and scaling logic. It needs
several times the API surface, including the `Parameter` system.

Each model directory documents its own network, checks and results. This page
covers the interface itself.

## Where this stands

Verified, each by a check in this repository that can be re-run:

* Both models produce spike files **byte identical** to the upstream Python,
  at one thread, at four threads, and on two MPI ranks.
* The microcircuit reproduces the **reference data the NEST team committed**
  with the PyNEST example: eight spike files, 20,505 spikes, produced by a
  different program on a different machine against an older kernel.
* Every entry point of the interface is exercised by `tests/api_test.cpp`
  against a running kernel: 70 checks, including the failures a caller can
  provoke.
* The packaging patch makes an installed NEST linkable, demonstrated by
  building the microcircuit against an install prefix alone and getting those
  same spikes.
* Every C++ file passes `clang-format --dry-run --Werror` against NEST's own
  `.clang-format`.

Not done, and not claimed:

* **Spatial networks, tripartite connections, `connect_arrays`, SONATA,
  structural plasticity and recording to memory** have no wrapper. They remain
  reachable through `nestkernel/nest.h` directly.
* **One compiler and one operating system.** GCC 16.2.1 on Linux. Nothing has
  been built with Clang or on macOS, and the packaging patch's Apple branch is
  reasoned rather than tested.
* **Two MPI ranks on one machine.** Nothing has been run on a cluster.
* **The full-scale microcircuit** has not been run here: 77,169 neurons and
  300 million synapses against a NEST built with assertions live does not
  belong on a laptop. Both implementations take the scaling factors as
  options and agree at every factor tried.
* **The decisions that are the NEST team's**, listed at the end of
  [docs/02_the_api.md](docs/02_the_api.md): where the header should live, what
  the namespace should be, and whether parts of the kernel API should be fixed
  rather than wrapped.

## The interface adds no simulation behaviour

Every call forwards to the kernel, the network is built in the same order, and
the same random numbers are drawn in the same sequence. That is checked rather
than asserted: for Brunel, the spike recorder files written by the C++ program
are byte identical to the one written by the upstream Python example, and the
derived Poisson rate agrees to all 17 printed digits. Identical spikes is a
stronger statement than "the results look similar", because it means the two
programs are the same simulation. See [brunel/README.md](brunel/README.md) for
the numbers and for what the network itself does.

## What the interface changes

What it adds is the part PyNEST adds on the Python side and nobody has added on
the C++ side. Every row below was met while writing a model against the kernel
API directly, and three of them are run time failures that the compiler does not
catch:

| Kernel API today | In `nest::api` |
| --- | --- |
| `init_nest` and `shutdown_nest` called by hand; shutdown must be reached on every path | `nest::api::Kernel` is a scoped object |
| `create` takes no parameters, so every population is created and then configured | `create(model, n, params)` |
| `set_nc_status` takes a non const vector reference, so the vector cannot be a temporary | hidden inside `NodeCollection::set` |
| `slice_nc` takes a 1 based start and an inclusive stop, undocumented; the natural call throws `BadParameter` | `slice(start, stop)` and `first(n)` with Python semantics |
| node collection status arrives as `AnyVector`, a vector of variants; asking for `std::vector<long>` throws `TypeMismatch` | `nc.get<long>(names::n_events)` |
| the connectivity rule must be named on every call | `ConnSpec` defaults to `all_to_all` |
| the synapse specification must be a dictionary inside a vector | `SynSpec` is constructible from a model name |
| `operator+` on node collections lives in `node_collection.h`, not `nest.h` | re-exported |
| the parameter operations live in `parameter.h`, not `nest.h`: `create_parameter` is in the API header, `redraw_parameter` and the arithmetic are not | `random::normal`, `math::redraw` and operators on `Parameter` |

Written against the interface, the Brunel example is 161 lines against 271 for
the same network written against the kernel API directly. The line count is the
lesser half of it. Three of the nine rows are mistakes that compile cleanly and
fail at run time, which is the category that costs a caller an afternoon. Two
others are the same problem wearing different clothes: `nest.h` is presented as
the API header and is not complete, so `operator+` and half the `Parameter`
system have to be fetched from other kernel headers.

## NEST's own conventions are used throughout

Both the interface and the models written against it follow the conventions the
kernel already has, so that a comparison is between two interfaces rather than
between good and bad style:

* **Dictionary keys come from `nest::names`.** Every key the kernel understands
  is a named constant in `nestkernel/nest_names.h`, so `names::tau_syn_ex` is
  checked by the compiler where `"tau_syn_ex"` is checked by nobody.
* **Constants come from `numerics`.** `numerics::e` is the value NEST's own
  models use. Substituting it for `std::exp(1.0)` left the derived rates
  unchanged in the 17th digit and the spike files byte identical, establishing
  the substitution as exact rather than a silent perturbation.
* **Failures are NEST exceptions.** The `nest::api` header throws
  `nest::BadParameter`, `nest::TypeMismatch`, `nest::DimensionMismatch` and
  `nest::KeyError`, so a caller catches one hierarchy.
* **Timing comes from NEST's own stopwatches.** The programs read
  `time_construction_create`, `time_construction_connect` and `time_simulate`
  out of the kernel status rather than wrapping calls in a clock of their own.
  The upstream Python examples use `time.time()`.

The one thing not taken from NEST is the Lambert W function that Brunel needs to
calibrate its synaptic weights, because NEST does not have one and does not
require GSL. It is solved by Halley iteration in `brunel/`, and returns bit
identical values to `scipy.special.lambertw` at the argument the model evaluates.

## What the interface costs

Nothing measurable. All the work is inside `libnestkernel.a`; a model of this
shape issues about a dozen calls and then waits, so the driver's own compiler
flags are irrelevant to runtime. The measurements below are on the Brunel case
study, on six performance cores of an Intel Core Ultra 7 155H under
`taskset -c 0,1,3,6,8,10`, reading NEST's own timers.

The one lever a driver actually holds is the thread count, and it is worth
having. One warm-up run discarded, minimum of three timed runs:

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
  they are not the same run. The default is one thread precisely so that the
  byte identity check has something to demand.
* **The NEST build dominates.** The library these numbers were measured against
  was compiled `-Wall -fopenmp -O2 -g -std=c++20`, with no `-DNDEBUG`, so
  assertions are live inside the kernel. That is a property of the NEST build
  and not something this project can change or should work around.

### Against PyNEST

`brunel/parity.sh` runs the same network, single threaded, through the upstream
Python, through C++ against the kernel API, and through C++ against this
interface, and reports NEST's own phase timers, the wall clock of the whole
process, and the difference between them. Four rounds of all three, order
reversed on alternate rounds so that each runs first as often as last, one
warm-up each discarded, minimum of the timed runs, same machine and mask as
above:

| | Create (s) | Connect (s) | Simulate (s) | Outside the kernel (s) | Whole process (s) |
| --- | --- | --- | --- | --- | --- |
| Python | 0.006 | 1.330 | 12.617 | 2.656 | 16.773 |
| C++, kernel API | 0.007 | 1.273 | 12.184 | 2.467 | 15.953 |
| C++, `nest::api` | 0.007 | 1.250 | 12.124 | 2.469 | 15.980 |

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
build/api_test                 # the interface itself
brunel/check.sh                # case study 1
pd14/check.sh                  # case study 2
RANKS=2 THREADS=1 pd14/check.sh   # the same, under MPI
brunel/scaling.sh              # what thread count buys
brunel/parity.sh               # C++ against PyNEST
upstream/assemble.sh           # build the pull request to NEST
```

`NEST_BUILD` must be a configured and built NEST source tree, not an install
prefix, for the reason at the top of this page.

## Version

NEST 3.10.0 at commit `acca9704d`. Built with GCC 16.2.1 on Linux.
