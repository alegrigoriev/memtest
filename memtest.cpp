// memtest.cpp

#include <windows.h>

#ifndef STANDALONE
#include <stdio.h>
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

const size_t cache1_preload_size = 4096;
const size_t cache2_preload_size = 1024 * 128;

MEMTEST_STARTUP_PARAMS TestParams;

int TestDelay;

BOOL RebootOnFinish;
static DWORD dwRandSeed = 0xFFFFFFFF;
size_t MemoryRowSize = 0;    // vary from 4K to 64K

char * pMemoryToTestStart;
char * pMemoryToTestEnd;
DWORD TestFlags = TEST_PRELOAD_CACHE2 | TEST_PRELOAD_CACHE1
                  | TEST_EMPTY_CACHE | TEST_SEESAW | TEST_DELAY;
int TestPass = 1;
char * const TestStartVirtAddr = (char*)0x800000;  // 8MB

////////////////////////////////////////////////////
// Page directory stuff

DWORD * PageTablePtr;    // VIRTUAL address of page table
size_t PageTableSize;
#define PAGE_4M 0x80    // 4 megabyte page
#define PAGE_CACHE_DISABLE 0x10
#define PAGE_WRITE_THROUGH 0x8

DWORD PageTableOffset;  // offset (in DWORDs) of page
// directory virtual address relative to physical

void * TopVirtualAddress;
void * TopUsedAddress;  // top virtual address used by the program and tables
char * TopProgrmaAddress;
size_t MemoryInUseByProgram;    // including page tables

void * CurrentPhysProgramLocation;
char * TopProgramAddress;
// Current Processor features
#define CPUID_4MB_PAGES     8
#define CPUID_MACHINE_CHECK_ARCHITECTURE 0x4000    // bit 14
#define CPUID_MACHINE_CHECK_EXCEPTION 0x80    // bit 7
#define CPUID_TIME_STAMP_COUNTER 0x10

#define RDMSR __emit 0x0F   __asm __emit 0x32
#define WRMSR __emit 0x0F   __asm __emit 0x30

// machine check MSRs
#define MCG_CAP 0x179
#define MCG_STATUS 0x17A
#define MCG_CTL 0x17B
#define MCG_CTL_PRESENT 0x100

#define CR4_4MB_PAGES_ENABLED 0x10
#define CR4_MACHINE_CHECK_ENABLED 0x40    // bit 6

BOOL f4MBPagesSupported;

//////////////////////////////////////////////////////////////
//// screen stuff
// white on black test color mask for text mode display buffer
const int screen_width = 80;
const int screen_height = 25;

// pointer to screen buffer in 32-bit address space
unsigned __int16 (*const screenbase)[screen_width]
= (unsigned __int16 (*)[screen_width]) 0xb8000;

int curr_row=24, curr_col=0; // current row and column on text mode display

char title[] = MEMTEST_TITLE;

/////////////////////////////////////////////////////////////
// Function declarations

void WriteBackAndInvalidate();
void DetectInstalledMemory(MEMTEST_STARTUP_PARAMS * pTestParams);
BOOL TestPageForPresence(void * VirtAddr, BOOL FastDetect);
DWORD GetPageFlags(void * VirtAddr);
void ModifyPageFlags(void * VirtAddr, size_t size, DWORD SetFlags,
                     DWORD ResetFlags = 0);
void * GetPhysAddr(void * VirtAddr);    // convert physical to virtual
void MapVirtualToPhysical(void * VirtAddr, void * PhysAddr, size_t size);
void InitVirtualToPhysical(void * VirtAddr, void * PhysAddr, size_t size,
                           DWORD * pPageTable);
void InitPageTable(void * VirtPageDirAddress, size_t BufSize);
void InitGate(GATE & g, WORD selector, void * offset, WORD flags);
// init interrupt table
void InitInterruptTable(GATE * Addr);
// move the program and all the tables to next 4 MByte
void RelocateProgram(void);
size_t MapMemoryToTest(void * ProgramRegion, size_t ProgramRegionSize,
                       void * PhysMemoryBottom, void * PhysMemoryTop);
int CheckForKey();

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
#ifdef _DEBUG
#define SwitchPageTable(PhysPageDirAddress)         \
    if (DWORD(PhysPageDirAddress) & 0xFFF)          \
        {                                           \
        my_puts("Error In SwitchPageTable", FALSE); \
        while(1);                                   \
        }                                           \
        __asm {mov     eax, PhysPageDirAddress}      \
        __asm {mov     cr3, eax}                     \
        __asm {jmp     label1}                     \
label1:;
#else
#define SwitchPageTable(PhysPageDirAddress)         \
        __asm {mov     eax, PhysPageDirAddress}      \
        __asm {mov     cr3, eax}                     \
        __asm {jmp     label1}                       \
label1:;
#endif


#define TRUE 1
#define FALSE 0
#define ASSERT(x)

void my_puts(const char * str, BOOL IsErrMsg = FALSE,
             unsigned __int16 color_mask = 0x0700)
{
#ifdef STANDALONE
    static int error_row = 0;
    unsigned __int16 * position = &screenbase[curr_row][curr_col];
    unsigned char c;
    if (IsErrMsg && 0 == error_row)
    {
        error_row = screen_height-1;
    }

    while ((c = *str++) != 0)
    {
        if ('\r' == c)
        {
            curr_col = 0;
            position = &screenbase[curr_row][0];
            continue;
        }

        if ('\n' == c || curr_col >= screen_width)
        {
            // do scrolling or move cursor to the next line
            curr_col = 0;
            if (curr_row < screen_height - 1)
            {
                curr_row++;
            }
            else
            {
                memcpy(screenbase, &screenbase[1][0], (screen_height - 1) * sizeof screenbase[0]);
                curr_row = screen_height - 1;
            }
            position = &screenbase[curr_row][0];
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
        curr_col++;
    }
#else
    printf("%s", str);
#endif
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
        char c = *format++;
        if (c != '%')
        {
            *buffer = c;
        }
        else
        {
            switch (c = *format++)
            {
            case 'x':
            case 'X':
                buffer = itox(buffer, * (int *) vararg) - 1;
                vararg = 1 + (int *) vararg;
                break;
            case 'D':
            case 'd':
                buffer = itod(buffer, * (int *) vararg) - 1;
                vararg = 1 + (int *) vararg;
                break;
            default:
                *buffer = c;
                break;
            }
        }
    } while (*buffer++);
}

void my_printf(const char * format, ...)
{
    char buffer[1024];
    my_vsprintf(buffer, format, 1 + &format);
    my_puts(buffer);
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
void PreheatMemory(void * addr, size_t memsize, size_t step)
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
}

void DoPreheatMemory(void * addr, size_t _size, size_t step,
                     DWORD flags)
{
    if (0 == (flags & TEST_FLAGS_PREHEAT_MEMORY))
    {
        return;
    }
    // save old page attributes
    DWORD OldAttrs = GetPageFlags(addr);

    ModifyPageFlags(addr, _size,
                    PAGE_DIR_FLAG_NOCACHE, 0);

    WriteBackAndInvalidate();

    PreheatMemory(addr, _size, step);

    ModifyPageFlags(addr, _size,
                    OldAttrs & PAGE_DIR_FLAG_NOCACHE,
                    (~OldAttrs) & PAGE_DIR_FLAG_NOCACHE);
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

// fill memory area with the specified QWORD data
// The function is called to write all zeros or ones.
void __stdcall
        FillMemoryPattern(void * addr, size_t _size,
                          DWORD pattern)
{
    // size is 32 bytes multiply
    __asm {
        mov     ecx,_size
        shr     ecx,5
        mov     edx,addr
        mov     eax,pattern
loop1:
        mov     [edx],eax
        mov     [edx+4],eax
        mov     [edx+8],eax
        mov     [edx+4+8],eax
        mov     [edx+16],eax
        mov     [edx+4+16],eax
        mov     [edx+24],eax
        mov     [edx+4+24],eax
        add     edx,32
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
        mov     ecx,_size
        add     esi,ecx    // calculate high address
        shr     ecx,5
        mov     eax,data0
        mov     edx,data1
        mov     ebx,new_data0
        mov     edi,new_data1
loop1:
        sub     esi,32
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
        dec     ecx
        jnz     loop1
    }

    return;

    _asm {

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
    }
    goto     continue24;
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

void WriteTestData(char * addr, size_t size,
                   DWORD pattern1, DWORD pattern2,
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

                if (flags & (TEST_ALL0 | TEST_ALL1))
                {
                    FillMemoryPattern(addr, curr_size, pattern1);
                }
                else
                {
                    FillMemoryPattern(addr, curr_size, pattern1,
                                      pattern2);
                }

                addr += curr_size;
                row_size -= curr_size;
            }
            DWORD tmp = pattern1;
            pattern1 = pattern2;
            pattern2 = tmp;
        }
    }
}

void CompareTestData(char * addr, size_t size,
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
#ifdef  _DEBUG
        if (MemoryRowSize > cache2_preload_size)
        {
            my_puts("MemoryRowSize > cache2_preload_size\n", FALSE);
            while(1);
        }
#endif
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
#ifdef _DEBUG
                if (flags & TEST_REPLACE)
                {
                    my_puts("\nWrong flags in CompareTestData\n", FALSE);
                    while(1);
                }
#endif
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

            addr += row_size;
            test_size -= row_size;

            DWORD tmp = pattern1;
            pattern1 = pattern2;
            pattern2 = tmp;

            tmp = new_pattern1;
            new_pattern1 = new_pattern2;
            new_pattern2 = tmp;
        }
    }
}

void CompareTestDataBackward(char * addr, size_t size,
                             DWORD pattern1, DWORD pattern2,
                             DWORD new_pattern1, DWORD new_pattern2,
                             DWORD flags)
{
    if(flags & TEST_SEESAW)
    {
        ReadMemorySeeSaw(addr, size, MemoryRowSize);
        WriteBackAndInvalidate();
    }

#ifdef _DEBUG
    if (size % MemoryRowSize != 0)
    {
        my_puts("(size % MemoryRowSize != 0) in CompareTestDataBackward\n", FALSE);
        while(1);
    }
#endif
    if (0 == ((size / MemoryRowSize) & 1))
    {
        DWORD tmp = pattern1;
        pattern1 = pattern2;
        pattern2 = tmp;

        tmp = new_pattern1;
        new_pattern1 = new_pattern2;
        new_pattern2 = tmp;
    }

    addr += size;
    while (size != 0)
    {
        size_t test_size = size;
        if(flags & TEST_PRELOAD_CACHE2)
        {
            if (test_size > cache2_preload_size)
                test_size = cache2_preload_size;
            PreloadCache(addr - test_size, test_size);
        }

#ifdef  _DEBUG
        if (MemoryRowSize > cache2_preload_size)
        {
            my_puts("MemoryRowSize > cache2_preload_size\n", FALSE);
            while(1);
        }
#endif
        size -= test_size;
        while(test_size != 0)
        {
            size_t row_size = MemoryRowSize;
            if (row_size > test_size)
            {
                row_size = test_size;
            }
            addr -= row_size;
            if (flags & (TEST_ALL0 | TEST_ALL1))
            {
#ifdef _DEBUG
                if (flags & TEST_REPLACE)
                {
                    my_puts("\nWrong flags in CompareTestData\n", FALSE);
                    while(1);
                }
#endif
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
}

DWORD WriteRandomTestData(char * addr, size_t size,
                          DWORD seed, DWORD polynom, DWORD flags)
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

        addr += test_size;
        size -= test_size;
    }
    return seed;
}

DWORD CompareRandomTestData(char * addr, size_t size,
                            DWORD seed, DWORD polynom, DWORD flags)
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

        seed = ComparePseudoRandom(addr, test_size, seed,
                                   polynom);
        addr += test_size;
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

void DoMemoryTestAlternatePattern(char * addr, size_t _size,
                                  DWORD flags)
{
    if (addr == NULL || _size == 0)
    {
        return;
    }
    for (int repeat = 16; repeat--;)
    {
        DoPreheatMemory(addr, _size, 0x10000, flags);
        CheckForKey();

        if(flags & TEST_EMPTY_CACHE)
        {
            WriteBackAndInvalidate();
        }
        DWORD pattern = 0x00010001 << repeat;
        if (flags & TEST_RUNNING0)
        {
            pattern = ~pattern;
        }
        // write test data to memory area
        // print current test
        my_printf("\r                                      "
                   "                                      \rPass %d,", TestPass);

        my_printf("Pattern: %X%X %X%X",
                   pattern, pattern,
                   ~pattern, ~pattern);

        // write the pattern first time,
        // if two compare passes, compare it upward,
        // then compare and rewrite with inverse pattern upward
        // then if two compare passes, compare it downward
        // then compare and rewrite with next pattern downward
        //
        if (15 == repeat)
        {
            WriteTestData(addr, _size, pattern, ~pattern, flags);
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
                my_printf("\nError introduced at %x, data = %x\r",
                           GetPhysAddr(erraddr), err);
            }
        }
#endif
        CheckForKey();

        if(flags & TEST_EMPTY_CACHE)
        {
            WriteBackAndInvalidate();
        }

        // check test data
        if (flags & TEST_DELAY)
        {
            // delay idle
            Delay(TestDelay);
            CheckForKey();
        }

        if (flags & TEST_READ_TWICE)
        {
            CompareTestData(addr, _size, pattern, ~pattern, 0, 0, flags);
        }
        CompareTestData(addr, _size, pattern, ~pattern, ~pattern, pattern,
                        flags | TEST_REPLACE);

        CheckForKey();

        if(flags & TEST_EMPTY_CACHE)
        {
            WriteBackAndInvalidate();
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
                my_printf("\nError introduced at %x, data = %x\r",
                           GetPhysAddr(erraddr), err);
            }
        }
#endif

        if (flags & TEST_DELAY)
        {
            // delay idle
            Delay(TestDelay);
            CheckForKey();
        }

        // check test data
        if (flags & TEST_READ_TWICE)
        {
            CompareTestDataBackward(addr, _size, ~pattern, pattern, 0, 0, flags);
            CheckForKey();
        }
        CompareTestDataBackward(addr, _size, ~pattern, pattern,
                                _rotr(pattern, 1), ~_rotr(pattern, 1), TEST_REPLACE | flags);

    }
}

void DoMemoryTestSpecialPattern(char * addr, size_t _size,
                                DWORD Pattern1, DWORD Pattern2,
                                DWORD flags)
{
    if (addr == NULL || _size == 0)
    {
        return;
    }
    for (int repeat = 32; repeat--;)
    {
        DoPreheatMemory(addr, _size, 0x10000, flags);
        CheckForKey();

        if(flags & TEST_EMPTY_CACHE)
        {
            WriteBackAndInvalidate();
        }

        // write test data to memory area
        // print current test
        my_printf("\r                                      "
                   "                                      \rPass %d,", TestPass);

        my_printf("Pattern: %X%X %X%X",
                   Pattern1, Pattern1,
                   Pattern2, Pattern2);

        // write the pattern first time,
        // if two compare passes, compare it upward,
        // then compare and rewrite with inverse pattern upward
        // then if two compare passes, compare it downward
        // then compare and rewrite with next pattern downward
        //
        if (31 == repeat)
        {
            WriteTestData(addr, _size, Pattern1, Pattern2, flags);
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
                my_printf("\nError introduced at %x, data = %x\r",
                          GetPhysAddr(erraddr), err);
            }
        }
#endif
        CheckForKey();

        if(flags & TEST_EMPTY_CACHE)
        {
            WriteBackAndInvalidate();
        }

        // check test data
        if (flags & TEST_DELAY)
        {
            // delay idle
            Delay(TestDelay);
            CheckForKey();
        }

        if (flags & TEST_READ_TWICE)
        {
            CompareTestData(addr, _size, Pattern1, Pattern2, 0, 0, flags);
        }
        CompareTestData(addr, _size, Pattern1, Pattern2, ~Pattern1, ~Pattern2,
                        flags | TEST_REPLACE);

        CheckForKey();

        if(flags & TEST_EMPTY_CACHE)
        {
            WriteBackAndInvalidate();
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
                my_printf("\nError introduced at %x, data = %x\r",
                          GetPhysAddr(erraddr), err);
            }
        }
#endif

        if (flags & TEST_DELAY)
        {
            // delay idle
            Delay(TestDelay);
            CheckForKey();
        }

        // check test data
        if (flags & TEST_READ_TWICE)
        {
            CompareTestDataBackward(addr, _size, ~Pattern1, ~Pattern2, 0, 0, flags);
            CheckForKey();
        }
        CompareTestDataBackward(addr, _size, ~Pattern1, ~Pattern2,
                                _rotr(Pattern1, 1), _rotr(Pattern2, 1),
                                TEST_REPLACE | flags);

        Pattern1 = _rotr(Pattern1, 1);
        Pattern2 = _rotr(Pattern2, 1);
    }
}

void DoMemoryTestUniformPattern(char * addr, size_t _size,
                                DWORD flags)
{
    if (addr == NULL || _size == 0)
    {
        return;
    }
    CheckForKey();

    DoPreheatMemory(addr, _size, 0x10000, flags);

    if(flags & TEST_EMPTY_CACHE)
    {
        WriteBackAndInvalidate();
    }
    DWORD pattern = 0;
    if (flags & TEST_ALL1)
    {
        pattern = 0xFFFFFFFF;
    }
    // write test data to memory area
    // print current test
    my_printf("\r                                      "
               "                                      \rPass %d,", TestPass);

    my_printf("Pattern: %X%X", pattern, pattern);

    WriteTestData(addr, _size, pattern, pattern, flags);

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
            my_printf("\nError introduced at %x, data = %x\r",
                       GetPhysAddr(erraddr), err);
        }
    }
#endif
    CheckForKey();

    // check test data
    if (flags & TEST_DELAY)
    {
        // delay idle
        Delay(TestDelay);
        CheckForKey();
    }

    if(flags & TEST_EMPTY_CACHE)
    {
        WriteBackAndInvalidate();
    }

    CompareTestData(addr, _size, pattern, pattern, 0, 0, flags);
    if (flags & TEST_READ_TWICE)
    {
        CompareTestData(addr, _size, pattern, pattern, 0, 0, flags);
    }
}

DWORD DoRandomMemoryTest(char * addr, size_t _size, DWORD seed,
                         DWORD polynom, DWORD flags)
{
    DoPreheatMemory(addr, _size, 0x10000, flags);
    CheckForKey();
    if(flags & TEST_EMPTY_CACHE)
    {
        WriteBackAndInvalidate();
    }
    // write test data to memory area
    // print current test
    my_printf("\r                                      "
              "                                      \rPass %d, "
              "Testing random pattern...", TestPass);
    my_puts("Testing random pattern...", FALSE);

    if (addr == NULL || _size == 0)
    {
        return seed;
    }

    DWORD new_seed = WriteRandomTestData(addr, _size, seed, polynom, flags);

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
            my_printf("\nError introduced at %x, data = %x\r",
                      GetPhysAddr(erraddr), err);
        }
    }
#endif
    // check test data
    CheckForKey();

    if (flags & TEST_DELAY)
    {
        // delay idle
        Delay(TestDelay);
    }

    CheckForKey();

    if(flags & TEST_EMPTY_CACHE)
    {
        WriteBackAndInvalidate();
    }

    CompareRandomTestData(addr, _size, seed, polynom, flags);
    if (flags & TEST_READ_TWICE)
    {
        CompareRandomTestData(addr, _size, seed, polynom, flags);
    }
    return new_seed;
}

#ifdef _DEBUG
void VerifyPageTable(void * pPhysStart, void * pPhysEnd,
                     void * pVirtStart, size_t TestSize,
                     void * TestCodePhysAddr, size_t TestCodeSize)
{
    // check that correct page table is used
    // check that the mapped memory does not include any memory outside the range
    // and doesn't include the program memory
    void * pVirtEnd = (char*)pVirtStart + TestSize;
    char * p = (char*)pVirtStart;
    char s[128];
    void * PhysAddr;
    my_sprintf(s, "Verifying the page table... Va=%x, siz=%x, code=%x\r", pVirtStart,
               TestSize, TestCodePhysAddr);
    my_puts(s);
    if (pVirtEnd < pVirtStart)
    {
        my_puts("pVirtStart + TestSize < pVirtStart\n", TRUE);
        return;
    }
    PhysAddr = GetPhysAddr(VerifyPageTable);
    if (PhysAddr < TestCodePhysAddr
        || PhysAddr >= (char*)TestCodePhysAddr+TestCodeSize)
    {
        my_sprintf(s, "\nProgram PhysAddr(%x)=%x is out of range (%x-%x)\n", VerifyPageTable,
                   PhysAddr, TestCodePhysAddr, (char*)TestCodePhysAddr+TestCodeSize);
        my_puts(s, TRUE);
    }

    // check every page
    for ( ; p < pVirtEnd; p += 0x1000)
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
        if (PhysAddr >= (void*)0xA0000 && PhysAddr < (void*)0x100000)
        {
            my_sprintf(s, "\nPhysAddr(%x)=%x in A0000-100000 range\n", p, PhysAddr);
            my_puts(s, TRUE);
        }
        if (PhysAddr >= CurrentPhysProgramLocation
            && PhysAddr < (char*)CurrentPhysProgramLocation + MemoryInUseByProgram)
        {
            my_sprintf(s, "\nPhysAddr(%x)=%x in program range(%x-%x)\n", p, PhysAddr,
                       CurrentPhysProgramLocation, (char*)CurrentPhysProgramLocation + MemoryInUseByProgram);
            my_puts(s, TRUE);
        }
        // check that the page is mapped only once
        if (0)for (char * p1 = (char*)pVirtStart; p1 < pVirtEnd; p1+=0x1000)
        {
            if (GetPhysAddr(p1) == PhysAddr
                && p1 != p)
            {
                my_sprintf(s, "\nPhysAddr %x mapped at %x and %x\n", PhysAddr, p, p1);
                my_puts(s, TRUE);
            }
        }
    }
}
#endif

void __stdcall MemtestEntry()
{
    DWORD flags;
    DWORD seed = TestParams.RandomSeed;
    DWORD row_size = 0x1000;
    while(1)
    {
        // set new mapping to skip the program
        RelocateProgram();
        // map all left physical memory
        size_t MemoryToTestSize = MapMemoryToTest(CurrentPhysProgramLocation, MemoryInUseByProgram,
                                                  pMemoryToTestStart, pMemoryToTestEnd);
#ifdef _DEBUG
        VerifyPageTable(pMemoryToTestStart, pMemoryToTestEnd,
                        TestStartVirtAddr, MemoryToTestSize,
                        CurrentPhysProgramLocation, MemoryInUseByProgram);
#endif

        // perform test with different read pattern and with refresh check delay
        flags = TestFlags;
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

        if (TestParams.CpuType >= 486)
        {
            if (row_size == 0x80000 || (flags & TEST_FLAGS_WRITETHRU))
            {
                ModifyPageFlags(TestStartVirtAddr, MemoryToTestSize,
                                PAGE_DIR_FLAG_WRITETHROUGH, 0);
            }
            else
            {
                ModifyPageFlags(TestStartVirtAddr, MemoryToTestSize,
                                0, PAGE_DIR_FLAG_WRITETHROUGH);
            }

            if (row_size == 0x100000 || flags & TEST_FLAGS_NOCACHE)
            {
                ModifyPageFlags(TestStartVirtAddr, MemoryToTestSize,
                                PAGE_DIR_FLAG_NOCACHE, 0);
            }
            else
            {
                ModifyPageFlags(TestStartVirtAddr, MemoryToTestSize,
                                0, PAGE_DIR_FLAG_NOCACHE);
            }
        }
        else
        {
            // for 80386
            flags &= ~TEST_FLAGS_PREHEAT_MEMORY;
        }

        if (TestParams.RowSize != 0)
        {
            MemoryRowSize = TestParams.RowSize;
        }

        for (int DoDelay = 0; DoDelay < 2; DoDelay++)
        {
            TestDelay = 0;
            if (TestPass & 1)
            {
                TestDelay = TestParams.ShortDelay;
            }
            if ((TestPass & 0x3E) == 0x3E)
            {
                TestDelay = TestParams.LongDelay;
            }

            if (TestFlags & TEST_FLAGS_PATTERN)
            {
                DoMemoryTestSpecialPattern(TestStartVirtAddr,
                                           MemoryToTestSize, TestParams.Pattern1, TestParams.Pattern2, flags);

                DoMemoryTestSpecialPattern(TestStartVirtAddr,
                                           MemoryToTestSize, TestParams.Pattern2, TestParams.Pattern1, flags);
            }
            else
            {
                seed = DoRandomMemoryTest(TestStartVirtAddr, MemoryToTestSize,
                    seed, 0x08080000, flags);

#ifndef _DEBUG
                DoMemoryTestAlternatePattern(TestStartVirtAddr,
                    MemoryToTestSize, flags | TEST_RUNNING0);

                DoMemoryTestAlternatePattern(TestStartVirtAddr,
                    MemoryToTestSize, flags | TEST_RUNNING1);

                DoMemoryTestUniformPattern(TestStartVirtAddr,
                    MemoryToTestSize, flags | TEST_ALL1);

                DoMemoryTestUniformPattern(TestStartVirtAddr,
                    MemoryToTestSize, flags | TEST_ALL0);

                DoMemoryTestUniformPattern(TestStartVirtAddr,
                    MemoryToTestSize, flags | TEST_ALL1);

                DoMemoryTestUniformPattern(TestStartVirtAddr,
                    MemoryToTestSize, flags | TEST_ALL0);

#endif
            }

            if (TestPass == TestParams.PassCount)
            {
                StoreResultAndReboot(TEST_RESULT_SUCCESS);    // success
            }
            TestPass++;
        }
        row_size *= 2;
        if (row_size > 0x100000)
            row_size = 0x1000;
    }
}

#pragma warning(disable: 4035)
inline __int64 ReadTSC()
{
    __asm {
        __emit 0x0f;
        __emit 0x31;
    }
}
#pragma warning(default: 4035)

void PrintMachinePerformance(MEMTEST_STARTUP_PARAMS * pTestParams)
{
    if ((pTestParams->CpuFeatures & CPUID_TIME_STAMP_COUNTER) == 0)
        return;
    // measure CPU clock rate
    Delay(10);
    DWORD cpu_clock = DWORD(ReadTSC());
    Delay(100);
    cpu_clock = DWORD(ReadTSC()) - cpu_clock;
    my_printf("CPU clock rate: %d MHz\n", (cpu_clock + 50000) / 100000);

    // measure L2 cache read/write speed
    MapVirtualToPhysical((void*)0x800000, (void*)0x800000, 0x400000);

    // reset nocache and writethrough flags
    ModifyPageFlags((void*)0x800000, 0x400000, 0,
                    PAGE_DIR_FLAG_NOCACHE | PAGE_DIR_FLAG_WRITETHROUGH);

    // preload L2 cache
    PreloadCache((void*)0x800000, 0x20000);
    DWORD rclock = DWORD(ReadTSC());
    int i;
    for (i = 0; i < 10; i++)
    {
        PreloadCache((void*)0x800000, 0x20000);
    }
    rclock = DWORD(ReadTSC()) - rclock;

    DWORD wclock = DWORD(ReadTSC());
    for (i = 0; i < 10; i++)
    {
        FillMemoryPattern((void*)0x800000, 0x20000, 0);
    }
    wclock = DWORD(ReadTSC()) - wclock;

    DWORD wpclock = DWORD(ReadTSC());
    for (i = 0; i < 10; i++)
    {
        FillMemoryPatternWriteAlloc((void*)0x800000, 0x20000, 0);
    }
    wpclock = DWORD(ReadTSC()) - wpclock;

    my_printf("L2 Cache speed: read=%d MB/s, write=%d MB/s, "
              "write/allocate=%d MB/s\n",
              (25 * (cpu_clock / 2)) / rclock,
              (25 * (cpu_clock / 2)) / wclock,
              (25 * (cpu_clock / 2)) / wpclock);

    // measure memory read/write speed
    rclock = DWORD(ReadTSC());
    for (i = 0; i < 10; i++)
    {
        PreloadCache((void*)0x800000, 0x400000);
    }
    rclock = DWORD(ReadTSC()) - rclock;

    wclock = DWORD(ReadTSC());
    for (i = 0; i < 10; i++)
    {
        FillMemoryPattern((void*)0x800000, 0x400000, 0);
    }
    wclock = DWORD(ReadTSC()) - wclock;

    wpclock = DWORD(ReadTSC());
    for (i = 0; i < 10; i++)
    {
        FillMemoryPatternWriteAlloc((void*)0x800000, 0x400000, 0);
    }
    wpclock = DWORD(ReadTSC()) - wpclock;

    my_printf("Main memory speed: read=%d MB/s, write=%d MB/s, "
              "write/allocate=%d MB/s\n",
              (25 * (cpu_clock / 2)) / (rclock / 32),
              (25 * (cpu_clock / 2)) / (wclock / 32),
              (25 * (cpu_clock / 2)) / (wpclock / 32));
    // find how much memory is cached
}

// InitMemtest returns address of new stack
char * InitMemtest(MEMTEST_STARTUP_PARAMS * pTestParams)
{
    TestParams = * pTestParams;
    // pTestParams is a pointer to test parameters,
    // obtained by startup module from system configuration
    // and command line options.

    // get current cursor position
    curr_row = pTestParams->CursorRow;


    if (TestParams.PassCount != 0)
    {
        RebootOnFinish = TRUE;
    }

    TestFlags |= pTestParams->Flags
                 & (TEST_READ_TWICE | TEST_FLAGS_PATTERN | TEST_FLAGS_128BIT);

    if (pTestParams->Flags & TEST_FLAGS_NOPREFETCH)
    {
        TestFlags &= ~(TEST_PRELOAD_CACHE1 | TEST_PRELOAD_CACHE2);
    }

#ifdef _DEBUG
    srand(pTestParams->RandomSeed);
#endif

    if ((pTestParams->CpuFeatures & CPUID_4MB_PAGES)
        && 0 == (pTestParams->Flags & TEST_FLAGS_NOLARGEPAGES))
    {
        f4MBPagesSupported = TRUE;
        // enable 4MB page size extension in CR4
        __asm {
            //mov     eax,cr4
            __emit 0x0F __asm __emit 0x20 __asm __emit 0xE0
            or      eax,CR4_4MB_PAGES_ENABLED
            //mov     cr4,eax
            __emit 0x0F __asm __emit 0x22 __asm __emit 0xE0
        }
    }

    // Detect installed memory size
    TopVirtualAddress = (void*)0x800000;   // 8 MB

    _asm {
        mov     eax,CR3
        mov     PageTablePtr,eax
    }

    PageTableOffset = 0;    // physical address is the same as virtual
    // test the memory starting from 100000 (or lower address to test)
    // to the last address found (or higher address to test)

    DetectInstalledMemory( & TestParams);

    if (TestParams.MemoryTop < 0x800000)
    {
        my_puts("Too little memory installed (< 8M).\n"
                "Press RESET button"
                " or turn power off then on to restart the computer.", FALSE, 0x0F00);
        while(1) CheckForKey();
    }
    pMemoryToTestStart = (char*)TestParams.MemoryStart;
    pMemoryToTestEnd = (char*)TestParams.MemoryTop;

    my_puts("To terminate test and restart the computer,\n"
            "press Ctrl+Alt+Del or RESET button, or turn power off then on\n", FALSE);

    my_printf("Memory to test: %x to %x (%d megabytes)\n",
              TestParams.MemoryStart, TestParams.MemoryTop,
              (TestParams.MemoryTop - TestParams.MemoryStart) >> 20);

    // allocate extra memory after the program for stack, TSS, GDT, IDT
    TopProgramAddress = (char*)(TestParams.ProgramTop);
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
    DWORD * pPageDirectory = (DWORD*)(DWORD(TopProgramAddress + 0xFFF) & ~0xFFF);
    // create new page table
    PageTableSize = 0x1000 + (pMemoryToTestEnd - pMemoryToTestStart + 0x800000U) / 0x400;
    PageTableSize = (PageTableSize + 0xFFF) & ~0xFFF;

    TopUsedAddress = PageTableSize + (char*)pPageDirectory;
    MemoryInUseByProgram = DWORD(TopUsedAddress) - 0x400000;
    CurrentPhysProgramLocation = NULL;

    InitPageTable(pPageDirectory, PageTableSize);
    TopVirtualAddress = (void *)((PageTableSize - 0x1000) * 0x400);

    // init new GDT
    // 32 bit data and stack segment sescriptor
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
    __asm lidt fptr.len;

    // switch to new TSS (no values is loaded yet from it)
    __asm mov eax, 10 * 8
    __asm ltr ax

    // switch to new page table
    PageTablePtr = pPageDirectory;
    DWORD * PhysPageTablePtr = (DWORD*)GetPhysAddr(pPageDirectory);
    PageTableOffset = pPageDirectory - PhysPageTablePtr;

    SwitchPageTable(PhysPageTablePtr);

    // init system timer to 10 ms rounds
    _outp(0x43, 0x34);  // command
    _outp(0x40, char(1193180 / 100));
    _outp(0x40, char((1193180 / 100) >> 8));

#ifndef _DEBUG
    if (TestParams.Flags & TEST_FLAGS_PERFORMANCE)
#endif
    {
        my_printf("CPU class: %d, feature word: %x\n",
                  TestParams.CpuType, TestParams.CpuFeatures);
    }

    if (TestParams.CpuFeatures & CPUID_MACHINE_CHECK_EXCEPTION
        && 0 == (TestParams.Flags & TEST_NO_MACHINE_CHECK))
    {
        if (TestParams.CpuFeatures & CPUID_MACHINE_CHECK_ARCHITECTURE)
        {
            // initialize machine check
            __asm {
                mov     ecx,MCG_CAP
                RDMSR
                test    eax,MCG_CTL_PRESENT
                jz      no_mcg_ctl

                push    eax
                mov     ecx,MCG_CTL
                mov     eax,-1
                mov     edx,eax
                WRMSR
                pop     eax
no_mcg_ctl:
                push    eax
                mov     ecx,0x401       // MC0_STATUS
init_mci_sts:
                sub     al,1
                jb      no_mci_sts
                push    eax
                xor     eax,eax
                xor     edx,edx
                WRMSR
                pop     eax
                add     ecx,4
                jmp     init_mci_sts
no_mci_sts:
                pop     eax

                mov     ecx,0x404       // MC1_CTL
init_mci_ctl:
                sub     al,1
                jbe      no_mci_ctl
                push    eax
                mov     eax,-1
                mov     edx,eax
                WRMSR
                pop     eax
                add     ecx,4
                jmp     init_mci_ctl
no_mci_ctl:
            }
#ifndef _DEBUG
            if (TestParams.Flags & TEST_FLAGS_PERFORMANCE)
#endif
            {
                my_puts("Machine Check Architecture enabled\n", FALSE);
            }
        }
        __asm {
            //mov     eax,cr4
            __emit 0x0F __asm __emit 0x20 __asm __emit 0xE0
            or      eax,CR4_MACHINE_CHECK_ENABLED
            //mov     cr4,eax
            __emit 0x0F __asm __emit 0x22 __asm __emit 0xE0
        }
#ifndef _DEBUG
        if (TestParams.Flags & TEST_FLAGS_PERFORMANCE)
#endif
        {
            my_puts("Machine Check Exception enabled\n", FALSE);
        }
    }

    if (TestParams.Flags & TEST_FLAGS_PERFORMANCE)
    {
        PrintMachinePerformance(& TestParams);
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
            const char Msg[] = "This program can't be run under Windows or other "
                               "Win32 environment.\n"
                               "Run the program from bare DOS.\n"
                               "\nTo start bare DOS session, restart the computer\n"
                               "and press Shift-F5 when \"Starting Windows95...\" message appears,\n"
                               "or boot from a diskette with system files only."
            ;
            WriteFile(hConsole, title, (sizeof title) -1, &temp, NULL);
            WriteFile(hConsole, Msg, (sizeof Msg)-1, & temp, NULL);
        }
        ExitProcess(255);
    }

    // DS privilege is Ring0. The program is running as a standalone module
    char * NewStack;
    NewStack = InitMemtest(pTestParams);

    // switch to new stack (current local variables no more available!)
    __asm   mov     esp,NewStack
    // call main memtest function
    MemtestEntry();
    while(1);
}

inline void WriteBackAndInvalidate()
{
    if (TestParams.CpuType >= 486)
    {
        // Encode WBINVD manually.
        // MSVC 4.2 incorrectly emits WBINVD inctruction
        __asm __emit 0x0f __asm __emit 0x09
    }
}

    // convert physical to virtual
void * GetPhysAddr(void * VirtAddr)
{
#ifdef _DEBUG
    if (VirtAddr >= TopVirtualAddress)
    {
        my_puts("Error In GetPhysAddr", FALSE);
        while(1);
    }
#endif
    // parse page table manually
    DWORD Addr = (DWORD) VirtAddr;
    DWORD TableDirectoryEntry = PageTablePtr[Addr >> 22];
    if (TableDirectoryEntry & PAGE_4M)
    {
        // page is 4 megabyte
        return (void *)((TableDirectoryEntry & 0xFFC00000) +
                        (Addr & 0x003FFFFF));
    }
    else
    {
        DWORD * PageDirectory =
            ((DWORD *)(TableDirectoryEntry & 0xFFFFF000))
            + PageTableOffset;
        DWORD PageDirectoryEntry =
            PageDirectory[(Addr & 0x3FF000) >> 12];
        return (void*)((PageDirectoryEntry & 0xFFFFF000) +
                       (Addr & 0x00000FFF));
    }
}

void MapVirtualToPhysical(void * VirtAddr, void * PhysAddr, size_t size)
{
#ifdef _DEBUG
    if (VirtAddr >= TopVirtualAddress
        || DWORD(VirtAddr) & 0xFFF
        || DWORD(PhysAddr) & 0xFFF
        || size & 0xFFF)
    {
        my_puts("Error In MapVirtualToPhysical", FALSE);
        while(1);
    }
#endif
    while(size)
    {
        // if 4MB pages are supported, use this feature for 4MB
        // aligned regions
        if (f4MBPagesSupported && size >= 0x400000
            && (DWORD(VirtAddr) & 0x3FFFFF) == 0
            && (DWORD(PhysAddr) & 0x3FFFFF) == 0)
        {
            // PageNum: index of 4 MB page
            DWORD PageNum = DWORD(VirtAddr) >> 22;
            // modify table directory entry
            PageTablePtr[PageNum] = (PageTablePtr[PageNum] & 0xFFF)
                                    | PAGE_4M | DWORD(PhysAddr);
            // modify also page directory, just for case if page
            // size will change afterwards
            // pPageDir: virtual address of page directory
            // Page directories are placed just after table directory
            DWORD * pPageDir = PageTablePtr
                               + 1024 * (PageNum + 1);
            for (int i = 0; i < 0x400; i++, pPageDir++) // 1024 pages
            {
                pPageDir[i] = (pPageDir[i] & 0xFFF) | DWORD(PhysAddr);
                __asm
                {
                    MOV     eax,VirtAddr
                    INVLPG  [eax]
                }
                VirtAddr = 0x1000 + (char*)VirtAddr;
                PhysAddr = 0x1000 + (char*)PhysAddr;
            }
            size -= 0x400000;
        }
        else
        {
            // TableNum: index of page directory
            DWORD TableNum = DWORD(VirtAddr) >> 22;
            // pPageDir: address of page directory
            DWORD * pPageDir = PageTablePtr
                               + 0x400 * (TableNum + 1);
            // restore table directory entry to make it pointing
            // to the page directory
            PageTablePtr[TableNum] = (PageTablePtr[TableNum]
                                         & (0xFFF & ~PAGE_4M)) | DWORD(pPageDir - PageTableOffset);
            pPageDir += (DWORD(VirtAddr) & 0x003FF000) >> 12;
            *pPageDir = (*pPageDir & 0x00000FFF) | DWORD(PhysAddr);
            __asm
            {
                MOV     eax,VirtAddr
                INVLPG  [eax]
            }
            VirtAddr = 0x1000 + (char*)VirtAddr;
            PhysAddr = 0x1000 + (char*)PhysAddr;
            size -= 0x1000;
        }
    }
}

// change mapping in non-active page table being initialized
void InitVirtualToPhysical(void * VirtAddr, void * PhysAddr, size_t size,
                           DWORD * pPageTable)
{
#ifdef _DEBUG
    if (VirtAddr >= TopVirtualAddress
        || DWORD(VirtAddr) & 0xFFF
        || DWORD(PhysAddr) & 0xFFF
        || DWORD(pPageTable) & 0xFFF
        || size & 0xFFF)
    {
        my_puts("Error In InitVirtualToPhysical", FALSE);
        while(1);
    }
#endif
    while(size)
    {
        // if 4MB pages are supported, use this feature for 4MB
        // aligned regions
        if (f4MBPagesSupported && size >= 0x400000
            && (DWORD(VirtAddr) & 0x3FFFFF) == 0
            && (DWORD(PhysAddr) & 0x3FFFFF) == 0)
        {
            // PageNum: index of 4 MB page
            DWORD PageNum = DWORD(VirtAddr) >> 22;
            // modify table directory entry
            pPageTable[PageNum] = (pPageTable[PageNum] & 0xFFF)
                                  | PAGE_4M | DWORD(PhysAddr);
            // modify also page directory, just for case if page
            // size will change afterwards
            // pPageDir: virtual address of page directory
            // Page directories are placed just after table directory
            DWORD * pPageDir = pPageTable
                               + 1024 * (PageNum + 1);
            for (int i = 0; i < 0x400; i++, pPageDir++) // 1024 pages
            {
                pPageDir[i] = (pPageDir[i] & 0xFFF) | DWORD(PhysAddr);
                VirtAddr = 0x1000 + (char*)VirtAddr;
                PhysAddr = 0x1000 + (char*)PhysAddr;
            }
            size -= 0x400000;
        }
        else
        {
            // TableNum: index of page directory
            DWORD TableNum = DWORD(VirtAddr) >> 22;
            // pPageDir: address of page directory
            DWORD * pPageDir = pPageTable
                               + 1024 * (TableNum + 1);
            // restore table directory entry to make it pointing
            // to the page directory
            pPageTable[TableNum] = (pPageTable[TableNum]
                                       & (0xFFF & ~PAGE_4M)) | DWORD(GetPhysAddr(pPageDir));
            pPageDir += (DWORD(VirtAddr) & 0x003FF000) >> 12;
            *pPageDir = (*pPageDir & 0x00000FFF) | DWORD(PhysAddr);
            VirtAddr = 0x1000 + (char*)VirtAddr;
            PhysAddr = 0x1000 + (char*)PhysAddr;
            size -= 0x1000;
        }
    }
}

DWORD GetPageFlags(void * VirtAddr)
{
#ifdef _DEBUG
    if (VirtAddr >= TopVirtualAddress
        || DWORD(VirtAddr) & 0xFFF)
    {
        my_puts("Error In GetPageFlags", FALSE);
        while(1);
    }
#endif
    // TableNum: index of 4 GB page
    DWORD TableNum = DWORD(VirtAddr) >> 22;
    // pPageDir: virtual address of page directory
    DWORD * pPageDir = PageTablePtr
                       + 1024 * (TableNum + 1);
    if (PageTablePtr[TableNum] & PAGE_4M)
    {
        // modify table directory entry
        return PageTablePtr[TableNum] & 0xFFF & ~PAGE_4M;
    }
    else
    {
        // restore table directory entry to make it pointing
        // to the page directory
        pPageDir += (DWORD(VirtAddr) & 0x003FF000) >> 12;
        return *pPageDir & 0xFFF;
    }
}

void ModifyPageFlags(void * VirtAddr, size_t size, DWORD SetFlags,
                     DWORD ResetFlags)
{
#ifdef _DEBUG
    if (VirtAddr >= TopVirtualAddress
        || DWORD(VirtAddr) & 0xFFF
        || size & 0xFFF
        || SetFlags & ~0xE7F
        || ResetFlags & ~0xE7F)
    {
        my_puts("Error In ModifyPageFlags", FALSE);
        while(1);
    }
#endif
    while(size)
    {
        // TableNum: index of 4 GB page
        DWORD TableNum = DWORD(VirtAddr) >> 22;
        // pPageDir: virtual address of page directory
        DWORD * pPageDir = PageTablePtr
                           + 1024 * (TableNum + 1);
        if (f4MBPagesSupported && size >= 0x400000
            && (DWORD(VirtAddr) & 0x3FFFFF) == 0)
        {
            // modify table directory entry
            PageTablePtr[TableNum] =
                (PageTablePtr[TableNum] & ~ResetFlags) | SetFlags;
            // modify also page directory, just for case if page
            // size will change afterwards
            // Page directories are placed just after table directory
            for (int i = 0; i < 0x400; i++, pPageDir++) // 1024 pages
            {
                pPageDir[i] =
                    (pPageDir[i] & ~ResetFlags) | SetFlags;
                __asm
                {
                    MOV     eax,VirtAddr
                    INVLPG  [eax]
                    jmp     label1  // reset pipelines
label1:
                }
                VirtAddr = 0x1000 + (char*)VirtAddr;
            }
            size -= 0x400000;
        }
        else
        {
            // restore table directory entry to make it pointing
            // to the page directory
            PageTablePtr[TableNum] = (PageTablePtr[TableNum]
                                         & (0xFFF & ~PAGE_4M)) | DWORD(pPageDir - PageTableOffset);
            pPageDir += (DWORD(VirtAddr) & 0x003FF000) >> 12;
            *pPageDir = (*pPageDir & ~ResetFlags) | SetFlags;
            __asm
            {
                MOV     eax,VirtAddr
                INVLPG  [eax]
                jmp     label2  // reset pipelines
label2:
            }
            VirtAddr = 0x1000 + (char*)VirtAddr;
            size -= 0x1000;
        }
    }
}

// virtual memory map:
// 00000...400000 - low physical memory (including A0000-FFFFF)
// 400000...4FF000 - program and stack
// 4FF000...800000 - page tables
// 800000 and up - memory to test (max 3 GB).
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
    if (DWORD(pMemoryToTestStart) < 0x400000 || NULL == CurrentPhysProgramLocation)
    {
        if (MemoryInUseByProgram < 0x100000)    // 1MB
        {
            CurrentPhysProgramLocation = 0x100000 + (char*)CurrentPhysProgramLocation;
            if (DWORD(CurrentPhysProgramLocation) >
                DWORD(pMemoryToTestEnd) - 0x100000)
            {
                CurrentPhysProgramLocation = (void*)0x100000;
            }
        }
        else
        {
            if (NULL == CurrentPhysProgramLocation)
            {
                CurrentPhysProgramLocation = (void*)0x00800000;
            }
            CurrentPhysProgramLocation = 0x400000 + (char*)CurrentPhysProgramLocation;
            if (DWORD(CurrentPhysProgramLocation) >
                DWORD(pMemoryToTestEnd) - 0x400000)
            {
                CurrentPhysProgramLocation = (void*)0x00800000;
            }
        }

#ifdef _DEBUG
        my_printf("\rRelocating the program to %X\n", CurrentPhysProgramLocation);
#endif
        MapVirtualToPhysical((void*)0x800000, CurrentPhysProgramLocation,
                             MemoryInUseByProgram);
        DWORD * pNewPageDirectory = PageTablePtr + 0x100000; // 1M DWORDS=4 MB up

        // Init and copy page directory
        InitPageTable(pNewPageDirectory, PageTableSize);
        InitVirtualToPhysical((void*)0x400000, CurrentPhysProgramLocation,
                              MemoryInUseByProgram, pNewPageDirectory);

        // switch to new page directory
        DWORD * pNewPhysTablePtr = (DWORD*)GetPhysAddr(pNewPageDirectory);
        PageTableOffset = PageTablePtr - pNewPhysTablePtr;

        // copy program code and data segment
        memcpy((void *) 0x800000, (void *) 0x400000,
               DWORD(TopProgramAddress) - 0x400000);

        SwitchPageTable(pNewPhysTablePtr);
    }
}

// function does not change program area mapping
size_t MapMemoryToTest(void * ProgramRegion, size_t ProgramRegionSize,
                       void * PhysMemoryBottom, void * PhysMemoryTop)
{
#ifdef _DEBUG
    if (ProgramRegionSize > 0x400000
        || DWORD(PhysMemoryBottom) & 0xFFFFF
        || DWORD(ProgramRegion) & 0xFFFFF
        || DWORD(PhysMemoryTop) & 0xFFFFF)
    {
        my_puts("Error In MapMemoryToTest", FALSE);
        while(1);
    }
#endif
    DWORD CurrVirtAddr = DWORD(TestStartVirtAddr);
    DWORD CurrPhysAddr = 0x400000;  // 4 MB

    if (CurrPhysAddr < DWORD(PhysMemoryBottom))
    {
        CurrPhysAddr = DWORD(PhysMemoryBottom);
    }

    while(CurrPhysAddr + 0x400000 <= DWORD(PhysMemoryTop))
    {
        if (CurrPhysAddr >= DWORD(ProgramRegion) + ProgramRegionSize
            || CurrPhysAddr + 0x400000 <= DWORD(ProgramRegion))
        {
            MapVirtualToPhysical(PVOID(CurrVirtAddr), PVOID(CurrPhysAddr),
                                 0x400000);
            CurrVirtAddr += 0x400000;   // 4MB
        }
        CurrPhysAddr += 0x400000;   // 4MB
    }

    // map incomplete top megabytes (if any)
    // don't map if the program is above this address
    if(CurrPhysAddr < DWORD(PhysMemoryTop)
        && CurrPhysAddr >= DWORD(ProgramRegion) + ProgramRegionSize)
    {
        size_t ToMap = DWORD(PhysMemoryTop) - CurrPhysAddr;
        MapVirtualToPhysical(PVOID(CurrVirtAddr), PVOID(CurrPhysAddr),
                             ToMap);
        CurrVirtAddr += ToMap;
    }
    // map 0x100000-0x400000
    if (DWORD(PhysMemoryBottom) < 0x400000
        && DWORD(ProgramRegion) >= 0x400000)
    {
        CurrPhysAddr = 0x100000;    // 1MB
        if (CurrPhysAddr < DWORD(PhysMemoryBottom))
            CurrPhysAddr = DWORD(PhysMemoryBottom);
        size_t ToMap = 0x400000 - CurrPhysAddr;
        MapVirtualToPhysical(PVOID(CurrVirtAddr), PVOID(CurrPhysAddr),
                             ToMap);
        CurrVirtAddr += ToMap;
    }
    // map spare MBs from program 4MB page
    if (ProgramRegionSize < 0x400000)
    {
        // round the address to 4 MB boundary
        DWORD ProgramPage = DWORD(ProgramRegion) & ~0x3FFFFF;
        CurrPhysAddr = ProgramPage;
        if (DWORD(PhysMemoryBottom) <= ProgramPage)
        {
            // don't map lowest memory here
            if (00000 == CurrPhysAddr)
            {
                CurrPhysAddr = 0x100000;
            }
            for (; CurrPhysAddr < ProgramPage + 0x400000; CurrPhysAddr += 0x100000)
            {
                if ((CurrPhysAddr >= DWORD(ProgramRegion) + ProgramRegionSize
                        || CurrPhysAddr + 0x100000 <= DWORD(ProgramRegion))
                    && CurrPhysAddr+0x100000 <= DWORD(PhysMemoryTop))
                {
                    MapVirtualToPhysical(PVOID(CurrVirtAddr),
                                         PVOID(CurrPhysAddr), 0x100000);
                    CurrVirtAddr += 0x100000;
                }
            }
        }
    }
    // map 0x00000-0xA0000
    if (PhysMemoryBottom == 0x00000)
    {
        MapVirtualToPhysical(PVOID(CurrVirtAddr), 0x000000,
                             0xA0000);
        CurrVirtAddr += 0xA0000;
    }

    return CurrVirtAddr - DWORD(TestStartVirtAddr);
}

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
    int DwordCount = 0x1000 / 4;
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
    void * const check_addr = (void *) 0x7FF000;  // 7MB-4K
    DWORD ZeroPageFlags = GetPageFlags(NULL);
    DWORD TargetPageFlags = GetPageFlags(check_addr);

    // set zero and target page attributes to non-cacheable
    if (TestParams.CpuType >= 486)
    {
        ModifyPageFlags(0, 0x1000, PAGE_CACHE_DISABLE, 0);
        ModifyPageFlags(check_addr, 0x1000, PAGE_CACHE_DISABLE, 0);
    }

    ModifyPageFlags(check_addr, 0x1000,
                    PAGE_DIR_FLAG_PRESENT | PAGE_DIR_FLAG_WRITABLE
                    | PAGE_DIR_FLAG_ACCESSED | PAGE_DIR_FLAG_DIRTY, 0);

    WriteBackAndInvalidate();

    if (NULL == pTestParams->MemoryTop)
    {
        pTestParams->MemoryTop = 0xC0000000;    // 3GB
    }
    char * pStart = (char *)0x100000;   // 1MB
    if (DWORD(pStart) < pTestParams->MemoryStart)
    {
        pStart = (char*)(pTestParams->MemoryStart);
    }
    for(; DWORD(pStart) < pTestParams->MemoryTop; pStart += 0x1000)
    {
        if ((DWORD(pStart) & 0xFFFFF) == 0)
        {
            my_printf("Detecting physical memory, %dM\r", DWORD(pStart) >> 20);
        }
        MapVirtualToPhysical(check_addr, pStart, 0x1000);

        if (! TestPageForPresence(check_addr, pTestParams->Flags & TEST_FLAGS_FASTDETECT))
            break;
    }

    pTestParams->MemoryTop = DWORD(pStart);
    if (TestParams.CpuType >= 486)
    {
        if (0 == (ZeroPageFlags & PAGE_CACHE_DISABLE))
        {
            ModifyPageFlags(0, 0x1000, 0, PAGE_CACHE_DISABLE);
        }
        if (0 == (TargetPageFlags & PAGE_CACHE_DISABLE))
        {
            ModifyPageFlags(check_addr, 0x1000, 0, PAGE_CACHE_DISABLE);
        }
    }
}

void InitPageTable(void * VirtPageDirAddress, size_t BufSize)
{
#ifdef _DEBUG
    if (DWORD(VirtPageDirAddress) & 0xFFF
        || BufSize & 0xFFF
        || BufSize < 0x3000
        || BufSize > 0x401000)
    {
        my_puts("Error in InitPageTable\n", FALSE);
        while(1);
    }
#endif

    DWORD * TableDir = (DWORD*) VirtPageDirAddress;
    memset(TableDir, 0, 0x1000);
    BufSize -= 0x1000;
    DWORD * PageDir = TableDir + 0x400;

    for (; BufSize != 0; BufSize -= 0x1000, PageDir += 0x400, TableDir++)
    {
        * TableDir = DWORD(GetPhysAddr(PageDir)) |
                     (PAGE_DIR_FLAG_PRESENT | PAGE_DIR_FLAG_WRITABLE
                         | PAGE_DIR_FLAG_ACCESSED | PAGE_DIR_FLAG_DIRTY);

        for (int i = 0; i < 0x400; i++)
        {
            PageDir[i] = PAGE_DIR_FLAG_PRESENT | PAGE_DIR_FLAG_WRITABLE
                         | PAGE_DIR_FLAG_ACCESSED | PAGE_DIR_FLAG_DIRTY;
        }
    }

    // map lower 8 MB as currently is
    for (char * addr = 0;DWORD(addr) < 0x800000; addr += 0x1000)
    {
        InitVirtualToPhysical(addr, GetPhysAddr(addr), 0x1000,
                              (DWORD*)VirtPageDirAddress);
    }
}

void InitGate(GATE & g, WORD Selector, void * Offset, WORD flags)
{
    g.offset_0_15 = WORD(Offset);
    g.selector = Selector;
    g.offset_16_31 = WORD(DWORD(Offset) >> 16);
    g.flags = flags;
}

static char buf[256];

void __cdecl PrintException(const char * pFormat)
{
    DWORD * pArgs = (DWORD*) & pFormat;
    my_sprintf(buf, "\nEIP: %x, EFLAGS: %x, EAX: %x, EBX: %x, ECX: %x,\n"
               "EDX: %x, ESI: %x, EDI: %x, EBP: %x\n",
               pArgs[8], pArgs[10], pArgs[1], pArgs[2], pArgs[3], pArgs[4],
               pArgs[5], pArgs[6], pArgs[7]);
    my_puts(buf, FALSE, 0x0F00);
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
