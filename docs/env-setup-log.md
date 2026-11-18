# 环境搭建记录（Lab1 报告素材）

记录日期：2026-11-18。服务器：`oscp-temp`（AutoDL 容器）。复现时在仓库根目录执行 `tools/setup_server.sh <ssh别名>`。

## 软件环境

| 项目 | 版本 |
|---|---|
| Linux | Ubuntu 22.04.5 LTS（Docker 容器），内核 5.15.0-78-generic，x86_64 |
| gcc/g++ | 11.4.0（Ubuntu 11.4.0-1ubuntu1~22.04.3），通过 `gcc-multilib`/`g++-multilib` 编译 32 位程序 |
| glibc | 2.35 |
| GNU Make | 4.3 |
| gdb | 12.1 |
| MIPS 交叉编译器 | gcc 2.8.1 + GNU assembler 2.9（`gcc-2.8.1-mips.tar.gz`，安装在 `/usr/local/mips`） |
| Nachos | Nachos-3.4-UALR-2022 |

## 安装步骤

```bash
apt-get install -y gcc-multilib g++-multilib gdb
tar xzf gcc-2.8.1-mips.tar.gz -C /usr/local      # 包内顶层目录是 mips/，解压后得到 /usr/local/mips
tar xzf nachos-3.4-ualr-2022.tar.gz -C /root/oscp
cd /root/oscp/nachos-3.4-ualr-2022/code
make -C bin && make -C threads && make -C userprog && make -C filesys && make -C network && make -C test
```

在 64 位系统上，`Makefile.dep` 和 `Makefile.common` 会检测 `getconf LONG_BIT`，自动加上 `-m32`（编译、链接）和 `--32`（汇编），所以生成的 `nachos` 是 32 位的 `ELF 32-bit LSB pie executable, Intel 80386`。

## 遇到的问题与解决方法

### 1. 链接时出现 TEXTREL 警告（无害）
```
/usr/bin/ld: arch/unknown-i386-linux/objects/switch-linux.o: warning: relocation against `_eax_save' in read-only section `.text'
/usr/bin/ld: warning: creating DT_TEXTREL in a PIE
```
- **原因**：上下文切换代码 `switch-linux.s` 是手写汇编，直接用绝对地址访问全局变量 `_eax_save`，而 gcc 11 默认生成位置无关可执行文件（PIE），链接器只好在代码段里留下需要重定位的位置。
- **影响**：不影响运行，threads、userprog、filesys、network 每个模块的编译都只有这 2 条警告。

### 2. 编译 MIPS 用户程序报 `Value too large for defined data type`
```
decstation-ultrix-gcc: /usr/local/mips/lib/gcc-lib/decstation-ultrix/2.8.1/specs: Value too large for defined data type
```
- **原因**：
  - MIPS 交叉编译器本身是很老的 32 位 i386 程序，调用的是 32 位的 `__xstat`/`__fxstat`。
  - 容器所在的文件系统（overlay/xfs）inode 号很大（例如 `specs` 的 inode 号是 27939192981），超过了 32 位 `ino_t` 能表示的范围，glibc 因此返回 EOVERFLOW。
- **解决**：写了一个 LD_PRELOAD 垫片 [`tools/stat64shim.c`](../tools/stat64shim.c)。
  - 它截获 `__xstat`/`__fxstat`，改调 `stat64`/`fstat64`，把 inode 号折叠到 32 位后，填回老程序要求的 32 位 `struct stat`。
  - `decstation-ultrix-{gcc,as,ld}` 换成了注入这个垫片的包装脚本，原程序移到 `/usr/local/mips/bin-real/`。
  - 这只改动开发环境，不涉及 Nachos 代码。在普通 ext4 系统上（inode 号较小）不会出现这个问题。

### 3. Nachos 的 `-cp` 读不到 UNIX 文件的修改时间（Lab4/Lab5 相关）
- **现象**：参考程序 `lab4/n4a -cp test/big big` 输出 `test/big: stat error`，`-D` 显示的修改时间是 `Thu Jan  1 08:00:00 1970`。
- **原因**：同问题 2。Nachos 是 32 位程序，32 位的 `stat()` 遇到大 inode 号就失败。
- **验证**：
  - 把文件复制到 `/dev/shm`（tmpfs，inode 号小）后再 `-cp`，时间正确。
  - 加 `LD_PRELOAD=/usr/local/mips/lib/libstat64shim.so` 运行参考程序，时间也正确。
- **我们自己代码的方案**：用 gcc 11 编译时，`stat` 链接的是 glibc 2.33 以后的新符号，垫片截获不到。
  - 在 `lab4`、`lab5` 的 `Makefile.local` 里加 `DEFINES += -D_FILE_OFFSET_BITS=64`，让 `stat` 使用 `stat64`。已用小程序验证：不加时 EOVERFLOW，加了能读出正确的 mtime。
  - 这样只改 lab 目录内的文件，在老师的机器上同样能用。

### 4. `userprog/nachos -x ../test/matmult.noff` 断言失败（预期行为）
```
Assertion failed: line 78, file "addrspace.cc"      // ASSERT(numPages <= NumPhysPages)
```
- **原因**：原版 Nachos 把用户程序整个装入内存，而 matmult 需要的页数超过了物理页数 `NumPhysPages = 32`。
- **说明**：这正是 Lab7 虚拟内存要解决的问题。`halt.noff` 能正常运行（`Machine halting!`，Ticks total 132，user 12）。

## 参考程序验证

| 程序 | 命令 | 结果 |
|---|---|---|
| lab2/n2 | `./n2` | 与 n2screen.txt 完全一致 |
| lab3/n3 | `./n3 -rs 1` | 与 n3screen.txt 完全一致 |
| lab4/n4a | readme 中的序列 | 需要垫片（见问题 3），加上后修改时间正确 |
| lab5/n5 | readme 中的序列 | 加垫片后 `-DI` 与 n5screen.txt 一致（51 个扇区已用；3 个文件共 5092 字节；内碎片 284 字节） |
| lab6/n6 | `./n6 -x ../test/exec.noff` | 与 n6screen.txt 一致（只差末尾一个空行） |
| lab7/n7 | `-pra -4` 后接 `-pra 0` | LRU：faults 889、write backs 224；OPT：faults 450、write backs 195，与 n7screent.txt 一致 |
