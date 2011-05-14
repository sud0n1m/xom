#include "efi.h"
#include "efilib.h"

#include "../lib/jlaefi.h"
#include "pmrm.h"
#include "addr.h"
#include "dbg.h"

UINTN krnInitAsm();
UINTN krnClientCall();

Address krnMemoryTop;

void initGDT() {
  UINT64 r;
  UINT32 gdtLimit;
  UINT32 gdtAddress;
  UINT32 selector;

  __asm sgdt r
  gdtLimit = (UINT32)(r & 0xFFFF);
  gdtAddress = (UINT32)((r >> 16) & 0xFFFFFFFF);
  Print(L"GDT Address: %X  Limit: %X.\n", gdtAddress, gdtLimit);

  // Use the last available selector slot
  selector = gdtLimit + 1 - 16;

  // Verify it's 0x30
  if (selector != 0x30) {
    Print(L"Incorrect EFI GDT size %x. Selector 0x30 & 0x38 not available.\n", gdtLimit + 1);
    exit(EFI_SUCCESS);
  }

  // Define selectors
  Print(L"Creating 16-bit code selectors %x, %x\n", selector, selector + 8);
  // if (peek64(gdtAddress + selector) || peek64(gdtAddress + data16Selector)) {
  //   Print(L"Selectors 0x%x and 0x%x in use (%lX, %lX). Can't proceed.\n", selector, data16Selector, peek64(gdtAddress + selector), peek64(gdtAddress + data16Selector));
  //   return EFI_ABORTED;
  // }
  poke64(gdtAddress + selector    , 0x00009A0E0000FFFF); // base 0xE0000 limit 0xffff
  poke64(gdtAddress + selector + 8, 0x0000920E0000FFFF); // base 0xE0000 limit 0xffff
}

void krnInit() {
  krnMemoryTop = addrRealFromSegOfs(0xA000, 0x0000);

  //
  // Allocate protected mode selector
  //
  initGDT();

  //
  // Initialize pm/rm library
  //
  krnInitAsm();

  Print(L"krnIDT: %X\n", &krnIDT);

  if (0) {
    debug(CONTEXT);
  }
}

UINT32 krnAdjustAddress(void* ptr);

Address krnAddress(void* ptr) {
  UINT32 adjAddress = krnAdjustAddress(ptr);
  Address address;
  address.offset = adjAddress & 0xFFFF;
  address.segment = (adjAddress & 0xF0000) >> 4;
  address.type = ADDRT_REAL;
  return address;
}

Address addrFromRegs(Registers* regs) {
  Address address;
  address.offset = regs->eip;
  address.segment = regs->cs;
  address.type = MODE;
  return address;
}

UINT32 krnCall(Address address) {
  Registers* regs = CONTEXT;
  regs->eip = address.offset;
  regs->cs = address.segment;
  regs->cr0 = address.type == ADDRT_FLAT ? 1 : 0;
  return krnClientCall();
}
