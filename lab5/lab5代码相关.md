操作系统课程设计 Lab 5 代码汇总实验内容: Network Driver & UDP Socket以下代码为本次实验中编写的核心功能模块。1. 网卡驱动层 (kernel/e1000.c)负责与 E1000 硬件交互，实现数据包的发送与接收。1.1 发送函数 (e1000_transmit)将数据包放入发送环形缓冲区，并更新 TDT 寄存器通知硬件发送。int
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
1.2 接收函数 (e1000_recv)扫描接收环，处理到达的数据包，并防止重入死锁。static void
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
2. 网络协议栈与 Socket (kernel/net.c)负责 UDP 协议处理、端口绑定及系统调用接口。2.1 数据结构定义与初始化定义 mbuf 队列、sock 结构体及初始化逻辑。// [Additions at the top of kernel/net.c]

// mbuf 用于接收队列链表
struct mbuf {
  struct mbuf  *next;
  char         *head; // 指向实际数据
  unsigned int len;
};

// 辅助函数：释放 mbuf
void mbuffree(struct mbuf *m) {
  if(m->head) kfree(m->head);
  kfree(m);
}

// Socket 结构体
struct sock {
  struct spinlock lock;
  int port;
  int bound;        // 1: 已占用, 0: 空闲
  struct mbuf *rxq; // 接收队列头指针
};

#define NSOCK 16
static struct sock sockets[NSOCK];

// Socket 初始化
void sockinit(void) {
  for(int i = 0; i < NSOCK; i++) {
    initlock(&sockets[i].lock, "sock");
    sockets[i].bound = 0;
    sockets[i].rxq = 0;
  }
}

// 修改 netinit 调用 sockinit
void netinit(void) {
  initlock(&netlock, "netlock");
  sockinit(); // <--- 新增
}
2.2 端口绑定 (sys_bind)实现 socket 分配与端口绑定。uint64 sys_bind(void) {
  int port;
  struct sock *s;
  
  argint(0, &port); // 获取端口参数

  for(int i = 0; i < NSOCK; i++) {
    s = &sockets[i];
    acquire(&s->lock);
    if(s->bound) {
      if(s->port == port) { // 端口已被占用
        release(&s->lock);
        return -1;
      }
      release(&s->lock);
    } else {
      // 找到空位，进行绑定
      s->port = port;
      s->bound = 1;
      s->rxq = 0;
      release(&s->lock);
      return 0;
    }
  }
  return -1; // 无空闲 socket
}
2.3 数据包分发 (ip_rx)解析协议头，查找对应端口的 Socket 并投递数据包。void ip_rx(char *buf, int len) {
  // ... (省略 printf) ...
  struct ip *iph;
  struct udp *uh;
  struct sock *s;

  // 1. 检查长度并跳过以太网头 (14字节)
  if(len < sizeof(struct eth)){ kfree(buf); return; }
  iph = (struct ip *)(buf + sizeof(struct eth)); // <--- 关键偏移修复

  // 2. 检查是否为 UDP 协议
  if(iph->ip_p == IPPROTO_UDP){
    // 检查总长度
    if(len < sizeof(struct eth) + sizeof(struct ip) + sizeof(struct udp)){
      kfree(buf); return;
    }

    // 定位 UDP 头
    uh = (struct udp *)((char *)iph + sizeof(struct ip));
    uint16 dport = ntohs(uh->dport);

    // 3. 遍历 socket 寻找接收者
    for(int i = 0; i < NSOCK; i++){
      s = &sockets[i];
      acquire(&s->lock);
      if(s->bound && s->port == dport){
        // 4. 封装 mbuf 节点
        struct mbuf *m = (struct mbuf *)kalloc();
        if(m == 0){ release(&s->lock); kfree(buf); return; }
        m->next = 0;
        m->head = buf; 
        m->len = len;

        // 5. 挂入队列 (带简单的长度检查防止溢出)
        if(s->rxq){
          struct mbuf *q = s->rxq;
          int qlen = 1;
          while(q->next) { q = q->next; qlen++; }
          
          if(qlen >= 16) { // 队列满，丢弃
             mbuffree(m);
             release(&s->lock);
             return;
          }
          q->next = m;
        } else {
          s->rxq = m;
        }

        // 6. 唤醒等待进程
        wakeup(s);
        release(&s->lock);
        return; // 成功移交，不需要 free buf
      }
      release(&s->lock);
    }
  }
  kfree(buf); // 无人接收或非 UDP，释放内存
}
2.4 数据接收 (sys_recv)用户态接收函数，包含阻塞等待和字节序转换。int sys_recv(void) {
  int port, maxlen;
  uint64 src, sport, buf;
  struct sock *s;
  
  // 获取参数
  argint(0, &port); argaddr(1, &src); argaddr(2, &sport);
  argaddr(3, &buf); argint(4, &maxlen);

  // 查找目标 socket
  struct sock *target_sock = 0;
  for(int i = 0; i < NSOCK; i++){
    s = &sockets[i];
    acquire(&s->lock);
    if(s->bound && s->port == port){
      target_sock = s;
      break; 
    }
    release(&s->lock);
  }
  if(target_sock == 0) return -1;

  s = target_sock; // s->lock 此时被持有

  // 循环等待直到队列非空
  while(s->rxq == 0){
    sleep(s, &s->lock);
    if(!s->bound){ release(&s->lock); return -1; }
  }

  // 取出数据包
  struct mbuf *m = s->rxq;
  s->rxq = m->next;
  release(&s->lock);

  // 跳过头部解析
  struct ip *iph = (struct ip *)(m->head + sizeof(struct eth));
  struct udp *uh = (struct udp *)((char *)iph + sizeof(struct ip));
  int payload_len = ntohs(uh->ulen) - sizeof(*uh);
  if(payload_len > maxlen) payload_len = maxlen;

  // 拷贝数据到用户空间
  if(copyout(myproc()->pagetable, buf, (char*)(uh + 1), payload_len) < 0){
    mbuffree(m); return -1;
  }

  // [关键] 修复字节序问题: 网络序(Big) -> 主机序(Little)
  uint32 tmp_ip = iph->ip_src;
  uint32 src_ip = ((tmp_ip & 0xFF000000) >> 24) |
                  ((tmp_ip & 0x00FF0000) >> 8)  |
                  ((tmp_ip & 0x0000FF00) << 8)  |
                  ((tmp_ip & 0x000000FF) << 24);

  copyout(myproc()->pagetable, src, (char*)&src_ip, sizeof(src_ip));
  
  // 拷贝源端口
  uint16 src_port = ntohs(uh->sport);
  copyout(myproc()->pagetable, sport, (char*)&src_port, sizeof(src_port));

  mbuffree(m);
  return payload_len;
}
