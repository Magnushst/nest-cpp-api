# C++ examples

Two networks driven from C++ through `nest_api.h`, the interface declared in
`nestkernel/nest_api.h`. They are the C++ counterparts of
`pynest/examples/brunel_alpha_nest.py` and of the PyNEST microcircuit, and they
produce the same spikes as those, down to the byte.

| File | What it is |
| --- | --- |
| `brunel_alpha.cpp` | The balanced random network of Brunel (2000). |
| `microcircuit.cpp` | The cortical microcircuit of Potjans and Diesmann (2014). |
| `microcircuit_params.h` | The microcircuit's parameters and the network it derives from them. |

## Building

These build against an **installed** NEST, which is the case they exist to
demonstrate. NEST must be installed with the kernel libraries, which
`nest-config --kernel-libs` reports:

```sh
cmake -B build -S examples/cpp -DNEST_CONFIG=<prefix>/bin/nest-config
cmake --build build
```

By hand, the whole of it is:

```sh
g++ -std=c++20 -fopenmp $(nest-config --includes) -c brunel_alpha.cpp -o brunel_alpha.o
g++ -fopenmp -o brunel_alpha brunel_alpha.o $(nest-config --kernel-libs)
```

## Running

Both write spike files into the current directory, so run them somewhere
disposable.

```sh
./brunel_alpha --quiet
./microcircuit --quiet --data-path=.
```

`brunel_alpha` takes `--threads=N`, `--seed=N` and `--quiet`. `microcircuit`
takes those plus `--n-scaling=F`, `--k-scaling=F`, `--presim=T`, `--sim=T`,
`--data-path=DIR` and `--derived`, which prints the derived network and exits.
Every default reproduces the behaviour of the Python example.

Both are rank agnostic and run under `mpirun` unchanged.

## Why the spikes are identical

Both examples make the same kernel calls in the same order as the Python they
were translated from, so the kernel draws the same random numbers in the same
sequence. That is checked rather than assumed: the microcircuit reproduces the
spike files committed in `reference_data/` alongside the PyNEST version.
