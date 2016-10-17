#include <iostream>
#include <boost/program_options.hpp>
#include <boost/regex.hpp>

#include "env.hpp"

using namespace boost::program_options;

CpuList::CpuList(const std::string &cpu_string)
{

  if (cpu_string == "all") {
    for (int i = 0; i < sysconf(_SC_NPROCESSORS_ONLN); i++) {
      cpus.push_back(i);
    }
    return;
  }

  // static boost::regex r("(\\d+|\\d+-\\d+)?");
  static boost::regex r("^(\\d+-\\d+|\\d+)(,\\d+-\\d+|,\\d+)?(,\\d+-\\d+|,\\d+)*$");

  // Do regex match and convert the interesting part to
  // int.
  boost::smatch what;
  if (!boost::regex_match(cpu_string, what, r)) {
    throw validation_error(validation_error::invalid_option_value);
  }

  std::string::const_iterator start = cpu_string.begin();
  std::string::const_iterator end = cpu_string.end();
  while (boost::regex_search(start, end, what, r)) {
    // what[1] single or a range of cpus
    if (!what[1].matched)
      throw validation_error(validation_error::invalid_option_value);

    std::string stest(what[1].first, what[1].second);
    auto minus = stest.find('-');
    if (minus == std::string::npos) {
      int value;
      try {
        value = std::stoi(stest);
      } catch (std::exception &e) {
        throw validation_error(validation_error::invalid_option_value);
      }
      cpus.push_back(value);
    } else {
      auto s = std::stoi(stest.substr(0, minus));
      auto e = std::stoi(stest.substr(minus+1));

      if (s > e)
        throw validation_error(validation_error::invalid_option_value);

      for (int cpu = s; cpu<=e; cpu++)
        cpus.push_back(cpu);
    }
    start = what[2].first;
    if (*start == ',')
      start++;
  }

  std::sort(cpus.begin(), cpus.end());
  cpus.erase(std::unique(cpus.begin(), cpus.end()), cpus.end());
}

void validate(boost::any& v,
              const std::vector<std::string>& values,
              CpuList* target_type, std::string)
{
  (void) target_type;

  // Make sure no previous assignment to 'a' was made.
  validators::check_first_occurrence(v);
  // Extract the first string from 'values'. If there is more than
  // one string, it's an error, and exception will be thrown.
  const std::string& s = validators::get_single_string(values);

  v = boost::any(CpuList(s));
}

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
