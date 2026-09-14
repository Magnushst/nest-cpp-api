/*
 *  api_test.cpp
 *
 *  Exercises every entry point of nest_cpp/nest.hpp against a running kernel.
 *
 *  The two model directories check that the interface builds the right network.
 *  This checks the interface itself: that each call reaches the kernel, that the
 *  conversions in both directions are right, and that the failures a caller can
 *  provoke arrive as the NEST exception they should be rather than as a crash or
 *  a wrong answer.
 *
 *  It runs in a second and needs no data files. Failures print the expression
 *  that failed and the program exits non-zero.
 *
 *  This file is not part of NEST and does not modify NEST.
 */

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "nest_cpp/nest.hpp"

namespace
{

namespace names = nestpp::names;

int failures = 0;
int checks = 0;

void
ok( const bool condition, const std::string& what )
{
  ++checks;
  if ( not condition )
  {
    ++failures;
    std::printf( "  FAIL  %s\n", what.c_str() );
  }
}

void
close_to( const double got, const double expected, const std::string& what, const double tol = 1e-12 )
{
  ok( std::abs( got - expected ) <= tol, what + " (got " + std::to_string( got ) + ")" );
}

/**
 * Run @p body and report whether it threw the expected NEST exception.
 */
template < typename Exception, typename F >
void
throws( F&& body, const std::string& what )
{
  ++checks;
  try
  {
    body();
  }
  catch ( const Exception& )
  {
    return;
  }
  catch ( const std::exception& e )
  {
    ++failures;
    std::printf( "  FAIL  %s: threw the wrong type (%s)\n", what.c_str(), e.what() );
    return;
  }
  ++failures;
  std::printf( "  FAIL  %s: did not throw\n", what.c_str() );
}

} // namespace

int
main( int argc, char* argv[] )
{
  ( void ) argc;
  ( void ) argv;
  nestpp::Kernel kernel;
  try
  {
  kernel.reset();
  kernel.set( { { names::resolution, 0.1 }, { names::print_time, false }, { names::rng_seed, 12345L } } );

  std::printf( "== kernel\n" );
  close_to( nestpp::get< double >( kernel.status(), names::resolution ), 0.1, "resolution round trips" );
  ok( nestpp::get< long >( kernel.status(), names::rng_seed ) == 12345, "rng_seed round trips" );
  ok( nestpp::num_processes() >= 1, "num_processes is at least one" );
  ok( nestpp::rank() >= 0 and nestpp::rank() < nestpp::num_processes(), "rank is within range" );
  ok( nestpp::num_virtual_processes() >= nestpp::num_processes(), "virtual processes cover the ranks" );

  std::printf( "== node collections\n" );
  const nestpp::Params neuron_params { { names::V_m, -60.0 }, { names::tau_m, 12.0 } };
  const auto neurons = nestpp::create( "iaf_psc_alpha", 10, neuron_params );
  ok( neurons.size() == 10, "create returns the requested number of nodes" );
  close_to( neurons[ 0 ].get< double >( names::V_m ), -60.0, "create applies its parameters" );
  close_to( neurons[ 9 ].get< double >( names::tau_m ), 12.0, "parameters reach the last node too" );

  const std::vector< size_t > ids = neurons.ids();
  ok( ids.size() == 10 and ids.front() == 1, "ids are the node IDs, counted from one" );
  ok( neurons.contains( ids[ 3 ] ), "contains finds a member" );
  ok( not neurons.contains( 10000 ), "contains rejects a non-member" );
  ok( neurons.index_of( ids[ 3 ] ) == 3, "index_of gives the position" );
  ok( neurons.index_of( 10000 ) == -1, "index_of gives -1 for a non-member" );
  ok( not neurons.to_string().empty(), "to_string produces something" );

  // Python semantics: nc[2:5] is three nodes starting at index 2, nc[:3] is the
  // first three. The kernel's own slice is 1-based and inclusive.
  const auto middle = neurons.slice( 2, 5 );
  ok( middle.size() == 3, "slice(2, 5) holds three nodes" );
  ok( middle.ids().front() == ids[ 2 ], "slice starts at the given index" );
  ok( neurons.first( 3 ).size() == 3, "first(3) holds three nodes" );
  ok( neurons[ 4 ].size() == 1, "indexing gives one node" );
  ok( neurons[ 4 ].ids().front() == ids[ 4 ], "indexing gives the right node" );
  ok( ( neurons.first( 3 ) + neurons.slice( 3, 10 ) ) == neurons, "concatenation reassembles the collection" );
  throws< nest::BadParameter >( [ & ] { neurons.slice( -1, 3 ); }, "a negative slice index" );

  const auto all_v_m = neurons.get_all< double >( names::V_m );
  ok( all_v_m.size() == 10, "get_all returns one value per node" );
  ok( all_v_m.front() == -60.0 and all_v_m.back() == -60.0, "get_all reads the values" );
  throws< nest::DimensionMismatch >( [ & ] { neurons.get< double >( names::V_m ); }, "a scalar read of many nodes" );
  throws< nest::KeyError >( [ & ] { neurons[ 0 ].get< double >( "no_such_key" ); }, "an unknown key" );
  throws< nest::TypeMismatch >( [ & ] { neurons[ 0 ].get< long >( names::V_m ); }, "the wrong type" );

  // Per-node parameters, as the microcircuit gives its Poisson generators.
  std::vector< nestpp::Params > per_node;
  for ( int i = 0; i < 10; ++i )
  {
    per_node.push_back( { { names::V_m, -70.0 + i } } );
  }
  neurons.set( per_node );
  const auto stepped = neurons.get_all< double >( names::V_m );
  ok( stepped.front() == -70.0 and stepped.back() == -61.0, "per-node parameters land on the right nodes" );
  throws< nest::DimensionMismatch >(
    [ & ] { neurons.set( std::vector< nestpp::Params > { { { names::V_m, -70.0 } }, { { names::V_m, -70.0 } } } ); },
    "a per-node vector of the wrong length" );

  const auto rebuilt = nestpp::node_collection( ids );
  ok( rebuilt == neurons, "a collection rebuilt from IDs equals the original" );
  ok( nestpp::get_nodes().size() >= neurons.size(), "get_nodes sees at least these nodes" );

  std::printf( "== parameters\n" );
  const auto two = nestpp::Parameter::constant( 2.0 );
  const auto three = nestpp::Parameter::constant( 3.0 );
  close_to( ( two * three + 1.0 ).value(), 7.0, "parameter arithmetic" );
  close_to( ( three - two ).value(), 1.0, "parameter subtraction" );
  close_to( ( three / two ).value(), 1.5, "parameter division" );
  close_to( nestpp::math::max( two, 5.0 ).value(), 5.0, "max" );
  close_to( nestpp::math::min( two, 5.0 ).value(), 2.0, "min" );
  close_to( nestpp::math::pow( two, 3.0 ).value(), 8.0, "pow" );
  close_to( nestpp::math::exp( nestpp::Parameter::constant( 0.0 ) ).value(), 1.0, "exp" );
  close_to( nestpp::math::sin( nestpp::Parameter::constant( 0.0 ) ).value(), 0.0, "sin" );
  close_to( nestpp::math::cos( nestpp::Parameter::constant( 0.0 ) ).value(), 1.0, "cos" );
  close_to( nestpp::math::compare( two, three, nestpp::math::Comparator::less ).value(), 1.0, "compare, true case" );
  close_to( nestpp::math::compare( three, two, nestpp::math::Comparator::less ).value(), 0.0, "compare, false case" );
  close_to( nestpp::math::conditional( nestpp::math::compare( two, three, nestpp::math::Comparator::less ), two, three )
              .value(),
    2.0,
    "conditional" );
  ok( not two.is_spatial(), "a constant is not spatial" );

  // A distribution, drawn per node. The bounds are what redraw guarantees; the
  // mean is not checked, because ten draws say nothing about it.
  const auto bounded = nestpp::math::redraw( nestpp::random::normal( 0.0, 10.0 ), -1.0, 1.0 );
  const std::vector< double > drawn = nestpp::apply( bounded, neurons );
  ok( drawn.size() == 10, "apply gives one value per node" );
  bool in_bounds = true;
  for ( const double v : drawn )
  {
    in_bounds = in_bounds and v >= -1.0 and v <= 1.0;
  }
  ok( in_bounds, "redraw keeps every value inside its bounds" );

  neurons.set( { { names::V_m, nestpp::random::normal( -60.0, 5.0 ) } } );
  const auto randomised = neurons.get_all< double >( names::V_m );
  ok( randomised[ 0 ] != randomised[ 1 ], "a distributed parameter gives each node its own value" );

  std::printf( "== connections\n" );
  nestpp::copy_model( "static_synapse", "test_synapse", { { names::weight, 2.5 }, { names::delay, 1.0 } } );
  close_to(
    nestpp::get< double >( nestpp::model_defaults( "test_synapse" ), names::weight ), 2.5, "copy_model sets defaults" );
  nestpp::set_model_defaults( "test_synapse", { { names::weight, 3.5 } } );
  close_to( nestpp::get< double >( nestpp::model_defaults( "test_synapse" ), names::weight ),
    3.5,
    "set_model_defaults changes them" );

  const auto sources = neurons.first( 2 );
  const auto targets = neurons.slice( 2, 6 );
  nestpp::connect( sources, targets, nestpp::ConnSpec::all_to_all(), "test_synapse" );
  auto conns = nestpp::get_connections( { { names::source, sources.handle() } } );
  ok( conns.size() == 8, "all_to_all makes sources times targets connections" );
  const auto weights = conns.get_all< double >( names::weight );
  ok( weights.size() == 8 and weights.front() == 3.5, "connection status reads back the weight" );
  conns.set( { { names::weight, 4.5 } } );
  close_to( nestpp::get_connections( { { names::source, sources.handle() } } ).get_all< double >( names::weight ).front(),
    4.5,
    "connection status can be written" );

  // one_to_one requires the two collections to have the same size, as it does
  // in PyNEST.
  const auto pair_of_targets = targets.first( 2 );
  nestpp::connect( pair_of_targets, sources, nestpp::ConnSpec::one_to_one(), "test_synapse" );
  ok( nestpp::get_connections( { { names::source, pair_of_targets.handle() } } ).size() == 2,
    "one_to_one connects the collections node by node" );
  throws< nest::DimensionMismatch >(
    [ & ] { nestpp::connect( sources, targets, nestpp::ConnSpec::one_to_one(), "test_synapse" ); },
    "one_to_one between collections of different sizes" );

  nestpp::connect( sources, targets, nestpp::ConnSpec::fixed_indegree( 2 ), "test_synapse" );
  ok( nestpp::get_connections( { { names::source, sources.handle() } } ).size() == 8 + 4 * 2,
    "fixed_indegree makes indegree times targets connections" );

  nestpp::connect( sources, targets, nestpp::ConnSpec::fixed_total_number( 5 ), "test_synapse" );
  ok( nestpp::get_connections( { { names::source, sources.handle() } } ).size() == 8 + 4 * 2 + 5,
    "fixed_total_number makes exactly N connections" );

  const size_t before = nestpp::get_connections().size();
  nestpp::get_connections( { { names::source, pair_of_targets.handle() } } ).disconnect();
  ok( nestpp::get_connections().size() == before - 2, "disconnecting removes exactly those connections" );

  std::printf( "== simulation\n" );
  const auto generator = nestpp::create( "poisson_generator", 1, { { names::rate, 1000.0 } } );
  const auto recorder = nestpp::create( "spike_recorder" );
  nestpp::connect( generator, neurons, nestpp::ConnSpec::all_to_all(), "test_synapse" );
  nestpp::connect( neurons, recorder );

  nestpp::simulate( 20.0 );
  const double after_simulate = nestpp::get< double >( kernel.status(), names::biological_time );
  close_to( after_simulate, 20.0, "simulate advances biological time" );

  nestpp::prepare();
  nestpp::run( 10.0 );
  nestpp::run( 10.0 );
  nestpp::cleanup();
  close_to( nestpp::get< double >( kernel.status(), names::biological_time ), 40.0, "run continues where simulate left off" );
  ok( recorder.get< long >( names::n_events ) > 0, "the network spiked and the recorder counted it" );

  ok( not nestpp::print_nodes().empty(), "print_nodes produces something" );

  // Two calls given the same Params must not interfere: Dictionary is a shared
  // pointer, and the kernel marks entries as accessed while reading them.
  const nestpp::Params shared { { names::V_m, -55.0 } };
  const auto first_use = nestpp::create( "iaf_psc_alpha", 1, shared );
  const auto second_use = nestpp::create( "iaf_psc_alpha", 1, shared );
  close_to( first_use.get< double >( names::V_m ), -55.0, "a Params used once works" );
  close_to( second_use.get< double >( names::V_m ), -55.0, "the same Params used again works" );

  }
  catch ( const std::exception& e )
  {
    ++failures;
    std::printf( "  FAIL  an unexpected exception escaped: %s\n", e.what() );
  }

  std::printf( "\n%d checks, %d failures\n", checks, failures );
  kernel.shutdown( failures == 0 ? 0 : 1 );
  return failures == 0 ? 0 : 1;
}
