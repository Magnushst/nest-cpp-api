# Making an installed NEST linkable

This is the one change that has to happen inside `nest-simulator` before any of
this interface is usable outside a build tree. It is delivered here as a patch,
`packaging/install-kernel-library.patch`, rather than as a fork.

## The problem

An installed NEST contains 295 C++ headers under `include/nest` and no library
to link them against:

* `nestkernel/CMakeLists.txt:145` builds the kernel as a static library.
* `nestkernel/CMakeLists.txt:172` installs headers only. `models` and
  `libnestutil` do the same.
* `nest-config --libs` prints `-L<prefix>/lib/nest`, a directory that is never
  created, followed by the external libraries an extension module needs. It
  names no NEST library at all, because there is none to name.

So `nestkernel/nest.h` declares a complete C++ API that a C++ program can
include and cannot link. The only consumer is the Cython module
`pynest/nestkernel_api.pyx`, which is built inside the tree and therefore links
against the build directory.

## What the patch changes

Five files, 35 added lines, no behaviour changed for any existing caller.

| File | Change |
| --- | --- |
| `nestkernel/CMakeLists.txt` | install the `nestkernel` archive next to its headers |
| `models/CMakeLists.txt` | install the `models` archive |
| `libnestutil/CMakeLists.txt` | install the `nestutil` archive |
| `CMakeLists.txt` | set `KERNEL_LINK_PREFIX` and `KERNEL_LINK_SUFFIX`, which are `-Wl,--start-group` and `-Wl,--end-group` everywhere except Apple, whose linker resolves archives without help and rejects that syntax |
| `bin/nest-config.in` | add `--kernel-libs`, printing the three archives in that group followed by the external libraries |

`--libs` is left exactly as it is. An extension module is loaded into a process
that already contains the kernel, so linking the kernel into the module as well
would be wrong; `--kernel-libs` is a separate option for the separate case of a
standalone program that embeds the kernel.

With the patch applied, building a C++ program against an installed NEST is:

```sh
g++ -std=c++20 -fopenmp $(nest-config --includes) -c model.cpp -o model.o
g++ -fopenmp -o model model.o $(nest-config --kernel-libs)
```

## What has been verified

* The patch applies cleanly to NEST 3.10.0 at `acca9704d` (`git apply --check`).
* A NEST built from a patched copy of that tree installs the three archives
  into `<prefix>/lib/nest`, and `nest-config --kernel-libs` prints them in a
  linker group followed by the external libraries.
* `pd14/microcircuit.cpp`, compiled and linked against that install prefix
  alone with no reference to any build directory, reproduces the eight spike
  files the NEST team committed with the microcircuit example: 20,505 spikes,
  identical. That is the whole claim of this patch demonstrated end to end, from
  an installed NEST to a correct simulation.

The same was then done from the assembled pull request branch rather than from
the patch alone: it builds, installs `nest_api.h` beside the kernel headers and
the three archives beside them, its `run_nest_api_test` passes from the
installed binary, and `examples/cpp` builds through its own CMakeLists against
the install prefix and reproduces those reference spikes. See
[../upstream/README.md](../upstream/README.md).

Both copies were made outside the NEST checkout, which this project does not
modify.

One thing the patch does not solve, because it is not NEST's to solve: the link
line names the external libraries at the paths CMake found them, so if NEST was
configured against a GSL outside the system library path, the resulting binary
needs that path at run time as well. Here GSL came from a conda prefix and the
binary needed `LD_LIBRARY_PATH` set to it.

## The alternative the NEST team may prefer

A shared kernel library instead of installed archives. That is a larger change:
symbol visibility has to be decided, the Cython module and any extension modules
have to agree on who owns the kernel, and the install becomes relocatable in a
different way. The patch here is the smallest change that makes the existing
static build usable, and it is deliberately not an argument against the larger
one.
