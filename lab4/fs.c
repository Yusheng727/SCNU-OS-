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
