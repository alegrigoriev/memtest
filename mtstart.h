// mtstart1.h
#pragma pack (1)

typedef int BOOL;
#define FALSE 0
#define TRUE 1
#define IMAGE_FILE_MACHINE_I386              0x14c   // Intel 386.

#define __emit  _emit

typedef unsigned char BYTE;
typedef unsigned short WORD;
typedef unsigned long DWORD;

struct MemorySizes
{
    long pBase1;
    long pSize1;
    long pBase2;
    long pSize2;
    long pBase3;
    long pSize3;
};

struct BIOS_MEMORY_MAP
{
    WORD cbSize;
    DWORD cbLocalMemSize1M16M;  // size of local memory between 1M-16M
    DWORD cbLocalMemSize16M4G;  // size of local memory between 16M-4G
    DWORD cbSystemMemSize1M16M;  // size of system memory between 1M-16M
    DWORD cbSystemMemSize16M4G;  // size of system memory between 16M-4G
    DWORD cbCacheableMemSize1M16M;  // size of cacheable memory between 1M-16M
    DWORD cbCacheableMemSize16M4G;  // size of cacheable memory between 16M-4G
    DWORD cbNonSystemMemOffset1M16M;  // size of non-system memory between 1M-16M
    DWORD cbNonSystemMemOffset16M4G;  // size of non-system memory between 16M-4G
    DWORD dwDummy[2];
};

struct MSDOS_EXE_HEADER {
    char mz[0x3c];
    DWORD PE_offset;
};

struct PE_HEADER {
    char Signature[4];
    WORD Machine;
    WORD NumOfSections;
    DWORD DateTimeStamp;
    DWORD PointerToSymbolTable;
    DWORD NumOfSymbols;
    WORD OptionalHeaderSize;
    WORD Attributes;
};

struct PE_OPTIONAL_HEADER
{
    WORD Magic;
    char LMajor;
    char LMinor;
    DWORD CodeSize;
    DWORD InitedDataSize;
    DWORD UninitDataSize;
    DWORD EntryPointRVA;
    DWORD BaseOfCode;
    DWORD BaseOfData;
    DWORD ImageBase;
    DWORD SectionAlignment;
    DWORD FileAlignment;
    WORD OSMajor;
    WORD OSMinor;
    WORD UserMajor;
    WORD UserMinor;
    WORD SubSysMajor;
    WORD SubSysMinor;
    DWORD Reserved;
    DWORD ImageSize;
    DWORD HeaderSize;
    DWORD FileChecksum;
    WORD SubSystem;
    WORD DLLFlags;
    DWORD StackReserveSize;
    DWORD StackCommitSize;
    DWORD HeapReserveSize;
    DWORD HeapCommitSize;
    DWORD LoaderFlags;
    DWORD NumberOfDataDirectories;
};

struct SECTION_HEADER
{
    char SectionName[8];
    DWORD VirtualSize;
    DWORD Offset;
    DWORD RawDataSize;
    DWORD RawDataPosition;
    DWORD RelocsPosition;
    DWORD LinenumbersPosition;
    WORD NumberOfRelocs;
    WORD NumberOfLinenumbers;
    DWORD Flags;
};

BOOL LoadPE(char * pFilename, struct PROTECTED_MODE_STARTUP_DATA * pStartup);

void ProtectedModeStart(struct PROTECTED_MODE_STARTUP_DATA * pStartup);

#ifdef _DEBUG
#define TRACE printf
#define PAUSE(t) printf(t); fgets(fgetsbuf, sizeof fgetsbuf, stdin)
#else
#define TRACE 1 ? (void) 0 : (void)
#define PAUSE(t)
#endif
