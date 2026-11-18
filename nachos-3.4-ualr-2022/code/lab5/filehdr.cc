// filehdr.cc 
//	Routines for managing the disk file header (in UNIX, this
//	would be called the i-node).
//
//	The file header is used to locate where on disk the 
//	file's data is stored.  We implement this as a fixed size
//	table of pointers -- each entry in the table points to the 
//	disk sector containing that portion of the file data
//	(in other words, there are no indirect or doubly indirect 
//	blocks). The table size is chosen so that the file header
//	will be just big enough to fit in one disk sector, 
//
//      Unlike in a real system, we do not keep track of file permissions, 
//	ownership, last modification date, etc., in the file header. 
//
//	A file header can be initialized in two ways:
//	   for a new file, by modifying the in-memory data structure
//	     to point to the newly allocated data blocks
//	   for a file already on disk, by reading the file header from disk
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

#include "copyright.h"

#include "system.h"
#include "filehdr.h"
#include <string.h>
#include <time.h>

//----------------------------------------------------------------------
// FileHeader::FileHeader
//	Lab4: initialize an empty header (length 0, no data sectors), so
//	that a header that is printed or written back before being
//	filled in does not contain garbage.
//----------------------------------------------------------------------

FileHeader::FileHeader()
{
    numBytes = 0;
    lastModTime = 0;
    memset(dataSectors, 0, sizeof(dataSectors));
    indexSector = -1;			// Lab5: no index block yet
}

//----------------------------------------------------------------------
// FileHeader::Allocate
// 	Initialize a fresh file header for a newly created file.
//	Allocate data blocks for the file out of the map of free disk blocks.
//	Return FALSE if there are not enough free blocks to accomodate
//	the new file.
//
//	Lab5: a new file is an empty file extended to fileSize bytes, so
//	the work (including the index block, if needed) is done by Extend.
//
//	"freeMap" is the bit map of free disk sectors
//	"fileSize" is the bit map of free disk sectors
//----------------------------------------------------------------------

bool
FileHeader::Allocate(BitMap *freeMap, int fileSize)
{
    if (fileSize < 0)
	return FALSE;

    numBytes = 0;
    indexSector = -1;
    lastModTime = (int) time(NULL);	// Lab4: a new file is "modified" now
    return Extend(freeMap, fileSize);
}

//----------------------------------------------------------------------
// FileHeader::Extend
//	Lab4: make the file newSize bytes long. If the new length needs
//	more data sectors than the file has now, allocate them from the
//	free map. The contents of the new part are undefined until they
//	are written. A file never shrinks here: if newSize is not larger
//	than the current length, nothing changes.
//
//	Return FALSE (and change nothing) if the file would be bigger than
//	MaxFileSize or there are not enough free sectors.
//
//	Lab5: data sectors 0 .. NumDirect-1 go into dataSectors[]; when the
//	file grows beyond NumDirect data sectors, an index block is
//	allocated first (if there is none yet), then the further data
//	sectors are recorded in it. The index block is written to disk
//	here, immediately; the header itself is written back by the caller.
//
//	"freeMap" is the bit map of free disk sectors; the caller must
//		write it back to disk if it has been changed. It may be
//		NULL if no new sector is needed, i.e. newSize still fits
//		into the last, partly used data sector.
//	"newSize" is the new length of the file in bytes
//----------------------------------------------------------------------

bool
FileHeader::Extend(BitMap *freeMap, int newSize)
{
    int oldSectors = NumDataSectors();
    int newSectors = divRoundUp(newSize, SectorSize);
    int needed;				// free sectors needed
    int index[NumIndirect];		// contents of the index block
    int i;

    if (newSize <= numBytes)
	return TRUE;			// already long enough
    if (newSectors > NumDirect + NumIndirect)
	return FALSE;			// bigger than MaxFileSize

    needed = newSectors - oldSectors;
    if (newSectors > NumDirect && indexSector == -1)
	needed++;			// Lab5: the index block itself
    if (needed > 0) {			// more sectors are needed
	if (freeMap == NULL || freeMap->NumClear() < needed)
	    return FALSE;		// disk full

	// first-level (direct) part
	for (i = oldSectors; i < newSectors && i < NumDirect; i++)
	    dataSectors[i] = freeMap->Find();

	// Lab5: second-level part, through the index block
	if (newSectors > NumDirect) {
	    if (indexSector == -1) {	// first time beyond NumDirect
		indexSector = freeMap->Find();
		for (i = 0; i < NumIndirect; i++)
		    index[i] = -1;
	    } else
		ReadIndex(index);
	    for (i = (oldSectors > NumDirect ? oldSectors : NumDirect);
		 i < newSectors; i++)
		index[i - NumDirect] = freeMap->Find();
	    WriteIndex(index);
	}
    }
    numBytes = newSize;
    return TRUE;
}

//----------------------------------------------------------------------
// FileHeader::MaxLength
//	Lab5: return the largest length (in bytes) this file can be
//	extended to: limited by MaxFileSize, and by the sectors the file
//	already has plus the free sectors on the disk -- one of which is
//	taken by the index block if the file grows beyond NumDirect
//	sectors for the first time.
//----------------------------------------------------------------------

int
FileHeader::MaxLength(BitMap *freeMap)
{
    int sectors = NumDataSectors() + freeMap->NumClear();

    if (sectors > NumDirect && indexSector == -1)
	sectors--;			// one sector for the index block
    if (sectors > NumDirect + NumIndirect)
	sectors = NumDirect + NumIndirect;
    return sectors * SectorSize;
}

//----------------------------------------------------------------------
// FileHeader::Deallocate
// 	De-allocate all the space allocated for data blocks for this file.
//	Lab5: including the data sectors listed in the index block, and
//	the index block itself.
//
//	"freeMap" is the bit map of free disk sectors
//----------------------------------------------------------------------

void
FileHeader::Deallocate(BitMap *freeMap)
{
    int numSectors = NumDataSectors();
    int index[NumIndirect];
    int i, sector;

    if (indexSector != -1)
	ReadIndex(index);
    for (i = 0; i < numSectors; i++) {
	sector = (i < NumDirect) ? dataSectors[i] : index[i - NumDirect];
	ASSERT(freeMap->Test(sector));  // ought to be marked!
	freeMap->Clear(sector);
    }
    if (indexSector != -1) {		// Lab5: free the index block too
	ASSERT(freeMap->Test(indexSector));
	freeMap->Clear(indexSector);
	indexSector = -1;
    }
}

//----------------------------------------------------------------------
// FileHeader::ReadIndex / FileHeader::WriteIndex
//	Lab5: read / write the index block (second-level index) of the
//	file, an array of NumIndirect data sector numbers.
//----------------------------------------------------------------------

void
FileHeader::ReadIndex(int *index)
{
    ASSERT(indexSector != -1);
    synchDisk->ReadSector(indexSector, (char *) index);
}

void
FileHeader::WriteIndex(int *index)
{
    ASSERT(indexSector != -1);
    synchDisk->WriteSector(indexSector, (char *) index);
}

//----------------------------------------------------------------------
// FileHeader::DataSector
//	Lab5: return the disk sector number of the i-th data sector of
//	the file. Sectors beyond the first NumDirect ones are found in the
//	index block, which costs one more disk read.
//----------------------------------------------------------------------

int
FileHeader::DataSector(int i)
{
    int index[NumIndirect];

    ASSERT(i >= 0 && i < NumDataSectors());
    if (i < NumDirect)
	return dataSectors[i];
    ReadIndex(index);
    return index[i - NumDirect];
}

//----------------------------------------------------------------------
// FileHeader::FetchFrom
// 	Fetch contents of file header from disk. 
//
//	"sector" is the disk sector containing the file header
//----------------------------------------------------------------------

void
FileHeader::FetchFrom(int sector)
{
    synchDisk->ReadSector(sector, (char *)this);
}

//----------------------------------------------------------------------
// FileHeader::WriteBack
// 	Write the modified contents of the file header back to disk. 
//
//	"sector" is the disk sector to contain the file header
//----------------------------------------------------------------------

void
FileHeader::WriteBack(int sector)
{
    synchDisk->WriteSector(sector, (char *)this); 
}

//----------------------------------------------------------------------
// FileHeader::ByteToSector
// 	Return which disk sector is storing a particular byte within the file.
//      This is essentially a translation from a virtual address (the
//	offset in the file) to a physical address (the sector where the
//	data at the offset is stored).
//
//	"offset" is the location within the file of the byte in question
//----------------------------------------------------------------------

int
FileHeader::ByteToSector(int offset)
{
    return DataSector(offset / SectorSize);	// Lab5: may use the index block
}

//----------------------------------------------------------------------
// FileHeader::FileLength
// 	Return the number of bytes in the file.
//----------------------------------------------------------------------

int
FileHeader::FileLength()
{
    return numBytes;
}

//----------------------------------------------------------------------
// FileHeader::NumDataSectors
// 	Lab4: return the number of data sectors of the file. It is no
//	longer stored in the header, because it follows from the length.
//----------------------------------------------------------------------

int
FileHeader::NumDataSectors()
{
    return divRoundUp(numBytes, SectorSize);
}

//----------------------------------------------------------------------
// FileHeader::Print
// 	Print the contents of the file header, and the contents of all
//	the data blocks pointed to by the file header.
//
//	Lab4: "showTime" -- also print the last modification time. Only
//	normal files keep a meaningful time, so Directory::Print passes
//	TRUE, while FileSystem::Print uses the default FALSE for the
//	headers of the bitmap and directory files.
//----------------------------------------------------------------------

void
FileHeader::Print(bool showTime)
{
    int i, j, k;
    int numSectors = NumDataSectors();
    char *data = new char[SectorSize];
    int index[NumIndirect];		// Lab5: the index block, read once
    int *sectors = new int[numSectors];	// Lab5: all data sector numbers

    for (i = 0; i < numSectors && i < NumDirect; i++)
	sectors[i] = dataSectors[i];
    if (numSectors > NumDirect) {
	ReadIndex(index);
	for (i = NumDirect; i < numSectors; i++)
	    sectors[i] = index[i - NumDirect];
    }

    if (showTime) {
	time_t t = (time_t) lastModTime;
	char timeStr[32];
	// e.g. "Thu Aug 12 00:38:17 2021", same as ctime(3) but without
	// the trailing newline; local time zone, like "ls -l"
	strftime(timeStr, sizeof(timeStr), "%a %b %e %H:%M:%S %Y",
		 localtime(&t));
	printf("FileHeader contents.  File size: %d.  "
	       "File modification time: %s.  File blocks:\n",
	       numBytes, timeStr);
    } else
	printf("FileHeader contents.  File size: %d.  File blocks:\n",
	       numBytes);
    for (i = 0; i < numSectors; i++) {
	if (i == NumDirect)		// Lab5: the rest is in the index block
	    printf(" Index2: ");
	printf("%d ", sectors[i]);
    }
    printf("\nFile contents:\n");
    for (i = k = 0; i < numSectors; i++) {
	synchDisk->ReadSector(sectors[i], data);
        for (j = 0; (j < SectorSize) && (k < numBytes); j++, k++) {
	    if ('\040' <= data[j] && data[j] <= '\176')   // isprint(data[j])
		printf("%c", data[j]);
            else
		printf("\\%x", (unsigned char)data[j]);
	}
        printf("\n");
    }
    delete [] data;
    delete [] sectors;
}
