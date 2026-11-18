# Lab5 讲解：具有二级索引的文件系统

代码在 `code/lab5/`，基于 Lab4 修改，全部改动在提交 `4946292` 中。
想只看 Lab5 相对 Lab4 改了什么，可以运行：

```bash
git diff eb6a0fd 4946292 -- nachos-3.4-ualr-2022/code/lab5
```

这条命令显示的是 lab5 从无到有的完整文件内容。更直观的方法是直接比较两个目录：

```bash
diff code/lab4/filehdr.cc code/lab5/filehdr.cc
```

## 1. 二级索引文件头

```
原版 / Lab4: [numBytes][time][dataSectors[0..29]]                 30 个直接索引，最大 3840 字节
Lab5      : [numBytes][time][dataSectors[0..28]][indexSector]      29 个直接索引 + 1 个指针
                                                     │
                                                     ▼
                              索引块（1 个扇区 = 32 个 int）：数据扇区 29..60
最大文件长度：(29 + 32) × 128 = 7808 字节
```

- 宏定义：`NumDirect = (128 − 3×4)/4 = 29`，`NumIndirect = 128/4 = 32`，`MaxFileSize = (29+32)×128`。
- **索引块按需分配**：文件不超过 29 个扇区时，`indexSector = -1`，不存在索引块。文件第一次增长到第 30 个扇区时，`Extend` **先**分配索引块，**再**分配数据扇区。所以 huge 的直接扇区是 6–34，索引块是 35，二级数据扇区从 36 开始，和参考程序完全一致。
- **索引块立即写盘，文件头关闭时写回**：`Extend` 修改索引块之后马上把它写回磁盘；文件头仍然沿用 Lab4 的脏标志，在关闭时写回。
- **删除**：`Deallocate` 先读入索引块，释放其中列出的数据扇区，再释放索引块本身。
- **地址转换**：`ByteToSector(offset)` → `DataSector(offset / 128)`。下标小于 29 时直接查 `dataSectors`；否则读入索引块再查，**多花一次磁盘读**。这就是多级索引的代价：文件越大，访问后面的数据越慢。
- **`-D` 显示**：`Print` 只读一次索引块。29 个直接扇区之后用 ` Index2: ` 分隔二级扇区，格式和参考程序相同。
- **可扩展的最大长度**：`MaxLength(freeMap)` 计算文件最多能扩展到多长。它要考虑"第一次跨过 29 个扇区时，索引块本身也要占用一个空闲扇区"。

`Allocate` 被改写成"把一个空文件 `Extend` 到 fileSize"。这样不管文件是在创建时就很大，比如 `-cp` 一个 5000 字节的文件，还是写着写着变大，都只走同一套分配代码。

## 2. `-DI` 磁盘信息

`main.cc` 中新增 `-DI` 选项，它调用 `FileSystem::PrintInfo()`。还给 `Directory` 加了一个 `GetFileSectors()`，用来列出所有文件头所在的扇区。

| 输出项 | 计算方法 |
|---|---|
| Disk size | `NumSectors`（1024）× 128 |
| Used / Free | 来自位图：`NumSectors − NumClear()` 和 `NumClear()`。Used 包括位图和目录文件、所有文件头、**索引块** |
| N bytes in M files | 遍历目录，累加每个文件头的 `numBytes` |
| occupy X bytes (S sectors) | 累加每个文件的数据扇区数 `divRoundUp(numBytes,128)`。**不算文件头和索引块**，但最后一个扇区没写满的部分，也就是内碎片，算在里面 |
| internal fragmentation | 每个文件的 `扇区数×128 − numBytes` 之和；"in K sectors" 是最后一个扇区没写满的文件个数 |

扇区数可以由长度算出来，所以 `-DI` 只读文件头，不读索引块。实测读盘次数和参考程序一样，都是 8 次。

**核对一个例子**（readme 序列中间那次 `-DI`）：
- huge 是 2432 + 2432 + 38 = 4902 字节，占 39 个扇区；small 是 38 字节，占 1 个扇区；medium 是 152 字节，占 2 个扇区。合计 5092 字节、42 个扇区。
- 内碎片：(39×128 − 4902) + (128 − 38) + (256 − 152) = 90 + 90 + 104 = 284 字节，分布在 3 个扇区。
- Used：5（系统文件）+ 3（文件头）+ 42（数据扇区）+ 1（huge 的索引块）= 51。

## 3. 测试结果

- **n5readme 全序列**：除 Ticks 以外，和参考 n5 只差 1 处。`-ap test/huge huge` 这一步我们读盘 556 次，参考程序 557 次。差的这 1 次，应该是新分配的索引块我们直接在内存里初始化成 -1，而参考程序从磁盘读了一次。三次 `-DI` 的输出、扇区分配、Index2 列表、文件内容、其他每一步的读写次数全部相同。
- **上限测试**：连续追加到 7808 字节时提示 full，`Index2` 后面正好有 32 个扇区；Used 是 68 = 5 + 1 + 61 + 1。
- **创建时就需要索引块**：`-cp` 一个 5000 字节的文件，占 40 个扇区，内碎片 120 字节；用 `-p` 读回，和原文件逐字节一致。
- **超过上限**：`-cp` 一个 8000 字节的文件会被拒绝。
- **删除后**：Used 回到 5，索引块也被回收了。
- **`-t` 性能测试**：通过。

## 4. 为文件增加 rwx 权限的方法（只写进报告，不写代码）

### 4.1 权限存在哪里

文件头的 32 个 int 已经全部用完了，有三个方案可选：

| 方案 | 做法 | 优缺点 |
|---|---|---|
| A. 放进目录项 | `DirectoryEntry` 是 `{bool inUse; int sector; char name[10];}`。`bool` 后面有 3 个字节的对齐填充，放一个 `unsigned char mode` 进去，**目录项大小仍然是 20 字节**，目录文件格式不用变 | 零成本，和现有磁盘格式兼容。缺点是 Unix 把权限存在 i-node 里，同一个文件有多个硬链接时会共享权限；不过 Nachos 本来就没有硬链接 |
| B. 放进文件头 | 直接索引减到 28 个，腾出一个 `int mode`，以后还可以再放 uid 等信息；最大长度变成 (28+32)×128 = 7680 字节 | 语义上最正确，和 Unix 的 i-node 一致；代价是少了一个直接索引 |
| C. 借用 numBytes 的高位 | 文件长度最多只用到 13 位，高位可以存权限 | 不推荐，可读性差，容易出错 |

推荐**方案 B**；如果要求不改变文件头布局，就用**方案 A**。权限位用 Unix 的编码：`#define PERM_R 4`、`#define PERM_W 2`、`#define PERM_X 1`。Nachos 没有用户的概念，所以只需要一组 rwx。如果要支持多用户，可以在文件头里加 `uid` 和 owner/group/other 三组权限，并在 Thread 中记录当前用户。

### 4.2 在哪里检查权限

1. **创建**：`FileSystem::Create(name, size, int mode = PERM_R | PERM_W)`，在 `FileHeader::Allocate` 时写入 mode。
2. **打开**：`FileSystem::Open(name, int access)` 打开文件时，检查 `(access & ~hdr->mode) == 0`，不满足就返回 NULL，相当于 Unix 的 EACCES。`OpenFile` 记住打开方式 `openMode`。
3. **读写**：`OpenFile::ReadAt` 检查 `openMode & PERM_R`，`OpenFile::WriteAt` 检查 `openMode & PERM_W`，没有权限就返回 0（或 -1）。这样即使打开时检查过，也能防止只读方式打开的文件被写。
4. **执行**：Lab6 的 `Exec` 和 `StartProcess` 在装入用户程序之前，检查文件是否有 `PERM_X`。这要求在真正的文件系统下运行（编译时不定义 `FILESYS_STUB`）。
5. **删除**：Unix 中删除一个文件，需要的是对所在**目录**的写权限，而不是对文件本身的权限。Nachos 只有一个根目录，可以给根目录的文件头也加上 mode，在 `FileSystem::Remove` 中检查。
6. **命令行**：
   - 在 `main.cc` 中增加 `-chmod <mode> <file>`，调用 `FileSystem::Chmod`。它读出文件头，修改 mode，再写回磁盘。修改权限本身不算修改内容，**不更新修改时间**；Unix 会更新 ctime，但我们没有 ctime。
   - `-D` 的输出中增加 `rwx` 字符串，例如 `File mode: rw-`。
   - `-cp` 可以继承 UNIX 源文件的 `st_mode & 0700`。
