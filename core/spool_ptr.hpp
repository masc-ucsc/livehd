#pragma once

#include <memory>

#include "iassert.hpp"
#include "spsc.hpp"

template <class T>
class spool_ptr_pool {
private:
  spsc256<T*> _pointer_queue;

public:
  explicit spool_ptr_pool() {
    T* raw_ptr            = new T();
    raw_ptr->shared_count = 1;
    bool something        = _pointer_queue.enqueue(raw_ptr);
    I(something);
  }

  ~spool_ptr_pool() {
    while (!_pointer_queue.empty()) {
      auto raw_ptr = _pointer_queue.dequeue();
      I(raw_ptr);
      I(*raw_ptr);
      delete *raw_ptr;
    }
  }

  T* get_ptr() {
    auto raw_retval = _pointer_queue.dequeue();
    if (!raw_retval) {
      T* raw_ptr            = new T();
      raw_ptr->shared_count = 1;
      return raw_ptr;
    }
    I(raw_retval);
    I(*raw_retval);
    (*raw_retval)->shared_count = 1;
    return *raw_retval;
  }

  void release_ptr(T* to_release) {
    I(to_release->shared_count == 1);
    bool fits = _pointer_queue.enqueue(to_release);
    if (fits) {
      return;
    }
    delete to_release;
  }
};

template <class T>
class spool_ptr {
public:
  spool_ptr() noexcept = default;

  template <class... Args>
  static spool_ptr<T> make(Args&&... args) {
    auto retval = pool.get_ptr();
    retval->reconstruct(std::forward<Args>(args)...);
    I(retval->shared_count == 1);
    return spool_ptr<T>(retval);
  }

  static spool_ptr<T> make() {
    auto retval = pool.get_ptr();
    I(retval->shared_count == 1);
    return spool_ptr<T>(retval);
  }

  spool_ptr(const spool_ptr& sp) noexcept : data_(sp.data_) {
    I(data_);
    ++data_->shared_count;
  }

  template <class D>
  spool_ptr(const spool_ptr<D>& sp) noexcept : data_(sp.data_) {
    I(data_);
    ++data_->shared_count;
  }

  spool_ptr(spool_ptr&& sp) noexcept {
    swap(sp);
    sp.reset();
  }

  ~spool_ptr() {
    if (data_) {
      dec_shared_count();
    }
  }

  spool_ptr& operator=(const spool_ptr& sp) noexcept {
    spool_ptr tmp(sp);
    swap(tmp);
    return *this;
  }

  spool_ptr& operator=(spool_ptr&& sp) noexcept {
    swap(sp);
    sp.reset();
    return *this;
  }

  T* get() const noexcept { return data_; }

  T* operator->() const noexcept { return data_; }

  T& operator*() const noexcept {
    I(*this);
    return *data_;
  }

  explicit operator bool() const noexcept { return data_ != nullptr; }

  void reset() noexcept {
    if (data_ == nullptr) {
      return;
    }
    dec_shared_count();
    data_ = nullptr;
  }

private:
  spool_ptr(T* data) noexcept : data_(data) { I(data_); }

  void swap(spool_ptr& sp) noexcept { std::swap(data_, sp.data_); }

  void dec_shared_count() {
    I(data_);

    if (data_->shared_count <= 1) {
      pool.release_ptr(data_);
      data_ = nullptr;
    } else {
      --data_->shared_count;
    }
  }

  T* data_ = nullptr;

  static inline thread_local spool_ptr_pool<T> pool;
};
