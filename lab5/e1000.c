int
e1000_transmit(char *buf, int len)
{
  acquire(&e1000_lock); // 获取驱动锁

  // 1. 获取下一个可用的发送描述符索引 (TDT)
  uint32 idx = regs[E1000_TDT];

  // 2. 检查环是否溢出 (检查该位置的描述符状态)
  // 如果 E1000_TXD_STAT_DD 没有被设置，说明之前的数据还没发完
  if((tx_ring[idx].status & E1000_TXD_STAT_DD) == 0){
    release(&e1000_lock);
    return -1; // 环满了
  }

  // 3. 释放旧的 buffer (如果这个位置之前有数据发完了)
  if(tx_bufs[idx]){
    kfree(tx_bufs[idx]);
  }

  // 4. 填入新的描述符信息
  tx_ring[idx].addr = (uint64)buf;
  tx_ring[idx].length = (uint16)len;
  // CMD_EOP: End of Packet, CMD_RS: Report Status
  tx_ring[idx].cmd = E1000_TXD_CMD_EOP | E1000_TXD_CMD_RS;

  // 5. 保存 buffer 指针以便后续释放
  tx_bufs[idx] = buf;

  // 6. 更新 TDT 指针 (注意取模)
  regs[E1000_TDT] = (idx + 1) % TX_RING_SIZE;

  release(&e1000_lock);
  return 0;
}



static void
e1000_recv(void)
{
  acquire(&e1000_lock);
  
  // RDT 指向硬件可用的最后一个位置，所以软件要处理的是 RDT + 1
  uint32 idx = (regs[E1000_RDT] + 1) % RX_RING_SIZE;

  // 循环处理所有已到达的数据包 (只要 DD 位为 1)
  while(rx_ring[idx].status & E1000_RXD_STAT_DD){
    
    char *buf = rx_bufs[idx]; 
    int len = rx_ring[idx].length;

    // 分配新内存替换旧的
    char *new_buf = kalloc(); 
    if(new_buf == 0) {
      panic("e1000_recv: kalloc failed");
    }

    // 更新环描述符：把新的空盘子放上去
    rx_bufs[idx] = new_buf;
    rx_ring[idx].addr = (uint64)new_buf;
    rx_ring[idx].status = 0;

    // 更新 RDT，告诉网卡这个位置处理完了
    regs[E1000_RDT] = idx;

    // ==========================================
    // [关键防死锁逻辑]
    // 在调用 net_rx 之前释放锁，防止 net_rx 内部调用 transmit 导致死锁
    // ==========================================
    release(&e1000_lock);

    // 向上层传递数据包
    if(buf){
       net_rx(buf, len); 
    }

    // 重新获取锁，准备处理下一个包
    acquire(&e1000_lock);

    idx = (idx + 1) % RX_RING_SIZE;
  }

  release(&e1000_lock);
}


