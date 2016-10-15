#pragma once

#include <fstream>

#include "tasks.hpp"

struct TaskTime
{
public:
  uint64_t utime;
  uint64_t stime;
  pid_t    pid;
  int      cpu;

  TaskTime(pid_t pid) : pid(pid) {}

  bool read()
  {

    std::string proc_path("/proc/" + std::to_string(pid) + "/stat");
    std::ifstream proc_stat(proc_path);

    if (!proc_stat.good())
      return false;

    std::string line;
    std::getline(proc_stat, line);
    assert(line.size() > 0);

    int ret, i = 1;
    // How many items we need to read
    int done = 2;
    std::string::size_type npos = 0;
    while (npos != std::string::npos) {
      switch(i) {
        case 14:
          // utime
          ret = sscanf(line.c_str() + npos, "%lu", &utime);
          assert(ret == 1);
          done --;
          break;
        case 15:
          // stime
          ret = sscanf(line.c_str() + npos, "%lu", &stime);
          assert(ret == 1);
          done --;
          break;
      }
      i++;
      npos = line.find_first_of(' ', npos + 1);
    }
    assert(done == 0);

    return true;
  }

  TaskTime operator-(const TaskTime &other)
  {
    TaskTime res(pid);

    assert(pid == other.pid);

    res.utime = utime - other.utime;
    res.stime = stime - other.stime;
    return res;
  }
};

struct CoreTime
{
  uint64_t idle;
  int cpu;

  CoreTime(int cpu) : cpu(cpu) {}

  CoreTime operator-(const CoreTime &other)
  {
    CoreTime res(cpu);

    res.idle = idle - other.idle;
    return res;
  }
};

class Measurement
{
  // Data
public:
  uint64_t _tsc;
  uint64_t _noise;
  std::vector<CoreTime> _cores;

public:
  std::vector<TaskTime> tasks;

  // Methods
private:
  uint64_t init_tsc()
  {
    struct timespec tp;
    clock_gettime(CLOCK_REALTIME, &tp);
    return tp.tv_sec * 1000 * 1000 * 1000 + tp.tv_nsec;
  }

  bool init_tasks(const std::vector<pid_t> &pids)
  {
    for (auto pid: pids) {
      TaskTime tt(pid);

      auto res = tt.read();
      if (!res)
        return res;

      tasks.push_back(tt);
    }

    return true;
  }

  CoreTime read_core(const std::string &line);

  void init_cores();
  void read_noise();

public:

  Measurement() : _tsc(0), _noise(0) {}

  bool read(const std::vector<pid_t> &pids)
  {
    _tsc = init_tsc();

    if (!init_tasks(pids))
      return false;

    init_cores();

    read_noise();

    return true;
  }

  bool valid()
  {
    return _tsc > 0;
  }

  Measurement operator-(const Measurement &other)
  {
    Measurement res;

    res._tsc = _tsc - other._tsc;
    res._noise = _noise - other._noise;

    assert(other.tasks.size() == tasks.size());
    for (unsigned i = 0; i < tasks.size(); i++)
      res.tasks.push_back(tasks[i] - other.tasks[i]);

    assert(other._cores.size() == _cores.size());
    for (unsigned i = 0; i < _cores.size(); i++)
      res._cores.push_back(_cores[i] - other._cores[i]);

    return res;
  }

  void save_schedule(const std::vector<Task> &schedule);

  void dump_csv(std::ostream &log) const
  {
    for (auto t : tasks) {
      auto idle = _cores[t.cpu].idle;

      assert(_cores[t.cpu].cpu == t.cpu);

      log << _tsc << ","
          << t.utime << ","
          << t.stime << ","
          << _noise << ","
          << idle  << ","
          << t.cpu  << ","
          << t.pid << std::endl;
    }
  }
};
