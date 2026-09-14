/*
 *  microcircuit_params.hpp
 *
 *  Parameters of the Potjans and Diesmann (2014) cortical microcircuit, and the
 *  quantities the model derives from them before it creates a single node: the
 *  scaled population sizes, the synapse count for every pair of populations, the
 *  external indegrees, the synaptic weights and the compensating DC current.
 *
 *  Every value and every formula is taken from the PyNEST microcircuit example
 *  vendored in reference/ (see reference/PROVENANCE.md). The arithmetic is
 *  reproduced operation by operation rather than simplified, because the results
 *  are rounded to integers that then decide how many random numbers the kernel
 *  draws. A difference of one synapse changes every spike that follows.
 *
 *  Two details of that arithmetic are easy to get wrong in C++:
 *
 *  - `np.round` rounds halves to even. `std::round` rounds them away from zero.
 *    `std::nearbyint` under the default rounding mode is the one that matches.
 *  - `np.sum` over the last axis of a contiguous array of eight elements is not
 *    a left to right sum. NumPy accumulates it as a balanced tree, which gives a
 *    different last bit. See `sum8` below.
 *
 *  This header needs no NEST headers: it is arithmetic only, so it can be
 *  compared against the Python reference without a kernel.
 */

#ifndef NEST_CPP_PD14_MICROCIRCUIT_PARAMS_HPP
#define NEST_CPP_PD14_MICROCIRCUIT_PARAMS_HPP

#include <array>
#include <cmath>
#include <string>

namespace pd14
{

constexpr size_t NUM_POPS = 8;

template < typename T >
using PopArray = std::array< T, NUM_POPS >;
template < typename T >
using PopMatrix = std::array< std::array< T, NUM_POPS >, NUM_POPS >;

//! Names of the populations, in creation order.
const PopArray< std::string > POPULATIONS { "L23E", "L23I", "L4E", "L4I", "L5E", "L5I", "L6E", "L6I" };

//! Number of neurons per population in the unscaled model.
constexpr PopArray< double > FULL_NUM_NEURONS { 20683, 5834, 21915, 5479, 4850, 1065, 14395, 2948 };

//! Firing rates of the unscaled model, used to compensate for scaled indegrees.
constexpr PopArray< double > FULL_MEAN_RATES { 0.903, 2.965, 4.414, 5.876, 7.569, 8.633, 1.105, 7.829 };

//! Indegree of the external Poisson drive, per population.
constexpr PopArray< double > K_EXT { 1600, 1500, 2100, 1900, 2000, 1900, 2900, 2100 };

//! Connection probabilities; first index is the target population, second the source.
constexpr PopMatrix< double > CONN_PROBS { {
  { 0.1009, 0.1689, 0.0437, 0.0818, 0.0323, 0.0, 0.0076, 0.0 },
  { 0.1346, 0.1371, 0.0316, 0.0515, 0.0755, 0.0, 0.0042, 0.0 },
  { 0.0077, 0.0059, 0.0497, 0.135, 0.0067, 0.0003, 0.0453, 0.0 },
  { 0.0691, 0.0029, 0.0794, 0.1597, 0.0033, 0.0, 0.1057, 0.0 },
  { 0.1004, 0.0622, 0.0505, 0.0057, 0.0831, 0.3726, 0.0204, 0.0 },
  { 0.0548, 0.0269, 0.0257, 0.0022, 0.06, 0.3158, 0.0086, 0.0 },
  { 0.0156, 0.0066, 0.0211, 0.0166, 0.0572, 0.0197, 0.0396, 0.2252 },
  { 0.0364, 0.001, 0.0034, 0.0005, 0.0277, 0.008, 0.0658, 0.1443 },
} };

//! Mean amplitude of an excitatory postsynaptic potential, in mV.
constexpr double PSP_EXC_MEAN = 0.15;
//! Standard deviation of a weight, relative to its mean.
constexpr double WEIGHT_REL_STD = 0.1;
//! Inhibitory weight relative to the excitatory one.
constexpr double G = -4.0;
//! Mean delays, in ms.
constexpr double DELAY_EXC_MEAN = 1.5;
constexpr double DELAY_INH_MEAN = 0.75;
//! Standard deviation of a delay, relative to its mean.
constexpr double DELAY_REL_STD = 0.5;
//! Rate of one external Poisson generator, in spikes/s, and its delay in ms.
constexpr double BG_RATE = 8.0;
constexpr double DELAY_POISSON = 1.5;

//! Neuron parameters. V0 is the distribution the initial membrane potential is drawn from.
constexpr double E_L = -65.0;
constexpr double V_TH = -50.0;
constexpr double V_RESET = -65.0;
constexpr double C_M = 250.0;
constexpr double TAU_M = 10.0;
constexpr double TAU_SYN = 0.5;
constexpr double T_REF = 2.0;
constexpr PopArray< double > V0_MEAN { -68.28, -63.16, -63.33, -63.45, -63.11, -61.66, -66.72, -61.43 };
constexpr PopArray< double > V0_STD { 5.36, 4.57, 4.74, 4.94, 4.94, 4.55, 5.46, 4.48 };

/**
 * Sum eight doubles the way `np.sum` does along the last axis of a contiguous array.
 *
 * NumPy's pairwise summation takes the eight elements as eight partial
 * accumulators and combines them as a balanced tree. A left to right sum gives a
 * different result in the last bit, which is visible in the compensating DC
 * current.
 */
inline double
sum8( const PopArray< double >& a )
{
  return ( ( a[ 0 ] + a[ 1 ] ) + ( a[ 2 ] + a[ 3 ] ) ) + ( ( a[ 4 ] + a[ 5 ] ) + ( a[ 6 ] + a[ 7 ] ) );
}

/**
 * Factor converting a postsynaptic potential in mV to a postsynaptic current in pA.
 *
 * Identical to `helpers.postsynaptic_potential_to_current`, which follows Eq. 5
 * of Hanuschkin et al. (2010).
 */
inline double
psc_over_psp( const double c_m, const double tau_m, const double tau_syn )
{
  const double sub = 1.0 / ( tau_syn - tau_m );
  const double pre = tau_m * tau_syn / c_m * sub;
  const double frac = std::pow( tau_m / tau_syn, sub );
  return 1.0 / ( pre * ( std::pow( frac, tau_m ) - std::pow( frac, tau_syn ) ) );
}

//! Everything the model derives from the parameters above.
struct Derived
{
  PopArray< long > num_neurons;     //!< population sizes after N_scaling
  PopMatrix< long > num_synapses;   //!< synapses per (target, source) pair after scaling
  PopArray< long > ext_indegrees;   //!< external indegree after K_scaling
  PopMatrix< double > weight_mean;  //!< mean synaptic weight in pA, per (target, source)
  double weight_ext;                //!< weight of an external connection in pA
  PopArray< double > dc_amp;        //!< DC current compensating the scaled indegrees, in pA
};

/**
 * Derive the network from the parameters, for the given scaling factors.
 *
 * Mirrors `Network.__derive_parameters` with `poisson_input` true and no
 * thalamic input, which is the configuration of the upstream defaults.
 */
inline Derived
derive( const double n_scaling, const double k_scaling )
{
  Derived d {};

  // Total synapse numbers of the unscaled model, from the connection
  // probabilities: helpers.num_synapses_from_conn_probs.
  PopMatrix< double > full_num_synapses {};
  for ( size_t i = 0; i < NUM_POPS; ++i )
  {
    for ( size_t j = 0; j < NUM_POPS; ++j )
    {
      const double prod = FULL_NUM_NEURONS[ i ] * FULL_NUM_NEURONS[ j ];
      full_num_synapses[ i ][ j ] = std::log( 1.0 - CONN_PROBS[ i ][ j ] ) / std::log( ( prod - 1.0 ) / prod );
    }
  }

  for ( size_t i = 0; i < NUM_POPS; ++i )
  {
    d.num_neurons[ i ] = static_cast< long >( std::nearbyint( FULL_NUM_NEURONS[ i ] * n_scaling ) );
    d.ext_indegrees[ i ] = static_cast< long >( std::nearbyint( K_EXT[ i ] * k_scaling ) );
    for ( size_t j = 0; j < NUM_POPS; ++j )
    {
      d.num_synapses[ i ][ j ] =
        static_cast< long >( std::nearbyint( full_num_synapses[ i ][ j ] * n_scaling * k_scaling ) );
    }
  }

  // Mean postsynaptic potentials, converted to currents. Columns alternate
  // excitatory and inhibitory sources; the L4E to L23E potential is doubled.
  const double conversion = psc_over_psp( C_M, TAU_M, TAU_SYN );
  PopMatrix< double > psc_mean {};
  for ( size_t i = 0; i < NUM_POPS; ++i )
  {
    for ( size_t j = 0; j < NUM_POPS; ++j )
    {
      psc_mean[ i ][ j ] = ( j % 2 == 0 ? PSP_EXC_MEAN : PSP_EXC_MEAN * G ) * conversion;
    }
  }
  psc_mean[ 0 ][ 2 ] = 2.0 * PSP_EXC_MEAN * conversion;
  double psc_ext = PSP_EXC_MEAN * conversion;

  // With Poisson input there is no DC input to compensate for a missing drive.
  PopArray< double > dc_amp {};
  dc_amp.fill( 0.0 );

  // Scaling the indegrees down thins the input, so the weights are scaled up by
  // the square root and the lost mean input is added back as DC current:
  // helpers.adjust_weights_and_input_to_synapse_scaling.
  if ( k_scaling != 1.0 )
  {
    const double root_k = std::sqrt( k_scaling );
    for ( size_t i = 0; i < NUM_POPS; ++i )
    {
      PopArray< double > terms {};
      for ( size_t j = 0; j < NUM_POPS; ++j )
      {
        const double indegree = full_num_synapses[ i ][ j ] / FULL_NUM_NEURONS[ i ];
        terms[ j ] = psc_mean[ i ][ j ] * indegree * FULL_MEAN_RATES[ j ];
        psc_mean[ i ][ j ] /= root_k;
      }
      const double input_rec = sum8( terms );
      const double input_ext = psc_ext * K_EXT[ i ] * BG_RATE;
      dc_amp[ i ] += 0.001 * TAU_SYN * ( 1.0 - root_k ) * input_rec;
      dc_amp[ i ] += 0.001 * TAU_SYN * ( 1.0 - root_k ) * input_ext;
    }
    psc_ext /= root_k;
  }

  d.weight_mean = psc_mean;
  d.weight_ext = psc_ext;
  d.dc_amp = dc_amp;
  return d;
}

} // namespace pd14

#endif /* NEST_CPP_PD14_MICROCIRCUIT_PARAMS_HPP */
