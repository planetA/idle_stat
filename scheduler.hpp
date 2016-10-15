#pragma once

#include <vector>
#include <map>
#include <set>
#include <memory>
#include <fstream>

#include "env.hpp"
#include "tasks.hpp"

class Scheduler
{

public:
  void loop(const std::vector<pid_t> &pids, std::ofstream &log);

  Scheduler(const Env& env) { (void) env; };
  virtual ~Scheduler() {};

  virtual std::vector<Task> initial_distribution(const std::vector<pid_t> &pids) = 0;
  virtual void do_scheduling(std::vector<Task> &tasks) = 0;

  static std::unique_ptr<Scheduler> create(const Env &env);
};

class Tracer : public Scheduler
{
public:
  Tracer(const Env& env) : Scheduler(env) { (void) env; }
  ~Tracer() {};

  std::vector<Task> initial_distribution(const std::vector<pid_t> &pids);
  void do_scheduling(std::vector<Task> &tasks){ (void) tasks; }
};

class PinnedRR : public Scheduler
{
protected:

  std::vector<Task> schedule_rr(const std::vector<pid_t> & pids);

public:
  PinnedRR(const Env& env) : Scheduler(env) { (void) env; }
  ~PinnedRR() {}

  std::vector<Task> initial_distribution(const std::vector<pid_t> &pids);

  void do_scheduling(std::vector<Task> &tasks){ (void) tasks; }

};

class DefaultScheduler : public PinnedRR
{
protected:
  typedef std::map<pid_t, std::set<pid_t> > ChildrenMap;

  ChildrenMap children;

private:

  void do_new_assignment(std::vector<Task> &schedule);
  std::set<pid_t> find_children(pid_t pid);

protected:
  ChildrenMap find_children(const std::vector<pid_t> &pids);

  std::vector<Task> schedule_rr(const std::vector<pid_t> & pids);

public:
  DefaultScheduler(const Env& env) : PinnedRR(env) { (void) env; }
  ~DefaultScheduler() {}

  std::vector<Task> initial_distribution(const std::vector<pid_t> &pids);

  void do_scheduling(std::vector<Task> &tasks);

};
