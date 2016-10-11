#define _GNU_SOURCE 1
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sched.h>
#include <cstring>

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <sstream>
#include <exception>
#include <stdexcept>

using std::strtok;

template<class T>
auto operator<<(std::ostream& os, const T& t) -> decltype(t.print(os), os)
{
    t.print(os);
    return os;
}

static void set_own_affinity(int cpu)
{
  cpu_set_t cpu_set;

  CPU_ZERO(&cpu_set);
  CPU_SET(cpu, &cpu_set);
  int ret = sched_setaffinity(getpid(), sizeof(cpu_set), &cpu_set);
  if (ret)
    throw std::runtime_error("Failed to set affinity to " + std::to_string(cpu));
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


  void print(std::ostream &os) const {
    os << tsc << ","
       << utime << ","
       << stime << ","
       << noise << ","
       << idle;
  }
};

static void init_ts(Timestep &ts)
{
  static uint64_t start = 0;

  struct timespec tp;
  clock_gettime(CLOCK_REALTIME, &tp);

  uint64_t time = tp.tv_sec * 1000 * 1000 * 1000 + tp.tv_nsec;

  if (!start)
    start = time;

  if (time < start)
    throw std::runtime_error("Time can't go backwards");

  ts.tsc = time - start;
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

// Checks /proc/[pid]/stat
const int stat_size = 1024;
char stat_buf[stat_size];

static bool read_process(const char *proc_path, Timestep &ts)
{
  int ret = read_proc_file(proc_path, stat_buf, stat_size);
  if (ret < 0)
    return false;

  static Timestep start;
  int i = 1;
  // How many items we need to read
  int done = 2;
  char *pch = strtok(stat_buf, " ");
  while (pch != NULL) {
    switch(i) {
      case 14:
        // utime
        sscanf(pch, "%lu", &ts.utime);
        done --;
        break;
      case 15:
        // stime
        sscanf(pch, "%lu", &ts.stime);
        done --;
        break;
    }
    i++;
    pch = strtok(NULL, " ");
  }

  if (!start.utime) {
    start.utime = ts.utime;
    start.stime = ts.stime;
  }

  ts.utime = ts.utime - start.utime;
  ts.stime = ts.stime - start.stime;

  if (done != 0)
    throw std::runtime_error("Failed to read right number of items");

  return true;
}

static bool read_noise(const char *proc_path, Timestep &ts)
{
  int ret = read_proc_file(proc_path, stat_buf, stat_size);
  if (ret < 0)
    return false;

  static Timestep start_ts;
  int i = 1;
  // How many items we need to read
  int done = 2;
  char *pch = strtok(stat_buf, " ");
  uint64_t noise;
  while (pch != NULL) {
    switch(i) {
      case 14:
        // utime
        sscanf(pch, "%lu", &noise);
        ts.noise = noise;
        done --;
        break;
      case 15:
        // stime
        sscanf(pch, "%lu", &noise);
        ts.noise += noise;
        done --;
        break;
    }
    i++;
    pch = strtok(NULL, " ");
  }

  if (!start_ts.noise)
    start_ts.noise = ts.noise;
  ts.noise = ts.noise - start_ts.noise;

  if (done != 0)
    throw std::runtime_error("Failed to read right number of items");

  return true;
}

void read_core(const char *cpu, Timestep &ts)
{
  int ret = read_proc_file("/proc/stat", stat_buf, stat_size);
  if (ret < 0)
    throw std::runtime_error("Failed to read cpu file");

  // Find core string
  char *core_str = strtok(stat_buf, "\n");
  while (core_str != NULL) {
    if (strstr(core_str, cpu) == core_str)
      break;
    core_str = strtok(NULL, "\n");
  }

  if (!core_str)
    throw std::runtime_error("Failed to find core string");

  const uint64_t clk_tck = sysconf(_SC_CLK_TCK);

  int i = 1;
  int done = 1;
  char *pch = strtok(core_str, " ");
  pch = strtok(NULL, " "); // Skip cpu name
  while (pch != NULL) {
    switch(i) {
      // idle time
      case 4:
        static uint64_t idle_start = 0;
        sscanf(pch, "%lu", &ts.idle);
        if (!idle_start)
          idle_start = ts.idle;
        ts.idle = ts.idle - idle_start;
        ts.idle = ts.idle * 1000 * 1000 * 1000 / clk_tck;
        done--;
        break;
    }
    i++;
    pch = strtok(NULL, " ");
  }

  if (done != 0)
    throw std::runtime_error("Failed to read right number of items from /proc/stat");
}

void trace(int cpu, pid_t pid, std::ofstream &log)
{
  set_own_affinity(cpu);

  check_target_affinity(cpu, pid);

  // Main loop
  std::vector<Timestep> data;

  const std::string proc_path("/proc/" + std::to_string(pid) + "/stat");
  const std::string cpu_name("cpu" + std::to_string(cpu));

  while (true) {
    Timestep ts;

    init_ts(ts);
    // Finishes when process terminates
    if(!read_process(proc_path.c_str(), ts))
      break;

    read_core(cpu_name.c_str(), ts);

    read_noise("/proc/self/stat", ts);
    data.push_back(ts);
    // Sleep 100 msec
    usleep(100*1000);
  }

  // Dump results
  for (const auto i : data) {
    log << i << std::endl;
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

  int cpu;
  std::string cpu_str;
  if (!int_valid(cpu, cpu_str, argv[1])) {
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


  std::ofstream log_file(log_name.c_str());
  if (!log_file) {
    return usage("Failed to create file: " + log_name);
  }

  daemon(1, 0);

  try {

  trace(cpu, pid, log_file);

  } catch(std::exception &e) {
    return usage(e.what());
  }
}
