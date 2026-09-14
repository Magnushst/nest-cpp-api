/*
 *  brunel_alpha.cpp
 *
 *  The Brunel (2000) balanced random network, written against the C++
 *  interface in nest_cpp/nest.hpp.
 *
 *  Compare with brunel_alpha_raw.cpp, which is the same network
 *  written against the kernel API directly, and with
 *  brunel_alpha_ref.py, which is the upstream Python example.
 *  All three build the same network in the same order, so they draw the same
 *  random numbers and must produce the same spikes; brunel/check.sh enforces
 *  that.
 *
 *  Options (all default to the upstream example's behaviour):
 *    --threads=N   OpenMP threads, default 1
 *    --seed=N      kernel RNG seed, default is NEST's own (143202461)
 *    --quiet       do not print simulation progress
 *
 *  This file is not part of NEST and does not modify NEST.
 */

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string_view>

#include "nest_api.h"

namespace
{

namespace api = nest::api;
namespace names = nest::names;
using numerics::e;

/**
 * Lower branch of the Lambert W function, W_{-1}(x), for x in (-1/e, 0), by
 * Halley iteration. The Python example takes this from SciPy; NEST has no
 * Lambert W of its own. At the single argument used here the result is bit
 * identical to scipy.special.lambertw(x, k=-1): -5.4003416797011869.
 */
double
lambert_wm1( const double x )
{
  double w = std::log( -x ) - std::log( -std::log( -x ) );

  for ( int i = 0; i < 100; ++i )
  {
    const double ew = std::exp( w );
    const double f = w * ew - x;
    const double denom = ew * ( w + 1.0 ) - ( w + 2.0 ) * f / ( 2.0 * w + 2.0 );
    const double dw = f / denom;
    w -= dw;
    if ( std::abs( dw ) < 1e-15 * ( 1.0 + std::abs( w ) ) )
    {
      break;
    }
  }
  return w;
}

/**
 * Peak of the postsynaptic potential produced by an alpha-shaped synaptic
 * current of unit amplitude, in mV per pA. Dividing the wanted PSP amplitude by
 * this gives the current amplitude to request.
 */
double
compute_psp_norm( const double tau_mem, const double c_mem, const double tau_syn )
{
  const double a = tau_mem / tau_syn;
  const double b = 1.0 / tau_syn - 1.0 / tau_mem;
  const double t_max = 1.0 / b * ( -lambert_wm1( -std::exp( -1.0 / a ) / a ) - 1.0 / a );

  return e / ( tau_syn * c_mem * b )
    * ( ( std::exp( -t_max / tau_mem ) - std::exp( -t_max / tau_syn ) ) / b - t_max * std::exp( -t_max / tau_syn ) );
}

struct Options
{
  long threads { 1 };
  long seed { 0 };  //!< 0 means "leave the kernel's own default alone"
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
    else if ( arg == "--quiet" )
    {
      opts.print_time = false;
    }
  }
  return opts;
}

}  // namespace

int
main( int argc, char* argv[] )
{
  const Options opts = parse_options( argc, argv );

  // The kernel comes up here and goes down when this scope ends, on every path
  // including the one an exception takes. The explicit shutdown at the end
  // reports the exit status; the destructor is the backstop for the paths that
  // do not reach it.
  api::Kernel kernel;
  kernel.reset();

  // --- parameters, verbatim from the Python example ----------------------
  const double dt = 0.1;          // resolution in ms
  const double simtime = 1000.0;  // simulated biological time in ms
  const double delay = 1.5;       // synaptic delay in ms

  const double g = 5.0;        // ratio of inhibitory to excitatory weight
  const double eta = 2.0;      // external rate relative to threshold rate
  const double epsilon = 0.1;  // connection probability

  const long order = 2500;
  const long NE = 4 * order;
  const long NI = 1 * order;
  const long N_rec = 50;

  const long CE = static_cast< long >( epsilon * static_cast< double >( NE ) );
  const long CI = static_cast< long >( epsilon * static_cast< double >( NI ) );

  const double tau_syn = 0.5;   // ms
  const double tau_mem = 20.0;  // ms
  const double c_mem = 250.0;   // pF
  const double theta = 20.0;    // mV

  const double J = 0.1;  // postsynaptic potential amplitude in mV
  const double J_unit = compute_psp_norm( tau_mem, c_mem, tau_syn );
  const double J_ex = J / J_unit;
  const double J_in = -g * J_ex;

  const double nu_th = ( theta * c_mem ) / ( J_ex * static_cast< double >( CE ) * e * tau_mem * tau_syn );
  const double nu_ex = eta * nu_th;
  const double p_rate = 1000.0 * nu_ex * static_cast< double >( CE );

  std::printf( "J_unit = %.17g\nJ_ex   = %.17g\np_rate = %.17g\n", J_unit, J_ex, p_rate );

  api::Params kernel_params { { names::resolution, dt },
    { names::print_time, opts.print_time },
    { names::overwrite_files, true },
    { names::local_num_threads, opts.threads } };
  if ( opts.seed > 0 )
  {
    kernel_params.set( names::rng_seed, opts.seed );
  }
  kernel.set( kernel_params );

  std::printf( "threads = %ld\nrng_seed = %ld\n",
    api::get< long >( kernel.status(), names::local_num_threads ),
    api::get< long >( kernel.status(), names::rng_seed ) );

  const api::Params neuron_params { { names::C_m, c_mem },
    { names::tau_m, tau_mem },
    { names::tau_syn_ex, tau_syn },
    { names::tau_syn_in, tau_syn },
    { names::t_ref, 2.0 },
    { names::E_L, 0.0 },
    { names::V_reset, 0.0 },
    { names::V_m, 0.0 },
    { names::V_th, theta } };

  std::printf( "Building network\n" );

  const auto nodes_ex = api::create( "iaf_psc_alpha", NE, neuron_params );
  const auto nodes_in = api::create( "iaf_psc_alpha", NI, neuron_params );
  const auto noise = api::create( "poisson_generator", 1, { { names::rate, p_rate } } );
  const auto espikes =
    api::create( "spike_recorder", 1, { { names::label, "brunel-hpp-ex" }, { names::record_to, "ascii" } } );
  const auto ispikes =
    api::create( "spike_recorder", 1, { { names::label, "brunel-hpp-in" }, { names::record_to, "ascii" } } );

  std::printf( "Connecting devices\n" );

  api::copy_model( "static_synapse", "excitatory", { { names::weight, J_ex }, { names::delay, delay } } );
  api::copy_model( "static_synapse", "inhibitory", { { names::weight, J_in }, { names::delay, delay } } );

  api::connect( noise, nodes_ex, api::ConnSpec::all_to_all(), "excitatory" );
  api::connect( noise, nodes_in, api::ConnSpec::all_to_all(), "excitatory" );

  api::connect( nodes_ex.first( N_rec ), espikes, api::ConnSpec::all_to_all(), "excitatory" );
  api::connect( nodes_in.first( N_rec ), ispikes, api::ConnSpec::all_to_all(), "excitatory" );

  std::printf( "Connecting network\n" );

  const auto all_nodes = nodes_ex + nodes_in;
  api::connect( nodes_ex, all_nodes, api::ConnSpec::fixed_indegree( CE ), "excitatory" );
  api::connect( nodes_in, all_nodes, api::ConnSpec::fixed_indegree( CI ), "inhibitory" );

  std::printf( "Simulating\n" );
  api::simulate( simtime );

  // --- results -----------------------------------------------------------
  const long events_ex = espikes.get< long >( names::n_events );
  const long events_in = ispikes.get< long >( names::n_events );

  const double rate_ex = static_cast< double >( events_ex ) / simtime * 1000.0 / static_cast< double >( N_rec );
  const double rate_in = static_cast< double >( events_in ) / simtime * 1000.0 / static_cast< double >( N_rec );

  const long num_synapses = api::get< long >( api::model_defaults( "excitatory" ), names::num_connections )
    + api::get< long >( api::model_defaults( "inhibitory" ), names::num_connections );

  // NEST instruments itself; read its own stopwatches rather than adding a
  // clock of our own. The per-thread timers beside these (time_update and the
  // rest) are only filled in a build configured with detailed timers.
  const Dictionary kernel_final = kernel.status();
  const double t_create = api::get< double >( kernel_final, names::time_construction_create );
  const double t_connect = api::get< double >( kernel_final, names::time_construction_connect );
  const double t_simulate = api::get< double >( kernel_final, names::time_simulate );

  std::printf( "Brunel network simulation (C++, nest::api interface)\n" );
  std::printf( "Number of neurons : %ld\n", NE + NI );
  std::printf( "Number of synapses: %ld\n", num_synapses );
  std::printf( "Excitatory events : %ld\n", events_ex );
  std::printf( "Inhibitory events : %ld\n", events_in );
  std::printf( "Excitatory rate   : %.2f Hz\n", rate_ex );
  std::printf( "Inhibitory rate   : %.2f Hz\n", rate_in );
  std::printf( "Building time     : %.3f s (create %.3f, connect %.3f)\n", t_create + t_connect, t_create, t_connect );
  std::printf( "Simulation time   : %.3f s\n", t_simulate );

  kernel.shutdown( 0 );
  return 0;
}
