/*
 *  brunel_alpha.cpp
 *
 *  The Brunel (2000) balanced random network, written against the drafted C++
 *  interface in nest_cpp/nest.hpp.
 *
 *  Compare with ../../reference/brunel_alpha_raw.cpp, which is the same network
 *  written against the kernel API directly, and with
 *  ../../validation/brunel_alpha_ref.py, which is the upstream Python example.
 *  All three build the same network and must produce the same spikes.
 *
 *  This file is not part of NEST and does not modify NEST.
 */

#include <cmath>
#include <cstdio>

#include "nest_cpp/nest.hpp"

namespace
{

/**
 * Lower branch of the Lambert W function, W_{-1}(x), for x in (-1/e, 0),
 * by Halley iteration. The Python example takes this from SciPy.
 */
double
lambert_wm1( const double x )
{
  double w = std::log( -x ) - std::log( -std::log( -x ) );

  for ( int i = 0; i < 100; ++i )
  {
    const double e = std::exp( w );
    const double f = w * e - x;
    const double denom = e * ( w + 1.0 ) - ( w + 2.0 ) * f / ( 2.0 * w + 2.0 );
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
 * Peak of the postsynaptic potential for a unit-amplitude alpha current,
 * used to normalise the synaptic weights.
 */
double
compute_psp_norm( const double tau_mem, const double c_mem, const double tau_syn )
{
  const double a = tau_mem / tau_syn;
  const double b = 1.0 / tau_syn - 1.0 / tau_mem;
  const double t_max = 1.0 / b * ( -lambert_wm1( -std::exp( -1.0 / a ) / a ) - 1.0 / a );

  return std::exp( 1.0 ) / ( tau_syn * c_mem * b )
    * ( ( std::exp( -t_max / tau_mem ) - std::exp( -t_max / tau_syn ) ) / b - t_max * std::exp( -t_max / tau_syn ) );
}

} // namespace

int
main()
{
  const nestpp::Kernel kernel;
  kernel.reset();

  // --- parameters --------------------------------------------------------
  const double dt = 0.1;         // resolution in ms
  const double simtime = 1000.0; // simulated biological time in ms
  const double delay = 1.5;      // synaptic delay in ms

  const double g = 5.0;       // ratio of inhibitory to excitatory weight
  const double eta = 2.0;     // external rate relative to threshold rate
  const double epsilon = 0.1; // connection probability

  const long order = 2500;
  const long NE = 4 * order;
  const long NI = 1 * order;
  const long N_rec = 50;

  const long CE = static_cast< long >( epsilon * static_cast< double >( NE ) );
  const long CI = static_cast< long >( epsilon * static_cast< double >( NI ) );

  const double tau_syn = 0.5;
  const double tau_mem = 20.0;
  const double c_mem = 250.0;
  const double theta = 20.0;

  const double J = 0.1;
  const double J_unit = compute_psp_norm( tau_mem, c_mem, tau_syn );
  const double J_ex = J / J_unit;
  const double J_in = -g * J_ex;

  const double nu_th = ( theta * c_mem ) / ( J_ex * static_cast< double >( CE ) * std::exp( 1.0 ) * tau_mem * tau_syn );
  const double p_rate = 1000.0 * eta * nu_th * static_cast< double >( CE );

  std::printf( "J_unit = %.17g\nJ_ex   = %.17g\np_rate = %.17g\n", J_unit, J_ex, p_rate );

  kernel.set( { { "resolution", dt }, { "print_time", true }, { "overwrite_files", true } } );

  const nestpp::Params neuron_params { { "C_m", c_mem },
    { "tau_m", tau_mem },
    { "tau_syn_ex", tau_syn },
    { "tau_syn_in", tau_syn },
    { "t_ref", 2.0 },
    { "E_L", 0.0 },
    { "V_reset", 0.0 },
    { "V_m", 0.0 },
    { "V_th", theta } };

  std::printf( "Building network\n" );

  const auto nodes_ex = nestpp::create( "iaf_psc_alpha", NE, neuron_params );
  const auto nodes_in = nestpp::create( "iaf_psc_alpha", NI, neuron_params );
  const auto noise = nestpp::create( "poisson_generator", 1, { { "rate", p_rate } } );
  const auto espikes = nestpp::create( "spike_recorder", 1, { { "label", "brunel-hpp-ex" }, { "record_to", "ascii" } } );
  const auto ispikes = nestpp::create( "spike_recorder", 1, { { "label", "brunel-hpp-in" }, { "record_to", "ascii" } } );

  std::printf( "Connecting devices\n" );

  nestpp::copy_model( "static_synapse", "excitatory", { { "weight", J_ex }, { "delay", delay } } );
  nestpp::copy_model( "static_synapse", "inhibitory", { { "weight", J_in }, { "delay", delay } } );

  nestpp::connect( noise, nodes_ex, nestpp::ConnSpec::all_to_all(), "excitatory" );
  nestpp::connect( noise, nodes_in, nestpp::ConnSpec::all_to_all(), "excitatory" );

  nestpp::connect( nodes_ex.first( N_rec ), espikes, nestpp::ConnSpec::all_to_all(), "excitatory" );
  nestpp::connect( nodes_in.first( N_rec ), ispikes, nestpp::ConnSpec::all_to_all(), "excitatory" );

  std::printf( "Connecting network\n" );

  const auto all_nodes = nodes_ex + nodes_in;
  nestpp::connect( nodes_ex, all_nodes, nestpp::ConnSpec::fixed_indegree( CE ), "excitatory" );
  nestpp::connect( nodes_in, all_nodes, nestpp::ConnSpec::fixed_indegree( CI ), "inhibitory" );

  std::printf( "Simulating\n" );
  nestpp::simulate( simtime );

  // --- results -----------------------------------------------------------
  const long events_ex = espikes.get< long >( "n_events" );
  const long events_in = ispikes.get< long >( "n_events" );

  const double rate_ex = static_cast< double >( events_ex ) / simtime * 1000.0 / static_cast< double >( N_rec );
  const double rate_in = static_cast< double >( events_in ) / simtime * 1000.0 / static_cast< double >( N_rec );

  const long num_synapses = nestpp::get< long >( nestpp::model_defaults( "excitatory" ), "num_connections" )
    + nestpp::get< long >( nestpp::model_defaults( "inhibitory" ), "num_connections" );

  std::printf( "Brunel network simulation (C++, drafted interface)\n" );
  std::printf( "Number of neurons : %ld\n", NE + NI );
  std::printf( "Number of synapses: %ld\n", num_synapses );
  std::printf( "Excitatory events : %ld\n", events_ex );
  std::printf( "Inhibitory events : %ld\n", events_in );
  std::printf( "Excitatory rate   : %.2f Hz\n", rate_ex );
  std::printf( "Inhibitory rate   : %.2f Hz\n", rate_in );

  return 0;
}
