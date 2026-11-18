# Lab3 讲解：使用信号量解决 N 线程屏障问题

代码在 `code/lab3/threadsbar.cc`，共三个版本，每个版本一次提交：

| 提交 | 版本 | 对应题目 |
|---|---|---|
| `56c9465` | v1：照搬 Little Book 3.6.4 | 题 3、题 5 的现象复现 |
| `109169d` | v2：在 mutex 内记录 count | 题 4 |
| `6fd1718` | v3：rendezvous 之前调用 MakeTicks | 题 6（最终提交版） |

用 `git show <提交号>` 可以看到每一步改了什么。`main.cc` 是课程预先给好的，在 `Initialize()` 之后直接调用 `ThreadsBarrier()`，这个文件不用改。

## 1. Nachos 的信号量是怎么实现的（题 1）

源码在 `threads/synch.cc`。一个信号量由三部分组成：一个非负整数 `value`，一个等待队列 `queue`（List），以及一个调试用的名字。

```cpp
void Semaphore::P() {
    IntStatus oldLevel = interrupt->SetLevel(IntOff);   // ① 关中断 = 原子操作
    while (value == 0) {                     // ② 不可用：
        queue->Append((void *)currentThread);    //    把自己挂到等待队列
        currentThread->Sleep();                  //    阻塞并切换走（醒来后重新检查）
    }
    value--;                                 // ③ 可用：消耗一个
    (void) interrupt->SetLevel(oldLevel);    // ④ 恢复中断
}
void Semaphore::V() {
    IntStatus oldLevel = interrupt->SetLevel(IntOff);
    thread = (Thread *)queue->Remove();      // 取出一个等待者（FIFO）
    if (thread != NULL) scheduler->ReadyToRun(thread);  // 只是放进就绪队列，并不立即切换
    value++;
    (void) interrupt->SetLevel(oldLevel);
}
```

要点：
- **怎么保证原子性**：靠关中断。Nachos 是单处理器，关中断后不会再有定时器中断来触发线程切换，所以 P、V 的检查和修改不会被打断。真实的多处理器做不到这一点，需要 test-and-set 之类的原子指令。
- **`value` 永远不会小于 0**：这和教材里"负值表示等待线程数"的定义不同。Nachos 用等待队列的长度表示有多少线程在等。
- **P 里用 `while` 而不是 `if`**（Mesa 语义）：V 只是把等待者放进就绪队列，等它真正运行时，`value` 可能已经被别的线程抢先拿走了，所以醒来后必须重新检查。
- **`Sleep()` 被调用时中断是关着的**：它会断言这一点。切换到别的线程后，中断状态由那个线程恢复。所以"挂入等待队列"和"睡眠"之间不会被插入别的操作。
- `Lock` 用一个初值为 1 的信号量实现，并记录持有者 `owner`；`Condition` 用一个等待队列实现。本实验只用到了 Semaphore。

## 2. 并发线程是怎么创建和运行的（题 2）

题目说的"并发"，指线程不是自己主动调用 Yield 让出 CPU，而是被打断后切换的。

1. **创建线程**：`new Thread(name)` 只是建了一个线程控制块。`Fork(func, arg)` 调用 `StackAllocate` 分配栈，并在栈上布置好 `ThreadRoot` 的初始帧，然后 `ReadyToRun` 把线程放进就绪队列。线程第一次被 `SWITCH` 切换上 CPU 时，从 `ThreadRoot` 开始执行：先开中断，再调用 `func(arg)`，最后调用 `Finish`。
2. **为什么能被打断**：`main` 解析到 `-rs seed` 时，会调用 `RandomInit(seed)` 并设置 `randomYield`，于是 `Initialize` 创建了一个 `Timer`。之后每隔 `1 + Random() % 200` ticks 产生一次定时器中断，处理函数 `TimerInterruptHandler` 调用 `interrupt->YieldOnReturn()`。
3. **打断在什么时候真正发生**：中断不会凭空打断 C++ 代码。只有模拟时钟前进，也就是调用 `Interrupt::OneTick()` 时，才会检查有没有到期的中断。内核线程只在重新开中断（`SetLevel(IntOn)`，即 P/V、Yield 等操作内部）时才会推进时钟，每次 10 ticks。在 `OneTick` 里，如果 `yieldOnReturn` 被设置了，就调用 `currentThread->Yield()`，于是线程在一个"随机"的时刻被换下 CPU。
4. 不加 `-rs` 时不会创建定时器，线程只会在 P 阻塞或者 Finish 时让出 CPU，所以执行顺序完全是固定的。

## 3. 照搬书上 3.6.4 的代码：多个线程都认为自己最后到达（题 3）

v1 的写法：

```cpp
mutex->P();  count = count + 1;  mutex->V();
if (count == N_THREADS) { printf("... is the last"); barrier->V(); }   // 在锁外读 count
barrier->P();  barrier->V();      // 十字转门（turnstile）
```

**实测**（300 个随机种子）：有 **67 次**出现了多个线程都打印"is the last"。比如 `./nachos -rs 12` 中，线程 9、1、5 都认为自己是最后一个。

**原因**：`mutex->V()` 返回前会重新打开中断，打开中断时会调用 `OneTick`，定时器可能恰好在这一刻到期，于是线程被抢占，停在"出锁"和"判断 `count == N`"之间。等它重新运行时，其他线程早就把 count 加到了 10，它读到的也是 10。所以任何一个在这个空隙被抢占、并且在 count 达到 10 之后才恢复运行的线程，都会认为自己是最后一个。线程 1 就是典型：它是第 2 个自增 count 的，却在最后才执行判断。

**会不会违反题目要求**：不会。我们检查了 v1 的 300 次运行，从来没有出现某个线程在第 10 个 rendezvous 之前就打印 critical point。原因是，不管哪个线程执行 `barrier->V()`，它在此之前都已经看到了 count == 10，也就是所有线程都已经到达了。

但这个现象仍然是错误的：
- **"谁是最后到达者"这个结论是错的**。如果最后到达的线程要承担特殊工作，比如重置 count、汇总结果、只能执行一次的初始化，这些工作就会被执行多次。
- **barrier 被多 V 了几次**。屏障通过后，barrier 的值不再是 1，而是"自认最后的线程数"。对只用一次的屏障来说没有影响；但如果把它改成可重复使用的屏障（书上 3.7 节），多出来的值会让下一轮的线程提前通过，屏障就失效了。
- 书上作者自己也提醒过："在 mutex 外读 count 看起来很危险……一般来说不是好主意。"

## 4. 修改代码消除这个现象（题 4）

在持有 mutex 时，把自增之后的值存进**局部变量**，出锁之后只比较这个局部变量：

```cpp
mutex->P();
count = count + 1;
arrived = count;          // 局部变量在本线程自己的栈上，别的线程改不了
mutex->V();
if (arrived == N_THREADS) { ... barrier->V(); }
```

只有那个"把 count 从 9 加到 10"的线程，看到的 `arrived` 才等于 10，所以**恰好有一个**最后到达者。实测 300 个种子：每次都恰好一个线程认为自己最后到达，10 行 critical point 全部输出。

## 5. rendezvous 的顺序基本就是创建顺序（题 5）

**实测**：v1 和 v2 在 300 个种子中，有 **149 次**的 rendezvous 顺序恰好是 0、1、2…9，其余也基本有序。

**`-rs` 起作用了吗**：起作用了。上面题 3 的现象正是 `-rs` 定时器造成的。问题在于线程一开始运行就立刻打印 rendezvous，**中间没有推进任何模拟时间**，定时器根本没有机会插进来：
- 就绪队列是先进先出的，线程按创建顺序 0..9 依次第一次得到 CPU；
- 线程得到 CPU 后马上打印，这两步之间不会被打断；
- 所以打印顺序就是创建顺序。

少数不按顺序的情况发生在 `ThreadRoot` 开中断，或者 `mutex` 的 P/V 开中断时，定时器恰好到期。

**加软件空循环或 Linux 的 `sleep` 能解决吗**：不能。我们实测了两种做法：
- 在 rendezvous 之前加空循环，并且编号越大的线程循环越久（`for (volatile long d = 0; d < 20000000L*(which+1); d++);`）；
- 在 rendezvous 之前加 `sleep(1)`。

| 版本（种子 1） | 模拟时钟 Ticks total | 真实耗时 | 顺序恰为 0..9（8 个种子） |
|---|---|---|---|
| v1 原样 | 774 | 瞬间 | — |
| 加空循环 | **774** | 约 2 秒/次 | 6/8 |
| 加 `sleep(1)` | **774** | 约 10 秒/次 | 6/8 |

模拟时钟一个 tick 都没多走。空循环和 `sleep` 消耗的是**宿主机的真实时间**，它们不会调用 `OneTick`，模拟时钟不前进，定时器中断也就不会到期。从 Nachos 内部看，这段延迟"不存在"。另外，Nachos 整个是一个 Linux 进程，`sleep` 只是让这个进程整体暂停，Nachos 里的所有线程一起停下，相对顺序不会改变。

## 6. 修改代码解决顺序问题（题 6）

只改我们自己写的屏障代码。打印 rendezvous 之前，先调用 `MakeTicks(N_TICKS)` 做一段"工作"，让模拟时钟**真正前进**：

```cpp
void MakeTicks(int n) {
    for (int i = 0; i < n; i++) {
        interrupt->SetLevel(IntOff);
        interrupt->SetLevel(IntOn);   // → OneTick：时钟加 10，检查定时器，可能被抢占
    }
}
```

- 每个线程要走 1000 次 OneTick，也就是 10000 ticks。在这段时间里，随机定时器平均每 100 ticks 抢占一次，所以每个线程会被切成大约 100 段，每段的长度都是随机的。哪个线程先做完取决于随机种子，rendezvous 的顺序因此被真正打乱了。
- 这里用的是 Nachos 内核的公开接口 `SetLevel`，并没有修改内核。

**实测**（300 个种子）：
- 顺序恰为 0..9 的次数是 **0**；
- 第一个到达的线程在 0–9 之间分布比较均匀，分别是 46、23、32、30、31、30、33、21、28、26 次。线程 0 稍多一些，因为它最先开始运行；
- 每次都恰好一个线程认为自己最后到达，没有违反屏障条件；
- **在全部 300 个种子下，rendezvous 顺序都和参考程序 n3 完全相同**；`-rs 1` 的 Ticks total 也同为 111003；
- critical point 的先后顺序和 n3 有少量差异。题目对这个顺序没有要求，原因是参考程序在屏障之后的代码和我们的写法不同。

**不加 `-rs` 时顺序仍然是 0..9**：没有定时器，就不会有抢占，这本来就是确定性的行为，不是 bug。

## 7. 验收演示

```bash
cd code/lab3 && make
./nachos -rs 1      # rendezvous 顺序和 n3screen.txt 相同：3 2 8 9 4 0 1 5 6 7
./nachos -rs 12     # 多换几个种子，顺序都不同，并且只有一个 "is the last"
```
