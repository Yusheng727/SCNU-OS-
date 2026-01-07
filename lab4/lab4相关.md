# 操作系统 Lab 4 (File System) 代码汇总

本文档汇总了在 xv6 操作系统 Lab 4 实验中修改和新增的所有关键代码片段。这些代码涵盖了“大文件支持”和“符号链接”两个任务。

## 任务一：大文件支持 (Large Files)

### 1. `kernel/fs.h`

修改了 `NDIRECT` 宏定义，调整了 `dinode` 结构体，并更新了 `MAXFILE`。

```
#define NDIRECT 11                // 修改为 11 (原为 12)
#define NINDIRECT (BSIZE / sizeof(uint))
// 更新 MAXFILE 计算公式，增加二级间接块的容量
#define MAXFILE (NDIRECT + NINDIRECT + NINDIRECT * NINDIRECT)

struct dinode {
  short type;           // File type
  short major;          // Major device number (T_DEVICE only)
  short minor;          // Minor device number (T_DEVICE only)
  short nlink;          // Number of links to inode in file system
  uint size;            // Size of file (bytes)
  // 修改数组大小为 NDIRECT + 2 (11 + 1 + 1 = 13)，保持结构体总大小不变
  uint addrs[NDIRECT+2];
};
```

### 2. `kernel/file.h`

同步修改内存中的 `inode` 结构体。

```
struct inode {
  uint dev;           // Device number
  uint inum;          // Inode number
  int ref;            // Reference count
  struct sleeplock lock; // protects everything below here
  int valid;          // inode has been read from disk?

  short type;         // copy of disk inode
  short major;
  short minor;
  short nlink;
  uint size;
  // 同步修改为 NDIRECT + 2
  uint addrs[NDIRECT+2];
};
```

### 3. `kernel/fs.c`

这是任务一的核心，修改了 `bmap` 和 `itrunc` 函数。

#### 修改后的 `bmap` 函数：

```
static uint
bmap(struct inode *ip, uint bn)
{
  uint addr, *a;
  struct buf *bp;

  // 1. 直接块 (Direct Blocks)
  if(bn < NDIRECT){
    if((addr = ip->addrs[bn]) == 0){
      addr = balloc(ip->dev);
      if(addr == 0)
        return 0;
      ip->addrs[bn] = addr;
    }
    return addr;
  }
  bn -= NDIRECT;

  // 2. 一级间接块 (Singly Indirect Block)
  if(bn < NINDIRECT){
    // Load indirect block, allocating if necessary.
    if((addr = ip->addrs[NDIRECT]) == 0){
      addr = balloc(ip->dev);
      if(addr == 0)
        return 0;
      ip->addrs[NDIRECT] = addr;
    }
    bp = bread(ip->dev, addr);
    a = (uint*)bp->data;
    if((addr = a[bn]) == 0){
      addr = balloc(ip->dev);
      if(addr){
        a[bn] = addr;
        log_write(bp);
      }
    }
    brelse(bp);
    return addr;
  }
  bn -= NINDIRECT;

  // 3. 二级间接块 (Doubly Indirect Block) - 新增逻辑
  if(bn < NINDIRECT * NINDIRECT){
    // 获取或分配二级索引块
    if((addr = ip->addrs[NDIRECT + 1]) == 0){
      addr = balloc(ip->dev);
      if(addr == 0) return 0;
      ip->addrs[NDIRECT + 1] = addr;
    }
    bp = bread(ip->dev, addr);
    a = (uint*)bp->data;
    
    // 获取或分配中间的一级索引块
    if((addr = a[bn / NINDIRECT]) == 0){
       addr = balloc(ip->dev);
       if(addr == 0){
         brelse(bp);
         return 0;
       }
       a[bn / NINDIRECT] = addr;
       log_write(bp);
    }
    brelse(bp); // 释放二级块

    // 读取一级块并获取最终数据块
    bp = bread(ip->dev, addr);
    a = (uint*)bp->data;
    
    if((addr = a[bn % NINDIRECT]) == 0){
      addr = balloc(ip->dev);
      if(addr == 0){
        brelse(bp);
        return 0;
      }
      a[bn % NINDIRECT] = addr;
      log_write(bp);
    }
    brelse(bp);
    return addr;
  }

  panic("bmap: out of range");
}
```

#### 修改后的 `itrunc` 函数：

```
void
itrunc(struct inode *ip)
{
  int i, j, k;
  struct buf *bp;
  uint *a;

  // 1. 释放直接块
  for(i = 0; i < NDIRECT; i++){
    if(ip->addrs[i]){
      bfree(ip->dev, ip->addrs[i]);
      ip->addrs[i] = 0;
    }
  }

  // 2. 释放一级间接块
  if(ip->addrs[NDIRECT]){
    bp = bread(ip->dev, ip->addrs[NDIRECT]);
    a = (uint*)bp->data;
    for(j = 0; j < NINDIRECT; j++){
      if(a[j])
        bfree(ip->dev, a[j]);
    }
    brelse(bp);
    bfree(ip->dev, ip->addrs[NDIRECT]);
    ip->addrs[NDIRECT] = 0;
  }

  // 3. 释放二级间接块 - 新增逻辑
  if(ip->addrs[NDIRECT + 1]){
    bp = bread(ip->dev, ip->addrs[NDIRECT+1]);
    a = (uint*)bp->data;
    for(j = 0; j < NINDIRECT; j++){
       if(a[j]){
          struct buf *bp2 = bread(ip->dev, a[j]);
          uint *a2 = (uint*)bp2->data;
          for(k = 0; k < NINDIRECT; k++){
             if(a2[k]) bfree(ip->dev, a2[k]);
          }
          brelse(bp2);
          bfree(ip->dev, a[j]);
       }
    }
    brelse(bp);
    bfree(ip->dev, ip->addrs[NDIRECT+1]);
    ip->addrs[NDIRECT+1] = 0;
  }

  ip->size = 0;
  iupdate(ip);
}
```

## 任务二：符号链接 (Symbolic Links)

### 1. `Makefile`

将测试程序 `_symlinktest` 加入编译列表。

```
UPROGS=\
    $U/_cat\
    # ... 其他程序 ...
    $U/_zombie\
    $U/_bigfile\      # 任务一的测试程序
    $U/_symlinktest\  # 任务二的测试程序
```

### 2. `kernel/stat.h`

新增文件类型定义。

```
#define T_DIR     1   // Directory
#define T_FILE    2   // File
#define T_DEVICE  3   // Device
#define T_SYMLINK 4   // Symbolic Link (新增)
```

### 3. `kernel/fcntl.h`

新增文件打开标志位。

```
#define O_RDONLY  0x000
#define O_WRONLY  0x001
#define O_RDWR    0x002
#define O_CREATE  0x200
#define O_TRUNC   0x400
#define O_NOFOLLOW 0x800 // 新增 (注意是字母 O 不是数字 0)
```

### 4. 系统调用注册相关文件

这部分是为了让用户程序能调用 `symlink`。

- **`kernel/syscall.h`**:

  ```
  #define SYS_symlink 22
  ```

- **`kernel/syscall.c`**:

  ```
  extern uint64 sys_symlink(void);
  static uint64 (*syscalls[])(void) = {
    // ...
    [SYS_symlink]   sys_symlink,
  };
  ```

- **`user/usys.pl`**:

  ```
  entry("symlink");
  ```

- **`user/user.h`**:

  ```
  int symlink(char*, char*);
  ```

### 5. `kernel/sysfile.c`

任务二的核心逻辑实现。

#### 新增 `sys_symlink` 函数：

```
uint64
sys_symlink(void)
{
  char target[MAXPATH], path[MAXPATH];
  struct inode *ip;

  if(argstr(0, target, MAXPATH) < 0 || argstr(1, path, MAXPATH) < 0)
    return -1;

  begin_op();
  // 创建类型为 T_SYMLINK 的新 inode
  if((ip = create(path, T_SYMLINK, 0, 0)) == 0){
    end_op();
    return -1;
  }
  // 将目标路径写入 inode 的数据块
  if(writei(ip, 0, (uint64)target, 0, MAXPATH) < MAXPATH){
    iunlockput(ip);
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}
```

#### 修改 `sys_open` 函数 (处理符号链接跟随)：

```
uint64
sys_open(void)
{
  // ... (参数获取逻辑不变) ...

  if(omode & O_CREATE){
    // ... (Create 逻辑不变) ...
  } else {
    if((ip = namei(path)) == 0){
      end_op();
      return -1;
    }
    ilock(ip);

    // === 新增：处理符号链接 ===
    if(ip->type == T_SYMLINK && !(omode & O_NOFOLLOW)){
      int depth = 0;
      // 循环跟随链接，直到找到非链接文件或达到最大深度
      while(ip->type == T_SYMLINK){
        if(depth >= 10){ // 防止死循环
          iunlockput(ip);
          end_op();
          return -1;
        }
        // 读取链接内容
        char target[MAXPATH];
        if(readi(ip, 0, (uint64)target, 0, MAXPATH) < 0){
          iunlockput(ip);
          end_op();
          return -1;
        }
        iunlockput(ip); // 释放当前 inode
        
        // 查找下一个
        if((ip = namei(target)) == 0){
          end_op();
          return -1;
        }
        ilock(ip);
        depth++;
      }
    }
    // ========================

    // ... (后续逻辑不变) ...
  }

  // ... (文件分配逻辑不变) ...
}
```