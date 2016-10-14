#pragma once

struct Task
{
  pid_t pid;
  int cpu;
  uint64_t size;

  Task(pid_t pid, int cpu) :
    pid(pid), cpu(cpu), size(0)
  {}
};
