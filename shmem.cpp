#include <signal.h>
#include <unistd.h>

#include <vector>
#include <atomic>

#include <boost/interprocess/shared_memory_object.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <boost/thread/barrier.hpp>

#include "shmem.hpp"

using namespace boost::interprocess;

class Shmem_impl
{
  bool _leader;
  int _nproc;
  shared_memory_object _shm_obj;

  boost::interprocess::mapped_region _sync_region;
  boost::interprocess::mapped_region _target_region;
  std::string _name;

  // boost::barrier *_barrier;
  std::atomic<int> *_barrier;

  int _my_idx;
  pid_t *_targets;

  const std::size_t _sync_size = 128;
  const std::size_t _target_size;
  const std::size_t _shmem_size;

  std::vector<pid_t> _partners;

public:
  Shmem_impl(bool leader, std::vector<pid_t> partners, const std::string &prefix) :
    _leader(leader), _nproc(partners.size()), _name(prefix + ".shmem"),
    _target_size(_nproc * sizeof(pid_t)), _shmem_size(_target_size + _sync_size),
    _partners(partners)
  {
    _name = "idle_stat.shmem";

    if (leader) {
      shared_memory_object::remove(_name.c_str());
      _shm_obj = shared_memory_object{create_only, _name.c_str(), read_write};

      _shm_obj.truncate(_shmem_size);

      _sync_region = mapped_region(_shm_obj, read_write, 0, _sync_size);
      _target_region = mapped_region(_shm_obj, read_write, _sync_size, _target_size);

      // _barrier = new(_sync_region.get_address()) boost::barrier(_nproc);
      _barrier = static_cast<std::atomic<int> *>(_sync_region.get_address());
      init_very_ugly_barrier();

      sleep(1);
      // Send signals
      auto my_pid = getpid();
      for (auto pid : partners) {
        if (pid != my_pid)
          kill(pid, SIGCONT);
      }

      _my_idx = 0;

    } else {
      // Wait for signal
      kill(getpid(), SIGSTOP);

      _shm_obj = shared_memory_object(open_only, _name.c_str(), read_write);

      _sync_region = mapped_region(_shm_obj, read_write, 0, _sync_size);
      _target_region = mapped_region(_shm_obj, read_write, _sync_size, _target_size);

      // _barrier = static_cast<boost::barrier*>(_sync_region.get_address());
      _barrier = static_cast<std::atomic<int> *>(_sync_region.get_address());

      // Index should always be present
      _my_idx = (std::find(partners.begin(), partners.end(), getpid()) -
                 partners.begin());

    }
  }

  std::vector<pid_t> targets()
  {
    return std::vector<pid_t>(_targets, _targets + _nproc);
  }

  ~Shmem_impl();

  void report_victims(pid_t pid);

  bool leader() { return _leader; }

  void very_ugly_barrier();
  void init_very_ugly_barrier();
};

Shmem_impl::~Shmem_impl()
{
  shared_memory_object::remove(_name.c_str());
}

void Shmem_impl::init_very_ugly_barrier()
{
  *_barrier = _nproc;
}

void Shmem_impl::very_ugly_barrier()
{
  int old = --*_barrier;

  if (old==0) {
    sleep(1);
    // Send signals
    auto my_pid = getpid();
    for (auto pid : _partners) {
      if (pid != my_pid)
        kill(pid, SIGCONT);
    }
  } else {
    kill(getpid(), SIGSTOP);
  }
}


void Shmem_impl::report_victims(pid_t pid)
{
  _targets = static_cast<pid_t*>(_target_region.get_address());
  _targets[_my_idx] = pid;

  very_ugly_barrier();
}


void Shmem::report_victims(pid_t pid)
{
  _shmem->report_victims(pid);
}

bool Shmem::leader()
{
  return _shmem->leader();
}

std::vector<pid_t> Shmem::targets()
{
  return _shmem->targets();
}

Shmem::Shmem(bool leader, std::vector<pid_t> partners, const std::string &prefix) :
  _shmem(new Shmem_impl(leader, partners, prefix))
{
}

Shmem::~Shmem() {
  delete _shmem;
}
