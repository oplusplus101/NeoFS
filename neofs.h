
#ifndef __NEOFS_H
#define __NEOFS_H

#include "types.h"

typedef struct
{
    BYTE arrJump[3];
    QWORD qwMagic;
    WORD wBytesPerSector;
    BYTE bSectorsPerCluster;
    QWORD qwNumberOfSectors;
    DWORD dwBootFileSystemSize;
    QWORD qwRootCluster; 
    BYTE arrCode[476];
    WORD wBootMagic;
} __attribute__((packed)) sBootSector;

typedef struct _tagBootFileDescriptor
{
    BYTE  bType; // 0 is empty, 1 is a file, 2 is a folder.
    WCHAR wsFileName[53];
    BYTE  bFileNameLength; // Filename length for faster enumeration
    QWORD qwFileSize;
    DWORD dwFirstCluster;
    QWORD qwWriteTimeStamp;
} __attribute__((packed)) sBootFileDescriptor;

#endif // __NEOFS_H
