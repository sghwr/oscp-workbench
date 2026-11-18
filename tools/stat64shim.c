/*
 * stat64shim.c —— 让 32 位的老 MIPS 交叉编译器 (gcc-2.8.1) 在 inode 号 > 2^32 的
 * 文件系统 (如 AutoDL 容器的 overlay/xfs) 上正常工作。
 *
 * 现象：decstation-ultrix-gcc: .../specs: Value too large for defined data type
 * 原因：老程序调用 32 位的 __xstat/__fxstat，内核返回的 64 位 inode 号放不进
 *       32 位 st_ino，glibc 返回 EOVERFLOW。
 * 方法：用 LD_PRELOAD 截获这两个函数，改调 stat64/fstat64，再把结果(inode 号折叠
 *       到 32 位)填回老程序期望的 32 位 struct stat。
 *
 * 只影响开发环境，不属于 Nachos 代码，不影响课程提交。
 * 编译：gcc -m32 -shared -fPIC -O2 -o libstat64shim.so stat64shim.c
 */
#define _LARGEFILE64_SOURCE
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>

static int fill(struct stat *dst, const struct stat64 *src)
{
    if (src->st_size > 0x7fffffffLL) { errno = EOVERFLOW; return -1; }
    memset(dst, 0, sizeof(*dst));
    dst->st_dev     = src->st_dev;
    /* 把 64 位 inode 号折叠为 32 位，cpp 用 (st_dev, st_ino) 判断是否同一文件 */
    dst->st_ino     = (ino_t)(src->st_ino ^ (src->st_ino >> 32));
    dst->st_mode    = src->st_mode;
    dst->st_nlink   = src->st_nlink;
    dst->st_uid     = src->st_uid;
    dst->st_gid     = src->st_gid;
    dst->st_rdev    = src->st_rdev;
    dst->st_size    = (off_t)src->st_size;
    dst->st_blksize = src->st_blksize;
    dst->st_blocks  = (blkcnt_t)src->st_blocks;
    dst->st_atim    = src->st_atim;
    dst->st_mtim    = src->st_mtim;
    dst->st_ctim    = src->st_ctim;
    return 0;
}

int __xstat(int ver, const char *path, struct stat *buf)
{
    struct stat64 s;
    (void)ver;
    if (stat64(path, &s) != 0) return -1;
    return fill(buf, &s);
}

int __fxstat(int ver, int fd, struct stat *buf)
{
    struct stat64 s;
    (void)ver;
    if (fstat64(fd, &s) != 0) return -1;
    return fill(buf, &s);
}
