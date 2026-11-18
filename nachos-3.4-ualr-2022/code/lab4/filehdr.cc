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
}

//----------------------------------------------------------------------
// FileHeader::Allocate
// 	Initialize a fresh file header for a newly created file.
//	Allocate data blocks for the file out of the map of free disk blocks.
//	Return FALSE if there are not enough free blocks to accomodate
//	the new file.
//
//	"freeMap" is the bit map of free disk sectors
//	"fileSize" is the bit map of free disk sectors
//----------------------------------------------------------------------

bool
FileHeader::Allocate(BitMap *freeMap, int fileSize)
{
    int numSectors = divRoundUp(fileSize, SectorSize);

    if (fileSize < 0 || numSectors > NumDirect)
	return FALSE;		// Lab4: too big for one file header
    if (freeMap->NumClear() < numSectors)
	return FALSE;		// not enough space

    numBytes = fileSize;
    lastModTime = (int) time(NULL);	// Lab4: a new file is "modified" now
    for (int i = 0; i < numSectors; i++)
	dataSectors[i] = freeMap->Find();
    return TRUE;
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

    if (newSize <= numBytes)
	return TRUE;			// already long enough
    if (newSectors > NumDirect)
	return FALSE;			// bigger than MaxFileSize
    if (newSectors > oldSectors) {	// more data sectors are needed
	if (freeMap == NULL || freeMap->NumClear() < newSectors - oldSectors)
	    return FALSE;		// disk full
	for (int i = oldSectors; i < newSectors; i++)
	    dataSectors[i] = freeMap->Find();
    }
    numBytes = newSize;
    return TRUE;
}

//----------------------------------------------------------------------
// FileHeader::Deallocate
// 	De-allocate all the space allocated for data blocks for this file.
//
//	"freeMap" is the bit map of free disk sectors
//----------------------------------------------------------------------

void
FileHeader::Deallocate(BitMap *freeMap)
{
    int numSectors = NumDataSectors();

    for (int i = 0; i < numSectors; i++) {
	ASSERT(freeMap->Test((int) dataSectors[i]));  // ought to be marked!
	freeMap->Clear((int) dataSectors[i]);
    }
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
    return(dataSectors[offset / SectorSize]);
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
    for (i = 0; i < numSectors; i++)
	printf("%d ", dataSectors[i]);
    printf("\nFile contents:\n");
    for (i = k = 0; i < numSectors; i++) {
	synchDisk->ReadSector(dataSectors[i], data);
        for (j = 0; (j < SectorSize) && (k < numBytes); j++, k++) {
	    if ('\040' <= data[j] && data[j] <= '\176')   // isprint(data[j])
		printf("%c", data[j]);
            else
		printf("\\%x", (unsigned char)data[j]);
	}
        printf("\n"); 
    }
    delete [] data;
}
