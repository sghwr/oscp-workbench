# AGENTS.md：Nachos 课程设计工作约定

任务背景、实验列表和提交规范见 [README.md](README.md)，本文件只写 AI 助手干活时必须遵守的规则。一律用简体中文回复。

## 1. 权威来源（以下文件优先于你的记忆和网上的 Nachos 资料）

1. `OSCP/实验内容及代码报告提交要求/OSCP实验内容及代码报告提交要求 -2022.docx`：每个实验的具体要求。
2. `code/labN/nNreadme.txt`、`nNscreen.txt`：参考实现的参数和预期输出。自己的实现应该在输出格式和行为上与之对齐。
3. `OSCP/课件/lab2-lab7目录下所需的源码文件 -2022.xlsx`：每个 lab 需要从哪个目录复制哪些文件。
4. 本仓库的 Nachos 是 **3.4-UALR-2022 版**，它的行号、Makefile 和 4.x 版以及网上其他移植版都不一样，不要照搬别人的代码。

`OSCP/` 是课程原始资料，**只读**，不要修改、移动或删除其中的文件。

## 2. 环境

- **编译和运行只在远端 Linux 上做**：`ssh oscp-temp`（Ubuntu 22.04 x86_64 容器）。本机是 Windows + Git Bash，不能编译 Nachos。
- **不要在 Windows 上解压源码包后再上传**：源码 tar 包里有符号链接（如 `test/*.noff`、`bin/coff2noff`），在 Windows 上会创建失败。要把 `.tar.gz` 原样传到服务器上再解压。
- **工作目录约定**：远端是 `/root/oscp/nachos-3.4-ualr-2022/`，本机副本是 `操作系统课设/nachos-3.4-ualr-2022/`。
- **服务器是临时的**：每完成一个阶段，就把远端代码打成 tar 包拉回本机，例如 `ssh oscp-temp 'tar czf - -C /root/oscp nachos-3.4-ualr-2022' > backup.tgz`。
- **重建环境**：换新服务器时，在本机仓库根目录运行 `tools/setup_server.sh <别名>`。问题记录见 `docs/env-setup-log.md`。
- **大 inode 问题**：AutoDL 容器的 inode 号超过 32 位，32 位程序调用 `stat()` 会返回 EOVERFLOW。
  - MIPS 工具链：已用 `/usr/local/mips/bin` 里的包装器加 LD_PRELOAD 垫片解决，正常调用即可。
  - 参考程序 `n4*`、`n5`：用 `-cp` 时要加上 `LD_PRELOAD=/usr/local/mips/lib/libstat64shim.so`，否则会报 `stat error`，修改时间显示为 1970。
  - **我们自己的 Lab4/5 代码**：在 `labN/Makefile.local` 里加 `DEFINES += -D_FILE_OFFSET_BITS=64`（已验证有效），不要依赖垫片。
- **不要写入任何凭据**：不要把密码、密钥或私人信息写进仓库文件、报告或命令历史。

## 3. 代码修改规则

- **在 labN 里覆盖，不改原目录**：每个 lab 的 `Makefile.local` 把 `-I- -I../labN` 放在 include 路径最前面，并通过 vpath 找源文件。所以要改的内核文件应该**复制到 `code/labN/` 再改**，`threads/`、`machine/`、`filesys/`、`userprog/` 里的原文件保持不动。参考 `code/demo0` 的做法。
- **唯一例外**：`code/userprog/syscall.h` 和 `code/test/start.s` 允许原位修改（Lab6 加 PrintInt 用），改之前先备份成 `*.orig`。`code/test/Makefile` 的 `targets` 需要加上 `exec`、`halt2`。
- **前后依赖**：Lab5 从 Lab4 复制后再改，Lab7 从 Lab6 复制后再改。前一个 lab 完成之后，不要再为了后一个 lab 去改它。
- **编译选项**：不要修改 `Makefile.common` 或 `Makefile.dep` 里的 32/64 位适配逻辑（`GCCOPT32`/`ASOPT32`）。编出来的 Nachos 必须是 32 位程序。
- **必须能直接 make**：提交时要做到"拿到压缩包后直接 `make` 就能编过"。新增的源文件要加进对应 `Makefile.local` 的 `CCFILES`（Lab3 的 `threadsbar.cc` 已经加好了）。
- **sort.c 只许改 ARRAYSIZE**：Lab7 只能修改 `code/test/sort.c` 的 `ARRAYSIZE`，其他代码不能动。
- **代码风格**：与 Nachos 原有风格保持一致（C++98 风格、`DEBUG()` 宏、`ASSERT`）。关键改动处加注释，方便报告直接引用。

## 4. 各 Lab 的硬性约定（容易做错的地方）

- **Lab2**：优先级范围 0–99，0 最高，默认 9，在 `Thread` 构造函数里设置。`ThreadTest` 里依次 Fork t1/t2/t3（优先级 1/2/3），最后调用 `SimpleThread(0)`。调度为非抢占式，可以利用 `List::SortedInsert`。上下文切换次数的问题需要同时给出原因分析和代码改进。
- **Lab3**：`N_THREADS 10`、`N_TICKS 1000`，`BarThread` 先调用 `MakeTicks` 再打印 rendezvous。只改自己写的屏障代码，不改内核。两类现象都要写清楚：先复现，再分析原因，最后修复。还要回答"加空循环或 Linux 的 sleep 能否解决"这个问题。
- **Lab4**：
  - 修改时间是从 1970 年起的 UTC 秒数，存在原 `numSectors` 的位置。
  - `-D` 只对普通文件显示时间。
  - `-hap` 不能把文件截短。
  - 文件头写回时机可以参考 n4a–n4d 四种方案，在报告里说明你选的是哪一种。
- **Lab5**：
  - 文件头的直接索引是 29 项，最后一项指向二级索引块（32 项）；不需要时该项设为 -1，并且要动态分配和释放二级索引块。
  - `-DI` 的输出格式对齐 `n5screen.txt`。
- **Lab6**：`Exec()` 在新地址空间运行新程序，不覆盖调用者。`AddrSpace::Print` 的输出格式对齐 `n6screen.txt`。物理页需要用 bitmap 分配，以支持多道程序。
- **Lab7**：
  - 每进程固定 5 帧、局部置换、纯按需调页（启动时不预先装入任何页）。
  - 置换算法不能用随机。
  - Statistics 输出 `Paging: faults X, write backs Y`。
  - 交换文件可以参考 n7 的 `SWAPn` 命名。

只需写进报告的题目（Lab2 aging、Lab5 rwx、Lab6 Fork/COW、Lab7 OPT 等）**不要写成可运行代码**，在报告里用文字加关键数据结构或代码片段说明即可。

## 5. 验证

- **每改一次都要实测**：在远端 `make` 后实际运行，把输出和参考程序 `./nN` 在同样参数下的输出对比（同一 `-rs` 种子），差异要能解释。
- **完成前必须核对**：报告"完成"之前，先执行 README 里的验收测试命令；Lab4 和 Lab5 要按 readme 走完整的命令序列，每次都从 `rm -f DISK` 开始。
- **如实报告**：编译警告、运行失败或和参考输出不一致的地方要如实说明，不要掩盖。

## 6. 实验报告

- **格式与命名**：按提交模板（`OSCP/实验内容及代码报告提交要求/*.zip` 里的 docx）的结构写：实验目的、软硬件环境、实验步骤与内容、结论分析与体会。文件命名为 `实验n_学号_班级_姓名.docx`，放在 `reports/`。
- **内容要求**：报告里贴的关键代码必须和最终提交的代码逐字一致；运行结果尽量用文本，必要时截图。
- **写作分工**：组员姓名、学号、班级向用户索取，不要编造。报告由 AI 起草，用户审阅定稿。
