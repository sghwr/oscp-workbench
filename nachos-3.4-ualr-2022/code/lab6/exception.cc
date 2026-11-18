// exception.cc 
//	Entry point into the Nachos kernel from user programs.
//	There are two kinds of things that can cause control to
//	transfer back to here from user code:
//
//	syscall -- The user code explicitly requests to call a procedure
//	in the Nachos kernel.  Right now, the only function we support is
//	"Halt".
//
//	exceptions -- The user code does something that the CPU can't handle.
//	For instance, accessing memory that doesn't exist, arithmetic errors,
//	etc.  
//
//	Interrupts (which can also cause control to transfer from user
//	code into the Nachos kernel) are handled elsewhere.
//
// For now, this only handles the Halt() system call.
// Everything else core dumps.
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

#include "copyright.h"
#include "system.h"
#include "syscall.h"
#include "addrspace.h"

// Lab6: system calls Halt, Exit, Exec and PrintInt are supported.

#define MaxFileNameLen	128	// Lab6: longest file name passed to Exec

//----------------------------------------------------------------------
// AdvancePC
//	Lab6: after a system call, continue with the instruction after
//	the "syscall" instruction; otherwise the user program would make
//	the same system call again and again. MIPS has a branch delay
//	slot, so the simulator keeps PrevPC, PC and NextPC.
//----------------------------------------------------------------------

static void
AdvancePC()
{
    machine->WriteRegister(PrevPCReg, machine->ReadRegister(PCReg));
    machine->WriteRegister(PCReg, machine->ReadRegister(NextPCReg));
    machine->WriteRegister(NextPCReg, machine->ReadRegister(NextPCReg) + 4);
}

//----------------------------------------------------------------------
// ReadUserString
//	Lab6: copy the null-terminated string at virtual address "addr"
//	of the current user program into the kernel buffer "buf" (at most
//	"size" bytes, including the '\0'). The kernel cannot just use the
//	user pointer: it is a virtual address of the user program, which
//	must be translated through its page table (machine->ReadMem does
//	that). Return FALSE if the string is too long or not readable.
//----------------------------------------------------------------------

static bool
ReadUserString(int addr, char *buf, int size)
{
    int i, c;

    for (i = 0; i < size; i++) {
	if (!machine->ReadMem(addr + i, 1, &c))
	    return FALSE;
	buf[i] = (char) c;
	if (c == '\0')
	    return TRUE;
    }
    buf[size - 1] = '\0';
    return FALSE;
}

//----------------------------------------------------------------------
// StartUserProgram
//	Lab6: the first thing a thread created by Exec does (it is
//	"forked" to run this function): set up the user registers and the
//	page table of its own address space, then start running the user
//	program. Never returns.
//----------------------------------------------------------------------

static void
StartUserProgram(_int arg)
{
    (void) arg;
    currentThread->space->InitRegisters();	// set the initial registers
    currentThread->space->RestoreState();	// load page table register
    machine->Run();				// jump to the user program
    ASSERT(FALSE);				// machine->Run never returns
}

//----------------------------------------------------------------------
// SysExec
//	Lab6: system call "SpaceId Exec(char *name)". Load the program in
//	the file "name" into a NEW address space and run it in a new
//	thread. Unlike UNIX exec(), the caller keeps running in its own
//	address space, which is not replaced. Return the SpaceId of the
//	new address space, or -1 on failure.
//----------------------------------------------------------------------

static SpaceId
SysExec(int nameAddr)
{
    char *name = new char[MaxFileNameLen];
    OpenFile *executable;
    AddrSpace *space;
    Thread *thread;

    printf("Execute system call of Exec()\n");
    if (!ReadUserString(nameAddr, name, MaxFileNameLen)) {
	printf("Exec: bad file name\n");
	delete [] name;
	return -1;
    }
    printf("Exec(%s):\n", name);

    executable = fileSystem->Open(name);
    if (executable == NULL) {
	printf("Exec: unable to open file %s\n", name);
	delete [] name;
	return -1;
    }
    space = new AddrSpace(executable);	// loads the program, prints it
    delete executable;			// close file
    if (!space->IsValid()) {
	delete space;
	delete [] name;
	return -1;
    }

    // A new thread runs the new program. "name" is kept as the thread
    // name (a Thread only stores the pointer), so it is not deleted.
    thread = new Thread(name);
    thread->space = space;
    thread->Fork(StartUserProgram, 0);
    return space->GetSpaceId();
}

//----------------------------------------------------------------------
// ExceptionHandler
// 	Entry point into the Nachos kernel.  Called when a user program
//	is executing, and either does a syscall, or generates an addressing
//	or arithmetic exception.
//
// 	For system calls, the following is the calling convention:
//
// 	system call code -- r2
//		arg1 -- r4
//		arg2 -- r5
//		arg3 -- r6
//		arg4 -- r7
//
//	The result of the system call, if any, must be put back into r2. 
//
// And don't forget to increment the pc before returning. (Or else you'll
// loop making the same system call forever!
//
//	"which" is the kind of exception.  The list of possible exceptions 
//	are in machine.h.
//----------------------------------------------------------------------

void
ExceptionHandler(ExceptionType which)
{
    int type = machine->ReadRegister(2);

    if (which != SyscallException) {
	printf("Unexpected user mode exception %d %d\n", which, type);
	ASSERT(FALSE);
    }

    switch (type) {
      case SC_Halt:
	DEBUG('a', "Shutdown, initiated by user program.\n");
   	interrupt->Halt();
	break;

      case SC_PrintInt:			// Lab6: void PrintInt(int number)
	printf("%d\n", machine->ReadRegister(4));
	AdvancePC();
	break;

      case SC_Exec: {			// Lab6: SpaceId Exec(char *name)
	SpaceId id = SysExec(machine->ReadRegister(4));
	machine->WriteRegister(2, id);	// return value
	AdvancePC();
	// Let the new program run first. There is only one CPU and, without
	// -rs, no timer to preempt the caller, so otherwise the new thread
	// would only run when the caller blocks or finishes -- with
	// exec.c, which calls Halt() right after Exec(), never at all.
	// The caller's registers (with the new PC and return value) are
	// saved by the context switch and restored when it runs again.
	if (id >= 0)
	    currentThread->Yield();
	break;
      }

      case SC_Exit: {			// Lab6: void Exit(int status)
	AddrSpace *space = currentThread->space;
	DEBUG('a', "Exit(%d) from address space %d\n",
	      machine->ReadRegister(4), space->GetSpaceId());
	currentThread->space = NULL;	// so that the scheduler does not
	delete space;			// save state into the freed space;
					// frees the frames and the SpaceId
	currentThread->Finish();	// never returns
	break;
      }

      default:
	printf("Unexpected user mode exception %d %d\n", which, type);
	ASSERT(FALSE);
    }
}
