#pragma once

#include <iostream>

struct Env
{
  int ncpu;
  int sleep;

  pid_t victim;

  bool debug;

  std::string log_prefix;
  std::string scheduler;

  Env(int argc, char **argv);
  Env() : initialized(false) {}

  static const Env &env(int argc = -1, char **argv = NULL);
  std::ostream &operator<<(std::ostream &os);
private:
  bool initialized;
};

