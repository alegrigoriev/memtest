// mtstart.cxx
// DOS module for loading the PE module.
// Linked as a stub to Portable Executable file.

#include <stdio.h>
#include <string.h>
#include <dos.h>
#include "mtstart.h"
#include "memtest.h"

#ifdef _DEBUG
extern char fgetsbuf[30];
#endif
// INT15 C7 function does not work in many BIOSes
// better use command line switches and write and read trial.

BOOL GetMemorySizes(MemorySizes * pSizes)
{
    // use Int 15h
    BIOS_MEMORY_MAP map;
    memset(&map, 0, sizeof map);
    map.cbSize = 32;
    __asm {
        //mov     ax,0C000h
        //int     15h
        //cmp     ah,0
        //jnz     err     // not supported
        //test    byte ptr es:[bx+6], 10h
        //jz      err     // not supported
        push    si
        // ss == ds
        lea     si,map
        mov     ax,0xC700
        int     15h
        pop     si
    }
    // see if non-zero values written in the structure
    if (map.cbSize != 32)
        return FALSE;
    return TRUE;
//err:
    //return FALSE;
}

BOOL IsV86()
{
#ifdef _DEBUG
    printf("Checking for V86, press Enter to continue...");
    fgets(fgetsbuf, sizeof fgetsbuf, stdin);
#endif
    DWORD msw = 0;
    __asm SMSW msw;
    return WORD(msw) & 1; // if PE bit in MSW (CR0) is set, it is V86 mode
}

inline DWORD GetFlatAddress(const void huge * ptr)
{
    return _FP_OFF(ptr) + _FP_SEG(ptr) * 16LU;
}

inline void InitDescriptor(DESCRIPTOR __far & d, const void far * base, DWORD limit,
                           DWORD flags)
{
    InitDescriptor(d, GetFlatAddress(base), limit, flags);
}

inline void huge * NormalizeHugePointer(void huge * ptr)
{
    unsigned seg = _FP_SEG(ptr) + (_FP_OFF(ptr) >> 4);
    unsigned off = _FP_OFF(ptr) & 0xF;
    return _MK_FP(seg, off);
}

BOOL LoadPE(char * pFilename, PROTECTED_MODE_STARTUP_DATA * pStartup)
{
    // open the file.
    //pFilename = "debug/memtest.exe";
    FILE * pFile = fopen(pFilename, "rb");
    if (NULL == pFile)
    {
        return FALSE;
    }

    // find PE header location.
    MSDOS_EXE_HEADER MZ_header;
    PE_HEADER PE_header;
    PE_OPTIONAL_HEADER PE_opt_header;

    if (fread (& MZ_header, sizeof MZ_header, 1, pFile) != 1
        || (MZ_header.mz[0] != 'M' || MZ_header.mz[1] != 'Z')
        && (MZ_header.mz[0] != 'Z' || MZ_header.mz[1] != 'M')
        || MZ_header.PE_offset < sizeof MZ_header)
    {
        fclose(pFile);
        return FALSE;
    }
    // load PE header

    if (fseek (pFile, MZ_header.PE_offset, SEEK_SET) != 0 // 0 - success
        || fread (& PE_header, sizeof PE_header, 1, pFile) != 1
        || PE_header.Signature[0] != 'P'
        || PE_header.Signature[1] != 'E'
        || PE_header.Signature[2] != 0
        || PE_header.Signature[3] != 0
        || PE_header.Machine != IMAGE_FILE_MACHINE_I386
        || PE_header.OptionalHeaderSize < sizeof PE_opt_header
        || fread (& PE_opt_header, sizeof PE_opt_header, 1, pFile) != 1
        || PE_opt_header.Magic != 0x10B
        // check header constraints:
        // the module should be bound to 0x400000 base address
        //
        || PE_opt_header.ImageBase != 0x00400000)
    {
        fclose(pFile);
        return FALSE;
    }
    // calculate required size and allocate the memory
    DWORD ModuleRequiredMemory = PE_opt_header.ImageSize + 0x1000u; // page alignment
    // allocate memory
    char huge * pModuleMemory = new huge char[ModuleRequiredMemory];
    if (NULL == pModuleMemory)
    {
        fclose(pFile);
        return FALSE;
    }

    // align allocated memory to page boundary
    pModuleMemory += 0xFFF & -(long)GetFlatAddress(pModuleMemory);
    pModuleMemory = (char huge *)NormalizeHugePointer(pModuleMemory);

    // skip the optional header data directories
    fseek(pFile, PE_opt_header.NumberOfDataDirectories * 8, SEEK_CUR);
    // the module should be aligned on 4K boundary
    pStartup->pProgramBase = pModuleMemory;
    for (unsigned i = 0; i < PE_header.NumOfSections; i++)
    {
        SECTION_HEADER sh;
        // read section descriptor
        if (fread(& sh, sizeof sh, 1, pFile) != 1
            || sh.Offset + sh.VirtualSize > PE_opt_header.ImageSize)
        {
            delete[] pModuleMemory;
            fclose(pFile);
            return FALSE;
        }
        // read the section
        long OldPos = ftell(pFile);
        fseek(pFile, sh.RawDataPosition, SEEK_SET);
        char huge * pSection = (char huge *)NormalizeHugePointer(pModuleMemory + sh.Offset);

        // read the data byte by byte
        for (unsigned long j = 0; j < sh.VirtualSize; j++)
        {
            int byte;
            if (j < sh.RawDataSize)
            {
                byte = fgetc(pFile);
                if (-1 == byte)
                {
                    delete[] pModuleMemory;
                    fclose(pFile);
                    return FALSE;
                }
            }
            else
            {
                byte = 0;
            }
            *pSection = (char) byte;
            pSection++;
        }
        fseek(pFile, OldPos, SEEK_SET);
    }
    // close the file
    fclose(pFile);

    pStartup->ProgramEntry = PE_opt_header.EntryPointRVA
                             + PE_opt_header.ImageBase;
    pStartup->ProgramSize = PE_opt_header.ImageSize;
    pStartup->msp.ProgramTop = PE_opt_header.ImageBase
                               + PE_opt_header.ImageSize;
    return TRUE;
}

void ProtectedModeStart(PROTECTED_MODE_STARTUP_DATA * pStartup)
{
    char huge * pTmp = new far char[0x1000 + sizeof PROTECTED_MODE_STARTUP_MEMORY];
    if (NULL == pTmp) return;   // unable to start

    // init pAuxMemory to page-aligned address and zero it
    PROTECTED_MODE_STARTUP_MEMORY far * pAuxMemory =
        (PROTECTED_MODE_STARTUP_MEMORY far *)
        ( pTmp + (0xFFF & -long(GetFlatAddress(pTmp))));
    _fmemset( pAuxMemory, 0, sizeof *pAuxMemory);
    // Build starting page table.
    // The table consists of 3 pages - one is first level, two other -
    // second level for 8 MB space.
    // Map low memory 1:1
    pAuxMemory->TableDir[0] = GetFlatAddress(& pAuxMemory->PageDir0) |
                              (PAGE_DIR_FLAG_PRESENT | PAGE_DIR_FLAG_WRITABLE
                                  | PAGE_DIR_FLAG_ACCESSED);
    pAuxMemory->TableDir[1] = GetFlatAddress(& pAuxMemory->PageDir1) |
                              (PAGE_DIR_FLAG_PRESENT | PAGE_DIR_FLAG_WRITABLE
                                  | PAGE_DIR_FLAG_ACCESSED);
    // fill PageDir0 to 1:1 mapping
    DWORD addr;
    for (addr = 0; addr < 0x400000LU; addr += 0x1000)
    {
        pAuxMemory->PageDir0[addr / 0x1000u] = addr |
                                               (PAGE_DIR_FLAG_PRESENT | PAGE_DIR_FLAG_WRITABLE
                                                   | PAGE_DIR_FLAG_ACCESSED | PAGE_DIR_FLAG_DIRTY);
        pAuxMemory->PageDir1[addr / 0x1000u] = (addr + 0x400000LU) |
                                               (PAGE_DIR_FLAG_PRESENT | PAGE_DIR_FLAG_WRITABLE
                                                   | PAGE_DIR_FLAG_ACCESSED | PAGE_DIR_FLAG_DIRTY);
    }
    // fill PageDir1 to map the program area from 4MB virtual
    for (addr = 0; addr < pStartup->ProgramSize; addr += 0x1000)
    {
        pAuxMemory->PageDir1[addr / 0x1000u]
            = (addr + GetFlatAddress(pStartup->pProgramBase))
                | (PAGE_DIR_FLAG_PRESENT | PAGE_DIR_FLAG_WRITABLE
                    | PAGE_DIR_FLAG_ACCESSED | PAGE_DIR_FLAG_DIRTY);
    }

    // Create starting interrupt table
    // All interrupts in the table should shutdown the system
    // (because we don't know actual entry points yet).
    // we use INT15/89h function to switch to protected mode

    // create starting GDT. We don't need any separate LDT
    // It should contain code segment descriptor and data segment descriptor,
    // both with 4GB limit.

/*
Format of BIOS switch-to-protected-mode Global Descriptor Table:
Offset  Size    Description
00h  8 BYTEs   null descriptor (initialize to zeros)
08h  8 BYTEs   GDT descriptor
10h  8 BYTEs   IDT descriptor
18h  8 BYTEs   DS descriptor
20h  8 BYTEs   ES
28h  8 BYTEs   SS
30h  8 BYTEs   CS
38h  8 BYTEs   uninitialized, used to build descriptor for BIOS CS

*/
    // GTD[0] is zeroed

    InitDescriptor(pAuxMemory->GDT[1], & pAuxMemory->GDT,
                   sizeof pAuxMemory->GDT - 1,
                   SEG_DESC_DATA);
    InitDescriptor(pAuxMemory->GDT[2], & pAuxMemory->IDT,
                   8, //sizeof pAuxMemory->IDT - 1, // only NMI
                   SEG_DESC_DATA);
    // code segment descriptor
    void far * tmp_ptr = NULL;
    // init ds, es
    __asm {
        mov     word ptr [tmp_ptr + 2],DS
    }
    InitDescriptor(pAuxMemory->GDT[3], tmp_ptr,
                   0x0000FFFFLU, SEG_DESC_DATA);
    pAuxMemory->GDT[4] = pAuxMemory->GDT[3];
    //init SS
    __asm {
        mov     word ptr [tmp_ptr + 2],SS
    }
    InitDescriptor(pAuxMemory->GDT[5], tmp_ptr,
                   0x0000FFFFLU, SEG_DESC_DATA);
    // init CS
    __asm {
        mov     word ptr [tmp_ptr + 2],CS
    }
    InitDescriptor(pAuxMemory->GDT[6], tmp_ptr,
                   0x0000FFFFLU, SEG_DESC_CODE);

    // 32 bit data and stack segment sescriptor
    InitDescriptor(pAuxMemory->GDT[8], 0LU, 0xFFFFFFFF,
                   SEG_DESC_DATA_32BIT);
    // 32 bit code segment descriptor
    InitDescriptor(pAuxMemory->GDT[9], 0LU, 0xFFFFFFFF,
                   SEG_DESC_CODE_32BIT);
    // TSS descriptor
    InitDescriptor(pAuxMemory->GDT[10], & pAuxMemory->TSS,
                   sizeof pAuxMemory->TSS, SEG_DESC_TSS_32BIT);

    InitDescriptor(pAuxMemory->GDT[11], 0xB8000,
                   0x0000FFFFLU, SEG_DESC_DATA);

    // set startup argument
    pAuxMemory->StartupArgument = GetFlatAddress(& pStartup->msp);   // MEMTEST_STARTUP_PARAMS

    // init TSS
    // IOMapOffset, _ldt, _gs, _fs - not required
    pAuxMemory->TSS._ds = 8*8;  // 8th descriptor
    pAuxMemory->TSS._es = 8*8;  // 8th descriptor
    pAuxMemory->TSS._ss = 8*8;  // 8th descriptor
    pAuxMemory->TSS._cs = 9*8;  // 9th descriptor
    pAuxMemory->TSS._esp = GetFlatAddress(& pAuxMemory->StackTop);
    pAuxMemory->TSS._eflags = 2;
    pAuxMemory->TSS._eip = pStartup->ProgramEntry;

    DWORD pdt_addr = GetFlatAddress( & pAuxMemory->TableDir);
    pAuxMemory->TSS._cr3 = pdt_addr;
    pAuxMemory->TSS.IOMapOffset = WORD(DWORD( & pAuxMemory->TSS.IOMap) - DWORD( & pAuxMemory->TSS));

    tmp_ptr = & pAuxMemory->GDT;
    void far * pTaskJump = (void far *) ((10 * 8L) << 16);

    //void far * pDisplay = (void far *) ((11 * 8L) << 16);
    // flush file caches (unlikely that any exists,
    // since XMS should not be installed).
    __asm {
        mov     ah,0Dh
        INT     21h

        // disable interrupts
        cli
        // switch to protected mode
        mov     ax,8900h
        mov     bx,3830h    // redirect interrupts to int30-int40
        les     si,tmp_ptr
        INT     15h
        jc      no_pmode
        // enable paging
        __emit 0x66 __asm mov     ax,word ptr pdt_addr // mov eax, pdt_addr
        __emit 0x0F __asm __emit 0x22 __asm __emit 0xD8    // mov  cr3, eax
        // mov  eax,0x80000001  // PE | PG
        _emit 66h _asm _emit 0B8h _asm _emit 1 _asm _emit 0 _asm _emit 0 _asm _emit 0x80
        // mov  cr0, eax
        __emit 0x0F __asm __emit 0x22 __asm __emit 0xC0
        // Load TSS address
        // Jump to Task
        jmp     dword ptr pTaskJump
    }

no_pmode:
    return;
}


