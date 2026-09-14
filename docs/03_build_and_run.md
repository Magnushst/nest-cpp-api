# Building and running

## What you need

A **built NEST source tree**, not an install prefix. An installed NEST ships C++
headers but no library to link them against, for the reason set out in
[01_state_of_the_kernel.md](01_state_of_the_kernel.md). The build tree must
contain all three of:

```
<build>/nestkernel/libnestkernel.a
<build>/models/libmodels.a
<build>/libnestutil/libnestutil.a
```

and a `nest-config`, normally at `<build>/install/bin/nest-config`, which
supplies the include paths and the external libraries.

Everything here was developed against NEST 3.10.0 at commit `acca9704d`, built
with GCC 16.2.1 on Linux. The compiler needs C++20.

## Build

```sh
NEST_BUILD=/path/to/nest/build ./build.sh
```

The script defaults `NEST_BUILD` to `/home/magnus/Projects/nest/nest_master/build`
and derives `NEST_CONFIG` from it; either can be overridden in the environment.

Each model lives in its own directory and every `.cpp` in it becomes a program.
`./build.sh` with no arguments builds them all; `./build.sh brunel` builds one.
Adding a model means adding a directory, not editing the script. Today that
gives two programs in `build/`:

| Program | Source |
| --- | --- |
| `build/brunel_alpha_raw` | `brunel/brunel_alpha_raw.cpp`, against the kernel API as it is |
| `build/brunel_alpha` | `brunel/brunel_alpha.cpp`, against the nest_cpp interface |

The link line puts the three static libraries inside
`-Wl,--start-group ... -Wl,--end-group`, because they refer to each other. No
`--whole-archive` is needed: NEST registers its models by explicit calls from
`register_models()`, not by static initialisers.

The binaries are large, a few hundred megabytes, because the NEST libraries
carry debug information. That is a property of how NEST was built.

## Run

Both programs take the same options, and all of them default to the behaviour of
the upstream Python example:

| Option | Default | Effect |
| --- | --- | --- |
| `--threads=N` | 1 | OpenMP threads, sets `local_num_threads` |
| `--seed=N` | NEST's own, 143202461 | sets `rng_seed` |
| `--quiet` | off | suppresses NEST's per step progress output |

They write spike files into the current directory, so run them somewhere
disposable:

```sh
mkdir -p /tmp/brunel && cd /tmp/brunel
/path/to/nest-cpp-api/build/brunel_alpha --quiet
```

Expected output at the default settings:

```
Number of neurons : 12500
Number of synapses: 15637600
Excitatory events : 1427
Inhibitory events : 1431
Excitatory rate   : 28.54 Hz
Inhibitory rate   : 28.62 Hz
```

The run takes about twelve seconds at one thread and under three at six, on a
recent laptop core.

## Check that all three agree

```sh
brunel/check.sh [scratch-dir]
```

This builds both programs, runs them and the Python reference, and fails unless:

* `p_rate` is `17789.007714721884` in all three, to all 17 printed digits;
* the spike recorder files from both C++ programs are byte identical to the one
  from Python;
* the reported neuron, synapse and event counts agree.

It needs a PyNEST matching the same build. It defaults to
`<build>/install/lib/python3.14/site-packages` and `/usr/bin/python3.14`;
override with `PYNEST` and `PYTHON`. The Python reference does not need SciPy:
it falls back to the same Halley iteration the C++ uses, which agrees with
`scipy.special.lambertw` bit for bit at the one argument it evaluates.

It then runs `brunel/state_check.py`, which asks a different question: not
whether the three programs agree, but whether the network they build behaves as
the theory says. That check measures the firing rate, the interspike interval
irregularity, the correlation between neurons and the population Fano factor
from the spike files, and compares the rate against a closed form prediction
derived from the parameters alone. It validates its own estimators against
synthetic Poisson input before trusting them on the simulation. See the network
state section of the [README](../README.md) for what it finds and why it
matters.

Run all of this after every change. It is the only thing standing between a tidy
up and a silently different simulation.

## Measure thread scaling

```sh
brunel/scaling.sh [scratch-dir]
```

Sweeps `--threads` over 1, 2, 4 and 6 under `taskset -c 0,1,3,6,8,10`, printing
NEST's own build and simulate timers. Each thread count gets one discarded
warm-up run followed by three timed ones, and the summary reports the minimum,
which is the run least contaminated by other work on the machine. Override with
`THREADS`, `REPEATS` and `MASK`.

**More than one thread changes the spikes.** Thread count decides which virtual
process owns which node and therefore which random stream it draws from. The
firing rate stays near 28.5 Hz, so the runs are statistically equivalent, but
they are not the same run. This is why the default is one thread and why
`check.sh` does not vary it.

## Compare against PyNEST

```sh
brunel/parity.sh [scratch-dir]
```

Runs the same network single threaded through the Python reference, the raw C++
program and the `nest_cpp` one, three timed runs each after a discarded warm-up,
and prints two clocks side by side: NEST's own create, connect and simulate
timers, and the wall clock of the whole process. Override with `REPEATS` and
`MASK`.

The phase timers answer whether the kernel does the same work, and they do agree.
The whole-process clock answers what the driver costs on top, which is where
Python's interpreter start-up and imports show up. The measured result is in the
speed section of the [README](../README.md): parity on the kernel phases, and
about 0.15 s of a 16 s run attributable to PyNEST.

Single threaded is not an accident here. The Python reference does not take a
thread count, and changing it would change which random stream each node draws
from, so the three would no longer be running the same network.

## If something goes wrong

**`missing <build>/nestkernel/libnestkernel.a`** — `NEST_BUILD` points at an
install prefix rather than a build tree. Point it at the directory you ran
`cmake` in.

**Undefined references to `nest::...`** — the static libraries are outside the
link group, or in the wrong order. `build.sh` handles this; if you are linking
by hand, keep all three inside `-Wl,--start-group ... -Wl,--end-group`.

**`nest::BadParameter: start < stop required.`** — a `slice_nc` call using 0
based indices. The kernel wants a 1 based start and an inclusive stop. The
nest_cpp interface converts for you; the raw program has to do it by hand.

**`nest::TypeMismatch` on a status read** — node collection status arrives as
`AnyVector`, a vector of variants. See finding 2 in
[01_state_of_the_kernel.md](01_state_of_the_kernel.md).

**A warning about the simulation time not being a multiple of the minimal
delay** — expected, and harmless here. The upstream Python example produces it
too. It warns about a problem that needs two conditions to hold together, and
Brunel meets neither; see the end of
[01_state_of_the_kernel.md](01_state_of_the_kernel.md).
