# Lab2 讲解：具有优先级的线程调度

代码在 `code/lab2/`。在 git 里可以看到两个提交：步骤 A（`5b5904c`，实现优先级调度）和步骤 B（`738dd0f`，减少上下文切换）。用 `git show <提交号>` 能看到每一步具体改了哪几行。

## 1. Nachos 原有的调度策略

原有策略是**先来先服务（FCFS），非抢占式**。如果命令行加了 `-rs`，会额外创建一个随机时间片定时器，这时就变成了时间片轮转。

| 函数（threads/） | 作用 |
|---|---|
| `Scheduler::ReadyToRun(t)` | 把 t 的状态设为 READY，并用 `readyList->Append` **放到队尾** |
| `Scheduler::FindNextToRun()` | 用 `readyList->Remove()` **取出队头**；队列空时返回 NULL |
| `Scheduler::Run(next)` | 调用 `SWITCH`（汇编），保存旧线程的寄存器、恢复新线程的寄存器，完成上下文切换 |
| `Thread::Yield()` | 主动让出 CPU。**先**取出下一个线程，**再**把自己放回就绪队列，然后切换过去；如果没有别的线程可运行，就直接返回 |
| `Thread::Sleep()` | 线程阻塞（等信号量，或者线程结束）。状态设为 BLOCKED，不放回就绪队列；如果没有线程可运行，就调用 `Idle` 等待中断 |
| `Thread::Finish()` | 设置 `threadToBeDestroyed` 后调用 `Sleep`，由下一个开始运行的线程负责回收它的栈 |

线程什么时候会让出 CPU：
- 不加 `-rs` 时**没有定时器**，线程只有在主动调用 `Yield`，或者因为 `Sleep`/`Finish` 阻塞时才会让出 CPU。
- 加 `-rs` 时，`Timer` 每隔一段随机时间产生一次中断，`TimerInterruptHandler` 调用 `interrupt->YieldOnReturn()`，中断返回时让当前线程 Yield，形成时间片轮转。

## 2. 静态优先级、非抢占式调度的实现

**一共只改了 3 处。**

1. **thread.h / thread.cc**：给线程加一个 `priority` 成员，0 最高、99 最低、默认 9。构造函数改成 `Thread(const char* name, int prio = DefaultPriority)`，有默认参数，所以原来所有的 `new Thread("xxx")` 都不用改。构造函数里用 `ASSERT` 检查优先级是否在合法范围内。
2. **scheduler.cc**：`ReadyToRun` 中的 `Append` 改成 `SortedInsert(thread, thread->getPriority())`，就绪队列于是按优先级从高到低排列，`FindNextToRun` 取出的队头就是优先级最高的线程。
   - `List::SortedInsert` 遇到相同的 key，会把新元素插在已有元素的**后面**（看它的判断条件 `sortKey < ptr->next->key`），所以同优先级的线程之间仍然是先来先服务。
3. **非抢占式**：一个高优先级线程变为就绪（比如被 `Fork` 出来）时，不会把正在运行的线程赶下 CPU。**所以这一点什么都不用加**，`Fork` 只是把新线程放进就绪队列。

### 为什么输出是 t1、t2 交替运行，而不是 t1 一口气跑完？

关键在于 `Yield` 的执行顺序：它**先**取出下一个线程，**再**把自己放回就绪队列。所以 t1 调用 Yield 时，自己暂时不在队列里，只能从剩下的线程里选，选中的是优先级最高的 t2。轮到 t2 Yield 时，队列里最高的又是 t1。

```
main 打印 0-0 → Yield：取出 t1，main(9) 入队    队列: t2 t3 main
t1   打印 1-0 → Yield：取出 t2，t1(1) 入队      队列: t1 t3 main
t2   打印 2-0 → Yield：取出 t1，t2(2) 入队      队列: t2 t3 main
…… t1 和 t2 交替，直到两个都结束；然后 t3 和 main 交替
```

## 3. `-d t` 下的上下文切换次数（思考题 3）

统计方法：`./nachos -d t | grep -c '^Switching'`，然后和打印行数比较。

| 版本 | 打印行数 | 上下文切换次数 | Ticks total |
|---|---|---|---|
| 步骤 A（原版 SimpleThread，每次打印后都 Yield） | 20 | **23** | 270 |
| 步骤 B（最后一次打印后不再 Yield） | 20 | **19** | 230 |
| 参考程序 n2 | 20 | 19 | 230 |

**多出的 4 次**：每个线程打印完第 5 行后，仍然会调用一次 `Yield` 切换到别的线程（第 1 次多余切换）。之后它被重新调度回来，什么都不打印，只是退出循环、调用 `Finish`，`Finish` 又要切换一次（第 2 次多余切换）。t1/t2 和 t3/main 这两对线程各多出 2 次，一共 4 次。
- 每次 Yield 都要关中断再开中断。开中断时 `OneTick` 让系统时钟前进 `SystemTick = 10`，所以 4 次多余的 Yield 恰好多出 40 ticks（270 − 230）。

**少掉的 1 次**：main 打印第 1 行时本来就在运行，不需要切换。另外最后一个线程 t3 调用 `Finish` 时，就绪队列已经空了，`Sleep` 里进入 `Idle`，然后机器停机，所以这次也没有切换。

**修改方法**：在 `SimpleThread` 的最后一轮循环里不调用 `Yield`（`if (num < 5 - 1)`），因为 `Finish` 本身就会让出 CPU。修改之后，除第 1 行以外，每一行都是由一个和上一行不同的线程打印的，所以每行恰好对应一次切换。19 次已经是最少的了。

## 4. 优先级老化（aging）的实现方法（只写进报告，不写代码）

**问题**：静态优先级可能导致低优先级线程长期得不到 CPU，也就是"饥饿"。比如本实验中，main（优先级 9）在 t1、t2 运行期间一直在等待。

**思路**：线程在就绪队列里每等待一段时间，优先级就提高一级；一旦拿到 CPU，就恢复成原来的静态优先级。

1. **Thread 类增加成员**
   ```cpp
   int basePriority;   // 构造时给定的静态优先级，永远不变
   int priority;       // 实际用于调度的"有效优先级"
   int readyTime;      // 最近一次进入就绪队列时的 stats->totalTicks
   ```
2. **`Scheduler::ReadyToRun`**：记录 `thread->readyTime = stats->totalTicks`，再按有效优先级 `SortedInsert`。
3. **老化的时机**：在 `FindNextToRun` 取队头之前调用 `Scheduler::Age()`。也可以在定时器中断处理函数里定期调用，但要注意，不加 `-rs` 时没有定时器。
   ```cpp
   #define AgingInterval 50                 // 每等待 50 ticks
   void Scheduler::Age() {                  // 调用时已关中断
       List *tmp = new List;
       Thread *t;
       while ((t = (Thread *)readyList->Remove()) != NULL) {
           int waited = stats->totalTicks - t->readyTime;
           int newPrio = t->basePriority - waited / AgingInterval;
           t->priority = (newPrio < MaxPriority) ? MaxPriority : newPrio;
           tmp->SortedInsert(t, t->priority);   // 按新优先级重新排序
       }
       delete readyList;  readyList = tmp;
   }
   ```
   - **为什么要重建整个队列**：`ListElement` 的 key 是在插入时复制进去的，单独修改线程的 `priority` 并不会改变它在队列中的位置，所以必须全部取出、按新优先级重新插入。
   - **为什么用"等待时长"而不是"每次减 1"**：老化的幅度只取决于等了多久，和调度被触发了多少次无关，结果更稳定。
4. **`Scheduler::Run`**：切换到 nextThread 时，把 `priority = basePriority` 复位，避免老化提上来的优先级一直保留。
5. **仍然是非抢占式**：老化只改变就绪队列里的先后顺序，不会打断正在运行的线程。如果要实现抢占，还要在 `ReadyToRun` 中比较新就绪线程和 currentThread 的优先级，必要时调用 `interrupt->YieldOnReturn()`（在中断上下文中）或 `currentThread->Yield()`。

## 5. 验收演示

```bash
cd code/lab2 && make
./nachos                         # 和 n2screen.txt 完全一致
./nachos -d t | grep -c '^Switching'   # 19
./nachos -d t | grep -c looped         # 20
```
