xv6 Lab 3 (pgtbl) 核心代码变更汇总本文档汇总了在完成 MIT 6.S081 / 2024 xv6 Lab 3 (pgtbl) 过程中修改的所有核心内核代码。1. kernel/memlayout.h修改内容：定义了超级页分配的起始物理地址。// 在 PHYSTOP 之前划出一部分空间给超级页 (16MB)
#define SUPERBASE (KERNBASE + 112*1024*1024) 
2. kernel/proc.h修改内容：在 struct proc 中添加共享页面的指针。struct proc {
  // ... 原有成员
  struct usyscall *usyscall;   // 任务 3.1：用于存放 PID 的共享页面
};
3. kernel/defs.h修改内容：添加新定义的函数原型，确保跨文件调用。// kalloc.c
void* superalloc(void);
void            superfree(void *);
void            superinit(void);

// vm.c
void            vmprint(pagetable_t);
pte_t* superwalk(pagetable_t, uint64, int);
4. kernel/proc.c修改内容：管理进程生命周期中 usyscall 页面的分配、映射和释放。allocproc()  // 分配 usyscall 页面
  if((p->usyscall = (struct usyscall *)kalloc()) == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }
  p->usyscall->pid = p->pid; // 初始化 PID
freeproc()  if(p->usyscall)
    kfree((void*)p->usyscall);
  p->usyscall = 0;
proc_pagetable()  // 映射 USYSCALL 页面，权限为用户只读
  if(mappages(pagetable, USYSCALL, PGSIZE,
              (uint64)(p->usyscall), PTE_R | PTE_U) < 0){
    uvmunmap(pagetable, TRAPFRAME, 1, 0);
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }
5. kernel/kalloc.c修改内容：实现超级页（2MB）专用分配器。struct {
  struct spinlock lock;
  struct run *freelist;
} supermem; // 超级页空闲链表

void superinit() {
  initlock(&supermem.lock, "supermem");
  char *p = (char*)SUPERPGROUNDUP(SUPERBASE);
  for(; p + SUPERPGSIZE <= (char*)PHYSTOP; p += SUPERPGSIZE)
    superfree(p);
}

void superfree(void *pa) {
  struct run *r;
  // 安全检查：必须对齐且在范围内
  if(((uint64)pa % SUPERPGSIZE) != 0 || (char*)pa < (char*)SUPERBASE || (uint64)pa >= PHYSTOP)
    panic("superfree");

  memset(pa, 1, SUPERPGSIZE); // 填充垃圾值
  r = (struct run*)pa;
  acquire(&supermem.lock);
  r->next = supermem.freelist;
  supermem.freelist = r;
  release(&supermem.lock);
}

void* superalloc(void) {
  struct run *r;
  acquire(&supermem.lock);
  r = supermem.freelist;
  if(r)
    supermem.freelist = r->next;
  release(&supermem.lock);
  if(r)
    memset((char*)r, 5, SUPERPGSIZE);
  return (void*)r;
}
6. kernel/vm.c修改内容：页表打印逻辑与超级页映射核心逻辑。vmprint() 及其辅助函数void vmprinthelper(pagetable_t pagetable, int level, uint64 va) {
  for(int i = 0; i < 512; i++){
    pte_t pte = pagetable[i];
    if(pte & PTE_V){
      for(int j = 0; j < (2-level); j++) printf(" ..");
      uint64 va_curr = va + ((uint64)i << (12 + 9 * level));
      printf("%p: pte %p pa %p\n", va_curr, pte, PTE2PA(pte));
      if((pte & (PTE_R|PTE_W|PTE_X)) == 0){
        vmprinthelper((pagetable_t)PTE2PA(pte), level-1, va_curr);
      }
    }
  }
}

void vmprint(pagetable_t pagetable) {
  printf("page table %p\n", pagetable);
  vmprinthelper(pagetable, 2, 0);
}
superwalk() 与 mappages() 修改// 专门获取一级页表项的 walk
pte_t* superwalk(pagetable_t pagetable, uint64 va, int alloc) {
  pte_t *pte = &pagetable[PX(2, va)];
  if(*pte & PTE_V) {
    pagetable = (pagetable_t)PTE2PA(*pte);
    return &pagetable[PX(1, va)];
  } else {
    if(!alloc || (pagetable = (pde_t*)kalloc()) == 0) return 0;
    memset(pagetable, 0, PGSIZE);
    *pte = PA2PTE(pagetable) | PTE_V;
    return &pagetable[PX(1, va)];
  }
}

// mappages 修改逻辑
// 根据 pa >= SUPERBASE 决定使用 PGSIZE 还是 SUPERPGSIZE
// 内部根据大小分别调用 walk() 或 superwalk()
uvmalloc() 三段式分配uint64 uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz, int xperm) {
  // 1. 分配 4KB 页面直到 2MB 对齐
  // 2. 尽可能分配 2MB 超级页 (memset 大小为 SUPERPGSIZE)
  // 3. 分配剩余的 4KB 页面
}
实验结论：所有测试通过，pgtbltest: all tests succeeded。
