// [Additions at the top of kernel/net.c]

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


uint64 sys_bind(void) {
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

void ip_rx(char *buf, int len) {
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


int sys_recv(void) {
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

