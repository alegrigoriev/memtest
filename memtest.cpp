// memtest.cpp

#include <windows.h>
typedef unsigned short WORD;
typedef unsigned long DWORD, *PDWORD;
typedef unsigned long ULONG;
typedef unsigned long ULONG_PTR;
typedef unsigned char BYTE, UCHAR, *PBYTE, *PUCHAR;
typedef unsigned long long ULONGLONG;

#define far // nothing
#include <intrin.h>

#include <stdio.h>
#ifndef countof
#define countof(array) (sizeof(array)/sizeof(array[0]))
#endif

#define __far
extern "C" {
    int __cdecl _inp(unsigned short);
    unsigned short __cdecl _inpw(unsigned short);
    unsigned long __cdecl _inpd(unsigned short);
    int __cdecl _outp(unsigned short, int);
    unsigned short __cdecl _outpw(unsigned short, unsigned short);
    unsigned long __cdecl _outpd(unsigned short, unsigned long);
}
#pragma intrinsic(_inp, _inpw, _inpd, _outp, _outpw, _outpd)

#include "memtest.h"

#include <memory.h>
#pragma intrinsic(memcpy, _rotr)

#ifndef NULL
#define NULL 0
#endif

typedef int BOOL;
typedef unsigned long DWORD;
typedef BYTE * __ptr64 PHYSICAL_ADDR;
typedef ULONGLONG SIZET64;

const size_t cache1_preload_size = 4096;
const size_t cache2_preload_size = 1024 * 128;
size_t MemoryRowSize = 0x10000;

class TestThread : public MEMTEST_STARTUP_PARAMS
{
public:
    TestThread()
    {
        dwRandSeed = -1L;
        m_TestPass = 1;
        m_TestDelay = 0;
        m_ThreadNumber = 1;
    }
    int m_ThreadNumber;
    DWORD dwRandSeed;
    int m_TestPass;
    int m_TestDelay;

    unsigned TestFunction();

    void DoMemoryTestPattern(char * addr, size_t _size,
                             DWORD const InitPattern1, DWORD const InitPattern2,
                             DWORD flags, DWORD MemoryFlags);
    DWORD DoRandomMemoryTest(char * addr, size_t _size, DWORD seed,
                             DWORD polynom, DWORD flags, DWORD MemoryFlags);
};

// TestThread derived classes used to exist for Windows memory test thread and disk test thread,
// but they are removed from this version, which is exclusively standalone

MEMTEST_STARTUP_PARAMS TestParams;

BOOL RebootOnFinish;

int CheckForKey();

PHYSICAL_ADDR pMemoryToTestStart;
PHYSICAL_ADDR pMemoryToTestEnd;

////////////////////////////////////////////////////
// Page directory stuff

PageTable * PageTablePtr;    // VIRTUAL address of page table
size_t PageTableSize;
#define PAGE_DIR_FLAG_LARGE_PAGE 0x80    // 4 megabyte page

PHYSICAL_ADDR PageTablePhysicalAddr;

void * TopVirtualAddress;
void * TopUsedAddress;  // top virtual address used by the program and tables
size_t MemoryInUseByProgram;    // including page tables

PHYSICAL_ADDR CurrentPhysProgramLocation;
PHYSICAL_ADDR PhysMemoryProgramUsedEnd;

char * TopProgramAddress;
// Current Processor features
#define CPUID_4MB_PAGES     8
#define CPUID_MACHINE_CHECK_ARCHITECTURE 0x4000    // bit 14
#define CPUID_MACHINE_CHECK_EXCEPTION 0x80    // bit 7
#define CPUID_TIME_STAMP_COUNTER 0x10

// machine check MSRs
#define MCG_CAP                     0x179
#define MCG_CTL_PRESENT             0x100
#define MCG_STATUS                  0x17A
#define MCG_CTL                     0x17B
#define MC0_STATUS                  0x401
#define MC0_CTL                     0x400

#define CR4_4MB_PAGES_ENABLED       0x10
#define CR4_PAE_ENABLED             0x20
#define CR4_MACHINE_CHECK_ENABLED   0x40    // bit 6

BOOL LargePagesSupported;

//////////////////////////////////////////////////////////////
//// screen stuff
// white on black test color mask for text mode display buffer
const int screen_width = 80;
const int screen_height = 25;

// pointer to screen buffer in 32-bit address space
unsigned __int16 (*const screenbase)[screen_width]
= (unsigned __int16 (*)[screen_width]) 0xb8000;

int CurrentCursorRow=24, CurrentCursorColumn=0; // current row and column on text mode display

char const title[] = MEMTEST_TITLE;

/////////////////////////////////////////////////////////////
// Function declarations

void DetectInstalledMemory(MEMTEST_STARTUP_PARAMS * pTestParams);
BOOL TestPageForPresence(void * VirtAddr, BOOL FastDetect);
DWORD GetPageFlags(void * VirtAddr);
void ModifyPageFlags(void * VirtAddr, size_t size, DWORD SetFlags,
                     DWORD ResetFlags = 0, PageTable *MapTable = PageTablePtr);
PHYSICAL_ADDR GetPhysAddr(void * VirtAddr, PageTable * pPageTable = PageTablePtr);    // convert physical to virtual
void MapVirtualToPhysical(void * VirtAddr, PHYSICAL_ADDR PhysAddr, size_t size,
                          PageTable* MapTable = PageTablePtr,
                          DWORD Flags = PAGE_DIR_FLAG_PRESENT
                                        | PAGE_DIR_FLAG_WRITABLE
                                        | PAGE_DIR_FLAG_ACCESSED);

#define MAP_VIRTUAL_TO_PHYS_NO_INVTLB 0x80000000

void InitPageTable(PageTable * VirtPageDirAddress, size_t BufSize);
void InitGate(GATE & g, WORD selector, void * offset, WORD flags);
// init interrupt table
void InitInterruptTable(GATE * Addr);
// move the program and all the tables to next 4 MByte
void RelocateProgram(void);

typedef DWORD MemoryOperationFunction(void * addr, size_t size,
                                      DWORD arg1, DWORD arg2, DWORD arg3, DWORD arg4, DWORD flags);

DWORD RunMemoryForward(MemoryOperationFunction *function, void * addr, size_t size,
                       DWORD arg1, DWORD arg2,
                       DWORD arg3, DWORD arg4,
                       DWORD flags, DWORD MapFlags);
DWORD RunMemoryBackward(MemoryOperationFunction *function, void * addr, size_t size,
                        DWORD arg1, DWORD arg2,
                        DWORD arg3, DWORD arg4,
                        DWORD flags, DWORD MapFlags);

extern "C" {
    int __cdecl _inp(unsigned short);
    unsigned short __cdecl _inpw(unsigned short);
    unsigned long __cdecl _inpd(unsigned short);
    int __cdecl _outp(unsigned short, int);
    unsigned short __cdecl _outpw(unsigned short, unsigned short);
    unsigned long __cdecl _outpd(unsigned short, unsigned long);
}
#pragma intrinsic(_inp, _inpw, _inpd, _outp, _outpw, _outpd)

static GATE g;
static PSEUDO_DESC fptr = {0, 8, & g};

#define RESET_IDT() do  \
    {                   \
    __asm lidt fptr.len   \
    } while(0)

//////////////////////////////////////////////////////////////

#ifdef _DEBUG
static long holdrand = 1L;

/***
*void srand(seed) - seed the random number generator
*
*Purpose:
*       Seeds the random number generator with the int given.  Adapted from the
*       BASIC random number generator.
*
*Entry:
*       unsigned seed - seed to seed rand # generator with
*
*Exit:
*       None.
*
*Exceptions:
*
*******************************************************************************/

void srand (
            unsigned int seed
            )
{
    holdrand = (long)seed;
}


/***
*int rand() - returns a random number
*
*Purpose:
*       returns a pseudo-random number 0 through 32767.
*
*Entry:
*       None.
*
*Exit:
*       Returns a pseudo-random number 0 through 32767.
*
*Exceptions:
*
*******************************************************************************/
//#define RAND_MAX 0x7FFF

int rand (
          void
          )
{
    return(((holdrand = holdrand * 214013L + 2531011L) >> 16) & 0x7fff);
}
#endif

inline void InitDescriptor(DESCRIPTOR & d, void * base, DWORD limit,
                           DWORD flags)
{
    InitDescriptor(d, DWORD(base), limit, flags);
}

// SwitchPageTable: change to new table directory address (CR3)
// page table switch should be implemented as an inline #define
// because stack contents should not change between stack copy and
// CR3 reload. This means that a call/ret pair can't be used
#define SwitchPageTable(PhysPageDirAddress)         \
    __writecr3((unsigned)ULONGLONG(PhysPageDirAddress));       \
        __asm {jmp     label1}                       \
label1:;


#define TRUE 1
#define FALSE 0
#ifdef _DEBUG
#define ASSERT(condition) do if ( ! (condition)) {my_puts(__FUNCTION__ " ASSERT for \"" #condition "\"", FALSE); while(1) CheckForKey(); } while(FALSE)
#define ASSERT_INFO(condition, format, ...) do if ( ! (condition)) {my_printf(FALSE, __FUNCTION__ " ASSERT for \"" #condition "\" " format, __VA_ARGS__); while(1) CheckForKey(); } while(FALSE)
#else
#define ASSERT(condition) do {} while(FALSE)
#define ASSERT_INFO(condition, format, ...) do {} while(FALSE)
#endif

void my_puts(const char * str, BOOL IsErrMsg,
                       unsigned __int16 color_mask)
{
    static int error_row = 0;
    unsigned __int16 * position = &screenbase[CurrentCursorRow][CurrentCursorColumn];
    unsigned char c;
    if (IsErrMsg && 0 == error_row)
    {
        error_row = screen_height-1;
    }

    while ((c = *str++) != 0)
    {
        if ('\r' == c)
        {
            CurrentCursorColumn = 0;
            position = &screenbase[CurrentCursorRow][0];
            continue;
        }

        if ('\n' == c || CurrentCursorColumn >= screen_width)
        {
            // do scrolling or move cursor to the next line
            CurrentCursorColumn = 0;
            if (CurrentCursorRow < screen_height - 1)
            {
                CurrentCursorRow++;
            }
            else
            {
                memcpy(screenbase, &screenbase[1][0], (screen_height - 1) * sizeof screenbase[0]);
                CurrentCursorRow = screen_height - 1;
            }
            position = &screenbase[CurrentCursorRow][0];
            // clear the row
            for (int i = 0; i < screen_width; i++)
            {
                position[i] = color_mask;
            }
            // make sure error messages are not gone off the screen
            if (1 == error_row)
            {
                my_puts("Press Enter to continue\r", FALSE, 0x8F00);    // blink
                int key;
                do {
                    key = CheckForKey();
                } while (key != 0x1c && key != 0x39);
                do {
                    key = CheckForKey();
                } while (key != 0);
                error_row = 0;
                // clear the line again
                for (int i = 0; i < screen_width; i++)
                {
                    position[i] = color_mask;
                }
            }
            if (error_row != 0)
            {
                error_row--;
            }

            if ('\n' == c) continue;
        }
        *position = c | color_mask;
        position++;
        CurrentCursorColumn++;
    }
}

char * itox(char * buffer, DWORD n)
{
    for (int i = 32-4; i >= 0; i-=4)
    {
        char c = char(((n >> i) & 0xF) + '0');
        if (c > '9')
            c += 'A' - ('9' + 1);
        *buffer++ = c;
    }
    return buffer;
}

char * i40tox(char * buffer, ULONGLONG n)
{
    // 40 least significant bits as hex
    for (int i = 40-4; i >= 0; i-=4)
    {
        char c = char(((n >> i) & 0xF) + '0');
        if (c > '9')
        {
            c += 'A' - ('9' + 1);
        }
        *buffer++ = c;
    }
    return buffer;
}

char * itod(char * buffer, DWORD n)
{
    DWORD rem = n % 10U;
    DWORD quot = n / 10U;
    if (quot)
    {
        buffer = itod(buffer, quot);
    }
    *buffer++ = char(rem + '0');
    return buffer;
}

// only %x and %d formats are supported
void my_vsprintf(char * buffer, const char * format, void * vararg)
{
    do
    {
        char c = *format;
        if (c != '%')
        {
            *buffer++ = c;
        }
        else
        {
            format++;
            switch (c = *format)
            {
            case 'x':
            case 'X':
                buffer = itox(buffer, * (int *) vararg);
                break;

            case 's':
            {
                const char * s = * (const char * *) vararg;
                int len = strlen(s);
                memcpy(buffer, s, len);
                buffer += len;
            }
                break;

            case 'D':
            case 'd':
                buffer = itod(buffer, * (int *) vararg);
                break;
            case 'p':   // physical address, 10 hex chars
                buffer = i40tox(buffer, * (ULONGLONG *) vararg);
                vararg = 1 + (ULONGLONG *) vararg;
                continue;   // don't advance vararg
                break;
            default:
                *buffer++ = c;
                continue;   // don't advance vararg
            }
            vararg = 1 + (int *) vararg;
        }
    } while (*format++);
}

void my_printf(BOOL IsError, const char * format, ...)
{
    char buffer[1024];
    my_vsprintf(buffer, format, 1 + &format);
    my_puts(buffer, IsError);
}

// only %x and %d formats are supported
void my_sprintf(char * buffer, const char * format, ...)
{
    my_vsprintf(buffer, format, 1 + &format);
}

void StoreResultAndReboot(int result)
{
    //test result is stored in the RTC alarm registers
    // result TEST_RESULT_SUCCESS: success
    // TEST_RESULT_CTRL_ALT_DEL: test was terminated because of Ctrl-Alt-Del
    // TEST_RESULT_MEMORY_ERROR: memory error detected
    // TEST_RESULT_CRASH: test seemed to crash or computer was reset
    WriteRTCReg(RTC_SECOND_ALARM, result);
    Reboot();
}

void __stdcall MemoryError(void * addr,
                           DWORD data_high, DWORD data_low, // read QWORD
                           DWORD ref_high, DWORD ref_low)  // reference QWORD
{
    // message format:
    // address = actual_data ref_data
    char buffer[80];
    ASSERT(sizeof "\nAddr: 12345678: written: 0123456789ABCDEF, read: 0123456789ABCDEF\n"
           < 79);
    if (RebootOnFinish)
    {
        StoreResultAndReboot(TEST_RESULT_MEMORY_ERROR);
    }

    my_sprintf(buffer, "\nAddr: %x: written: %x%x, read: %x%x\n",
               GetPhysAddr(addr), ref_high, ref_low, data_high, data_low);
    my_puts(buffer, TRUE, 0x0f00); // TRUE - it is error message
}

size_t CalculatePageTableSize(size_t LastVirtualAddress)
{
    // LastVirtualAddress is area size-1. For example, for 4GB, it is 0xFFFFFFFF
    size_t TableSize = PAGE_SIZE; // page directory pointer table
    while (LastVirtualAddress >= (ULONG)MEMORY_PER_DIRECTORY_ENTRY)
    {
        TableSize += PAGE_SIZE + PAGE_DESCRIPTORS_PER_PAGE * PAGE_SIZE;
        LastVirtualAddress -= (ULONG)MEMORY_PER_DIRECTORY_ENTRY;
    }
    TableSize += PAGE_SIZE + ((LastVirtualAddress + (LARGE_PAGE_SIZE-1)) / LARGE_PAGE_SIZE) * PAGE_SIZE;    // 1 page per 2MB
    return TableSize;
}

DWORD __stdcall WritePseudoRandom(void * addr,
                                  size_t _size, DWORD seed, DWORD poly)
{
    __asm {
        mov     esi,addr
        mov     ecx,_size
        mov     edi,seed
        mov     ebx,poly
        shr     ecx,5
loop1:
        shr     edi,1
        sbb     eax,eax
        mov     [esi],eax
        mov     [esi+4],eax
        and     eax,ebx
        xor     edi,eax
        shr     edi,1
        sbb     eax,eax
        mov     [esi+8],eax
        mov     [esi+4+8],eax
        and     eax,ebx
        xor     edi,eax
        shr     edi,1
        sbb     eax,eax
        mov     [esi+16],eax
        mov     [esi+4+16],eax
        and     eax,ebx
        xor     edi,eax
        shr     edi,1
        sbb     eax,eax
        mov     [esi+24],eax
        mov     [esi+4+24],eax
        and     eax,ebx
        xor     edi,eax
        add     esi,32
        dec     ecx
        jg      loop1

        mov     seed,edi     // return last seed value
    }
    return seed;
}

// preload data to the cache
void __stdcall PreloadCache(void * addr, size_t _size)
{
    __asm {
        mov     ecx,_size
        shr     ecx,5
        mov     edx,addr
loop1:
        mov     eax,[edx]
        add     edx,32
        dec     ecx
        jg      loop1
    }
}

// The function makes a lot of real memory accesses
// Since it makes consequtive accesses that do not
// fall in one memory row, all accesses lead to full memory cycle
// the memory must not be cached
MemoryOperationFunction PreheatMemory;

DWORD PreheatMemory(void * addr, size_t memsize, DWORD step, DWORD, DWORD, DWORD, DWORD)
{
    __asm {
        mov     eax,[memsize]
        sub     edx,edx
        mov     ebx,[step]
        div     ebx  // number of memory accesses in loop
        mov     [memsize],eax
        shr     ebx,2    // number of big loops
loop2:
        mov     ecx,[memsize]
        mov     edx,[addr]
loop1:
        mov     eax,[edx]
        add     edx,[step]
        dec     ecx
        jg      loop1
        add     [addr],4
        dec     ebx
        jg      loop2
    }
    return 0;
}

void DoPreheatMemory(void * addr, size_t _size, size_t step,
                     DWORD flags)
{
    if (0 == (flags & TEST_FLAGS_PREHEAT_MEMORY))
    {
        return;
    }

    __wbinvd();

    RunMemoryForward(PreheatMemory, addr, _size, step, 0, 0, 0, flags, PAGE_DIR_FLAG_NOCACHE);
}

// preload data to the cache, jumping from one to another
// memory matrix row. Row step is half of 'size' argument
void __stdcall ReadMemorySeeSaw(void * addr, size_t _size, size_t step)
{
    __asm {
        mov     ecx,_size
        mov     ebx,step
        sub     ecx,ebx
        shr     ecx,5
        mov     edi,addr
loop1:
        mov     eax,[edi]
        mov     edx,[edi+ebx]
        add     edi,32
        dec     ecx
        jg      loop1
    }
}

void __stdcall
        FillMemoryPatternWriteAlloc(void * addr, size_t _size,
                                    DWORD pattern)
{
    // size is 32 bytes multiply
    __asm {
        mov     ecx,_size
        shr     ecx,5
        mov     edi,addr
        mov     eax,pattern
loop1:
        mov     edx,[edi]   // read the cache line first
        mov     ebx,[edi+4]
        mov     [edi],eax
        mov     [edi+4],eax
        mov     [edi+8],eax
        mov     [edi+4+8],eax
        mov     [edi+16],eax
        mov     [edi+4+16],eax
        mov     [edi+24],eax
        mov     [edi+4+24],eax
        add     edi,32
        dec     ecx
        jg      loop1
    }
}

// fill memory area with 2 different QWORDs.
// each QWORD is composed of two equal DWORDs.
// is used to write running 1/running 0
void __stdcall
        FillMemoryPattern(void * addr, size_t _size,
                          unsigned __int32 data0,
                          unsigned __int32 data1)
{
    // data is stored in the following order:
    // data0, data0, data1, data1, data0, data0, data1, data1
    // size is 32 bytes multiply
    __asm {
        mov     ecx,_size
        shr     ecx,5
        mov     esi,addr
        mov     eax,data0
        mov     edx,data1
loop1:
        mov     [esi],eax
        mov     [esi+4],eax
        mov     [esi+8],edx
        mov     [esi+4+8],edx
        mov     [esi+16],eax
        mov     [esi+4+16],eax
        mov     [esi+24],edx
        mov     [esi+4+24],edx
        add     esi,32
        dec     ecx
        jg      loop1
    }
}

// compare memory area with the specified QWORD
void __stdcall CompareMemory(void * addr, size_t _size,
                             unsigned __int32 pattern)
{
    __asm {
        mov     ecx,_size
        shr     ecx,5
        mov     esi,addr
        mov     eax,pattern
loop1:
        mov     ebx,[esi]
        mov     edx,[esi+4]
        cmp     ebx,eax
        jnz     error0
        cmp     edx,eax
        jnz     error0
continue0:
        mov     ebx,[esi+8]
        mov     edx,[esi+4+8]
        cmp     ebx,eax
        jnz     error8
        cmp     edx,eax
        jnz     error8
continue8:
        mov     ebx,[esi+16]
        mov     edx,[esi+4+16]
        cmp     ebx,eax
        jnz     error16
        cmp     edx,eax
        jnz     error16
continue16:
        mov     ebx,[esi+24]
        mov     edx,[esi+4+24]
        cmp     ebx,eax
        jnz     error24
        cmp     edx,eax
        jnz     error24
continue24:
        add     esi,32
        dec     ecx
        jnz     loop1
    }
    return;

    __asm {
error0:
        push    eax
        push    edx
        push    ecx
        push    eax
        push    eax
        push    ebx
        push    edx
        push    esi
        call    MemoryError
        pop     ecx
        pop     edx
        pop     eax
        jmp     continue0
error8:
        push    eax
        push    edx
        push    ecx
        push    eax
        push    eax
        push    ebx
        push    edx
        lea     eax,[esi+8]
        push    eax
        call    MemoryError
        pop     ecx
        pop     edx
        pop     eax
        jmp     continue8
error16:
        push    eax
        push    edx
        push    ecx
        push    eax
        push    eax
        push    ebx
        push    edx
        lea     eax,[esi+16]
        push    eax
        call    MemoryError
        pop     ecx
        pop     edx
        pop     eax
        jmp     continue16
error24:
        push    eax
        push    edx
        push    ecx
        push    eax
        push    eax
        push    ebx
        push    edx
        lea     eax,[esi+24]
        push    eax
        call    MemoryError
        pop     ecx
        pop     edx
        pop     eax
        jmp     continue24
    }
}


// compare memory area with the specified QWORD backward
void __stdcall CompareMemoryBackward(void * addr, size_t _size,
                                     unsigned __int32 pattern)
{
    __asm {
        mov     ecx,_size
        mov     esi,addr
        lea     esi,[esi+ecx-32]
        shr     ecx,5
        mov     eax,pattern
loop1:
        mov     ebx,[esi]
        mov     edx,[esi+4]
        cmp     ebx,eax
        jnz     error0
        cmp     edx,eax
        jnz     error0
continue0:
        mov     ebx,[esi+8]
        mov     edx,[esi+4+8]
        cmp     ebx,eax
        jnz     error8
        cmp     edx,eax
        jnz     error8
continue8:
        mov     ebx,[esi+16]
        mov     edx,[esi+4+16]
        cmp     ebx,eax
        jnz     error16
        cmp     edx,eax
        jnz     error16
continue16:
        mov     ebx,[esi+24]
        mov     edx,[esi+4+24]
        cmp     ebx,eax
        jnz     error24
        cmp     edx,eax
        jnz     error24
continue24:
        sub     esi,32
        dec     ecx
        jnz     loop1
    }
    return;

    __asm {
error0:
        push    eax
        push    edx
        push    ecx
        push    eax
        push    eax
        push    ebx
        push    edx
        push    esi
        call    MemoryError
        pop     ecx
        pop     edx
        pop     eax
        jmp     continue0
error8:
        push    eax
        push    edx
        push    ecx
        push    eax
        push    eax
        push    ebx
        push    edx
        lea     eax,[esi+8]
        push    eax
        call    MemoryError
        pop     ecx
        pop     edx
        pop     eax
        jmp     continue8
error16:
        push    eax
        push    edx
        push    ecx
        push    eax
        push    eax
        push    ebx
        push    edx
        lea     eax,[esi+16]
        push    eax
        call    MemoryError
        pop     ecx
        pop     edx
        pop     eax
        jmp     continue16
error24:
        push    eax
        push    edx
        push    ecx
        push    eax
        push    eax
        push    ebx
        push    edx
        lea     eax,[esi+24]
        push    eax
        call    MemoryError
        pop     ecx
        pop     edx
        pop     eax
        jmp     continue24
    }
}


// compare memory area with the specified QWORD pattern
void  __stdcall
        CompareMemoryPattern(void * addr, size_t _size,
                             unsigned long data0,
                             unsigned long data1)
{
    __asm {
        mov     ecx,_size
        shr     ecx,5
        mov     esi,addr
        mov     eax,data0
        mov     edx,data1
loop1:
        cmp     eax,[esi]
        jnz     error0
        cmp     eax,[esi+4]
        jnz     error0
continue0:
        cmp     edx,[esi+8]
        jnz     error8
        cmp     edx,[esi+4+8]
        jnz     error8
continue8:
        cmp     eax,[esi+16]
        jnz     error16
        cmp     eax,[esi+4+16]
        jnz     error16
continue16:
        cmp     edx,[esi+24]
        jnz     error24
        cmp     edx,[esi+4+24]
        jnz     error24
continue24:
        add     esi,32
        dec     ecx
        jnz     loop1
    }
    return;
    __asm {

error0:
        push    eax
        push    edx
        push    ecx
        push    eax // reference
        push    eax
        mov     eax,[esi]  // read data
        push    eax
        mov     eax,[esi+4]
        push    eax
        push    esi
        call    MemoryError
        pop     ecx
        pop     edx
        pop     eax
        jmp     continue0
error8:
        push    eax
        push    edx
        push    ecx
        push    edx // reference
        push    edx
        mov     eax,[esi+8]  // read data
        push    eax
        mov     eax,[esi+8+4]
        push    eax
        lea     eax,[esi+8]
        push    eax
        call    MemoryError
        pop     ecx
        pop     edx
        pop     eax
        jmp     continue8
error16:
        push    eax
        push    edx
        push    ecx
        push    eax // reference
        push    eax
        mov     eax,[esi+16]  // read data
        push    eax
        mov     eax,[esi+16+4]
        push    eax
        lea     eax,[esi+16]
        push    eax
        call    MemoryError
        pop     ecx
        pop     edx
        pop     eax
        jmp     continue16
error24:
        push    eax
        push    edx
        push    ecx
        push    edx // reference
        push    edx
        mov     eax,[esi+24]  // read data
        push    eax
        mov     eax,[esi+24+4]
        push    eax
        lea     eax,[esi+24]
        push    eax
        call    MemoryError
        pop     ecx
        pop     edx
        pop     eax
        jmp     continue24
    }
}

// compare memory area with the specified QWORD pattern
// from high to low addresses
void __stdcall
        CompareMemoryPatternBackward(void * addr, size_t _size,
                                     unsigned long data0,
                                     unsigned long data1)
{
    __asm {
        mov     ecx,_size
        mov     esi,addr
        add     esi,ecx
        shr     ecx,5
        mov     eax,data0
        mov     edx,data1
loop1:
        sub     esi,32
        cmp     eax,[esi]
        jnz     error0
        cmp     eax,[esi+4]
        jnz     error0
continue0:
        cmp     edx,[esi+8]
        jnz     error8
        cmp     edx,[esi+4+8]
        jnz     error8
continue8:
        cmp     eax,[esi+16]
        jnz     error16
        cmp     eax,[esi+4+16]
        jnz     error16
continue16:
        cmp     edx,[esi+24]
        jnz     error24
        cmp     edx,[esi+4+24]
        jnz     error24
continue24:
        dec     ecx
        jnz     loop1
    }

    return;

    __asm {

error0:
        push    eax
        push    edx
        push    ecx
        push    eax // reference
        push    eax
        push    [esi]  // read data
        push    [esi+4]
        push    esi
        call    MemoryError
        pop     ecx
        pop     edx
        pop     eax
        jmp     continue0
error8:
        push    eax
        push    edx
        push    ecx
        push    edx // reference
        push    edx
        push    [esi+8]   // read data
        push    [esi+8+4]
        lea     eax,[esi+8]
        push    eax
        call    MemoryError
        pop     ecx
        pop     edx
        pop     eax
        jmp     continue8
error16:
        push    eax
        push    edx
        push    ecx
        push    eax // reference
        push    eax
        push    [esi+16]  // read data
        push    [esi+16+4]
        lea     eax,[esi+16]
        push    eax
        call    MemoryError
        pop     ecx
        pop     edx
        pop     eax
        jmp     continue16
error24:
        push    eax
        push    edx
        push    ecx
        push    edx // reference
        push    edx
        push    [esi+24]  // read data
        push    [esi+24+4]
        lea     eax,[esi+24]
        push    eax
        call    MemoryError
        pop     ecx
        pop     edx
        pop     eax
        jmp     continue24
    }
}

// compare memory area with the specified QWORD pattern
// compared area is rewritten with another pattern
void  __stdcall
        CompareMemoryPatternAndReplace(void * addr, size_t _size,
                                       unsigned long data0,
                                       unsigned long data1,
                                       unsigned long new_data0,
                                       unsigned long new_data1)
{
    __asm {
        mov     ecx,_size
        shr     ecx,5
        mov     esi,addr
        mov     eax,data0
        mov     edx,data1
        mov     ebx,new_data0
        mov     edi,new_data1
loop1:
        cmp     eax,[esi]
        jnz     error0
        cmp     eax,[esi+4]
        jnz     error0
continue0:
        mov     [esi],ebx
        mov     [esi+4],ebx
        cmp     edx,[esi+8]
        jnz     error8
        cmp     edx,[esi+4+8]
        jnz     error8
continue8:
        mov     [esi+8],edi
        mov     [esi+4+8],edi
        cmp     eax,[esi+16]
        jnz     error16
        cmp     eax,[esi+4+16]
        jnz     error16
continue16:
        mov     [esi+16],ebx
        mov     [esi+4+16],ebx
        cmp     edx,[esi+24]
        jnz     error24
        cmp     edx,[esi+4+24]
        jnz     error24
continue24:
        mov     [esi+24],edi
        mov     [esi+4+24],edi
        add     esi,32
        dec     ecx
        jnz     loop1
    }
    return;

    __asm {

error0:
        push    eax
        push    edx
        push    ecx
        push    eax // reference
        push    eax
        mov     eax,[esi]  // read data
        push    eax
        mov     eax,[esi+4]
        push    eax
        push    esi
        call    MemoryError
        pop     ecx
        pop     edx
        pop     eax
        jmp     continue0
error8:
        push    eax
        push    edx
        push    ecx
        push    edx // reference
        push    edx
        mov     eax,[esi+8]  // read data
        push    eax
        mov     eax,[esi+8+4]
        push    eax
        lea     eax,[esi+8]
        push    eax
        call    MemoryError
        pop     ecx
        pop     edx
        pop     eax
        jmp     continue8
error16:
        push    eax
        push    edx
        push    ecx
        push    eax // reference
        push    eax
        mov     eax,[esi+16]  // read data
        push    eax
        mov     eax,[esi+16+4]
        push    eax
        lea     eax,[esi+16]
        push    eax
        call    MemoryError
        pop     ecx
        pop     edx
        pop     eax
        jmp     continue16
error24:
        push    eax
        push    edx
        push    ecx
        push    edx // reference
        push    edx
        mov     eax,[esi+24]  // read data
        push    eax
        mov     eax,[esi+24+4]
        push    eax
        lea     eax,[esi+24]
        push    eax
        call    MemoryError
        pop     ecx
        pop     edx
        pop     eax
        jmp     continue24
    }
}

// compare memory area with the specified QWORD pattern
// from high to low addresses
void __stdcall
        CompareMemoryPatternBackwardAndReplace(void * addr, size_t _size,
                                               unsigned long data0,
                                               unsigned long data1,
                                               unsigned long new_data0,
                                               unsigned long new_data1)
{
    __asm {
        mov     esi,addr
        mov     edi,_size
        add     esi,edi    // calculate high address
        shr     edi,5
loop1:
        mov     eax,data1
        mov     ebx,new_data1
        sub     esi,32
        mov     edx,eax
        mov     ecx,ebx
        lock cmpxchg8b [esi+24]
        jnz     error24
continue24:
        mov     eax,data0
        mov     ebx,new_data0
        mov     edx,eax
        mov     ecx,ebx
        cmpxchg8b [esi+16]
        jnz     error16
continue16:
        mov     eax,data1
        mov     ebx,new_data1
        mov     edx,eax
        mov     ecx,ebx
        cmpxchg8b [esi+8]
        jnz     error8
continue8:
        mov     eax,data0
        mov     ebx,new_data0
        mov     edx,eax
        mov     ecx,ebx
        lock cmpxchg8b [esi]
        jnz     error0
continue0:
        dec     edi
        jnz     loop1
    }

    return;

    _asm {

error0:
        mov     [esi],ecx
        mov     [esi+4],ebx
        mov     ecx,data0
        push    ecx // reference
        push    ecx
        push    edx // read data
        push    eax
        push    esi
        call    MemoryError
        jmp     continue0
error8:
        mov     [esi+8],ecx
        mov     [esi+8+4],ebx
        mov     ecx,data1
        push    ecx // reference
        push    ecx
        push    edx // read data
        push    eax
        lea     eax,[esi+8]
        push    eax
        call    MemoryError
        jmp     continue8
error16:
        mov     [esi+16],ecx
        mov     [esi+4+16],ebx
        mov     ecx,data0
        push    ecx // reference
        push    ecx
        push    edx // read data
        push    eax
        lea     eax,[esi+16]
        push    eax
        call    MemoryError
        jmp     continue16
error24:
        mov     [esi+24],ecx
        mov     [esi+4+24],ebx
        mov     ecx,data1
        push    ecx // reference
        push    ecx
        push    edx // read data
        push    eax
        lea     eax,[esi+24]
        push    eax
        call    MemoryError
        jmp     continue24
    }
}

DWORD __stdcall ComparePseudoRandom(void * addr,
                                    size_t _size, DWORD seed, DWORD poly)
{
    __asm {
        mov     esi,addr
        mov     ecx,_size
        mov     edi,seed
        mov     ebx,poly
        shr     ecx,5
loop1:
        shr     edi,1
        sbb     eax,eax
        cmp     eax,[esi]
        jnz     error0
        cmp     eax,[esi+4]
        jnz     error0
continue0:
        and     eax,ebx
        xor     edi,eax
        shr     edi,1
        sbb     eax,eax
        cmp     eax,[esi+8]
        jnz     error8
        cmp     eax,[esi+4+8]
        jnz     error8
continue8:
        and     eax,ebx
        xor     edi,eax
        shr     edi,1
        sbb     eax,eax
        cmp     eax,[esi+16]
        jnz     error16
        cmp     eax,[esi+4+16]
        jnz     error16
continue16:
        and     eax,ebx
        xor     edi,eax
        shr     edi,1
        sbb     eax,eax
        cmp     eax,[esi+24]
        jnz     error24
        cmp     eax,[esi+4+24]
        jnz     error24
continue24:
        and     eax,ebx
        xor     edi,eax
        add     esi,32
        dec     ecx
        jg      loop1

        mov     seed,edi     // return last seed value
    }
    return seed;

    __asm {
error0:
        push    eax
        push    edx
        push    ecx
        push    eax // reference
        push    eax
        mov     eax,[esi]  // read data
        push    eax
        mov     eax,[esi+4]
        push    eax
        push    esi
        call    MemoryError
        pop     ecx
        pop     edx
        pop     eax
        jmp     continue0
error8:
        push    eax
        push    edx
        push    ecx
        push    eax // reference
        push    eax
        mov     eax,[esi+8]  // read data
        push    eax
        mov     eax,[esi+8+4]
        push    eax
        lea     eax,[esi+8]
        push    eax
        call    MemoryError
        pop     ecx
        pop     edx
        pop     eax
        jmp     continue8
error16:
        push    eax
        push    edx
        push    ecx
        push    eax // reference
        push    eax
        mov     eax,[esi+16]  // read data
        push    eax
        mov     eax,[esi+16+4]
        push    eax
        lea     eax,[esi+16]
        push    eax
        call    MemoryError
        pop     ecx
        pop     edx
        pop     eax
        jmp     continue16
error24:
        push    eax
        push    edx
        push    ecx
        push    eax // reference
        push    eax
        mov     eax,[esi+24]  // read data
        push    eax
        mov     eax,[esi+24+4]
        push    eax
        lea     eax,[esi+24]
        push    eax
        call    MemoryError
        pop     ecx
        pop     edx
        pop     eax
        jmp     continue24
    }
}

MemoryOperationFunction WriteTestData;
DWORD WriteTestData(void * addr, size_t size,
                   DWORD pattern1, DWORD pattern2, DWORD, DWORD,
                   DWORD flags)
{
    while (size != 0)
    {
        size_t test_size = size;
        if(flags & TEST_PRELOAD_CACHE2)
        {
            if (test_size > cache2_preload_size)
                test_size = cache2_preload_size;
            PreloadCache(addr, test_size);
        }

        size -= test_size;
        while(test_size != 0)
        {
            size_t row_size = MemoryRowSize;
            if (row_size > test_size)
            {
                row_size = test_size;
            }
            test_size -= row_size;
            while(row_size != 0)
            {
                size_t curr_size = row_size;
                if(flags & TEST_PRELOAD_CACHE1)
                {
                    if (curr_size > cache1_preload_size)
                        curr_size = cache1_preload_size;
                    PreloadCache(addr, curr_size);
                }

                FillMemoryPattern(addr, curr_size, pattern1, pattern2);

                addr = curr_size + (char*)addr;
                row_size -= curr_size;
            }
            DWORD tmp = pattern1;
            pattern1 = pattern2;
            pattern2 = tmp;
        }
    }
    return 0;
}

MemoryOperationFunction CompareTestData;
DWORD CompareTestData(void * addr, size_t size,
                     DWORD pattern1, DWORD pattern2,
                     DWORD new_pattern1, DWORD new_pattern2,
                     DWORD flags)
{
    if(flags & TEST_SEESAW)
    {
        ReadMemorySeeSaw(addr, size, MemoryRowSize);
        if (flags & TEST_READ_TWICE)
        {
            ReadMemorySeeSaw(addr, size, MemoryRowSize);
        }
    }

    while (size > 0)
    {
        size_t test_size = size;
        if(flags & TEST_PRELOAD_CACHE2)
        {
            if (test_size > cache2_preload_size)
                test_size = cache2_preload_size;
            PreloadCache(addr, test_size);
        }

        //size_t row_size = 0;
        // MemoryRowSize <= cache2_preload_size
        ASSERT(MemoryRowSize <= cache2_preload_size);

        size -= test_size;
        while(test_size != 0)
        {
            size_t row_size = MemoryRowSize;
            if (row_size > test_size)
            {
                row_size = test_size;
            }
            if (flags & (TEST_ALL0 | TEST_ALL1))
            {
                ASSERT(0 == (flags & TEST_REPLACE));

                CompareMemory(addr, row_size, pattern1);
            }
            else
            {
                if (flags & TEST_REPLACE)
                {
                    CompareMemoryPatternAndReplace(addr, row_size, pattern1, pattern2,
                                                   new_pattern1, new_pattern2);
                }
                else
                {
                    CompareMemoryPattern(addr, row_size, pattern1, pattern2);
                }
            }

            addr = row_size + (char*)addr;
            test_size -= row_size;

            DWORD tmp = pattern1;
            pattern1 = pattern2;
            pattern2 = tmp;

            tmp = new_pattern1;
            new_pattern1 = new_pattern2;
            new_pattern2 = tmp;
        }
    }
    return 0;
}

MemoryOperationFunction CompareTestDataBackward;
DWORD CompareTestDataBackward(void * addr, size_t size,
                             DWORD pattern1, DWORD pattern2,
                             DWORD new_pattern1, DWORD new_pattern2,
                             DWORD flags)
{
    if(flags & TEST_SEESAW)
    {
        ReadMemorySeeSaw(addr, size, MemoryRowSize);
        __wbinvd();
    }

    ASSERT (size % MemoryRowSize == 0);

    if (0 == ((size / MemoryRowSize) & 1))
    {
        DWORD tmp = pattern1;
        pattern1 = pattern2;
        pattern2 = tmp;

        tmp = new_pattern1;
        new_pattern1 = new_pattern2;
        new_pattern2 = tmp;
    }

    addr = size + (char*)addr;
    while (size != 0)
    {
        size_t test_size = size;
        if(flags & TEST_PRELOAD_CACHE2)
        {
            if (test_size > cache2_preload_size)
                test_size = cache2_preload_size;
            PreloadCache((char*)addr - test_size, test_size);
        }

        ASSERT (MemoryRowSize <= cache2_preload_size);

        size -= test_size;
        while(test_size != 0)
        {
            size_t row_size = MemoryRowSize;
            if (row_size > test_size)
            {
                row_size = test_size;
            }

            addr = (char*)addr - row_size;
            if (flags & (TEST_ALL0 | TEST_ALL1))
            {
                ASSERT(0 == (flags & TEST_REPLACE));

                CompareMemoryBackward(addr, row_size, pattern1);
            }
            else
            {
                if (flags & TEST_REPLACE)
                {
                    CompareMemoryPatternBackwardAndReplace(addr, row_size, pattern1, pattern2,
                                                           new_pattern1, new_pattern2);
                }
                else
                {
                    CompareMemoryPatternBackward(addr, row_size, pattern1, pattern2);
                }
            }

            test_size -= row_size;
            DWORD tmp = pattern1;
            pattern1 = pattern2;
            pattern2 = tmp;

            tmp = new_pattern1;
            new_pattern1 = new_pattern2;
            new_pattern2 = tmp;
        }
    }
    return 0;
}

MemoryOperationFunction WriteRandomTestData;
DWORD WriteRandomTestData(void * addr, size_t size,
                          DWORD seed, DWORD polynom, DWORD, DWORD, DWORD flags)
{
    while (size > 0)
    {
        size_t test_size = size;
        if(flags & TEST_PRELOAD_CACHE1)
        {
            if (test_size > cache1_preload_size)
                test_size = cache1_preload_size;
            PreloadCache(addr, test_size);
        }
        else if(flags & TEST_PRELOAD_CACHE2)
        {
            if (test_size > cache2_preload_size)
                test_size = cache2_preload_size;
            PreloadCache(addr, test_size);
        }

        seed = WritePseudoRandom(addr, test_size, seed, polynom);

        addr = test_size + (char*)addr;
        size -= test_size;
    }
    return seed;
}

MemoryOperationFunction CompareRandomTestData;

DWORD CompareRandomTestData(void * addr, size_t size,
                            DWORD seed, DWORD polynom, DWORD, DWORD, DWORD flags)
{
    if(flags & TEST_SEESAW)
    {
        ReadMemorySeeSaw(addr, size, MemoryRowSize);
    }

    while (size > 0)
    {
        size_t test_size = size;
        if(flags & TEST_PRELOAD_CACHE2)
        {
            if (test_size > cache2_preload_size)
                test_size = cache2_preload_size;
            PreloadCache(addr, test_size);
        }

        seed = ComparePseudoRandom(addr, test_size, seed, polynom);
        addr = test_size + (char*)addr;
        size -= test_size;
    }

    return seed;
}

void Delay10ms()
{
    int num1, num2;
    _outp(0x43, 4);
    num2 = 0xFF & _inp(0x40);
    num2 |= (0xFF & _inp(0x40)) << 8;

    do {
        num1 = num2;
        _outp(0x43, 4);
        num2 = 0xFF & _inp(0x40);
        num2 |= (0xFF & _inp(0x40)) << 8;
    } while (num2 <= num1);
}

void Delay(int nDelay)
{
    while(nDelay > 0)
    {
        Delay10ms();
        nDelay -= 10;
        CheckForKey();
    }
}

//__declspec(naked)
void Reboot()
{
    // try to reset through port 92
    _outp(0x92, _inp(0x92) | 1);
    // then reset the processor using the keyboard controler
    while (_inp(0x64) & 2);
    _outp(0x64, 0xFE);  // pulse RESET line
//    while(1) __asm hlt;
    // if it does not reset, try enter to shutdown mode
    RESET_IDT();        // reset IDT to a single descriptor
    __asm INT   0xFF    // causes processor shutdown and reset
    while(1) __asm hlt;
}

int CheckForKey()
{
    static int alt = 0;
    static int ctrl = 0;
    static int off = 0;

    while(_inp(0x64) & 1)
    {
        int key = 0xFF & _inp(0x60);
        switch(key)
        {
        case 0x1D:  // ctrl
            if (off)
            {
                ctrl = 0;
            }
            else
            {
                ctrl = 1;
            }
            break;
        case 0x80|0x1D:  // ctrl off
            ctrl = 0;
            break;

        case 0x38:  // Alt
            if (off)
            {
                alt = 0;
            }
            else
            {
                alt = 1;
            }
            break;
        case 0x80|0x38:  // Alt off
            alt = 0;
            break;

        case 0x53:  // del
            if (off)
            {
                break;
            }
            if (ctrl && alt)
            {
                if (RebootOnFinish)
                {
                    StoreResultAndReboot(TEST_RESULT_CTRL_ALT_DEL);
                }
                else
                {
                    Reboot();
                }
            }
            break;

        case 0xE0:
            continue;
            break;
        case 0x2A:
        case 0xAA:
            break;

        case 0xF0:
            off = 1;
            continue;
            break;
        default:
            if ((key & 0x80) == 0 && ! off)
            {
                return key;
            }
            off = 0;
            break;
        }
        off = 0;
    }
    return  0;
}

void TestThread::DoMemoryTestPattern(char * addr, size_t _size,
                         DWORD const InitPattern1, DWORD const InitPattern2,
                         DWORD flags, DWORD MemoryFlags)
{
    if (addr == NULL || _size == 0)
    {
        return;
    }
    DWORD Pattern1 = InitPattern1;
    DWORD Pattern2 = InitPattern2;

    for (int loop = 0; loop < 32; loop++)
    {
        DoPreheatMemory(addr, _size, 0x10000, flags);
        CheckForKey();

        if(flags & TEST_EMPTY_CACHE)
        {
            __wbinvd();
        }

        // write test data to memory area
        // print current test
        my_printf(FALSE, "\r                                      "
                  "                                      \rPass %d,"
                  "Pattern: %X%X %X%X", m_TestPass,
                  Pattern1, Pattern1,
                  Pattern2, Pattern2);

        // write the pattern first time,
        // if two compare passes, compare it upward,
        // then compare and rewrite with inverse pattern upward
        // then if two compare passes, compare it downward
        // then compare and rewrite with next pattern downward
        //
        if (0 == loop)
        {
            RunMemoryForward(WriteTestData, addr, _size, Pattern1, Pattern2, 0, 0,
                             flags, MemoryFlags);
        }

#if defined(MAKEERROR) && defined (_DEBUG)
        // insert random error
        {
            DWORD _offset = 0;
            __asm {
                call    rand
                MUL     _size
                mov     ecx,RAND_MAX
                DIV     ecx
                mov     _offset,eax
            }
            char * erraddr = addr + _offset;
            unsigned char err = char(rand());
            if (erraddr >= addr && erraddr < addr + _size)
            {
                *erraddr ^= err;
                my_printf(TRUE, "\nError introduced at %x, data = %x\r",
                          GetPhysAddr(erraddr), err);
            }
        }
#endif
        CheckForKey();

        if(flags & TEST_EMPTY_CACHE)
        {
            __wbinvd();
        }

        // check test data
        if (flags & TEST_DELAY)
        {
            // delay idle
            Delay(m_TestDelay);
            CheckForKey();
        }

        if (flags & TEST_READ_TWICE)
        {
            RunMemoryForward(CompareTestData, addr, _size, Pattern1, Pattern2, 0, 0, flags, MemoryFlags);
        }

        RunMemoryForward(CompareTestData, addr, _size, Pattern1, Pattern2, ~Pattern1, ~Pattern2,
                         flags | TEST_REPLACE, MemoryFlags);

        CheckForKey();

        if(flags & TEST_EMPTY_CACHE)
        {
            __wbinvd();
        }

#if defined(MAKEERROR) && defined (_DEBUG)
        // insert random error
        {
            DWORD _offset = 0;
            __asm {
                call    rand
                MUL     _size
                mov     ecx,RAND_MAX
                DIV     ecx
                mov     _offset,eax
            }
            char * erraddr = addr + _offset;
            unsigned char err = char(rand());
            if (erraddr >= addr && erraddr < addr + _size)
            {
                *erraddr ^= err;
                my_printf(TRUE, "\nError introduced at %x, data = %x\r",
                          GetPhysAddr(erraddr), err);
            }
        }
#endif

        if (flags & TEST_DELAY)
        {
            // delay idle
            Delay(m_TestDelay);
            CheckForKey();
        }

        // check test data
        if (flags & TEST_READ_TWICE)
        {
            RunMemoryBackward(CompareTestDataBackward, addr, _size, ~Pattern1, ~Pattern2, 0, 0, flags, MemoryFlags);
            CheckForKey();
        }

        RunMemoryBackward(CompareTestDataBackward, addr, _size, ~Pattern1, ~Pattern2,
                          _rotr(Pattern1, 1), _rotr(Pattern2, 1),
                          TEST_REPLACE | flags, MemoryFlags);

        Pattern1 = _rotr(Pattern1, 1);
        Pattern2 = _rotr(Pattern2, 1);

        // the pattern is rotated until it matches the initial
        if (InitPattern1 == Pattern1
            && InitPattern2 == Pattern2)
        {
            break;
        }
        RelocateProgram();
    }
}

DWORD TestThread::DoRandomMemoryTest(char * addr, size_t _size, DWORD seed,
                                     DWORD polynom, DWORD flags, DWORD MemoryFlags)
{
    DWORD NextSeed = seed;
    // write test data to memory area
    // print current test
    if (addr == NULL || _size == 0)
    {
        return seed;
    }

    my_printf(FALSE, "\r                                      "
              "                                      \rPass %d, "
              "Testing random pattern...", m_TestPass);

    DoPreheatMemory(addr, _size, 0x10000, flags);

    CheckForKey();
    if(flags & TEST_EMPTY_CACHE)
    {
        __wbinvd();
    }

    DWORD new_seed = RunMemoryForward(WriteRandomTestData, addr, _size, seed, polynom, 0, 0,
                                      flags, MemoryFlags);

#if defined(MAKEERROR) && defined (_DEBUG)
    // insert random error
    {
        DWORD _offset = 0;
        __asm {
            call    rand
            MUL     _size
            mov     ecx,RAND_MAX
            DIV     ecx
            mov     _offset,eax
        }
        char * erraddr = addr + _offset;
        unsigned char err = char(rand());
        if (erraddr >= addr && erraddr < addr + _size)
        {
            *erraddr ^= err;
            my_printf(TRUE, "\nError introduced at %p, data = %x\r",
                      GetPhysAddr(erraddr), err);
        }
    }
#endif
    // check test data
    CheckForKey();

    if(flags & TEST_EMPTY_CACHE)
    {
        __wbinvd();
    }

    if (flags & TEST_DELAY)
    {
        // delay idle
        Delay(m_TestDelay);
    }

    CheckForKey();

    RunMemoryForward(CompareRandomTestData, addr, _size, seed, polynom, 0, 0, flags, MemoryFlags);
    if (flags & TEST_READ_TWICE)
    {
        RunMemoryForward(CompareRandomTestData, addr, _size, seed, polynom, 0, 0, flags, MemoryFlags);
    }
    return new_seed;
}

#ifdef _DEBUG
void VerifyPageTable(PHYSICAL_ADDR pPhysStart, PHYSICAL_ADDR pPhysEnd,
                     void * pVirtStart, size_t TestSize,
                     PHYSICAL_ADDR TestCodePhysAddr, size_t TestCodeSize)
{
    // check that correct page table is used
    // check that the mapped memory does not include any memory outside the range
    // and doesn't include the program memory
    char * p = (char*)pVirtStart;
    void * pVirtEnd = p + TestSize;
    char s[128];
    PHYSICAL_ADDR PhysAddr;
    my_sprintf(s, "Verifying the page table... Va=%x, siz=%x, code=%p\r", pVirtStart,
               TestSize, TestCodePhysAddr);
    my_puts(s);
    if (pVirtEnd < pVirtStart)
    {
        my_puts("pVirtStart + TestSize < pVirtStart\n", TRUE);
        return;
    }
    PhysAddr = GetPhysAddr(VerifyPageTable);
    if (PhysAddr < TestCodePhysAddr
        || PhysAddr >= TestCodePhysAddr+TestCodeSize)
    {
        my_sprintf(s, "\nProgram PhysAddr(%x)=%p is out of range (%p-%p)\n", VerifyPageTable,
                   PhysAddr, TestCodePhysAddr, TestCodePhysAddr+TestCodeSize);
        my_puts(s, TRUE);
    }

    // check every page
    for ( ; p < pVirtEnd; p += PAGE_SIZE)
    {
        PhysAddr = GetPhysAddr(p);
        if (PhysAddr < pMemoryToTestStart)
        {
            my_sprintf(s, "\nPhysAddr(%x)=%x < pMemoryToTestStart\n", p, PhysAddr);
            my_puts(s, TRUE);
        }
        if (PhysAddr >= pMemoryToTestEnd)
        {
            my_sprintf(s, "\nPhysAddr(%x)=%x >= pMemoryToTestEnd\n", p, PhysAddr);
            my_puts(s, TRUE);
        }
        if (PhysAddr >= (PHYSICAL_ADDR)0xA0000 && PhysAddr < (PHYSICAL_ADDR)MEGABYTE)
        {
            my_sprintf(s, "\nPhysAddr(%x)=%p in A0000-100000 range\n", p, PhysAddr);
            my_puts(s, TRUE);
        }
        if (PhysAddr >= CurrentPhysProgramLocation
            && PhysAddr < PhysMemoryProgramUsedEnd)
        {
            my_sprintf(s, "\nPhysAddr(%x)=%p in program range(%p-%p)\n", p, PhysAddr,
                       CurrentPhysProgramLocation, PhysMemoryProgramUsedEnd);
            my_puts(s, TRUE);
        }
        // check that the page is mapped only once
        if (0)for (char * p1 = (char*)pVirtStart; p1 < pVirtEnd; p1+=PAGE_SIZE)
        {
            if (GetPhysAddr(p1) == PhysAddr
                && p1 != p)
            {
                my_sprintf(s, "\nPhysAddr %p mapped at %x and %x\n", PhysAddr, p, p1);
                my_puts(s, TRUE);
            }
        }
    }
}
#endif

DWORD RunMemoryForward(MemoryOperationFunction *function, void * addr, size_t size,
                       DWORD arg1, DWORD arg2,
                       DWORD arg3, DWORD arg4,
                       DWORD flags, DWORD MapFlags)
{
    // map memory piece by piece, and run the function over it
    // skip CurrentPhysProgramLocation MemoryInUseByProgram,
    //
    // From pMemoryToTestStart to pMemoryToTestEnd
    for(unsigned phys_range = 0; phys_range < TestParams.MemoryMapUsed; phys_range++)
    {
        ULARGE_INTEGER tmp;
        tmp.LowPart = TestParams.Map[phys_range].BaseAddressLow;
        tmp.HighPart = TestParams.Map[phys_range].BaseAddressHigh;

        PHYSICAL_ADDR MapStart = (PHYSICAL_ADDR)tmp.QuadPart;

        tmp.LowPart = TestParams.Map[phys_range].RangeSizeLow;
        tmp.HighPart = TestParams.Map[phys_range].RangeSizeHigh;

        PHYSICAL_ADDR MapEnd = MapStart + tmp.QuadPart;

        // skip incomplete pages
        MapStart = PHYSICAL_ADDR((ULONGLONG(MapStart) + 0xFFF) & ~0xFFFULL);
        MapEnd = PHYSICAL_ADDR(ULONGLONG(MapEnd) & ~0xFFFULL);

        if (MapEnd <= pMemoryToTestStart
            || MapStart >= pMemoryToTestEnd)
        {
            continue;
        }

        if (MapEnd > pMemoryToTestEnd)
        {
            MapEnd = pMemoryToTestEnd;
        }

        if (MapStart < pMemoryToTestStart)
        {
            MapStart = pMemoryToTestStart;
        }

        for (PHYSICAL_ADDR CurrentStart = MapStart; CurrentStart < MapEnd; )
        {
            PHYSICAL_ADDR CurrentEnd = MapEnd;
            // exclude memory used by the program
            // skip CurrentPhysProgramLocation to PhysMemoryProgramUsedEnd,
            if (CurrentStart < PhysMemoryProgramUsedEnd
                && CurrentEnd > CurrentPhysProgramLocation)
            {
                if (CurrentStart < CurrentPhysProgramLocation)
                {
                    CurrentEnd = CurrentPhysProgramLocation;
                }
                else
                {
                    CurrentStart = PhysMemoryProgramUsedEnd;
                }
                if (CurrentStart >= CurrentEnd)
                {
                    CurrentStart = CurrentEnd;
                    continue;
                }
            }

            size_t size_to_map = size;
            if (CurrentEnd < CurrentStart + size_to_map)
            {
                size_to_map = (size_t)(CurrentEnd - CurrentStart);
            }

            MapVirtualToPhysical(addr, CurrentStart, size_to_map, PageTablePtr, MapFlags);

            arg4 = function(addr, size_to_map, arg1, arg2, arg3, arg4, flags);

            CurrentStart += size_to_map;
        }
    }
    return arg4;
}

DWORD RunMemoryBackward(MemoryOperationFunction *function, void * addr, size_t size,
                        DWORD arg1, DWORD arg2,
                        DWORD arg3, DWORD arg4,
                        DWORD flags, DWORD MapFlags)
{
    // map memory piece by piece, and run the function over it
    // skip CurrentPhysProgramLocation MemoryInUseByProgram,
    //
    // From pMemoryToTestStart to pMemoryToTestEnd
    for(unsigned i = 1; i <= TestParams.MemoryMapUsed; i++)
    {
        unsigned phys_range = TestParams.MemoryMapUsed - i;

        ULARGE_INTEGER tmp;
        tmp.LowPart = TestParams.Map[phys_range].BaseAddressLow;
        tmp.HighPart = TestParams.Map[phys_range].BaseAddressHigh;

        PHYSICAL_ADDR MapStart = (PHYSICAL_ADDR)tmp.QuadPart;

        tmp.LowPart = TestParams.Map[phys_range].RangeSizeLow;
        tmp.HighPart = TestParams.Map[phys_range].RangeSizeHigh;

        PHYSICAL_ADDR MapEnd = MapStart + tmp.QuadPart;

        // skip incomplete pages
        MapStart = PHYSICAL_ADDR((ULONGLONG(MapStart) + 0xFFF) & ~0xFFFULL);
        MapEnd = PHYSICAL_ADDR(ULONGLONG(MapEnd) & ~0xFFFULL);

        if (MapEnd <= pMemoryToTestStart
            || MapStart >= pMemoryToTestEnd)
        {
            continue;
        }

        if (MapEnd > pMemoryToTestEnd)
        {
            MapEnd = pMemoryToTestEnd;
        }

        if (MapStart < pMemoryToTestStart)
        {
            MapStart = pMemoryToTestStart;
        }

        for (PHYSICAL_ADDR CurrentEnd = MapEnd; CurrentEnd > MapStart; )
        {
            PHYSICAL_ADDR CurrentStart = MapStart;
            // exclude memory used by the program
            // skip CurrentPhysProgramLocation to PhysMemoryProgramUsedEnd,
            if (CurrentStart < PhysMemoryProgramUsedEnd
                && CurrentEnd > CurrentPhysProgramLocation)
            {
                if (CurrentEnd > PhysMemoryProgramUsedEnd)
                {
                    CurrentStart = PhysMemoryProgramUsedEnd;
                }
                else
                {
                    CurrentEnd = CurrentPhysProgramLocation;
                }

                if (CurrentStart >= CurrentEnd)
                {
                    CurrentEnd = CurrentStart;
                    continue;
                }
            }

            size_t size_to_map = size;
            if (CurrentEnd < CurrentStart + size_to_map)
            {
                size_to_map = (size_t)(CurrentEnd - CurrentStart);
            }

            MapVirtualToPhysical(addr, CurrentStart, size_to_map, PageTablePtr, MapFlags);

            arg4 = function(addr, size_to_map, arg1, arg2, arg3, arg4, flags);

            CurrentEnd -= size_to_map;
        }
    }
    return arg4;
}

void __stdcall _MemtestEntry()
{
    static TestThread thread;
    static_cast<MEMTEST_STARTUP_PARAMS & >(thread) = TestParams;
    thread.TestFunction();
}

unsigned TestThread::TestFunction()
{
    DWORD seed = RandomSeed;
    DWORD row_size = 0x1000;
    while(1)
    {
        // set new mapping to skip the program
        RelocateProgram();

        char * const TestStartVirtAddr = (char*)TEST_AREA_START_VIRTUAL;  // 16MB

        size_t MemoryTestWindowSize = TEST_AREA_END_VIRTUAL - TEST_AREA_START_VIRTUAL;
#if 0 //def _DEBUG
        VerifyPageTable(pMemoryToTestStart, pMemoryToTestEnd,
                        TestStartVirtAddr, MemoryTestWindowSize,
                        CurrentPhysProgramLocation, MemoryInUseByProgram);
#endif

        // perform test with different read pattern and with refresh check delay
        DWORD flags = m_Flags;
        DWORD MemoryFlags = 0;

        MemoryRowSize = row_size;
        if (row_size > 0x10000)
        {
            flags &= ~ (TEST_SEESAW | TEST_PRELOAD_CACHE1);
            MemoryRowSize = 0x10000;
        }
        if (row_size > 0x20000)
        {
            flags &= ~ (TEST_PRELOAD_CACHE2);
            MemoryRowSize = 0x10000;
        }

        if (row_size == 0x80000 || (flags & TEST_FLAGS_WRITETHRU))
        {
            MemoryFlags = PAGE_DIR_FLAG_WRITETHROUGH;
        }

        if (row_size == 0x100000 || (flags & TEST_FLAGS_NOCACHE))
        {
            MemoryFlags |= PAGE_DIR_FLAG_NOCACHE;
        }

        if (RowSize != 0)
        {
            MemoryRowSize = RowSize;
        }

        for (int DoDelay = 0; DoDelay < 2; DoDelay++)
        {
            m_TestDelay = 0;
            if (m_TestPass & 1)
            {
                m_TestDelay = ShortDelay;
            }
            if ((m_TestPass & 0x3E) == 0x3E)
            {
                m_TestDelay = LongDelay;
            }

            seed = DoRandomMemoryTest(TestStartVirtAddr, MemoryTestWindowSize,
                                      seed, 0x08080000, flags, MemoryFlags);

            RelocateProgram();

            DoMemoryTestPattern(TestStartVirtAddr,
                                MemoryTestWindowSize, 0x00000000, 0x00000000, flags, MemoryFlags);

            RelocateProgram();

            DoMemoryTestPattern(TestStartVirtAddr,
                                MemoryTestWindowSize, 0xFFFFFFFF, 0xFFFFFFFF, flags, MemoryFlags);

            RelocateProgram();

            DoMemoryTestPattern(TestStartVirtAddr,
                                MemoryTestWindowSize, m_Pattern1, m_Pattern2, flags, MemoryFlags);

            RelocateProgram();

            DoMemoryTestPattern(TestStartVirtAddr,
                                MemoryTestWindowSize, m_Pattern2, m_Pattern1, flags, MemoryFlags);

            if (m_TestPass == m_PassCount)
            {
                StoreResultAndReboot(TEST_RESULT_SUCCESS);    // success
            }
            m_TestPass++;
        }
        row_size *= 2;
        if (row_size > 0x100000)
            row_size = 0x1000;
    }
    return 0;
}

#if 0
void PrintMachinePerformance(MEMTEST_STARTUP_PARAMS * pTestParams)
{
    if ((pTestParams->CpuFeatures & CPUID_TIME_STAMP_COUNTER) == 0)
        return;
    // measure CPU clock rate
    Delay(10);
    DWORD cpu_clock = DWORD(__rdtsc());
    Delay(100);
    cpu_clock = DWORD(__rdtsc()) - cpu_clock;

    my_printf(TRUE, "CPU clock rate: %d MHz\n", (cpu_clock + 50000) / 100000);

    // measure L2 cache read/write speed
    MapVirtualToPhysical((void*)(8*MEGABYTE), (PHYSICAL_ADDR)(8*MEGABYTE), 4*MEGABYTE);

    // reset nocache and writethrough flags
    ModifyPageFlags((void*)(8*MEGABYTE), 4*MEGABYTE, 0,
                    PAGE_DIR_FLAG_NOCACHE | PAGE_DIR_FLAG_WRITETHROUGH);

    // preload L2 cache
    PreloadCache((void*)(8*MEGABYTE), 0x20000);
    DWORD rclock = DWORD(__rdtsc());
    int i;
    for (i = 0; i < 10; i++)
    {
        PreloadCache((void*)(8*MEGABYTE), 0x20000);
    }
    rclock = DWORD(__rdtsc()) - rclock;

    DWORD wclock = DWORD(__rdtsc());
    for (i = 0; i < 10; i++)
    {
        FillMemoryPattern((void*)(8*MEGABYTE), 0x20000, 0, 0);
    }
    wclock = DWORD(__rdtsc()) - wclock;

    DWORD wpclock = DWORD(__rdtsc());
    for (i = 0; i < 10; i++)
    {
        FillMemoryPatternWriteAlloc((void*)(8*MEGABYTE), 0x20000, 0);
    }
    wpclock = DWORD(__rdtsc()) - wpclock;

    my_printf(TRUE, "L2 Cache speed: read=%d MB/s, write=%d MB/s, "
              "write/allocate=%d MB/s\n",
              (25 * (cpu_clock / 2)) / rclock,
              (25 * (cpu_clock / 2)) / wclock,
              (25 * (cpu_clock / 2)) / wpclock);

    // measure memory read/write speed
    rclock = DWORD(__rdtsc());
    for (i = 0; i < 10; i++)
    {
        PreloadCache((void*)(8*MEGABYTE), 4*MEGABYTE);
    }
    rclock = DWORD(__rdtsc()) - rclock;

    wclock = DWORD(__rdtsc());
    for (i = 0; i < 10; i++)
    {
        FillMemoryPattern((void*)(8*MEGABYTE), 4*MEGABYTE, 0, 0);
    }
    wclock = DWORD(__rdtsc()) - wclock;

    wpclock = DWORD(__rdtsc());
    for (i = 0; i < 10; i++)
    {
        FillMemoryPatternWriteAlloc((void*)(8*MEGABYTE), 4*MEGABYTE, 0);
    }
    wpclock = DWORD(__rdtsc()) - wpclock;

    my_printf(TRUE, "Main memory speed: read=%d MB/s, write=%d MB/s, "
              "write/allocate=%d MB/s\n",
              (25 * (cpu_clock / 2)) / (rclock / 32),
              (25 * (cpu_clock / 2)) / (wclock / 32),
              (25 * (cpu_clock / 2)) / (wpclock / 32));
    // find how much memory is cached
}
#endif

bool IsPageInTheSystemMap(PHYSICAL_ADDR addr, MEMTEST_STARTUP_PARAMS const * pTestParams)
{
    for (unsigned i = 0; i < pTestParams->MemoryMapUsed; i++)
    {
        LARGE_INTEGER start;
        LARGE_INTEGER size;
        start.LowPart = pTestParams->Map[i].BaseAddressLow;
        start.HighPart = pTestParams->Map[i].BaseAddressHigh;

        size.LowPart = pTestParams->Map[i].RangeSizeLow;
        size.HighPart = pTestParams->Map[i].RangeSizeHigh;

        if (addr >= PHYSICAL_ADDR(start.QuadPart)
            && size.QuadPart >= PAGE_SIZE
            && addr <= PHYSICAL_ADDR(start.QuadPart + size.QuadPart))
        {
            return true;
        }
    }
    return false;
}

// InitMemtest returns address of new stack
char * InitMemtest(MEMTEST_STARTUP_PARAMS * pTestParams)
{
    TestParams = * pTestParams;
    // pTestParams is a pointer to test parameters,
    // obtained by startup module from system configuration
    // and command line options.

    // get current cursor position
    CurrentCursorRow = pTestParams->CursorRow;


    if (TestParams.m_PassCount != 0)
    {
        RebootOnFinish = TRUE;
    }

    if (pTestParams->m_Flags & TEST_FLAGS_NOPREFETCH)
    {
        TestParams.m_Flags &= ~(TEST_PRELOAD_CACHE1 | TEST_PRELOAD_CACHE2);
    }

#ifdef _DEBUG
    srand(pTestParams->RandomSeed);
#endif

    // we won't be using large pages. It doesn't affect test speed much
    if (0) if (0 == (pTestParams->m_Flags & TEST_FLAGS_NOLARGEPAGES))
    {
        LargePagesSupported = TRUE;
        // enable 4MB page size extension in CR4
        //__writecr4(__readcr4() | CR4_4MB_PAGES_ENABLED);
        // When PAE is enabled, large pages are always supported
    }

    // Detect installed memory size
    TopVirtualAddress = (void*)(INITIAL_MAPPED_VIRTUAL_MEMORY-1);
    PageTablePtr = (PageTable*)__readcr3();
    // physical address now is the same as virtual
    PageTablePhysicalAddr = (PHYSICAL_ADDR)PageTablePtr;

    // test the memory starting from 1000000 (or lower address to test)
    // to the last address found (or higher address to test)

    DetectInstalledMemory( & TestParams);

    if (pMemoryToTestEnd < (PHYSICAL_ADDR) INITIAL_MAPPED_VIRTUAL_MEMORY)  // 32 megabytes
    {
        my_puts("Too little memory installed (< 32M).\n"
                "Press RESET button"
                " or turn power off then on to restart the computer.", FALSE, 0x0F00);
        while(1) CheckForKey();
    }
    pMemoryToTestStart = (PHYSICAL_ADDR)(TestParams.MemoryStart * (ULONGLONG)MEGABYTE);
    pMemoryToTestEnd = (PHYSICAL_ADDR)(TestParams.MemoryTop * (ULONGLONG)MEGABYTE);

    my_puts("To terminate test and restart the computer,\n"
            "press Ctrl+Alt+Del or RESET button, or turn power off then on\n", FALSE);

    my_printf(TRUE, "Memory to test: %x to %x (%d megabytes)\n",
              TestParams.MemoryStart, TestParams.MemoryTop,
              (TestParams.MemoryTop - TestParams.MemoryStart));

    // allocate extra memory after the program for stack, TSS, GDT, IDT
    TopProgramAddress = (char*)((TestParams.ProgramTop + PAGE_SIZE-1) & ~(PAGE_SIZE-1));
    char * NewStack = TopProgramAddress + 0x4000; // 16 K
    DESCRIPTOR * pGDT = (DESCRIPTOR *) NewStack;
    const int GDT_SIZE = 12;
    memset(pGDT, 0, GDT_SIZE * sizeof (DESCRIPTOR));
    GATE * pIDT = (GATE*)(pGDT + GDT_SIZE);
    memset(pIDT, 0, 256 * sizeof (GATE));
    // TSS is not required anymore, but we will provide valid one
    _TSS * pTss = (_TSS*) (pIDT + 256);
    memset(pTss, 0, sizeof (_TSS));

    TopProgramAddress = (char*)(pTss) + sizeof (_TSS);

    // init new page directory/table
    PageTable * pPageDirectory = (PageTable*)(ULONG_PTR(TopProgramAddress + PAGE_SIZE-1) & ~(PAGE_SIZE-1));
    // create new page table
    PageTableSize = CalculatePageTableSize(0xFFFFFFFF);

    TopUsedAddress = PageTableSize + (char*)pPageDirectory;
    MemoryInUseByProgram = ULONG_PTR(TopUsedAddress) - PROGRAM_BASE_ADDRESS;

    InitPageTable(pPageDirectory, PageTableSize);

    CurrentPhysProgramLocation = GetPhysAddr((void*)PROGRAM_BASE_ADDRESS, pPageDirectory);

    PhysMemoryProgramUsedEnd = CurrentPhysProgramLocation + MemoryInUseByProgram;
    TopVirtualAddress = (void *)((PageTableSize - 5*PAGE_SIZE) * PAGE_DESCRIPTORS_PER_PAGE - 1);

    // init new GDT
    // 32 bit data and stack segment descriptor
    InitDescriptor(pGDT[8], 0LU, 0xFFFFFFFF,
                   SEG_DESC_DATA_32BIT);
    // 32 bit code segment descriptor
    InitDescriptor(pGDT[9], 0LU, 0xFFFFFFFF,
                   SEG_DESC_CODE_32BIT);
    // TSS descriptor
    InitDescriptor(pGDT[10], pTss,
                   sizeof (_TSS), SEG_DESC_TSS_32BIT);

    // init new IDT
    InitInterruptTable(pIDT);

    // switch to new GDT
    volatile PSEUDO_DESC fptr;
    fptr.len = 12 * sizeof (DESCRIPTOR);
    fptr.ptr = pGDT;
    __asm lgdt fptr.len;

    // switch to new IDT
    fptr.len = 256 * sizeof (GATE);
    fptr.ptr = pIDT;
    __lidt((void*)&fptr);

    // switch to new TSS (no values is loaded yet from it)
    __asm mov eax, 10 * 8
    __asm ltr ax

    // switch to new page table
    PageTablePhysicalAddr = GetPhysAddr(pPageDirectory);
    PageTablePtr = pPageDirectory;

    SwitchPageTable(PageTablePhysicalAddr);

    // init system timer to 10 ms rounds
    _outp(0x43, 0x34);  // command
    _outp(0x40, char(1193180 / 100));
    _outp(0x40, char((1193180 / 100) >> 8));

#ifdef _DEBUG
    {
        my_printf(TRUE, "CPU class: %d, feature word: %x\n",
                  TestParams.CpuType, TestParams.CpuFeatures);
    }
#endif

    if (0 == (TestParams.m_Flags & TEST_NO_MACHINE_CHECK))
    {
        // initialize machine check
        ULONGLONG msg_cap = __readmsr(MCG_CAP);
        if (msg_cap & MCG_CTL_PRESENT)
        {
            __writemsr(MCG_CTL, -1LL);
        }
        for (unsigned i = 0; i < (msg_cap & 0xFF); i++)
        {
            __writemsr(MC0_STATUS+i*4, 0);
        }
        for (unsigned i = 1; i < (msg_cap & 0xFF); i++)
        {
            __writemsr(MC0_CTL+i*4, -1LL);
        }
#ifdef _DEBUG
        {
            my_puts("Machine Check Architecture enabled\n", FALSE);
        }
#endif
        __writecr4(__readcr4() | CR4_MACHINE_CHECK_ENABLED);

#ifdef _DEBUG
        {
            my_puts("Machine Check Exception enabled\n", FALSE);
        }
#endif
    }

    return NewStack;
}

extern "C" void _cdecl MemtestStartup(MEMTEST_STARTUP_PARAMS * pTestParams)
{
    int _ds;
    __asm {
        xor     eax,eax
        mov     ax,ds
        mov     [_ds],eax
    }
    if (_ds & 3)
    {
        // running under Win32
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        if (hConsole != INVALID_HANDLE_VALUE)
        {
            DWORD temp;
            static const char Msg[] = "This program can't be run under Windows or other "
                "Win32 environment.\n"
                "Run the program from bare DOS.\n"
                "\nTo start bare DOS session, restart the computer\n"
                "and press Shift-F5 when \"Starting Windows 95...\" message appears,\n"
                "or boot from a diskette with system files only."
                ;
            WriteFile(hConsole, title, (sizeof title) - 1, &temp, NULL);
            WriteFile(hConsole, Msg, (sizeof Msg) - 1, &temp, NULL);
        }
        ExitProcess(255);
    }

    // DS privilege is Ring0. The program is running as a standalone module
    char * NewStack;
    NewStack = InitMemtest(pTestParams);

    // switch to new stack (current local variables no more available!)
    __asm   mov     esp,NewStack
    // call main memtest function
    _MemtestEntry();
    while(1);
}

PageTableElement* GetPageDirectoryElement(void * VirtAddr, PageTable * pPageTable = PageTablePtr)
{
    return &pPageTable->PageDirectory[(ULONG_PTR(VirtAddr) >> 21) & 0x7FF];
}

PageTableElement* GetPageTableElement(void * VirtAddr, PageTable * pPageTable = PageTablePtr)
{
    return &pPageTable->PageTableArray[ULONG_PTR(VirtAddr) >> 12];
}
    // convert physical to virtual
PHYSICAL_ADDR GetPhysAddr(void * VirtAddr, PageTable * pPageTable)
{
    ASSERT_INFO(VirtAddr <= TopVirtualAddress, "VirtAddr=%x, TopVirtualAddress=%x", VirtAddr, TopVirtualAddress);

    // parse page table manually
    DWORD Addr = (DWORD) VirtAddr;

    PageTableElement PageDirectoryElement = *GetPageDirectoryElement(VirtAddr, pPageTable);
    if (PageDirectoryElement.low & PAGE_DIR_FLAG_LARGE_PAGE)
    {
        // page is 2 megabyte
        return PHYSICAL_ADDR((PageDirectoryElement.q & (-1LL << 21)) |     // upper bits except for lower 21
                             (Addr & (LARGE_PAGE_SIZE-1)));   // lower 21
    }
    else
    {
        PageTableElement PageTableElement = *GetPageTableElement(VirtAddr, pPageTable);
        return PHYSICAL_ADDR((PageTableElement.q & (-1LL << 12)) +
                             (Addr & (PAGE_SIZE-1)));
    }
}

void MapVirtualToPhysical(void * VirtAddr, PHYSICAL_ADDR PhysAddr, size_t size,
                          PageTable* MapTable,
                          DWORD Flags)
{
    ASSERT_INFO(VirtAddr <= TopVirtualAddress, "VirtAddr=%x", VirtAddr);
    ASSERT_INFO(0 == (ULONG_PTR(VirtAddr) & (PAGE_SIZE-1)), "VirtAddr=%x", VirtAddr);
    ASSERT_INFO(0 == (ULONGLONG(PhysAddr) & (PAGE_SIZE-1)), "PhysAddr=%p", PhysAddr);
    ASSERT_INFO(0 == (size & (PAGE_SIZE-1)), "size=%x", size);

    while(size)
    {
        // if 2MB pages are supported, use this feature for 4MB
        // aligned regions
        PageTableElement *PageDirectoryElement = GetPageDirectoryElement(VirtAddr, MapTable);
        PageTableElement *PageTableElement = GetPageTableElement(VirtAddr, MapTable);

        if (LargePagesSupported && size >= LARGE_PAGE_SIZE
            && (ULONG_PTR(VirtAddr) & (LARGE_PAGE_SIZE-1)) == 0
            && (ULONGLONG(PhysAddr) & (LARGE_PAGE_SIZE-1)) == 0)
        {
            // PageNum: index of 2 MB page
            // modify table directory entry
            PageDirectoryElement->q = ULONGLONG(PhysAddr)
                                    | (Flags & 0xFFF)
                                    | PAGE_DIR_FLAG_PRESENT
                                    | PAGE_DIR_FLAG_WRITABLE
                                    | PAGE_DIR_FLAG_LARGE_PAGE;

            // modify also page directory, just for case if page
            // size will change afterwards
            // pPageDir: virtual address of page directory
            // Page directories are placed just after table directory

            for (int i = 0; i < PAGE_DESCRIPTORS_PER_PAGE; i++, PageTableElement++)
            {
                PageTableElement->q = ULONGLONG(PhysAddr)
                                      | (Flags & 0xFFF)
                                      | PAGE_DIR_FLAG_PRESENT
                                      | PAGE_DIR_FLAG_WRITABLE;

                if (0 == (Flags & MAP_VIRTUAL_TO_PHYS_NO_INVTLB))
                {
                    __invlpg(VirtAddr);
                }

                VirtAddr = PAGE_SIZE + (char*)VirtAddr;
                PhysAddr = PAGE_SIZE + PhysAddr;
                size -= PAGE_SIZE;
            }
        }
        else
        {
            // TableNum: index of page directory
            // restore table directory entry to make it pointing
            // to the page directory
            if (PageDirectoryElement->low & PAGE_DIR_FLAG_LARGE_PAGE)
            {
                PageDirectoryElement->q =
                    (ULONGLONG(GetPhysAddr(PageTableElement, MapTable)) & ~0xFFFULL)
                    | PAGE_DIR_FLAG_PRESENT
                    | PAGE_DIR_FLAG_WRITABLE
                    | (Flags & (0xFFF & ~PAGE_DIR_FLAG_LARGE_PAGE));
            }

            PageTableElement->q = ULONGLONG(PhysAddr)
                                | PAGE_DIR_FLAG_PRESENT
                                | PAGE_DIR_FLAG_WRITABLE
                                | (Flags & (0xFFF & ~PAGE_DIR_FLAG_LARGE_PAGE));

            if (0 == (Flags & MAP_VIRTUAL_TO_PHYS_NO_INVTLB))
            {
                __invlpg(VirtAddr);
            }

            VirtAddr = PAGE_SIZE + (char*)VirtAddr;
            PhysAddr = PAGE_SIZE + PhysAddr;
            size -= PAGE_SIZE;
        }
    }
}

DWORD GetPageFlags(void * VirtAddr)
{
    ASSERT_INFO(VirtAddr <= TopVirtualAddress, "VirtAddr=%x", VirtAddr);
    ASSERT_INFO(0 == (ULONG_PTR(VirtAddr) & 0xFFF), "VirtAddr=%x", VirtAddr);

    PageTableElement PageDirectoryElement = *GetPageDirectoryElement(VirtAddr, PageTablePtr);
    if (PageDirectoryElement.low & PAGE_DIR_FLAG_LARGE_PAGE)
    {
        // return table directory entry
        return PageDirectoryElement.low & 0xFFF & ~PAGE_DIR_FLAG_LARGE_PAGE;
    }
    else
    {
        // get page directory entry
        PageTableElement PageTableElement = *GetPageTableElement(VirtAddr, PageTablePtr);
        return PageTableElement.low & 0xFFF;
    }
}

void ModifyPageFlags(void * VirtAddr, size_t size, DWORD SetFlags,
                     DWORD ResetFlags, PageTable *MapTable)
{
    ASSERT_INFO(VirtAddr <= TopVirtualAddress, "VirtAddr=%x", VirtAddr);
    ASSERT_INFO(0 == (ULONG_PTR(VirtAddr) & (PAGE_SIZE-1)), "VirtAddr=%x", VirtAddr);
    ASSERT(0 == (SetFlags & ~0xE7F));
    ASSERT(0 == (ResetFlags & ~0xE7F));

    while(size)
    {
        // if 4MB pages are supported, use this feature for 4MB
        // aligned regions
        PageTableElement *PageDirectoryElement = GetPageDirectoryElement(VirtAddr, MapTable);
        PageTableElement *PageTableElement = GetPageTableElement(VirtAddr, MapTable);

        if (LargePagesSupported && size >= LARGE_PAGE_SIZE
            && (ULONG_PTR(VirtAddr) & (LARGE_PAGE_SIZE-1)) == 0
            && (PageDirectoryElement->low & PAGE_DIR_FLAG_LARGE_PAGE))
        {
            // PageNum: index of 2 MB page
            // modify table directory entry
            PageDirectoryElement->low |= SetFlags;
            PageDirectoryElement->low  &= ~(ULONGLONG)ResetFlags;

            // modify also page directory, just for case if page
            // size will change afterwards
            // pPageDir: virtual address of page directory
            // Page directories are placed just after table directory

            for (int i = 0; i < PAGE_DESCRIPTORS_PER_PAGE; i++, PageTableElement++)
            {
                PageTableElement->low |= SetFlags;
                PageTableElement->low &= ~ResetFlags;

                __invlpg(VirtAddr);

                VirtAddr = PAGE_SIZE + (char*)VirtAddr;
                size -= PAGE_SIZE;
            }
        }
        else
        {
            // TableNum: index of page directory
            // restore table directory entry to make it pointing
            // to the page directory
            if (PageDirectoryElement->low & PAGE_DIR_FLAG_LARGE_PAGE)
            {
                PageDirectoryElement->q =
                    (ULONGLONG(GetPhysAddr(PageTableElement, MapTable)) & ~0xFFFULL)
                    | PAGE_DIR_FLAG_PRESENT
                    | PAGE_DIR_FLAG_WRITABLE;
            }

            PageTableElement->low |= SetFlags;
            PageTableElement->low &= ~(ULONGLONG)ResetFlags;

            __invlpg(VirtAddr);

            VirtAddr = PAGE_SIZE + (char*)VirtAddr;
            size -= PAGE_SIZE;
        }
    }
}

// virtual memory map:
// 00000...400000 - low physical memory (including A0000-FFFFF)
// 400000...4FF000 - program and stack
// 4FF000...1000000 - page tables
// 1000000 and up - memory to test (max 4 GB).
// Physical memory map:
// 000000-A0000 - low memory area
// A0000-100000 - video buffers and other,
// is mapped to the same virtual address
// 100000-400000 - reserved area (for spare test)
// 400000 and up - regular
// Low memory is always mapped after last megabyte and never used for
// storing the test code and data segments.
// Code and data occupies integer number of megabytes (up to 4) and
// is relocated in 1 or 4 megabyte steps.
// Regular memory is usually mapped in 4 MB pages, except for
// 1-4 MB area occupied by test code and tables.
// Memory being tested (starting from 400000 physical)
// is mapped in 4MB pages from 800000 virtual, except for
// 1-4 MB occupied by the program. After that, addresses 100000-400000
// are mapped, and spare megabytes left in 4 MB page occupied by program,
// and on the top, physical addresses 0-A0000 are mapped.

void RelocateProgram(void)
{
    // map an area where to copy code and data segments
    // the program only gets relocated if we need to test the memory below what the program needs
    if (DWORD(pMemoryToTestStart) < 16*MEGABYTE || NULL == CurrentPhysProgramLocation)
    {
        CurrentPhysProgramLocation = OFFSET_FOR_NEXT_PASS + CurrentPhysProgramLocation;
        if (CurrentPhysProgramLocation > (PHYSICAL_ADDR) GIGABYTE)  //FIXME
        {
            CurrentPhysProgramLocation = (PHYSICAL_ADDR)INITIAL_PROGRAM_LOCATION_PHYS;
        }

#ifdef _DEBUG
        my_printf(FALSE, "\rRelocating the program to %X\n", CurrentPhysProgramLocation);
#endif
        PhysMemoryProgramUsedEnd = CurrentPhysProgramLocation + MemoryInUseByProgram;
        MapVirtualToPhysical((void*)TEST_AREA_START_VIRTUAL, CurrentPhysProgramLocation,
                             MemoryInUseByProgram);
        PageTable * pNewPageDirectory = (PageTable*)((PUCHAR)PageTablePtr + OFFSET_FOR_NEXT_PASS);

        // Init and copy page directory
        InitPageTable(pNewPageDirectory, PageTableSize);
        MapVirtualToPhysical((void*)PROGRAM_BASE_ADDRESS, CurrentPhysProgramLocation,
                             MemoryInUseByProgram, pNewPageDirectory);

        // switch to new page directory
        PHYSICAL_ADDR pNewPhysTablePtr = GetPhysAddr(pNewPageDirectory, PageTablePtr);

        // copy program code and data segment
        memcpy((void *) TEST_AREA_START_VIRTUAL, (void *) PROGRAM_BASE_ADDRESS,
               DWORD(TopProgramAddress) - PROGRAM_BASE_ADDRESS);

        SwitchPageTable(pNewPhysTablePtr);
    }
}

// function does not change program area mapping
// the function maps as much as possible starting from PhysAddressToStart

// test if 4 KB page physically present.
// This means it is readable and writable
// and does not produce address overlap with 0-FFF page.

BOOL TestPageForPresence(void * VirtAddr, BOOL FastDetect)
{
    DWORD * const pZero = NULL;   // NULL now is a valid pointer!
    DWORD * const pPage = (DWORD *) VirtAddr;
    DWORD checksum = 0;
    DWORD checksum1 = 0;
    int i;
    // get zero page checksum
    int DwordCount = PAGE_SIZE / 4;
    if (FastDetect)
    {
        DwordCount = 0x100 / 4;
    }
    for (i = 0; i < DwordCount; i += 2)
    {
        checksum += pZero[i] + pZero[i+1];
    }
    // fill the page with 0xA5A5A5A5
    for (i = 0; i < DwordCount; i+=2)
    {
        pPage[i] = 0xA5A5A5A5;
        pPage[i+1] = 0xA5A5A5A5;
    }
    // check the page
    for (i = 0; i < DwordCount; i+=2)
    {
        if ((pPage[i] - 0xA5A5A5A5) | (pPage[i + 1] - 0xA5A5A5A5))
            return FALSE;
    }
    // check zero page
    for (i = 0; i < DwordCount; i += 2)
    {
        checksum1 += pZero[i] + pZero[i+1];
    }
    if (checksum1 != checksum)
        return FALSE;
    // fill the page with 0x5A5A5A5A
    for (i = 0; i < DwordCount; i+=2)
    {
        pPage[i] = 0x5A5A5A5A;
        pPage[i+1] = 0x5A5A5A5A;
    }
    // check the page
    for (i = 0; i < DwordCount; i+=2)
    {
        if ((pPage[i] - 0x5A5A5A5A) | (pPage[i + 1] - 0x5A5A5A5A))
            return FALSE;
    }
    // check zero page
    checksum1 = 0;
    for (i = 0; i < DwordCount; i +=2)
    {
        checksum1 += pZero[i] + pZero[i+1];
    }
    if (checksum1 != checksum)
        return FALSE;

    return TRUE;
}

void DetectInstalledMemory(MEMTEST_STARTUP_PARAMS * pTestParams)
{
    // get zero and target page flags
    void * const check_addr = (void *) 0xFFF000;  // 16MB-4K
    DWORD ZeroPageFlags = GetPageFlags(NULL);
    DWORD TargetPageFlags = GetPageFlags(check_addr);

    if (0 == pTestParams->MemoryTop)
    {
        pTestParams->MemoryTop = 0x200000000LL / MEGABYTE;    // 8GB in MB
    }

    PHYSICAL_ADDR MemoryTop = PHYSICAL_ADDR(pTestParams->MemoryTop * (ULONGLONG)MEGABYTE);
    PHYSICAL_ADDR MemoryStart = PHYSICAL_ADDR(pTestParams->MemoryStart * (ULONGLONG)MEGABYTE);

    // set zero and target page attributes to non-cacheable
//    if (TestParams.CpuType >= 486)    // assumed
    {
        ModifyPageFlags(0, PAGE_SIZE, PAGE_DIR_FLAG_NOCACHE, 0);
        ModifyPageFlags(check_addr, PAGE_SIZE, PAGE_DIR_FLAG_NOCACHE, 0);
    }

    ModifyPageFlags(check_addr, PAGE_SIZE,
                    PAGE_DIR_FLAG_PRESENT | PAGE_DIR_FLAG_WRITABLE
                    | PAGE_DIR_FLAG_ACCESSED | PAGE_DIR_FLAG_DIRTY, 0);

    __wbinvd();

    PHYSICAL_ADDR pStart = (PHYSICAL_ADDR)(16*MEGABYTE);
    if (pStart  < MemoryStart)
    {
        pStart = MemoryStart;
    }
    for(; pStart < MemoryTop; pStart += PAGE_SIZE)
    {
        if ( ! IsPageInTheSystemMap(pStart, pTestParams))
        {
            continue;
        }
        if ((ULONGLONG(pStart) & 0xFFFFFF) == 0)
        {
            my_printf(FALSE, "Detecting physical memory, %dM\r", DWORD(ULONGLONG(pStart) / MEGABYTE));
        }
        MapVirtualToPhysical(check_addr, pStart, PAGE_SIZE);

        if (! TestPageForPresence(check_addr, pTestParams->m_Flags & TEST_FLAGS_FASTDETECT))
            break;
    }

    pTestParams->MemoryTop = DWORD(ULONGLONG(pStart) / MEGABYTE);

    if (0 == (ZeroPageFlags & PAGE_DIR_FLAG_NOCACHE))
    {
        ModifyPageFlags(0, PAGE_SIZE, 0, PAGE_DIR_FLAG_NOCACHE);
    }
    if (0 == (TargetPageFlags & PAGE_DIR_FLAG_NOCACHE))
    {
        ModifyPageFlags(check_addr, PAGE_SIZE, 0, PAGE_DIR_FLAG_NOCACHE);
    }
}

void InitPageTable(PageTable * VirtPageDirAddress, size_t BufSize)
{
    // assuming 8byte PTEs,
    // we need 4KB/2MB of first level PTE = 8MB for 4GB,
    // 16 KB of second level, 32B third level
    // for simplicity, we'll use 16 MB of virt space for the program and its tables
    ASSERT(0 == (DWORD(VirtPageDirAddress) & 0xFFF));
    ASSERT((BufSize & 0xFFF) == 0);
    ASSERT(BufSize >= 0x3000);
    ASSERT_INFO(BufSize <= RESERVED_FOR_PAGE_TABLE, "BufSize=%x", BufSize);

    memset(VirtPageDirAddress, 0, BufSize);

    BufSize -= PAGE_SIZE;

    for (unsigned i = 0; i < 4; i++)
    {
        VirtPageDirAddress->PageDirPointerTable[i].q = ULONGLONG(GetPhysAddr(
                                                               &VirtPageDirAddress->PageDirectory[i*PAGE_DESCRIPTORS_PER_PAGE]))
                                                       | PAGE_DIR_FLAG_PRESENT;
    }

    for (unsigned i = 0; i < PAGE_DESCRIPTORS_PER_PAGE*4; i++)
    {
        VirtPageDirAddress->PageDirectory[i].q = ULONGLONG(GetPhysAddr(
                                                               &VirtPageDirAddress->PageTableArray[i*PAGE_DESCRIPTORS_PER_PAGE]))
                                                 | PAGE_DIR_FLAG_PRESENT | PAGE_DIR_FLAG_WRITABLE;
    }
    // map lower 32 MB as currently is, to allow for page table switch
    for (char * addr = 0;DWORD(addr) < 32*MEGABYTE; addr += PAGE_SIZE)
    {
        MapVirtualToPhysical(addr, GetPhysAddr(addr), PAGE_SIZE,
                             VirtPageDirAddress,
                             PAGE_DIR_FLAG_PRESENT
                             | PAGE_DIR_FLAG_WRITABLE
                             | MAP_VIRTUAL_TO_PHYS_NO_INVTLB);
    }
}

void InitGate(GATE & g, WORD Selector, void * Offset, WORD flags)
{
    g.offset_0_15 = WORD(Offset);
    g.selector = Selector;
    g.offset_16_31 = WORD(DWORD(Offset) >> 16);
    g.flags = flags;
}

static char PrintExceptionBuf[256];

void __cdecl PrintException(const char * pFormat)
{
    DWORD * pArgs = (DWORD*) & pFormat;
    my_sprintf(PrintExceptionBuf, "\nEIP: %x, EFLAGS: %x, EAX: %x, EBX: %x, ECX: %x,\n"
               "EDX: %x, ESI: %x, EDI: %x, EBP: %x\n",
               pArgs[8], pArgs[10], pArgs[1], pArgs[2], pArgs[3], pArgs[4],
               pArgs[5], pArgs[6], pArgs[7]);
    my_puts(PrintExceptionBuf, FALSE, 0x0F00);
    my_puts(pFormat, FALSE,0x8F00);
}

__declspec(naked) void NMIHandler()
{
    RESET_IDT();
    my_puts("Non-Maskable Interrupt occured, probably memory parity error. Program halted", FALSE, 0x0F00);
    do __asm hlt
    while(1);
}
__declspec(naked) void MachineCheckHandler()
{
    RESET_IDT();
    my_puts("Machine Check Exception occured, probably internal CPU error. Program halted", FALSE, 0x0F00);
    do __asm hlt
    while(1);
}

__declspec(naked) void DoubleFaultHandler()
{
    RESET_IDT();
    my_puts("Double Fault Exception occured. Program halted", FALSE, 0x0F00);
    do __asm hlt
    while(1);
}

__declspec(naked) void SegmentNotPresentHandler()
{
    RESET_IDT();
    my_puts("Segment Not Present Exception occured. Program halted", FALSE, 0x0F00);
    do __asm hlt
    while(1);
}

__declspec(naked) void UnexpectedExceptionWithCode()
{
    static DWORD _eip;
    __asm add     esp,4
    RESET_IDT();    // to avoid nested faults
    __asm {
        push    ebp
        push    edi
        push    esi
        push    edx
        push    ecx
        push    ebx
        push    eax
    }

    PrintException("Unexpected exception occured, program halted");
    do __asm hlt
    while(1);
}

__declspec(naked) void PageFaultHandler()
{
    static DWORD _eip;
    __asm add     esp,4
    RESET_IDT();    // to avoid nested faults
    __asm {
        push    ebp
        push    edi
        push    esi
        push    edx
        push    ecx
        push    ebx
        push    eax
    }

    PrintException("Page Fault occured, program halted");
    do __asm hlt
    while(1);
}

__declspec(naked) void UnexpectedException()
{
    static DWORD _eip;
    RESET_IDT();    // to avoid nested faults
    __asm {
        push    ebp
        push    edi
        push    esi
        push    edx
        push    ecx
        push    ebx
        push    eax
    }

    PrintException("Unexpected exception occured, program halted");
    do __asm hlt
    while(1);
}

#define DivideErrorHandler       UnexpectedException
#define DebugExceptionHandler    UnexpectedException
#define BreakHandler             UnexpectedException
#define OverflowHandler          UnexpectedException
#define BOUNDHandler              UnexpectedException
#define InvalidOpcodeHandler       UnexpectedException
#define DeviceNotAvailableHandler  UnexpectedException
#define CoprocessorOverrunHandler  UnexpectedException
#define InvalidTSSHandler          UnexpectedExceptionWithCode
#define StackFaultHandler          UnexpectedExceptionWithCode
#define GeneralProtectionHandler   UnexpectedExceptionWithCode
//#define PageFaultHandler           UnexpectedExceptionWithCode
#define ReservedHandler            UnexpectedExceptionWithCode
#define FloatingPointHandler       UnexpectedException
#define AlignmentHandler           UnexpectedExceptionWithCode

void InitInterruptTable(GATE * pIDT)
{
    InitGate(pIDT[0], 9 * 8, DivideErrorHandler, GATE_PRESENT | GATE_TRAP_GATE);
    InitGate(pIDT[1], 9 * 8, DebugExceptionHandler, GATE_PRESENT | GATE_TRAP_GATE);
    InitGate(pIDT[2], 9 * 8, NMIHandler, GATE_PRESENT | GATE_INTERRUPT_GATE);
    InitGate(pIDT[3], 9 * 8, BreakHandler, GATE_PRESENT | GATE_TRAP_GATE);
    InitGate(pIDT[4], 9 * 8, OverflowHandler, GATE_PRESENT | GATE_TRAP_GATE);
    InitGate(pIDT[5], 9 * 8, BOUNDHandler, GATE_PRESENT | GATE_TRAP_GATE);
    InitGate(pIDT[6], 9 * 8, InvalidOpcodeHandler, GATE_PRESENT | GATE_TRAP_GATE);
    InitGate(pIDT[7], 9 * 8, DeviceNotAvailableHandler, GATE_PRESENT | GATE_TRAP_GATE);
    InitGate(pIDT[8], 9 * 8, DoubleFaultHandler, GATE_PRESENT | GATE_INTERRUPT_GATE);
    InitGate(pIDT[9], 9 * 8, CoprocessorOverrunHandler, GATE_PRESENT | GATE_TRAP_GATE);
    InitGate(pIDT[10], 9 * 8, InvalidTSSHandler, GATE_PRESENT | GATE_TRAP_GATE);
    InitGate(pIDT[11], 9 * 8, SegmentNotPresentHandler, GATE_PRESENT | GATE_INTERRUPT_GATE);
    InitGate(pIDT[12], 9 * 8, StackFaultHandler, GATE_PRESENT | GATE_TRAP_GATE);
    InitGate(pIDT[13], 9 * 8, GeneralProtectionHandler, GATE_PRESENT | GATE_TRAP_GATE);
    InitGate(pIDT[14], 9 * 8, PageFaultHandler, GATE_PRESENT | GATE_TRAP_GATE);
    InitGate(pIDT[15], 9 * 8, ReservedHandler, GATE_PRESENT | GATE_TRAP_GATE);
    InitGate(pIDT[16], 9 * 8, FloatingPointHandler, GATE_PRESENT | GATE_TRAP_GATE);
    InitGate(pIDT[17], 9 * 8, AlignmentHandler, GATE_PRESENT | GATE_TRAP_GATE);
    InitGate(pIDT[18], 9 * 8, MachineCheckHandler, GATE_PRESENT | GATE_INTERRUPT_GATE);

    for (int i = 19; i < 256; i++)
    {
        InitGate(pIDT[i], i * 8, 0, GATE_INTERRUPT_GATE);   // non-present
    }
}

