# MEMTEST - x86 system memory test

AleGr MEMTEST is a program for testing DRAM (main memory) of PC-compatible 
computers built on Intel 386 or higher processor. The version 2.0 is now able 
to run under Windows.

Some of older programs for memory testing were written for cacheless processors 
and required to disable the cache, when used on modern computers.  This 
program is designed to run on the processors with memory cache, and takes into 
account how the system bus, cache and main memory of Pentium and Pentium 
Pro (Pentium II, III, 4) processors work. Test patterns and access sequences 
used by the program are designed to test burst transfers between CPU and L2 
cache and/or memory, because such transfers occur most of the time in real 
applications. It also tests some special access cycles, such as locked 
read-modify-write.

Maximum size of memory the program is able to test is 3 gigabytes under DOS and 
less than 2 gigabytes under Windows. If you have 4 GB or more, you can start 
several test sessions under Windows, to cover all the memory.

The program can be run under "bare" DOS session, and under Windows.

The program runs differently under DOS and Windows.

## License terms

MEMTEST version 2.0 program is free for personal (non-business) 
use.

WARNING: There is no guarantee of any kind. On some low-power 
systems, the program may cause excessive current consumption, and trigger 
overcurrent protection or cause permanent damage. When the program is run under 
Windows, it can cause a marginally stable system to crash with possible 
loss of data or OS damage.

## Running the program under "bare" DOS

"Bare" DOS means "real mode" DOS without any memory managers, such as 
HIMEM.SYS and EMM386.EXE. Version 2.0 can now start if HIMEM.SYS is present.

Windows cannot boot into DOS. You will need to boot from a floppy or a bootable USB. 
You can create a boot disk when you format a floppy.

Command line to start the program is as follows:

<samp>**MEMTEST &lt;memory size&gt; &lt;base address of 
area to test&gt; /program switches**</samp>

Optional **memory size** and **base address** of an 
area to test are specified in megabytes. If they are not specified, all 
available memory will be tested. Since the program detects actual 
memory size, it is safe to specify more memory than actually installed.

The program cannot test less than 8 megabytes.

The following command line switches are recognized (they can be abbreviated 
down to the part shown with <U>underscore</U>):

/<U>ro</U>w:&lt;size&gt; - specifies size (in kilobytes) of DRAM memory array 
row. Allowed values are 4 up to 64 (only powers of 2). By default all possible 
row sizes from 4 to 64 are tested. Specify this option only if you exactly know 
row size (it depends on memory chip manufacturer and model and is **not**
usually published even in memory data sheets). If specified size does not match 
real value, the program won't be able to test the memory chips for maximum 
noise on word lines.

/<U>de</U>lay &lt;time1&gt; &lt;time2&gt; - specifies the delay (in seconds) 
between memory write and read passes. The delay specified by **time1**
is inserted on every other test pass, default value is 1 second. The delay 
specified by **time2 **is inserted on 62th and 63th passes of 64, 
default is 60 seconds. This allows to test for proper DRAM refresh.

/<U>sp</U>eed - tells the program to measure CPU clock rate and also L2 cache 
and main memory read/write speed (including write in write-allocate mode);

/<U>re</U>adtwice - tells the program to compare test data twice;

/<U>wr</U>itethru - disables writeback cache policy;

/<U>noca</U>che - disables cache at all (not recommended).

/<U>nopr</U>efetch - doesn't perform cache prefetch.

/<U>noch</U>eck - disables machine check on Pentium (and newer) systems. Machine 
check interrupt is triggered when bus or cache parity error occurs.

/<U>pre</U>heat - the program will try to preheat the memory chips by the 
special access sequence, before writing each test pattern. Remember, though, 
that using this option on some notebook computers may trigger overcurrent 
protection.


/<U>nous</U>blegacy - disables USB legacy emulation interrupt. Use it when the 
test shows memory errors in the very first kilobyte of physical memory. If this 
switch doesn't help, run the test starting from megabyte 1, for example: "**MEMTEST 
512 1**".


/<U>fa</U>stdetect - use fast memory size detection algorithm (though less 
thorough).


/<U>ig</U>norexmm - run the program even though XMM (HIMEM.SYS) is detected. Is 
necessary is you use Windows ME boot floppy.

/<U>nola</U>rgepages - do not use 4MB pages. Use regular 4KB pages only.

/<U>pat</U>tern:&lt;pattern1&gt;:&lt;pattern2&gt; - sets a pair of special 
32-bit patterns. &lt;pattern1&gt; and &lt;pattern2&gt; are specified in 
hexadecimal notation, for example: /pat:01234567:FEDCBA98. Each value is 
duplicated to form a 64-bit quad-word, so the memory will be written with the 
following data pattern:

<FONT face="Courier New">
0123456701234567<BR>
FEDCBA98FEDCBA98<BR>
0123456701234567<BR>
FEDCBA98FEDCBA98
</FONT>

After testing the complete memory, the pattern is rotated, and it is repeated 
until the pattern returns to the original value.

Once MEMTEST is started, it is not possible to return back to DOS, because 
the program switches the CPU to 32-bit protected mode. Testing can only be 
stopped by the system restart - either by pressing `Ctrl+Alt+Del`
or by pressing `Reset` (it can be 
found on the system case, not on the keyboard), or by turning the power off 
then off (recommended only for the systems without `Reset`
button).

If the program detects less memory than you know is installed, it probably means 
that a piece of very unstable memory is detected and the program decided to 
stop scanning for more.

When memory errors messages fill all the screen, the program pauses screen 
output to prevent error messages loss. To resume the output, press 
`Enter`.

While the program is working, it displays the test pass number. Full test 
consists of 64 passes. Required time depends on CPU and memory size. Keep in 
mind that every other test pass includes 36 seconds of delay and two passes of 
64 both include 36 minutes of delay. **After the program completed all 64 
passes, it will continue to run the test from the beginning. To stop the test, 
you need to restart the computer, by pressing Ctrl+Alt+Del.**

### Running the program under Windows

This capability was added to provide more realistic testing of the 
overall system stability.


Under Windows, the program starts a few threads running in 
parallel, each testing its own region of virtual addresses. It can also run 
disk read/write test, to check if disk DMA operations could cause the memory 
corruption.

Disk I/O test has been added because:

- there were known IDE RAID controller chipsets that suffered 
from data corruption during simultaneous data transfer on both IDE 
channels under Windows 2000 and Linux, when overall throughput of IDE 
interface exceeds PCI bus throughput. Probably the controller could not 
properly arbitrate requests from its own channels in case of contention.

- a mainboard in my computer was suffering from memory corruption 
(some bits were unstable) during heavy disk I/O. The computer was crashing 
whenever I tried to copy about 10 GB of files. The problem never showed up in 
DOS-based MEMTEST run, but the new test could catch the error in a couple of 
minutes. DIMM and CPU replacement didn't help. A new board with the same 
chipset (though slightly different modification - without on-chip video 
controller), but from different OEM, works fine. I don't know whether it 
was the board layout problem, or some PCB traces had defects.

Under Windows, the test log can be saved to a file.

Command line to start the program is as follows:

```
MEMTEST <program switches>
```

The following command line switches are recognized:

/<U>ti</U>me:&lt;time limit&gt; - sets test time in minutes. If you don't 
specify this option, the default test time is 60 minutes;

/<U>mem</U>ory:&lt;n&gt; - specifies the test area size in megabytes, from 4 to 
1024. Up to 4 **/memory** command line switches allowed, each of 
them causes the program to start a separate test thread. /pattern option that 
goes after /memory specifies a test pattern for this thread. Max total allowed 
memory size is about 2000 MB. If you need to test more memory, you 
can start more test sessions.

/<U>fi</U>le:&lt;directory&gt; &lt;n&gt; - specifies the test file location 
and size in megabytes (from 4 up to 64000). Up to 4 **/file**
command line switches allowed, each of them causes the program to start a 
separate test thread. /pattern option that goes after /file specifies a test 
pattern for this thread. If the directory name contains spaces, surround it 
with double quote characters. The directory should exist before the program is 
run.

/<U>maxe</U>rrors:&lt;n&gt; - maximum number of errors before the test stops. 
The test runs until time specified by /time switch elapses, or maximum number 
of errors found;

/<U>re</U>adtwice - tells the program to compare test data twice;

/<U>noca</U>che - disables cache at all (not recommended).

/<U>pre</U>heat - the program will try to preheat the memory chips by the 
special access sequence, before writing each test pattern. Remember, though, 
that using this option on some notebook computers may trigger overcurrent 
protection.


/<U>log</U>file:&lt;file name&gt; - writes a test log to the file. If the '+' 
character precedes the file name, test log will be appended to the file. The 
log file is always opened with write-through option, which prevents loss 
of the messages if the system crashes.

/<U>pat</U>tern:&lt;pattern1&gt;:&lt;pattern2&gt; - sets a pair of 32-bit test 
patterns. &lt;pattern1&gt; and &lt;pattern2&gt; are specified in hexadecimal 
notation, for example: /pat:01234567:FEDCBA98. Each value is duplicated to form 
a 64-bit quad-word, so the memory will be written with the following data 
pattern:

```
01234567 01234567
FEDCBA98 FEDCBA98
01234567 01234567
FEDCBA98 FEDCBA98
```

After writing/reading the complete test area, the pattern is rotated (right 
shift by one bit), and the test repeats. The pass is completed 
when the pattern returns to the original value.

Each memory or file test thread can have its own test pattern. For example:

MEMTEST /mem:64 /pat:01234567:FEDCBA98 /mem:64 /pat:80008000:7FFF7FFF
<BR>
   /file:c:\ 256 /pat:80007FFF:7FFF8000  /file:d:\ 256 
/pat:0080FF7F:0080FF7F

For file test, it makes sense to use patterns which are inverted every 16 bits, 
like 80007FFF. It is because the IDE data bus is 16 bits wide. For SCSI 
disk, different test pattern may be better, for example inverted every 8 
bits for 8-bit SCSI bus.

It is recommended that different test threads use different test patterns.

To avoid disk thrashing, use one file test per physical drive. If you're running 
a RAID disk array, use one thread per controller channel, although the disks 
will experience continuous thrashing.

Check if you're specified not too much memory to test. Open Task Manager 
(Ctrl+Shift+Esc) and on the Performance tab see if the CPU is fully loaded. If 
it is not, then there is memory paging (see also the hard drive light), and you 
need to reduce test area size.

The test can be stopped by pressing either Ctrl+Break or Ctrl+C. It may not stop 
immediately, though.


### Memory test sequence

First test performs pseudorandom data read/write. The 
data consists of "all ones"/"all zeros" patterns produced by pseudorandom 
sequence generator. The test checks for address errors.

The second test uses a pair of 32-bit patterns. Each 
pattern is duplicated to form a 64-bit quadword. These quadwords are written 
each after other:

```
01111111111111110111111111111111 01111111111111110111111111111111 
10000000000000001000000000000000 10000000000000001000000000000000
01111111111111110111111111111111 01111111111111110111111111111111
10000000000000001000000000000000 10000000000000001000000000000000
```

The test patterns can be specified in the program command line. The 
default pattern is running zero/running one pair, like shown above. This test 
sequence allows also to check the system bus in maximum noise conditions.


After all the memory being tested is filled with the pattern, it is read 
in ascending direction (from address 0 to the highest address) and compared 
with reference data. As the memory is read, the data is replaced with inverted 
test pattern. After all the memory is read and replaced with inverted pattern, 
it is read in descending direction and compared with new reference data. As the 
memory is read, the data is replaced with next test pattern, which is like 
first one cyclically shifted (rotated) to right:

```
1011111111111111 1011111111111111 1011111111111111 1011111111111111
0100000000000000 0100000000000000 0100000000000000 0100000000000000
1011111111111111 1011111111111111 1011111111111111 1011111111111111
0100000000000000 0100000000000000 0100000000000000 0100000000000000
```

Pattern replacement on the descending pass is done with uncacheable 64-bit 
exchange operation: CMPEXCH8B. This allows to test locked read-modify-write 
memory cycle.

Such tests are performed for all bit positions (total 16).

Next tests work with "all ones"/"all zeros" patterns, with pattern inverted on 
every other memory array row. This test exploits the dynamic RAM structure.

Dynamic RAM array is crossed by **word** (or row) lines (that go 
from row address decoder) and column lines. Each pair of column lines is 
connected to differential inputs of read detector. Even rows are connected to 
even column lines, and odd rows are connected to odd lines. Column lines are 
connected also to reference cells that are charged to half voltage of a regular 
cell. When, for instance, even row is selected, memory cells are read to even 
column lines and reference cells are read to odd column lines. Voltage 
difference (tens of millivolts) is detected by read detectors and converted to 
logical 0 or 1. Then read cells are charged back to full voltage (refreshed) 
and row is deselected. After that, all column lines should be discharged to 
fixed voltage level, it is called "RAS precharge".

Maximum noise to column lines occurres when all cells are charged to same "low" 
or "high" state, this means that adjacent rows should be written with inverted 
data. To achieve this, test pattern is inverted every 4 to 64 KB (step is 
doubled on every test pass), and data is read from interlaced rows - 32 bytes 
from one row, 32 bytes from adjacent one. "All zeros" and "all ones" patterns 
allow also to induce maximum noise on adjacent column lines.

When running in a standalone mode, the program relocates itself in the physical 
memory after every test pass, to test the area it just occupied.

In a standalone mode, every other pass includes 1 second delay between memory 
writing and reading. During the delay there are no accesses to the DRAM, 
because all instructions are fetched from the L1 cache. This allows to check 
how reliable is memory refresh. On two passes from 64 the delay is increased to 
60 seconds. These delays can be specified in the command line at the program 
start.

When running under Windows, the program decommits the memory area after every 
pass, it means releases the physical pages. It allows to test different pages 
every time.

To test transfers from L2 cache to the processor, some of test passes are 
performed with data prefetched from the memory to L2 cache. Without such 
prefetch, the data goes directly from memory to the processor. To test 
transfers from processor to L2 cache, some of the test passes are performed 
with data prefetched to L2 cache during test pattern write.

### File test sequence

To test file transfers, a temporary file (named TstXX.tmp) is 
created. Test file location and size (in megabytes from 4 to 640000) is 
specified in **/file** command line option. The program can run 
simultaneous read/write up to four files. The files are opened with 
file cache disabled.

The file is first written with the specified pattern. The default 
initial pattern pair is `7FFF8000/80007FFF`. The file is then read and compared 
with the pattern. In case of a mismatch, an error message is logged.

The pattern is then cyclically shifted (rotated), each 16-bit word 
individually. For example `7FFF8000` becomes `DFFF4000`. This pattern is also 
written and verified. It repeats until the pattern returns to the original 
value.

### Version history

2.00 (August 1, 2002). Fixed a bug in memory speed test. Added 
capability to work under Windows. Removed default memory size limit in 512 
megabytes.

1.04 (August 10, 2000). Fixed a bug in the test relocation, which 
caused it to reboot on machines with more than 960 MB of memory. Added /**nousblegacy**
and /**fastdetect** command line options.

1.03 (March 28, 1999). Fixed a bug in timer function in V1.02, 
which was caused by compiler upgrade to MSVC 5.0. Added **/preheat**
command line option.

1.02 (Feb 25, 1999). Fixed i386 and i486 processor detection. Fixed 
problem with Intel P-II motherboards, which caused the test to reboot a 
computer. Added **/nocheck** command line option to disable 
machine check interrupt on P5 and P6 processors.

1.01 (Jan 22, 1998). Changed way to reset CPU for reboot. Instead of putting the 
CPU to shutdown mode (did not work on Pentium Pro+FX440) reset by keyboard 
controller is used. Added reset of floppy drives' motors before going to 
protected mode.

1.0 (Dec 30, 1997). First version.
