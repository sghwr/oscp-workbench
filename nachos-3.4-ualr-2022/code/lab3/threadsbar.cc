// threadsbar.cc
//	Lab3: solve the N-thread barrier problem with Nachos semaphores.
//
//	N_THREADS threads are forked. Each of them prints "rendezvous",
//	then waits at the barrier. No thread may print "critical point"
//	until all N_THREADS threads have printed "rendezvous".
//
//	Version 1: exactly the "Barrier solution" of The Little Book of
//	Semaphores, section 3.6.4 (mutex + count + turnstile).
//
//	Run with:  ./nachos -rs <seed>
//	(-rs starts a timer that interrupts the running thread at random
//	moments, so the threads really run concurrently.)

#include "copyright.h"
#include "system.h"
#include "synch.h"

#define N_THREADS  10    // the number of threads
#define MAX_NAME   16    // the maximum length of a thread name

Thread *threads[N_THREADS];		// the barrier threads
char threadNames[N_THREADS][MAX_NAME];	// their names (must outlive the
					// threads, Thread keeps the pointer)

int count = 0;			// number of threads arrived at the barrier
Semaphore *mutex;		// mutual exclusion for "count", initially 1
Semaphore *barrier;		// the turnstile, initially 0 (locked)

//----------------------------------------------------------------------
// BarThread
//	The code run by every barrier thread.
//	"which" is the number of the thread, 0 .. N_THREADS-1.
//----------------------------------------------------------------------

void
BarThread(_int which)
{
    printf("Thread %d rendezvous\n", (int) which);

    mutex->P();
    count = count + 1;
    mutex->V();

    if (count == N_THREADS) {		// NOTE: reads count outside mutex
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
