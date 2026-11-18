# Lab6 讲解：系统调用与多道用户程序

代码在 `code/lab6/`，提交号是 `2366b68`。另有两个文件按要求**原位修改**，改之前已备份为 `.orig`：
- `code/userprog/syscall.h`
- `code/test/start.s`

`code/test/Makefile` 的 `targets` 中增加了 `exec halt2 sort`。

## 1. 一次系统调用是怎么走完的

```
用户程序 exec.c:  PrintInt(12345);
  → 编译成 jal PrintInt；a0($4) = 12345
  → start.s 桩函数:   addiu $2,$0,SC_PrintInt   # 系统调用号放入 r2
                      syscall                   # 陷入内核
  → mipssim.cc 执行 syscall 指令：RaiseException(SyscallException)
  → machine.cc：切到内核态，调用 ExceptionHandler(which)
  → exception.cc：读 r2 判断是哪个系统调用，读 r4 取得参数，打印出来
  → AdvancePC()：PrevPC←PC，PC←NextPC，NextPC+=4
  → 返回用户态，执行 syscall 下一条指令 "j $31"，回到 C 代码
```

- **为什么必须推进 PC**：异常返回后会继续执行 PC 指向的指令。不推进的话，PC 仍然指向 `syscall`，就会一遍又一遍地重复同一个系统调用。
- **为什么有三个 PC 寄存器**：MIPS 有分支延迟槽，所以模拟器同时维护 PrevPC、PC 和 NextPC。
- **增加一个系统调用需要改三处**：
  1. `syscall.h` 中定义调用号 `SC_PrintInt 11` 和函数原型；
  2. `start.s` 中添加桩函数；
  3. `ExceptionHandler` 中添加对应的 case。

## 2. 从单道程序到多道程序

**原版的问题**：
- `AddrSpace` 让虚页 i 直接对应物理页 i；
- `bzero(machine->mainMemory, size)` 从物理地址 0 开始清零；
- 代码段直接读到 `mainMemory[virtualAddr]`。

第二个程序一装入，就会把第一个程序整个覆盖掉。

**修改**：

| 修改点 | 做法 |
|---|---|
| 物理页框管理 | `system.cc` 中新增全局变量 `physPageMap = new BitMap(NumPhysPages)`，把 32 个页框管起来；每个虚页通过 `physPageMap->Find()` 分配一个空闲页框 |
| SpaceId | 全局变量 `spaceIdMap` 记录哪些 ID 已被使用，每个地址空间分配一个唯一的 ID，释放后可以重复使用 |
| 清零 | 只清零分配给自己的页框（逐帧调用 `bzero`），不再碰别人的内存 |
| 装入代码段和数据段 | 新增 `LoadSegment`，**按页拆开拷贝**：先用页表把虚拟地址转换成物理地址，再读入，每一段不跨越页边界。原因是相邻的虚页不一定映射到相邻的页框，而且段的起止位置不一定和页边界对齐 |
| 内存不足 | 原来是 `ASSERT(numPages <= NumPhysPages)`，现在改成检查空闲页框够不够；不够时不分配任何东西，`IsValid()` 返回 FALSE，Exec 返回 -1 |
| 释放 | 析构函数把页框和 SpaceId 都归还 |
| `Print()` | 打印 SpaceId 和页表（虚页 → 页框），格式和 n6screen 相同，在构造函数的最后调用 |

## 3. Exec 系统调用

`SpaceId Exec(char *name)` 的处理流程：

1. **取文件名**：r4 里是用户程序中的**虚拟地址**，内核不能直接当指针使用。`ReadUserString` 逐个字节调用 `machine->ReadMem` 读取，它会通过当前页表做地址转换，一直读到 `'\0'` 为止。
2. **装入程序**：调用 `fileSystem->Open(name)`，然后 `new AddrSpace(executable)`。这一步分配页框、装入程序，并打印页表。
3. **创建线程**：`new Thread(name)`，设置 `thread->space = space`，然后 `Fork(StartUserProgram)`。新线程第一次运行时，在 `StartUserProgram` 中执行 `InitRegisters()`（PC=0，栈顶设在地址空间末尾）和 `RestoreState()`（让 `machine->pageTable` 指向自己的页表），然后调用 `machine->Run()` 开始执行用户程序。
4. **返回**：r2 设为新的 SpaceId（失败时为 -1），然后推进 PC。
5. **`Yield()`，让新程序先运行**。Nachos 只有一个 CPU，不加 `-rs` 时也没有定时器来抢占。如果不让出 CPU，新线程要等到调用者阻塞或结束时才能运行。而 exec.c 在 `Exec` 之后紧接着就调用 `Halt()`，新程序就根本没有机会运行。Linux 也有类似的"子进程先运行"策略。
   - 调用者在内核里 Yield 时，`Scheduler::Run` 会先 `SaveUserState()`，把**已经推进过的 PC 和已经写好的 r2** 保存到线程控制块中；等它重新被调度时再恢复，用户程序就能从 Exec 的下一条指令继续执行。

**和 Unix 的 exec() 有什么不同**：Unix 的 exec 是**替换**当前进程的地址空间。本实验的 Exec 是**新建**一个地址空间来运行新程序，调用者继续运行，它的地址空间不受影响。效果上相当于 Unix 的 `fork() + exec()`，或者 `posix_spawn`。

**顺便实现了 Exit**：当 main 返回时，`start.s` 中的 `__start` 会调用 `Exit(0)`，Lab7 也会用到它。处理过程是：先把 `currentThread->space` 设为 NULL，再 delete 地址空间，归还页框和 SpaceId，最后调用 `Finish()`。必须先置空，否则后面上下文切换时，调度器会把用户状态保存到已经释放的对象里。

## 4. 测试结果

**`./nachos -x ../test/exec.noff`**：输出和参考程序 n6 **逐字一致**，包括 `Ticks: total 68, idle 0, system 30, user 38`。

```
spaceID: 0 …… 虚页 0–10 → 页框 0–10
12345
Execute system call of Exec()
Exec(../test/halt2.noff):
spaceID: 1 …… 虚页 0–10 → 页框 11–21     ← 没有覆盖 space 0
67890
Machine halting!
```

**另外用临时测试程序验证**（这些测试程序没有放进仓库）：一个父进程先后两次 Exec 子进程，每个子进程 `PrintInt(111); Exit(0)`。

| 测试内容 | 结果 |
|---|---|
| 子进程 Exit 之后，父进程是否正确继续 | 正确，并且拿到了返回值 1 |
| 第二次 Exec | 复用了 SpaceId 1，说明页框和 ID 都已经归还 |
| Exec 一个不存在的文件 | 返回 -1 |
| Exec matmult（需要 54 页，只剩 20 页空闲） | 打印内存不足，返回 -1 |

## 5. 实现 Fork()/Exec() 与写时复制的方法（只写进报告，不写代码）

题目假设物理内存足够，不需要页面置换。

### 5.1 Unix 语义的 Fork()

`SpaceId Fork()` 在子进程中返回 0，在父进程中返回子进程的 SpaceId。

1. **`AddrSpace` 增加一个拷贝构造函数** `AddrSpace(AddrSpace *parent)`：
   - `numPages = parent->numPages`；
   - 分配新的 SpaceId；
   - 页表逐项拷贝（不做写时复制时，就逐页分配新页框并 `bcopy` 页面内容）。
2. **在 `ExceptionHandler` 中处理 `SC_Fork`**：
   ```cpp
   AdvancePC();                                   // 先推进 PC，父子进程都从 Fork 的下一条指令继续
   AddrSpace *childSpace = new AddrSpace(currentThread->space);
   Thread *child = new Thread("forked");
   child->space = childSpace;
   currentThread->SaveUserState();                // 把父进程当前的寄存器保存到父线程控制块
   child->CopyUserRegisters(currentThread);       // 子进程寄存器 = 父进程寄存器的拷贝
   child->SetUserRegister(2, 0);                  // 子进程中 Fork 返回 0
   child->Fork(ForkedChildStart, 0);              // ForkedChildStart: RestoreUserState + RestoreState + machine->Run()
   machine->WriteRegister(2, childSpace->GetSpaceId());   // 父进程中返回子进程的 ID
   ```
   要给 `Thread` 增加 `CopyUserRegisters` 和 `SetUserRegister` 两个方法，用来操作 `userRegisters[]`。
3. **打开文件表**（如果已经实现了 Open/Read/Write）：子进程拷贝父进程的打开文件表，共享文件读写位置。

### 5.2 Unix 语义的 Exec()（替换当前地址空间）

1. 和第 3 节一样读取文件名，打开可执行文件，`new AddrSpace(executable)` 生成新的地址空间。如果失败，返回 -1，旧的地址空间保持不变。
2. 成功后：
   - `delete currentThread->space`，归还旧的页框；
   - `currentThread->space = newSpace`；
   - 调用 `InitRegisters()` 和 `RestoreState()`；
   - 然后**不推进 PC 直接返回**，因为 `InitRegisters` 已经把 PC 设成 0 了，相当于从新程序的入口开始执行。成功的 exec 不会返回到原来的调用点。
3. SpaceId 保持不变，因为还是同一个进程。

### 5.3 写时复制（Copy-on-Write）

Fork 时不拷贝页面内容，父子进程**共享**页框，等到有一方真正要写某一页时，才复制那一页。

1. **页框引用计数**：全局数组 `int frameRefCount[NumPhysPages]`。分配页框时置 1，释放时减 1，减到 0 才在 `physPageMap` 中清除这一位。
2. **Fork 时共享页框**：子进程的页表项和父进程指向同一个页框，`frameRefCount[f]++`。父进程和子进程的页表项都设置成 `readOnly = TRUE`，并在页表项中**另外记一个软件位** `cow = TRUE`，表示"本来可写，只是暂时只读"，用来和代码段这类真正只读的页区分开。`TranslationEntry` 需要加一个 `bool cow` 字段，或者在 AddrSpace 中用一张并行的数组来记录。
3. **写入时触发异常**：用户程序写这一页时，`Machine::Translate` 检查到 `readOnly && writing`，返回 `ReadOnlyException`，`ExceptionHandler` 收到 `which == ReadOnlyException`，出错的虚拟地址在 `BadVAddrReg` 中：
   ```cpp
   vpn = machine->ReadRegister(BadVAddrReg) / PageSize;
   e = &space->pageTable[vpn];
   if (!e->cow) { /* 真正的只读页：杀死进程 */ }
   if (frameRefCount[e->physicalPage] == 1) {       // 只剩自己在用：直接恢复可写，不用复制
       e->readOnly = FALSE;  e->cow = FALSE;
   } else {                                          // 有人共享：复制一份
       int nf = physPageMap->Find();
       bcopy(&mainMemory[e->physicalPage*PageSize], &mainMemory[nf*PageSize], PageSize);
       frameRefCount[e->physicalPage]--;  frameRefCount[nf] = 1;
       e->physicalPage = nf;  e->readOnly = FALSE;  e->cow = FALSE;
   }
   // 不推进 PC：返回后重新执行这条写指令，这一次就能成功
   ```
   Nachos 在 `ReadMem`/`WriteMem` 失败时会返回 FALSE，`OneInstruction` 直接 return，下一轮循环会重新执行这条指令，所以"重新执行"是现成的。
4. **释放**：`~AddrSpace` 对每个页框执行 `frameRefCount--`，减到 0 时才归还页框。
5. **好处**：Fork 之后紧接着就 Exec 是最常见的用法，这时几乎没有页面会被真正复制，Fork 的开销从"复制整个地址空间"降低到"复制一张页表"。
