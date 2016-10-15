#include <sys/resource.h>
#include <unistd.h>
#include <dirent.h>

#include <cstring>
#include <iostream>
#include <string>
#include <sstream>
#include <stdexcept>
#include <algorithm>

#include <vector>
#include <cassert>

#include "scheduler.hpp"
#include "measurement.hpp"


// Static methods

static void schedule_set_priority(pid_t pid, int priority)
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



static void rise_priority()
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

static void drop_priority()
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

// class Scheduler

void Scheduler::loop(const std::vector<pid_t> &pids, std::ofstream &log)
{
  set_own_affinity();

  // Main loop
  std::vector<Measurement> data;

  // Allow victims to initialize themselves.
  sleep(Env::env().sleep);

  // Set initial affinity
  std::vector<Task> tasks = this->initial_distribution(pids);

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
      this->do_scheduling(tasks);
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

// Tracer

std::vector<Task> Tracer::initial_distribution(const std::vector<pid_t> &pids)
{
  std::vector<Task> tasks;
  for (auto pid : pids)
    tasks.push_back(Task{pid, 0});
  return tasks;
}

// DefaultScheduler

void DefaultScheduler::do_new_assignment(std::vector<Task> &schedule)
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
      pid_t pid = schedule[i].pid;
      schedule_set_priority(schedule[i].pid, prio);
      for (const auto &child : children.at(pid)) {
        schedule_set_priority(child, prio);
      }
      i++;
      prio++;
    } while ((i < schedule.size()) &&
             (schedule[i].cpu == schedule[i-1].cpu));
  }
}

std::set<pid_t> DefaultScheduler::find_children(pid_t pid)
{
  // Open the /proc directory
  std::set<pid_t> res;
  std::string task_path = std::string("/proc/") + std::to_string(pid) + "/task";

  DIR *task_dir = opendir(task_path.c_str());
  if (task_dir != NULL) {
    // Enumerate all entries in directory until process found
    struct dirent *task_entry;
    while ((task_entry = readdir(task_dir))) {
      std::string line;

      pid_t child = atoi(task_entry->d_name);
      if (child <= 0) {
        // Either '.' or '..'
        continue;
      }
      res.insert(child);


      std::string children_path = task_path + "/" + task_entry->d_name + "/children";
      std::getline(std::ifstream(children_path), line);
      std::stringstream iss(line);
      while (!iss.eof()) {
        iss >> child;
        res.insert(child);
      }
    }
  }

  closedir(task_dir);
  return res;
}

auto DefaultScheduler::find_children(const std::vector<pid_t> &pids) ->
  ChildrenMap
{
  ChildrenMap res;

  for (auto pid : pids) {
    res[pid] = find_children(pid);
  }

  return res;
}

std::vector<Task> DefaultScheduler::schedule_rr(const std::vector<pid_t> & pids)
{
  std::vector<Task> tasks;
  int cpu = 0;
  for (auto pid : pids) {
    tasks.push_back(Task{pid, cpu});
    set_affinity(pid, cpu);
    schedule_set_priority(pid, 1);
    cpu = (cpu + 1) % Env::env().ncpu;
  }

  return tasks;
}

auto DefaultScheduler::initial_distribution(const std::vector<pid_t> &pids) ->
  std::vector<Task>
{
  children = find_children(pids);
  return schedule_rr(pids);
}

void DefaultScheduler::do_scheduling(std::vector<Task> &tasks)
{
  std::vector<uint64_t> load(Env::env().ncpu);

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

std::unique_ptr<Scheduler> create_scheduler(const Env &env)
{
  if (env.only_tracing)
    return std::unique_ptr<Scheduler>(new Tracer(env));
  else
    return std::unique_ptr<Scheduler>(new DefaultScheduler(env));
}
