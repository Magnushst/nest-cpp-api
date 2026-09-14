/*
 *  microcircuit.cpp
 *
 *  The cortical microcircuit of Potjans and Diesmann (2014), written against
 *  the C++ interface in nest_cpp/nest.hpp.
 *
 *  Compare with reference/, which is the upstream PyNEST implementation of the
 *  same model. Both build the same network in the same order with the same
 *  parameters, so they draw the same random numbers and must produce the same
 *  spikes; pd14/check.sh enforces that.
 *
 *  The order of the calls below is not a matter of taste. Every create, every
 *  set of a distributed parameter and every connect consumes random numbers, so
 *  the sequence has to follow reference/network.py statement for statement. The
 *  comments name the method each block corresponds to.
 *
 *  Options (all default to the upstream example's behaviour):
 *    --threads=N      OpenMP threads, default 4
 *    --seed=N         kernel RNG seed, default 55
 *    --n-scaling=F    factor on the population sizes, default 0.1
 *    --k-scaling=F    factor on the indegrees, default 0.1
 *    --presim=T       presimulation time in ms, default 500
 *    --sim=T          simulation time in ms, default 1000
 *    --data-path=DIR  where the spike files go, default the working directory
 *    --derived        print the derived quantities and exit
 *    --quiet          do not print simulation progress
 *
 *  This file is not part of NEST and does not modify NEST.
 */

#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include "microcircuit_params.hpp"
#include "nest_api.h"

namespace
{

namespace api = nest::api;
namespace names = nest::names;

constexpr double INF = std::numeric_limits< double >::infinity();

struct Options
{
  long threads { 4 };
  long seed { 55 };
  double n_scaling { 0.1 };
  double k_scaling { 0.1 };
  double presim { 500.0 };
  double sim { 1000.0 };
  std::string data_path { "." };
  bool derived_only { false };
  bool print_time { true };
};

Options
parse_options( const int argc, char* const argv[] )
{
  Options opts;
  for ( int i = 1; i < argc; ++i )
  {
    const std::string_view arg { argv[ i ] };
    if ( arg.starts_with( "--threads=" ) )
    {
      opts.threads = std::atol( argv[ i ] + 10 );
    }
    else if ( arg.starts_with( "--seed=" ) )
    {
      opts.seed = std::atol( argv[ i ] + 7 );
    }
    else if ( arg.starts_with( "--n-scaling=" ) )
    {
      opts.n_scaling = std::atof( argv[ i ] + 12 );
    }
    else if ( arg.starts_with( "--k-scaling=" ) )
    {
      opts.k_scaling = std::atof( argv[ i ] + 12 );
    }
    else if ( arg.starts_with( "--presim=" ) )
    {
      opts.presim = std::atof( argv[ i ] + 9 );
    }
    else if ( arg.starts_with( "--sim=" ) )
    {
      opts.sim = std::atof( argv[ i ] + 6 );
    }
    else if ( arg.starts_with( "--data-path=" ) )
    {
      opts.data_path = argv[ i ] + 12;
    }
    else if ( arg == "--derived" )
    {
      opts.derived_only = true;
    }
    else if ( arg == "--quiet" )
    {
      opts.print_time = false;
    }
  }
  return opts;
}

/**
 * Print the derived network to 17 significant digits, in the format
 * reference/run_ref.py --derived uses, so that the two can be diffed.
 */
void
print_derived( const pd14::Derived& d )
{
  std::printf( "num_neurons:  " );
  for ( const long v : d.num_neurons )
  {
    std::printf( " %ld", v );
  }
  std::printf( "\next_indegrees:" );
  for ( const long v : d.ext_indegrees )
  {
    std::printf( " %ld", v );
  }
  std::printf( "\n" );
  for ( size_t i = 0; i < pd14::NUM_POPS; ++i )
  {
    std::printf( "num_synapses[%zu]:", i );
    for ( const long v : d.num_synapses[ i ] )
    {
      std::printf( " %ld", v );
    }
    std::printf( "\n" );
  }
  std::printf( "weight_ext:    %.17g\n", d.weight_ext );
  for ( size_t i = 0; i < pd14::NUM_POPS; ++i )
  {
    std::printf( "weight[%zu]:", i );
    for ( const double v : d.weight_mean[ i ] )
    {
      std::printf( " %.17g", v );
    }
    std::printf( "\n" );
  }
  std::printf( "DC_amp:       " );
  for ( const double v : d.dc_amp )
  {
    std::printf( " %.17g", v );
  }
  std::printf( "\n" );
}

} // namespace

int
main( int argc, char* argv[] )
{
  const Options opts = parse_options( argc, argv );
  const pd14::Derived net = pd14::derive( opts.n_scaling, opts.k_scaling );

  if ( opts.derived_only )
  {
    print_derived( net );
    return 0;
  }

  // The kernel comes up here and goes down when this scope ends, on every path
  // including the one an exception takes.
  api::Kernel kernel;

  // --- Network.__setup_nest ----------------------------------------------
  kernel.reset();
  kernel.set( { { names::local_num_threads, opts.threads },
    { names::resolution, 0.1 },
    { names::rng_seed, opts.seed },
    { names::overwrite_files, true },
    { names::print_time, opts.print_time } } );

  const double resolution = api::get< double >( kernel.status(), names::resolution );

  // Every rank runs this same program; only rank 0 says so, as the upstream
  // model does.
  const bool speak = api::rank() == 0;
  const long num_ranks = api::num_processes();
  if ( speak )
  {
    std::printf( "RNG seed: %ld\n", api::get< long >( kernel.status(), names::rng_seed ) );
    std::printf( "Total number of virtual processes: %ld\n", api::num_virtual_processes() );
  }

  // --- Network.__create_neuronal_populations ------------------------------
  // Create, then set the fixed parameters, then set the distributed initial
  // membrane potential: three kernel calls per population, in that order,
  // because the third draws random numbers and the first two do not.
  if ( speak )
  {
    std::printf( "Creating neuronal populations.\n" );
  }
  std::vector< api::NodeCollection > pops;
  pops.reserve( pd14::NUM_POPS );
  for ( size_t i = 0; i < pd14::NUM_POPS; ++i )
  {
    const auto population = api::create( "iaf_psc_exp", net.num_neurons[ i ] );
    population.set( { { names::tau_syn_ex, pd14::TAU_SYN },
      { names::tau_syn_in, pd14::TAU_SYN },
      { names::E_L, pd14::E_L },
      { names::V_th, pd14::V_TH },
      { names::V_reset, pd14::V_RESET },
      { names::t_ref, pd14::T_REF },
      { names::I_e, net.dc_amp[ i ] } } );
    population.set( { { names::V_m, api::random::normal( pd14::V0_MEAN[ i ], pd14::V0_STD[ i ] ) } } );
    pops.push_back( population );
  }

  // The node IDs bounding each population, as reference/network.py writes them.
  // They are part of what pd14/check.sh compares: identical IDs mean the two
  // programs created the same nodes in the same order.
  {
    const std::string path = opts.data_path + "/population_nodeids.dat";
    std::FILE* f = std::fopen( path.c_str(), "w" );
    if ( f == nullptr )
    {
      throw nest::IOError();
    }
    for ( const auto& pop : pops )
    {
      const long n = static_cast< long >( pop.size() );
      std::fprintf( f, "%ld %ld\n", pop[ 0 ].get< long >( names::global_id ), pop[ n - 1 ].get< long >( names::global_id ) );
    }
    std::fclose( f );
  }

  // --- Network.__create_recording_devices ---------------------------------
  if ( speak )
  {
    std::printf( "Creating recording devices.\n" );
  }
  const auto spike_recorders = api::create( "spike_recorder",
    pd14::NUM_POPS,
    { { names::record_to, std::string( "ascii" ) }, { names::label, opts.data_path + "/spike_recorder" } } );

  // --- Network.__create_poisson_bg_input ----------------------------------
  if ( speak )
  {
    std::printf( "Creating Poisson generators for background input.\n" );
  }
  const auto poisson_bg_input = api::create( "poisson_generator", pd14::NUM_POPS );
  {
    std::vector< api::Params > rates;
    rates.reserve( pd14::NUM_POPS );
    for ( size_t i = 0; i < pd14::NUM_POPS; ++i )
    {
      rates.push_back( { { names::rate, pd14::BG_RATE * static_cast< double >( net.ext_indegrees[ i ] ) } } );
    }
    poisson_bg_input.set( rates );
  }

  // --- Network.__connect_neuronal_populations -----------------------------
  // Weights and delays are drawn per connection. Redrawing rather than clipping
  // keeps the distribution intact: an excitatory weight stays positive, an
  // inhibitory one stays negative, and a delay stays at or above the
  // resolution. The lower bound on the delay is half a step below the
  // resolution because the kernel rounds delays to the grid.
  if ( speak )
  {
    std::printf( "Connecting neuronal populations recurrently.\n" );
  }
  const double delay_min = resolution - 0.5 * resolution;
  for ( size_t i = 0; i < pd14::NUM_POPS; ++i )
  {
    for ( size_t j = 0; j < pd14::NUM_POPS; ++j )
    {
      const double weight_mean = net.weight_mean[ i ][ j ];
      const double weight_std = std::abs( weight_mean * pd14::WEIGHT_REL_STD );
      const double weight_min = weight_mean < 0.0 ? -INF : 0.0;
      const double weight_max = weight_mean < 0.0 ? 0.0 : INF;

      const double delay_mean = j % 2 == 0 ? pd14::DELAY_EXC_MEAN : pd14::DELAY_INH_MEAN;
      const double delay_std = delay_mean * pd14::DELAY_REL_STD;

      const api::SynSpec syn { api::Params {
        { names::synapse_model, std::string( "static_synapse" ) },
        { names::weight, api::math::redraw( api::random::normal( weight_mean, weight_std ), weight_min, weight_max ) },
        { names::delay, api::math::redraw( api::random::normal( delay_mean, delay_std ), delay_min, INF ) } } };

      api::connect( pops[ j ], pops[ i ], api::ConnSpec::fixed_total_number( net.num_synapses[ i ][ j ] ), syn );
    }
  }

  // --- Network.__connect_recording_devices --------------------------------
  if ( speak )
  {
    std::printf( "Connecting recording devices.\n" );
  }
  for ( size_t i = 0; i < pd14::NUM_POPS; ++i )
  {
    api::connect( pops[ i ], spike_recorders[ static_cast< long >( i ) ] );
  }

  // --- Network.__connect_poisson_bg_input ---------------------------------
  if ( speak )
  {
    std::printf( "Connecting Poisson generators for background input.\n" );
  }
  for ( size_t i = 0; i < pd14::NUM_POPS; ++i )
  {
    const api::SynSpec syn { api::Params { { names::synapse_model, std::string( "static_synapse" ) },
      { names::weight, net.weight_ext },
      { names::delay, pd14::DELAY_POISSON } } };
    api::connect( poisson_bg_input[ static_cast< long >( i ) ], pops[ i ], api::ConnSpec::all_to_all(), syn );
  }

  // Building the presynaptic side of the connections happens on the first
  // simulate. The upstream model forces it here so that it is charged to the
  // connection phase rather than to the simulation; doing the same keeps the
  // reported timings comparable.
  api::prepare();
  api::cleanup();

  // --- simulate ------------------------------------------------------------
  // A presimulation first, whose spikes are recorded but whose startup
  // transient is not representative of the network state.
  if ( speak )
  {
    std::printf( "Simulating %.1f ms.\n", opts.presim );
  }
  api::simulate( opts.presim );
  if ( speak )
  {
    std::printf( "Simulating %.1f ms.\n", opts.sim );
  }
  api::simulate( opts.sim );

  // --- results --------------------------------------------------------------
  const Dictionary kernel_final = kernel.status();
  const double t_create = api::get< double >( kernel_final, names::time_construction_create );
  const double t_connect = api::get< double >( kernel_final, names::time_construction_connect );
  const double t_simulate = api::get< double >( kernel_final, names::time_simulate );
  const long network_size = api::get< long >( kernel_final, names::network_size );
  const long num_connections = api::get< long >( kernel_final, names::num_connections );
  const double t_total = opts.presim + opts.sim;

  if ( not speak )
  {
    kernel.shutdown( 0 );
    return 0;
  }

  std::printf( "Microcircuit simulation (C++, nest_cpp interface)\n" );
  // On more than one rank each recorder counts only the spikes of the neurons
  // that live on this rank, so the counts below are rank 0's share rather than
  // the total. The spike files hold all of it.
  if ( num_ranks > 1 )
  {
    std::printf( "Event counts below are rank 0's share of %ld ranks.\n", num_ranks );
  }
  std::printf( "Number of neurons : %ld\n", network_size - 2 * static_cast< long >( pd14::NUM_POPS ) );
  std::printf( "Number of synapses: %ld\n", num_connections );
  for ( size_t i = 0; i < pd14::NUM_POPS; ++i )
  {
    const long events = spike_recorders[ static_cast< long >( i ) ].get< long >( names::n_events );
    std::printf( "%-5s : %6ld neurons, %9ld events, %7.3f Hz\n",
      pd14::POPULATIONS[ i ].c_str(),
      net.num_neurons[ i ],
      events,
      static_cast< double >( events ) / static_cast< double >( net.num_neurons[ i ] ) / t_total * 1000.0 );
  }
  std::printf( "Building time     : %.3f s (create %.3f, connect %.3f)\n", t_create + t_connect, t_create, t_connect );
  std::printf( "Simulation time   : %.3f s\n", t_simulate );

  kernel.shutdown( 0 );
  return 0;
}
