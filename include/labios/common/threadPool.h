/*
 * Copyright (C) 2019  SCS Lab <scs-help@cs.iit.edu>, Hariharan
 * Devarajan <hdevarajan@hawk.iit.edu>, Anthony Kougkas
 * <akougkas@iit.edu>, Xian-He Sun <sun@iit.edu>
 *
 * This file is part of Labios
 *
 * Labios is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program.  If not, see
 * <http://www.gnu.org/licenses/>.
 */
/*******************************************************************************
 * Created by akougkas on 6/30/18.
 ******************************************************************************/
#ifndef LABIOS_THREAD_POOL_H
#define LABIOS_THREAD_POOL_H
/******************************************************************************
 *include files
 ******************************************************************************/
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <utility>
#include <vector>
/******************************************************************************
 *Class
 ******************************************************************************/
class threadPool {
private:
  /*******************
   *Helper subclasses
   *******************/
  template <typename T> class SafeQueue {
  private:
    std::queue<T> m_queue;
    std::mutex m_mutex;

  public:
    SafeQueue() = default;
    SafeQueue(SafeQueue &other) {}
    virtual ~SafeQueue() {}

    bool empty() {
      std::unique_lock<std::mutex> lock(m_mutex);
      return m_queue.empty();
    }

    int size() {
      std::unique_lock<std::mutex> lock(m_mutex);
      return m_queue.size();
    }

    void enqueue(T &t) {
      std::unique_lock<std::mutex> lock(m_mutex);
      m_queue.push(t);
    }

    bool dequeue(T &t) {
      std::unique_lock<std::mutex> lock(m_mutex);
      if (m_queue.empty())
        return false;
      t = std::move(m_queue.front());
      m_queue.pop();
      return true;
    }
  };

  class ThreadWorker {
  private:
    int m_id;
    threadPool *m_pool;

  public:
    ThreadWorker(threadPool *pool, const int id) : m_pool(pool), m_id(id) {}

    void operator()() {
      std::function<void()> func;
      bool dequeued;
      while (!m_pool->m_shutdown) {
        {
          std::unique_lock<std::mutex> lock(m_pool->m_conditional_mutex);
          if (m_pool->m_queue.empty()) {
            m_pool->m_conditional_lock.wait(lock);
          }
          dequeued = m_pool->m_queue.dequeue(func);
        }
        if (dequeued) {
          func();
        }
      }
    }
  };
  /******************************************************************************
   *Variables and members
   ******************************************************************************/
  bool m_shutdown;
  SafeQueue<std::function<void()>> m_queue;
  std::vector<std::thread> m_threads;
  std::mutex m_conditional_mutex;
  std::condition_variable m_conditional_lock;

public:
  /******************************************************************************
   *Constructor
   ******************************************************************************/
  explicit threadPool(const int n_threads)
      : m_threads(std::vector<std::thread>(n_threads)), m_shutdown(false) {}
  threadPool(const threadPool &) = delete;
  threadPool(threadPool &&) = delete;
  threadPool &operator=(const threadPool &) = delete;
  threadPool &operator=(threadPool &&) = delete;
  /******************************************************************************
   *Interface
   ******************************************************************************/
  // Inits thread pool
  void init() {
    for (int i = 0; i < m_threads.size(); ++i) {
      m_threads[i] = std::thread(ThreadWorker(this, i));
    }
  }

  // Waits until threads finish their current task and shutdowns the pool
  void shutdown() {
    m_shutdown = true;
    m_conditional_lock.notify_all();
    for (int i = 0; i < m_threads.size(); ++i) {
      if (m_threads[i].joinable()) {
        m_threads[i].join();
      }
    }
  }

  // Submit a function to be executed asynchronously by the pool
  template <typename F, typename... Args>
  auto submit(F &&f, Args &&...args) -> std::future<decltype(f(args...))> {
    // Create a function with bounded parameters ready to execute
    std::function<decltype(f(args...))()> func =
        std::bind(std::forward<F>(f), std::forward<Args>(args)...);
    // Encapsulate it into a shared ptr in order to be able to copy construct /
    // assign
    auto task_ptr =
        std::make_shared<std::packaged_task<decltype(f(args...))()>>(func);
    // Wrap packaged task into void function
    std::function<void()> wrapper_func = [task_ptr]() { (*task_ptr)(); };
    // Enqueue generic wrapper function
    m_queue.enqueue(wrapper_func);
    // Wake up one thread if its waiting
    m_conditional_lock.notify_one();
    // Return future from promise
    return task_ptr->get_future();
  }
};

#endif // LABIOS_THREAD_POOL_H
