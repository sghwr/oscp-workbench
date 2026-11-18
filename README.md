# 操作系统课程设计（OSCP）· Nachos 实验

山东大学软件学院《操作系统课程设计》，基于 **Nachos-3.4-UALR-2022**。在 Nachos 已有框架上，分模块实现或完善线程调度、同步、文件系统、系统调用和虚拟内存，每个实验配一份实验报告。

## 目录结构

```
操作系统课设/
├── README.md        本文件：任务说明与进度
├── AGENTS.md        给 AI 编码助手的工作约定
├── CLAUDE.md        引用 AGENTS.md（供 Claude Code 自动加载）
├── OSCP/            课程下发的原始资料（只读，不要修改）
│   ├── 实验内容及代码报告提交要求/   ← 最权威的要求文档 + 提交压缩包模板
│   ├── 操作系统课程设计实验教学大纲 -2022.doc
│   ├── 安装环境/    nachos-3.4-ualr-2022.tar.gz、gcc-2.8.1-mips.tar.gz
│   ├── 课件/        各专题 PPT；“lab2-lab7目录下所需的源码文件 -2022.xlsx”
│   ├── 部分教程/    张鸿烈《指导教程》、Little Book of Semaphores 等
│   └── 其他参考资料/
├── tools/           setup_server.sh（一键搭建远端环境）、stat64shim.c（大 inode 垫片）
├── docs/            env-setup-log.md（环境搭建记录 / Lab1 素材）
├── nachos-3.4-ualr-2022/   （计划）工作代码的本地副本，从远端服务器同步回来
└── reports/                （计划）实验报告 docx
```

## 开发环境

Nachos 只能在 **x86 Linux** 上编译运行，本机（Windows）只用来存放资料、写报告。

| 项目 | 说明 |
|---|---|
| 远端服务器 | `ssh oscp-temp`（AutoDL 临时容器，Ubuntu 22.04 x86_64，密钥登录）。**这是临时机器，到期后数据会丢失，代码要及时同步回本机** |
| 宿主编译器 | gcc/g++ 11.4。Nachos 会编译成 32 位程序（`-m32`），需要 `gcc-multilib`、`g++-multilib` |
| MIPS 交叉编译器 | 用 `gcc-2.8.1-mips.tar.gz`，解压到 `/usr/local/mips`（`code/Makefile.dep` 里写死了 `GCCDIR = /usr/local/mips/bin/decstation-ultrix-`）。只有编译 `code/test` 下的用户程序时才需要 |
| 调试 | gdb（需要安装） |

环境状态（2026-11-18）：☑ 32 位编译库 ☑ gdb ☑ MIPS 交叉编译器 ☑ 基础模块（bin/threads/userprog/filesys/network/test）编译通过 ☑ 参考程序 n2–n7 验证通过

- **换新服务器后一键重建**：在仓库根目录运行 `tools/setup_server.sh <ssh别名>`。
- **详细记录**：版本、遇到的问题和解决方法、参考程序的验证结果见 [docs/env-setup-log.md](docs/env-setup-log.md)，可作为 Lab1 报告素材。
- **AutoDL 容器的特殊问题**：文件系统的 inode 号超过 32 位，32 位老程序调用 `stat()` 会报 EOVERFLOW。脚本已为 MIPS 工具链自动装好垫片；Lab4/5 自己的代码需要另外处理，见 AGENTS.md。

## 七个实验

每个 `code/labN` 目录里都有老师给的参考可执行文件 `nN`、说明 `nNreadme.txt` 和预期输出 `nNscreen.txt`，可以用来对照自己的实现。

| Lab | 题目 | 要实现的功能 | 只需在报告中描述的题目 | 依赖 |
|---|---|---|---|---|
| 1 | 环境准备、安装与源码分析 | 不写代码。报告写：环境安装测试、源码目录分析、Makefile 分析、中断/时钟/CPU 指令模拟原理 | — | — |
| 2 | 具有优先级的线程调度 | 静态优先级、非抢占调度（优先级 0–99，0 最高，默认 9；t1/t2/t3 优先级为 1/2/3）；分析 `./nachos -d t` 下上下文切换次数与打印行数的差异，并改代码缩小差距 | 优先级老化（aging） | — |
| 3 | 使用信号量解决 N 线程屏障问题 | 新建 `threadsbar.cc`，按 Little Book 3.6.4 实现屏障（10 个线程）；消除“多个线程都认为自己最后到达”的现象；让 rendezvous 行的输出顺序随 `-rs` 种子变化 | — | — |
| 4 | 基本文件系统扩展 | 文件长度可变（边写边分配扇区）；用原 `numSectors` 的位置存最后修改时间，`-D` 时显示；支持 `-ap`、`-hap`、`-nap` | — | — |
| 5 | 具有二级索引的文件系统 | 29 个直接索引 + 1 个二级索引块（32 项），文件最大 7808 字节；新增 `-DI` 显示磁盘统计 | rwx 权限 | Lab4 |
| 6 | 系统调用与多道用户程序 | `AddrSpace::Print`；`Exec()`（在新地址空间运行，不覆盖调用者）；新系统调用 `PrintInt()`；用户程序 `test/exec.c`、`test/halt2.c` | Fork/Exec 与写时复制 | — |
| 7 | 虚拟内存 | 每进程固定 5 帧、局部置换、纯按需调页；置换算法用 LRU/FIFO/二次机会等（不能用随机）；Statistics 输出页故障次数和写回次数；用 `test/sort.c` 测试 | 不同算法的 user ticks 是否一致；如何统计 OPT 的页故障数 | Lab6 |

进度（代码均已完成并与参考程序对比验证，报告待写）：☐ Lab1 报告 ☑ Lab2 ☑ Lab3 ☑ Lab4 ☑ Lab5 ☑ Lab6 ☑ Lab7

每个实验的讲解（实现思路、改动说明、思考题答案、测试数据）在 `docs/labN-notes.md`，可直接作为报告素材：
[Lab2](docs/lab2-notes.md) · [Lab3](docs/lab3-notes.md) · [Lab4](docs/lab4-notes.md) · [Lab5](docs/lab5-notes.md) · [Lab6](docs/lab6-notes.md) · [Lab7](docs/lab7-notes.md)

## 验收测试命令（来自各 readme）

```bash
cd code/lab2 && ./nachos            # 对照 n2screen.txt；另看 ./nachos -d t
cd code/lab3 && ./nachos -rs 1      # 换不同种子多跑几次
cd code/lab4 && rm -f DISK && ./nachos -f && ./nachos -cp test/big big && ./nachos -D   # 完整序列见 n4areadme.txt
cd code/lab5 && rm -f DISK && ./nachos -f && ./nachos -DI   # 完整序列见 n5readme.txt
cd code/lab6 && ./nachos -x ../test/exec.noff               # 对照 n6screen.txt
cd code/lab7 && ./nachos -x ../test/sort.noff               # 对照 n7screent.txt
```

## 提交规范（不符合会扣分）

- **代码位置**：代码放在 `code/lab2` 到 `code/lab7`，包含源码和已编译好的程序。例外：`userprog/syscall.h` 和 `test/start.s` 在原位置修改（改之前先备份）。
- **编译要求**：拿到压缩包后不用手动改任何东西，直接 `make` 就能编译成功并正常运行。
- **报告**：每个实验一份（包括 Lab1），按模板写，命名为 `实验n_学号_班级_姓名.docx`。报告头部的姓名顺序决定该实验组内的贡献排名。报告里贴的关键代码必须和提交的代码一致。
- **压缩包**：
  - 所有报告和最终代码打成一个压缩包，格式只限 `.zip`、`.rar`、`.tar.gz`，包内不能嵌套压缩包。
  - 命名为 `学号1_班级代码1_姓名1_学号2_…`，模板见 `OSCP/实验内容及代码报告提交要求/`。
  - 必须先通过全部演示验收才能上传。
- **评分**：Lab2–7 演示通过并提交代码得 30 分，其余 70 分看报告和代码质量；Lab1 完全看报告。总分里 Lab6 乘 0.5、Lab7 乘 0.3。在时间点 A 前提交约加 15 分，D 后提交约扣 15 分。

## 主要参考

- 《操作系统课程设计 指导教程》张鸿烈 2012：Lab4 看 4.2 节 pp.84-89，Lab6 看 pp.53-60 和 pp.64-68，Lab7 看 pp.60-63，Lab3 看 P39 的 2.2.4
- *The Little Book of Semaphores* 3.6.4（Lab3）
- `code/demo0`（如何在新目录里覆盖内核源码）、`code/demo1`（信号量生产者/消费者，Lab3 可以参考）
- 《操作系统概念》5.3.3（优先级调度）、第 9 章（虚拟内存）
