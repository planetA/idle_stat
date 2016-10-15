#define _GNU_SOURCE 1
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <fcntl.h>
#include <unistd.h>
#include <sched.h>
#include <dirent.h>
#include <signal.h>

#include <cstring>
#include <cassert>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <sstream>
#include <exception>
#include <stdexcept>
#include <memory>
#include <algorithm>

#include "shmem.hpp"
#include "tasks.hpp"
#include "env.hpp"
#include "scheduler.hpp"



using std::strtok;

template<class T>
auto operator<<(std::ostream& os, const T& t) -> decltype(t.print(os), os)
{
    t.print(os);
    return os;
}


static void check_target_affinity(int cpu, pid_t pid)
{
  cpu_set_t cpu_set, cmp_set;

  CPU_ZERO(&cpu_set);

  if (CPU_SETSIZE < sysconf(_SC_NPROCESSORS_ONLN))
    throw std::range_error("Too many CPUs on your machine");

  if (sched_getaffinity(pid, sizeof(cpu_set), &cpu_set))
    throw std::runtime_error("Failed to get affinity of " + std::to_string(pid));

  CPU_ZERO(&cmp_set);
  CPU_SET(cpu, &cmp_set);

  if (!CPU_EQUAL(&cpu_set, &cmp_set)) {
    throw std::invalid_argument("Target does not have desired affinity");
  }
}

struct Timestep
{
  uint64_t tsc;
  uint64_t utime;
  uint64_t stime;
  uint64_t noise;
  uint64_t idle;


  void print(std::ostream &os) const
  {
    os << tsc << ","
       << utime << ","
       << stime << ","
       << noise << ","
       << idle;
  }
};

std::vector<pid_t> get_partners(const char *argv0)
{
  auto get_exe = [&](const std::string path) {
    size_t pos = path.rfind('/');
    if (pos != std::string::npos)
      return path.substr(pos + 1);
    return path;
  };

  std::vector<pid_t> pids;

  std::string name = get_exe(argv0);

  // XXX: Ensure that all process are started
  sleep(5);

  // Open the /proc directory
  DIR *dp = opendir("/proc");
  if (dp != NULL) {
    // Enumerate all entries in directory until process found
    struct dirent *dirp;
    while ((dirp = readdir(dp))) {
      // Skip non-numeric entries
      int id = atoi(dirp->d_name);
      if (id > 0) {
        // Read contents of virtual /proc/{pid}/cmdline file
        std::string cmdPath = std::string("/proc/") + dirp->d_name + "/cmdline";
        std::ifstream cmdFile(cmdPath.c_str());
        std::string cmdLine;
        getline(cmdFile, cmdLine);
        if (!cmdLine.empty())
        {
          // Keep first cmdline item which contains the program path
          size_t pos = cmdLine.find('\0');
          if (pos != std::string::npos)
            cmdLine = cmdLine.substr(0, pos);
          // Keep program name only, removing the path
          cmdLine = get_exe(cmdLine);
          // Compare against requested process name
          if (name == cmdLine)
            pids.push_back(id);
        }
      }
    }
  }

  closedir(dp);

  std::sort(pids.begin(), pids.end(), [](pid_t a, pid_t b) { return a < b; });

  return pids;
}

bool elect_leader(std::vector<pid_t> partners)
{
  pid_t me = getpid();
  if (me == partners[0])
    return true;
  return false;
}

int read_proc_file(const char *path, char *buf, const int buf_size)
{

// TODO: check errno, throw an error if errno is set not as expected
  int fd = open(path, O_RDONLY);
  if (fd < 0)
    return fd;

  int ret = read(fd, buf, buf_size);
  if (ret < 0)
    throw std::runtime_error("Failed to read proc file" + std::string(path));

  close(fd);

  buf[ret] = 0;
  return ret;
}


inline bool int_valid(int &i, std::string &i_str, const char *str)
{
  std::stringstream iss(str);
  iss >> i;
  std::ostringstream oss;
  oss << i;
  i_str = iss.str();
  return !(i < 0 || (oss.str() != iss.str()));
}

inline bool file_exists(const std::string &name)
{
  return std::ifstream(name.c_str()).good();
}

int main(int argc, char **argv)
{
  Env::env(argc, argv);
  // auto usage = [&](std::string str) {
  //   std::cerr << "Usage: " << argv[0] << " <cpu> <pid> <logfile> " << std::endl
  //   << str << std::endl;
  //   return 1;
  // };

  // if (argc != 4) {
  //   return usage("Wrong number of arguments");
  // }

  // std::string cpu_str;
  // if ((!int_valid(ncpu, cpu_str, argv[1]))
  //     || (ncpu < 1) || (ncpu > )) {
  //   return usage("<cpu> should be a decimal number");
  // }

  // pid_t pid;
  // std::string pid_str;
  // if (!int_valid(pid, pid_str, argv[2])) {
  //   return usage("<pid> should be a decimal number");
  // }

  if (Env::env().debug)
    std::cout << Env::env().env << std::endl;

  std::string log_name = (Env::env().log_prefix + "." +
                          std::to_string(Env::env().victim));

  if (file_exists(log_name)) {
    throw std::runtime_error("File exists: " + log_name);
  }



  auto partners = get_partners(argv[0]);

  bool leader = elect_leader(partners);

  Shmem shm(leader, partners, argv[0]);

  shm.report_victims(Env::env().victim);

  if (!leader)
    exit(0);

  std::ofstream log_file(log_name.c_str());
  if (!log_file) {
    throw std::runtime_error("Failed to create file: " + log_name);
  }

  sleep(10);

  std::unique_ptr<Scheduler> scheduler = Scheduler::create(Env::env());

  scheduler->loop(shm.targets(), log_file);

  // daemon(1, 0);
}
