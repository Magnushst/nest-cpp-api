/*
 *  nest_api.h
 *
 *  A user-facing C++ interface to the NEST simulation kernel.
 *
 *  This is a header-only layer over `nestkernel/nest.h`. It adds no simulation
 *  behaviour of its own: every call forwards to the kernel API, and the network
 *  that results is the same network, built in the same order, drawing the same
 *  random numbers. What it adds is the part PyNEST adds on the Python side and
 *  nobody has yet added on the C++ side: a kernel that closes itself, node
 *  collections that are values, parameters written where they are used, and
 *  results that come back as the type you asked for.
 *
 *  Scope is what the models in this repository need, and nothing speculative.
 *  Every entry point here exists because a model directory uses it, and the
 *  check for that model pins its behaviour. See docs/02_the_api.md for what is
 *  missing and why that is the right size.
 *
 *  This header is not part of NEST and does not modify NEST.
 *
 *  Requires C++20. Link against a NEST build tree; see build.sh.
 */

#ifndef NEST_API_H
#define NEST_API_H

#include <deque>
#include <map>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "dictionary.h"
#include "exceptions.h"
#include "nest.h"
#include "nest_names.h"
#include "node_collection.h"
#include "numerics.h"
#include "parameter.h"

namespace nest
{

/**
 * The user-facing C++ interface.
 *
 * Kept in a nested namespace because the names here deliberately match the
 * kernel functions they forward to: @c nest::api::create calls @c nest::create.
 */
namespace api
{

/**
 * NEST's own dictionary key constants, re-exported.
 *
 * Every key the kernel understands is declared in `nestkernel/nest_names.h` as
 * a named constant, so `names::tau_syn_ex` is checked by the compiler where
 * `"tau_syn_ex"` is checked by nobody. Use these rather than string literals.
 */
namespace names = ::nest::names;

/**
 * NEST's own numerical constants, re-exported. In particular @c numerics::e,
 * which is the value the kernel's own models use.
 */
namespace numerics = ::numerics;

namespace detail
{

/**
 * True if @c T is one of the alternatives of the kernel's @c any_type variant.
 */
template < typename T >
concept DictAlternative = requires( const any_type& v ) { std::holds_alternative< T >( v ); };

/**
 * Read a scalar of type @c T out of a status dictionary.
 *
 * Status returned for a node collection arrives as an @c AnyVector: one variant
 * per node, holding @c std::monostate where the node is not local. A scalar
 * property of a one-node collection is therefore a one-element vector of
 * variants, and asking the kernel dictionary for the obvious @c long throws.
 * This function unwraps the three shapes the kernel actually produces.
 */
template < DictAlternative T >
T
element( const any_type& value, const std::string& key )
{
  if ( not std::holds_alternative< T >( value ) )
  {
    throw nest::TypeMismatch( pretty_typename< T >() + " for key '" + key + "'", get_typename( value ) );
  }
  return std::get< T >( value );
}

template < DictAlternative T >
T
scalar( const Dictionary& dict, const std::string& key )
{
  if ( not dict.known( key ) )
  {
    throw nest::KeyError( key, "Dictionary", "nest::api::get" );
  }

  const any_type& value = dict.at( key );

  if ( std::holds_alternative< AnyVector >( value ) )
  {
    const AnyVector& items = std::get< AnyVector >( value );
    if ( items.size() != 1 )
    {
      throw nest::DimensionMismatch( 1, static_cast< int >( items.size() ) );
    }
    return element< T >( items.front(), key );
  }

  if constexpr ( DictAlternative< std::vector< T > > )
  {
    if ( std::holds_alternative< std::vector< T > >( value ) )
    {
      const std::vector< T >& items = std::get< std::vector< T > >( value );
      if ( items.size() != 1 )
      {
        throw nest::DimensionMismatch( 1, static_cast< int >( items.size() ) );
      }
      return items.front();
    }
  }

  return element< T >( value, key );
}

} // namespace detail

/**
 * Read a scalar of type @c T out of any dictionary the kernel returns.
 */
template < detail::DictAlternative T >
T
get( const Dictionary& dict, const std::string& key )
{
  return detail::scalar< T >( dict, key );
}

/**
 * A value the kernel draws per node or per connection, rather than a constant.
 *
 * This is the kernel's @c Parameter: an object that yields a value when the
 * kernel asks it for one, using the random stream of the virtual process that
 * owns the node. Assigning one to @c V_m gives every neuron its own initial
 * membrane potential; putting one in a synapse specification gives every
 * connection its own weight or delay.
 *
 * It converts implicitly to the kernel's dictionary value type, so it can be
 * written straight into a @ref Params entry.
 */
class Parameter
{
public:
  Parameter() = default;

  explicit Parameter( nest::ParameterPTR ptr )
    : ptr_( std::move( ptr ) )
  {
  }

  const nest::ParameterPTR&
  ptr() const
  {
    return ptr_;
  }

  operator any_type() const // NOLINT: implicit by design, so it can be a Params value
  {
    return ptr_;
  }

  /**
   * Draw one value, using the rank-synchronised random stream.
   *
   * Only meaningful for a parameter that does not depend on a node or a
   * position; the kernel throws for one that does.
   */
  double
  value() const
  {
    return nest::get_value( ptr_ );
  }

  //! True if the value depends on the position of the node, as in a spatial network.
  bool
  is_spatial() const
  {
    return nest::is_spatial( ptr_ );
  }

  /**
   * A parameter that always yields @p constant.
   *
   * Written out rather than made an implicit conversion, because a @ref Params
   * entry accepts both a @c double and a @ref Parameter and the conversion
   * would make which one is meant ambiguous.
   */
  static Parameter
  constant( const double value )
  {
    return Parameter( nest::create_parameter( value ) );
  }

  Parameter
  operator*( const Parameter& rhs ) const
  {
    return Parameter( nest::multiply_parameter( ptr_, rhs.ptr_ ) );
  }

  Parameter
  operator/( const Parameter& rhs ) const
  {
    return Parameter( nest::divide_parameter( ptr_, rhs.ptr_ ) );
  }

  Parameter
  operator+( const Parameter& rhs ) const
  {
    return Parameter( nest::add_parameter( ptr_, rhs.ptr_ ) );
  }

  Parameter
  operator-( const Parameter& rhs ) const
  {
    return Parameter( nest::subtract_parameter( ptr_, rhs.ptr_ ) );
  }

  Parameter
  operator*( const double rhs ) const
  {
    return *this * constant( rhs );
  }

  Parameter
  operator/( const double rhs ) const
  {
    return *this / constant( rhs );
  }

  Parameter
  operator+( const double rhs ) const
  {
    return *this + constant( rhs );
  }

  Parameter
  operator-( const double rhs ) const
  {
    return *this - constant( rhs );
  }

private:
  nest::ParameterPTR ptr_;
};

/**
 * Distributions, named as PyNEST names them in @c nest.random.
 *
 * Each builds a kernel @c Parameter through @c create_parameter with the same
 * type name and the same specification dictionary that PyNEST passes, so the
 * kernel draws the same values in the same order.
 */
namespace random
{

inline Parameter
normal( const double mean = 0.0, const double std = 1.0 )
{
  Dictionary specs;
  specs[ "mean" ] = mean;
  specs[ "std" ] = std;
  return Parameter( nest::create_parameter( "normal", specs ) );
}

inline Parameter
lognormal( const double mean = 0.0, const double std = 1.0 )
{
  Dictionary specs;
  specs[ "mean" ] = mean;
  specs[ "std" ] = std;
  return Parameter( nest::create_parameter( "lognormal", specs ) );
}

inline Parameter
uniform( const double min = 0.0, const double max = 1.0 )
{
  Dictionary specs;
  specs[ "min" ] = min;
  specs[ "max" ] = max;
  return Parameter( nest::create_parameter( "uniform", specs ) );
}

inline Parameter
exponential( const double beta = 1.0 )
{
  Dictionary specs;
  specs[ "beta" ] = beta;
  return Parameter( nest::create_parameter( "exponential", specs ) );
}

} // namespace random

/**
 * Operations on parameters, named as PyNEST names them in @c nest.math.
 */
namespace math
{

/**
 * Redraw @p parameter until its value lies within [@p min, @p max].
 *
 * Both bounds are inclusive. The kernel gives up after 1000 redraws and throws.
 * This is how the microcircuit keeps excitatory weights positive and delays at
 * or above the resolution.
 *
 * @note @c redraw_parameter is declared in @c nestkernel/parameter.h and not in
 * @c nestkernel/nest.h, so a caller who includes only the API header cannot
 * reach it.
 */
inline Parameter
redraw( const Parameter& parameter, const double min, const double max )
{
  return Parameter( nest::redraw_parameter( parameter.ptr(), min, max ) );
}

//! The larger of @p parameter and @p other.
inline Parameter
max( const Parameter& parameter, const double other )
{
  return Parameter( nest::max_parameter( parameter.ptr(), other ) );
}

//! The smaller of @p parameter and @p other.
inline Parameter
min( const Parameter& parameter, const double other )
{
  return Parameter( nest::min_parameter( parameter.ptr(), other ) );
}

inline Parameter
exp( const Parameter& parameter )
{
  return Parameter( nest::exp_parameter( parameter.ptr() ) );
}

inline Parameter
sin( const Parameter& parameter )
{
  return Parameter( nest::sin_parameter( parameter.ptr() ) );
}

inline Parameter
cos( const Parameter& parameter )
{
  return Parameter( nest::cos_parameter( parameter.ptr() ) );
}

inline Parameter
pow( const Parameter& parameter, const double exponent )
{
  return Parameter( nest::pow_parameter( parameter.ptr(), exponent ) );
}

//! How two parameters are compared in @ref compare.
enum class Comparator : long
{
  less = 0,
  less_equal = 1,
  equal = 2,
  not_equal = 3,
  greater_equal = 4,
  greater = 5
};

/**
 * A parameter yielding 1 where the comparison holds and 0 where it does not.
 *
 * The codes are the kernel's own, which PyNEST spells as the comparison
 * operators on a parameter.
 */
inline Parameter
compare( const Parameter& lhs, const Parameter& rhs, const Comparator comparator )
{
  Dictionary d;
  d[ "comparator" ] = static_cast< long >( comparator );
  return Parameter( nest::compare_parameter( lhs.ptr(), rhs.ptr(), d ) );
}

/**
 * @p if_true where @p condition yields a non-zero value, @p if_false elsewhere.
 */
inline Parameter
conditional( const Parameter& condition, const Parameter& if_true, const Parameter& if_false )
{
  return Parameter( nest::conditional_parameter( condition.ptr(), if_true.ptr(), if_false.ptr() ) );
}

} // namespace math

/**
 * An ordered set of named parameters, written where it is used.
 *
 * Values are held in an owned map rather than in a @ref Dictionary, because
 * @ref Dictionary is a @c shared_ptr and copying one aliases it. A fresh
 * dictionary is materialised on each call to @ref dict, so passing a @ref Params
 * to two different calls cannot make them share state.
 */
class Params
{
public:
  using value_type = std::pair< const std::string, any_type >;

  Params() = default;

  Params( std::initializer_list< value_type > entries )
    : entries_( entries )
  {
  }

  Params&
  set( std::string key, any_type value )
  {
    entries_.insert_or_assign( std::move( key ), std::move( value ) );
    return *this;
  }

  bool
  has( const std::string& key ) const
  {
    return entries_.contains( key );
  }

  bool
  empty() const
  {
    return entries_.empty();
  }

  Dictionary
  dict() const
  {
    Dictionary d;
    for ( const auto& [ key, value ] : entries_ )
    {
      d[ key ] = value;
    }
    return d;
  }

private:
  std::map< std::string, any_type > entries_;
};

/**
 * A connectivity rule and its parameters.
 *
 * The kernel requires the rule to be named on every call. PyNEST defaults it to
 * @c all_to_all and so does this, which is what makes the device connections in
 * the Brunel example one line each.
 */
struct ConnSpec : Params
{
  ConnSpec()
  {
    set( names::rule, std::string( "all_to_all" ) );
  }

  ConnSpec( Params params )
    : Params( std::move( params ) )
  {
    if ( not has( names::rule ) )
    {
      set( names::rule, std::string( "all_to_all" ) );
    }
  }

  static ConnSpec
  all_to_all()
  {
    return ConnSpec {};
  }

  static ConnSpec
  one_to_one()
  {
    ConnSpec spec;
    spec.set( names::rule, std::string( "one_to_one" ) );
    return spec;
  }

  static ConnSpec
  fixed_indegree( const long indegree )
  {
    ConnSpec spec;
    spec.set( names::rule, std::string( "fixed_indegree" ) );
    spec.set( names::indegree, indegree );
    return spec;
  }

  /**
   * @p n connections drawn at random from all source-target pairs, sources and
   * targets both drawn with replacement. This is how the microcircuit realises
   * its connection probabilities.
   */
  static ConnSpec
  fixed_total_number( const long n )
  {
    ConnSpec spec;
    spec.set( names::rule, std::string( "fixed_total_number" ) );
    spec.set( names::N, n );
    return spec;
  }
};

/**
 * A synapse specification.
 *
 * Implicitly constructible from a model name, so that `connect(a, b, conn,
 * "excitatory")` works as it does in Python.
 */
struct SynSpec : Params
{
  SynSpec() = default;

  SynSpec( const char* synapse_model )
  {
    set( names::synapse_model, std::string( synapse_model ) );
  }

  SynSpec( std::string synapse_model )
  {
    set( names::synapse_model, std::move( synapse_model ) );
  }

  SynSpec( Params params )
    : Params( std::move( params ) )
  {
  }
};

/**
 * A set of nodes, as a value.
 *
 * Wraps the kernel's @c NodeCollectionPTR. Copying is cheap and shares the
 * underlying collection, which is what the kernel intends: a node collection is
 * immutable once built.
 */
class NodeCollection
{
public:
  NodeCollection() = default;

  explicit NodeCollection( nest::NodeCollectionPTR handle )
    : handle_( std::move( handle ) )
  {
  }

  const nest::NodeCollectionPTR&
  handle() const
  {
    return handle_;
  }

  size_t
  size() const
  {
    return nest::nc_size( handle_ );
  }

  /**
   * Concatenation, as `nodes_ex + nodes_in` in Python.
   */
  NodeCollection
  operator+( const NodeCollection& rhs ) const
  {
    return NodeCollection( handle_ + rhs.handle_ );
  }

  /**
   * Half-open slice with Python semantics: @p start is 0-based and @p stop is
   * exclusive.
   *
   * The kernel's @c slice_nc takes a 1-based @p start and an inclusive @p stop,
   * a convention its declaration does not state and which throws rather than
   * misbehaving when you assume otherwise. That conversion happens here, once.
   *
   * Negative indices are rejected rather than silently given a different
   * meaning from Python's.
   */
  NodeCollection
  slice( const long start, const long stop, const long step = 1 ) const
  {
    if ( start < 0 or stop < 0 )
    {
      throw nest::BadParameter( "nest::api::NodeCollection::slice: negative indices are not supported" );
    }
    return NodeCollection( nest::slice_nc( handle_, start + 1, stop, step ) );
  }

  /**
   * The first @p n nodes, as `nc[:n]` in Python.
   */
  NodeCollection
  first( const long n ) const
  {
    return slice( 0, n );
  }

  /**
   * Node @p i on its own, as `nc[i]` in Python. Still a collection, because
   * that is what every kernel call takes.
   */
  NodeCollection
  operator[]( const long i ) const
  {
    return slice( i, i + 1 );
  }

  void
  set( const Params& params ) const
  {
    // set_nc_status takes a non-const reference, so the vector must be named.
    std::vector< Dictionary > per_node { params.dict() };
    nest::set_nc_status( handle_, per_node );
  }

  /**
   * Give each node its own parameters. The number of entries must equal the
   * size of the collection, as it must in PyNEST.
   */
  void
  set( const std::vector< Params >& params ) const
  {
    if ( params.size() != size() )
    {
      throw nest::DimensionMismatch( static_cast< int >( size() ), static_cast< int >( params.size() ) );
    }
    std::vector< Dictionary > per_node;
    per_node.reserve( params.size() );
    for ( const Params& p : params )
    {
      per_node.push_back( p.dict() );
    }
    nest::set_nc_status( handle_, per_node );
  }

  Dictionary
  status() const
  {
    return nest::get_nc_status( handle_ );
  }

  /**
   * A scalar property, for a collection of one node.
   *
   * Throws @c nest::DimensionMismatch for a larger collection; use
   * @ref get_all for that.
   */
  template < detail::DictAlternative T >
  T
  get( const std::string& key ) const
  {
    return detail::scalar< T >( status(), key );
  }

  /**
   * One property of every node in the collection.
   *
   * Status for a collection comes back as an @c AnyVector, one variant per
   * node, holding @c std::monostate where the node does not live on this rank.
   * Those entries are given @p absent, since there is no value to report and
   * dropping them silently would misalign the result with the collection.
   */
  template < detail::DictAlternative T >
  std::vector< T >
  get_all( const std::string& key, const T absent = T {} ) const
  {
    const Dictionary d = status();
    if ( not d.known( key ) )
    {
      throw nest::KeyError( key, "Dictionary", "nest::api::NodeCollection::get_all" );
    }

    const any_type& value = d.at( key );
    std::vector< T > result;

    if ( std::holds_alternative< AnyVector >( value ) )
    {
      const AnyVector& items = std::get< AnyVector >( value );
      result.reserve( items.size() );
      for ( const any_type& item : items )
      {
        result.push_back( std::holds_alternative< std::monostate >( item ) ? absent : detail::element< T >( item, key ) );
      }
      return result;
    }

    if constexpr ( detail::DictAlternative< std::vector< T > > )
    {
      if ( std::holds_alternative< std::vector< T > >( value ) )
      {
        return std::get< std::vector< T > >( value );
      }
    }

    result.push_back( detail::element< T >( value, key ) );
    return result;
  }

  //! The node IDs, in order.
  std::vector< size_t >
  ids() const
  {
    return nest::node_collection_to_array( handle_, "all" );
  }

  //! True if @p node_id is one of these nodes.
  bool
  contains( const size_t node_id ) const
  {
    return nest::contains( handle_, node_id );
  }

  /**
   * The position of @p node_id in the collection, or -1 if it is not in it.
   */
  long
  index_of( const size_t node_id ) const
  {
    return nest::find( handle_, node_id );
  }

  //! Two collections are equal if they hold the same nodes in the same order.
  bool
  operator==( const NodeCollection& rhs ) const
  {
    return nest::equal( handle_, rhs.handle_ );
  }

  //! The collection as NEST prints it, for diagnostics.
  std::string
  to_string() const
  {
    return nest::pprint_to_string( handle_ );
  }

private:
  nest::NodeCollectionPTR handle_;
};

/**
 * A set of connections, as returned by @ref get_connections.
 *
 * The kernel identifies a connection by source, target thread, synapse model
 * and port rather than by an object, and hands back a @c std::deque of those.
 * This wraps the deque so that reading and writing their properties, and
 * removing them, does not require the caller to carry the container type
 * around.
 */
class ConnectionCollection
{
public:
  ConnectionCollection() = default;

  explicit ConnectionCollection( std::deque< nest::ConnectionID > connections )
    : connections_( std::move( connections ) )
  {
  }

  const std::deque< nest::ConnectionID >&
  handle() const
  {
    return connections_;
  }

  size_t
  size() const
  {
    return connections_.size();
  }

  bool
  empty() const
  {
    return connections_.empty();
  }

  //! One status dictionary per connection, in the order the kernel returned them.
  std::vector< Dictionary >
  status() const
  {
    return nest::get_connection_status( connections_ );
  }

  //! One property of every connection.
  template < detail::DictAlternative T >
  std::vector< T >
  get_all( const std::string& key ) const
  {
    std::vector< T > result;
    result.reserve( connections_.size() );
    for ( const Dictionary& d : status() )
    {
      result.push_back( detail::scalar< T >( d, key ) );
    }
    return result;
  }

  //! Give every connection the same properties.
  void
  set( const Params& params ) const
  {
    nest::set_connection_status( connections_, params.dict() );
  }

  //! Remove these connections from the network.
  void
  disconnect() const
  {
    nest::disconnect( connections_ );
  }

private:
  std::deque< nest::ConnectionID > connections_;
};

/**
 * The simulation kernel, as a scoped resource.
 *
 * Construction brings the kernel and MPI up; destruction takes them down. There
 * must be at most one alive at a time, which is why it is neither copyable nor
 * movable. Holding it in @c main is what makes every early return safe.
 */
class Kernel
{
public:
  explicit Kernel( std::vector< std::string > args = { "nest" } )
    : args_( std::move( args ) )
  {
    if ( args_.empty() )
    {
      args_.emplace_back( "nest" );
    }

    argv_.reserve( args_.size() + 1 );
    for ( std::string& arg : args_ )
    {
      argv_.push_back( arg.data() );
    }
    argv_.push_back( nullptr ); // OpenMPI requires the null terminator

    argc_ = static_cast< int >( args_.size() );
    char** argv = argv_.data();
    nest::init_nest( &argc_, &argv );
  }

  /**
   * Shut the kernel and MPI down with an explicit exit code.
   *
   * Call this from @c main when the program has an exit status worth reporting.
   * Calling it more than once, or after the destructor has run, is not allowed;
   * the destructor checks and does nothing if this has already run.
   */
  void
  shutdown( const int exitcode )
  {
    if ( not down_ )
    {
      down_ = true;
      nest::shutdown_nest( exitcode );
    }
  }

  /**
   * Backstop shutdown.
   *
   * Reached on ordinary scope exit and during stack unwinding alike, which is
   * the point of the type. It reports success, because a destructor has no way
   * to know otherwise; a program that cares should call @ref shutdown itself.
   * It swallows any exception, because throwing from a destructor during
   * unwinding calls std::terminate.
   */
  ~Kernel()
  {
    try
    {
      shutdown( 0 );
    }
    catch ( ... ) // NOLINT: a destructor must not propagate
    {
    }
  }

  Kernel( const Kernel& ) = delete;
  Kernel& operator=( const Kernel& ) = delete;
  Kernel( Kernel&& ) = delete;
  Kernel& operator=( Kernel&& ) = delete;

  void
  reset() const
  {
    nest::reset_kernel();
  }

  void
  set( const Params& params ) const
  {
    nest::set_kernel_status( params.dict() );
  }

  Dictionary
  status() const
  {
    return nest::get_kernel_status();
  }

private:
  std::vector< std::string > args_;
  std::vector< char* > argv_;
  int argc_ { 0 };
  bool down_ { false };
};

/**
 * Create @p n nodes of @p model and give them @p params.
 *
 * The kernel's @c create takes no parameters, so this is two calls. Doing them
 * together is the difference between the network parameters appearing beside the
 * population they belong to and appearing three statements later.
 */
inline NodeCollection
create( const std::string& model, const long n = 1, const Params& params = Params {} )
{
  NodeCollection nodes { nest::create( model, static_cast< size_t >( n ) ) };
  if ( not params.empty() )
  {
    nodes.set( params );
  }
  return nodes;
}

/**
 * Derive a new synapse model from an existing one, with new defaults.
 */
inline void
copy_model( const std::string& from, const std::string& to, const Params& params = Params {} )
{
  nest::copy_model( from, to, params.dict() );
}

/**
 * Connect @p pre to @p post.
 */
inline void
connect( const NodeCollection& pre,
  const NodeCollection& post,
  const ConnSpec& conn = ConnSpec {},
  const SynSpec& syn = SynSpec {} )
{
  const std::vector< Dictionary > syn_specs { syn.dict() };
  nest::connect( pre.handle(), post.handle(), conn.dict(), syn_specs );
}

/**
 * Remove the connections from @p pre to @p post that match @p conn and @p syn.
 *
 * The rule must be @c one_to_one or @c all_to_all, as it must in PyNEST.
 */
inline void
disconnect( const NodeCollection& pre,
  const NodeCollection& post,
  const ConnSpec& conn = ConnSpec {},
  const SynSpec& syn = SynSpec {} )
{
  const std::vector< Dictionary > syn_specs { syn.dict() };
  nest::disconnect( pre.handle(), post.handle(), conn.dict(), syn_specs );
}

/**
 * The connections matching @p query.
 *
 * The query is the one PyNEST's @c GetConnections takes: @c source and
 * @c target as node collections, @c synapse_model as a name, and
 * @c synapse_label as a long. An empty query returns every connection in the
 * network, which on a large network is a great many.
 */
inline ConnectionCollection
get_connections( const Params& query = Params {} )
{
  return ConnectionCollection( nest::get_connections( query.dict() ) );
}

/**
 * A node collection built from node IDs directly.
 *
 * The IDs must be sorted and free of duplicates; the kernel checks.
 */
inline NodeCollection
node_collection( const std::vector< size_t >& node_ids )
{
  return NodeCollection( nest::make_nodecollection( node_ids ) );
}

/**
 * Every node matching @p query, as PyNEST's @c GetNodes does.
 *
 * With @p local_only, only the nodes on this MPI rank. An empty query returns
 * every node in the network.
 */
inline NodeCollection
get_nodes( const Params& query = Params {}, const bool local_only = false )
{
  return NodeCollection( nest::get_nodes( query.dict(), local_only ) );
}

/**
 * The value of @p parameter for every node in @p nodes.
 */
inline std::vector< double >
apply( const Parameter& parameter, const NodeCollection& nodes )
{
  return nest::apply( parameter.ptr(), nodes.handle() );
}

/**
 * Simulate for @p t milliseconds of biological time.
 */
inline void
simulate( const double t )
{
  nest::simulate( t );
}

/**
 * Build the connection infrastructure and open the recording files.
 *
 * @c simulate does this itself. Calling it explicitly separates the cost of
 * building the presynaptic side of the connections from the cost of propagating
 * the network state, which is why the microcircuit does so at the end of its
 * connection phase. Each @ref prepare must be matched by a @ref cleanup.
 */
inline void
prepare()
{
  nest::prepare();
}

/**
 * Simulate for @p t milliseconds between a @ref prepare and a @ref cleanup.
 */
inline void
run( const double t )
{
  nest::run( t );
}

/**
 * Close what @ref prepare opened.
 */
inline void
cleanup()
{
  nest::cleanup();
}

/**
 * The default parameters of a node or synapse model.
 */
inline Dictionary
model_defaults( const std::string& model )
{
  return nest::get_model_defaults( model );
}

/**
 * Change the default parameters of a node or synapse model.
 *
 * Affects nodes created after the call, not nodes that already exist.
 */
inline void
set_model_defaults( const std::string& model, const Params& params )
{
  nest::set_model_defaults( model, params.dict() );
}

/**
 * Load an external module of models, as PyNEST's @c Install does.
 */
inline void
install_module( const std::string& module_name )
{
  nest::install_module( module_name );
}

/**
 * Bring every MPI rank to the same point.
 *
 * Only needed when ranks have done different amounts of work outside the
 * kernel; the simulation synchronises itself.
 */
inline void
synchronize()
{
  nest::synchronize();
}

/**
 * This process's MPI rank, and how many there are.
 *
 * A program built on this interface is rank agnostic: every rank runs the same
 * code and the kernel distributes the nodes. These are for deciding which rank
 * prints, and for nothing else.
 */
inline long
rank()
{
  return get< long >( nest::get_kernel_status(), names::mpi_rank );
}

inline long
num_processes()
{
  return get< long >( nest::get_kernel_status(), names::num_processes );
}

//! Ranks times threads, which is how many random streams the network has.
inline long
num_virtual_processes()
{
  return get< long >( nest::get_kernel_status(), names::total_num_virtual_procs );
}

//! The nodes in the network, as NEST prints them, for diagnostics.
inline std::string
print_nodes()
{
  return nest::print_nodes_to_string();
}

} // namespace api
} // namespace nest

#endif /* NEST_API_H */
