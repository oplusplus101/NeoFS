
/// Initialises a filesystem
#define _POSIX_C_SOURCE 2
#include "types.h"
#include "config.h"
#include "utils.h"
#include "neofs.h"
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <string.h>
#include <linux/fs.h>
#include <time.h>

#define ATTR_METAFILE      1
#define ATTR_READONLY      2
#define ATTR_HIDDEN        4
#define ATTR_VIRTUAL       8
#define ATTR_FOLDER        16
#define ATTR_MOUNTPOINT    32
#define ATTR_SYMLINK       64
#define ATTR_ENCRYPTED     128
#define ATTR_COMPRESSED    256

WORD g_wBytesPerSector;
BYTE g_bSectorsPerCluster;
QWORD g_qwBytesPerCluster;
QWORD g_qwBootFileSystemSize;
DWORD g_dwNonDataRegion;

DWORD *g_arrFAT;

const unsigned char g_arrBootCode[] =
{
    0xb4, 0x0e, 0xfc, 0xe8, 0x00, 0x00, 0x5e, 0x83, 
    0xc6, 0x18, 0xac, 0x84, 0xc0, 0x74, 0x04, 0xcd, 
    0x10, 0xeb, 0xf7, 0xb8, 0x00, 0x00, 0xcd, 0x16, 
    0xb0, 0xfe, 0xe6, 0x64, 0xeb, 0xfe, 0x54, 0x68, 
    0x69, 0x73, 0x20, 0x69, 0x73, 0x20, 0x6e, 0x6f, 
    0x74, 0x20, 0x61, 0x20, 0x62, 0x6f, 0x6f, 0x74, 
    0x61, 0x62, 0x6c, 0x65, 0x20, 0x64, 0x72, 0x69, 
    0x76, 0x65, 0x2e, 0x20, 0x50, 0x72, 0x65, 0x73, 
    0x73, 0x20, 0x61, 0x6e, 0x79, 0x20, 0x6b, 0x65, 
    0x79, 0x20, 0x74, 0x6f, 0x20, 0x72, 0x65, 0x73, 
    0x74, 0x61, 0x72, 0x74, 0x2e, 0x00
};

void LoadBootSector(INT iFile)
{
    sBootSector sBS;
    lseek(iFile, 0, SEEK_SET);
    read(iFile, &sBS, sizeof(sBootSector));

    if (sBS.qwMagic != 0x38303430534f454e)
        Error("Invalid magic number %16llX", sBS.qwMagic);
    
    if (sBS.dwBootFileSystemSize == 0)
        Error("Filesystem doesn't have a boot filesystem");

    g_wBytesPerSector = sBS.wBytesPerSector;
    g_bSectorsPerCluster = sBS.bSectorsPerCluster;
    g_qwBytesPerCluster = (QWORD) g_wBytesPerSector * g_bSectorsPerCluster;
    g_qwBootFileSystemSize = sBS.dwBootFileSystemSize;
    g_dwNonDataRegion = 1 + (g_qwBootFileSystemSize * sizeof(DWORD) + g_qwBytesPerCluster - 1) / g_qwBytesPerCluster;
}

void LoadFAT(INT iFile)
{
    g_arrFAT = malloc(g_qwBootFileSystemSize * sizeof(DWORD));
    lseek(iFile, g_qwBytesPerCluster, SEEK_SET);
    read(iFile, g_arrFAT, g_qwBootFileSystemSize * sizeof(DWORD));
}

void StoreFAT(INT iFile)
{
    lseek(iFile, g_qwBytesPerCluster, SEEK_SET);
    write(iFile, g_arrFAT, g_qwBootFileSystemSize * sizeof(DWORD));
}

// Returns 0xFFFFFFFF if an invalid cluster is passed (e.g. the FAT, bootsector or a cluster outside the boot filesystem)
DWORD ClusterToBootFATIndex(DWORD dwCluster)
{
    if (dwCluster < g_dwNonDataRegion)
        return 0xFFFFFFFF;
    return dwCluster - g_dwNonDataRegion;
}

void WriteFAT(DWORD dwCluster, DWORD dwNextCluster)
{
    g_arrFAT[ClusterToBootFATIndex(dwCluster)] = dwNextCluster;
}

DWORD ReadFAT(DWORD dwCluster)
{
    return g_arrFAT[ClusterToBootFATIndex(dwCluster)];
}

BOOL IsClusterReserved(DWORD dwCluster)
{
    return ReadFAT(dwCluster) != 0;
}

DWORD GetCluster()
{
    for (DWORD i = 0; i < g_qwBootFileSystemSize; i++)
        if (!IsClusterReserved(i + g_dwNonDataRegion))
            return i + g_dwNonDataRegion;
    return 0xFFFFFFFF;
}

DWORD AppendCluster(DWORD dwCluster, INT iFile)
{
    DWORD dwNextCluster = GetCluster();
    if (dwNextCluster == 0xFFFFFFFF)
        Error("Not enough space");
    
    WriteFAT(dwCluster, dwNextCluster);
    return dwNextCluster;
}

void WriteClusteredData(DWORD dwClusterStart, PVOID pData, QWORD qwLengthInBytes, INT iFile)
{
    WriteFAT(dwClusterStart, 0xFFFFFFFF);
    for (DWORD dwCluster = dwClusterStart; qwLengthInBytes > 0; )
    {
        if (qwLengthInBytes <= g_qwBytesPerCluster)
        {
            WriteFAT(dwCluster, 0xFFFFFFFF);
            lseek(iFile, dwCluster * g_qwBytesPerCluster, SEEK_SET);
            write(iFile, pData, qwLengthInBytes);
            qwLengthInBytes = 0;
        }
        else
        {
            DWORD dwNextCluster = AppendCluster(dwCluster, iFile);
            WriteFAT(dwNextCluster, 0xFFFFFFFF);
            lseek(iFile, dwCluster * g_qwBytesPerCluster, SEEK_SET);
            write(iFile, pData, g_qwBytesPerCluster);

            qwLengthInBytes -= g_qwBytesPerCluster;
            dwCluster = dwNextCluster;
            pData = (PVOID) ((PBYTE) pData + g_qwBytesPerCluster);
        }
    }
}

/// @brief Reads clustered data into pBuffer 
/// @return The total number of bytes read upon success, zero otherwise
QWORD ReadClusteredData(DWORD dwClusterStart, PVOID pBuffer, QWORD qwLengthInBytes, INT iFile)
{
    PVOID pClusterBuffer = malloc(g_qwBytesPerCluster);
    QWORD qwTotalBytesRead = 0;
    DWORD dwCurrentCluster = dwClusterStart;

    while (dwCurrentCluster != 0xFFFFFFFF && qwLengthInBytes > 0)
    {
        lseek(iFile, dwCurrentCluster * g_qwBytesPerCluster, SEEK_SET);
        read(iFile, pClusterBuffer, g_qwBytesPerCluster);

        // The last cluster
        if (qwLengthInBytes <= g_qwBytesPerCluster)
        {
            memcpy(pBuffer, pClusterBuffer, qwLengthInBytes);
            qwTotalBytesRead += qwLengthInBytes;
            qwLengthInBytes = 0;
        }
        else
        {
            memcpy(pBuffer, pClusterBuffer, g_qwBytesPerCluster);
            pBuffer = (PBYTE) pBuffer + g_qwBytesPerCluster;
            qwTotalBytesRead += g_qwBytesPerCluster;
            qwLengthInBytes -= g_qwBytesPerCluster;
        }

        DWORD dwFATIndex = ClusterToBootFATIndex(dwCurrentCluster);
        if (dwFATIndex >= g_qwBootFileSystemSize)
            Error("Invalid FAT index: %u", dwFATIndex);
        dwCurrentCluster = g_arrFAT[dwFATIndex];
    }

    free(pClusterBuffer);
    return qwTotalBytesRead;
}

QWORD GetLastCluster(QWORD qwCluster)
{
    QWORD qwNextCluster = ReadFAT(qwCluster);
    if (qwNextCluster == 0xFFFFFFFF) // qwCluster is the last cluster.
        return qwCluster;
    
    QWORD qwPreviousCluster;
    while (qwNextCluster != 0xFFFFFFFF)
    {
        qwPreviousCluster = qwNextCluster;
        qwNextCluster = ReadFAT(qwPreviousCluster);
    }

    return qwPreviousCluster;
}

void AddToBootDirectory(sBootFileDescriptor *pParent, sBootFileDescriptor *pChild, INT iFile)
{
    PBYTE pBuffer = malloc(g_qwBytesPerCluster);
    DWORD dwCurrentCluster = pParent->dwFirstCluster;
    
    for (;;)
    {
        lseek(iFile, dwCurrentCluster * g_qwBytesPerCluster, SEEK_SET);
        read(iFile, pBuffer, g_qwBytesPerCluster);

        for (QWORD i = 0; i < g_qwBytesPerCluster / sizeof(sBootFileDescriptor) * sizeof(sBootFileDescriptor); i += sizeof(sBootFileDescriptor))
        {
            sBootFileDescriptor *pDesc = (sBootFileDescriptor *) (pBuffer + i);
            if (pDesc->bType != 0) continue; // Skip existing entries

            lseek(iFile, dwCurrentCluster * g_qwBytesPerCluster + i, SEEK_SET);
            write(iFile, pChild, sizeof(sBootFileDescriptor));
            free(pBuffer);
            return;
        }
        
        dwCurrentCluster = ReadFAT(dwCurrentCluster);
        if (dwCurrentCluster == 0xFFFFFFFF)
            break;
    }

    dwCurrentCluster = AppendCluster(GetLastCluster(pParent->dwFirstCluster), iFile);
    WriteFAT(dwCurrentCluster, 0xFFFFFFFF);
    lseek(iFile, dwCurrentCluster * g_qwBytesPerCluster, SEEK_SET);
    write(iFile, pChild, sizeof(sBootFileDescriptor));
    
    free(pBuffer);
}

sBootFileDescriptor CreateDescriptor(BYTE bType, PWCHAR wszFileName, QWORD qwFileSize, QWORD dwFirstCluster)
{
    sBootFileDescriptor sDescriptor = { 0 };
    sDescriptor.bType               = bType;
    sDescriptor.bFileNameLength     = strlenW(wszFileName);
    sDescriptor.qwFileSize          = qwFileSize;
    sDescriptor.qwWriteTimeStamp    = TimeStamp();
    sDescriptor.dwFirstCluster      = dwFirstCluster;
    memcpy(sDescriptor.wsFileName, wszFileName, sDescriptor.bFileNameLength * sizeof(WCHAR));
    return sDescriptor;
}

BOOL FindFileInDirectory(const sBootFileDescriptor *pParent, const PWCHAR wszFileName, sBootFileDescriptor *pResult, const INT iFile)
{
    PBYTE pBuffer = malloc(g_qwBytesPerCluster);
    QWORD qwCurrentCluster = pParent->dwFirstCluster;
    if (qwCurrentCluster == 0)
        Error("Descriptor is either invalid or empty");
    QWORD qwFileNameLength = strlenW(wszFileName);

    while (qwCurrentCluster != 0xFFFFFFFF)
    {
        lseek(iFile, qwCurrentCluster * g_qwBytesPerCluster, SEEK_SET);
        read(iFile, pBuffer, g_qwBytesPerCluster);
        
        for (QWORD i = 0; i < g_qwBytesPerCluster; i += sizeof(sBootFileDescriptor))
        {
            sBootFileDescriptor *pDesc = (sBootFileDescriptor *) (pBuffer + i);

            if (pDesc->bType == 0) continue; // The descriptor is empty
            if (pDesc->bFileNameLength != qwFileNameLength) continue; // Optimisation
            if (memcmp(pDesc->wsFileName, wszFileName, qwFileNameLength * sizeof(WCHAR))) continue;

            memcpy(pResult, pDesc, sizeof(sBootFileDescriptor));
            free(pBuffer);
            return true;
        }
        
        qwCurrentCluster = ReadFAT(qwCurrentCluster);
    }

    free(pBuffer);
    return false;
}

sBootFileDescriptor ParsePath(char *szPath, char *szResultFileName, INT iFile)
{
    sBootFileDescriptor sRoot = { 0 };
    sRoot.dwFirstCluster = g_dwNonDataRegion;

    char *sPathDup   = strdup(szPath);
    char *szFileName = strtok(sPathDup, "/\\");

    for (;;)
    {
        char *sNextFileName = strtok(NULL, "/\\");
        if (sNextFileName == NULL) // The previous entry was the last
            break;

        WCHAR wszFileName[65] = { 0 };
        ToWideString(wszFileName, szFileName);
        if (!FindFileInDirectory(&sRoot, wszFileName, &sRoot, iFile))
            Error("Could not find '%s'", szFileName);
        szFileName = sNextFileName;
    }

    strncpy(szResultFileName, szFileName, 53);

    return sRoot;
}

void CreateFilesystem(int iArgc, char **arrArgv, INT iFile, QWORD qwFileSize)
{
    QWORD qwBootFSSize = 0;
    g_bSectorsPerCluster = DEFAULT_SECTORS_PER_CLUSTER;
    g_wBytesPerSector = DEFAULT_BYTES_PER_SECTOR;

    if (iArgc > 3)
    {
        char c;
        while ((c = getopt (iArgc - 2, &arrArgv[2], "b:c:s:")) != -1) {
            switch (c) {
                case 'b':
                    qwBootFSSize = atoll(optarg);
                    break;
                case 'c':
                    g_bSectorsPerCluster = atol(optarg);
                    break;
                case 's':
                    g_wBytesPerSector = atol(optarg);
                    break;
            }
        }
    }

    g_qwBytesPerCluster = (QWORD) g_wBytesPerSector * g_bSectorsPerCluster;
    g_qwBootFileSystemSize = qwFileSize / g_qwBytesPerCluster;
    g_dwNonDataRegion = 1 + (g_qwBootFileSystemSize * sizeof(DWORD) + g_qwBytesPerCluster - 1) / g_qwBytesPerCluster;
    
    sBootSector sBS;
    sBS.arrJump[0]            = 0xEB;
    sBS.arrJump[1]            = 0x16;
    sBS.arrJump[2]            = 0x00;

    sBS.qwMagic               = 0x38303430534f454e; // NEOFS0408
    sBS.wBytesPerSector       = g_wBytesPerSector;
    sBS.bSectorsPerCluster    = g_bSectorsPerCluster;
    sBS.qwRootCluster         = 0;
    sBS.dwBootFileSystemSize  = qwBootFSSize;
    sBS.qwNumberOfSectors     = qwFileSize / sBS.wBytesPerSector;
    sBS.wBootMagic            = 0xAA55;
    
    memset(sBS.arrCode, 0, sizeof(sBS.arrCode));
    memcpy(sBS.arrCode, g_arrBootCode, sizeof(g_arrBootCode));

    // Create a FAT for the boot partition
    if (qwBootFSSize != 0)
    {
        if (qwBootFSSize * g_qwBytesPerCluster + qwBootFSSize * sizeof(DWORD) + g_qwBytesPerCluster > qwFileSize)
            Error("Not enough space for boot filesystem");
        
        PDWORD pFAT = calloc(1, qwBootFSSize * sizeof(DWORD));
        pFAT[0] = 0xFFFFFFFF;
        
        // The boot FAT is always at cluster 1
        lseek(iFile, g_qwBytesPerCluster, SEEK_SET);
        write(iFile, pFAT, qwBootFSSize * sizeof(DWORD));
        free(pFAT);
    }
    
    lseek(iFile, 0, SEEK_SET);
    write(iFile, &sBS, sizeof(sBootSector));
}

void AddBootFile(int iArgc, char **arrArgv, INT iFile, QWORD qwFileSize)
{
    if (iArgc != 5)
        Error("Incorrect number of arguments");

    LoadBootSector(iFile);
    LoadFAT(iFile);
    
    char szFileName[65] = { 0 };
    sBootFileDescriptor sParent = ParsePath(arrArgv[4], szFileName, iFile);

    WCHAR wszFileName[65] = { 0 };
    ToWideString(wszFileName, szFileName);

    // Check if the file exists
    sBootFileDescriptor sFile;
    if (FindFileInDirectory(&sParent, wszFileName, &sFile, iFile))
    {
        // Clean-up
        free(g_arrFAT);
        Error("File '%s' already exists", szFileName);
    }

    // Read the host's file
    // TODO: Add a write function, that copies a file chunk-for-chunk, so less RAM is used.
    FILE *pHostFile = fopen(arrArgv[3], "rb");
    if (pHostFile == NULL)
        Error("Host file '%s' doesn't exist", arrArgv[3]);

    // Get the file size
    fseek(pHostFile, 0, SEEK_END);
    QWORD qwHostFileSize = ftell(pHostFile);
    fseek(pHostFile, 0, SEEK_SET);

    PVOID pBuffer = malloc(qwHostFileSize);
    fread(pBuffer, qwHostFileSize, 1, pHostFile);

    fclose(pHostFile);

    sFile = CreateDescriptor(1, wszFileName, qwHostFileSize, GetCluster());
    if (sFile.dwFirstCluster == 0xFFFFFFFF)
        Error("Not enough space");

    WriteFAT(sFile.dwFirstCluster, 0xFFFFFFFF);
    AddToBootDirectory(&sParent, &sFile, iFile);
    
    // Write the data
    WriteClusteredData(sFile.dwFirstCluster, pBuffer, qwHostFileSize, iFile);

    StoreFAT(iFile);

    // Clean-up
    free(pBuffer);
    free(g_arrFAT);
}

void AddBootFolder(int iArgc, char **arrArgv, INT iFile, QWORD qwFileSize)
{
    if (iArgc != 4)
        Error("Incorrect number of arguments");

    LoadBootSector(iFile);
    LoadFAT(iFile);

    // Construct a file descriptor
    char szFileName[65] = { 0 };
    sBootFileDescriptor sParent = ParsePath(arrArgv[3], szFileName, iFile);
    WCHAR wszFileName[65] = { 0 };
    ToWideString(wszFileName, szFileName);

    sBootFileDescriptor sFile;
    if (FindFileInDirectory(&sParent, wszFileName, &sFile, iFile))
    {
        // Clean-up
        free(g_arrFAT);
        Error("Folder '%s' already exists", szFileName);
    }

    // Create the directory entry
    sFile = CreateDescriptor(2, wszFileName, 0, GetCluster());
    WriteFAT(sFile.dwFirstCluster, 0xFFFFFFFF);
    AddToBootDirectory(&sParent, &sFile, iFile);

    StoreFAT(iFile);

    // Clean-up
    free(g_arrFAT);
}


void TreeBoot(int iArgc, char **arrArgv, INT iFile, QWORD qwFileSize)
{
    if (iArgc != 4)
        Error("Incorrect number of arguments");
    
    Error("Not yet implemented");
}


int main(int iArgc, char **arrArgv)
{
    if (iArgc < 2)
    {
        printf("Usage: %s create <filesystem> [params]\n", arrArgv[0]);
        printf("               -b --boot-fs <size in clusters>\n\
               -c --cluster-size <num sectors>\n\
               -s --sector-size <num bytes>\n");
        printf("       %s add-boot-file <filesystem> <source file> <destination path>\n", arrArgv[0]);
        printf("       %s add-boot-folder <filesystem> <path>\n", arrArgv[0]);
        return 1;
    }
    else if (!strcmp(arrArgv[1], "version"))
    {
        printf("Compile date %s time %s\n", __DATE__, __TIME__);
        return 0;
    }

    INT iFile = open(arrArgv[2], O_RDWR);
    
    struct stat st;
    if (fstat(iFile, &st) == -1)
    {
        perror("fstat");
        close(iFile);
        return 1;
    }

    QWORD qwFileSize;
    
    // Regular file
    if (S_ISREG(st.st_mode))
        qwFileSize = st.st_size;
    // Block device
    else if (S_ISBLK(st.st_mode))
    {
        if (ioctl(iFile, BLKGETSIZE64, &qwFileSize) == -1)
        {
            perror("ioctl");
            close(iFile);
            return 1;
        }
    }
    // Failure
    else
    {
        printf("Invalid file path: %s\n", arrArgv[2]);
        close(iFile);
        return 1;
    }
    

    if (!strcmp(arrArgv[1], "create"))
        CreateFilesystem(iArgc, arrArgv, iFile, qwFileSize);
    else if (!strcmp(arrArgv[1], "add-boot-file"))
        AddBootFile(iArgc, arrArgv, iFile, qwFileSize);
    else if (!strcmp(arrArgv[1], "add-boot-folder"))
        AddBootFolder(iArgc, arrArgv, iFile, qwFileSize);
    // else if (!strcmp(arrArgv[1], "tree-boot"))
    //     TreeBoot(iArgc, arrArgv, iFile, qwFileSize);
    else
        printf("Invalid command '%s'", arrArgv[1]);
    
    close(iFile);

    return 0;
}
