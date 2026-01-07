extern uint64 sys_symlink(void);
static uint64 (*syscalls[])(void) = {
  // ...
  [SYS_symlink]   sys_symlink,
};
