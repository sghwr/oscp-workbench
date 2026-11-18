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
//	First, set up the translation from program memory to physical
//	memory.  For now, this is really simple (1:1), since we are
//	only uniprogramming, and we have a single unsegmented page table
//
//	Lab6: multiprogramming. Each virtual page gets a free page frame
//	from physPageMap (no longer virtual page i -> frame i), so several
//	programs can be in memory at the same time without overwriting
//	each other. Each address space also gets a unique SpaceId.
//	If there are not enough free frames (or SpaceIds), nothing is
//	allocated and IsValid() returns FALSE.
//
//	"executable" is the file containing the object code to load into memory
//----------------------------------------------------------------------

AddrSpace::AddrSpace(OpenFile *executable)
{
    NoffHeader noffH;
    unsigned int i, size;

    pageTable = NULL;
    numPages = 0;
    spaceId = -1;

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

    // Lab6: check that there are enough free page frames, instead of
    // only checking that the program fits into the whole memory
    if (numPages > (unsigned int) physPageMap->NumClear()
	    || spaceIdMap->NumClear() == 0) {
	printf("Not enough memory to load the program: %d pages needed, "
	       "%d free\n", numPages, physPageMap->NumClear());
	numPages = 0;
	return;				// IsValid() is FALSE
    }
    spaceId = spaceIdMap->Find();

    DEBUG('a', "Initializing address space %d, num pages %d, size %d\n",
					spaceId, numPages, size);
// first, set up the translation
    pageTable = new TranslationEntry[numPages];
    for (i = 0; i < numPages; i++) {
	pageTable[i].virtualPage = i;
	pageTable[i].physicalPage = physPageMap->Find();  // Lab6: any free
							  // frame
	pageTable[i].valid = TRUE;
	pageTable[i].use = FALSE;
	pageTable[i].dirty = FALSE;
	pageTable[i].readOnly = FALSE;  // if the code segment was entirely on
					// a separate page, we could set its
					// pages to be read-only

	// zero out the frame, to zero the unitialized data segment and
	// the stack segment (Lab6: frame by frame, not the whole memory)
	bzero(&(machine->mainMemory[pageTable[i].physicalPage * PageSize]),
	      PageSize);
    }

// then, copy in the code and data segments into memory
    if (noffH.code.size > 0) {
        DEBUG('a', "Initializing code segment, at 0x%x, size %d\n",
			noffH.code.virtualAddr, noffH.code.size);
	LoadSegment(executable, noffH.code.virtualAddr, noffH.code.size,
		    noffH.code.inFileAddr);
    }
    if (noffH.initData.size > 0) {
        DEBUG('a', "Initializing data segment, at 0x%x, size %d\n",
			noffH.initData.virtualAddr, noffH.initData.size);
	LoadSegment(executable, noffH.initData.virtualAddr,
		    noffH.initData.size, noffH.initData.inFileAddr);
    }

    Print();				// Lab6: show the page table
}

//----------------------------------------------------------------------
// AddrSpace::LoadSegment
//	Lab6: copy "size" bytes at offset "inFileAddr" of the executable
//	to virtual address "virtAddr" of this address space.
//
//	Virtual pages are no longer at the same place in physical memory,
//	and consecutive virtual pages are not necessarily in consecutive
//	frames, so the segment is copied piece by piece, one page at a
//	time, each piece to the frame given by the page table. (A segment
//	need not start or end at a page boundary.)
//----------------------------------------------------------------------

void
AddrSpace::LoadSegment(OpenFile *executable, int virtAddr, int size,
		       int inFileAddr)
{
    int vpn, offset, chunk, physAddr;

    while (size > 0) {
	vpn = virtAddr / PageSize;
	offset = virtAddr % PageSize;
	chunk = PageSize - offset;		// up to the end of the page
	if (chunk > size)
	    chunk = size;
	ASSERT(vpn < (int) numPages);
	physAddr = pageTable[vpn].physicalPage * PageSize + offset;
	executable->ReadAt(&(machine->mainMemory[physAddr]), chunk, inFileAddr);
	virtAddr += chunk;
	inFileAddr += chunk;
	size -= chunk;
    }
}

//----------------------------------------------------------------------
// AddrSpace::~AddrSpace
// 	Dealloate an address space.
//	Lab6: give its page frames and its SpaceId back.
//----------------------------------------------------------------------

AddrSpace::~AddrSpace()
{
    for (unsigned int i = 0; i < numPages; i++)
	physPageMap->Clear(pageTable[i].physicalPage);
    if (spaceId >= 0)
	spaceIdMap->Clear(spaceId);
    delete [] pageTable;
}

//----------------------------------------------------------------------
// AddrSpace::Print
// 	Lab6: print the SpaceId and the page table of this address space,
//	i.e. which page frame each virtual page is mapped to.
//----------------------------------------------------------------------

void
AddrSpace::Print()
{
    printf("spaceID: %d\n", spaceId);
    printf("Page table dump: %d pages in total\n", numPages);
    printf("============================================\n");
    printf("VirtPage, PhysPage\n");
    for (unsigned int i = 0; i < numPages; i++)
	printf("%8d, %8d\n", pageTable[i].virtualPage,
	       pageTable[i].physicalPage);
    printf("============================================\n\n");
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
