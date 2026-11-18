// threadsbar.cc
//	Lab3: solve the N-thread barrier problem with Nachos semaphores.
//
//	N_THREADS threads are forked. Each of them first does some work
//	(MakeTicks), prints "rendezvous", then waits at the barrier.
//	No thread may print "critical point" until all N_THREADS threads
//	have printed "rendezvous".
//
//	Version 1: exactly the "Barrier solution" of The Little Book of
//	Semaphores, section 3.6.4 (mutex + count + turnstile).
//	Version 2: only the thread whose increment made count reach
//	N_THREADS is the last one (see BarThread).
//	Version 3: each thread advances the simulated clock (MakeTicks)
//	before "rendezvous", so the -rs timer really interleaves them and
//	the rendezvous order is no longer simply the creation order.
//
//	Run with:  ./nachos -rs <seed>
//	(-rs starts a timer that interrupts the running thread at random
//	moments, so the threads really run concurrently.)

#include "copyright.h"
#include "system.h"
#include "synch.h"

#define N_THREADS  10    // the number of threads
#define N_TICKS    1000  // the number of ticks to advance simulated time
#define MAX_NAME   16    // the maximum length of a thread name

Thread *threads[N_THREADS];		// the barrier threads
char threadNames[N_THREADS][MAX_NAME];	// their names (must outlive the
					// threads, Thread keeps the pointer)

int count = 0;			// number of threads arrived at the barrier
Semaphore *mutex;		// mutual exclusion for "count", initially 1
Semaphore *barrier;		// the turnstile, initially 0 (locked)

//----------------------------------------------------------------------
// MakeTicks
//	Let n ticks of the simulated clock pass, i.e. pretend that the
//	thread is doing some work.
//
//	A Nachos kernel thread runs as real host code, which does not
//	advance the simulated clock at all: a busy loop or a Linux sleep()
//	only burns *real* time, the simulated clock stays where it is, so
//	no timer interrupt can happen during it. The clock only moves when
//	interrupts are re-enabled: Interrupt::SetLevel(IntOn) calls
//	OneTick(), which adds SystemTick (10) to totalTicks and fires any
//	interrupt that is due -- including the -rs timer interrupt, which
//	makes the running thread Yield. So each off/on pair below is one
//	tick, and it is also a point where the thread can be preempted.
//----------------------------------------------------------------------

void
MakeTicks(int n)
{
    for (int i = 0; i < n; i++) {
	interrupt->SetLevel(IntOff);
	interrupt->SetLevel(IntOn);
    }
}

//----------------------------------------------------------------------
// BarThread
//	The code run by every barrier thread.
//	"which" is the number of the thread, 0 .. N_THREADS-1.
//----------------------------------------------------------------------

void
BarThread(_int which)
{
    int arrived;		// value of count right after *my* increment

    // Version 3: do some work first. Without it a thread prints
    // "rendezvous" as soon as it gets the CPU for the first time, and
    // no simulated time passes in between, so the timer almost never
    // gets a chance to interrupt it: threads are started from the FIFO
    // ready list in creation order, hence print in order 0, 1, ..., 9.
    // While the threads are doing MakeTicks, the random timer slices
    // their work at random points, so who finishes first depends on
    // the -rs seed.
    MakeTicks(N_TICKS);
    printf("Thread %d rendezvous\n", (int) which);

    // Version 2: remember the value of count while still holding the
    // mutex. In version 1, "if (count == N_THREADS)" read count *after*
    // mutex->V(). mutex->V() re-enables interrupts, so the -rs timer can
    // preempt the thread right there; if it only runs again after the
    // other threads have arrived, it reads count == N_THREADS too and
    // also believes it is the last one. "arrived" is a local variable
    // (on this thread's own stack), so nobody else can change it, and
    // exactly one thread sees arrived == N_THREADS.
    mutex->P();
    count = count + 1;
    arrived = count;
    mutex->V();

    if (arrived == N_THREADS) {
	printf("Thread %d is the last\n", (int) which);
	barrier->V();
    }

    barrier->P();			// turnstile: wait ...
    barrier->V();			// ... and let the next one pass

    printf("Thread %d critical point\n", (int) which);
}

//----------------------------------------------------------------------
// ThreadsBarrier
//	Called by main(). Create the semaphores, then create and fork
//	N_THREADS threads running BarThread.
//----------------------------------------------------------------------

void
ThreadsBarrier()
{
    int i;

    DEBUG('t', "Entering ThreadsBarrier\n");

    mutex = new Semaphore("mutex", 1);
    barrier = new Semaphore("barrier", 0);

    // Create and fork N_THREADS threads
    for (i = 0; i < N_THREADS; i++) {
	sprintf(threadNames[i], "BarThread %d", i);
	threads[i] = new Thread(threadNames[i]);
	threads[i]->Fork(BarThread, i);
    }
}
