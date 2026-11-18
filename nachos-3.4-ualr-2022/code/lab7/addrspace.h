// addrspace.h
//	Data structures to keep track of executing user programs
//	(address spaces).
//
//	For now, we don't keep any information about address spaces.
//	The user level CPU state is saved and restored in the thread
//	executing the user program (see thread.h).
//
//	Lab7: virtual memory with pure demand paging. The whole image of
//	the address space lives in a swap file "SWAPn" (n = SpaceId); a
//	page is brought into a page frame only when it is referenced. A
//	process has at most maxFramesPerProcess frames (fixed allocation);
//	when it needs another one, one of its own pages is replaced (local
//	replacement), chosen by the algorithm pageReplaceAlgo.
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation
// of liability and disclaimer of warranty provisions.

#ifndef ADDRSPACE_H
#define ADDRSPACE_H

#include "copyright.h"
#include "filesys.h"

#define UserStackSize		1024 	// increase this as necessary!

class AddrSpace {
  public:
    AddrSpace(OpenFile *executable, char *fileName);
					// Create an address space,
					// initializing it with the program
					// stored in the file "executable"
					// (Lab7: "fileName" is its name)
    ~AddrSpace();			// De-allocate an address space

    void InitRegisters();		// Initialize user-level CPU registers,
					// before jumping to user code

    void SaveState();			// Save/restore address space-specific
    void RestoreState();		// info on a context switch

    void Print();			// Lab6/7: print the SpaceId and the
					// page table
    int GetSpaceId() { return spaceId; }	// Lab6
    bool IsValid() { return spaceId >= 0; }	// Lab6: FALSE if the
					// program could not be loaded

    void PageFault(int virtAddr);	// Lab7: bring the page containing
					// virtAddr into memory
    void MakeResident(int virtAddr);	// Lab7: same, but only if it is not
					// in memory (for the kernel, which
					// reads user memory in system calls)

  private:
    TranslationEntry *pageTable;	// Assume linear page table translation
					// for now!
    unsigned int numPages;		// Number of pages in the virtual
					// address space
    int spaceId;			// Lab6: unique id of this address
					// space, -1 if not loaded

    // Lab7: virtual memory
    char swapFileName[16];		// "SWAPn"
    OpenFile *swapFile;			// backing store: page i of the
					// address space is at offset
					// i * PageSize in this file
    int maxFrames;			// at most this many frames
    int numFrames;			// frames allocated so far
    int *frameOf;			// frameOf[slot]: page frame number of
					// the slot-th frame of this process
    int *pageInFrame;			// pageInFrame[slot]: virtual page in
					// that frame, -1 if none
    int hand;				// FIFO / clock: next slot to look at

    int FindVictim();			// choose a slot whose page is replaced
    const char *AlgorithmName();	// name of pageReplaceAlgo
};

#endif // ADDRSPACE_H
