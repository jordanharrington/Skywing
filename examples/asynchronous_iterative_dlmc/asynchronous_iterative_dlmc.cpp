#include "skywing_core/skywing.hpp"

#include <algorithm>
#include <tuple>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>
#include <cmath>

#include "skywing_core/enable_logging.hpp"

using DataTag = skywing::PublishTag<std::vector<double>>;

struct MachineConfig {
  std::string name;
  std::string remote_address;
  std::vector<DataTag> tags_produced;
  std::vector<DataTag> tags_to_subscribe_to;
  std::vector<std::string> machines_to_connect_to;
  std::uint16_t port;

  template<typename T>
  static void read_until_dash(std::istream& in, std::vector<T>& read_into)
  {
    std::string temp;
    while (std::getline(in, temp)) {
      if (temp.empty()) { continue; }
      if (!in || temp.front() == '-') { break; }
      read_into.push_back(T{std::move(temp)});
    }
  }

  friend std::istream& operator>>(std::istream& in, MachineConfig& config)
  {
    std::getline(in, config.name);
    std::getline(in, config.remote_address);
    in >> config.port >> std::ws;
    read_until_dash(in, config.tags_produced);
    read_until_dash(in, config.tags_to_subscribe_to);
    read_until_dash(in, config.machines_to_connect_to);
    return in;
  }
};

std::vector<double> getDistribution
  (
      double x_mu, double x_sigma, size_t numberOfValues
  )
  {
      std::mt19937 gen((std::random_device())());
      std::normal_distribution<double> nd(x_mu, x_sigma);
      std::vector<double> result;
      result.reserve(numberOfValues);
      while(numberOfValues-- > 0) { result.push_back(nd(gen));}
      return result;
  }

double grad_log_like
  (
    double x, double mu, double sigma
  ) 
  {
     return (1 / pow(sigma, 2)) * (x - mu);
  }

// Callable signature: auto func(const double&, std::vector<double>&) -> std::pair<double, bool>
// The bool is to indicate if the function should continue iterating
template<typename Callable>
void asynchronous_iterative(
  const MachineConfig& config,
  const std::unordered_map<std::string, MachineConfig>& machines,
  const std::vector<double> distribution,
  const std::vector<double> initial_value,
  Callable act_on)
{
  skywing::Manager manager(config.port, config.name);
  if (config.tags_produced.empty()) {
    std::cerr << config.name << ": Must produce at least one tag\n";
    std::exit(1);
  }
  manager.submit_job("job", [&](skywing::Job& job, skywing::ManagerHandle manager_handle) {
    for (const auto& connect_to_name : config.machines_to_connect_to) {
      const auto conn_to_iter = machines.find(connect_to_name);
      if (conn_to_iter == machines.cend()) {
        std::cerr << "Could not find machine \"" << connect_to_name << "\" to connect to.\n";
      }
      const auto time_limit = std::chrono::steady_clock::now() + std::chrono::seconds{10};
      while (!manager_handle.connect_to_server("127.0.0.1", conn_to_iter->second.port).get()) {
        if (std::chrono::steady_clock::now() > time_limit) {
          std::cerr << config.name << ": Took too long to connect to " << conn_to_iter->second.remote_address << ":"
                    << conn_to_iter->second.port << '\n';
          return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
      }
    }
    job.declare_publication_intent_range(config.tags_produced);
    // Subscribe to all the relevant tags
    auto fut = job.subscribe_range(config.tags_to_subscribe_to);
    // Should adjust seconds value based on number of neighbors for each machine 
    // i.e. more tags for each machine, more time needed to fully subscribe
    if (!fut.wait_for(std::chrono::seconds(60))) {
      std::cerr << config.name << ": Took too long to subscribe to tags\n";
      std::exit(1);
    }
    // Cache previous values seen to feed to the callable function
    std::unordered_map<std::string,std::vector<double>> neighbor_values;
    std::vector<double> own_value = initial_value;
    job.publish(config.tags_produced.front(), own_value);
    std::ranlux48 prng{std::random_device{}()};
    while (true) {
      // Gather data from subscriptions
      for (const auto& sub_tag : config.tags_to_subscribe_to) {
        if (job.has_data(sub_tag)) { neighbor_values[sub_tag.id()] = *job.get_waiter(sub_tag).get(); }
      }
      // Only call the function if there's any data that's been seen
      if (neighbor_values.empty()) {
        // No values seen - check if all subscriptions are gone and exit if so
        const bool should_exit = [&]() {
          for (const auto& sub_tag : config.tags_to_subscribe_to) {
            if (job.tag_has_subscription(sub_tag)) { return false; }
          }
          // All tags failed
          return true;
        }();
        if (should_exit) { break; }
      }
      else {
        std::vector<std::vector<double>> other_values;
        // Collects values from neighboring nodes
        for (auto value : neighbor_values){ other_values.push_back(value.second); }
        bool should_exit = false;
        std::tie(own_value, should_exit) = act_on(own_value, other_values, distribution, config.name);
        job.publish(config.tags_produced.front(), own_value);
        if (should_exit) { break; }
      }
      // Sleep for a random amount of time
      const auto sleep_ms = std::uniform_int_distribution<int>{1, 5}(prng);
      std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
    }
    std::cout << config.name << ": Final value is mu=" << own_value[0] << " and gradient=" << own_value[2] << '\n';
  });
  manager.run();
}


int main(const int argc, const char* const argv[])
{
  // Explicitly disable logging as the output is too noisy otherwise
  SKYNET_SET_LOG_LEVEL_TO_WARN();
  if (argc != 3) {
    std::cerr << "Usage:\n" << argv[0] << " config_file machine_name\n";
    return 1;
  }
  std::ifstream fin(argv[1]);
  const char* machine_name = argv[2];
  if (!fin) {
    std::cerr << "Error opening config file \"" << argv[1] << "\"\n";
    return 1;
  }
  const std::unordered_map<std::string, MachineConfig> configurations = [&]() {
    MachineConfig temp;
    std::unordered_map<std::string, MachineConfig> to_ret;
    while (fin >> temp) {
      to_ret[temp.name] = std::move(temp);
    }
    return to_ret;
  }();
  const auto config_iter = configurations.find(machine_name);
  if (config_iter == configurations.cend()) {
    std::cerr << "Could not find configuration for machine \"" << machine_name << "\"\n";
    return 1;
  }
  // Each node is given a distribution with the same mean value (std = 10 for all)
  std::vector<double> distribution = getDistribution(300, 10, 100); 
  // Initally each node belives the mean of their distribution is 0 and gradient is 1)
  auto value = std::vector<double>{0.0, 1.0};
  std::cout << machine_name << ": Own value is mu=" << value[0] << " and gradient="<< value[1] << '\n';

  asynchronous_iterative(
    config_iter->second,
    configurations,
    distribution,
    value,
    // DLMC Algo from Stochastic Gradient-Based Distributed Bayesian Estimation in Cooperative Sensor Networks
    [iter = 1](const std::vector<double>& self_value, 
              const std::vector<std::vector<double>>& other_values, 
              const std::vector<double>& distribution,
              const std::string machine_name
              ) mutable {
      // Setting max number of iterations
      constexpr int num_iters = 50;
      // NOTE: sigma must be identical to that in initial distribution split (main method: around line 101)
      double v_j = 0.0, g_j = 0.0, num_nbrs = 0.0, sigma = 10.0, epsilon = 100.0;
      // Aggregating and finding average for neighbor values of theta and gradient
      for(std::vector<double> nbr_val : other_values) {v_j+=nbr_val[0]; g_j+=nbr_val[1]; ++num_nbrs;}
      v_j = (v_j / num_nbrs);
      g_j = (g_j / num_nbrs);
      // Getting random error value
      std::vector<double> n_error = getDistribution(0, (epsilon/iter), 1);
      // local update of theta and gradient values with respect to neighbor values (theta in zero index in self_vlues, gradient is 1)
      const auto new_value_theta = v_j + (((epsilon/iter)/2) * (grad_log_like(v_j, self_value[0], sigma) + (num_nbrs * g_j))) + n_error[0];
      const auto new_value_grad = grad_log_like(distribution[iter-1], new_value_theta, sigma);
      // Output used for graphing outside of skywing. Can be commented out for an easier to read terminal
      std::cout << "\ndata," << machine_name << "," << new_value_theta << "," << new_value_grad << "," << iter << "\n";
      ++iter;
      // Publishing new values
      return std::make_pair(std::vector<double>{new_value_theta,new_value_grad}, iter > num_iters);
    });
}
