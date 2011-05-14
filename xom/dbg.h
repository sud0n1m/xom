#ifndef __DEBUGGER_H
#define __DEBUGGER_H

#include "pmrm.h"

typedef UINT32 SEGOFS;

void dbgSetBreakpoint(Address breakpoint);
UINT32 dbgStart(Address addr, UINTN breakOnEntry);
void debug(Registers* regs);

static UINT32 xSEG(SEGOFS segOfs) { return segOfs >> 16; }
static UINT32 xOFS(SEGOFS segOfs) { return segOfs & 0xffff; }
static UINT32 xSOADDR(SEGOFS segOfs) { return (xSEG(segOfs) << 4) + xOFS(segOfs); }
static UINT32 xMakeSEGOFS(UINT16 segment, UINT16 offset) { return (segment << 16) | offset; }

#endif // __DEBUGGER_H