#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <unistd.h>
#include <assert.h>

#include <atomic>
#include <thread>
#include <mutex>
#include <vector>
#include <functional>
#include <condition_variable>
#include <type_traits>

#include "mpmc.hpp"

template<class Func,class ... Args>
inline auto forward_as_lambda(Func &&func,Args &&...args) {
  return [f=std::forward<decltype(func)>(func)
    ,tup=std::tuple<std::conditional_t<std::is_lvalue_reference_v<Args>,Args,std::remove_reference_t<Args>>...>(std::forward<decltype(args)>(args)...)
  ]() mutable {
    return std::apply(std::move(f),std::move(tup));
  };

}

template<class Func, class T, class ... Args>
inline auto forward_as_lambda2(Func &&func, T &&first, Args &&...args) {
  return [f=std::forward<decltype(func)>(func)
    ,tt=std::forward<decltype(first)>(first)
    ,tup=std::tuple<std::conditional_t<std::is_lvalue_reference_v<Args>,Args,std::remove_reference_t<Args>>...>(std::forward<decltype(args)>(args)...)
  ]() mutable {
    return std::apply(std::move(f)
        ,std::tuple_cat(std::forward_as_tuple(tt), std::move(tup))
       );
  };
}

class Thread_pool {

  std::vector<std::thread> threads;
  mpmc<std::function<void(void)>> queue;

  std::atomic<int>        jobs_left;
  std::atomic<bool>       finishing;

  size_t thread_count;

  std::condition_variable job_available_var;
  std::mutex              queue_mutex;

  void task() {
    while( !finishing) {
      while( !queue.empty() ) {
        next_job()();
      }
    }
  }

  std::function<void(void)> next_job() {
    std::function<void(void)> res;
    std::unique_lock<std::mutex> job_lock( queue_mutex );

    // Wait for a job if we don't have any.
    job_available_var.wait( job_lock, [this]() ->bool { return !queue.empty() || finishing; } );

    bool has_work = queue.dequeue(res);
    if (has_work) {
      jobs_left--;
      return res;
    }

    return []{}; // Nothing to do
  }

  void add_( std::function<void(void)> job ) {
    if (jobs_left>48) {
      job();
      return;
    }
    jobs_left++;
    queue.enqueue( job );
    job_available_var.notify_one();
  }

  public:
  Thread_pool(int _thread_count=0)
    : queue(64)
      , jobs_left( 0 )
      , finishing( false ) {

    thread_count = _thread_count;
    int lim = (std::thread::hardware_concurrency()-1)/2;

    if (thread_count > lim)
      thread_count = lim;
    else if (thread_count<1)
      thread_count = 1;

    assert(thread_count);

    for( unsigned i = 0; i < thread_count; ++i )
      threads.push_back(std::thread( [this]{ this->task(); } ));
  }

  ~Thread_pool() {
    wait_all();

    finishing = true;
    job_available_var.notify_all();

    for( auto &x : threads )
      if( x.joinable() )
        x.join();
  }

  inline unsigned size() const {
    return thread_count;
  }

  template<class Func,class ... Args>
    void add(Func &&func, Args &&...args) {
      return add_(forward_as_lambda(std::forward<decltype(func)>(func),std::forward<decltype(args)>(args)...));
    }
#if 1
  template<class Func,class T, class ... Args>
    void add(Func &&func, T *first, Args &&...args) {
      return add_(forward_as_lambda2(
             std::forward<decltype(func)>(func)
            ,std::forward<decltype(first)>(first)
            ,std::forward<decltype(args)>(args)...));
    }
#endif

  void wait_all() {
    while(!queue.empty()) {
      std::function<void(void)> res;
      bool has_work = queue.dequeue(res);
      if (has_work) {
        jobs_left--;
        res();
      }
    }
    assert(jobs_left==0);
  }
};

#endif
