#include <unistd.h>

#include <iostream>
#include <cassert>
#include <fstream>
#include <vector>
#include <algorithm>
#include <map>

#include "measurement.hpp"

void Measurement::save_schedule(const std::vector<Task> &schedule)
{
  // Prepare pid-to-core map
  std::map<pid_t, int> pid2core;
  for (auto t : schedule) {
    pid2core[t.pid] = t.cpu;
  }

  for (auto &t : tasks) {
    t.cpu = pid2core[t.pid];
  }
}

void Measurement::read_noise()
{
  std::ifstream proc_stat("/proc/self/stat");

  std::string line;
  std::getline(proc_stat, line);

  int i = 1, ret;
  // How many items we need to read
  int done = 2;
  std::string::size_type npos = 0;
  uint64_t noise_part = 0;
  while (npos != std::string::npos) {
    switch(i) {
      case 14:
        // utime
        ret = sscanf(line.c_str() + npos, "%lu", &noise_part);
        assert(ret == 1);
        _noise += noise_part;
        done --;
        break;
      case 15:
        // stime
        ret = sscanf(line.c_str() + npos, "%lu", &noise_part);
        assert(ret == 1);
        _noise += noise_part;
        done --;
        break;
    }
    i++;
    npos = line.find_first_of(' ', npos + 1);
  }

  assert(done == 0);
}

CoreTime Measurement::read_core(const std::string &line)
{
  // Find core string
  int cpu = std::stoi(line.substr(3));

  const uint64_t clk_tck = sysconf(_SC_CLK_TCK);

  CoreTime ct(cpu);
  int i = 1, done = 1;
  std::string::size_type npos = 0;
  while (npos != std::string::npos) {
    switch(i) {
      // idle time
      case 4:
        int ret = sscanf(line.c_str() + npos, "%lu", &ct.idle);
        assert(ret == 1);
        ct.idle = ct.idle * 1000 * 1000 * 1000 / clk_tck;
        done--;
        break;
    }
    i++;
    npos = line.find_first_of(' ', npos + 1);
  }

  assert(done == 0);
  return ct;
}

void Measurement::init_cores()
{
  std::ifstream proc_stat("/proc/stat");

  std::string line;
  while (std::getline(proc_stat, line)) {
    if (line.compare(0, 3, "cpu")) {
      // All strings starting with "cpu" are consecutive
      break;
    }

    // Check if line starts as "cpu "
    if (line[3] == ' ') {
      // Skip aggregate information
      continue;
    }

    int cpu = std::stoi(line.substr(3));

    assert(cpu == static_cast<int>(_cores.size()));

    _cores.push_back(read_core(line));
  }
}
