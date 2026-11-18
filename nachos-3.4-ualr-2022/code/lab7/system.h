// system.h 
//	All global variables used in Nachos are defined here.
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

#ifndef SYSTEM_H
#define SYSTEM_H

#include "copyright.h"
#include "utility.h"
#include "thread.h"
#include "scheduler.h"
#include "interrupt.h"
#include "stats.h"
#include "timer.h"

// Initialization and cleanup routines
extern void Initialize(int argc, char **argv); 	// Initialization,
						// called before anything else
extern void Cleanup();				// Cleanup, called when
						// Nachos is done.

extern Thread *currentThread;			// the thread holding the CPU
extern Thread *threadToBeDestroyed;  		// the thread that just finished
extern Scheduler *scheduler;			// the ready list
extern Interrupt *interrupt;			// interrupt status
extern Statistics *stats;			// performance metrics
extern Timer *timer;				// the hardware alarm clock

#ifdef USER_PROGRAM
#include "machine.h"
#include "bitmap.h"
extern Machine* machine;	// user program memory and registers

// Lab6: multiprogramming. Several address spaces share the physical
// memory, so we must know which page frames are free, and give each
// address space a unique SpaceId (as returned by Exec).
#define MaxSpaces	NumPhysPages	// every space needs >= 1 frame
extern BitMap *physPageMap;	// page frames in use (NumPhysPages bits)
extern BitMap *spaceIdMap;	// SpaceIds in use (MaxSpaces bits)

// Lab7: virtual memory. Every user process gets at most
// maxFramesPerProcess page frames (fixed allocation, local
// replacement); pages are loaded only when they are referenced (pure
// demand paging). The numbers of the algorithms are the same as for
// the reference program n7 (0 = optimal and 5 = random are not
// implemented here).
#define DefaultMaxFrames	5
#define MinFramesPerProcess	2	// instruction page + data page
#define PRA_FIFO		1	// first in, first out
#define PRA_CLOCK		2	// second chance (clock)
#define PRA_ENHANCED_CLOCK	3	// enhanced second chance (use, dirty)
#define PRA_LRU			4	// least recently used (default)
extern int maxFramesPerProcess;	// -mf m: frames per user process
extern int pageReplaceAlgo;	// -pra a: page replacement algorithm
#endif

#ifdef FILESYS_NEEDED 		// FILESYS or FILESYS_STUB 
#include "filesys.h"
extern FileSystem  *fileSystem;
#endif

#ifdef FILESYS
#include "synchdisk.h"
extern SynchDisk   *synchDisk;
#endif

#ifdef NETWORK
#include "post.h"
extern PostOffice* postOffice;
#endif

#endif // SYSTEM_H
