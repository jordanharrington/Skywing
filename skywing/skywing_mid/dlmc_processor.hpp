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
 * @brief 
 *
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
    int size_of_network,
    std::size_t iteration_num)
    : 
    T_(iteration_num), 
    num_nbrs_(size_of_network - 1),
    local_partition_(getDistribution(10, 0, 101)),
    sigma_(10), 
    theta_(101, 0.0), 
    gradient_(101, 1.0), 
    epsilon_(100), 
    mailbox_(2, 0.0), 
    publish_values_(4, 0.0)
  {
    dlmc_computation();
  }

  /** @brief Initialize values to communicate.
   *
   *  Must send an index along with each value, so vector length is
   *  twice the number of components updated.
   */
  ValueType get_init_publish_values()
  { return std::vector<element_t>{ 0.0, theta_[0], 1.0, gradient_[0]}; }

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
    mailbox_[0] = 0.0;
    mailbox_[1] = 0.0;
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
        size_t updated_index = static_cast<size_t>(nbr_value[nbr_vals_ind * 2]);
        mailbox_[updated_index] += nbr_value[nbr_vals_ind * 2 + 1];
      }
    }
    mailbox_[0] /= num_nbrs_;
    mailbox_[1] /= num_nbrs_;
    dlmc_computation();
  }
  
  /** @brief Prepare values to send to neighbors.
   */
  ValueType prepare_for_publication(ValueType vals_to_publish)
  {
    for(size_t i = 0 ; i < mailbox_.size(); i ++)
    {
      vals_to_publish[i*2] = i*1.0;
      vals_to_publish[i*2+1] = mailbox_[i];
    }
    return vals_to_publish;
  }

  /** @brief Return only the components for which this process updates. 
   */
  std::vector<element_t> return_partition_solution() const
  {
    return mailbox_;
  }

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
     return (1 / pow(sigma, 2)) * (x - mu);
  }


  /** @brief Perform the DLMC update.
   */
  void dlmc_computation()
  {
    double local_mean = local_partition_[T_-1];
    std::vector<double> n_error = getDistribution(0, (epsilon_/T_), 1);
    theta_[T_] = mailbox_[0] + ((epsilon_/T_) / 2) * 
                    (grad_log_like(mailbox_[0], theta_[T_-1], sigma_) + (num_nbrs_) * mailbox_[1]) + n_error[0];
    gradient_[T_] = grad_log_like(local_mean, theta_[T_-1], sigma_);
    mailbox_[0] = theta_[T_];
    mailbox_[1] = gradient_[T_];
  }

  std::vector<double> gradient_;
  std::vector<double> theta_;
  std::vector<double> local_partition_;
  double num_nbrs_;
  size_t sigma_;
  size_t T_;
  size_t epsilon_;

  // Variables internal to this class.
  std::vector<element_t> mailbox_;
  std::vector<element_t> publish_values_;
}; // class DLMCProcessor

}

#endif 
