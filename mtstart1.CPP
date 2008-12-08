#include <stdio.h>
#include <string.h>
#include <dos.h>
#include <stdlib.h>
#include <time.h>
#include <conio.h>

#pragma pack (1)

#include "mtstart.h"
#include "memtest.h"

BOOL IsV86();

#ifdef _DEBUG
char fgetsbuf[30];
#endif

char title[] = MEMTEST_TITLE;
char help[] = "Command line: MEMTEST [<size> [<start>]] <options>\n"
              "<size>\t- optional size of memory to test (in megabytes)\n"
              "<start>\t- optional address to start testing from (in megabytes)\n"
              "Options are:\n"
              "/row <size> - memory chip row size;\n"
              "/delay <time1> <time2> - delays to test refresh quality in seconds;\n"
              "/speed\t- show processor and memory speed;\n"
              "/readtwice - compare test data twice;\n"
              "/writethru - disable write-back caching;\n"
              "/noprefetch - don't preload data to the cache;\n"
              "/preheat - perform memory preheat;\n"
              "/nocache - disable caching (not recommended);\n"
              "/nocheck - don't enable machine check interrupt;\n"
              "/nolargepages - don't use 4MB page mapping;\n"
              "/pattern:xxxxxxxx:xxxxxxxx - use the specified test pattern (hex code);\n"
              "/fastdetect - use fast memory size detection;\n"
              "/ignorexmm - start even if HIMEM.SYS is loaded\n"
              "        (if Windows ME recovery disk is booted).\n"
              "\nFor more details see README.HTM\n"
              "Check http://www.home.earthlink.net/~alegr/download/memtest.htm\n"
              " for program updates."
;

extern "C" long cpu_type();

// The following functions are allowed to use 8086 instructions only
BOOL IsWindowsRunning()
{
    // check for Windows running
    PAUSE("Checking for Windows, press Enter to continue...");

    __asm {
        mov     ax, 160Ah
        int     2Fh
        test    ax,ax
        jz      windows_running
        mov     ax, 1600h
        int     2Fh
        test    al,al
        jnz     windows_running
    }
    return FALSE;
windows_running:
    return TRUE;
}
char EMS_FILENAME[] = "EMMXXXX0";

BOOL IsEmm386Present()
{
    PAUSE("Checking for EMM386, press Enter to continue...");
    // check for EMS386 loaded
    __asm {
        SUB     CX,CX
        LEA     DX,[EMS_FILENAME]
        MOV     AX,3D00H
        INT     21H
        JC      NoEmm   // EMM not installed
        push    si
        MOV     SI,AX
        MOV     BX,AX
        MOV     AX,4400H
        INT     21H
        JC      NoEmm1
        TEST    DL,80H
        JZ      NoEmm1
        MOV     BX,SI
        MOV     AH,3EH
        INT     21H
        pop     si
        MOV     AX,4000H
        INT     67H
        TEST    AH,AH
        JNZ     NoEmm  // EMS not installed
    }
    return TRUE;
NoEmm1: // close opened handle
    __asm {
        MOV     BX,SI
        MOV     AH,3EH
        INT     21H
        pop     si
    }
NoEmm:
    return FALSE;
}

BOOL IsHimemPresent()
{
    PAUSE("Checking for HIMEM, press Enter to continue...");

    __asm {
        mov     ax, 0x4300
        int     0x2f
        cmp     al,0x80
        jnz     NoXms
    }
    return TRUE;
NoXms:
    return FALSE;
}

inline BOOL IsBareDOS(BOOL IgnoreXmm)
{
    if (IsWindowsRunning() || IsEmm386Present()
        || (! IgnoreXmm && IsHimemPresent()) || IsV86())
        return FALSE;
    else
        return TRUE;
}

DWORD GetSystemMemoryMap(SystemMemoryMapElement far * pMap, DWORD Offset_)
{

    __asm {
        push    di
        push    bx
        push    bp
        push    si
        //mov  eax,0x0000E820
        _emit 0x66 _asm _emit 0B8h _asm _emit 20h _asm _emit 0xE8 _asm _emit 0 _asm _emit 0
        //mov edx, 0x534D4150
        _emit 0x66 _asm mov dx,0x4150 _asm _emit 0x4D _asm _emit 0x53
        les di, pMap
        _emit 0x66 _asm mov bx, word ptr Offset_
        int     15h
        pop     si
        pop     bp
        jnc     good
        _emit 0x66 _asm xor     bx, bx
good:
        _emit 0x66 _asm mov word ptr Offset_, bx
        pop     bx
        pop     di
    }
    return Offset_;
}

void FillMemoryMap(PROTECTED_MODE_STARTUP_DATA * pStartup)
{
    unsigned i;
    DWORD Offset;
    SystemMemoryMapElement map;
    for (i = 0, Offset = 0; i < MAX_NUMBER_OF_MEMORY_RANGES; )
    {
        Offset = GetSystemMemoryMap(&map, Offset);
        TRACE("GetSystemMemoryMap returned base=%lX%08lX, length: %lX%08lX, type=%ld\n",
              map.BaseAddressHigh, map.BaseAddressLow,
              map.RangeSizeHigh, map.RangeSizeLow,
              map.RangeType);

        if (Offset == 0)
        {
            break;
        }
        if (map.RangeType != 1)
        {
            continue;
        }
        pStartup->msp.Map[i].BaseAddressHigh = map.BaseAddressHigh;
        pStartup->msp.Map[i].BaseAddressLow = map.BaseAddressLow;
        pStartup->msp.Map[i].RangeSizeHigh = map.RangeSizeHigh;
        pStartup->msp.Map[i].RangeSizeLow = map.RangeSizeLow;
        i++;
    }
    pStartup->msp.MemoryMapUsed = i;
}

void ResetFloppies()
{
    __asm   {
        mov     dx,0x3F2    // diskette digital output register
        mov     al,0
        // turn motors off
        out    dx,al
    }
}

void DisableUsbLegacy(MEMTEST_STARTUP_PARAMS * pParams)
{
    // find PCI device, vendor ID=0x8086, device ID=0x7113
    WORD DevNum;
    WORD IOAddr;
    __asm {
        push    si
        mov     ax,0xB102
        mov     dx,0x8086
        mov     cx,0x7113
        mov     si,0
        INT     0x1A
        pop     si
        JNC     device_found
        push    si
        mov     ax,0xB102
        mov     dx,0x8086
        mov     cx,0x7030
        mov     si,0
        INT     0x1A
        pop     si
        JC      device_not_found1
device_found:
        mov DevNum,bx
    }

    if ((DevNum & 7) != 3)
    {
        // function number should be 3
        return;
    }
    // read base address
    __asm {
        push    di
        mov     ax,0xB10A
        mov     bx,DevNum
        mov     di,0x40
        INT     0x1A
        pop     di
device_not_found1:
        jc      usb_device_not_found1
        mov     IOAddr,cx
    }
    // global enable register offset=0x20
    pParams->SMIEAddr = 0x20 + (IOAddr & ~0x3F);

    WORD UsbLegacySupportReg;
    __asm {
        push    si
        mov     ax,0xB102
        mov     dx,0x8086
        mov     cx,0x7112
        mov     si,0
        INT     0x1A
        pop     si
        JC      usb_device_not_found1

        mov     DevNum,bx
        // read base address
        push    di
        mov     ax,0xB10A
        mov     bx,DevNum
        mov     di,0x20
        INT     0x1A
        pop     di
usb_device_not_found1:
        jc      usb_device_not_found
        mov     IOAddr,cx
        push    di
        mov     ax,0xB109
        mov     bx,DevNum
        mov     di,0xC0
        INT     0x1A
        pop     di
        mov     UsbLegacySupportReg,cx

        push    di
        mov     cx,UsbLegacySupportReg
        and     cx,~0x10
        mov     ax,0xB10C
        mov     bx,DevNum
        mov     di,0xC0
        INT     0x1A
        pop     di
    }

usb_device_not_found:
    return;
}

void CheckForTestRerun()
{
    BYTE AlarmS = ReadRTCReg(RTC_SECOND_ALARM);
    WriteRTCReg(RTC_SECOND_ALARM, 0);

    switch (AlarmS)
    {
    case TEST_RESULT_SUCCESS:
        printf("\nPrevious test run successfully completed\n");
        break;
    case TEST_RESULT_CTRL_ALT_DEL:
        printf("\nPrevious test run terminated by Ctrl+Alt+Del\n");
        break;
    case TEST_RESULT_MEMORY_ERROR:
        printf("\nPrevious test run found a memory error\n");
        break;
    case TEST_RESULT_CRASH:
        printf("\nPrevious test run crashed or terminated by Reset\n");
        break;
    default:
        return;
    }
    exit(AlarmS - TEST_RESULT_SUCCESS);
}

inline void SetRTCDefaults()
{
    WriteRTCReg(RTC_SECOND_ALARM, TEST_RESULT_CRASH);
}

inline void GetCursorPosition(MEMTEST_STARTUP_PARAMS * pParams)
{
    BYTE row;
    __asm {
        mov     ah,3
        mov     bh,0
        int     0x10
        mov     row,dh
    }
    pParams->CursorRow = row;
}

char const * IsOption(char const * arg, char const * option)
{
    // non-required characters in the option name are separated by '*'
    BOOL Enough = FALSE;
    BOOL WantParameter = FALSE;
    while (*option != 0)
    {
        if ('*' == *option)
        {
            Enough = TRUE;
            option++;
            continue;
        }

        if (':' == *option)
        {
            Enough = TRUE;
            WantParameter = TRUE;
            option++;
            continue;
        }

        if (0 != strnicmp(arg, option, 1))
        {
            break;
        }
        arg++;
        option++;
    }

    if ((0 == *option || Enough)
        && (0 == *arg || (WantParameter && ':' == *(arg++))))
    {
        return arg;
    }
    return NULL;
}

int ProcessCommandLine(int argc, char * * argv, MEMTEST_STARTUP_PARAMS * pParams)
{
    argc--;
    argv++;
    int ArgIdx = 1;
    if (argc > 0 && '-' != argv[0][0] && '/' != argv[0][0])
    {
        char * end_ptr;
        long size = strtol(argv[0], & end_ptr, 10);
        if (*end_ptr != 0 || size < 4)
            return FALSE;

        argv++;
        argc --;
        ArgIdx++;

        long start = 0;
        if (argc > 0 && '-' != argv[0][0] && '/' != argv[0][0])
        {
            start = strtol(argv[0], & end_ptr, 10);
            if (*end_ptr != 0 || start < 0)
                return ArgIdx;
            argv++;
            argc --;
            ArgIdx++;
        }

        if (start + size > 3072)
            return ArgIdx - 1;
        pParams->MemoryStart = start;
        pParams->MemoryTop = (start + size);
    }
    // process other options
    while(argc > 0)
    {
        char const * option = argv[0];
        if (option[0] != '/' && option[0] != '-')
        {
            return ArgIdx;
        }

        option ++;

        char const * nextch;

        if (NULL != (nextch = IsOption(option, "de:lay")))
        {
            char * last;
            long delay1, delay2;
            ArgIdx++;
            if (argc < 3
                || (delay1 = strtol(argv[1], & last, 10)) < 0
                || delay1 > 1000
                || *last != 0
                || (delay2 = strtol(argv[2], & last, 10)) < 0
                || delay2 > 1000
                || *last != 0)
            {
                return ArgIdx;
            }

            pParams->ShortDelay = delay1 * 1000L;
            pParams->LongDelay = delay2 * 1000L;
            argv += 2;
            argc -= 2;
            ArgIdx++;
        }
        else if (NULL != (nextch = IsOption(option, "ro:w")))
        {
            char * last;
            long row;
            if (0 == *nextch)
            {
                if (argc < 2)
                {
                    return ArgIdx;
                }
                argv ++;
                argc --;
                ArgIdx++;
                nextch = argv[0];
            }

            row = strtol(nextch, & last, 10);
            if ((row != 4 && row != 8 && row != 16 && row != 32 && row != 64)
                || *last != 0)
            {
                return ArgIdx;
            }
            pParams->RowSize = row;
        }
        else if (IsOption(option, "wr*itethru"))
        {
            pParams->m_Flags |= TEST_FLAGS_WRITETHRU;
        }
        else if (IsOption(option, "noca*che"))
        {
            pParams->m_Flags |= TEST_FLAGS_NOCACHE;
        }
        else if (IsOption(option, "pre*heat"))
        {
            pParams->m_Flags |= TEST_FLAGS_PREHEAT_MEMORY;
        }
        else if (IsOption(option, "re*adtwice"))
        {
            pParams->m_Flags |= TEST_READ_TWICE;
        }
        else if (IsOption(option, "noch*eck"))
        {
            pParams->m_Flags |= TEST_NO_MACHINE_CHECK;
        }
        else if (IsOption(option, "fa*stdetect"))
        {
            pParams->m_Flags |= TEST_FLAGS_FASTDETECT;
        }
        else if (IsOption(option, "nopr*efetch"))
        {
            pParams->m_Flags |= TEST_FLAGS_NOPREFETCH;
        }
        else if (IsOption(option, "nola*rgepages"))
        {
            pParams->m_Flags |= TEST_FLAGS_NOLARGEPAGES;
        }
        else if (NULL != (nextch = IsOption(option, "pat:tern")))
        {
            char * last = NULL;
            pParams->m_Pattern1 = strtoul(nextch, & last, 16);

            if (NULL != last && *last == ':')
            {
                pParams->m_Pattern2 = strtoul(last+1, & last, 16);
                if (NULL == last || *last != 0)
                {
                    return ArgIdx;
                }
            }
            else
            {
                return ArgIdx;
            }
        }
        else if (IsOption(option, "nous*blegacy"))
        {
            pParams->m_Flags |= TEST_NO_USB_LEGACY;
        }
        else if (NULL != (nextch = IsOption(option, "pas:scount")))
        {
            char * last;
            int count;
            if (0 == *nextch)
            {
                if (argc < 2)
                {
                    return ArgIdx;
                }
                argv ++;
                argc --;
                ArgIdx++;
                nextch = argv[0];
            }

            count = (int)strtol(nextch, & last, 10);

            if (*last != 0)
            {
                return ArgIdx;
            }
            pParams->m_PassCount = count;
        }
        else
        {
            return ArgIdx;
        }
        argv++;
        argc--;
        ArgIdx++;
    }
    return 0;
}

int _cdecl main(int argc, char *argv[])
{
    puts(title);
    if (argc >= 2
        && (strcmp(argv[1], "?") == 0
            || strcmp(argv[1], "-?") == 0
            || strcmp(argv[1], "/?") == 0
            || stricmp(argv[1], "-h") == 0
            || stricmp(argv[1], "/h") == 0))
    {
        puts(help);
        return 0;
    }

    PROTECTED_MODE_STARTUP_DATA Startup;
    Startup.pProgramBase = 0;
    Startup.ProgramEntry = 0;
    Startup.ProgramSize = 0;

    Startup.msp.RandomSeed = time(NULL);

    int ArgResult = ProcessCommandLine( argc, argv, & Startup.msp);
    if (ArgResult)
    {
        fprintf(stderr, "Command Line Error in or after \"%s\", type MEMTEST /? for help", argv[ArgResult]);
        return -1;
    }

    PAUSE("Going to check for bare DOS...(press Enter)");

    if ( ! IsBareDOS(TRUE))
    {
        fputs("\nError: Can't run - memory managers detected.\n"
              "\tThe program can be run only from bare DOS.\n"
              "\tTo start bare DOS session, restart the computer\n"
              "\tand press Shift-F5 when \"Starting Windows 98...\" message appears,\n"
              "or boot from a diskette with system files only."
              "\tType MEMTEST /? for help.\n", stderr);
        return -1;
    }

    PAUSE("Going to check CPU type...(press Enter)");

    long CpuType = cpu_type();     // upper word - feature bits
    int CpuModel = WORD(CpuType);
    if (CpuModel < 386)
    {
        fputs("Error: The Program requires i386 or higher CPU type\n", stderr);
        return -1;
    }

    Startup.msp.CpuType = WORD(CpuType);
    Startup.msp.CpuFeatures = WORD(CpuType >> 16);

    if (Startup.msp.m_PassCount != 0)
    {
        CheckForTestRerun();
        // set RTC values to detect crash
        SetRTCDefaults();
    }

    FillMemoryMap(&Startup);

    PAUSE("Going to load PE module...(press Enter)");

    if ( argc < 1 || !LoadPE(argv[0], & Startup))
    {
        fputs("\nUnable to load the program\n", stderr);
        return -1;
    }

    //PAUSE("Going to reset FDC...(press Enter)");

    ResetFloppies();

    if (Startup.msp.m_Flags & TEST_NO_USB_LEGACY)
    {
        DisableUsbLegacy( & Startup.msp);
    }

    GetCursorPosition( & Startup.msp);

    PAUSE("Going to enter protected mode...(press Enter)");

    ProtectedModeStart( & Startup);
    // if the program successfully started, execution should not return here
    fputs("\nUnable to enter protected mode\n", stderr);
    return -1;
}
