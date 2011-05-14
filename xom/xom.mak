#
# Copyright (c) 1999, 2000
# Intel Corporation.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without modification,
# are permitted provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice,
#    this list of conditions and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
#
# 3. All advertising materials mentioning features or use of this software must
#    display the following acknowledgement:
#
#    This product includes software developed by Intel Corporation and its
#    contributors.
#
# 4. Neither the name of Intel Corporation or its contributors may be used to
#    endorse or promote products derived from this software without specific
#    prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY INTEL CORPORATION AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED.  IN NO EVENT SHALL INTEL CORPORATION OR CONTRIBUTORS BE LIABLE FOR
# ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
# ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#

#
# Include sdk.env environment
#

!include $(SDK_INSTALL_DIR)\build\$(SDK_BUILD_ENV)\sdk.env

#
# Set the base output name and entry point
#

BASE_NAME         = xom
IMAGE_ENTRY_POINT = InitializeXpOnMac
MASM = $(SDK_INSTALL_DIR)\..\sample\Tools\ia32\masm611\bin\ml.exe
COMPRESS = $(SDK_INSTALL_DIR)\..\sample\build\tools\bin\eficompress

#
# Globals needed by master.mak
#

TARGET_APP = $(BASE_NAME)
SOURCE_DIR = $(SDK_INSTALL_DIR)\apps\$(BASE_NAME)
BUILD_DIR  = $(SDK_BUILD_DIR)\apps\$(BASE_NAME)

#
# Include paths
#

!include $(SDK_INSTALL_DIR)\include\$(EFI_INC_DIR)\makefile.hdr
INC = -I $(SDK_INSTALL_DIR)\include\$(EFI_INC_DIR) \
      -I $(SDK_INSTALL_DIR)\include\$(EFI_INC_DIR)\$(PROCESSOR) $(INC)

#
# Libraries
#

LIBS = $(LIBS) $(SDK_BUILD_DIR)\lib\libefi\libefi.lib

#
# Default target
#

all : dirs $(LIBS) $(OBJECTS)

#
# Program object files
#

INC_DEPS = addr.h dbg.h pmrm.h txt.h ..\lib\jlaefi.h

OBJECTS = $(OBJECTS) \
					$(BUILD_DIR)\$(BASE_NAME).obj \
					$(BUILD_DIR)\jlaefi.obj \
					$(BUILD_DIR)\addr.obj \
					$(BUILD_DIR)\dbg.obj \
					$(BUILD_DIR)\i386.obj \
					$(BUILD_DIR)\pmrm.obj \
					$(BUILD_DIR)\txt.obj \
					$(BUILD_DIR)\handoff.obj \
					$(BUILD_DIR)\font.obj \
					$(BUILD_DIR)\osx.obj \
					$(BUILD_DIR)\wxpl.obj \
					$(BUILD_DIR)\wxp.obj

#
# Source file dependencies
#

jlaefi.c : ..\lib\jlaefi.c
	copy ..\lib\jlaefi.c .

$(BUILD_DIR)\$(BASE_NAME).obj : $(*B).c $(INC_DEPS)

$(BUILD_DIR)\jlaefi.obj : jlaefi.c ..\lib\jlaefi.h

$(BUILD_DIR)\addr.obj : addr.c $(INC_DEPS)

$(BUILD_DIR)\dbg.obj : dbg.c $(INC_DEPS)

$(BUILD_DIR)\i386.obj : i386.c $(INC_DEPS)

$(BUILD_DIR)\pmrm.obj : pmrm.c $(INC_DEPS)

$(BUILD_DIR)\txt.obj : txt.c $(INC_DEPS)

$(BUILD_DIR)\font.obj : font.bin
	objcopy -Ibinary -Ope-i386 -Bi386 font.bin ../../build/bios32/output/apps/xom/font.obj

$(BUILD_DIR)\osx.obj : osx.tga
	$(COMPRESS) osx.tga osx.z
	objcopy -Ibinary -Ope-i386 -Bi386 osx.z ../../build/bios32/output/apps/xom/osx.obj

$(BUILD_DIR)\wxpl.obj : wxpl.tga
	$(COMPRESS) wxpl.tga wxpl.z
	objcopy -Ibinary -Ope-i386 -Bi386 wxpl.z ../../build/bios32/output/apps/xom/wxpl.obj

$(BUILD_DIR)\wxp.obj : wxp.tga
	$(COMPRESS) wxp.tga wxp.z
	objcopy -Ibinary -Ope-i386 -Bi386 wxp.z ../../build/bios32/output/apps/xom/wxp.obj

$(BUILD_DIR)\handoff.obj : handoff.asm
	@$(MASM) /nologo /c /Fo$(BUILD_DIR)\handoff.obj handoff.asm

$(BUILD_DIR)\realmode.obj : realmode.asm
	@$(MASM) /nologo /c /Fo$(BUILD_DIR)\realmode.obj realmode.asm

#
# Handoff to master.mak
#

!include $(SDK_INSTALL_DIR)\build\master.mak
