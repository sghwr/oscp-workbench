// openfile.cc 
//	Routines to manage an open Nachos file.  As in UNIX, a
//	file must be open before we can read or write to it.
//	Once we're all done, we can close it (in Nachos, by deleting
//	the OpenFile data structure).
//
//	Also as in UNIX, for convenience, we keep the file header in
//	memory while the file is open.
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

#include "copyright.h"
#include "filehdr.h"
#include "openfile.h"
#include "system.h"
#include <time.h>

// Lab4: sectors of the free-map and directory file headers. They must be
// the same as in filesys.cc (which keeps them private to that file).
#define FreeMapSector 		0
#define DirectorySector 	1

//----------------------------------------------------------------------
// OpenFile::OpenFile
// 	Open a Nachos file for reading and writing.  Bring the file header
//	into memory while the file is open.
//
//	"sector" -- the location on disk of the file header for this file
//----------------------------------------------------------------------

OpenFile::OpenFile(int sector)
{
    hdr = new FileHeader;
    hdr->FetchFrom(sector);
    seekPosition = 0;
    hdrSector = sector;		// Lab4
    hdrDirty = FALSE;		// Lab4
}

//----------------------------------------------------------------------
// OpenFile::~OpenFile
// 	Close a Nachos file, de-allocating any in-memory data structures.
//
//	Lab4: the file header is kept in memory while the file is open,
//	and WriteAt() only changes this in-memory copy (new length, new
//	data sectors, new modification time). So it is written back to
//	disk here, when the file is closed -- but only if it has really
//	been changed, so that just reading a file costs no disk write and
//	does not touch its modification time.
//----------------------------------------------------------------------

OpenFile::~OpenFile()
{
    if (hdrDirty)
	hdr->WriteBack(hdrSector);
    delete hdr;
}

//----------------------------------------------------------------------
// OpenFile::Seek
// 	Change the current location within the open file -- the point at
//	which the next Read or Write will start from.
//
//	"position" -- the location within the file for the next Read/Write
//----------------------------------------------------------------------

void
OpenFile::Seek(int position)
{
    seekPosition = position;
}	

//----------------------------------------------------------------------
// OpenFile::Read/Write
// 	Read/write a portion of a file, starting from seekPosition.
//	Return the number of bytes actually written or read, and as a
//	side effect, increment the current position within the file.
//
//	Implemented using the more primitive ReadAt/WriteAt.
//
//	"into" -- the buffer to contain the data to be read from disk 
//	"from" -- the buffer containing the data to be written to disk 
//	"numBytes" -- the number of bytes to transfer
//----------------------------------------------------------------------

int
OpenFile::Read(char *into, int numBytes)
{
   int result = ReadAt(into, numBytes, seekPosition);
   seekPosition += result;
   return result;
}

int
OpenFile::Write(char *into, int numBytes)
{
   int result = WriteAt(into, numBytes, seekPosition);
   seekPosition += result;
   return result;
}

//----------------------------------------------------------------------
// OpenFile::ReadAt/WriteAt
// 	Read/write a portion of a file, starting at "position".
//	Return the number of bytes actually written or read, but has
//	no side effects (except that Write modifies the file, of course).
//
//	There is no guarantee the request starts or ends on an even disk sector
//	boundary; however the disk only knows how to read/write a whole disk
//	sector at a time.  Thus:
//
//	For ReadAt:
//	   We read in all of the full or partial sectors that are part of the
//	   request, but we only copy the part we are interested in.
//	For WriteAt:
//	   We must first read in any sectors that will be partially written,
//	   so that we don't overwrite the unmodified portion.  We then copy
//	   in the data that will be modified, and write back all the full
//	   or partial sectors that are part of the request.
//
//	"into" -- the buffer to contain the data to be read from disk 
//	"from" -- the buffer containing the data to be written to disk 
//	"numBytes" -- the number of bytes to transfer
//	"position" -- the offset within the file of the first byte to be
//			read/written
//----------------------------------------------------------------------

int
OpenFile::ReadAt(char *into, int numBytes, int position)
{
    int fileLength = hdr->FileLength();
    int i, firstSector, lastSector, numSectors;
    char *buf;

    if ((numBytes <= 0) || (position >= fileLength))
    	return 0; 				// check request
    if ((position + numBytes) > fileLength)		
	numBytes = fileLength - position;
    DEBUG('f', "Reading %d bytes at %d, from file of length %d.\n", 	
			numBytes, position, fileLength);

    firstSector = divRoundDown(position, SectorSize);
    lastSector = divRoundDown(position + numBytes - 1, SectorSize);
    numSectors = 1 + lastSector - firstSector;

    // read in all the full and partial sectors that we need
    buf = new char[numSectors * SectorSize];
    for (i = firstSector; i <= lastSector; i++)	
        synchDisk->ReadSector(hdr->ByteToSector(i * SectorSize), 
					&buf[(i - firstSector) * SectorSize]);

    // copy the part we want
    bcopy(&buf[position - (firstSector * SectorSize)], into, numBytes);
    delete [] buf;
    return numBytes;
}

//----------------------------------------------------------------------
// OpenFile::ExtendTo
//	Lab4: try to make the file newSize bytes long, allocating new data
//	sectors as needed (see FileHeader::Extend). If the file cannot
//	grow that much (it would exceed MaxFileSize, or the disk is full),
//	grow it as much as possible. Return the new length of the file.
//
//	The file header is only changed in memory (written back on close).
//	The free map is read from and written back to the free-map file,
//	but only if new sectors are really needed: while the new data still
//	fits into the last, partly used sector, only the length changes.
//----------------------------------------------------------------------

int
OpenFile::ExtendTo(int newSize)
{
    OpenFile *freeMapFile;
    BitMap *freeMap;
    int oldSize = hdr->FileLength();
    int maxSize;

    if (newSize <= oldSize)
	return oldSize;
    if (newSize <= hdr->NumDataSectors() * SectorSize) {
	// fits into the sectors the file already has
	ASSERT(hdr->Extend(NULL, newSize));
	hdrDirty = TRUE;
	return newSize;
    }

    freeMapFile = new OpenFile(FreeMapSector);
    freeMap = new BitMap(NumSectors);
    freeMap->FetchFrom(freeMapFile);

    // the longest the file can become: limited by the size of the file
    // header, and by the sectors it has plus the free ones on the disk
    // (Lab5: computed by the header, which knows about the index block)
    maxSize = hdr->MaxLength(freeMap);
    if (newSize > maxSize) {
	DEBUG('f', "Cannot extend file to %d bytes, only to %d.\n",
	      newSize, maxSize);
	newSize = maxSize;
    }

    if (newSize > oldSize) {
	ASSERT(hdr->Extend(freeMap, newSize));
	freeMap->WriteBack(freeMapFile);	// new sectors are now in use
	hdrDirty = TRUE;
	DEBUG('f', "Extended file from %d to %d bytes.\n", oldSize, newSize);
    }
    delete freeMap;
    delete freeMapFile;
    return hdr->FileLength();
}

int
OpenFile::WriteAt(char *from, int numBytes, int position)
{
    int fileLength = hdr->FileLength();
    int i, firstSector, lastSector, numSectors;
    bool firstAligned, lastAligned;
    char *buf;

    // Lab4: writing may start at the end of the file (position ==
    // fileLength, i.e. append) and go beyond it; the file then grows.
    // Starting beyond the end would leave a "hole" of undefined data,
    // which is not supported.
    if ((numBytes <= 0) || (position > fileLength))
	return 0;				// check request
    if ((position + numBytes) > fileLength)	// Lab4: grow the file,
	fileLength = ExtendTo(position + numBytes); // as much as possible
    if ((position + numBytes) > fileLength)
	numBytes = fileLength - position;	// could not grow enough
    if (numBytes <= 0)
	return 0;
    DEBUG('f', "Writing %d bytes at %d, from file of length %d.\n",
			numBytes, position, fileLength);

    firstSector = divRoundDown(position, SectorSize);
    lastSector = divRoundDown(position + numBytes - 1, SectorSize);
    numSectors = 1 + lastSector - firstSector;

    buf = new char[numSectors * SectorSize];

    firstAligned = (bool)(position == (firstSector * SectorSize));
    lastAligned = (bool)((position + numBytes) == ((lastSector + 1) * SectorSize));

// read in first and last sector, if they are to be partially modified
    if (!firstAligned)
        ReadAt(buf, SectorSize, firstSector * SectorSize);	
    if (!lastAligned && ((firstSector != lastSector) || firstAligned))
        ReadAt(&buf[(lastSector - firstSector) * SectorSize], 
				SectorSize, lastSector * SectorSize);	

// copy in the bytes we want to change 
    bcopy(from, &buf[position - (firstSector * SectorSize)], numBytes);

// write modified sectors back
    for (i = firstSector; i <= lastSector; i++)	
        synchDisk->WriteSector(hdr->ByteToSector(i * SectorSize),
					&buf[(i - firstSector) * SectorSize]);
    delete [] buf;

    // Lab4: the file has been modified now. Only normal files keep a
    // modification time; the bitmap and directory files are written
    // all the time but have a fixed size, so their headers never need
    // to be written back.
    if (hdrSector != FreeMapSector && hdrSector != DirectorySector) {
	hdr->SetModTime((int) time(NULL));
	hdrDirty = TRUE;
    }
    return numBytes;
}

//----------------------------------------------------------------------
// OpenFile::Length
// 	Return the number of bytes in the file.
//----------------------------------------------------------------------

int
OpenFile::Length()
{
    return hdr->FileLength();
}

//----------------------------------------------------------------------
// OpenFile::GetModTime / OpenFile::SetModTime
// 	Lab4: get/set the last modification time of the file, in seconds
//	since 1970-01-01 00:00:00 UTC. Setting it marks the header dirty,
//	so it is written back to disk when the file is closed.
//----------------------------------------------------------------------

int
OpenFile::GetModTime()
{
    return hdr->GetModTime();
}

void
OpenFile::SetModTime(int t)
{
    hdr->SetModTime(t);
    hdrDirty = TRUE;
}
