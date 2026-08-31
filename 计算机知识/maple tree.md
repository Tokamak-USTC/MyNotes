## 功能抽象

maple tree 维护的是从**范围**到**值**的映射关系，即 `[start, end] -> value(addr)` 这种映射。`[0, ULONG_MAX]` 内部数个不重叠的区间，每个区间会对应一个 `[0, ULONG_MAX]` 上的 value，或者是NULL（即hole）。对于单个值 index，可以认为是 `[index, index] -> value` 的映射。

## 结构



## 基本操作

### Init

```c
DEFINE_MTREE(mt);
void mt_init(struct maple_tree *mt);
```

静态分配的 maple tree 可以直接用 `DEFINE_MTREE()`，动态分配的则调用 `mt_init()`。刚初始化完成时，整棵树可理解为：

```text
[0, ULONG_MAX] -> NULL
```

也就是整段地址空间一开始都是空洞（hole）。

maple tree 有两种常见模式：

- 普通 tree：分支因子更高，适合一般的区间映射。
- allocation tree：初始化时带 `MT_FLAGS_ALLOC_RANGE`，分支因子更低，但支持“找一段足够大的空闲区间”。

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
[180, 199] -> A
```

如果把 `entry` 设为 `NULL`，那么它也可以起到“删除一部分映射”的效果，即把某段范围重新打成 hole。

### Create

```c
int mtree_insert_range(struct maple_tree *mt, unsigned long first,
		       unsigned long last, void *entry, gfp_t gfp);
int mtree_insert(struct maple_tree *mt, unsigned long index,
		 void *entry, gfp_t gfp);
```

`insert` 和 `store` 的区别在于：

- `store` 是“无条件覆盖写入”。
- `insert` 是“仅当目标范围当前为 NULL 时才写入”。

如果目标范围非空，`mtree_insert()` / `mtree_insert_range()` 会返回 `-EEXIST`。所以 `insert` 更像“创建新映射”，适合需要保证“不覆盖旧值”的场景。

### Read

```c
void *mtree_load(struct maple_tree *mt, unsigned long index);
void *mt_find(struct maple_tree *mt, unsigned long *index,
	      unsigned long max);
```

`mtree_load()` 用于查某个 `index` 当前落在哪个区间上，并返回该区间的 value。

`mt_find()` 则是“从某个位置开始，向上找下一个存在的 entry”。它更像范围扫描入口，而不是点查询。

遍历相关接口包括：

- `mt_for_each()`：遍历给定范围内的所有 entry。
- `mt_next()`：从当前位置找后一个 entry。
- `mt_prev()`：从当前位置找前一个 entry。

这些接口适合把 maple tree 当成“有序区间表”来扫，而不是只做单点查找。

### Erase

```c
void *mtree_erase(struct maple_tree *mt, unsigned long index);
```

`mtree_erase()` 只需要给出**落在某个区间内部**的一个 `index`，就会把这个 index 所在的**整个映射区间**删掉，而不是只删一个点。

如果要做“局部删除”，通常不是用 `mtree_erase()`，而是用 `mtree_store_range(..., NULL, ...)` 把指定子区间重新打成 hole。

### Dup / Destroy

```c
int mtree_dup(struct maple_tree *new, struct maple_tree *old, gfp_t gfp);
void mtree_destroy(struct maple_tree *mt);
```

- `mtree_dup()` 可以高效复制整棵树，比逐项重新插入更合适。
- `mtree_destroy()` 会清空整棵 tree；如果 entry 本身是外部对象指针，要先由调用者负责释放那些对象。

### 普通 API 的锁语义

普通 API 默认帮你处理同步，读侧主要依赖 RCU，写侧主要依赖内部 `ma_lock`。从使用者角度可以记成：

- 读操作：`mtree_load()`、`mt_find()`、`mt_for_each()`、`mt_next()`、`mt_prev()`
- 写操作：`mtree_store()`、`mtree_store_range()`、`mtree_insert()`、`mtree_insert_range()`、`mtree_erase()`、`mtree_dup()`、`mtree_destroy()`

也就是说，绝大多数场景直接用普通 API 即可，不需要自己写树遍历逻辑，也不需要自己接管加锁。

## 高级操作

高级 API 的核心不是“多几个函数”，而是显式引入一个 `ma_state`（通常配套 `mas_*` 接口），把“当前游标处在哪个范围上”这个状态暴露给调用者，从而换取更高灵活性和更低重复查树开销。

### ma_state

`ma_state` 可以理解为 maple tree 上的一个“**游标 + 当前范围状态**”：常见情况下，`mas` 会先从根节点开始**逐层下探**到**叶子层**，即**顺着树枝向下移动**。如果后续继续做**相邻访问**或**范围遍历**，则会在当前状态基础上继续定位前后 entry，即在叶子层**水平移动**。其结构如下：

```c
struct ma_state {
	struct maple_tree *tree;
	unsigned long index;
	unsigned long last;
	struct maple_enode *node;
	unsigned long min;
	unsigned long max;
	struct slab_sheaf *sheaf;
	struct maple_node *alloc;
	unsigned long node_request;
	enum maple_status status;
	unsigned char depth;
	unsigned char offset;
	unsigned char mas_flags;
	unsigned char end;
	enum store_type store_type;
};
```

- `tree`：当前操作绑定的那棵 **maple tree**。
- `index`：当前 `ma_state` 关注范围的左边界。调用某些接口前，它可以表示你希望**查找/修改**的**位置/起点**；调用某些接口后，它也可能被更新为最**终命中、最终删除，或者当前落点所属范围的起点**。
- `last`：当前 `ma_state` 关注范围的右边界。它既可以表示本次操作希望覆盖到哪里，也可以在操作完成后表示**实际命中、实际删除，或者当前落点所属范围的终点**。
- `node`：当前**游标落到的树节点**。查找时通常会从根节点下探到目标位置。
- `min` / `max`：当前 `node` 所覆盖的索引边界，也就是**这个节点在树中负责的范围**。
- `sheaf` / `alloc` / `node_request`：和写路径上的节点预分配有关。`sheaf` 表示这次操作预留的一批节点，`alloc` 表示 fast path 可直接拿来用的单个节点，`node_request` 表示当前操作预计还需要多少节点。
- `status`：这个 `ma_state` 当前处于什么状态，例如起始态、活跃态、空态等。
- `depth`：写路径下当前下降到树的哪一层。
- `offset`：当前 **entry** 或 **slot** 在节点内部的**偏移位置**。
- `mas_flags`：这次状态对象上的一些附加标志位。
- `end`：当前节点内有效 slot/pivot 的结束位置。
- `store_type`：当前写操作需要走哪一种 store 路径。

### Walk / Store / Erase

- `mas_walk()`：沿树走到 `mas->index` 对应的位置，并把当前 entry 的真实范围回填到 `mas->index` / `mas->last`。
- `mas_store()`：把 `mas->index ~ mas->last` 这段范围映射到某个 entry；它会返回被覆盖掉的原 entry。
- `mas_erase()`：按 `ma_state` 中给定的范围删掉命中的 entry，并返回被删掉的旧 entry。

### 遍历与邻接访问

- `mas_for_each()`：遍历一个范围内所有 entry。
- `mas_next()`：取当前 entry 之后的下一个 entry。
- `mas_prev()`：取当前 entry 之前的上一个 entry。

### 查找

- `mas_find()`：首次调用时找“`index` 及其之后”的第一个 entry，之后连续调用会继续往后找。
- `mas_find_rev()`：首次调用时找“`last` 及其之前”的第一个 entry，之后连续调用会继续往前找。

### Pause

如果遍历或修改过程中必须临时释放锁，不能直接把旧的 `ma_state` 当成永远有效，需要先调用 `mas_pause()`。

原因是：高级 API 默认假设你自己管理锁；一旦中途让出锁，树结构可能已经变化，游标状态也就需要显式“暂停/重新衔接”。

### 查找空洞

这部分只在 allocation tree 上特别有意义：

- `mas_empty_area()`：在给定范围内，自低地址向高地址寻找一段足够大的 hole。
- `mas_empty_area_rev()`：在给定范围内，自高地址向低地址寻找一段足够大的 hole。

### 高级 API 的锁责任

高级 API 不再像普通 API 那样帮你把同步细节都藏起来。使用时要自己保证并发安全，常见保护方式包括：

- 使用 maple tree 自带的 `ma_lock`
- 使用 RCU
- 初始化时配置外部锁（例如 `MT_FLAGS_LOCK_EXTERN`）

结论上可以这样记：

- 普通 API：更安全、更省心，默认首选。
- 高级 API：更灵活、更高性能，但你要自己负责锁、游标状态和中途让锁后的恢复。
