# Lab4 讲解：扩展文件系统（文件可变长 + 最后修改时间）

代码在 `code/lab4/`，全部改动在提交 `eb6a0fd` 中，用 `git show eb6a0fd` 可以逐行查看。

## 0. 先搞懂 Nachos 文件系统的结构

磁盘是宿主机上的一个文件 `DISK`，Nachos 把它当成 32 道 × 32 扇区 = **1024 个扇区**来用，每个扇区 **128 字节**。

```
扇区 0 : 位图文件的文件头 ─→ 位图数据(128 字节 = 1024 位，每一位表示一个扇区是否已用)
扇区 1 : 目录文件的文件头 ─→ 目录数据(10 个目录项 × 20 字节 = 200 字节，占 2 个扇区)
其他   : 普通文件的文件头（每个文件 1 个扇区）以及数据扇区
```

一个**文件头**（FileHeader，相当于 Unix 的 i-node）正好占 1 个扇区，也就是 32 个 int：

```
原版:   [numBytes][numSectors][dataSectors[0..29]]     ← 30 个直接索引，最大 30×128 = 3840 字节
Lab4:   [numBytes][lastModTime][dataSectors[0..29]]    ← numSectors 的位置改存修改时间
```

`numSectors` 是多余的：它总是等于 `divRoundUp(numBytes, 128)`，可以随时算出来，所以把这个位置让给时间。时间用 `int` 而不用 `time_t` 存储，这样文件头在磁盘上的布局不会因为宿主机不同而改变。代价是会遇到 2038 年问题。

几个相关的类：
- `OpenFile`：一个打开的文件。打开期间，它在**内存里**保存一份文件头 `hdr`，以及当前读写位置 `seekPosition`。
- `FileSystem`：实现 Create、Open、Remove。它在整个运行期间一直打开着位图文件和目录文件。
- `fstest.cc`：命令行 `-cp`、`-ap`、`-hap`、`-nap`、`-D`、`-t` 对应的测试函数。

## 1. 让文件可以变长

**原版为什么不能变长**：`Create(name, size)` 一次性分配好所有扇区。`WriteAt` 遇到 `position >= fileLength` 时直接返回 0，写到结尾的部分会被截断。

**改法**：分三层。

1. **`FileHeader::Extend(freeMap, newSize)`**：把长度改成 newSize。如果需要更多扇区，就从位图中找空闲扇区，填进 `dataSectors` 数组。如果超过 30 个扇区，或者磁盘已满，就返回 FALSE，并且什么都不改。
2. **`OpenFile::ExtendTo(newSize)`**：
   - 如果新长度还在最后一个扇区的剩余空间里，比如 608 字节的文件追加 10 字节后仍在第 5 个扇区内，那么**只改长度**，不读写位图。`Append` 每次只写 10 字节，所以这一点很重要，否则每次写都要多 3 次磁盘 I/O。
   - 如果确实需要新扇区，就读入位图，调用 `Extend`，再把位图写回磁盘。
   - 如果无法扩展到要求的长度，就扩展到能达到的最大长度。这和 Unix 的"部分写"语义一致。
3. **`OpenFile::WriteAt`**：允许从文件末尾开始写（`position == fileLength`，也就是追加），超出文件末尾的部分先调用 `ExtendTo`，之后的流程和原来一样：读出首尾两个不完整的扇区，拼接数据，再写回。
   - 不允许从文件末尾之后开始写（`position > fileLength`），因为那样会在文件中间留下内容未定义的"空洞"。

**`-hap` 不能截短文件（注 3）**：`-hap` 从文件的一半处开始写。如果写的内容没有超过原来的末尾，长度保持不变；只有超过原末尾时才会变长。程序里没有任何地方会把长度改小，所以和 Unix 的 `lseek` 加 `write` 语义一致。实测 646 字节的 big 执行 `-hap` 之后长度仍然是 646，`(small…)` 被写到了文件中间。

## 2. 最后修改时间

| 操作 | 修改时间怎么变 | 实现位置 |
|---|---|---|
| 新建文件（Create） | 设为当前时间 | `FileHeader::Allocate` |
| 写文件（WriteAt 成功） | 设为当前时间 | `OpenFile::WriteAt` 的末尾 |
| 读文件、`-D` 打印 | **不变** | 读的路径上不会修改 hdr |
| `-cp` UNIX 文件 → Nachos | **继承 UNIX 源文件的 mtime**（类似 `cp -p`） | `Copy` 写完数据后调用 `SetModTime(stat(from).st_mtime)` |
| `-ap`、`-nap` 追加到**已有**文件 | 当前时间 | 由 WriteAt 设置 |
| `-ap`、`-nap` 的目标文件**不存在**，需要新建 | 继承源文件的时间 | `created` 标志；`-nap` 的源是 Nachos 文件，用 `GetModTime()` 取得 |

最后两行和参考程序的行为一致。readme 要求"small 及 small2 的修改时间应不是当前时间"，指的就是这种情况：通过追加新建出来的文件，本质上是源文件的一份拷贝。

**只对普通文件显示时间（注 1）**：给 `Print` 加了参数 `Print(bool showTime = FALSE)`。`Directory::Print` 列出普通文件时传 TRUE；`FileSystem::Print` 打印位图和目录的文件头时用默认值 FALSE。这样 `filesys.cc` 一行都不用改。位图文件和目录文件写入时也不更新时间，见下一节。

**显示格式**：用 `strftime(... "%a %b %e %H:%M:%S %Y", localtime(...))`，效果等同于 `ctime()` 去掉末尾的换行，按本地时区显示，和 `ls -l` 的结果一致。

## 3. 文件头什么时候写回磁盘（和 n4a–n4d 的比较）

`WriteAt` 修改的长度、扇区表、时间都只存在于**内存中的文件头**里，必须写回磁盘才能持久保存。参考程序给了四种做法：

| 版本 | 写回时机 | 问题 |
|---|---|---|
| n4a | 调用者手动调用 `OpenFile::WriteBack()` | 容易忘记调用 |
| n4b | `~OpenFile` 无条件写回 | 只读不写也要多一次磁盘写 |
| n4c | `~OpenFile` 在长度改变时写回 | **`-hap` 长度不变，修改时间就不会写回** |
| n4d | `~FileHeader` 在长度改变时写回 | 问题同 n4c |
| **我们** | `~OpenFile` 在**文件头被修改过**时写回（脏标志 `hdrDirty`） | — |

`hdrDirty` 的思路和虚拟内存页表里的"脏位"相同：只要长度、扇区或时间中有任何一项变了，就置为 TRUE，关闭文件时写回；只读的话什么都不写。实测每一步的磁盘读写次数都和 n4a 完全一致。和 n4c、n4d 相比，只在 `-hap` 这一步多了 1 次写，多出的正是把新修改时间写回磁盘的那一次。

**位图文件和目录文件不记录修改时间**：它们一直被 FileSystem 打开着，而且长度固定，所以没必要写回它们的文件头。`ExtendTo` 修改位图时，会临时打开一个位图文件；如果不排除位图和目录，这个临时文件关闭时会多写一次扇区 0。

**代价**：从"扩展时写位图"到"关闭时写文件头"之间如果系统崩溃，位图里已经标记为占用的扇区，不会出现在任何文件头里，这些扇区就泄漏了。真实的文件系统用日志（journaling）或者 fsck 来解决这个问题。

## 4. 环境相关：`-D_FILE_OFFSET_BITS=64`

Nachos 是 32 位程序。服务器的文件系统 inode 号超过 2³²，32 位的 `stat()` 会返回 EOVERFLOW，参考程序也因此打印 `test/big: stat error`，时间显示成 1970 年。我们在 `Makefile.local` 中加了 `-D_FILE_OFFSET_BITS=64`，让 `stat` 变成 `stat64`。在普通的 ext4 系统上加不加都可以，这样做不影响可移植性。

## 5. 测试结果

按 readme 的完整序列测试，每次都先执行 `rm -f DISK`：

| 步骤 | 结果 |
|---|---|
| `-cp test/big big` | 608 字节，扇区 6–10；时间 = `ls --full-time test/big` |
| `-ap test/small big` | 646 字节，新分配了扇区 11；时间 = 当前时间 |
| `-hap test/small big` | 仍是 646 字节，内容从中间被覆盖；时间 = 当前时间 |
| `-ap test/small small`、`-nap small small2` | 都是 38 字节，时间 = test/small 的 mtime（2021 年） |
| 再次 `-nap small small2` | small2 变成 76 字节，时间 = 当前时间；small 不变 |

- **和参考程序逐行对比**（时间值替换成占位符后）：和 n4a 只差 Ticks 行，**位图、扇区分配、文件内容、每一步的磁盘读写次数全部相同**。
- **其他测试**：
  - 超过 3840 字节时写满为止，并提示 `file huge is full`，原版会触发 ASSERT 崩溃；
  - 文件已满时再追加：0 次写，时间不变；
  - `-t` 性能测试：从长度 0 开始写 1000 字节，再读回校验，通过；
  - `-r` 删除文件后，位图恢复。

## 6. 验收演示

```bash
cd code/lab4 && make && rm -f DISK
./nachos -f; ./nachos -cp test/big big; ./nachos -D; ls --full-time test/big
./nachos -ap test/small big; ./nachos -D          # 长度 646，时间为当前时间
./nachos -hap test/small big; ./nachos -D         # 长度不变
./nachos -ap test/small small; ./nachos -nap small small2; ./nachos -D   # 时间是 2021 年
./nachos -nap small small2; ./nachos -D           # small2 为 76 字节，时间为当前时间
```
