/*
 *  nest_cpp/nest.hpp
 *
 *  A drafted user-facing C++ interface to the NEST simulation kernel.
 *
 *  This is a header-only layer over `nestkernel/nest.h`. It adds no simulation
 *  behaviour of its own: every call forwards to the kernel API, and the network
 *  that results is the same network, built in the same order, drawing the same
 *  random numbers. What it adds is the part PyNEST adds on the Python side and
 *  nobody has yet added on the C++ side: a kernel that closes itself, node
 *  collections that are values, parameters written where they are used, and
 *  results that come back as the type you asked for.
 *
 *  Scope is deliberately the Brunel network and nothing else. Every entry point
 *  here exists because ../examples/brunel_alpha.cpp needs it. See
 *  ../../docs/02_design.md for what is missing and why that is the right size
 *  for a draft.
 *
 *  This header is not part of NEST and does not modify NEST.
 *
 *  Requires C++20. Link against a NEST build tree; see ../../build.sh.
 */

#ifndef NEST_CPP_NEST_HPP
#define NEST_CPP_NEST_HPP

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

namespace nestpp
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
    throw nest::KeyError( key, "Dictionary", "nestpp::get" );
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
    set( "rule", std::string( "all_to_all" ) );
  }

  ConnSpec( Params params )
    : Params( std::move( params ) )
  {
    if ( not has( "rule" ) )
    {
      set( "rule", std::string( "all_to_all" ) );
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
    spec.set( "rule", std::string( "one_to_one" ) );
    return spec;
  }

  static ConnSpec
  fixed_indegree( const long indegree )
  {
    ConnSpec spec;
    spec.set( "rule", std::string( "fixed_indegree" ) );
    spec.set( "indegree", indegree );
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
    set( "synapse_model", std::string( synapse_model ) );
  }

  SynSpec( std::string synapse_model )
  {
    set( "synapse_model", std::move( synapse_model ) );
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
      throw nest::BadParameter( "nestpp::NodeCollection::slice: negative indices are not supported" );
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

  void
  set( const Params& params ) const
  {
    // set_nc_status takes a non-const reference, so the vector must be named.
    std::vector< Dictionary > per_node { params.dict() };
    nest::set_nc_status( handle_, per_node );
  }

  Dictionary
  status() const
  {
    return nest::get_nc_status( handle_ );
  }

  template < detail::DictAlternative T >
  T
  get( const std::string& key ) const
  {
    return detail::scalar< T >( status(), key );
  }

private:
  nest::NodeCollectionPTR handle_;
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

  ~Kernel()
  {
    nest::shutdown_nest( 0 );
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
 * Simulate for @p t milliseconds of biological time.
 */
inline void
simulate( const double t )
{
  nest::simulate( t );
}

/**
 * The default parameters of a node or synapse model.
 */
inline Dictionary
model_defaults( const std::string& model )
{
  return nest::get_model_defaults( model );
}

} // namespace nestpp

#endif /* NEST_CPP_NEST_HPP */
