// memtest.h

#define MEMTEST_VERSION "1.03"
#define MEMTEST_DATE    "March 28, 1999"
#define MEMTEST_COPYRIGHT "Copyright 1997-1999 Alexander Grigoriev (AleGr SoftWare), alegr@aha.ru"
#define MEMTEST_TITLE   "MEMTEST - PC memory test, Version " MEMTEST_VERSION \
            ", " MEMTEST_DATE \
            ". All Rights Reserved\n" \
               MEMTEST_COPYRIGHT "\n"

#define TEST_ALL1                  0x00000001UL
#define TEST_ALL0                  0x00000002UL
#define TEST_PRELOAD_CACHE1        0x00000004UL // preload L1 cache
#define TEST_PRELOAD_CACHE2        0x00000008UL // preload L2 cache
#define TEST_USE_OFFSET            0x00000010UL    // test using DRAM row offset
#define TEST_USE_RANDOM            0x00000020UL    // use random pattern, dwPattern1 - seed, dwPattern2 - poly
#define TEST_CHECK_DIFFERENT_BURST 0x00000040UL // check with different burst sequences
#define TEST_EMPTY_CACHE           0x00000080UL
#define TEST_RUNNING0              0x00000100UL   // perform running 0 test
#define TEST_RUNNING1              0x00000200UL   // perform running 1 test
#define TEST_DELAY                 0x00000400UL   // do delay between write and read (to check refresh)
#define TEST_SEESAW                0x00000800UL   // place stress on rows
#define TEST_READ_TWICE            0x00001000UL  // read memory twice
#define TEST_REPLACE               0x00002000UL  // replace compared test data with new pattern
#define TEST_FLAGS_PERFORMANCE     0x00004000UL   // print processor type,
                                    // features, memory performance
#define TEST_FLAGS_WRITETHRU       0x00008000UL  // disable writeback cache
#define TEST_FLAGS_NOCACHE         0x00010000UL
#define TEST_NO_MACHINE_CHECK      0x00020000UL
#define TEST_FLAGS_PREHEAT_MEMORY  0x00040000UL  // preheat memory chips
                                    // by placing big load on them
#define TEST_NO_USB_LEGACY         0x00080000UL

// test results:
#define TEST_RESULT_SUCCESS         0x60
#define TEST_RESULT_CTRL_ALT_DEL    0x61
#define TEST_RESULT_MEMORY_ERROR    0x62
#define TEST_RESULT_CRASH           0x63

// RTC registers:
#define RTC_SECONDS       0
#define RTC_SECOND_ALARM  1
#define RTC_MINUTES       2
#define RTC_MINUTES_ALARM 3
#define RTC_HOURS         4
#define RTC_HOURS_ALARM   5
#define RTC_REGISTER_B      0x0B

#define PAGE_DIR_FLAG_PRESENT       1
#define PAGE_DIR_FLAG_WRITABLE      2
#define PAGE_DIR_FLAG_USER          4
#define PAGE_DIR_FLAG_WRITETHROUGH  8
#define PAGE_DIR_FLAG_NOCACHE       0x10
#define PAGE_DIR_FLAG_ACCESSED      0x20
#define PAGE_DIR_FLAG_DIRTY         0x40

#define SEG_DESC_4K_GRANULARITY     0x00800000UL
#define SEG_DESC_32BIT              0x00400000UL
#define SEG_DESC_PRESENT            0x00008000UL
#define SEG_DESC_APPLICATION_TYPE   0x00001000UL
#define SEG_DESC_ACCESSED           0x00000100UL
#define SEG_DESC_EXPAND_DOWN        0x00000400UL
#define SEG_DESC_NORMAL    (SEG_DESC_PRESENT | SEG_DESC_APPLICATION_TYPE \
    | SEG_DESC_ACCESSED)

#define SEG_DESC_READONLY           0x00001000UL
#define SEG_DESC_READWRITE          0x00001200UL
#define SEG_DESC_CODE_READ          0x00001A00UL
#define SEG_DESC_CODE_NOREAD        0x00001800UL

#define SEG_DESC_DATA (SEG_DESC_PRESENT | SEG_DESC_APPLICATION_TYPE \
    | SEG_DESC_ACCESSED | SEG_DESC_READWRITE)

#define SEG_DESC_CODE (SEG_DESC_PRESENT | SEG_DESC_APPLICATION_TYPE \
    | SEG_DESC_ACCESSED | SEG_DESC_CODE_READ)

#define SEG_DESC_DATA_32BIT (SEG_DESC_PRESENT | SEG_DESC_APPLICATION_TYPE \
    | SEG_DESC_ACCESSED | SEG_DESC_READWRITE \
    | SEG_DESC_4K_GRANULARITY | SEG_DESC_32BIT)

#define SEG_DESC_CODE_32BIT (SEG_DESC_PRESENT | SEG_DESC_APPLICATION_TYPE \
    | SEG_DESC_ACCESSED | SEG_DESC_CODE_READ \
    | SEG_DESC_4K_GRANULARITY | SEG_DESC_32BIT)

#define SEG_DESC_TSS_32BIT (SEG_DESC_PRESENT | SEG_DESC_ACCESSED | 0x00000900L)

#define GATE_PRESENT             0x8000
#define GATE_INTERRUPT_GATE      0x0E00
#define GATE_INTERRUPT_GATE16    0x0600
#define GATE_TRAP_GATE           0x0F00
#define GATE_TRAP_GATE16         0x0700

struct _TSS
{
    WORD _old_tss;
    WORD dummy11;
    DWORD _esp0;
    WORD _ss0;
    WORD dummy10;
    DWORD _esp1;
    WORD _ss1;
    WORD dummy9;
    DWORD _esp2;
    WORD _ss2;
    WORD dummy8;
    DWORD _cr3;
    DWORD _eip;
    DWORD _eflags;
    DWORD _eax;
    DWORD _ecx;
    DWORD _edx;
    DWORD _ebx;
    DWORD _esp;
    DWORD _ebp;
    DWORD _esi;
    DWORD _edi;
    WORD _es;
    WORD dummy7;
    WORD _cs;
    WORD dummy6;
    WORD _ss;
    WORD dummy5;
    WORD _ds;
    WORD dummy4;
    WORD _fs;
    WORD dummy3;
    WORD _gs;
    WORD dummy2;
    WORD _ldt;
    WORD dummy1;
    WORD dummy0;
    WORD IOMapOffset;
    BYTE IOMap[128];
};

struct DESCRIPTOR
{
    WORD limit_0_15;
    WORD base_0_15;
    union {
        struct {
            BYTE base_16_23;
            BYTE flags1;
            BYTE flags2_limit_16_19;
            BYTE base_24_31;
        };
        DWORD flags;
    };
};

struct PSEUDO_DESC
{
    WORD dummy;
    WORD len;
    void * ptr;
};

struct GATE
{
    WORD offset_0_15;
    WORD selector;
    WORD flags;
    WORD offset_16_31;
};

struct PROTECTED_MODE_STARTUP_MEMORY
{
    DWORD TableDir[1024];
    DWORD PageDir0[1024];
    DWORD PageDir1[1024];
    GATE IDT[256];
    _TSS TSS;
    DESCRIPTOR GDT[12];
    char stack[0x2000];
    DWORD StackTop; // dummy return address
    DWORD StartupArgument;
};

struct MEMTEST_STARTUP_PARAMS
{
    WORD CpuType;       // 386, 486, 586, 686
    WORD CpuFeatures;   // CPUID info
    DWORD MemoryStart;  // begin of memory to test
    DWORD MemoryTop;    // end of memory to test
    DWORD ProgramTop;   // top virtual address of memory used by program code
    DWORD ShortDelay;
    DWORD LongDelay;
    DWORD RowSize;
    DWORD RandomSeed;
    DWORD Flags;
    WORD PassCount;
    WORD SMIEAddr;     // address of SMI Global Enable register in I/O space
    BYTE CursorRow;
    BYTE CursorColumn;
};

struct PROTECTED_MODE_STARTUP_DATA
{
    DWORD ProgramEntry;
    char far * pProgramBase;
    DWORD ProgramSize;
    MEMTEST_STARTUP_PARAMS msp;
};

static void InitDescriptor(DESCRIPTOR __far & d, DWORD flat_base, DWORD limit,
                           DWORD flags)
{
    d.flags = flags & 0x00D0FF00;
    d.base_0_15 = WORD(flat_base);
    d.base_16_23 = BYTE(flat_base >> 16);
    d.base_24_31 = BYTE(flat_base >> 24);
    if (d.flags & SEG_DESC_4K_GRANULARITY)
    {
        d.limit_0_15 = WORD(limit >> 12);
        d.flags2_limit_16_19 |= 0xF & (limit >> 28);
    }
    else
    {
        d.limit_0_15 = WORD(limit);
        d.flags2_limit_16_19 |= 0xF & (limit >> 16);
    }
}

#define RTC_INDEX_REG   0x70
#define RTC_DATA_REG    0x71

extern "C" void _enable();
extern "C" void _disable();
#pragma intrinsic(_enable, _disable)

static inline void WriteRTCReg(int Addr, BYTE Data)
{
    _disable();
    _outp(RTC_INDEX_REG, Addr & 0x7F);
    _outp(RTC_DATA_REG, Data);
    _enable();
}

static inline BYTE ReadRTCReg(int Addr)
{
    BYTE tmp;
    _disable();
    _outp(RTC_INDEX_REG, Addr & 0x7F);
    tmp = _inp(RTC_DATA_REG);
    _enable();
    return tmp;
}
