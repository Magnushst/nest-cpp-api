# The pull request

Everything in this repository that belongs in NEST, shaped as a branch of
`nest-simulator` and written out as a patch series.

```sh
upstream/assemble.sh            # clone NEST, build the branch, write the patches
```

The script clones NEST rather than touching any checkout you already have, makes
a `cpp-api` branch, and commits five changes on it. The files that go upstream
are generated from the ones in this repository rather than kept twice: each gets
NEST's licence header, and every reference to this repository's own paths is
rewritten to the NEST path it corresponds to. A guard refuses to write the
patches if any reference is left over.

## The five commits

| # | What it does | Why it is separate |
| --- | --- | --- |
| 1 | Install the kernel libraries, add `nest-config --kernel-libs` | It is the only change that touches the build system, and it is useful on its own: it is what makes the existing C++ API reachable at all. |
| 2 | Add `nestkernel/nest_api.h` | The interface. Adds a header and one line to a source list; changes nothing that exists. |
| 3 | Add `testsuite/cpptests/nest_api_test.cpp` | The test for commit 2, as its own executable because it brings a kernel up. |
| 4 | Add `examples/cpp/` | The first C++ examples in the tree, building against an installed NEST through commit 1. |
| 5 | Add the documentation page | Prose, separable from the code. |

| Path in this repository | Path in NEST |
| --- | --- |
| `include/nest/nest_api.h` | `nestkernel/nest_api.h` |
| `tests/api_test.cpp` | `testsuite/cpptests/nest_api_test.cpp` |
| `brunel/brunel_alpha.cpp` | `examples/cpp/brunel_alpha.cpp` |
| `pd14/microcircuit.cpp` | `examples/cpp/microcircuit.cpp` |
| `pd14/microcircuit_params.hpp` | `examples/cpp/microcircuit_params.h` |
| `upstream/examples_CMakeLists.txt` | `examples/cpp/CMakeLists.txt` |
| `upstream/examples_README.md` | `examples/cpp/README.md` |
| `upstream/cpp_api_doc.rst` | `doc/htmldoc/developer_space/cpp_api.rst` |
| `packaging/install-kernel-library.patch` | the build system changes of commit 1 |

## What stays here

The Python references, the checks, the measurements and the audit. They are how
the claims in the pull request were arrived at, but a simulator does not want a
second copy of its own examples in Python, nor scripts that compare them.

## What a reviewer will ask

**Where should the header live, and what should the namespace be?** It is
proposed as `nestkernel/nest_api.h` in `nest::api`, nested inside the kernel's
namespace so that `nest::api::create` can forward to `nest::create` without
colliding. Both are one-line changes if the team prefers otherwise.

**Should the kernel API be fixed rather than wrapped?** Some of what the
interface smooths over are arguably defects: `slice_nc` takes a 1-based
inclusive range that nothing documents, and a scalar node property comes back as
a vector of variants. Changing them at source would break
`pynest/nestkernel_api.pyx`, which is today the only caller, so the cost is
known and bounded. See `docs/01_state_of_the_kernel.md`.

**Is it tested?** `run_nest_api_test` covers every entry point against a running
kernel, including the failures a caller can provoke. Beyond that, the two
examples reproduce their PyNEST counterparts spike for spike, and the
microcircuit reproduces the reference data committed with the PyNEST version.

**Is it maintained by hand?** The examples are translations, and a change to the
PyNEST versions does not propagate to them automatically. The checks in this
repository are what would catch that, and they are not part of the pull request.
That is a real cost and a reviewer should weigh it.
