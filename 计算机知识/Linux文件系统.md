## VFS

### 路径解析

```text
VFS: path_lookup("/home/user/a.txt")
    │
    ├─ 从根目录 dentry 开始（内核启动时建立，指向 / 的 dentry）
    │
    ├─ 解析第一分量 "home"
    │   │
    │   ├─ 调用 link_path_walk() 逐分量处理
    │   │
    │   ├─ 查 dcache：d_lookup(根目录dentry, "home")
    │   │   ├─ 计算哈希：(根目录 inode->i_ino + hash("home"))
    │   │   ├─ 查 dcache 哈希表
    │   │   │   ├─ 命中 → 直接拿到 "home" 的 dentry
    │   │   │   │   └─ 跳到 2.2
    │   │   │   └─ 未命中 → 🔴 进入磁盘查找
    │   │   │
    │   │   └─ 未命中路径：
    │   │       ├─ VFS 调用根目录 dentry->d_op->lookup() 
    │   │       │   （实际是 ext4 的 ext4_lookup）
    │   │       │
    │   │       ├─ ext4_lookup()：
    │   │       │   ├─ 获取根目录的 inode（根目录 dentry->d_inode）
    │   │       │   ├─ 读取根目录的数据块（磁盘目录项）
    │   │       │   │   └─ 遍历目录项，找文件名 "home"
    │   │       │   ├─ 找到 → 拿到 inode 号（比如 128）
    │   │       │   ├─ 调用 iget_locked(文件系统, inode号=128)
    │   │       │   │   │
    │   │       │   │   ├─ 查 inode cache 哈希表
    │   │       │   │   │   ├─ key = (super_block, 128)
    │   │       │   │   │   ├─ 命中 → 返回 struct inode *
    │   │       │   │   │   └─ 未命中 → 🔴 创建 inode！（见第三步）
    │   │       │   │   │
    │   │       │   │   └─ 返回 struct inode * (命名为 inode_home)
    │   │       │   │
    │   │       │   └─ 返回 inode_home 给 VFS
    │   │       │
    │   │       ├─ VFS 分配 struct dentry（从 dentry slab 缓存分配）
    │   │       │   ├─ d_name = "home"（拷贝字符串）
    │   │       │   ├─ d_parent = 根目录 dentry
    │   │       │   ├─ d_inode = inode_home（第三步返回的指针）
    │   │       │   ├─ d_flags = 0
    │   │       │   └─ d_count = 1
    │   │       │
    │   │       ├─ 将 dentry 加入 dcache 哈希表
    │   │       │   └─ key = (根目录 inode->i_ino, hash("home"))
    │   │       │
    │   │       └─ 返回该 dentry 给 VFS
    │   │
    │   └─ 拿到 "home" 的 dentry
    │
    └─ 继续解析下一分量
```