#ifndef SKYNET_UPPER_ASYNCHRONOUS_JACOBI_HPP
#define SKYNET_UPPER_ASYNCHRONOUS_JACOBI_HPP

#include "skywing_core/job.hpp"
#include "skywing_core/manager.hpp"
#include <random>
#include <vector>
#include <cmath>

namespace skywing
{

/** 
 * @brief A Processor used in an IterativeMethod for solving square linear systems of equations Ax=b.
 *
 * This processor applies the Jacobi update method. Convergence is
 * guaranteed when the matrix A is square and strictly diagonally
 * dominant. This class stores the full solution vector x_iter at each
 * machine as well as the partition of x_iter corresponding to the
 * partition Ax=b that each machine is responsible for updating.
 * 
 * This processor supports overlapping computations, nonuniform
 * partitioning, and non-sequential partitioning of the linear
 * system. This is why the row_index for each component of x must be
 * sent along with every method for proper processing.
 * 
 * @tparam E The matrix and vector element type to be used. Most often
 * a scalar type such as @p double, but could also be more complex
 * types. For example, if E is a matrix type with proper operator
 * overloading, this processor can be used to implement a block Jacobi
 * method.
*/
template<typename E = double>
class DLMCProcessor
{
public:
  using element_t = E;
  using ValueType = std::vector<element_t>;
  using ValueTag = skywing::PublishTag<ValueType>;

  /**
   */
  DLMCProcessor(
    std::size_t epsilon,
    std::size_t num_nbrs,
    std::size_t num_iter,
    std::vector<size_t> local_partition
  )
    : 
    epsilon_(epsilon), T_(1), sigma_(10),
    theta_(num_iter, 0.0),
    gradient_(num_iter, 0.0),
    local_partition_(local_partition),
    mailbox_(num_nbrs, 0.0),
    number_of_updated_components_(num_nbrs),
    publish_values_(2 * number_of_updated_components_, 0.0)
  {
    dlmc_computation();
  }

  /** @brief Initialize values to communicate.
   *
   *  Must send an index along with each value, so vector length is
   *  twice the number of components updated.
   */
  ValueType get_init_publish_values()
  { return std::vector<element_t>(2 * number_of_updated_components_, 0.0); }

  /** @brief Process an update with a set of new neighbor values.
   *
   * @param nbr_tags The tags of the updated values. Each tag is an element of this->tags_.
   * @param nbr_values The new values from the neighbors.
   * @param caller The iterative wrapper calling this method.
   */
  template<typename NbrDataHandler, typename IterMethod>
  void process_update(const NbrDataHandler& nbr_data_handler,
                      [[maybe_unused]] const IterMethod&)
  {
    for (const auto& pTag : nbr_data_handler.get_updated_tags())
    {
      ValueType nbr_value = nbr_data_handler.get_data_unsafe(*pTag);
      // This cycles through the received_values in order not to
      // replace a component that each process is updating with
      // another processes update if there's overlapping
      // computations.  Since messages of the form [component index
      // ; component], we have to parse these messages in pairs,
      // which is easier to do without iterators.
      for(size_t nbr_vals_ind = 0; nbr_vals_ind < (nbr_value.size()/2); nbr_vals_ind++)
      {
        bool use_this_value = true;
        // Cycles through individual values in row_index to avoid
        // replacing its own updates if there's overlap in the
        // linear system partition.
        for(size_t row_index_cycle = 0 ; row_index_cycle < number_of_updated_components_; row_index_cycle++)
        {
          if(nbr_value[nbr_vals_ind*2] == row_indices_[row_index_cycle])
            use_this_value = false;
        }
        if(use_this_value)
        {
          size_t updated_index = static_cast<size_t>(nbr_value[nbr_vals_ind * 2]);
          mailbox_[updated_index] = nbr_value[nbr_vals_ind * 2 + 1];
          dlmc_computation();
        }
      }
    }
  }
  
  /** @brief Prepare values to send to neighbors.
   */
  ValueType prepare_for_publication(ValueType vals_to_publish)
  {
    for(size_t i = 0 ; i < number_of_updated_components_; i ++)
    {
      vals_to_publish[i*2] = row_indices_[i]*1.0;
      vals_to_publish[i*2+1] = mailbox_[row_indices_[i]];
    }
    return vals_to_publish;
  }
  
  /** @brief Return only the components for which this process updates. 
   */
  std::vector<element_t> return_partition_solution() const
  {
    std::vector<element_t> return_vector;
    for(size_t i = 0 ; i < number_of_updated_components_; i++)
    {
      return_vector.push_back(mailbox_[row_indices_[i]]);
    }
    return return_vector;
  }

  /** @brief Return the full solution.
   */
  const std::vector<element_t>& return_full_solution() const
  {
    return mailbox_;
  }

private:

  std::vector<double> getDistribution
  (
      double x_mu, double x_sigma, size_t numberOfValues
  )
  {
      std::mt19937 gen((std::random_device())());
      std::normal_distribution<double> nd(x_mu, x_sigma);
      std::vector<double> result;
      result.reserve(numberOfValues);
      while(numberOfValues-- > 0)
      {
          result.push_back(nd(gen));
      }
      return result;
  }

  double grad_log_like
  (
    double x, double mu, double sigma
  ) 
  {
     return (1 / pow(sigma, 2)) * (x - mu)
  }


  /** @brief Perform the DLMC update.
   */
  void dlmc_computation()
  {
    size_t v_j = 0, g_j = 0;
    for(size_t i = 0 ; i < _num_recieved; i += 2)
    {
      v_j += mailbox_[i];
      g_j += mailbox_[i+1];
    }
    v_j = v_j / _num_recieved;
    g_j = g_j / _num_recieved;

    local_mean = 0
    for(size_t i = 0 ; i < local_partition_.size(); i++) { local_mean += local_partition_[i];}
    local_mean /= local_partition_.size();

    std::vector<double> n_error = getDistribution(0, (epsilon_/t_), 1);
    theta_[T_-1] = v_j + ((epsilon_/t_) / 2) * 
                    (grad_log_like(v_j, theta_[T_-2], sigma_) + (num_nbrs) * g_j) + n_error[0];
    gradient_[T_-1] = grad_log_like(local_mean, theta_[T_-2], sigma_);
   
  }

  std::vector<double> gradient_;
  std::vector<double> theta_;
  std::vector<size_t> local_partition_;
  size_t sigma_;
  size_t T_;
  size_t epsilon_;
  
  

  // Variables internal to this class.
  std::vector<element_t> mailbox_;
  std::vector<element_t> publish_values_;
}; // class DLMCProcessor

}

#endif 
