#include "skywing_core/skywing.hpp"
#include "skywing_core/manager.hpp"
#include "skywing_mid/dlmc_processor.hpp"
#include "skywing_mid/asynchronous_iterative.hpp"
#include "skywing_mid/data_input.hpp"
#include "skywing_mid/stop_policies.hpp"
#include "skywing_mid/publish_policies.hpp"

#include <array>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <random>
#include <thread>
#include <cstdint>
#include <fstream>
#include <type_traits>

using namespace skywing;
// using ValueTag = skywing::PublishTag<std::vector<double>>;

// First three functions are for the Skywing setup step.
std::vector<std::string> obtain_machine_names(std::uint16_t size_of_network)
{
  std::vector<std::string > machine_names;
  machine_names.resize(size_of_network);
  for(int i = 0 ; i < size_of_network; i++)
  {
    machine_names[i] = "node" + std::to_string(i+1);
  }
return machine_names;
}

std::vector<std::uint16_t>  set_port(std::uint16_t starting_port_number, std::uint16_t size_of_network)
{
  std::vector<std::uint16_t> ports;

  for(std::uint16_t i = 0; i < size_of_network; i++)
  {
    ports.push_back(starting_port_number + (i * 1));
  }
  return ports;
}

std::vector<std::string> obtain_tag_ids(std::uint16_t size_of_network)
{
  std::vector<std::string> tag_ids;
  for(int i = 0; i < size_of_network; i++)
  {
    std::string hold = "tag" +  std::to_string(i);
    tag_ids.push_back(hold);
  }
  return tag_ids;
}

// All of the Skywing specific code is located in this function.
void machine_task(
    int machine_number,
    int size_of_network,
    std::size_t iteration_num,
    std::vector<std::uint16_t> ports,
    std::vector<std::string> machine_names,
    std::vector<std::string> tag_ids)
{

  std::cout << machine_number << std::endl;
  print_vec<std::uint16_t>(ports);
  print_vec<std::string>(machine_names);
 
  skywing::Manager manager{ports[machine_number], machine_names[machine_number]};

  std::cout << "Got HERE" << std::endl;

  //skywing::Manager manager{ports[machine_number], machine_names[machine_number]};

  std::cout << "Got HERE" << std::endl;

  manager.submit_job("job", [&](skywing::Job& job, ManagerHandle manager_handle) {


  if (machine_number != static_cast<int>((ports.size()) - 1) )
  {
    // Connecting to the server is an asynchronous operation and can fail.
    while (!manager_handle.connect_to_server("127.0.0.1", ports[machine_number + 1]).get())
    {
      std::cout << "Machine " << machine_number << " trying to connect to " << ports[machine_number+1] << std::endl;
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }

  std::cout << "Machine " << machine_number << " creating iteration object." << std::endl;

  
  using IterMethod = AsynchronousIterative<DLMCProcessor<double>, PublishOnLinfShift<double>,
                                           StopAfterTime, TrivialResiliencePolicy>;
  Waiter<IterMethod> iter_waiter =
    WaiterBuilder<IterMethod>(manager_handle, job, tag_ids[machine_number], tag_ids)
    .set_processor(size_of_network, iteration_num)
    .set_publish_policy(1e-6)
    .set_stop_policy(std::chrono::seconds(5))
    .set_resilience_policy()
    .build_waiter();

  std::cout << "Machine " << machine_number << " about to get iteration object." << std::endl;
  IterMethod async_dlmc = iter_waiter.get();
                                       
  std::cout << "Machine " << machine_number << " about to start dlmc iteration." << std::endl;  
  async_dlmc.run([&](const decltype(async_dlmc)& p)
    {
      std::cout << p.run_time().count() << "ms: Machine " << machine_number << " has values ";
      print_vec<double>(p.get_processor().return_partition_solution());
    });
  std::cout << "Machine " << machine_number << " finished dlmc iteration." << std::endl;

  double run_time = async_dlmc.run_time().count();
  int information_received = async_dlmc.get_iteration_count();
  std::cout << std::endl;
  std::cout << "\t New Info: \t" << information_received ; 
  std::cout << std::endl;
  std::cout << "\t Runtime: \t" << run_time; 
  std::cout << std::endl;      
  std::cout << "\t Iteration Complete: \t" << !async_dlmc.return_iterate() ; 
  std::cout << std::endl;
  std::cout << "--------------------------------------------" << std::endl;
  std::this_thread::sleep_for(std::chrono::seconds(10));
  });
  manager.run();
}


int main(int argc, char* argv[])
{
  // Error checking for the number of arguments
  if (argc != 5)
  {
    std::cout << "Usage: Wrong Number of Arguments: " << argc << std::endl;
    return 1;
  }

  // Parse the machine number, starting_port_number, and size_of_network that was passed in
  // Do this in a lambda so that if there's an exception a dummy value can be
  // returned which will always trigger an error
    int machine_number = [&]() {
    try
    {
      return std::stoi(argv[1]);
    }
    catch (...)
    {
      return -1;
    }
  }();
  const std::uint16_t starting_port_number = [&]() {
    try
    {
      return std::stoi(argv[2]);
    }
    catch (...)
    {
      return -1;
    }
  }();
   int size_of_network = [&]() {
    try
    {
      return std::stoi(argv[3]);
    }
    catch (...)
    {
      return -1;
    }
  }();
  
  if (machine_number < 0 || machine_number >= size_of_network)
  {
    std::cerr
      << "Invalid machine_number of " << std::quoted(argv[1]) << ".\n"
      << "Must be an integer between 0 and " << size_of_network - 1 << '\n';
    return -1;
  }
  if (size_of_network <= 0)
  {
    std::cerr
      << "Invalid size_of_network of " << std::quoted(argv[3]) << ".\n"
      << "Must be an integer greater than 0 and  match the number of threads created. \n";
    return -1;
  }

  int iteration_num = std::stoi(argv[4]);
  //This creates the relevant vectors needed to interact with skywing.
  auto ports = set_port(starting_port_number, size_of_network);
  auto machine_names = obtain_machine_names(size_of_network);
  std::vector<std::string> tag_ids = obtain_tag_ids(size_of_network);

  // Skywing call
 machine_task(machine_number, size_of_network, iteration_num, ports, machine_names, tag_ids);

  return 0;
}
