// NOTE: shared_ptr 核心接口：
// 1. 构造函数
// 2. 析构函数
// 3. 拷贝构造函数
// 4. 拷贝赋值运算符
// 5. 移动构造函数
// 6. 移动赋值运算符
// 7. 解引用 / 箭头运算符
// 8. 引用计数 / 原始指针 / 重置指针

#include <atomic>

template <typename T> class shared_ptr {
public:
  // 默认构造
  shared_ptr() : ptr_(nullptr), ref_cnt_(nullptr) {};

  // 有参构造
  explicit shared_ptr(T *ptr) : ptr_(ptr) {
    if (ptr != nullptr) {
      ref_cnt_ = new std::atomic<size_t>(1);
    } else {
      ref_cnt_ = nullptr;
    }
  };

  ~shared_ptr() { release(); }

  shared_ptr(const shared_ptr<T> &other)
      : ptr_(other.ptr_), ref_cnt_(other.ref_cnt_) {
    retain();
  }

  shared_ptr(shared_ptr<T> &&other) noexcept
      : ptr_(other.ptr_), ref_cnt_(other.ref_cnt_) {
    other.ptr_ = nullptr;
    other.ref_cnt_ = nullptr;
  }

  void operator=(const shared_ptr<T> &other) {
    if (this == &other) {
      return;
    }
    release();
    ptr_ = other.ptr_;
    ref_cnt_ = other.ref_cnt_;
    retain();
  }

  void operator=(shared_ptr<T> &&other) {
    if (this == &other) {
      return;
    }
    release();
    ptr_ = other.ptr_;
    ref_cnt_ = other.ref_cnt_;
    other.ptr_ = nullptr;
    other.ref_cnt_ = nullptr;
  }

  T *operator->() { return ptr_; }

  T *get() { return ptr_; }

  void reset(T *ptr = nullptr) {
    release();
    ptr_ = ptr;
    if (ptr != nullptr) {
      ref_cnt_ = new std::atomic<size_t>(1);
    } else {
      ref_cnt_ = nullptr;
    }
  }

private:
  void retain() {
    if (ref_cnt_) {
      ref_cnt_->fetch_add(1, std::memory_order_relaxed);
    }
  }

  void release() {
    if (ref_cnt_ && ref_cnt_->fetch_sub(1, std::memory_order_acq_rel) == 1) {
      delete ptr_;
      delete ref_cnt_;
    }
    ptr_ = nullptr;
    ref_cnt_ = nullptr;
  }

  T *ptr_;
  std::atomic<size_t> *ref_cnt_;
};
