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

