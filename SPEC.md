
# The specification for the NeoFS filesystem

# General
- The GUID of the filesystem shall be 204f4857-5349-4c20-4f52-443f20343036


# Type definitions
- A ```BYTE``` is 1 byte and unsigned
- A ```WORD``` is 2 bytes and unsigned
- A ```DWORD``` is 4 bytes and unsigned
- A ```QWORD``` is 8 bytes and unsigned
- A ```CHAR``` is 1 byte and signed
- A ```SHORT``` is 2 bytes and signed
- An ```INT``` is 4 bytes and signed
- A ```LONGLONG``` is 8 bytes and signed


# The boot sector
- qwMagic must be 0x38303430534f454e 'NEOFS0408'
```c
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
```

# The boot filesystem
- The boot filesystem contains only the essentials to boot the system. For external storage mediums, the value qwBootStartCluster can be set to zero to remove it.
- If a FAT-entry is zero, it is empty. If it is 0xFFFFFFFF, that is the last entry of a file.
- The FAT starts at the 2nd sector and its size is calculated in the following manner: 4 * dwBootFileSystemSize. The remaining space is padded with zeroes.
- The space after the FAT is called the data region with everything before being the non-data region.
- The FAT only represents clusters inside the data region of the boot filesystem.
- The boot filesystem, save for the bootsector is entirely isolated from the main filesystem.
- Folders contain boot file descriptors (a filesystem inside a filesystem).
- The folder's file size field is used for the number of entries instead of actual file size.

## File entry
```c
typedef struct _tagBootFileDescriptor
{
    BYTE  bType; // 0 is empty, 1 is a file, 2 is a folder.
    WCHAR wsFileName[53];
    BYTE  bFileNameLength; // Filename length for faster enumeration
    QWORD qwFileSize;
    DWORD dwFirstCluster;
    QWORD qwWriteTimeStamp;
} __attribute__((packed)) sBootFileDescriptor;
```


# The main filesystem
- The main filesystem comes right after the boot filesytem if there is any.
- The each file is an object with an logical ID (what software sees) and a physical ID (what hardware sees).
- Each object can have different versions (like alternate data streams in NTFS). This is used both for backups and sandboxing.
- Each file will have the option to be compressed with a specified algorithm: 0x00 is none, 0x01 is ZSTD, 0x02 is LZ4, 0x03 is XZ. The second nibble is an extra parameter (e.g. compression ratio).
- Files can either inherit their encrpytion key from their parent or have a custom encryption key.

