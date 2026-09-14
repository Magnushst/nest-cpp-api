# What NEST's C++ side looks like today

An audit of NEST 3.10.0 at commit `acca9704d`, done before any code was written
here, to establish what a C++ API project actually has to build. Every claim
below names the file and line it came from, and the run time behaviour was
checked by running it rather than by reading.

Line numbers refer to the NEST source tree, not to this repository.

## There is already a C++ API

`nestkernel/nest.h` declares the whole simulator as ordinary C++ free functions
in namespace `nest`:

* `create( model_name, n )`, `create_spatial`, `make_nodecollection`
* `connect( sources, targets, connectivity, synapse_params )`,
  `connect_tripartite`, `connect_arrays`, `connect_sonata`, `disconnect`
* `simulate( t )`, and `prepare` / `run` / `cleanup` for partial runs
* `set_kernel_status`, `get_kernel_status`, `set_nc_status`, `get_nc_status`,
  `set_node_status`, `get_node_status`, `get_connection_status`
* `copy_model`, `set_model_defaults`, `get_model_defaults`
* `create_parameter`, `create_mask`, `apply`
* `init_nest`, `shutdown_nest`, `reset_kernel`, `install_module`

The parameter exchange type is `Dictionary`, from `libnestutil/dictionary.h`.
Its own documentation, at `libnestutil/dictionary.h:204`, reads:

> Dictionary class for interface to Python and C++ API.

So C++ use is intended, not accidental.

## Nothing is shipped that a C++ program can link against

This is the gap.

* `nestkernel/CMakeLists.txt:145` reads
  `add_library( nestkernel STATIC ${nestkernel_sources} )`. The kernel is a
  static library.
* `nestkernel/CMakeLists.txt:172` reads `install( FILES ${install_headers} ...)`
  with destination `include/nest`. **Headers are installed. The library is
  not.**
* An installed NEST prefix therefore contains 295 headers under `include/nest`,
  including `nest.h` and `dictionary.h`, and a `lib` directory containing only
  the Python site-packages tree. The only compiled artefact installed is
  `nest/nestkernel_api.so`, the Cython extension module.
* `nest-config --libs` prints `-L<prefix>/lib/nest` followed by the external
  dependencies. That `lib/nest` directory is never created by the install, and
  no `-lnestkernel` is named. The flags cannot link a C++ program.

Checked on two separate installed prefixes built from this tree. Both behave the
same way.

## Nobody uses the C++ API except Cython

`grep` for `init_nest` across the whole tree finds four hits: the declaration in
`nestkernel/nest.h:52`, the definition in `nestkernel/nest.cpp:54`, the Cython
declaration in `pynest/nestkernel_api.pxd:144`, and the single call site in
`pynest/nestkernel_api.pyx:60`.

There is no C++ example in the repository. `examples/` contains one
subdirectory, `music`. There is no documentation page describing C++ use.

## Static linking works, and what it needs

A NEST build tree produces three static libraries: `nestkernel/libnestkernel.a`,
`models/libmodels.a` and `libnestutil/libnestutil.a`. Linking a program against
them works, with two details:

* **Model registration is explicit, not by static initialiser.**
  `models/models.h:37` declares `void register_models()`, which
  `nestkernel/model_manager.cpp:86` calls during kernel initialisation. The
  function body is generated into the build directory as `models/models.cpp` and
  contains a direct call per model. Because nothing depends on static
  initialisers running in a translation unit the linker might discard,
  `-Wl,--whole-archive` is not needed.
* **The libraries refer to each other, so they need a group.** Link them inside
  `-Wl,--start-group ... -Wl,--end-group`.

A C++ program calling `nest::init_nest` gets the full set of built in neuron,
device and synapse models with no further work. It compiled and linked on the
first attempt, with no position independent code problem and no flag mismatch.

## Seven places the API surprises a caller

These were all found by writing the Brunel network against the kernel API and
running it. Three of them compile cleanly and fail at run time, which is the
category that matters.

1. **`slice_nc` uses a 1 based start and an inclusive stop.**
   `nestkernel/nest.cpp:330` subtracts one from a non negative `start` and
   leaves `stop` alone. So Python's `nc[:50]` becomes `slice_nc(nc, 1, 50, 1)`.
   The declaration in `nest.h` documents none of this. Calling it with the
   natural 0 based `(0, 50)` compiles and throws
   `nest::BadParameter: start < stop required.`

2. **Node collection status comes back as `AnyVector`.**
   `AnyVector` is `std::vector<any_type>`, one variant per node, holding
   `std::monostate` where a node is not local. Reading one integer off a one
   node collection is `get<AnyVector>(key).at(0)` and then `std::get<long>` on
   the variant. Asking for the obvious `std::vector<long>` compiles and throws
   `nest::TypeMismatch`.

3. **`create` takes no parameter dictionary.** `nest.h` has
   `create( model_name, n )` only, so the Python idiom of creating a population
   with its parameters becomes two statements. The second one,
   `set_nc_status`, takes `std::vector<Dictionary>&` by non const reference, so
   the vector has to be a named local and cannot be written in place.

4. **The connectivity rule must be named on every call.** PyNEST defaults it to
   `all_to_all`; the kernel does not.

5. **The synapse specification is a vector of dictionaries.** Python accepts the
   bare string `"excitatory"`. At this level it must be
   `std::vector<Dictionary>` containing one dictionary with a `synapse_model`
   entry.

6. **`operator+` on node collections is not in `nest.h`.** It is declared at
   `nestkernel/node_collection.h:924`, so the translation of
   `nodes_ex + nodes_in` is the only reason a Brunel program needs a second
   kernel header.

7. **There is no scoped kernel.** `init_nest` and `shutdown_nest` are called by
   hand, and `shutdown_nest` must be reached on every path or MPI is left
   un-finalised. Any exception between them leaks the finalisation.

## Two things that turned out to be fine

Worth recording, because both were suspected and neither is true.

* **The variant converts sensibly.** A string literal assigned into a
  `Dictionary` is stored as `std::string`, not silently as `bool` through a
  pointer to bool conversion, and an `int` is stored as `long`. Checked by
  printing `std::variant::index()` for both cases. What the variant cannot do is
  tell you that a key is misspelled, which is what `nest::names` is for.

* **The minimum delay warning does not apply to Brunel.** Running for 1000 ms
  with a minimum delay of 1.5 ms triggers a warning from
  `nestkernel/simulation_manager.cpp:653`. Reading the message, it fires
  whenever the simulation time is not a multiple of the minimum delay, but it
  warns about inconsistency only when two conditions hold together: the network
  has more than one source of randomness, and `Simulate` is called repeatedly
  with times that are not multiples of the minimum delay. Brunel has one
  `poisson_generator` and calls `Simulate` once, so neither holds. The upstream
  Python example produces the same warning for the same reason.

## What this means for the project

The C++ API surface largely exists and works. Three things are missing, in
descending order of how hard they are for someone outside the NEST team to fix:

1. **Packaging.** An installed NEST cannot be linked against. This is a change
   to `nestkernel/CMakeLists.txt` and `nest-config`, and only the NEST team can
   make it. Everything in this repository links against a build tree instead.
2. **Ergonomics and safety.** The seven surprises above. This is what
   [02_the_api.md](02_the_api.md) sets out.
3. **An example and documentation.** There is currently neither. This repository
   is one of each.
