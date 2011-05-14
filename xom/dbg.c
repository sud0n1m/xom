#include "efi.h"
#include "efilib.h"

#include "../lib/jlaefi.h"
#include "pmrm.h"
#include "dbg.h"
#include "i386.h"
#include "txt.h"

extern EFI_BLOCK_IO* hd80;

INT32 dbgSavedInstruction = -1;
INT32 dbgDumpSize;
Address dbgBreakpoint;
Address dbgLastDump;
Address dbgLastDisasm;
Address dbgWatch;

Address addrFromString(CHAR16* ptr, UINT16 segment, Address dflt, CHAR16** endStr, UINTN* pFoundFlag, Registers* regs) {
  Address address;
  CHAR16* str;
  UINTN delim;

  str = txtGetString(&ptr, L":", &delim);
  if (!delim) {
    if (pFoundFlag)
      *pFoundFlag = 0;
    return dflt;
  }
  address.type = MODE;
  if (*str == '%') {
    address.type = address.type == ADDRT_FLAT ? ADDRT_REAL : ADDRT_FLAT;
    str++;
  }
  if (delim == ':') {
    address.segment = !StrCmp(L"cs", str) ? regs->cs :
                      !StrCmp(L"ds", str) ? regs->ds :
                      !StrCmp(L"es", str) ? regs->es :
                      !StrCmp(L"ss", str) ? regs->ss :
                      !StrCmp(L"fs", str) ? regs->fs :
                      !StrCmp(L"gs", str) ? regs->gs : xtoi(str);
    str = txtGetString(&ptr, 0, 0);
  }
  else
    address.segment = segment;
  address.offset = xtoi(str);
  if (endStr)
    *endStr = ptr;
  if (pFoundFlag)
    *pFoundFlag = 1;
  return address;
}

UINT32 dbgStart(Address addr, UINTN breakOnEntry) {
  dbgWatch = ADDR_NULL;
  if (breakOnEntry)
    dbgSetBreakpoint(addr);
  krnCall(addr);
  return 0;
}

Address disasm(Address sa, Address ea, INTN lines, UINTN f32bits) {
  CHAR16 buffer[80];
  UINTN i;

  while (addrGTE(ea, sa) && lines-- && !kbdGet()) {
    UINT32 size = DasmI386(buffer, sizeof(buffer), sa, f32bits, f32bits);
    UINT32 addr = addrToOffset(sa);

    Print(L"%s ", addrToString(sa));
    for (i = 0; i < 6; ++i) {
      if (i < size)
        Print(L"%02x ", peek8(addr + i));
      else
        Print(L"   ");
    }
    Print(L"%s\n", buffer);
    sa = addrAdd(sa, size);
  }
  return sa;
}

Address memdump(Address sa, Address ea) {
  while (addrGTE(ea, sa) && !kbdGet()) {
    UINT8* ptr = (UINT8*)addrToPointer(sa);
    UINTN i;

    Print(L"%s ", addrToString(sa));
    for (i = 0; i < 16; ++i)
      Print(L"%02x%c", ptr[i], i == 7 ? '-' : ' ');

    Print(L" *");
    for (i = 0; i < 16; ++i) {
      CHAR16 c = ptr[i];
      Print(L"%c", c < 0x20 || c > 0x7f ? '.' : c);
    }
    Print(L"*\n");

    sa = addrAdd(sa, 0x10);
  }
  return sa;
}

Address dumpRegs(Registers* regs) {
  if (regs) {
    Print(L"int %x called from %s. GDT=%XL%04X  IDT=%XL%04X\n", regs->interrupt, addrToString(CSIP), regs->gdtAddress, regs->gdtLimit, regs->idtAddress, regs->idtLimit);
    Print(L"   EAX=%X    EBX=%X    ECX=%X    EDX=%X    CR0=%X\n", regs->eax, regs->ebx, regs->ecx, regs->edx, regs->cr0);
    Print(L"   ESP=%X    EBP=%X    ESI=%X    EDI=%X    CR3=%X\n", regs->esp, regs->ebp, regs->esi, regs->edi, regs->cr3);
    Print(L"   DS=%04x ES=%04x FS=%04x GS=%04x SS=%04x FL=%08X\n", regs->ds, regs->es, regs->fs, regs->gs, regs->ss, regs->eflags);
    if (!addrIsNull(dbgWatch))
      memdump(dbgWatch, addrAdd(dbgWatch, 8));
    dbgLastDisasm = CSIP;
    return disasm(addrFromRegs(regs), ADDR_FLAT_MAX, 1, MODE == ADDRT_FLAT);
  }
  return ADDR_NULL;
}

UINTN setReg(Registers* regs, CHAR16* reg, UINTN value) {
  CHAR16 c = *reg++;
  switch (c) {
    case 'c': regs->cs = (UINT16)value; break;
    case 'd': regs->ds = (UINT16)value; break;
    case 'e':
      c = *reg++;
      switch (c) {
        case 'a': regs->eax = value; break;
        case 'b':
          c = *reg++;
          switch(c) {
            case 'x': regs->ebx = value; break;
            case 'p': regs->ebp = value; break;
            default: return 0;
          }
          break;
        case 'c': regs->ecx = value; break;
        case 'd':
          c = *reg++;
          switch(c) {
            case 'x': regs->edx = value; break;
            case 'i': regs->edi = value; break;
            default: return 0;
          }
          break;
        case 'i': regs->eip = value; break;
        case 's':
          c = *reg++;
          switch(c) {
            case 0: regs->es = (UINT16)value;
            case 'p': regs->esp = value; break;
            case 'i': regs->esi = value; break;
            default: return 0;
          }
          break;
        default: return 0;
      }
    case 'f':
      c = *reg++;
      switch (c) {
        case 's': regs->fs = (UINT16)value; break;
        case 'l': regs->eflags = value; break;
        case 0: return 0;
      }
      break;
    case 'g': regs->gs = (UINT16)value; break;
    case 's': regs->ss = (UINT16)value; break;
    default: return 0;
  }
  return 1;
}

typedef void (*op_t)(Address, Address, UINT8*, UINTN);

void fillOp(Address sa, Address ea, UINT8* buffer, UINTN bufferSize) {
  if (bufferSize == 1)
    SetMem(addrToPointer(sa), addrDiff(ea, sa), buffer[0]);
  else {
    UINT8* ptr = (UINT8*)addrToPointer(sa);
    UINT32 sz = addrDiff(ea, sa);
    UINTN j = 0;
    while (sz--) {
      *ptr++ = buffer[j++];
      j %= bufferSize;
    }
  }
}

void editOp(Address sa, Address ea, UINT8* buffer, UINTN bufferSize) {
  CopyMem(addrToPointer(sa), buffer, bufferSize);
}

void searchOp(Address sa, Address ea, UINT8* buffer, UINTN bufferSize) {
  UINT8* ptr = (UINT8*)addrToPointer(sa);
  UINT32 sz = addrDiff(ea, sa), ofs = 0;
  while (ofs <= sz - bufferSize) {
    UINTN j;
    for (j = 0; j < bufferSize && buffer[j] == ptr[j + ofs]; ++j);
    if (j == bufferSize)
      Print(L"%s ", addrToString(addrAdd(sa, ofs)));
    ofs++;
  }
}

UINTN runOp(CHAR16* ptr, UINT16 defaultSegment, UINTN fGetEA, op_t op, Registers* regs) {
  UINT8 buffer[256];
  UINTN f, i = 0;
  Address sa = addrFromString(ptr, defaultSegment, ADDR_NULL, &ptr, &f, regs);
  Address ea = fGetEA ? addrFromString(ptr, sa.segment, ADDR_NULL, &ptr, &f, regs) : sa;
  if (!f || fGetEA && addrLT(ea, sa))
    return 0;
  do {
    buffer[i] = (UINT8)txtGetHex(&ptr, &f);
  } while (f && ++i < sizeof(buffer));
  if (!i)
    return 0;
  op(sa, ea, buffer, i);
  return 1;
}

void dbgSetBreakpoint(Address breakpoint) {
  UINT32 ofs = addrToOffset(breakpoint);
  dbgBreakpoint = breakpoint;
  dbgSavedInstruction = peek8(ofs);
  poke8(ofs, 0xcc);
  // Print(L"Breakpoint address: %X old=%X new=%X\n", ofs, dbgSavedInstruction, peek8(ofs));
}

void xomEnableConsole();

void debug(Registers* regs) {
  Address nextAddress;

  xomEnableConsole();

  regs->eflags &= ~0x100;
  if (dbgSavedInstruction != -1) {
    poke8(addrToOffset(dbgBreakpoint), dbgSavedInstruction);
    dbgSavedInstruction = -1;

    if (addrEQ(addrAdd(CSIP, -1), dbgBreakpoint)) {
      regs->eip--;
    }
  }

  nextAddress = dumpRegs(regs);

  while (1) {
    CHAR16 buffer[1024];
    CHAR16* ptr = buffer;
    CHAR16 cmd;

    Input(L"-", buffer, 1024);
    Print(L"\n");

    // Eat whitespace
    eatWhitespace(&ptr);
    cmd = *ptr ? *ptr++ : ' ';

    switch (cmd) {
      case 'q':
        krnAbort();
        break;

      case 'p': // step over (proceed in debug.com lingo)
        dbgSetBreakpoint(nextAddress);
        return;

      case 'n': // single step
        regs->eflags |= 0x100;
        return;

      case 'g': { // go
        UINTN f;
        Address breakpoint = addrFromString(ptr, regs->cs, ADDR_NULL, &ptr, &f, regs);
        if (f)
          dbgSetBreakpoint(breakpoint);
        return;
      }

      case 'u': { // unassemble
        UINTN f;
        Address sa = addrFromString(ptr, regs->cs, dbgLastDisasm, &ptr, 0, regs);
        Address ea = addrFromString(ptr, sa.segment, addrAdd(sa, 32), &ptr, &f, regs);
        dbgLastDisasm = disasm(sa, f ? ea : ADDR_FLAT_MAX, f ? 0x7FFFFFFF : 16, MODE == ADDRT_FLAT);
        break;
      }

      case 'U': { // unassemble 32 bits
        UINTN f;
        Address sa = addrFromString(ptr, regs->cs, dbgLastDisasm, &ptr, 0, regs);
        Address ea = addrFromString(ptr, sa.segment, addrAdd(sa, 32), &ptr, &f, regs);
        dbgLastDisasm = disasm(sa, f ? ea : ADDR_FLAT_MAX, f ? 0x7FFFFFFF : 16, MODE == ADDRT_REAL);
        break;
      }

      case '@': { // set freeze point
        UINT8 freezeData[] = { 0xcc, 0x90 };
        // UINT8 freezeData[] = { 0xcc, 0xbf, 0, 0, 1, 0x80, 0xb9, 0xff, 0xff, 0, 0, 0xab, 0x90, 0x90, 0xfe, 0xc0, 0xe2, 0xf9, 0xeb, 0xed, 0xf4 };
        // UINT8 freezeData[] = { 0xcc, 0xbb, 0x10, 0, 0x8e, 0xdb, 0x66, 0xbe, 0, 0, 1, 0x80, 0xb9, 0xff, 0xff, 0x66, 0x67, 0x89, 6, 0x46, 0x66, 0xf7, 0xd0, 0xe2, 0xf6, 0xeb, 0xeb, 0xf4 };
        // UINT32 addr = txtGetHex(&ptr, &f);
        CopyMem((void*)0x17400, &krnIDT, 0x800);
        /*if (!f || addr < 0x301000)
          break;*/
        CopyMem((void*)(0x30125c - 0x2dbf50), freezeData, sizeof(freezeData));
        SetMem((void*)(0x3028ac - 0x2dbf50), 0x30294a - 0x3028ac, 0x90); // nop debugger hooks
        poke32(0x3028ac - 0x2dbf50, 0x900cc483); // recover stack adjustment
        // CopyMem((void*)(addr - 0x2dbf50), freezeData, sizeof(freezeData));
        //CopyMem((void*)0x20285, freezeData, sizeof(freezeData));
        break;
      }

      case 'I': { // patch IDT
        Address addr = addrFromString(ptr, regs->ds, addrFlatFromOffset(0x17400), &ptr, 0, regs);
        CopyMem(addrToPointer(addr), &krnIDT, 0x800);
        break;
      }

      case 'd': { // memory dump
        Address sa = addrFromString(ptr, regs->ds, dbgLastDump, &ptr, 0, regs);
        Address ea = addrFromString(ptr, sa.segment, addrAdd(sa, dbgDumpSize), &ptr, 0, regs);
        dbgLastDump = memdump(sa, ea);
        break;
      }

      case 'D': { // Dump size
        UINTN f;
        dbgDumpSize = txtGetHex(&ptr, &f);
        if (!f)
          goto error;
        break;
      }

      case 'm': {
        UINTN f;
        Address sa = addrFromString(ptr, regs->ds, ADDR_NULL, &ptr, 0, regs);
        Address ea = addrFromString(ptr, sa.segment, ADDR_NULL, &ptr, 0, regs);
        Address da = addrFromString(ptr, regs->ds, ADDR_NULL, &ptr, &f, regs);
        if (!f || addrLT(ea, sa))
          break;

        CopyMem(addrToPointer(da), addrToPointer(sa), addrDiff(ea, sa));
        break;
      }

      case 'e':
        if (!runOp(ptr, regs->ds, 0, &editOp, regs))
          goto error;
        break;

      case 'f':
        if (!runOp(ptr, regs->ds, 1, &fillOp, regs))
          goto error;
        break;

      case 's':
        if (!runOp(ptr, regs->ds, 1, &searchOp, regs))
          goto error;
        break;

      case 'l': {
        EFI_FILE* file;
        UINTN f;
        UINT32 fileSize, availableSize;
        CHAR16* filename = txtGetString(&ptr, 0, 0);
        Address sa = addrFromString(ptr, regs->ds, ADDR_NULL, &ptr, &f, regs);
        if (!f)
          goto error;
        availableSize = addrDiff(krnMemoryTop, sa);
        file = fileOpen(NULL, filename, 0);
        if (!file)
          break;
        fileSize = (UINT32)LibFileInfo(file)->FileSize;
        file->Read(file, &availableSize, addrToPointer(sa));
        if (availableSize != fileSize)
          Print(L"%s (%,d bytes. %,d read)\n", filename, fileSize, availableSize, sa, addrAdd(sa, availableSize));
        else
          Print(L"%s (%,d bytes)\n", filename, fileSize, sa, addrAdd(sa, availableSize));
        file->Close(file);
        break;
      }

      case 'w': {
        EFI_FILE* file;
        UINTN f;
        UINT32 size;
        CHAR16* filename = txtGetString(&ptr, 0, 0);
        Address sa = addrFromString(ptr, regs->ds, ADDR_NULL, &ptr, 0, regs);
        Address ea = addrFromString(ptr, regs->ds, ADDR_NULL, &ptr, &f, regs);
        if (!f || addrLT(ea, sa))
          goto error;
        size = addrDiff(ea, sa);
        file = fileOpen(NULL, filename, 1);
        if (!file)
          break;
        file->Write(file, &size, addrToPointer(sa));
        Print(L"Wrote %s (%,d bytes)\n", filename, size);
        file->Close(file);
        break;
      }

      case 'L': {
        UINTN f;
        UINT32 size;
        Address sa = addrFromString(ptr, regs->ds, ADDR_NULL, &ptr, 0, regs);
        UINT32 lba = txtGetHex(&ptr, 0);
        UINT32 cnt = txtGetHex(&ptr, &f);
        if (!f)
          goto error;
        size = cnt * 512;
        hd80->ReadBlocks(hd80, hd80->Media->MediaId, lba, size, addrToPointer(sa));
        Print(L"Read %,d sectors (%,d bytes)\n", cnt, size);
        break;
      }

      case 'W': {
        UINTN f;
        UINT32 size;
        Address sa = addrFromString(ptr, regs->ds, ADDR_NULL, &ptr, 0, regs);
        UINT32 lba = txtGetHex(&ptr, 0);
        UINT32 cnt = txtGetHex(&ptr, &f);
        if (!f)
          goto error;
        size = cnt * 512;
        hd80->WriteBlocks(hd80, hd80->Media->MediaId, lba, size, addrToPointer(sa));
        hd80->FlushBlocks(hd80);
        Print(L"Wrote %,d sectors (%,d bytes)\n", cnt, size);
        break;
      }

      case 'k': {
        EFI_FILE* file;
        void* pBuffer;
        UINTN f, size;
        CHAR16* filename = txtGetString(&ptr, 0, 0);
        UINT32 lba = txtGetHex(&ptr, 0);
        UINT32 cnt = txtGetHex(&ptr, &f);
        if (!f)
          goto error;
        file = fileOpen(NULL, filename, 1);
        if (!file) {
          Print(L"Unable to open file %s for writing\n", filename);
          break;
        }
        size = cnt * 512;
        pBuffer = AllocatePool(size);
        if (!pBuffer) {
          Print(L"Unable to allocate %,d bytes for buffer\n", size);
          break;
        }
        hd80->ReadBlocks(hd80, hd80->Media->MediaId, lba, size, pBuffer);
        file->Write(file, &size, pBuffer);
        Print(L"Wrote %,d sectors to %s (%,d bytes)\n", cnt, filename, size);
        file->Close(file);
        FreePool(pBuffer);
        break;
      }

      case 'r': { // registers
        UINTN f;
        CHAR16* reg = txtGetString(&ptr, 0, 0);
        UINT32 value = txtGetHex(&ptr, &f);
        if (!f) {
          dumpRegs(regs);
          break;
        }
        if (!setReg(regs, reg, value))
          goto error;
        dumpRegs(regs);
        break;
      }

      /*case 'y':
        regs->flags |= 0x100;
        dbgInstructionBreakpoint = txtGetString(&ptr, 0, 0);
        break;*/

      case 'z': // watch
        dbgWatch = addrFromString(ptr, regs->ds, ADDR_NULL, &ptr, 0, regs);
        break;

      case '!': // toggle paging
        addrEnablePaging(1 - addrIsPagingEnabled());
        Print(L"Paging %s\n", addrIsPagingEnabled() ? L"enabled" : L"disabled");
        break;

      case ' ':
        break;

error:
      default:
        Print(L"?Syntax error\n");
    }
  }
}
