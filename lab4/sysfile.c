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


