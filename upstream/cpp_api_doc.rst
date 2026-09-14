.. _cpp_api:

Driving NEST from C++
=====================

NEST's simulation kernel is a C++ library, and ``nestkernel/nest.h`` declares
its API as ordinary C++ functions. ``nestkernel/nest_api.h`` is a header-only
layer over that API, in namespace ``nest::api``, for programs that want to embed
NEST rather than drive it from Python.

It adds no simulation behaviour. Every call forwards to the kernel, so a network
built through it is built in the same order and draws the same random numbers as
the same network built from PyNEST. The two examples in ``examples/cpp`` produce
spike files byte identical to the PyNEST examples they were translated from.

A first program
---------------

.. code-block:: cpp

   #include "nest_api.h"

   namespace api = nest::api;
   namespace names = nest::names;

   int main( int argc, char* argv[] )
   {
     api::Kernel kernel;                     // init_nest here, shutdown at scope exit
     kernel.set( { { names::resolution, 0.1 }, { names::local_num_threads, 4L } } );

     const auto neurons = api::create( "iaf_psc_alpha", 1000, { { names::V_th, -50.0 } } );
     const auto noise = api::create( "poisson_generator", 1, { { names::rate, 8000.0 } } );
     const auto recorder = api::create( "spike_recorder" );

     api::connect( noise, neurons );
     api::connect( neurons, neurons, api::ConnSpec::fixed_indegree( 100 ),
       api::SynSpec { api::Params { { names::weight, api::random::normal( 10.0, 1.0 ) } } } );
     api::connect( neurons, recorder );

     api::simulate( 1000.0 );

     std::printf( "%ld spikes\n", recorder.get< long >( names::n_events ) );
     kernel.shutdown( 0 );
   }

Building
--------

An installed NEST provides the headers and the kernel libraries, and
``nest-config`` reports both:

.. code-block:: sh

   g++ -std=c++20 -fopenmp $(nest-config --includes) -c model.cpp -o model.o
   g++ -fopenmp -o model model.o $(nest-config --kernel-libs)

``--kernel-libs`` is for a standalone program that embeds the kernel. Extension
modules keep using ``--libs``, since they are loaded into a process that already
holds a kernel and must not link a second copy of it.

What the interface offers
-------------------------

============================  ==========================================================
``Kernel``                    Brings the kernel and MPI up, and takes them down on every
                              path out of the scope, including an exception.
``Params``                    Named parameters with their own storage, written where
                              they are used.
``create``                    Create nodes and give them their parameters in one call.
``NodeCollection``            Slicing and indexing with Python semantics, concatenation,
                              membership, node IDs, and typed status reads.
``ConnSpec`` / ``SynSpec``    Connectivity rules with the default PyNEST uses, and a
                              synapse specification built from a model name.
``Parameter``                 The kernel's distributions and the arithmetic on them:
                              ``random::normal``, ``math::redraw``, and the rest.
``ConnectionCollection``      Query connections, read and write their properties,
                              disconnect them.
``simulate`` / ``prepare`` /  Whole and partial simulation.
``run`` / ``cleanup``
``rank`` / ``num_processes``  Which MPI rank this is, for deciding which one prints.
============================  ==========================================================

Failures arrive as the kernel's own exceptions, all deriving from
``nest::KernelException``: ``nest::BadParameter``, ``nest::TypeMismatch``,
``nest::DimensionMismatch`` and ``nest::KeyError``.

What it does not cover
----------------------

Spatial networks (``create_spatial``, masks, position-dependent parameters),
tripartite connections, ``connect_arrays``, SONATA, and structural plasticity.
These are reachable through ``nest.h`` directly; they simply have no wrapper
yet.

Reproducibility
---------------

The thread count and the number of MPI ranks decide which virtual process owns
which node, and therefore which random stream each node draws from. A network
built with a different number of virtual processes is statistically equivalent
but not the same network, exactly as in PyNEST.
