#include <stdio.h>
#include <string.h>
#include <dos.h>
#include <stdlib.h>
#include <time.h>

#pragma pack (1)

#include "mtstart.h"
#include "memtest.h"

BOOL GetMemorySizes(MemorySizes * pSizes);
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
              "/preheat - perform memory preheat;\n"
              "/nocache - disable caching (not recommended);\n"
              "/nocheck - don't enable machine check interrupt.\n"
              "\nFor more details see README.HTM\n"
              "Check http://www.aha.ru/~alegr/index_en.htm for program updates."
;

extern "C" long cpu_type();

// The following functions are allowed to use 8086 instructions only
BOOL IsWindowsRunning()
{
    // check for Windows running
#ifdef _DEBUG
    printf("Checking for Windows, press Enter to continue...");
    fgets(fgetsbuf, sizeof fgetsbuf, stdin);
#endif
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
#ifdef _DEBUG
    printf("Checking for EMM386, press Enter to continue...");
    fgets(fgetsbuf, sizeof fgetsbuf, stdin);
#endif
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
#ifdef _DEBUG
    printf("Checking for HIMEM, press Enter to continue...");
    fgets(fgetsbuf, sizeof fgetsbuf, stdin);
#endif
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

inline BOOL IsBareDOS()
{
    if (IsWindowsRunning() || IsEmm386Present()
        || IsHimemPresent() || IsV86())
        return FALSE;
    else
        return TRUE;
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

BOOL ProcessCommandLine(int argc, char * * argv, MEMTEST_STARTUP_PARAMS * pParams)
{
    argc--;
    argv++;
    if (argc > 0 && '-' != argv[0][0] && '/' != argv[0][0])
    {
        char * end_ptr;
        long size = strtol(argv[0], & end_ptr, 10);
        if (*end_ptr != 0 || size < 4)
            return FALSE;
        argv++;
        argc --;
        long start = 0;
        if (argc > 0 && '-' != argv[0][0] && '/' != argv[0][0])
        {
            start = strtol(argv[0], & end_ptr, 10);
            if (*end_ptr != 0 || start < 0)
                return FALSE;
            argv++;
            argc --;
        }
        if (start + size > 3072)
            return FALSE;
        pParams->MemoryStart = start * 0x100000;
        pParams->MemoryTop = (start + size) * 0x100000;
    }
    // process other options
    while(argc > 0)
    {
        char const * option = argv[0];
        if (option[0] != '/' && option[0] != '-')
        {
            return FALSE;
        }

        option ++;

        if (0 == stricmp(option, "delay"))
        {
            char * last;
            long delay1, delay2;
            if (argc < 3
                || (delay1 = strtol(argv[1], & last, 10)) < 0
                || delay1 > 1000
                || *last != 0
                || (delay2 = strtol(argv[2], & last, 10)) < 0
                || delay2 > 1000
                || *last != 0)
                return FALSE;
            pParams->ShortDelay = delay1 * 1000L;
            pParams->LongDelay = delay2 * 1000L;
            argv += 2;
            argc -= 2;
        }
        else if (0 == stricmp(option, "row"))
        {
            char * last;
            long row;
            if (argc < 2
                || ((row = strtol(argv[1], & last, 10)) != 4
                    && row != 8 && row != 16 && row != 32 && row != 64)
                || *last != 0)
                return FALSE;
            pParams->RowSize = row;
            argv ++;
            argc --;
        }
        else if (0 == stricmp(option, "writethru"))
        {
            pParams->Flags |= TEST_FLAGS_WRITETHRU;
        }
        else if (0 == stricmp(option, "nocache"))
        {
            pParams->Flags |= TEST_FLAGS_NOCACHE;
        }
        else if (0 == stricmp(option, "preheat"))
        {
            pParams->Flags |= TEST_FLAGS_PREHEAT_MEMORY;
        }
        else if (0 == stricmp(option, "speed"))
        {
            pParams->Flags |= TEST_FLAGS_PERFORMANCE;
        }
        else if (0 == stricmp(option, "readtwice"))
        {
            pParams->Flags |= TEST_READ_TWICE;
        }
        else if (0 == stricmp(option, "nocheck"))
        {
            pParams->Flags |= TEST_NO_MACHINE_CHECK;
        }
        else
        {
            return FALSE;
        }
        argv++;
        argc--;
    }
    return TRUE;
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
#ifdef _DEBUG
    printf("Going to check for bare DOS...(press Enter)");
    fgets(fgetsbuf, sizeof fgetsbuf, stdin);
#endif
    if ( ! IsBareDOS())
    {
        fputs("\nError: Can't run - memory managers detected.\n"
              "\tThe program can be run only from bare DOS.\n"
              "\tTo start bare DOS session, restart the computer\n"
              "\tand press Shift-F5 when \"Starting Windows95...\" message appears,\n"
              "or boot from a diskette with system files only."
              "\tType MEMTEST /? for help.\n", stderr);
        return -1;
    }

#ifdef _DEBUG
    printf("Going to check CPU type...(press Enter)");
    fgets(fgetsbuf, sizeof fgetsbuf, stdin);
#endif
    long CpuType = cpu_type();     // upper word - feature bits
    int CpuModel = WORD(CpuType);
    if (CpuModel < 386)
    {
        fputs("Error: The Program requires i386 or higher CPU type\n", stderr);
        return -1;
    }

    if (CpuModel < 586)
    {
        fputs("Warning: The program is designed to test 64 bit data paths,\n"
              "\tand may not be worth running on systems with 32-bit memory bus.\n", stderr);
        // continue execution anyway
    }

    PROTECTED_MODE_STARTUP_DATA Startup;
    memset( & Startup, 0, sizeof Startup);

    Startup.msp.MemoryTop = 0x20000000;   // 512 MB
    Startup.msp.LongDelay = 60000;
    Startup.msp.ShortDelay = 1000;
    Startup.msp.CpuType = WORD(CpuType);
    Startup.msp.CpuFeatures = WORD(CpuType >> 16);
    Startup.msp.RandomSeed = time(NULL);

    if ( ! ProcessCommandLine( argc, argv, & Startup.msp))
    {
        fputs("Command Line Error, type MEMTEST /? for help", stderr);
        return -1;
    }

    //MemorySizes ms;
    //GetMemorySizes( & ms);

#ifdef _DEBUG
    printf("Going to load PE module...(press Enter)");
    fgets(fgetsbuf, sizeof fgetsbuf, stdin);
#endif
    if ( argc < 1 || !LoadPE(argv[0], & Startup))
    {
        fputs("\nUnable to load the program\n", stderr);
        return -1;
    }

#ifdef _DEBUG
    printf("Going to reset FDC...(press Enter)");
    fgets(fgetsbuf, sizeof fgetsbuf, stdin);
#endif
    ResetFloppies();

#ifdef _DEBUG
    printf("Going to enter protected mode...(press Enter)");
    fgets(fgetsbuf, sizeof fgetsbuf, stdin);
#endif
    ProtectedModeStart( & Startup);
    // if the program successfully started, execution should not return here
    fputs("\nUnable to enter protected mode\n", stderr);
    return -1;
}
