#pragma once

class Shmem_impl;

class Shmem
{
  Shmem_impl *_shmem;

public:
  Shmem(bool leader, std::vector<pid_t> partners, const std::string &prefix);

  void report_victims(pid_t pid);

  bool leader();

  std::vector<pid_t> targets();

  ~Shmem();
};
