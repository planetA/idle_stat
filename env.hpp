#pragma once

#include <iostream>

struct Env
{
  int ncpu;
  std::string log_prefix;
  pid_t victim;
  bool only_tracing;
  bool debug;

  Env(int argc, char **argv);
  Env() : initialized(false) {}

  static const Env &env(int argc = -1, char **argv = NULL);
  std::ostream &operator<<(std::ostream &os);
private:
  bool initialized;
};

