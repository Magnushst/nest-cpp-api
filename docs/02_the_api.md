# The nest::api interface

`include/nest/nest_api.h` is a header only layer over `nestkernel/nest.h`, in
namespace `nest::api`. It is written to be dropped into the NEST tree as
`nestkernel/nest_api.h`, where it installs into `include/nest` and is included
as `#include "nest_api.h"`, the same spelling as inside the tree. It adds no
simulation behaviour:
every call forwards to the kernel, the network is built in the same order, and
the same random numbers are drawn. The proof of that is that
`brunel/brunel_alpha.cpp` writes spike files byte identical to the
upstream Python example.

What it adds is the part PyNEST adds on the Python side and nobody has added on
the C++ side.

## Scope, and how it grows

The interface covers what the models in this repository need and what
`tests/api_test.cpp` can exercise against a running kernel, and nothing
speculative. That is a deliberate rule rather than an accident of being early: an
interface whose every line has been run against a checked result is more useful
than a sketch of the whole API. Each new model extends it by exactly what that
model needs, and the check for that model pins the extension.

Two models and one test have shaped it so far. Brunel fixed the core: the
kernel as an object, node collections as values, parameters beside the
population they belong to, typed status reads, connectivity rules. The
microcircuit added the `Parameter` system, `fixed_total_number`, per-node
parameter vectors, indexing, and `prepare`/`run`/`cleanup`. The test added what
a C++ caller needs but neither model happens to use: parameter arithmetic,
connection queries, `get_nodes`, membership and identity on node collections,
vector-valued status, module loading and the MPI rank queries.

## The decisions, each answering an audit finding

Seven decisions against the seven surprises in the audit. The findings are
numbered as in [01_state_of_the_kernel.md](01_state_of_the_kernel.md); the last
decision answers two of them at once, which is why the summary table in the
[README](../README.md) has eight rows against seven headings here.

### 1. `Kernel` is a scoped object (finding 7)

```cpp
nest::api::Kernel kernel;   // init_nest here
kernel.reset();
...
kernel.shutdown( 0 );    // explicit, with a real exit code
                         // and the destructor as the backstop for paths that
                         // do not reach it, including an exception unwinding
```

Non copyable and non movable, because there is one kernel per process. It owns
the `argv` storage that `init_nest` needs, including the null terminator OpenMPI
requires.

The destructor is a backstop rather than the normal route. It reports success,
because a destructor cannot know otherwise, and it swallows any exception,
because throwing out of a destructor during unwinding calls `std::terminate`. A
program with a meaningful exit status calls `shutdown` itself; calling it twice
is harmless.

### 2. `create` takes parameters (finding 3)

```cpp
const auto nodes_ex = nest::api::create( "iaf_psc_alpha", NE, neuron_params );
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
const nest::api::Params neuron_params { { names::C_m, c_mem },
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
nest::api::connect( noise, nodes_ex, nest::api::ConnSpec::all_to_all(), "excitatory" );
nest::api::connect( nodes_ex, all_nodes, nest::api::ConnSpec::fixed_indegree( CE ), "excitatory" );
```

`ConnSpec` has named factories for the rules Brunel uses and falls back to
`all_to_all`, as PyNEST does. `SynSpec` is implicitly constructible from a model
name, so the bare string works as it does in Python. The vector wrapping the
synapse dictionary is built inside `connect`.

### 7. `operator+` and the names are re-exported (finding 6)

`nest::api::NodeCollection::operator+` forwards to the one in
`nestkernel/node_collection.h`, and `nest::api::names` is an alias for
`nest::names`, so a program needs one include.

### 8. Parameters are values, and the operations on them are reachable (finding 8)

```cpp
const api::SynSpec syn { api::Params {
  { names::synapse_model, std::string( "static_synapse" ) },
  { names::weight, api::math::redraw( api::random::normal( w_mean, w_std ), w_min, w_max ) },
  { names::delay, api::math::redraw( api::random::normal( d_mean, d_std ), delay_min, INF ) } } };

api::connect( pops[ j ], pops[ i ], api::ConnSpec::fixed_total_number( n ), syn );
```

The microcircuit writes that 64 times, once per pair of populations. Against the
kernel API directly, the same specification is this, which compiles and is what
the interface removes:

```cpp
#include "parameter.h"   // redraw_parameter is not declared in nest.h

Dictionary weight_spec;
weight_spec[ nest::names::mean ] = w_mean;
weight_spec[ nest::names::std ] = w_std;
nest::ParameterPTR weight =
  nest::redraw_parameter( nest::create_parameter( "normal", weight_spec ), w_min, w_max );

Dictionary delay_spec;
delay_spec[ nest::names::mean ] = d_mean;
delay_spec[ nest::names::std ] = d_std;
nest::ParameterPTR delay =
  nest::redraw_parameter( nest::create_parameter( "normal", delay_spec ), delay_min, INF );

Dictionary syn;
syn[ nest::names::synapse_model ] = std::string( "static_synapse" );
syn[ nest::names::weight ] = weight;
syn[ nest::names::delay ] = delay;
const std::vector< Dictionary > syn_specs { syn };

Dictionary conn;
conn[ nest::names::rule ] = std::string( "fixed_total_number" );
conn[ nest::names::N ] = n;

nest::connect( pops[ j ], pops[ i ], conn, syn_specs );
```

Twenty lines against five, a named temporary for every dictionary because
`connect` takes the synapse specification as a vector, and an extra kernel
header because half the `Parameter` system is not in `nest.h`. Both forms were
compiled; neither is a caricature of the other.

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
Brunel programs, one against this interface and one against the kernel API
directly, agree to within run to run noise, which is what should happen when the
work is all inside the kernel. See the table in the README.

## Tested

`tests/api_test.cpp` runs 62 checks against a running kernel, covering every
entry point in the header and the failures a caller can provoke: a negative
slice index, a scalar read of a many-node collection, an unknown key, a wrong
type, a per-node vector of the wrong length, and `one_to_one` between
collections of different sizes. Each must arrive as the NEST exception it should
be rather than as a crash or a wrong answer.

## What is deliberately not here

* **Spatial networks.** `create_spatial`, masks, and position based parameters.
* **Recording to memory.** Both models record to file, as the upstream examples
  do. Reading spikes back out of a `spike_recorder` in memory means reading a
  dictionary of vectors out of one node's status, which is a different shape
  from `get_all`, and no model here needs it.
* **Tripartite connections**, `connect_arrays` and SONATA. Each is a separate
  entry point in `nest.h` with its own argument conventions, and nothing here
  uses any of them.
* **Structural plasticity.** `enable_structural_plasticity` and the growth
  curves behind it.
* **MUSIC.** The only C++ code in NEST's own `examples/` directory is MUSIC,
  and it goes through a different interface.

All of these remain reachable through `nestkernel/nest.h` directly: the
interface is a layer over the kernel API, not a replacement for it, and mixing
the two in one program is expected.

## Open questions for the NEST team

1. **Namespace.** The interface sits in `nest::api`, nested inside the kernel's
   own namespace, because the names deliberately match the kernel functions they
   forward to: `nest::api::create` calls `nest::create`. Putting it directly in
   `nest` would collide; a separate top level namespace would read as though it
   were a separate project. This is a proposal, not a constraint.
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
