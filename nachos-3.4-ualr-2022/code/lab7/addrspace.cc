// addrspace.cc 
//	Routines to manage address spaces (executing user programs).
//
//	In order to run a user program, you must:
//
//	1. link with the -N -T 0 option 
//	2. run coff2noff to convert the object file to Nachos format
//		(Nachos object code format is essentially just a simpler
//		version of the UNIX executable object code format)
//	3. load the NOFF file into the Nachos file system
//		(if you haven't implemented the file system yet, you
//		don't need to do this last step)
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

#include "copyright.h"
#include "system.h"
#include "addrspace.h"
#include "noff.h"

//----------------------------------------------------------------------
// SwapHeader
// 	Do little endian to big endian conversion on the bytes in the 
//	object file header, in case the file was generated on a little
//	endian machine, and we're now running on a big endian machine.
//----------------------------------------------------------------------

static void 
SwapHeader (NoffHeader *noffH)
{
	noffH->noffMagic = WordToHost(noffH->noffMagic);
	noffH->code.size = WordToHost(noffH->code.size);
	noffH->code.virtualAddr = WordToHost(noffH->code.virtualAddr);
	noffH->code.inFileAddr = WordToHost(noffH->code.inFileAddr);
	noffH->initData.size = WordToHost(noffH->initData.size);
	noffH->initData.virtualAddr = WordToHost(noffH->initData.virtualAddr);
	noffH->initData.inFileAddr = WordToHost(noffH->initData.inFileAddr);
	noffH->uninitData.size = WordToHost(noffH->uninitData.size);
	noffH->uninitData.virtualAddr = WordToHost(noffH->uninitData.virtualAddr);
	noffH->uninitData.inFileAddr = WordToHost(noffH->uninitData.inFileAddr);
}

//----------------------------------------------------------------------
// AddrSpace::AddrSpace
// 	Create an address space to run a user program.
//	Load the program from a file "executable", and set everything
//	up so that we can start executing user instructions.
//
//	Assumes that the object code file is in NOFF format.
//
//	Lab7: virtual memory, pure demand paging. Nothing is loaded into
//	physical memory here: all pages are invalid, so the very first
//	instruction fetch causes a page fault. Instead, the whole image
//	of the address space (code, initialized data, and zeros for the
//	uninitialized data and the stack) is written into the swap file
//	SWAPn, from which PageFault() reads the pages when they are
//	referenced. So a program may be bigger than physical memory.
//
//	"executable" is the file containing the object code to load into memory
//	"fileName" is its name (only printed)
//----------------------------------------------------------------------

AddrSpace::AddrSpace(OpenFile *executable, char *fileName)
{
    NoffHeader noffH;
    unsigned int i, size;
    char *image;			// the initial image of the space

    pageTable = NULL;
    numPages = 0;
    spaceId = -1;
    swapFile = NULL;
    frameOf = pageInFrame = NULL;
    numFrames = hand = 0;
    maxFrames = maxFramesPerProcess;

    executable->ReadAt((char *)&noffH, sizeof(noffH), 0);
    if ((noffH.noffMagic != NOFFMAGIC) &&
		(WordToHost(noffH.noffMagic) == NOFFMAGIC))
    	SwapHeader(&noffH);
    ASSERT(noffH.noffMagic == NOFFMAGIC);

// how big is address space?
    size = noffH.code.size + noffH.initData.size + noffH.uninitData.size
			+ UserStackSize;	// we need to increase the size
						// to leave room for the stack
    numPages = divRoundUp(size, PageSize);
    size = numPages * PageSize;

    // Lab7: the program need not fit into physical memory any more; it
    // only needs at least one free page frame to run in
    if (physPageMap->NumClear() == 0 || spaceIdMap->NumClear() == 0) {
	printf("Not enough memory to start the program %s\n", fileName);
	numPages = 0;
	return;				// IsValid() is FALSE
    }
    spaceId = spaceIdMap->Find();

    DEBUG('a', "Initializing address space %d, num pages %d, size %d\n",
					spaceId, numPages, size);

// first, set up the translation: no page is in memory yet
    pageTable = new TranslationEntry[numPages];
    for (i = 0; i < numPages; i++) {
	pageTable[i].virtualPage = i;
	pageTable[i].physicalPage = -1;	// Lab7: not in any frame
	pageTable[i].valid = FALSE;	// Lab7: pure demand paging
	pageTable[i].use = FALSE;
	pageTable[i].dirty = FALSE;
	pageTable[i].readOnly = FALSE;  // if the code segment was entirely on
					// a separate page, we could set its
					// pages to be read-only
	pageTable[i].lastUse = 0;
    }

// Lab7: build the image of the address space: zeros (uninitialized data
// and stack), with the code and data segments copied in
    image = new char[size];
    bzero(image, size);
    if (noffH.code.size > 0) {
        DEBUG('a', "Initializing code segment, at 0x%x, size %d\n",
			noffH.code.virtualAddr, noffH.code.size);
	ASSERT((unsigned int) (noffH.code.virtualAddr + noffH.code.size) <= size);
        executable->ReadAt(&image[noffH.code.virtualAddr],
			noffH.code.size, noffH.code.inFileAddr);
    }
    if (noffH.initData.size > 0) {
        DEBUG('a', "Initializing data segment, at 0x%x, size %d\n",
			noffH.initData.virtualAddr, noffH.initData.size);
	ASSERT((unsigned int) (noffH.initData.virtualAddr
				+ noffH.initData.size) <= size);
        executable->ReadAt(&image[noffH.initData.virtualAddr],
			noffH.initData.size, noffH.initData.inFileAddr);
    }

// Lab7: ... and write it into the swap file, page i at offset i*PageSize
    sprintf(swapFileName, "SWAP%d", spaceId);
    ASSERT(fileSystem->Create(swapFileName, size));
    swapFile = fileSystem->Open(swapFileName);
    ASSERT(swapFile != NULL);
    ASSERT(swapFile->WriteAt(image, size, 0) == (int) size);
    delete [] image;

// Lab7: the frames of this process, allocated on demand in PageFault
    frameOf = new int[maxFrames];
    pageInFrame = new int[maxFrames];
    for (i = 0; i < (unsigned int) maxFrames; i++)
	frameOf[i] = pageInFrame[i] = -1;

    printf("User program: %s, SpaceId: %d, Memory size: %d\n",
	   fileName, spaceId, size);
    printf("Max frames per user process: %d, Swap file: %s, "
	   "Page replacement algorithm: %s\n",
	   maxFrames, swapFileName, AlgorithmName());
    Print();
}

//----------------------------------------------------------------------
// AddrSpace::~AddrSpace
// 	Dealloate an address space.
//	Lab6: give its page frames and its SpaceId back.
//	Lab7: only the frames it really got; remove the swap file.
//----------------------------------------------------------------------

AddrSpace::~AddrSpace()
{
    for (int i = 0; i < numFrames; i++)
	physPageMap->Clear(frameOf[i]);
    if (spaceId >= 0)
	spaceIdMap->Clear(spaceId);
    if (swapFile != NULL) {
	delete swapFile;
	fileSystem->Remove(swapFileName);
    }
    delete [] frameOf;
    delete [] pageInFrame;
    delete [] pageTable;
}

//----------------------------------------------------------------------
// AddrSpace::AlgorithmName
// 	Lab7: the name of the page replacement algorithm in use.
//----------------------------------------------------------------------

const char *
AddrSpace::AlgorithmName()
{
    switch (pageReplaceAlgo) {
      case PRA_FIFO:		return "FIFO";
      case PRA_CLOCK:		return "2nd Chance(Clock)";
      case PRA_ENHANCED_CLOCK:	return "Enhanced 2nd Chance";
      case PRA_LRU:		return "LRU(Stack)";
      default:			return "unknown";
    }
}

//----------------------------------------------------------------------
// AddrSpace::PageFault
// 	Lab7: the page containing "virtAddr" is not in memory (its page
//	table entry is invalid, so the MMU raised a PageFaultException).
//	Get a page frame for it:
//	  - if this process still has fewer than maxFrames frames, take
//	    a free frame from physPageMap;
//	  - otherwise replace one of this process's own pages (local
//	    replacement), chosen by FindVictim(); if the victim has been
//	    modified (dirty), first write it back to the swap file.
//	Then read the page from the swap file into the frame and make its
//	page table entry valid.
//
//	The faulting instruction is NOT skipped: after the exception the
//	simulator executes it again, and this time the translation works.
//----------------------------------------------------------------------

void
AddrSpace::PageFault(int virtAddr)
{
    int vpn = (unsigned) virtAddr / PageSize;
    int slot, frame, victim;

    ASSERT(vpn >= 0 && vpn < (int) numPages);
    ASSERT(!pageTable[vpn].valid);
    stats->numPageFaults++;

    if (numFrames < maxFrames && physPageMap->NumClear() > 0) {
	// a new frame for this process
	slot = numFrames++;
	frame = frameOf[slot] = physPageMap->Find();
	printf("Demand page %d in(frame %d)\n", vpn, frame);
    } else {
	// replace one of our own pages
	ASSERT(numFrames > 0);
	slot = FindVictim();
	frame = frameOf[slot];
	victim = pageInFrame[slot];
	printf("Swap page %d out, demand page %d in(frame %d)\n",
	       victim, vpn, frame);
	if (pageTable[victim].dirty) {		// modified: save it
	    swapFile->WriteAt(&(machine->mainMemory[frame * PageSize]),
			      PageSize, victim * PageSize);
	    stats->numPageWrites++;
	    printf("Write back victim page %d to disk\n", victim);
	}
	// (a clean page need not be written: the swap file still has an
	// up-to-date copy of it)
	pageTable[victim].valid = FALSE;
	pageTable[victim].physicalPage = -1;
	pageTable[victim].use = FALSE;
	pageTable[victim].dirty = FALSE;
    }

    // bring the page in
    swapFile->ReadAt(&(machine->mainMemory[frame * PageSize]),
		     PageSize, vpn * PageSize);
    pageTable[vpn].physicalPage = frame;
    pageTable[vpn].valid = TRUE;
    pageTable[vpn].use = TRUE;		// it is about to be referenced
    pageTable[vpn].dirty = FALSE;	// same as the copy in the swap file
    pageTable[vpn].lastUse = referenceClock;
    pageInFrame[slot] = vpn;

    Print();
}

//----------------------------------------------------------------------
// AddrSpace::MakeResident
// 	Lab7: make sure the page containing "virtAddr" is in memory. The
//	kernel calls this before reading user memory in a system call
//	(e.g. the file name of Exec), so that machine->ReadMem does not
//	cause a page fault inside the kernel.
//----------------------------------------------------------------------

void
AddrSpace::MakeResident(int virtAddr)
{
    unsigned int vpn = (unsigned) virtAddr / PageSize;

    if (vpn < numPages && !pageTable[vpn].valid)
	PageFault(virtAddr);
}

//----------------------------------------------------------------------
// AddrSpace::FindVictim
// 	Lab7: choose which of this process's frames gets a new page;
//	return its slot (0 .. numFrames-1). All slots are in use.
//
//	FIFO: the frames are filled in slot order 0, 1, 2, ..., and then
//	    replaced in the same round-robin order, so "hand" always
//	    points to the page that has been in memory the longest.
//	2nd Chance (clock): like FIFO, but a page whose use bit is set
//	    gets a second chance: clear its use bit and go on.
//	Enhanced 2nd Chance: consider (use, dirty) pairs. Look for the
//	    best class (0,0) -- not used recently, clean; then for (0,1),
//	    clearing the use bits of the pages passed; then again. A clean
//	    page is preferred because it need not be written back.
//	LRU: the page whose last reference is the oldest (smallest
//	    lastUse, which the MMU sets on every reference).
//----------------------------------------------------------------------

int
AddrSpace::FindVictim()
{
    int slot, i, round;
    TranslationEntry *e;

    switch (pageReplaceAlgo) {
      case PRA_FIFO:
	slot = hand;
	hand = (hand + 1) % numFrames;
	return slot;

      case PRA_CLOCK:
	for (;;) {
	    e = &pageTable[pageInFrame[hand]];
	    slot = hand;
	    hand = (hand + 1) % numFrames;
	    if (!e->use)
		return slot;
	    e->use = FALSE;			// second chance
	}

      case PRA_ENHANCED_CLOCK:
	for (round = 0; ; round++) {
	    // step 1: look for (use, dirty) = (0, 0), change nothing
	    for (i = 0; i < numFrames; i++) {
		e = &pageTable[pageInFrame[hand]];
		slot = hand;
		hand = (hand + 1) % numFrames;
		if (!e->use && !e->dirty)
		    return slot;
	    }
	    // step 2: look for (0, 1), clearing the use bits on the way
	    for (i = 0; i < numFrames; i++) {
		e = &pageTable[pageInFrame[hand]];
		slot = hand;
		hand = (hand + 1) % numFrames;
		if (!e->use && e->dirty)
		    return slot;
		e->use = FALSE;
	    }
	    ASSERT(round < 2);	// after clearing, step 1 or 2 must succeed
	}

      case PRA_LRU:
      default:
	slot = 0;
	for (i = 1; i < numFrames; i++)
	    if (pageTable[pageInFrame[i]].lastUse
		    < pageTable[pageInFrame[slot]].lastUse)
		slot = i;
	return slot;
    }
}

//----------------------------------------------------------------------
// AddrSpace::Print
// 	Lab6: print the SpaceId and the page table of this address space.
//	Lab7: with the valid, use and dirty bits; -1 = not in a frame.
//----------------------------------------------------------------------

void
AddrSpace::Print()
{
    printf("SpaceId: %d, Page table dump: %d pages in total\n",
	   spaceId, numPages);
    printf("===============================\n");
    printf(" Page, Frame, Valid, Use, Dirty\n");
    for (unsigned int i = 0; i < numPages; i++)
	printf("%5d, %5d, %5d, %3d, %5d\n", pageTable[i].virtualPage,
	       pageTable[i].physicalPage, pageTable[i].valid,
	       pageTable[i].use, pageTable[i].dirty);
    printf("===============================\n\n");
}

//----------------------------------------------------------------------
// AddrSpace::InitRegisters
// 	Set the initial values for the user-level register set.
//
// 	We write these directly into the "machine" registers, so
//	that we can immediately jump to user code.  Note that these
//	will be saved/restored into the currentThread->userRegisters
//	when this thread is context switched out.
//----------------------------------------------------------------------

void
AddrSpace::InitRegisters()
{
    int i;

    ASSERT(IsValid());		// Lab6: the program must have been loaded

    for (i = 0; i < NumTotalRegs; i++)
	machine->WriteRegister(i, 0);

    // Initial program counter -- must be location of "Start"
    machine->WriteRegister(PCReg, 0);	

    // Need to also tell MIPS where next instruction is, because
    // of branch delay possibility
    machine->WriteRegister(NextPCReg, 4);

   // Set the stack register to the end of the address space, where we
   // allocated the stack; but subtract off a bit, to make sure we don't
   // accidentally reference off the end!
    machine->WriteRegister(StackReg, numPages * PageSize - 16);
    DEBUG('a', "Initializing stack register to %d\n", numPages * PageSize - 16);
}

//----------------------------------------------------------------------
// AddrSpace::SaveState
// 	On a context switch, save any machine state, specific
//	to this address space, that needs saving.
//
//	For now, nothing!
//----------------------------------------------------------------------

void AddrSpace::SaveState() 
{}

//----------------------------------------------------------------------
// AddrSpace::RestoreState
// 	On a context switch, restore the machine state so that
//	this address space can run.
//
//      For now, tell the machine where to find the page table.
//----------------------------------------------------------------------

void AddrSpace::RestoreState() 
{
    machine->pageTable = pageTable;
    machine->pageTableSize = numPages;
}
