#pragma once

#include <iostream>

struct CpuList {
public:
  CpuList() {}
  CpuList(const std::string &cpu_string);

  std::vector<int> cpus;
};

struct Env
{
  int sleep;

  pid_t victim;

  bool debug;

  std::string log_prefix;
  std::string scheduler;

  CpuList cpu_list;

  Env(int argc, char **argv);
  Env() : initialized(false) {}

  static const Env &env(int argc = -1, char **argv = NULL);
  std::ostream &operator<<(std::ostream &os);
private:
  bool initialized;
};

