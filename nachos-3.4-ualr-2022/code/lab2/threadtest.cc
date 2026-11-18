// threadtest.cc 
//	Simple test case for the threads assignment.
//
//	Create two threads, and have them context switch
//	back and forth between themselves by calling Thread::Yield, 
//	to illustratethe inner workings of the thread system.
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

#include "copyright.h"
#include "system.h"

//----------------------------------------------------------------------
// SimpleThread
// 	Loop 5 times, yielding the CPU to another ready thread 
//	each iteration.
//
//	"which" is simply a number identifying the thread, for debugging
//	purposes.
//----------------------------------------------------------------------

void
SimpleThread(_int which)
{
    int num;
    
    for (num = 0; num < 5; num++) {
	printf("*** thread %d looped %d times, priority=%d\n",
	       (int) which, num, currentThread->getPriority());
        currentThread->Yield();
    }
}

//----------------------------------------------------------------------
// ThreadTest
// 	Lab2: test the static priority, non-preemptive scheduler.
//	Create 3 threads t1, t2, t3 with priority 1, 2, 3 respectively,
//	fork them in that order, then call SimpleThread ourselves in
//	the "main" thread, which has the default priority 9.
//----------------------------------------------------------------------

void
ThreadTest()
{
    DEBUG('t', "Entering SimpleTest\n");

    Thread *t1 = new Thread("forked thread 1", 1);
    Thread *t2 = new Thread("forked thread 2", 2);
    Thread *t3 = new Thread("forked thread 3", 3);

    t1->Fork(SimpleThread, 1);
    t2->Fork(SimpleThread, 2);
    t3->Fork(SimpleThread, 3);
    SimpleThread(0);
}

