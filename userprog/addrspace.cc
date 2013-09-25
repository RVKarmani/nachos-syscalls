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
// totalPagesCount 
// this is a global variable that keeps a count of the number of pages that
// have been used by processes up until now
//----------------------------------------------------------------------
// Initialize the value of the totalPagesCount to zero the very first time
int totalPagesCount = 0;

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
//  memory.
//  this works for a generic mapping
//
//	"executable" is the file containing the object code to load into memory
//----------------------------------------------------------------------

AddrSpace::AddrSpace(OpenFile *executable)
{
    NoffHeader noffH;
    unsigned int i, size;

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

    ASSERT(numPages <= NumPhysPages);		// check we're not trying
    // to run anything too big --
    // at least until we have
    // virtual memory

    // We have to add the totalPagesCount to the physicalPage because of the new
    // mapping function that we have used to allocate Pages
    // first, set up the translation 
    pageTable = new TranslationEntry[numPages];
    for (i = 0; i < numPages; i++) {
        pageTable[i].virtualPage = i;	
        pageTable[i].physicalPage = i + totalPagesCount;
        pageTable[i].valid = TRUE;
        pageTable[i].use = FALSE;
        pageTable[i].dirty = FALSE;
        pageTable[i].readOnly = FALSE;  // if the code segment was entirely on 
        // a separate page, we could set its 
        // pages to be read-only
    }

    // Increment the totalPagesCount
    totalPagesCount += numPages;
    ASSERT(totalPagesCount <= NumPhysPages);  // Ensure that the totalPages Count
                                                // does not go beyond available
                                                // pages
                                                
    // So the earlier size assumed a one to one mapping, so we modify this bit
    // so that it reflects the new mapping
    // The thing is that size stores the value of 
    size = totalPagesCount * PageSize;
    
    DEBUG('a', "Initializing address space, num pages %d, size %d\n", 
            numPages, size);
    DEBUG('a', "totalPagesCount %d\n", totalPagesCount);
   
    // zero out the entire address space, to zero the unitialized data segment 
    // and the stack segment
    bzero(machine->mainMemory, size);

    // We translate the virtualAddr to a physical address
    int physicalAddress;

    // then, copy in the code and data segments into memory
    if (noffH.code.size > 0) {
        // translate noffH.code.virtualAddr to physicalAddress
        Translate(noffH.code.virtualAddr, &physicalAddress, pageTable, numPages);
        DEBUG('a', "Initializing code segment, at 0x%x, size %d\n", 
                physicalAddress, noffH.code.size);
        executable->ReadAt(&(machine->mainMemory[(unsigned)physicalAddress]),
                noffH.code.size, noffH.code.inFileAddr);
    }
    if (noffH.initData.size > 0) {
        // translate noffH.initData.virtualAddr to physicalAddress
        Translate(noffH.initData.virtualAddr, &physicalAddress, pageTable, numPages);
        DEBUG('a', "Initializing data segment, at 0x%x, size %d\n", 
                physicalAddress, noffH.initData.size);
        executable->ReadAt(&(machine->mainMemory[(unsigned)physicalAddress]),
                noffH.initData.size, noffH.initData.inFileAddr);
    }
}

//----------------------------------------------------------------------
// AddrSpace::AddrSpace
// 	Create an address space to run a user program.
//  
//  Setup the translation table
//  Copy the physical memoy of the parent into the new page table
//----------------------------------------------------------------------
AddrSpace::AddrSpace(unsigned int numParentPages, unsigned int parentStartPhysPage)
{ 
    unsigned int i, k, size;
    numPages = numParentPages;        // number of pages is equal to parent
    size = numPages * PageSize;
    k = totalPagesCount * PageSize;

    ASSERT(numPages <= NumPhysPages);		// check we're not trying
    // to run anything too big --
    // at least until we have
    // virtual memory

    DEBUG('a', "Initializing address space\nparent pages %d num pages %d, size %d\n", 
            numParentPages , numPages, size);

    // first, set up the translation 
    pageTable = new TranslationEntry[numPages];
    for (i = 0; i < numPages; i++) {
        pageTable[i].virtualPage = i;	
        pageTable[i].physicalPage = i + totalPagesCount;
        pageTable[i].valid = TRUE;
        pageTable[i].use = FALSE;
        pageTable[i].dirty = FALSE;
        pageTable[i].readOnly = FALSE;  // if the code segment was entirely on 
        // a separate page, we could set its 
        // pages to be read-only
    }

    // Increment the totalPagesCount
    totalPagesCount += numPages;

    DEBUG('a', "totalPagesCount %d\n", totalPagesCount);
    ASSERT(totalPagesCount <= NumPhysPages);  // Ensure that the totalPages Count
                                                // does not go beyond available
                                                // pages

    // Now we have to copy the parent's code into the physical pages
    unsigned int parentPhysEnd = parentStartPhysPage + numPages * PageSize; 
    i = parentStartPhysPage * PageSize;

    DEBUG('a', "Copying memory %d - %d to %d to %d", i, parentPhysEnd, k, k);
    for(; i<parentPhysEnd; ++i, ++k) {
        machine->mainMemory[k] = machine->mainMemory[i];
    }
}
//----------------------------------------------------------------------
// AddrSpace::~AddrSpace
// 	Dealloate an address space.  Nothing for now!
//----------------------------------------------------------------------

AddrSpace::~AddrSpace()
{
   delete pageTable;
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

//----------------------------------------------------------------------
// AddrSpace::getNumPages
// Return the num of virtual pages of the thread
//----------------------------------------------------------------------

unsigned int AddrSpace::getNumPages() 
{
    return numPages;
}

//----------------------------------------------------------------------
// AddrSpace::getStartPhysPage
// Return the num of virtual pages of the thread
//----------------------------------------------------------------------

unsigned int AddrSpace::getStartPhysPage() 
{
    return pageTable[0].physicalPage;
}

//----------------------------------------------------------------------
// AddrSpace::Translate
// This is a function which is used for translating a virtual address to a
// physical address given a pagetable to it, this is a specialization of the
// machin Translate function which used the pagetable of the currently running
// thread
//----------------------------------------------------------------------
ExceptionType
AddrSpace::Translate(int virtAddr, int* physAddr, TranslationEntry *pgTable, unsigned int pgSize)
{
    int i;
    unsigned int vpn, offset;
    TranslationEntry *entry;
    unsigned int pageFrame;
    int size = 4;

    // check for alignment errors
    if (((virtAddr & 0x3))) {
        DEBUG('A', "alignment problem at %d, size %d!\n", virtAddr, size);
        return AddressErrorException;
    }

    // calculate the virtual page number, and offset within the page,
    // from the virtual address
    vpn = (unsigned) virtAddr / PageSize;
    offset = (unsigned) virtAddr % PageSize;

    if (vpn >= pgSize) {
        DEBUG('A', "virtual page # %d too large for page table size %d!\n", 
                virtAddr, pgSize);
        return AddressErrorException;
    } else if (!pageTable[vpn].valid) {
        DEBUG('A', "virtual page # %d too large for page table size %d!\n", 
                virtAddr, pgSize);
        return PageFaultException;
    }
    entry = &pageTable[vpn];

    pageFrame = entry->physicalPage;

    // if the pageFrame is too big, there is something really wrong! 
    // An invalid translation was loaded into the page table or TLB. 
    if (pageFrame >= NumPhysPages) { 
        DEBUG('A', "*** frame %d > %d!\n", pageFrame, NumPhysPages);
        return BusErrorException;
    }
    entry->use = TRUE;		// set the use, dirty bits
    *physAddr = pageFrame * PageSize + offset;
    ASSERT((*physAddr >= 0) && ((*physAddr + size) <= MemorySize));
    DEBUG('A', "phys addr = 0x%x\n", *physAddr);
    return NoException;
}
