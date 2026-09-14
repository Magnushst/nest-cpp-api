/*
 *  brunel_alpha_raw.cpp
 *
 *  The Brunel (2000) balanced random network, written against the NEST kernel
 *  C++ API exactly as that API exists today in `nestkernel/nest.h`.
 *
 *  This file is deliberately unhelped: nothing is wrapped, shortened or made
 *  safer. It is the control against which the nest_cpp interface in
 *  include/nest_cpp/nest.hpp is compared, and it is the evidence that the kernel
 *  API can be driven from an ordinary C++ program. It follows NEST's own
 *  conventions, so that the comparison is between two interfaces and not
 *  between good and bad style: dictionary keys come from `nest::names`, the
 *  constant e comes from `numerics`, and failures are NEST exceptions.
 *
 *  It is a translation of pynest/examples/brunel_alpha_nest.py from NEST 3.10.0
 *  (commit acca9704d). Every model parameter is taken from that file.
 *
 *  Options (all default to the upstream example's behaviour):
 *    --threads=N   OpenMP threads, default 1
 *    --seed=N      kernel RNG seed, default is NEST's own (143202461)
 *    --quiet       do not print simulation progress
 *
 *  This file is NOT part of NEST and does not modify NEST.
 */

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "dictionary.h"
#include "exceptions.h"
#include "nest.h"
#include "nest_names.h"
#include "node_collection.h"
#include "numerics.h"

namespace
{

/**
 * Lower branch of the Lambert W function, W_{-1}(x), for x in (-1/e, 0).
 *
 * The Python example calls scipy.special.lambertw with k = -1. NEST has no
 * Lambert W of its own and does not require GSL, so rather than add a dependency
 * for one scalar we solve w * exp(w) = x by Halley iteration. The branch is
 * selected by the starting point, which lies below -1 for x in this range.
 *
 * At the single argument this program evaluates, x = -exp(-1/40)/40, the result
 * agrees with SciPy to all 16 significant digits (-5.400341679701187).
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
 * Peak of the postsynaptic potential produced by an alpha-shaped synaptic
 * current of unit amplitude, in mV per pA.
 *
 * Dividing the wanted PSP amplitude by this gives the current amplitude to
 * request, which is what makes J below a postsynaptic *potential* of 0.1 mV
 * rather than an uncalibrated current. Same expression as ComputePSPnorm in the
 * Python example.
 */
double
compute_psp_norm( const double tau_mem, const double c_mem, const double tau_syn )
{
  const double a = tau_mem / tau_syn;
  const double b = 1.0 / tau_syn - 1.0 / tau_mem;
  const double t_max = 1.0 / b * ( -lambert_wm1( -std::exp( -1.0 / a ) / a ) - 1.0 / a );

  return numerics::e / ( tau_syn * c_mem * b )
    * ( ( std::exp( -t_max / tau_mem ) - std::exp( -t_max / tau_syn ) ) / b - t_max * std::exp( -t_max / tau_syn ) );
}

struct Options
{
  long threads { 1 };
  long seed { 0 }; //!< 0 means "leave the kernel's own default alone"
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

} // namespace

int
main( int argc, char* argv[] )
{
  // ---------------------------------------------------------------------
  // The kernel must be brought up by hand. There is no scoped handle for it,
  // and shutdown_nest() has to be reached on every path, including the ones
  // an exception takes, or MPI is left un-finalised.
  // ---------------------------------------------------------------------
  nest::init_nest( &argc, &argv );
  nest::reset_kernel();

  const Options opts = parse_options( argc, argv );

  // --- simulation parameters, verbatim from the Python example -----------
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

  const double tau_syn = 0.5; // ms
  const double tau_mem = 20.0; // ms
  const double c_mem = 250.0;  // pF
  const double theta = 20.0;   // mV

  const double J = 0.1; // postsynaptic potential amplitude in mV
  const double J_unit = compute_psp_norm( tau_mem, c_mem, tau_syn );
  const double J_ex = J / J_unit;
  const double J_in = -g * J_ex;

  // Rate of external input that would on its own drive the membrane to
  // threshold, and the Poisson rate that realises eta times it.
  const double nu_th = ( theta * c_mem ) / ( J_ex * static_cast< double >( CE ) * numerics::e * tau_mem * tau_syn );
  const double nu_ex = eta * nu_th;
  const double p_rate = 1000.0 * nu_ex * static_cast< double >( CE );

  std::printf( "J_unit = %.17g\nJ_ex   = %.17g\np_rate = %.17g\n", J_unit, J_ex, p_rate );

  // --- kernel status -----------------------------------------------------
  // Keys come from nest::names, so a misspelling is a compile error rather than
  // a silently ignored entry.
  Dictionary kernel_status;
  kernel_status[ nest::names::resolution ] = dt;
  kernel_status[ nest::names::print_time ] = opts.print_time;
  kernel_status[ nest::names::overwrite_files ] = true;
  kernel_status[ nest::names::local_num_threads ] = opts.threads;
  if ( opts.seed > 0 )
  {
    kernel_status[ nest::names::rng_seed ] = opts.seed;
  }
  nest::set_kernel_status( kernel_status );

  std::printf( "threads = %ld\nrng_seed = %ld\n",
    nest::get_kernel_status().get< long >( nest::names::local_num_threads ),
    nest::get_kernel_status().get< long >( nest::names::rng_seed ) );

  // --- neuron parameters -------------------------------------------------
  Dictionary neuron_params;
  neuron_params[ nest::names::C_m ] = c_mem;
  neuron_params[ nest::names::tau_m ] = tau_mem;
  neuron_params[ nest::names::tau_syn_ex ] = tau_syn;
  neuron_params[ nest::names::tau_syn_in ] = tau_syn;
  neuron_params[ nest::names::t_ref ] = 2.0;
  neuron_params[ nest::names::E_L ] = 0.0;
  neuron_params[ nest::names::V_reset ] = 0.0;
  neuron_params[ nest::names::V_m ] = 0.0;
  neuron_params[ nest::names::V_th ] = theta;

  std::printf( "Building network\n" );

  // --- nodes -------------------------------------------------------------
  // nest::create() takes no parameter dictionary. Unlike nest.Create(...,
  // params=...) in Python, the parameters must be applied in a second step, and
  // set_nc_status() takes a *vector* of dictionaries by non-const reference, so
  // the vector cannot be a temporary.
  nest::NodeCollectionPTR nodes_ex = nest::create( "iaf_psc_alpha", NE );
  nest::NodeCollectionPTR nodes_in = nest::create( "iaf_psc_alpha", NI );

  std::vector< Dictionary > neuron_params_vec { neuron_params };
  nest::set_nc_status( nodes_ex, neuron_params_vec );
  nest::set_nc_status( nodes_in, neuron_params_vec );

  nest::NodeCollectionPTR noise = nest::create( "poisson_generator", 1 );
  Dictionary noise_params;
  noise_params[ nest::names::rate ] = p_rate;
  std::vector< Dictionary > noise_params_vec { noise_params };
  nest::set_nc_status( noise, noise_params_vec );

  nest::NodeCollectionPTR espikes = nest::create( "spike_recorder", 1 );
  nest::NodeCollectionPTR ispikes = nest::create( "spike_recorder", 1 );

  Dictionary rec_ex;
  rec_ex[ nest::names::label ] = std::string( "brunel-cpp-ex" );
  rec_ex[ nest::names::record_to ] = std::string( "ascii" );
  std::vector< Dictionary > rec_ex_vec { rec_ex };
  nest::set_nc_status( espikes, rec_ex_vec );

  Dictionary rec_in;
  rec_in[ nest::names::label ] = std::string( "brunel-cpp-in" );
  rec_in[ nest::names::record_to ] = std::string( "ascii" );
  std::vector< Dictionary > rec_in_vec { rec_in };
  nest::set_nc_status( ispikes, rec_in_vec );

  std::printf( "Connecting devices\n" );

  // --- synapse models ----------------------------------------------------
  Dictionary syn_ex_defaults;
  syn_ex_defaults[ nest::names::weight ] = J_ex;
  syn_ex_defaults[ nest::names::delay ] = delay;
  nest::copy_model( "static_synapse", "excitatory", syn_ex_defaults );

  Dictionary syn_in_defaults;
  syn_in_defaults[ nest::names::weight ] = J_in;
  syn_in_defaults[ nest::names::delay ] = delay;
  nest::copy_model( "static_synapse", "inhibitory", syn_in_defaults );

  // In Python a synapse specification may be the bare string "excitatory". At
  // this level it must always be a dictionary inside a vector, and the
  // connectivity rule must always be named: there is no all_to_all default here.
  Dictionary all_to_all;
  all_to_all[ nest::names::rule ] = std::string( "all_to_all" );

  Dictionary syn_ex;
  syn_ex[ nest::names::synapse_model ] = std::string( "excitatory" );
  const std::vector< Dictionary > syn_ex_vec { syn_ex };

  Dictionary syn_in;
  syn_in[ nest::names::synapse_model ] = std::string( "inhibitory" );
  const std::vector< Dictionary > syn_in_vec { syn_in };

  nest::connect( noise, nodes_ex, all_to_all, syn_ex_vec );
  nest::connect( noise, nodes_in, all_to_all, syn_ex_vec );

  // nodes_ex[:N_rec]. Note the indexing convention, which nest.h does not state:
  // `start` is 1-based, `stop` is an inclusive 1-based bound, and the step is
  // mandatory. Passing the natural 0-based (0, N_rec) compiles and then throws
  // nest::BadParameter("start < stop required.") at run time.
  nest::NodeCollectionPTR ex_rec = nest::slice_nc( nodes_ex, 1, N_rec, 1 );
  nest::NodeCollectionPTR in_rec = nest::slice_nc( nodes_in, 1, N_rec, 1 );

  nest::connect( ex_rec, espikes, all_to_all, syn_ex_vec );
  nest::connect( in_rec, ispikes, all_to_all, syn_ex_vec );

  std::printf( "Connecting network\n" );

  // nodes_ex + nodes_in. operator+ is declared in node_collection.h, not in
  // nest.h, so this one line is the reason the program needs a second header.
  nest::NodeCollectionPTR all_nodes = nodes_ex + nodes_in;

  Dictionary conn_ex;
  conn_ex[ nest::names::rule ] = std::string( "fixed_indegree" );
  conn_ex[ nest::names::indegree ] = CE;
  nest::connect( nodes_ex, all_nodes, conn_ex, syn_ex_vec );

  Dictionary conn_in;
  conn_in[ nest::names::rule ] = std::string( "fixed_indegree" );
  conn_in[ nest::names::indegree ] = CI;
  nest::connect( nodes_in, all_nodes, conn_in, syn_in_vec );

  // --- simulate ----------------------------------------------------------
  std::printf( "Simulating\n" );
  nest::simulate( simtime );

  // --- results -----------------------------------------------------------
  // get_nc_status() returns one dictionary for the whole collection, and a
  // per-node property arrives as an AnyVector: a vector of variants, one entry
  // per node, holding std::monostate where the node is not local. So reading a
  // single integer off a single-node collection costs a get<AnyVector>(), an
  // .at(), and a std::get<long>() on the variant. Asking for the obvious
  // std::vector<long> compiles and throws nest::TypeMismatch at run time.
  const Dictionary ex_status = nest::get_nc_status( espikes );
  const Dictionary in_status = nest::get_nc_status( ispikes );
  const long events_ex = std::get< long >( ex_status.get< AnyVector >( nest::names::n_events ).at( 0 ) );
  const long events_in = std::get< long >( in_status.get< AnyVector >( nest::names::n_events ).at( 0 ) );

  const double rate_ex = static_cast< double >( events_ex ) / simtime * 1000.0 / static_cast< double >( N_rec );
  const double rate_in = static_cast< double >( events_in ) / simtime * 1000.0 / static_cast< double >( N_rec );

  const Dictionary def_ex = nest::get_model_defaults( "excitatory" );
  const Dictionary def_in = nest::get_model_defaults( "inhibitory" );
  const long num_synapses =
    def_ex.get< long >( nest::names::num_connections ) + def_in.get< long >( nest::names::num_connections );

  // NEST instruments itself. Rather than wrap the calls in a clock of our own,
  // read the kernel's own stopwatches: time_construction_create and
  // time_construction_connect cover network build, time_simulate covers the
  // run. The per-thread timers beside them (time_update, time_deliver_spike_data
  // and the rest) are only filled in a build configured with detailed timers.
  const Dictionary kernel_final = nest::get_kernel_status();
  const double t_create = kernel_final.get< double >( nest::names::time_construction_create );
  const double t_connect = kernel_final.get< double >( nest::names::time_construction_connect );
  const double t_simulate = kernel_final.get< double >( nest::names::time_simulate );

  std::printf( "Brunel network simulation (C++, raw kernel API)\n" );
  std::printf( "Number of neurons : %ld\n", NE + NI );
  std::printf( "Number of synapses: %ld\n", num_synapses );
  std::printf( "Excitatory events : %ld\n", events_ex );
  std::printf( "Inhibitory events : %ld\n", events_in );
  std::printf( "Excitatory rate   : %.2f Hz\n", rate_ex );
  std::printf( "Inhibitory rate   : %.2f Hz\n", rate_in );
  std::printf( "Building time     : %.3f s (create %.3f, connect %.3f)\n", t_create + t_connect, t_create, t_connect );
  std::printf( "Simulation time   : %.3f s\n", t_simulate );

  nest::shutdown_nest( 0 );
  return 0;
}
