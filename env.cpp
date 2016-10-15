#include <iostream>
#include <boost/program_options.hpp>

#include "env.hpp"

using namespace boost::program_options;

Env::Env(int argc, char **argv) :
  initialized(true)
{
  try {
    options_description desc{"Options"};

    // TODO: Need validation for the options

    desc.add_options()
      ("help,h", "Help screen")
      ("ncpu", value<int>(&ncpu)->default_value(sysconf(_SC_NPROCESSORS_ONLN)),
       "Number of used cores")
      ("sched", value<std::string>(&scheduler)->default_value("default"), "Pick a scheduler")
      ("debug,d", value<bool>(&debug)->default_value(false), "Enable debugging")
      ("sleep", value<int>(&sleep)->default_value(3), "Sleep before switching on scheduling (allows initialization before setting realtime priorities)")
      ("out,o", value<std::string>(&log_prefix)->required(), "Prefix for output file")
      ("pid,p", value<pid_t>(&victim)->required(), "Process id of a victim");

    variables_map vm;
    store(parse_command_line(argc, argv, desc), vm);
    notify(vm);

    if (vm.count("help"))
      std::cout << desc << std::endl;
  } catch (const error &ex) {
    std::cerr << ex.what() << std::endl;
  }
}

const Env &Env::env(int argc, char **argv)
{
  static Env env;

  if (!env.initialized)
    env = Env(argc, argv);
  else if (argc != -1 || argv != NULL)
    throw std::runtime_error("Env can be initialized only once");

  return env;
}

std::ostream &Env::operator<<(std::ostream &os)
{
  os << "NCPU : "           << ncpu       << std::endl
     << "Scheduler : "      << scheduler  << std::endl
     << "log_prefix : "     << log_prefix << std::endl
     << "victim : "         << victim     << std::endl
     << "Activate after : " << sleep      << std::endl
     << "Debug : "          << debug      << std::endl;
  return os;
}
