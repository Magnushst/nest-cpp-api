# The drafted C++ interface

`draft/include/nest_cpp/nest.hpp` is a header only layer over
`nestkernel/nest.h`, in namespace `nestpp`. It adds no simulation behaviour:
every call forwards to the kernel, the network is built in the same order, and
the same random numbers are drawn. The proof of that is that
`draft/examples/brunel_alpha.cpp` writes spike files byte identical to the
upstream Python example.

What it adds is the part PyNEST adds on the Python side and nobody has added on
the C++ side.

## Scope, and why it is this small

The interface covers the Brunel network and nothing else. Every entry point
exists because `draft/examples/brunel_alpha.cpp` needs it. That is about a dozen
operations: bring the kernel up, set kernel status, create nodes with
parameters, copy a synapse model, connect with `all_to_all` and with
`fixed_indegree`, slice a population, concatenate two populations, simulate,
read a scalar out of node status and out of model defaults.

A draft that covers one network completely and says so is more useful than a
sketch of the whole API, because every line of it has been run. Things known to
be missing are listed at the end.

## The seven decisions, each answering an audit finding

The findings are numbered as in [01_state_of_the_kernel.md](01_state_of_the_kernel.md).

### 1. `Kernel` is a scoped object (finding 7)

```cpp
const nestpp::Kernel kernel;   // init_nest here
kernel.reset();
...                            // shutdown_nest when this scope ends, on any path
```

Non copyable and non movable, because there is one kernel per process. It owns
the `argv` storage that `init_nest` needs, including the null terminator OpenMPI
requires.

### 2. `create` takes parameters (finding 3)

```cpp
const auto nodes_ex = nestpp::create( "iaf_psc_alpha", NE, neuron_params );
```

This is the kernel's `create` followed by `set_nc_status`, with the non const
vector kept out of sight. The difference is that the parameters of a population
appear beside the population rather than three statements later.

### 3. `Params` holds its own storage

`Dictionary` derives from `std::shared_ptr`, so copying one aliases it. Passing
the same `Dictionary` to two calls would let them share state, and the kernel
marks entries as accessed as it reads them. `Params` therefore holds an owned
`std::map<std::string, any_type>` and materialises a fresh `Dictionary` on every
call to `dict()`. Two calls given the same `Params` cannot interfere.

It is written as an initialiser list, which reads close to the Python:

```cpp
const nestpp::Params neuron_params { { names::C_m, c_mem },
                                     { names::tau_m, tau_mem },
                                     { names::V_th, theta } };
```

### 4. Slicing uses Python semantics (finding 1)

```cpp
nodes_ex.first( N_rec )      // nodes_ex[:N_rec]
nodes_ex.slice( a, b )       // nodes_ex[a:b], 0 based, stop exclusive
```

The conversion to the kernel's 1 based inclusive convention happens once, inside
the header, where it can be commented. Negative indices are rejected with
`nest::BadParameter` rather than given a different meaning from Python's, since
guessing at that is how a silent difference in results gets introduced.

### 5. Status reads are typed (finding 2)

```cpp
const long events_ex = espikes.get< long >( names::n_events );
```

`detail::scalar` handles the three shapes the kernel actually returns: a bare
value, a one element `std::vector<T>`, and a one element `AnyVector` of
variants. A missing key raises `nest::KeyError` naming the key, a wrong type
raises `nest::TypeMismatch` naming both types, and a collection with more than
one node raises `nest::DimensionMismatch`. A caller catches one hierarchy.

### 6. `ConnSpec` defaults the rule, `SynSpec` accepts a name (findings 4 and 5)

```cpp
nestpp::connect( noise, nodes_ex, nestpp::ConnSpec::all_to_all(), "excitatory" );
nestpp::connect( nodes_ex, all_nodes, nestpp::ConnSpec::fixed_indegree( CE ), "excitatory" );
```

`ConnSpec` has named factories for the rules Brunel uses and falls back to
`all_to_all`, as PyNEST does. `SynSpec` is implicitly constructible from a model
name, so the bare string works as it does in Python. The vector wrapping the
synapse dictionary is built inside `connect`.

### 7. `operator+` and the names are re-exported (finding 6)

`nestpp::NodeCollection::operator+` forwards to the one in
`nestkernel/node_collection.h`, and `nestpp::names` is an alias for
`nest::names`, so a program needs one include.

## Conventions taken from NEST rather than invented

* **Keys are `nest::names` constants.** `names::tau_syn_ex` rather than
  `"tau_syn_ex"`. A misspelling becomes a compile error. This is NEST's own
  mechanism, declared in `nestkernel/nest_names.h` and used throughout the
  kernel; it had simply never been available to a C++ caller because no C++
  caller existed.
* **Constants are `numerics` constants.** `numerics::e` is what NEST's own
  models use. Substituting it for `std::exp(1.0)` was checked rather than
  assumed: `p_rate` stayed at `17789.007714721884` and the spike files stayed
  byte identical.
* **Exceptions are NEST exceptions.** `nest::BadParameter`,
  `nest::TypeMismatch`, `nest::DimensionMismatch`, `nest::KeyError`, all from
  `nestkernel/exceptions.h`, all deriving from `nest::KernelException`.
* **Timing is NEST's own.** The kernel status carries
  `time_construction_create`, `time_construction_connect` and `time_simulate` as
  scalars, maintained by `nest::Stopwatch`. The examples read those rather than
  wrapping calls in a clock. The per thread timers beside them, `time_update`
  and `time_deliver_spike_data` among others, come back as one value per thread
  and are only filled in a build configured with detailed timers.

## Cost

Every wrapper is a forwarding call or a small value type, and the header is
compiled into the caller. The measured build and simulate times of the two C++
programs agree to within run to run noise, which is what should happen when the
work is all inside the kernel. See the table in the README.

## What is deliberately not here

* **Spatial networks.** `create_spatial`, masks, and position based parameters.
* **The `Parameter` system.** `nest::create_parameter` builds distributions that
  can be assigned to node parameters, which is how randomised initial states and
  distance dependent weights are written. Brunel does not use it, so it is not
  drafted. It is the first thing to add, because the Potjans and Diesmann
  microcircuit needs it.
* **`prepare` / `run` / `cleanup`.** Partial simulation, needed for anything
  that inspects or changes the network mid run.
* **Connection queries.** `get_connections` and the synapse collection that
  PyNEST builds on it.
* **Recording to memory.** The example records to file, as the upstream example
  does. Reading spikes back out of a `spike_recorder` in memory means reading
  vector valued status, which `detail::scalar` deliberately refuses.
* **MPI.** `init_nest` initialises MPI and the code is rank agnostic, but
  nothing here has been run on more than one rank.

## Open questions for the NEST team

1. **Namespace.** `nestpp` is a placeholder. A real interface would presumably
   live in `nest` itself, which makes the wrapper names collide with the kernel
   names they forward to.
2. **Should the kernel API move rather than be wrapped?** Several findings, in
   particular the 1 based slice and the `AnyVector` status shape, are arguably
   defects in `nest.h` rather than ergonomics to be papered over. Fixing them at
   source would break `nestkernel_api.pyx`, which is the only current caller, so
   it is a decision with a known and bounded cost.
3. **How typed should parameters be?** `Params` is string keyed and variant
   valued, which mirrors Python. A per model parameter struct would catch more
   at compile time and would have to be generated, presumably from the same
   place the model documentation comes from.
4. **What happens to `slice_nc`'s convention?** Changing it is a one line change
   in `nest.cpp` plus a corresponding change in `hl_api_types.py`, and would
   remove a trap that is currently undocumented.
