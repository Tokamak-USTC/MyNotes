#include <cassert>
#include <cstddef>
#include <utility>

/*
结论先行：
1. 手撕 vector 的核心不是“会写动态数组”，而是维护好三段区间：
   [begin_, end_)      已构造对象
   [end_, cap_)        已分配但未构造的原始内存
2. size() 和 capacity() 是两回事：
   capacity 表示最多能放多少个 T，size 表示当前真的构造了多少个 T。
3. 扩容不是 memcpy 字节，而是：
   分配新原始内存 -> 用 placement new 逐个构造 -> 销毁旧对象 -> 释放旧内存。
4. 这个版本是“面试/学习版”：
   覆盖 Rule of Five、push_back/emplace_back/reserve/clear/pop_back 的主干机制，
   用 operator new / placement new 展示对象生命周期，不展开工业级异常安全细节。
*/

template <class T> class my_vector {
public:
  my_vector() = default;

  ~my_vector() {
    clear();
    deallocate();
  }

  my_vector(const my_vector &other) {
    reserve(other.size());
    for (size_t i = 0; i < other.size(); ++i) {
      new (begin_ + i) T(other.begin_[i]);
    }
    end_ = begin_ + other.size();
  }

  my_vector &operator=(const my_vector &other) {
    if (this == &other) {
      return *this;
    }
    my_vector tmp(other);
    swap(tmp);
    return *this;
  }

  my_vector(my_vector &&other) noexcept { steal_from(other); }

  my_vector &operator=(my_vector &&other) noexcept {
    if (this == &other) {
      return *this;
    }
    clear();
    deallocate();
    steal_from(other);
    return *this;
  }

  size_t size() const { return static_cast<size_t>(end_ - begin_); }

  size_t capacity() const { return static_cast<size_t>(cap_ - begin_); }

  bool empty() const { return begin_ == end_; }

  T &operator[](size_t i) {
    assert(i < size());
    return begin_[i];
  }

  const T &operator[](size_t i) const {
    assert(i < size());
    return begin_[i];
  }

  T *data() { return begin_; }

  const T *data() const { return begin_; }

  void push_back(const T &value) {
    ensure_capacity_for_one_more();
    new (end_) T(value);
    ++end_;
  }

  void push_back(T &&value) {
    ensure_capacity_for_one_more();
    new (end_) T(std::move(value));
    ++end_;
  }

  template <class... Args> T &emplace_back(Args &&...args) {
    ensure_capacity_for_one_more();
    new (end_) T(std::forward<Args>(args)...);
    ++end_;
    return *(end_ - 1);
  }

  void pop_back() {
    assert(!empty());
    --end_;
    end_->~T();
  }

  void clear() {
    while (end_ != begin_) {
      --end_;
      end_->~T();
    }
  }

  void reserve(size_t new_cap) {
    if (new_cap <= capacity()) {
      return;
    }

    T *new_begin = static_cast<T *>(::operator new(sizeof(T) * new_cap));

    for (size_t i = 0; i < size(); ++i) {
      new (new_begin + i) T(std::move(begin_[i]));
    }

    size_t old_size = size();
    clear();
    deallocate();

    begin_ = new_begin;
    end_ = new_begin + old_size;
    cap_ = new_begin + new_cap;
  }

  void swap(my_vector &other) noexcept {
    std::swap(begin_, other.begin_);
    std::swap(end_, other.end_);
    std::swap(cap_, other.cap_);
  }

private:
  void ensure_capacity_for_one_more() {
    if (end_ != cap_) {
      return;
    }
    size_t old_cap = capacity();
    size_t new_cap = old_cap == 0 ? 1 : old_cap * 2;
    reserve(new_cap);
  }

  void deallocate() {
    if (begin_ != nullptr) {
      ::operator delete(begin_);
    }
    begin_ = nullptr;
    end_ = nullptr;
    cap_ = nullptr;
  }

  void steal_from(my_vector &other) noexcept {
    begin_ = other.begin_;
    end_ = other.end_;
    cap_ = other.cap_;
    other.begin_ = nullptr;
    other.end_ = nullptr;
    other.cap_ = nullptr;
  }
  // 三个指针对应 vector 最核心的不变量：
  // begin_ <= end_ <= cap_
  T *begin_ = nullptr;
  T *end_ = nullptr;
  T *cap_ = nullptr;
};

/*
几个最容易写错的点：

1. operator new 和 new[] 不是一回事
   operator new 只分配原始内存，不会自动调用 T 的构造函数。
   placement new 才是在某个地址上真正构造对象。

2. 不能把 size 和 capacity 混起来
   reserve(10) 之后只是拿到能放 10 个 T 的原始内存，不代表已经有 10 个对象。

3. clear() 只销毁对象，不释放容量
   这也是 vector 清空后 capacity 往往还在的原因。

4. move 之后旧对象仍然要析构
   “被 move 走”不等于“不需要析构”，只是其资源所有权可能已经转移。

5. 这个版本为了讲清主线，省略了严格异常安全
   真正的 std::vector 在扩容迁移时不会像这里这样直接裸写一段 for，
   它会处理“构造到一半抛异常时怎么回滚”的问题。

6. 面试里如果被追问 insert/erase
   本质上是在已构造区间里搬移对象，需要区分尾插和中间插入的构造/赋值顺序。
*/
