# Microsoft Developer Studio Generated NMAKE File, Format Version 4.20
# ** DO NOT EDIT **

# TARGTYPE "Win32 (x86) Console Application" 0x0103

!IF "$(CFG)" == ""
CFG=memtest - Win32 Debug
!MESSAGE No configuration specified.  Defaulting to memtest - Win32 Debug.
!ENDIF 

!IF "$(CFG)" != "memtest - Win32 Release" && "$(CFG)" !=\
 "memtest - Win32 Debug"
!MESSAGE Invalid configuration "$(CFG)" specified.
!MESSAGE You can specify a configuration when running NMAKE on this makefile
!MESSAGE by defining the macro CFG on the command line.  For example:
!MESSAGE 
!MESSAGE NMAKE /f "memtest.mak" CFG="memtest - Win32 Debug"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "memtest - Win32 Release" (based on "Win32 (x86) Console Application")
!MESSAGE "memtest - Win32 Debug" (based on "Win32 (x86) Console Application")
!MESSAGE 
!ERROR An invalid configuration is specified.
!ENDIF 

!IF "$(OS)" == "Windows_NT"
NULL=
!ELSE 
NULL=nul
!ENDIF 
################################################################################
# Begin Project
# PROP Target_Last_Scanned "memtest - Win32 Debug"
RSC=rc.exe
CPP=cl.exe

!IF  "$(CFG)" == "memtest - Win32 Release"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 0
# PROP BASE Output_Dir "Release"
# PROP BASE Intermediate_Dir "Release"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 0
# PROP Output_Dir "Release"
# PROP Intermediate_Dir "Release"
# PROP Target_Dir ""
OUTDIR=.\Release
INTDIR=.\Release

ALL : "$(OUTDIR)\memtest.exe"

CLEAN : 
	-@erase "$(INTDIR)\memtest.obj"
	-@erase "$(INTDIR)\memtest.res"
	-@erase "$(OUTDIR)\memtest.exe"

"$(OUTDIR)" :
    if not exist "$(OUTDIR)/$(NULL)" mkdir "$(OUTDIR)"

# ADD BASE CPP /nologo /W3 /GX /O2 /D "WIN32" /D "NDEBUG" /D "_CONSOLE" /YX /c
# ADD CPP /nologo /G5 /W3 /O2 /D "NDEBUG" /D "WIN32" /D "_CONSOLE" /D "STANDALONE" /YX /c
CPP_PROJ=/nologo /G5 /ML /W3 /O2 /D "NDEBUG" /D "WIN32" /D "_CONSOLE" /D\
 "STANDALONE" /Fp"$(INTDIR)/memtest.pch" /YX /Fo"$(INTDIR)/" /c 
CPP_OBJS=.\Release/
CPP_SBRS=.\.
# ADD BASE RSC /l 0x419 /d "NDEBUG"
# ADD RSC /l 0x409 /d "NDEBUG"
RSC_PROJ=/l 0x409 /fo"$(INTDIR)/memtest.res" /d "NDEBUG" 
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
BSC32_FLAGS=/nologo /o"$(OUTDIR)/memtest.bsc" 
BSC32_SBRS= \
	
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /subsystem:console /machine:I386
# ADD LINK32 kernel32.lib user32.lib gdi32.lib /nologo /entry:"MemtestStartup" /subsystem:console /machine:I386 /nodefaultlib /stub:"memtst1.exe" /FIXED
# SUBTRACT LINK32 /pdb:none
LINK32_FLAGS=kernel32.lib user32.lib gdi32.lib /nologo /entry:"MemtestStartup"\
 /subsystem:console /incremental:no /pdb:"$(OUTDIR)/memtest.pdb" /machine:I386\
 /nodefaultlib /stub:"memtst1.exe" /out:"$(OUTDIR)/memtest.exe" /FIXED 
LINK32_OBJS= \
	"$(INTDIR)\memtest.obj" \
	"$(INTDIR)\memtest.res"

"$(OUTDIR)\memtest.exe" : "$(OUTDIR)" $(DEF_FILE) $(LINK32_OBJS)
    $(LINK32) @<<
  $(LINK32_FLAGS) $(LINK32_OBJS)
<<

!ELSEIF  "$(CFG)" == "memtest - Win32 Debug"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir "Debug"
# PROP BASE Intermediate_Dir "Debug"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 1
# PROP Output_Dir "Debug"
# PROP Intermediate_Dir "Debug"
# PROP Target_Dir ""
OUTDIR=.\Debug
INTDIR=.\Debug

ALL : "$(OUTDIR)\memtest.exe"

CLEAN : 
	-@erase "$(INTDIR)\memtest.obj"
	-@erase "$(INTDIR)\memtest.res"
	-@erase "$(INTDIR)\vc40.idb"
	-@erase "$(INTDIR)\vc40.pdb"
	-@erase "$(OUTDIR)\memtest.exe"
	-@erase "$(OUTDIR)\memtest.pdb"

"$(OUTDIR)" :
    if not exist "$(OUTDIR)/$(NULL)" mkdir "$(OUTDIR)"

# ADD BASE CPP /nologo /W3 /Gm /GX /Zi /Od /D "WIN32" /D "_DEBUG" /D "_CONSOLE" /YX /c
# ADD CPP /nologo /G5 /W3 /Gm /Zi /Od /Gf /Gy /D "_DEBUG" /D "WIN32" /D "_CONSOLE" /D "STANDALONE" /YX /c
CPP_PROJ=/nologo /G5 /MLd /W3 /Gm /Zi /Od /Gf /Gy /D "_DEBUG" /D "WIN32" /D\
 "_CONSOLE" /D "STANDALONE" /Fp"$(INTDIR)/memtest.pch" /YX /Fo"$(INTDIR)/"\
 /Fd"$(INTDIR)/" /c 
CPP_OBJS=.\Debug/
CPP_SBRS=.\.
# ADD BASE RSC /l 0x419 /d "_DEBUG"
# ADD RSC /l 0x409 /d "_DEBUG"
RSC_PROJ=/l 0x409 /fo"$(INTDIR)/memtest.res" /d "_DEBUG" 
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
BSC32_FLAGS=/nologo /o"$(OUTDIR)/memtest.bsc" 
BSC32_SBRS= \
	
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /subsystem:console /debug /machine:I386
# ADD LINK32 kernel32.lib user32.lib gdi32.lib /nologo /entry:"MemtestStartup" /subsystem:console /incremental:no /debug /machine:I386 /nodefaultlib /stub:"memtst1.exe" /FIXED
# SUBTRACT LINK32 /pdb:none /map
LINK32_FLAGS=kernel32.lib user32.lib gdi32.lib /nologo /entry:"MemtestStartup"\
 /subsystem:console /incremental:no /pdb:"$(OUTDIR)/memtest.pdb" /debug\
 /machine:I386 /nodefaultlib /stub:"memtst1.exe" /out:"$(OUTDIR)/memtest.exe"\
 /FIXED 
LINK32_OBJS= \
	"$(INTDIR)\memtest.obj" \
	"$(INTDIR)\memtest.res"

"$(OUTDIR)\memtest.exe" : "$(OUTDIR)" $(DEF_FILE) $(LINK32_OBJS)
    $(LINK32) @<<
  $(LINK32_FLAGS) $(LINK32_OBJS)
<<

!ENDIF 

.c{$(CPP_OBJS)}.obj:
   $(CPP) $(CPP_PROJ) $<  

.cpp{$(CPP_OBJS)}.obj:
   $(CPP) $(CPP_PROJ) $<  

.cxx{$(CPP_OBJS)}.obj:
   $(CPP) $(CPP_PROJ) $<  

.c{$(CPP_SBRS)}.sbr:
   $(CPP) $(CPP_PROJ) $<  

.cpp{$(CPP_SBRS)}.sbr:
   $(CPP) $(CPP_PROJ) $<  

.cxx{$(CPP_SBRS)}.sbr:
   $(CPP) $(CPP_PROJ) $<  

################################################################################
# Begin Target

# Name "memtest - Win32 Release"
# Name "memtest - Win32 Debug"

!IF  "$(CFG)" == "memtest - Win32 Release"

!ELSEIF  "$(CFG)" == "memtest - Win32 Debug"

!ENDIF 

################################################################################
# Begin Source File

SOURCE=.\memtest.cpp
DEP_CPP_MEMTE=\
	".\memtest.h"\
	

!IF  "$(CFG)" == "memtest - Win32 Release"


"$(INTDIR)\memtest.obj" : $(SOURCE) $(DEP_CPP_MEMTE) "$(INTDIR)"
   $(CPP) /nologo /G5 /ML /W3 /O2 /D "NDEBUG" /D "WIN32" /D "_CONSOLE" /D\
 "STANDALONE" /Fp"$(INTDIR)/memtest.pch" /YX /Fo"$(INTDIR)/" /c $(SOURCE)


!ELSEIF  "$(CFG)" == "memtest - Win32 Debug"

# ADD CPP /O2 /FAcs

"$(INTDIR)\memtest.obj" : $(SOURCE) $(DEP_CPP_MEMTE) "$(INTDIR)"
   $(CPP) /nologo /G5 /MLd /W3 /Gm /Zi /O2 /D "_DEBUG" /D "WIN32" /D "_CONSOLE"\
 /D "STANDALONE" /FAcs /Fa"$(INTDIR)/" /Fp"$(INTDIR)/memtest.pch" /YX\
 /Fo"$(INTDIR)/" /Fd"$(INTDIR)/" /c $(SOURCE)


!ENDIF 

# End Source File
################################################################################
# Begin Source File

SOURCE=.\memtest.rc

"$(INTDIR)\memtest.res" : $(SOURCE) "$(INTDIR)"
   $(RSC) $(RSC_PROJ) $(SOURCE)


# End Source File
# End Target
# End Project
################################################################################
