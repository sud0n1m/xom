#ifndef __PMRM_H
#define __PMRM_H

#include "efi.h"
#include "efilib.h"

#include "addr.h"

static void poke8 (UINT32 addr, UINT8  value) { *((INT8* )addr) = value; }
static void poke16(UINT32 addr, UINT16 value) { *((INT16*)addr) = value; }
static void poke32(UINT32 addr, UINT32 value) { *((INT32*)addr) = value; }
static void poke64(UINT32 addr, UINT64 value) { *((INT64*)addr) = value; }
static UINT8  peek8 (UINT32 addr) { return *((INT8*)addr); }
static UINT16 peek16(UINT32 addr) { return *((INT16*)addr); }
static UINT32 peek32(UINT32 addr) { return *((INT32*)addr); }
static UINT64 peek64(UINT32 addr) { return *((INT64*)addr); }

#define RL(n)               ((n) & 0xff)
#define RH(n)               (((n) >> 8) & 0xff)
#define RW(n)               ((n) & 0xffff)
#define WL(n, v)            ((n) = (n) & 0xffffff00 | ((v) & 0xff))
#define WH(n, v)            ((n) = (n) & 0xffff00ff | (((v) & 0xff) << 8))
#define WW(n, v)            ((n) = (n) & 0xffff0000 | ((v) & 0xffff))
#define SO(s, o)            ((((s) & 0xffff) << 4) + ((o) & 0xffff))
#define CLC                 (regs->eflags &= ~1)
#define STC                 (regs->eflags |= 1)
#define CLZ                 (regs->eflags &= ~0x40)
#define STZ                 (regs->eflags |= 0x40)
#define CSIP                addrFromRegs(regs)
#define MODE                (regs->cr0 & 1 ? ADDRT_FLAT : ADDRT_REAL)
#define BCD(n)              ((((n) / 10) << 4) | ((n) % 10))

#define CONTEXT             ((Registers*)0xE4000)

typedef struct Registers_t {
  #pragma pack(push, 2)
  UINT32 eax;
  UINT32 ecx;
  UINT32 edx;
  UINT32 ebx;
  UINT32 esp;
  UINT32 ebp;
  UINT32 esi;
  UINT32 edi;
  UINT32 cs;
  UINT32 ds;
  UINT32 es;
  UINT32 fs;
  UINT32 gs;
  UINT32 ss;
  UINT32 eip;
  UINT32 eflags;
  UINT32 cr0;
  UINT32 cr3;
  UINT16 gdtLimit;
  UINT32 gdtAddress;
  UINT16 idtLimit;
  UINT32 idtAddress;
  UINT32 hEax;
  UINT32 hEcx;
  UINT32 hEdx;
  UINT32 hEbx;
  UINT32 hEsp;
  UINT32 hEbp;
  UINT32 hEsi;
  UINT32 hEdi;
  UINT32 hCs;
  UINT32 hDs;
  UINT32 hEs;
  UINT32 hFs;
  UINT32 hGs;
  UINT32 hSs;
  UINT32 hEip;
  UINT32 hEflags;
  UINT32 hCr0;
  UINT32 hCr3;
  UINT16 hGdtLimit;
  UINT32 hGdtAddress;
  UINT16 hIdtLimit;
  UINT32 hIdtAddress;
  UINT32 interrupt;
  #pragma pack(pop)
} Registers;

Address addrFromRegs(Registers* regs);

void krnInit();
Address krnAddress(void* ptr);
UINT32 krnCall(Address addr);
UINTN krnAbort();

extern UINT8 krnIDT;
extern Address krnMemoryTop;

extern UINT16 krnVesaXResolution;
extern UINT16 krnVesaYResolution;
extern UINT8  krnVesaBitsPerPixel;
extern UINT16 krnVesaScanline;
extern UINT16 krnVesa3Scanline;
extern UINT32 krnVesaLFBStart;
extern UINT32 krnVesaLFBEnd;
extern UINT16 krnVesaLFBUKB;

#endif // __PMRM_H