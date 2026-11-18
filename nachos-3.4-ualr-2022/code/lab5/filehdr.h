// filehdr.h 
//	Data structures for managing a disk file header.  
//
//	A file header describes where on disk to find the data in a file,
//	along with other information about the file (for instance, its
//	length, owner, etc.)
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

#include "copyright.h"

#ifndef FILEHDR_H
#define FILEHDR_H

#include "disk.h"
#include "bitmap.h"

// Lab5: two-level index. Of the 30 sector numbers in the header, the
// first 29 are direct indexes; the last one is the sector number of an
// "index block" (second-level index), a whole sector holding 32 more
// data sector numbers. So:
//    NumDirect   = (128 - 3 * 4) / 4 = 29   (numBytes, lastModTime and
//                                            the index block pointer)
//    NumIndirect = 128 / 4           = 32
//    MaxFileSize = (29 + 32) * 128   = 7808 bytes
#define NumDirect 	(int)((SectorSize - 3 * sizeof(int)) / sizeof(int))
#define NumIndirect	(int)(SectorSize / sizeof(int))
#define MaxFileSize 	((NumDirect + NumIndirect) * SectorSize)

// The following class defines the Nachos "file header" (in UNIX terms,  
// the "i-node"), describing where on disk to find all of the data in the file.
// The file header is organized as a simple table of pointers to
// data blocks. 
//
// The file header data structure can be stored in memory or on disk.
// When it is on disk, it is stored in a single sector -- this means
// that we assume the size of this data structure to be the same
// as one disk sector.  Without indirect addressing, this
// limits the maximum file length to just under 4K bytes.
//
// There is no constructor; rather the file header can be initialized
// by allocating blocks for the file (if it is a new file), or by
// reading it from disk.

// Lab4 changes:
//   1. A file can grow: Extend() allocates more data sectors when data is
//      written beyond the end of the file.
//   2. The header records the time of the last modification, in seconds
//      since 00:00:00 UTC, January 1, 1970 (the value of time(2)). It is
//      stored where "numSectors" used to be: numSectors is redundant,
//      it can always be computed as divRoundUp(numBytes, SectorSize).
//      So the on-disk layout is still exactly one sector (32 ints):
//          [numBytes][lastModTime][dataSectors[0] .. dataSectors[29]]
//
// Lab5 changes: two-level index (see NumDirect above). On-disk layout:
//     [numBytes][lastModTime][dataSectors[0..28]][indexSector]
//                                                     |
//               index block (one sector): [data sector 29 .. 60] <--+
// The index block exists only when the file needs more than 29 data
// sectors; otherwise indexSector is -1. It is allocated when the file
// grows beyond 29 sectors, and freed together with the file.

class FileHeader {
  public:
    FileHeader();				// Lab4: start with an empty,
						//  zero-filled header

    bool Allocate(BitMap *bitMap, int fileSize);// Initialize a file header,
						//  including allocating space
						//  on disk for the file data
    bool Extend(BitMap *bitMap, int newSize);	// Lab4: grow the file to
						//  newSize bytes, allocating
						//  more data sectors if needed
    int MaxLength(BitMap *bitMap);		// Lab5: the largest length the
						//  file can be extended to
    void Deallocate(BitMap *bitMap);  		// De-allocate this file's
						//  data blocks

    void FetchFrom(int sectorNumber); 	// Initialize file header from disk
    void WriteBack(int sectorNumber); 	// Write modifications to file header
					//  back to disk

    int ByteToSector(int offset);	// Convert a byte offset into the file
					// to the disk sector containing
					// the byte

    int FileLength();			// Return the length of the file
					// in bytes
    int NumDataSectors();		// Lab4: number of data sectors,
					// computed from numBytes

    int GetModTime() { return lastModTime; }	// Lab4: last modification
    void SetModTime(int t) { lastModTime = t; }	//  time, seconds since 1970

    void Print(bool showTime = FALSE);	// Print the contents of the file.
					// Lab4: also print the modification
					// time if showTime (only meaningful
					// for normal files, not for the
					// bitmap and directory files)

  private:
    int numBytes;			// Number of bytes in the file
    int lastModTime;			// Lab4: time of last modification,
					// seconds since 1970-01-01 00:00:00
					// UTC; replaces "numSectors". An int
					// (not time_t) so the on-disk layout
					// does not depend on the host.
    int dataSectors[NumDirect];		// Disk sector numbers for each data
					// block in the file (Lab5: the first
					// NumDirect ones only)
    int indexSector;			// Lab5: sector of the index block
					// (second-level index) holding the
					// numbers of data sectors NumDirect ..
					// NumDirect + NumIndirect - 1; -1 if
					// the file is not that big

    int DataSector(int i);		// Lab5: sector number of the i-th
					// data sector of the file
    void ReadIndex(int *index);		// Lab5: read / write the index
    void WriteIndex(int *index);	// block (NumIndirect ints)
};

#endif // FILEHDR_H
