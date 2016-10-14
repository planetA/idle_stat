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
#include <algorithm>

#include "shmem.hpp"
#include "measurement.hpp"
#include "tasks.hpp"

using std::strtok;

static int ncpu;

template<class T>
auto operator<<(std::ostream& os, const T& t) -> decltype(t.print(os), os)
{
    t.print(os);
    return os;
}

static void set_affinity(pid_t pid, int cpu)
{
  cpu_set_t cpu_set;

  CPU_ZERO(&cpu_set);
  CPU_SET(cpu, &cpu_set);
  int ret = sched_setaffinity(pid, sizeof(cpu_set), &cpu_set);
  if (ret)
    throw std::runtime_error("Failed to set affinity to " + std::to_string(cpu));
}

static void set_own_affinity()
{
  set_affinity(getpid(), 0);
}

void rise_priority()
{
  // Max priority reserved for the scheduler itself
  struct sched_param param;

  int max = sched_get_priority_max(SCHED_FIFO);

  struct rlimit rlim;
  int ret = getrlimit(RLIMIT_RTPRIO, &rlim);
  assert(ret == 0);

  // cast from long long to int is safe, because values are up to 99
  max = std::min(max, static_cast<int>(rlim.rlim_max));
  param.sched_priority = max;

  ret = sched_setscheduler(0, SCHED_FIFO, &param);
  if (ret) {
    throw std::runtime_error(std::string("Failed to rise own priority: ") +
                             std::strerror(errno));
  }
}

void drop_priority()
{
  // Max priority reserved for the scheduler itself
  struct sched_param param;

  int min = sched_get_priority_min(SCHED_FIFO);

  // cast from long long to int is safe, because values are up to 99
  param.sched_priority = min;

  int ret = sched_setscheduler(0, SCHED_FIFO, &param);
  if (ret) {
    throw std::runtime_error(std::string("Failed to drop own priority: ") +
                             std::strerror(errno));
  }
}

void schedule_set_priority(pid_t pid, int priority)
{
  struct sched_param param;

  int min = sched_get_priority_min(SCHED_FIFO);
  int max = sched_get_priority_max(SCHED_FIFO);

  // Max reserved for the scheduler itself
  priority = std::min(priority + min - 1, max - 1);

  param.sched_priority = priority;
  int ret = sched_setscheduler(pid, SCHED_FIFO, &param);

  assert(ret == 0);
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

std::vector<Task> schedule_rr(const std::vector<pid_t> & pids)
{
  std::vector<Task> tasks;
  int cpu = 0;
  for (auto pid : pids) {
    tasks.push_back(Task{pid, cpu});
    set_affinity(pid, cpu);
    schedule_set_priority(pid, 1);
    cpu = (cpu + 1) % ncpu;
  }

  return tasks;
}

void do_new_assignment(std::vector<Task> &schedule)
{
  for (auto t : schedule)
    set_affinity(t.pid, t.cpu);

  // Set priorities. Using stable sort, sort by load, then sort by
  // core id. As a result we get a vector of tasks grouped by core
  // and each group sorted by size.
  std::sort(schedule.begin(), schedule.end(), [](Task a, Task b) {
      return a.size < b.size;
    });

  std::stable_sort(schedule.begin(), schedule.end(), [](Task a, Task b) {
      return a.cpu < b.cpu;
    });

  unsigned i = 0, prio;
  while (i < schedule.size()) {
    prio = 1;
    do {
      schedule_set_priority(schedule[i].pid, prio);
      i++;
      prio++;
    } while ((i < schedule.size()) &&
             (schedule[i].cpu == schedule[i-1].cpu));
  }
}

void do_scheduling(std::vector<Task> &tasks)
{
  std::vector<uint64_t> load(ncpu);

  for (auto t : tasks) {
    load[t.cpu] += t.size;
  }

  // auto imbalance = [](std::vector<uint64_t> &load) {
  //   uint64_t sum = std::accumulate(load.begin(), load.end(), 0);
  //   uint64_t max = *std::max_element(load.begin(), load.end());
  //   return ((double) max / (double) sum) * load.size();
  // };
  // double old_imb = imbalance(load);

  double old_rt = *std::max_element(load.begin(), load.end());

  // new_schedule we want to try out
  std::vector<Task> ns(tasks);
  std::sort(ns.begin(), ns.end(), [](Task a, Task b) {
      return a.size < b.size;
    });

  load.assign(load.size(), 0);
  for (auto &t : ns) {
    int min_load = std::min_element(load.begin(), load.end()) - load.begin();
    t.cpu = min_load;
    load[min_load] += t.size;
  }

  double new_rt = *std::max_element(load.begin(), load.end());

  // if new schedule at least x% faster ...
  if (new_rt / 1.05 < old_rt) {
    do_new_assignment(ns);
  }
}

void scheduler(const std::vector<pid_t> pids, std::ofstream &log)
{
  set_own_affinity();

  // Main loop
  std::vector<Measurement> data;

  // Allow victims to initialize themselves.
  sleep(3);

  // Set initial affinity
  std::vector<Task> tasks = schedule_rr(pids);

  Measurement last;
  while (true) {
    Measurement m;

    rise_priority();

    // Returns false, when process terminates
    if(!m.read(pids))
      break;

    m.save_schedule(tasks);

    data.push_back(m);

    if (last.valid()) {
      auto ts = m - last;
      for (unsigned i = 0; i < tasks.size(); i++) {
        assert(tasks[i].pid == ts.tasks[i].pid);
        tasks[i].size = ts.tasks[i].utime + ts.tasks[i].stime + 1;
      }
      do_scheduling(tasks);
    } else {
      std::cout << "Invalid "
                << m._tsc << " "
                << m._noise << " "
                << m._cores.size() << " "
                << m.tasks.size() << std::endl;
    }

    last = m;

    // Sleep 100 msec
    drop_priority();
    usleep(100*1000);
  }

  // Dump results
  for (const auto i : data) {
    i.dump_csv(log);
  }

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
  auto usage = [&](std::string str) {
    std::cerr << "Usage: " << argv[0] << " <cpu> <pid> <logfile> " << std::endl
    << str << std::endl;
    return 1;
  };

  if (argc != 4) {
    return usage("Wrong number of arguments");
  }

  std::string cpu_str;
  if ((!int_valid(ncpu, cpu_str, argv[1]))
      || (ncpu < 1) || (ncpu > sysconf(_SC_NPROCESSORS_ONLN))) {
    return usage("<cpu> should be a decimal number");
  }

  pid_t pid;
  std::string pid_str;
  if (!int_valid(pid, pid_str, argv[2])) {
    return usage("<pid> should be a decimal number");
  }

  std::string log_name = std::string(argv[3]) + "." + pid_str;

  if (file_exists(log_name)) {
    return usage("Failed exists: " + log_name);
  }



  try {

    auto partners = get_partners(argv[0]);

    bool leader = elect_leader(partners);

    Shmem shm(leader, partners, argv[0]);

    shm.report_victims(pid);

    if (!leader)
      exit(0);

    std::ofstream log_file(log_name.c_str());
    if (!log_file) {
      throw std::runtime_error("Failed to create file: " + log_name);
    }

    sleep(10);

    scheduler(shm.targets(), log_file);

    // daemon(1, 0);

  } catch(std::runtime_error &e) {
    return usage(e.what());
  }
}