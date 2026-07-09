## 数据结构抽象

maple tree 维护的是从**范围**到**值**的映射关系，即 `[start, end] -> value(addr)` 这种映射。`[0, ULONG_MAX]` 内部数个不重叠的区间，每个区间会对应一个 `[0, ULONG_MAX]` 上的 value，或者是NULL（即hole）。对于单个值 index，可以认为是 `[index, index] -> value` 的映射。

## 基本操作

### Init

### Set

```c
int mtree_store_range(struct maple_tree *mt, unsigned long first,
		      unsigned long last, void *entry, gfp_t gfp);
int mtree_store(struct maple_tree *mt, unsigned long index,
		void *entry, gfp_t gfp);
```

`mtree_store_range()` 的语义是：把 `[first, last]` 这个范围映射到 `entry`，必要时会把相关的旧区间拆开，缩短，删除或覆盖。`mtree_store()` 是 `mtree_store_range()` 的包装，相当于 `first` 和 `last` 都是 `index`。例如：原来有 `[100, 199] -> A`，然后使用`mtree_store_range()` 映射 `[120, 179] -> B`，那么结果会是：

```
[100, 119] -> A
[120, 179] -> B
[180, 200] -> A
```

### Create

### Read