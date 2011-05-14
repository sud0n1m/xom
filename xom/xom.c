/*++

Copyright (c) 2006 JLA

Module Name:

    xom.c

Abstract:

    We'll see where this goes...

Revision History

--*/

#include "efi.h"
#include "efilib.h"

#include "../lib/jlaefi.h"
#include "pmrm.h"
#include "dbg.h"

extern UINT8 SysConf;

UINT8* floppyImage;
EFI_BLOCK_IO* hd82 = NULL;
EFI_BLOCK_IO* hd80 = NULL;
EFI_BLOCK_IO* pCDROMBlockIO = NULL;
EFI_SCSI_PASS_THRU_PROTOCOL* pCDROMScsi;
EFI_HANDLE hCDROM = NULL;
SIMPLE_INPUT_INTERFACE* pKeyboard;
SIMPLE_TEXT_OUTPUT_INTERFACE* pScreen;
UINT32 BiosMapStart = 0x9e000, BiosMapEnd;

UINTN enableConsole = 0;
UINTN enableDebugger = 0;
UINTN enableElTorito = 1;
UINTN enableROMs = 0;

UINT8* scrGlyphTable;

typedef struct biosMemoryMap_t {
  UINT64 baseAddress;
  UINT64 length;
  UINT32 type;
} BiosMemoryMapEntry;

typedef enum {
  BootEfi,
  BootLegacy,
  BootMaxType,
} BootType;

void xomDetectHardware();
BootType xomChooseBoot();
void xomBootOSX();
void xomBootLegacy();
void xomInit();
void lcyInit();

extern UINT8  binary_osx_z_start[];
extern UINT32 binary_osx_z_size;
extern UINT8  binary_wxpl_z_start[];
extern UINT32 binary_wxpl_z_size;
extern UINT8  binary_wxp_z_start[];
extern UINT32 binary_wxp_z_size;

TGA* tgaApple;
TGA* tgaWindowsLoading;
TGA* tgaWindows;

typedef enum {
  AtiM56,
  i945IGD,
  VideoCardMax,
} VIDEOCARD_TYPE;

UINT32 vidResolutionX;
UINT32 vidResolutionY;
UINT32 vidScanlineSize;
UINT32 vidBitsPerPixel;
UINT32 vidRefreshRate;
UINT32 vidFrameBuffer;
UINT32 vidVRAMSize;
VIDEOCARD_TYPE vidVideoCard;

EFI_STATUS
InitializeXpOnMac (
    IN EFI_HANDLE           ImageHandle,
    IN EFI_SYSTEM_TABLE     *SystemTable
    );

EFI_DRIVER_ENTRY_POINT (InitializeXpOnMac)

EFI_STATUS
InitializeXpOnMac (
    IN EFI_HANDLE           ImageHandle,
    IN EFI_SYSTEM_TABLE     *SystemTable
    )
{

    //
    // Initialize the Library.
    //
    InitJLAEFI(ImageHandle, SystemTable);
    pKeyboard = SystemTable->ConIn;
    pScreen = SystemTable->ConOut;
    BS->SetWatchdogTimer(0, 0, 0, 0);

    // Banner
    Print(L"XpOnMac v0.01 (C) JLA 2006.\n");

    // Initialize images
    tgaApple          = decompress(binary_osx_z_start , (UINT32)&binary_osx_z_size );
    tgaWindows        = decompress(binary_wxp_z_start , (UINT32)&binary_wxp_z_size );
    tgaWindowsLoading = decompress(binary_wxpl_z_start, (UINT32)&binary_wxpl_z_size);

    // Determine iMac type
    xomDetectHardware();

    // Choose
    switch (xomChooseBoot()) {
      case BootEfi:
        xomBootOSX();
        break;

      case BootLegacy:
        // Init CSM
        if (!enableConsole)
          tgaDraw(tgaWindowsLoading, vidResolutionX / 2, vidResolutionY /  2, tgaCentered);

        // Boot legacy
        xomBootLegacy();
        break;
    }
    return EFI_SUCCESS;
}

void xomEnableConsole() {
  if (!enableConsole) {
    consoleControl(0);
    enableConsole = 1;
  }
}

void xomDetectHardware() {
  // iMac 17 and MacBook pro are 1440x900
  // mini is 1280x1024 during EFI initalization
  status = pUgaDraw->GetMode(pUgaDraw, &vidResolutionX, &vidResolutionY, &vidBitsPerPixel, &vidRefreshRate);
  if (EFI_ERROR(status)) {
    Print(L"Unable to query UGA Draw for resolution. Assuming iMac 17\" defaults\n");
    vidResolutionX = 1440;
    vidResolutionY = 900;
  }

  // Now detect video card based on current resolution (how bad is this??)
  if (vidResolutionX == 1440 && vidResolutionY == 900 && vidBitsPerPixel == 32) {
    // iMac 17 or MBP
    vidScanlineSize = 0x1700;
    vidFrameBuffer = 0x80010000;
    vidVRAMSize = 0x8000000; // 128 MB
    vidVideoCard = AtiM56;
  }
  else if (vidResolutionX == 1680 && vidResolutionY == 1050) {
    // iMac20
    vidScanlineSize = 0x1B00;
    vidFrameBuffer = 0x80010000;
    vidVRAMSize = 0x8000000;
    vidVideoCard = AtiM56;
  }
  else if (vidResolutionX == 1280 && vidResolutionY == 1024) {
    // mini
    vidScanlineSize = 0x2000;
    vidFrameBuffer = 0x80000000;
    vidVRAMSize = 0x4000000; // 64 MB
    vidVideoCard = i945IGD;
  }
  else {
    // TODO: what else? Let's go for mini's defaults
    Print(L"Warning: Unable to detect video card\n");
    vidScanlineSize = 0x2000;
    vidFrameBuffer = 0x80000000;
    vidVRAMSize = 0x4000000; // 64 MB
    vidVideoCard = i945IGD;
  }

  Print(L"Using resolution %dx%dx%d@%d. ScanLineSize=0x%04x. Video=%d\n",
    vidResolutionX, vidResolutionY, vidBitsPerPixel, vidRefreshRate, vidScanlineSize, vidVideoCard);
}

void xomBootError(TGA* img) {
  // Error
  img->descriptor &= 0xDF;
  if (!enableConsole)
    tgaDraw(img, vidResolutionX / 2, vidResolutionY /  2, tgaCentered);
  else
    Print(L"Boot Error----- Enter: Reboot     F5: Exit\n");
  while (1) {
    CHAR16 key = kbdGet();
    if (!key)
      continue;

    switch (key) {
      case 0x800E: // F4
        xomEnableConsole();
        break;
      case 0x800F: // F5
        return;
      case 0x000D: // enter
      case 0x0020: // space
        RT->ResetSystem(EfiResetCold, EFI_NOT_FOUND, 0, 0);
        return;
    }
  }
}

void xomBootOSX() {
  EFI_STATUS                      Status;

  UINTN                           Index;

  UINTN                           NoHandles;
  EFI_HANDLE                      *Handles;

  Status = LibLocateHandle(ByProtocol, &BlockIoProtocol, NULL, &NoHandles, &Handles);
  for (Index = 0; Index < NoHandles; Index++) {
    EFI_HANDLE               handle = Handles[Index];
    EFI_DEVICE_PATH*         DevPath;
    CHAR16*                  devPathStr;

    DevPath = DevicePathFromHandle(handle);
    if (!DevPath)
      continue;

    devPathStr = DevicePathToStr(DevPath);
    if (!devPathStr)
      continue;

    if (StrStr(devPathStr, L"/Ata(Primary,Slave)/")) {
      EFI_BLOCK_IO*                 pBlockIO;

      Status = BS->HandleProtocol(handle, &BlockIoProtocol, &pBlockIO);
      if (EFI_ERROR(Status))
        continue;

      Print(L"xomBootOSX: Attempting boot from %s\n", devPathStr);
      exec(handle, L"System\\Library\\CoreServices\\boot.efi");
    }
  }
  xomBootError(tgaApple);
}

void updateSelection(BootType selection) {
  Print(L"%d: Boot %s       \r", selection, selection == BootLegacy ? L"Windows XP" : L"Mac OS X");
  if (!enableConsole)
    tgaDraw(selection == BootLegacy ? tgaWindows : tgaApple,
            vidResolutionX / 2, vidResolutionY /  2, tgaCentered);
}

BootType xomChooseBoot() {
  UINTN selection = BootEfi;

  updateSelection(selection);
  while (1) {
    CHAR16 key = kbdGet();
    if (!key)
      continue;

    switch (key) {
      case 0x8001: // up
      case 0x8005: // home
        if (selection > 0)
          updateSelection(--selection);
        break;

      case 0x8002: // down
      case 0x8006: // end
        if (selection < BootMaxType - 1)
          updateSelection(++selection);
        break;

      case 0x0D: // enter

        return selection;

      case 0x800B: // F1
        // legacy boot with break
        if (enableConsole) {
          Print(L"Debugger enabled                 \n");
          enableDebugger = 1;
          // Enter text mode
          xomEnableConsole();
          return BootLegacy;
        }
        break;

      case 0x800C: // F2
        // boot HD
        Print(L"El Torito disabled               \r");
        enableElTorito = 0;
        break;

      case 0x800D: // F3
        // boot CD
        Print(L"El Torito enabled                \r");
        enableElTorito = 1;
        break;

      case 0x800E: // F4
        // disable screen
        Print(L"Console enabled                  \r");
        xomEnableConsole();
        break;

      case 0x800F: // F5
        // exit
        if (enableConsole) {
          Print(L"Goodbye...                     \n");
          exit(EFI_SUCCESS);
        }
        break;

      case 0x8010: // F6
        // toggle ROM usage
        if (enableConsole) {
          enableROMs = 1 - enableROMs;
          Print(L"ROMs are %s                      \r", enableROMs ? L"enabled" : L"disabled");
        }
        break;

      case 0x8011: // F7
        // Manual video
        if (enableConsole) {
          vidResolutionX = inputDec(L"X Resolution       ", vidResolutionX);
          vidResolutionY = inputDec(L"Y Resolution       ", vidResolutionY);
          vidBitsPerPixel= inputDec(L"BitsPerPixel       ", vidBitsPerPixel);
          vidScanlineSize= inputHex(L"Scanline size      ", vidScanlineSize);
          vidFrameBuffer = inputHex(L"Linear Frame Buffer", vidFrameBuffer);
          vidVideoCard   = inputDec(L"Video Card         ", vidVideoCard);
          updateSelection(selection);
        }
    }
  }
}

UINTN bootElTorito(EFI_BLOCK_IO* pBlockIO) {
  Address      bootAddress = addrRealFromSegOfs(0x0000, 0x7C00);
  UINT8        sectorBuffer[2048];
  EFI_STATUS   Status;
  EFI_LBA      lba;
  UINT32       bootLoadSegment;
  Address      bootLoadAddress;
  UINT32       bootSize;
  UINT32       bootSectors;

  // No device, no game
  if (!pBlockIO) {
    Print(L"CDROMBoot: No CDROM to boot from\n");
    return 0;
  }

  // Load El Torito boot record volume descriptor
  Status = pBlockIO->ReadBlocks(pBlockIO, pBlockIO->Media->MediaId, 0x11, 2048, sectorBuffer);
  if (EFI_ERROR(Status)) {
    // Retry in case the CD was swapped out
    Status = BS->HandleProtocol(hCDROM, &BlockIoProtocol, &pBlockIO);
    if (!EFI_ERROR(Status)) {
      pCDROMBlockIO = pBlockIO;
      Status = pBlockIO->ReadBlocks(pBlockIO, pBlockIO->Media->MediaId, 0x11, 2048, sectorBuffer);
    }
    if (EFI_ERROR(Status)) {
      Print(L"CDROMBoot: Unable to read block %X: %r\n", 0x11, Status);
      return 0;
    }
  }

  if (strcmpa(sectorBuffer + 0x7, "EL TORITO SPECIFICATION")) {
    Print(L"CDROMBoot: Not an El Torito Specification disk\n");
    return 0;
  }

  // Find the boot catalog
  lba = sectorBuffer[0x47] + sectorBuffer[0x48] * 256 + sectorBuffer[0x49] * 65536 + sectorBuffer[0x4A] * 16777216;
  Status = pBlockIO->ReadBlocks(pBlockIO, pBlockIO->Media->MediaId, lba, 2048, sectorBuffer);
  if (EFI_ERROR(Status)) {
    Print(L"CDROMBoot: Unable to read block %X: %r\n", lba, Status);
    return 0;
  }

  if (sectorBuffer[0x00] != 1 || sectorBuffer[0x1E] != 0x55 || sectorBuffer[0x1F] != 0xAA) {
    Print(L"CDROMBoot: Invalid El Torito validation entry in boot catalog LBA %X\n", lba);
    DumpHex(0, 0, 64, sectorBuffer);
    return 0;
  }

  if (sectorBuffer[0x01] != 0) {
    Print(L"CDROMBoot: Platform mismatch: %d\n", sectorBuffer[0x01]);
    return 0;
  }

  if (sectorBuffer[0x20] != 0x88) {
    Print(L"CDROMBoot: CD-ROM is not bootable\n");
    return 0;
  }

  if (sectorBuffer[0x21] != 0) {
    Print(L"CDROMBoot: Currently only non-emulated CDROMs are supported");
    return 0;
  }

  bootLoadSegment = sectorBuffer[0x22] + sectorBuffer[0x23] * 256;
  if (!bootLoadSegment)
    bootLoadSegment = 0x7C0;
  bootSectors = sectorBuffer[0x26] + sectorBuffer[0x27] * 256;
  bootSize = bootSectors * pBlockIO->Media->BlockSize;
  bootLoadAddress = addrRealFromSegOfs(bootLoadSegment, 0);
  if (addrLT(bootLoadAddress, bootAddress) || addrGTE(bootLoadAddress, krnMemoryTop)) {
    Print(L"CDROMBoot: Illegal boot load address %sL%x\n", addrToString(bootLoadAddress), bootSize);
    return 0;
  }

  lba = sectorBuffer[0x28] + sectorBuffer[0x29] * 256 + sectorBuffer[0x2A] * 65536 + sectorBuffer[0x2B] * 16777216;
  Print(L"CDROMBoot: Booting LBA %ld @%sL%x\n", lba, addrToString(bootLoadAddress), bootSize);

  // Read the boot sectors into the boot load address
  Status = pBlockIO->ReadBlocks(pBlockIO, pBlockIO->Media->MediaId, lba, bootSize, addrToPointer(bootLoadAddress));
  if (EFI_ERROR(Status)) {
    Print(L"CDROMBoot: Unable to read block %ld: %r\n", lba, Status);
    return 0;
  }

  // Configure drive
  hd82 = pBlockIO;

  // Initialize Registers
  CONTEXT->edx = 0x82;

  // Boot it
  dbgStart(bootLoadAddress, enableDebugger);

  // Success - Should never get here unless debugger aborts
  return 1;
}

UINTN bootMBR(EFI_BLOCK_IO* pDisk) {
  typedef struct _partition_t {
    UINT8 Drive;
    UINT8 StartCHS[3];
    UINT8 Type;
    UINT8 EndCHS[3];
    UINT32 Start;
    UINT32 Size;
  } partition_t;
  EFI_STATUS Status;
  UINT8* pMBR = (void*)0x600;
  UINT8* pBootSector = (void*)0x7C00;
  partition_t* activePartition = 0;
  UINTN partitionIndex;

  // No device, no game
  if (!pDisk) {
    Print(L"HDBoot: No HD to boot from\n");
    return 0;
  }

  // Read the MBR
  Status = pDisk->ReadBlocks(pDisk, pDisk->Media->MediaId, 0, 512, pMBR);
  if (EFI_ERROR(Status)) {
    Print(L"HDBoot: Unable to read MBR: %r\n", 0x11, Status);
    return 0;
  }

  // Check validity of MBR
  if (pMBR[510] != 0x55 || pMBR[511] != 0xAA) {
    Print(L"HDBoot: Invalid MBR signature 0x%02X%02X (not 0xAA55)\n", pMBR[511], pMBR[510]);
    return 0;
  }

  // Traverse partitions
  for (partitionIndex = 0; partitionIndex < 4; ++partitionIndex) {
    partition_t* partition = (partition_t*)(pMBR + 0x1BE + sizeof(partition_t) * partitionIndex);

    // Not the active partition?
    if (partition->Drive != 0x80)
      continue;

    // Is the partition valid?
    if (partition->Start == 0 || partition->Size == 0) {
      Print(L"HDBoot: Invalid active partition %d: (%08X L %08X)\n", partition->Start, partition->Size);
      return 0;
    }

    activePartition = partition;
    break;
  }

  // No active partitions found?
  if (!activePartition) {
    Print(L"HDBoot: No active partitions found.\n");
    return 0;
  }

  Print(L"HDBoot: Found active partition #%d.\n", partitionIndex);

  // Read the boot sector
  Status = pDisk->ReadBlocks(pDisk, pDisk->Media->MediaId, activePartition->Start, 512, pBootSector);
  if (EFI_ERROR(Status)) {
    Print(L"HDBoot: Unable to read partition %d's boot sector: %r\n", partitionIndex, status);
    return 0;
  }

  // Check boot sector
  if (pBootSector[0x1FE] != 0x55 || pBootSector[0x1FF] != 0xAA) {
    Print(L"HDBoot: Invalid Boot Sector signature 0x%02X%02X (not 0xAA55)\n", pBootSector[0x1FF], pBootSector[0x1FE]);
    return 0;
  }

  Print(L"HDBoot: Found valid boot sector on partition #%d. Booting...\n", partitionIndex);

  // Initialize Registers
  CONTEXT->edx = 0x80;
  CONTEXT->esi = (UINT16)activePartition;
  CONTEXT->ds = 0;
  CONTEXT->ds = 0;
  CONTEXT->es = 0;
  CONTEXT->ss = 0;
  CONTEXT->esp = 0x7C00;
  CONTEXT->cs = 0;
  CONTEXT->eip = 0x7C00;

  // Boot it
  dbgStart(addrRealFromPointer(pBootSector, /*segment*/0), enableDebugger);

  // Success - Should never get here unless debugger aborts
  return 1;

}

typedef struct {
  UINT8 c, h, s;
} CHS;

static CHS chsValue = { 0xFF, 0xFF, 0xFF };

typedef struct {
#pragma pack(push, 1)
  UINT8  drive;
  CHS    chsStart;
  UINT8  type;
  CHS    chsEnd;
  UINT32 lbaStart;
  UINT32 lbaSize;
#pragma pack(pop)
} MBR_ENTRY;

typedef struct {
#pragma pack(push, 1)
  UINT8 loader[0x1BE];
  MBR_ENTRY p[4];
  UINT16 signature;
#pragma pack(pop)
} MBR;

typedef struct {
#pragma pack(push, 1)
  UINT64 signature;
  UINT32 revision;
  UINT32 headerSize;
  UINT32 headerCRC;
  UINT32 reserved;
  UINT64 myLBA;
  UINT64 alternateLBA;
  UINT64 firstUsableLBA;
  UINT64 lastUsableLBA;
  EFI_GUID diskGUID;
  UINT64 partitionEntryLBA;
  UINT32 numberOfPartitionEntries;
  UINT32 sizeOfPartitionEntry;
  UINT32 partitionEntryArrayCRC32;
  UINT8  filler[];
#pragma pack(pop)
} GPT_HEADER;

typedef struct {
#pragma pack(push, 1)
  EFI_GUID   partitionType;
  EFI_GUID   partitionGuid;
  UINT64 startingLBA;
  UINT64 endingLBA;
  UINT64 attributes;
  CHAR16 partitionName[72 / 2];
#pragma pack(pop)
} GPT_ENTRY;

static EFI_GUID GPT_EFI_SYSTEM_PARTITION = \
  { 0xc12a7328, 0xf81f, 0x11d2, 0xba, 0x4b, 0x00, 0xa0, 0xc9, 0x3e, 0xc9, 0x3b };
static EFI_GUID GPT_MSDOS_PARTITION = \
  { 0xebd0a0a2, 0xb9e5, 0x4433, 0x87, 0xc0, 0x68, 0xb6, 0xb7, 0x26, 0x99, 0xc7 };
static EFI_GUID GPT_HFSPLUS_PARTITION = \
  { 0x48465300, 0x0000, 0x11aa, 0xaa, 0x11, 0x00, 0x30, 0x65, 0x43, 0xec, 0xac };
static EFI_GUID GPT_EMPTY_PARTITION = \
  { 0x00000000, 0x0000, 0x0000, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

void mbrUpdate(EFI_BLOCK_IO* pDisk) {
  static UINT8 buffer[1024], buffer2[1024];
  MBR* mbr = (MBR*)buffer;
  GPT_HEADER* gpt = (GPT_HEADER*)(buffer + 0x200);
  UINTN wipeDOS = 0, count, i, activePartition = 0;

  status = pDisk->ReadBlocks(pDisk, pDisk->Media->MediaId, 0, sizeof(buffer), buffer);
  if (EFI_ERROR(status)) {
    Print(L"mbrUpdate: Unable to read partition tables: %r\n", status);
    return;
  }

  if (mbr->signature != 0xAA55)
    Print(L"mbrUpdate: Invalid MBR signature %04x. Rebuilding MBR...\n", mbr->signature);
  else
    wipeDOS = !mbr->loader[0] && !mbr->loader[1] && !mbr->loader[2];

  if (gpt->signature != 0x5452415020494645l) {
    Print(L"mbrUpdate: Invalid GPT signature %16lx. Cancelling MBR update.\n", gpt->signature);
    return;
  }

  count = gpt->numberOfPartitionEntries;
  if (count > 4) {
    Print(L"mbrUpdate: GPT defines %d partitions. mbrUpdate will only mimic the first four partitions.\n", count);
    count = 4;
  }

  status = pDisk->ReadBlocks(pDisk, pDisk->Media->MediaId, gpt->partitionEntryLBA, sizeof(buffer2), buffer2);
  if (EFI_ERROR(status)) {
    Print(L"mbrUpdate: Unable to read GPT entries at LBA %X: %r\n", gpt->partitionEntryLBA, status);
    return;
  }

  for (i = 0; i < 4; ++i) {
    GPT_ENTRY* gptEntry = (GPT_ENTRY*)(buffer2 + i * gpt->sizeOfPartitionEntry);
    MBR_ENTRY* mbrEntry = &mbr->p[i];

    if (i < count && CompareGuid(&gptEntry->partitionType, &GPT_EMPTY_PARTITION)) {
      if (gptEntry->startingLBA > 0x100000000l || gptEntry->endingLBA > 0x100000000l) {
        Print(L"mbrUpdate: Partition #%d is beyond 32 bit LBA limit. Ignoring...\n", i);
        goto zeroPartition;
      }

      mbrEntry->drive = 0;
      mbrEntry->chsStart = chsValue;
      if (!CompareGuid(&gptEntry->partitionType, &GPT_MSDOS_PARTITION)) {
        Print(L"mbrUpdate: Partition #%d - MSDOS %X-%X %s\n", i, (UINT32)gptEntry->startingLBA, (UINT32)gptEntry->endingLBA, wipeDOS ? L"ERASING!!" : L"");

        if (wipeDOS) {
          static UINT8 zeroBootSector[512] = {0};
          status = pDisk->WriteBlocks(pDisk, pDisk->Media->MediaId, gptEntry->startingLBA, sizeof(zeroBootSector), zeroBootSector);
          if (EFI_ERROR(status))
            Print(L"mbrUpdate: Unable to erase MSDOS partition #%d: %r\n", i, status);
        }
        else {
          if (!activePartition) {
            mbrEntry->drive = 0x80;
            activePartition = 1;
          }
        }
      }
      else {
        mbrEntry->type = !CompareGuid(&gptEntry->partitionType, &GPT_EFI_SYSTEM_PARTITION) ? 0xEF : 0xAF;
        Print(L"mbrUpdate: Partition #%d - Type %02x %X-%X\n", i, mbrEntry->type, (UINT32)gptEntry->startingLBA, (UINT32)gptEntry->endingLBA);
      }
      mbrEntry->chsEnd = chsValue;
      mbrEntry->lbaStart = (UINT32)gptEntry->startingLBA;
      mbrEntry->lbaSize = (UINT32)gptEntry->endingLBA + 1 - mbrEntry->lbaStart;
    }
    else {
zeroPartition:
      Print(L"mbrUpdate: Partition #%d - Empty\n", i);
      ZeroMem(&mbr->p[i], sizeof(MBR_ENTRY));
    }
  }
  mbr->signature = 0xAA55;

  status = pDisk->WriteBlocks(pDisk, pDisk->Media->MediaId, 0, sizeof(MBR), mbr);
  if (EFI_ERROR(status))
    Print(L"mbrUpdate: Unable to update mbr: %r\n", status);

  Print(L"mbrUpdate: Update at %X\n", mbr);
}

void xomBootLegacy() {
  EFI_STATUS                      Status;

  UINTN                           Index;

  UINTN                           NoHandles;
  EFI_HANDLE                      *Handles;

  lcyInit();

  // First try DISK IO guys (We're looking for a CD-ROM or an HD)
  Status = LibLocateHandle(ByProtocol, &BlockIoProtocol, NULL, &NoHandles, &Handles);
  for (Index = 0; Index < NoHandles; Index++) {
    EFI_HANDLE               handle = Handles[Index];
    EFI_DEVICE_PATH*         DevPath;
    CHAR16*                  devPathStr;

    DevPath = DevicePathFromHandle(handle);
    if (!DevPath)
      continue;

    devPathStr = DevicePathToStr(DevPath);
    if (StrEndsWith(devPathStr, L"/Ata(Primary,Master)")) {
      EFI_BLOCK_IO*                 pBlockIO;
      // EFI_SCSI_PASS_THRU_PROTOCOL*  pScsi;

      Status = BS->HandleProtocol(handle, &BlockIoProtocol, &pBlockIO);
      if (EFI_ERROR(Status))
        continue;

      /* Status = BS->HandleProtocol(handle, &ScsiPassThruProtocol, &pScsi);
      if (EFI_ERROR(Status))
        Print(L"CD-ROM does not support Scsi pass thru\n");
      else
        Print(L"CD-ROM support Scsi pass thru\n");*/

      Print(L"CD-ROM detected: %s\n", devPathStr);
      hCDROM = handle;
      pCDROMBlockIO = pBlockIO;
      // pCDROMScsi = pScsi;
    }
    else if (StrEndsWith(devPathStr, L"/Ata(Primary,Slave)")) {
      EFI_BLOCK_IO*          pBlockIO;

      Status = BS->HandleProtocol(handle, &BlockIoProtocol, &pBlockIO);
      if (EFI_ERROR(Status))
        continue;

      Print(L"Hard drive detected: %s\n", devPathStr);
      hd80 = pBlockIO;

      mbrUpdate(hd80);
    }
  }

  // Get floppy image
  do {
    UINTN size, readSize;
    EFI_FILE* file;

    file = fileOpen(NULL, L"floppy.img", 0);
    if (!file) {
      Print(L"FLOPPY: No floppy image (floppy.img) found\n");
      break;
    }

    floppyImage = AllocatePool(1474560);

    size = (UINTN)LibFileInfo(file)->FileSize;
    if (size != 1474560 && size != 1475584)
      Print(L"FLOPPY: Invalid floppy image size %d.\n", size);
    if (size > 1474560)
      size = 1474560;

    readSize = size;
    file->Read(file, &readSize, floppyImage);
    if (size != readSize)
      Print(L"FLOPPY: Floppy image read failed. Only %d bytes read.\n", readSize);

  } while (0);

  // Try to boot the CDROM first
  if (enableElTorito && bootElTorito(pCDROMBlockIO))
    return;

  // Now try to boot from the hard disk
  if (bootMBR(hd80))
    return;

  xomBootError(tgaWindows);
}

void out8(UINT16 port, UINT8 data) {
  __asm mov dx, port
  __asm mov al, data
  __asm out dx, al
}

void ioFeed(UINT16 addrPort, UINT16 dataPort, UINT8* food, UINTN size) {
  UINTN i;
  for (i = 0; i < size; ++i) {
    out8(addrPort, i);
    out8(dataPort, food[i]);
  }
}

#define SCR_ADDR (0xB8000)
#define SCR_PTR ((UINT16*)SCR_ADDR)
#define SCR_COLS 80
#define SCR_ROWS 25
UINT16 scrScreenBuffer[SCR_COLS * SCR_ROWS];
UINTN scrCol = 0, scrRow = 0;

UINT32 scrPalette[] = { 0x000000, 0xA8, 0x00A800, 0xA8A8, 0xA80000, 0xA800A8, 0xA8A800, 0xD0D0D0,
                        0xA8A8A8, 0xFC, 0x00FC00, 0xFCFC, 0xFC0000, 0xFC00FC, 0xFCFC00, 0xFCFCFC, };

// If turned on, B800 framebuffer is emulated
UINTN  scrEnable = 0;
extern UINT8 binary_font_bin_start[];

void scrDrawChar(UINTN col, UINTN row, UINT16 ch) {
  static int f = 0;

  // Draw character 'ch' at col,row
  {
    UINT32* dstPtr = (UINT32*)vidFrameBuffer + vidScanlineSize / 4 * 8 * row + 8 * col;
    UINT8* srcPtr = scrGlyphTable + 8 * 8 * (ch & 0xFF);
    UINT32 onPixel = scrPalette[(ch >> 8) & 0xF];
    UINT32 offPixel = scrPalette[(ch >> 12) & 0xF];
    UINTN r, c;
    for (r = 0; r < 8; ++r) {
      for (c = 0; c < 8; ++c)
        *dstPtr++ = *srcPtr++ ? onPixel : offPixel;
      dstPtr += vidScanlineSize / 4 - 8;
    }
  }
}

void scrUpdate() {
  static firstTime = 1;
  UINT16* ptr = SCR_PTR;
  UINT16* buf = scrScreenBuffer;
  UINTN r, c;

  if (!scrEnable)
    return;

  if (!enableConsole)
    return;

  for (r = 0; r < SCR_ROWS; ++r) {
    for (c = 0; c < SCR_COLS; ++c, ptr++, buf++) {
      if (firstTime || *ptr != *buf) {
        UINT16 ch = *buf = *ptr;
        scrDrawChar(c, r, ch);
      }
    }
  }
  firstTime = 0;
}

void scrEnter() {
  scrCol = 0;
  scrRow++;
  if (scrRow >= SCR_ROWS) {
    CopyMem(SCR_PTR, SCR_PTR + SCR_COLS * 2, (SCR_ROWS - 1) * SCR_COLS * 2);
    scrRow = 24;
  }
}

void scrWriteChar(UINTN ch) {
  poke16(SCR_ADDR + (scrRow * SCR_COLS + scrCol) * 2, ch);
  scrCol++;
  if (scrCol >= SCR_COLS)
    scrEnter();
}

void scrClear() {
  UINT16* ptr = SCR_PTR;
  UINTN i;
  for (i = 0; i < SCR_COLS * SCR_ROWS; ++i)
    *ptr++ = 0x720;
}

void scrInit() {
  UINTN ch, row, col;
  UINT8* srcPtr = binary_font_bin_start; // Found at 0xC70C3 on the M56 BIOS or at 0xFFA6E on AT BIOSes
  UINT8* dstPtr;

  // Do nothing if disabled
  if (!scrEnable)
    return;

  // Allocate a buffer for 256 characters
  scrGlyphTable = AllocatePool(8 * 8 * 256);
  if (!scrGlyphTable) {
    Print(L"Unable to allocate buffer for glyphs data\n");
    exit(EFI_SUCCESS);
  }

  // Initialize it with font data
  dstPtr = scrGlyphTable;
  for (ch = 0; ch < 256; ++ch) {
    for (row = 0; row < 8; ++row) {
      UINT8 b = *srcPtr++;
      for (col = 0; col < 8; ++col, b <<= 1)
        *dstPtr++ = b & 0x80 ? 0xFF : 0;
    }
  }

  // Initialize the screen with a test pattern
  {
    UINT16* ptr = SCR_PTR;
    UINT16 i = 0;
    for (row = 0; row < SCR_ROWS; ++row) {
      for (col = 0; col < SCR_COLS; ++col) {
        *ptr++ = i++;
      }
    }
    scrUpdate();
  }
}

void lcyLoadROM(UINT32 address, CHAR16* filename, UINTN expectedSize) {
  EFI_FILE* file;
  UINTN actualSize = expectedSize;

  file = fileOpen(NULL, filename, 0);
  if (!file) {
    Print(L"Unable to open BIOS file '%s'. Exiting...\n", filename);
    exit(EFI_SUCCESS);
  }

  if (LibFileInfo(file)->FileSize != expectedSize) {
    Print(L"BIOS file '%s' is not %04x bytes exactly. Exiting...\n", filename, expectedSize);
    exit(EFI_SUCCESS);
  }

  file->Read(file, &actualSize, (void*)address);

  if (actualSize != expectedSize) {
    Print(L"BIOS file '%s' read %04x, less than %04x. Exiting...\n", filename, actualSize, expectedSize);
    exit (EFI_SUCCESS);
  }

  file->Close(file);
}

UINT32 MAGICMAGIC = 0xEA31FCE2; // Guess where the numbers come from?

// Attempts to set mode 3. On the Ati M56, this does pretty much nothing,
// except enable the B800 framebuffer (voila!)
void lcyVgaMode3() {
  // These are the canonical values used to set video mode 3 (text mode, 80 cols)
  UINT8 cr[] = { 0x5f, 0x4f, 0x50, 0x82, 0x55, 0x81, 0xbf, 0x1f, 0, 0xc7, 6, 7, 0, 0, 0, 0, 0x9c, 0x8e, 0x8f, 0x28, 0x1f, 0x96, 0xb9, 0xa3, 0xff };
  UINT8 sr[] = { 0, 1, 3, 0, 2 };
  UINT8 gr[] = { 0, 0, 0, 0, 0, 0x10, 0xe, 0, 0xff };
  UINT8 ar[] = { 0, 1, 2, 3, 4, 5, 6, 7, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 8, 0, 0xf, 0, 0 };

  out8(0x3c2, 0x63); // enable frame buffer access
  ioFeed(0x3b4, 0x3b5, cr, sizeof(cr));
  ioFeed(0x3c4, 0x3c5, sr, sizeof(sr));
  ioFeed(0x3ce, 0x3cf, gr, sizeof(gr));
  ioFeed(0x3c0, 0x3c0, ar, sizeof(ar));

  // Make sure the frame buffer worked
  poke32(SCR_ADDR, MAGICMAGIC);
  if (peek32(SCR_ADDR) != MAGICMAGIC) {
    Print(L"Unable to enable frame buffer at 0x%05x. Disabling...\n", SCR_ADDR);
    scrEnable = 0;
  }
}

void lcyRomTable(UINT32 addr, EFI_GUID* guid, UINTN size, CHAR16* desc) {
  void* pTable;

  status = LibGetSystemConfigurationTable(guid, &pTable);
  if (EFI_ERROR(status))
    Print(L"Unable to locate %s table: %r\n", desc, status);
  else {
    Print(L"%s table at 0x%X\n", desc, pTable);
    CopyMem((void*)addr, pTable, size);
  }
}

void lcyInit() {
  // Video card initialization
  switch (vidVideoCard) {
    case AtiM56:
      // Enable legacy VGA IO space
      poke8(0xE0000052, 0); // disable IGD from reading VGA addresses
      poke8(0xE0000054, 0x83); // disable IGD altogether*/
      poke8(0xE000803E, 0xC);  // enable VGA 16-bit decode for PCIX devices (M56)

      // If only we knew how to truly enable text mode...
      // We are missing some ATI specific stuff here...
      // Let's use screen emulation instead
      scrEnable = 1;

      // Go to text mode
      lcyVgaMode3();
      break;

    case i945IGD:
      // Need research on how to enable text mode for this guy.
      // For now, we play safe: Do nothing!
      break;

    default:
      Print(L"Warning: Unknown video card %d...\n", vidVideoCard);
  }

  // Enable ROM area
  poke8(0xE0000090, 0x30); // enable RAM at F000:0000
  poke8(0xE0000091, 0x33); // enable RAM at C000:0000
  poke8(0xE0000092, 0x33); // enable RAM at C800:0000
  poke8(0xE0000095, 0x33); // Enable RAM at E000:0000

  // Load ROMs
  {
    static CHAR8 BiosDate[] = { 0xEA, 0xF0, 0xFF, 0x00, 0xF0, '0', '3', '/', '1', '4', '/', '0', '6', 0, 0xFC, 0 };

    if (enableROMs) {
     lcyLoadROM(0xC0000, L"vga.rom", 0xfe00);
     lcyLoadROM(0xf0000, L"bios.rom", 0x10000);
    }
    else {
      SetMem((void*)0xF0000, 0x10000, 0xEA);
      SetMem((void*)0xC0000, 0x10000, 0xEA);
      CopyMem((void*)0xFFFF0, BiosDate, 16);
    }

    // Create SMBIOS, ACPI tables
    lcyRomTable(0xF8000, &SMBIOSTableGuid, 0x20, L"SMBios");
    lcyRomTable(0xF8100, &Acpi20TableGuid, 0x24, L"ACPI 2.0");

    poke8(0xE0000090, 0x10); // lock down RAM at F000:0000, make it read-only
    poke8(0xE0000091, 0x11); // lock down RAM at C000:0000, make it read-only
    poke8(0xE0000092, 0x11); // lock down RAM at C800:0000, make it read-only
  }

  // Init Kernel
  {
    // Init vesa table
    krnVesaXResolution = vidResolutionX;
    krnVesaYResolution = vidResolutionY;
    krnVesaBitsPerPixel = vidBitsPerPixel;
    krnVesaScanline = vidScanlineSize;
    krnVesa3Scanline = vidScanlineSize;
    krnVesaLFBStart = vidFrameBuffer;
    krnVesaLFBEnd = vidFrameBuffer + vidScanlineSize * vidResolutionY;
    krnVesaLFBUKB = (vidVRAMSize - vidScanlineSize * vidResolutionY) / 1024;

    krnInit();

    poke8(0xE0000095, 0x31); // E000:0000 ROM, E400:0000 RAM
  }

  // Initialize screen subsystem
  scrInit();

  // Generate memory map
  {
    UINTN NoEntries, MapKey, DescriptorSize, DescriptorVersion;
    EFI_MEMORY_DESCRIPTOR*    map;
    UINT32                    i = 0, j = 0;

    map = LibMemoryMap(&NoEntries, &MapKey, &DescriptorSize, &DescriptorVersion);

    for (i = 0, j = 0; i < NoEntries; ++i) {
      EFI_MEMORY_DESCRIPTOR* ptr = (EFI_MEMORY_DESCRIPTOR*) (((UINT32)map) + i * DescriptorSize);
      EFI_PHYSICAL_ADDRESS address = ptr->PhysicalStart;
      UINT64 length = ptr->NumberOfPages;
      EFI_MEMORY_TYPE type = ptr->Type;
      BiosMemoryMapEntry* dstPtr = (BiosMemoryMapEntry*)(BiosMapStart + 20 * j);

      if (address == 0x90000)
        length -= 1;
      else if (address == 0x9f000) {
        address -= 0x1000;
        length += 1;
      }

      dstPtr->baseAddress = address;
      dstPtr->length = length << 12;
      dstPtr->type = type == EfiConventionalMemory ? 1 :
                     /*type == EfiACPIReclaimMemory ? 3 :
                     type == EfiACPIMemoryNVS ? 4 : */2;
      // Print(L"%d: %X %08lx %08lx %d\n", j, dstPtr, dstPtr->baseAddress, dstPtr->length, dstPtr->type);
      j++;
    }
    BiosMapEnd = BiosMapStart + 20 * j;

    FreePool(map);
  }
}

void unhandledInterrupt(Registers* regs);
void int3(Registers* regs);
void int10(Registers* regs);
void int13(Registers* regs);
void int14(Registers* regs);
void int15(Registers* regs);
void int16(Registers* regs);
void int17(Registers* regs);
void int1A(Registers* regs);

int freeze = 0;
int heartbeat = 0;
int heartbeatState = 0;
int displayInterrupts = 0;

void pmInterruptHandler(Registers* regs);

void interruptHandler() {
  pmInterruptHandler(CONTEXT);
}

void pmInterruptHandler(Registers* regs) {
  UINTN acc = 0;

  if (regs->interrupt == 0x30) {
    __asm int 0x68;
    if (heartbeat++ < 5000)
      return;
    heartbeat = 0;
    poke8(SCR_ADDR, heartbeatState);
    heartbeatState = 1 - heartbeatState;
    scrUpdate();

    if (!kbdGet())
      return;
  }
  if (regs->interrupt == 0xEE) {
    __asm int 0x68;
  }
  displayInterrupts = 0;
  acc = regs->eax & 0xFFFF;

  scrUpdate();
  if (freeze) {
    __asm hlt
  }
  /*unhandledInterrupt(regs);*/
  switch (regs->interrupt) {
    case 0x3: // single step
      int3(regs);
      break;
    case 0x10: // Video
      int10(regs);
      break;
    case 0x11: // BIOS Equipment list
      regs->eax = 0x63; // One floppy installed, x87 installed, 80x25 color
      break;
    case 0x12: // Get memory size
      regs->eax = 639; // Save 1kb from top
      break;
    case 0x13:
      int13(regs);
      break;
    case 0x14:
      int14(regs);
      break;
    case 0x15:
      int15(regs);
      break;
    case 0x16:
      int16(regs);
      break;
    case 0x17:
      int17(regs);
      break;
    case 0x1a:
      int1A(regs);
      break;
    case 0x37: {
      Print(L"int 37 triggered at %s. Ignoring...\n", addrToString(CSIP));
      break;
    }
    default:
      unhandledInterrupt(regs);
  }

  if (displayInterrupts) {
    Print(L"%s: int %02x - %04x called.\n", addrToString(CSIP), regs->interrupt, acc);
  }
}

void unhandledInterrupt(Registers* regs) {
  debug(regs);
}

void int3(Registers* regs) {
  debug(regs);
}

void int10_00(Registers* regs) {
  scrClear();
  scrUpdate();
}

void int10_02(Registers* regs) {
  scrRow = RH(regs->edx);
  scrCol = RL(regs->edx);
  if (scrRow < 0 || scrRow >= SCR_ROWS)
    scrRow = 24;
  if (scrCol < 0 || scrCol >= SCR_COLS)
    scrCol = 79;
}

void int10_0A(Registers* regs) {
  UINTN ch = RL(regs->eax);
  UINTN pg = RH(regs->ebx);
  UINTN attr = RL(regs->ebx);
  UINTN cnt = RW(regs->ecx);

  while (cnt--) {
    scrWriteChar(ch | (attr << 8));
  }
  scrUpdate();
}

void int10_0E(UINTN ch) {
  if (ch == 13)
    scrEnter();
  else if (ch >= 32)
    scrWriteChar(ch | 0x0700);
  scrUpdate();
}

void int10_11(Registers* regs) {
  switch (regs->eax & 0xFF) {
    case 0x30: { // Get Font information
      UINTN font = RH(regs->ebx);
      regs->es = 0xF000;
      WW(regs->ebp, 0xFA6E);
      WW(regs->ecx, font == 2 ? 14 : font == 6 ? 16 : 8);
      WL(regs->edx, 25);
      break;
    }
    default:
      unhandledInterrupt(regs);
  }
}

void int10(Registers* regs) {
  switch ((regs->eax >> 8) & 0xff) {
    case 0: // Set video mode: TODO
      int10_00(regs);
      break;
    case 2: // Set cursor position
      int10_02(regs);
      break;
    case 0xe: // Teletype output
      int10_0E(regs->eax & 0xFF);
      break;
    case 0x12: // Get blanking attribute
      break;
    case 0x0A: // Write char: TODO
      int10_0A(regs);
      break;
    case 0x11:
      int10_11(regs);
      break;
    case 0x20: // Unknown
      break;
    default:
      unhandledInterrupt(regs);
  }
}

void int14(Registers* regs) {
  switch ((regs->eax >> 8) & 0xff) {
    case 0x0:  // Initialize port
      WW(regs->eax, 0x6000);
      break;
    default:
      unhandledInterrupt(regs);
  }
}

void int13_2(Registers* regs) {
  UINTN sectorCount = RL(regs->eax);
  UINTN track = RH(regs->ecx);
  UINTN sector = RL(regs->ecx);
  UINTN head = RH(regs->edx);
  UINTN drive = RL(regs->edx);
  UINT32 ptr = SO(regs->es, regs->ebx);
  UINT32 headsPerTrack = 0;
  UINT32 sectorsPerTrack = 0;
  UINT32 bytesPerSector = 0;
  INT32 lba = -1;

  track = track | ((sector & 0xc0) << 2);
  sector = sector & 0x3f;

  if (drive == 0) {
    if (!floppyImage) {
      STC;
      return;
    }
    headsPerTrack = 2;
    sectorsPerTrack = 18;
    bytesPerSector = 512;
  }
  else if (drive == 0x80) {
    headsPerTrack = 255;
    sectorsPerTrack = 63;
    bytesPerSector = 512;
  }

  lba = (track * headsPerTrack + head) * sectorsPerTrack + sector - 1;

  // Print(L"%s: Read DCHS(%d:%d:%d:%d)L%d @%X, LBA %X\n",
  //   addrToString(CSIP), drive, track, head, sector, sectorCount, ptr, lba);

  if (!bytesPerSector) {
    unhandledInterrupt(regs);
    STC;
    return;
  }

  if (drive == 0) {
    CopyMem((void*)ptr, floppyImage + lba * bytesPerSector, sectorCount * bytesPerSector);
  }
  else if (drive == 0x80) {
    hd80->ReadBlocks(hd80, hd80->Media->MediaId, lba, sectorCount * bytesPerSector, (void*)ptr);
  }

  CLC;
  displayInterrupts = 0;
}

void int13_3(Registers* regs) {
  UINTN sectorCount = RL(regs->eax);
  UINTN track = RH(regs->ecx);
  UINTN sector = RL(regs->ecx);
  UINTN head = RH(regs->edx);
  UINTN drive = RL(regs->edx);
  UINT32 ptr = SO(regs->es, regs->ebx);
  UINT32 headsPerTrack = 255;
  UINT32 sectorsPerTrack = 63;
  UINT32 bytesPerSector = 512;
  INT32 lba = -1;

  track = track | ((sector & 0xc0) << 2);
  sector = sector & 0x3f;

  if (drive != 0x80) {
    STC;
    return;
  }

  lba = (track * headsPerTrack + head) * sectorsPerTrack + sector - 1;

  Print(L"%s: Write DCHS(%d:%d:%d:%d)L%d @%X, LBA %X\n",
    addrToString(CSIP), drive, track, head, sector, sectorCount, ptr, lba);

  hd80->WriteBlocks(hd80, hd80->Media->MediaId, lba, sectorCount * bytesPerSector, (void*)ptr);
  CLC;
}

void int13_42(Registers* regs) {
  UINT32 drive = RL(regs->edx);
  UINT32 pDAP = SO(regs->ds, regs->esi);
  UINT32 sectorCount = peek16(pDAP + 0x2);
  UINT32 ptr = SO(peek16(pDAP + 0x6), peek16(pDAP + 0x4));
  EFI_LBA lba = peek64(pDAP + 0x8);
  UINT64 flatPtr = peek64(pDAP + 0x10);
  UINT32 DAPSize = peek8(pDAP);
  EFI_BLOCK_IO* pBlockIO = drive == 0x80 ? hd80 :
                           drive == 0x82 ? hd82 : NULL;

  STC;
  if (pBlockIO == NULL) { // HD2
    Print(L"int13_42(dl=%x): Unhandled drive\n", drive);
    unhandledInterrupt(regs);
    return;
  }

  if (DAPSize != 0x10) { // HD0
    Print(L"int13_42(pDAP=%X, pDAP->Size=%d): Unhandled DAP Size\n", pDAP, DAPSize);
    unhandledInterrupt(regs);
    return;
  }

  /*Print(L"%s: %s read LBA %ldL%d @%X\n",
      addrToString(CSIP), drive == 0x80 ? L"HD" : L"CDROM", lba, sectorCount, ptr);*/
  pBlockIO->ReadBlocks(pBlockIO, pBlockIO->Media->MediaId, lba, sectorCount * pBlockIO->Media->BlockSize, (void*)ptr);
  CLC;
  displayInterrupts = 0;
}

void int13_43(Registers* regs) {
  UINT32 drive = RL(regs->edx);
  UINT32 pDAP = SO(regs->ds, regs->esi);
  UINT32 sectorCount = peek16(pDAP + 0x2);
  UINT32 ptr = SO(peek16(pDAP + 0x6), peek16(pDAP + 0x4));
  EFI_LBA lba = peek64(pDAP + 0x8);
  UINT64 flatPtr = peek64(pDAP + 0x10);
  UINT32 DAPSize = peek8(pDAP);
  EFI_BLOCK_IO* pBlockIO = drive == 0x80 ? hd80 :
                           drive == 0x82 ? hd82 : NULL;

  STC;
  if (pBlockIO == NULL) { // HD2
    Print(L"int13_42(dl=%x): Unhandled drive\n", drive);
    unhandledInterrupt(regs);
    return;
  }

  if (DAPSize != 0x10) { // HD0
    Print(L"int13_43(pDAP=%X, pDAP->Size=%d): Unhandled DAP Size\n", pDAP, DAPSize);
    unhandledInterrupt(regs);
    return;
  }

  Print(L"%s: %s extended write LBA %ldL%d @%X\n",
      addrToString(CSIP), drive == 0x80 ? L"HD" : L"CDROM", lba, sectorCount, ptr);
  pBlockIO->WriteBlocks(pBlockIO, pBlockIO->Media->MediaId, lba, sectorCount * pBlockIO->Media->BlockSize, (void*)ptr);
  CLC;
}

void int13_4B(Registers* regs) {
  switch (regs->eax & 0xff) {
    case 1:  // Bootable CD-ROM: Get Status
      CLC;   // TODO: fill buffer. WinXP ignores the buffer, so we only return success for now
      Print(L"%s: int 13_4B\n", addrToString(CSIP));
      break;
    default:
      unhandledInterrupt(regs);
  }
}

void int13_8(Registers* regs) {
  UINTN drive = regs->edx & 0xff;

  // The iMac has a CDROM as primary and the HD as secondary.
  // The BIOS would recognize the HD as drive 0x80 while the CD would go unnoticed.
  switch (drive) {
    case 0x00: // Floppy
      WL(regs->ebx, 4);
      WW(regs->ecx, 0x4F12);
      WW(regs->edx, 0x0101);
      CLC;
      break;
    case 0x80: // HD
      WW(regs->eax, 0);
      WW(regs->ecx, 0xffff); // the legacy int13-8 does not support large drives, just max out the result
      WW(regs->edx, 0xfe01); // as returned by test subject
      CLC;
      break;
    case 0x82: // CDROM Emulation
      unhandledInterrupt(regs);
    default:
      STC;
      break;
  }
}

void int13_15(Registers* regs) {
  UINTN drive = regs->edx & 0xff;

  switch (drive) {
    case 0x00: // Floppy
      WH(regs->eax, 2);
      CLC;
      break;
    case 0x80: // HD
      WH(regs->eax, 3); // As returned by test subject
      WW(regs->ecx, 0x1315);
      WW(regs->edx, 0x7400);
      CLC;
      break;
    case 0x82: // CDROM Emulation
      unhandledInterrupt(regs);
    default:
      WH(regs->eax, 1);
      STC;
      break;
  }
}

void int13_41(Registers* regs) {
  UINTN drive = regs->edx & 0xff;

  switch (drive) {
    case 0x80: // HD
      WW(regs->eax, 0x2100); // As returned by test subject
      WW(regs->ebx, 0xaa55);
      WW(regs->ecx, 5);
      CLC;
      break;
    case 0x00: // Floppy
    case 0x82: // CDROM Emulation
      unhandledInterrupt(regs);
    default:
      STC;
      break;
  }
}

UINT32 getDayMillis() {
  EFI_TIME time;
  EFI_STATUS Status;

  Status = RT->GetTime(&time, NULL);
  if (EFI_ERROR(Status))
    return 0;
  return time.Hour * 3600000 + time.Minute * 60000 + time.Second * 1000 + time.Nanosecond / 1000000;
}

UINTN pwdState = 0;
UINT32 pwdStartTime = 0;

typedef struct _patch_t {
  INT16 signature[32];
  INT16 patch[16];
  UINTN  patchOffset;
  UINTN  address;
  UINTN  address2;
} patch_t;

patch_t PatchTable[] = {
  { { 8, 0, 0, 0, 0x16, 0, 0x14, 0, 1, 0, 0, 1, 0x16, 0, 0x1e, 0, -1 },
    { 0, -1 }, 0 },
  { { 0x83, 0xce, 0xc, 0x89, 0x70, 0x38, 0x47, 0x83, 0xc1, 0x3c, -1 },
    { 0x8, -1 }, 2 },
};

#define PATCHCOUNT  (sizeof(PatchTable) / sizeof(patch_t))

UINTN pwdPatch() {
  UINTN i, j, error = 0;
  UINT32 addr;

  // Reset all patches
  for (i = 0; i < PATCHCOUNT; ++i);
    PatchTable[i].address = PatchTable[i].address2 = 0;

  // Search all patches
  for (addr = 0; addr < 0x2000000; ++addr) {
    for (i = 0; i < PATCHCOUNT; ++i) {
      for (j = 0; PatchTable[i].signature[j] == peek8(addr + j); ++j);
      if (PatchTable[i].signature[j] != -1)
        continue;
      if (!PatchTable[i].address)
        PatchTable[i].address = addr;
      else
        PatchTable[i].address2 = addr;
    }
  }

  // Determine patch status
  for (i = 0; i < PATCHCOUNT; ++i) {
    if (PatchTable[i].address == 0) {
      Print(L"Patch #%d: Not found.\n", i);
      error = 1;
    }
    else {
      Print(L"Patch #%d: Applied at %X\n", i, PatchTable[i].address);
      for (j = 0; PatchTable[i].patch[j] != -1; ++j)
        poke8(PatchTable[i].address + PatchTable[i].patchOffset + j, (UINT8)PatchTable[i].patch[j]);
      if (PatchTable[i].address2) {
        Print(L"Patch #%d: Found more than once at %X and %X\n", i, PatchTable[i].address, PatchTable[i].address2);
        Print(L"Patch #%d: Applied at %X\n", i, PatchTable[i].address2);
        for (j = 0; PatchTable[i].patch[j] != -1; ++j)
          poke8(PatchTable[i].address2 + PatchTable[i].patchOffset + j, (UINT8)PatchTable[i].patch[j]);
      }
    }
  }
  if (error)
    Print(L"Patch errors detected!!\n");
  else
    Print(L"Patching succeeded.\n");
  return !error;
}

void pwdNotify() {
  if (pwdState == 0) {
    pwdStartTime = getDayMillis();
    pwdState = 1;
  }
  if (pwdState == 1) {
    UINT32 delta = getDayMillis() - pwdStartTime;
    if (delta < 0)
      delta += 86400000;
    // Print(L"Watchdog: S1 trigger %d\n", delta);
    if (delta > 120000) {  // 2 minutes
      pwdState = 2;
    }
  }
  if (pwdState == 2) {
    // Print(L"Watchdog: Fired!\n");
    if (!pwdPatch())
      unhandledInterrupt(CONTEXT);
    pwdState = 3;
  }
}

void int13_48(Registers* regs) {
  UINTN drive = regs->edx & 0xff;
  Address buffer = addrRealFromSegOfs(regs->ds, (UINT16)regs->esi);

  switch (drive) {
    case 0x80: { // HD
      UINT16 params[] = { 0x1e, 0, 0x3fff, 0, 16, 0, 63, 0, 0x7400, 0x1315, 0, 0, 0x200, 0, 0 };

      WH(regs->eax, 0); // As returned by test subject
      CopyMem(addrToPointer(buffer), params, sizeof(params));
      CLC;
      pwdNotify();
      Print(L"%s: int 13_48\n", addrToString(CSIP)); // not fully implemented (ptr to bios disk cfg is not present) We'll keep this bios call on check to make sure callers don't use the result
      break;
    }
    case 0x00: // Floppy
    case 0x82: // CDROM Emulation
      unhandledInterrupt(regs);
    default:
      STC;
      break;
  }
}

// 1e 00 00 00 ff 3f 00 00 10 00 00 00 3f 00 00 00
// f0 6a 31 01 00 00 00 00 00 02 a0 9f 00 f0

void int13(Registers* regs) {
  switch ((regs->eax >> 8) & 0xff) {
    case 0:  // Reset Disk System
      CLC;
      break;
    case 2:  // Read sectors into memory
      int13_2(regs);
      break;
    case 3: // Write sectors
      int13_3(regs);
      break;
    case 8:  // Get drive parameters
      int13_8(regs);
      break;
    case 0x15: // Get disk type
      int13_15(regs);
      break;
    case 0x41: // Extensions Installation Check
      int13_41(regs);
      break;
    case 0x42: // TODO: Extended read
      int13_42(regs);
      break;
    case 0x43: // Extended write
      int13_43(regs);
      break;
    case 0x48: // Get drive parameters
      int13_48(regs);
      break;
    case 0x4B: // bootable CDROM disk emulation
      int13_4B(regs);
      break;
    default:
      unhandledInterrupt(regs);
  }
}

void int15_e820(Registers* regs) {
  UINT32 ptr = SO(regs->es, regs->edi);

  if (regs->ebx == 0)
    regs->ebx = BiosMapStart;
  regs->eax = 0x534d4150;
  CopyMem((void*)ptr, (void*)regs->ebx, 20);
  regs->ecx = 20;
  regs->ebx += 20;
  if (regs->ebx >= BiosMapEnd) {
    regs->ebx = 0;
    // poke8(0x20228, 0xcc);
    // poke8(0x20285, 0xf4);
    // poke8(Adjust16BitAddress(EA(rmInterruptHandler)) | 0x90000, 0xf4);
    // freeze = 1;
    // unhandledInterrupt(regs);
  }
  CLC;
}

void int15(Registers* regs) {
  if ((regs->eax & 0xffff) == 0xe820) {
    int15_e820(regs);
    return;
  }
  else if ((regs->eax & 0xffff) == 0xe980) {
    // SMBios stuff... TODO
    return;
  }
  /*else if ((regs->eax & 0xffff) == 0xe801) {
    CLC;
    regs->eax = 0x3c00;
    regs->ebx = 0x1b2c;
    regs->ecx = 0x3c00;
    regs->edx = 0x1b2c;
    return;
  }*/

  switch ((regs->eax >> 8) & 0xff) {
    /*case 0x88: { // Get extended memory size
      CLC;
      regs->eax = 0x3c00;
    }*/
    case 0xc0: { // Get Configuration
      Address addr = krnAddress(&SysConf);
      regs->es = addr.segment;
      regs->ebx = addr.offset;
      WH(regs->eax, 0);
      CLC;
      break;
    }
    case 0x53: // APM. TODO: not supported
      STC;
      break;
    default:
      unhandledInterrupt(regs);
  }
}

UINTN kbdHasPendingKeystroke = 0;
EFI_INPUT_KEY kbdPendingKeystroke;

UINT8 scanCodeTable[] = {
  0x00, 0x48, 0x50, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F,
  0x40, 0x41, 0x42, 0x43, 0x44, 0x00, 0x00, 0x01,
};

UINT8 kbdGetScanCode(EFI_INPUT_KEY key) {
  UINT8 scanCode = (UINT8)key.ScanCode;
  if (scanCode == 0x17)
    unhandledInterrupt(CONTEXT);
  if (key.UnicodeChar)
    return scanCode;
  if (scanCode < sizeof(scanCodeTable))
    return scanCodeTable[scanCode];
  return scanCode;
}

void int16_0(Registers* regs) {
  xomEnableConsole();
  while (!kbdHasPendingKeystroke && pKeyboard->ReadKeyStroke(pKeyboard, &kbdPendingKeystroke) != EFI_SUCCESS);
  if (!kbdHasPendingKeystroke)
    kbdPendingKeystroke.ScanCode = kbdGetScanCode(kbdPendingKeystroke);
  else
    kbdHasPendingKeystroke = 0;
  WH(regs->eax, kbdPendingKeystroke.ScanCode);
  WL(regs->eax, kbdPendingKeystroke.UnicodeChar);
  // Print(L"%s: int 16_00 returning keystroke %02x %02x\n", addrToString(CSIP), kbdPendingKeystroke.ScanCode, kbdPendingKeystroke.UnicodeChar);
}

UINT32 keyCounter = 0;

void int16_1(Registers* regs) {
  keyCounter++;
  if (keyCounter > 128)
    xomEnableConsole();
  if (kbdHasPendingKeystroke || pKeyboard->ReadKeyStroke(pKeyboard, &kbdPendingKeystroke) == EFI_SUCCESS) {
    kbdHasPendingKeystroke = 1;
    kbdPendingKeystroke.ScanCode = kbdGetScanCode(kbdPendingKeystroke);
    WH(regs->eax, kbdPendingKeystroke.ScanCode);
    WL(regs->eax, kbdPendingKeystroke.UnicodeChar);
    xomEnableConsole();
    CLZ;
  }
  else
    STZ;
  displayInterrupts = 0;
}

void int16_5(Registers* regs) {
  if (!kbdHasPendingKeystroke) {
    kbdHasPendingKeystroke = 1;
    kbdPendingKeystroke.ScanCode = RH(regs->ecx);
    kbdPendingKeystroke.UnicodeChar = RL(regs->ecx);
    WL(regs->eax, 0);
  }
  else {
    WL(regs->eax, 1);
  }
}

void int16(Registers* regs) {
  switch ((regs->eax >> 8) & 0xff) {
    case 0: // Get keystroke
      int16_0(regs);
      break;
    case 0x1:  // Check for keystroke
      int16_1(regs);
      break;
    case 0x2: // Get shift flags: TODO
      WL(regs->eax, 0);
      break;
    case 0x5: // Store keystroke
      int16_5(regs);
      break;
    default:
      unhandledInterrupt(regs);
  }
}

void int17(Registers* regs) {
  switch ((regs->eax >> 8) & 0xff) {
    case 0x1:  // Initialize printer port
      WH(regs->eax, 0x90);
      break;
    default:
      unhandledInterrupt(regs);
  }
}

void int1A_B1(Registers* regs) {
  switch (regs->eax & 0xFF) {
    case 0x01: // PCI BIOS v2.0c+ Installation CHeck
      regs->eax = 1; // PCI BIOS installed, conf  mechanism #1
      regs->ebx = 0x210; // PCI BIOS v2.10
      regs->ecx = 4; // Up to bus #4 for the iiMac
      regs->edx = 0x20494350; // 'PCI'
      regs->edi = 0; // PM entry, NOT IMPLEMENTED
      CLC;
      break;
    case 0x0E: // Get IRQ routing: TODO
      break;
    default:
      unhandledInterrupt(regs);
  }
}

void int1A(Registers* regs) {
  switch ((regs->eax >> 8) & 0xff) {
    case 0: { // Get SysTicks
      UINT32 milis = getDayMillis();
      UINT32 ticks = milis * 7 / 1000 * 2809 / 1080;

      WW(regs->ecx, ticks >> 16);
      WW(regs->edx, ticks & 0xffff);
      WL(regs->eax, 0); // TODO: midnight flag
      displayInterrupts = 0;
      break;
    }
    case 0x2: { // Get RTC
      EFI_TIME time;
      EFI_STATUS Status;

      Status = RT->GetTime(&time, NULL);
      if (EFI_ERROR(Status)) {
        STC;
        break;
      }
      WH(regs->ecx, BCD(time.Hour));
      WL(regs->ecx, BCD(time.Minute));
      WH(regs->edx, BCD(time.Second));
      WL(regs->edx, time.Daylight & EFI_TIME_IN_DAYLIGHT ? 1 : 0);
      CLC;
      break;
    }
    case 0x4: { // Get RTC
      EFI_TIME time;
      EFI_STATUS Status;

      Status = RT->GetTime(&time, NULL);
      if (EFI_ERROR(Status)) {
        STC;
        break;
      }
      WH(regs->ecx, BCD((time.Year - 2001) / 100 + 21));
      WL(regs->ecx, BCD(time.Year));
      WH(regs->edx, BCD(time.Month));
      WL(regs->edx, BCD(time.Day));
      CLC;
      break;
    }
    case 0xB1:
      int1A_B1(regs);
      break;
    default:
      unhandledInterrupt(regs);
  }
}

//  __asm mov ds:[0x900], eax
//  __asm mov ds:[0x904], ebx
//  __asm mov ds:[0x908], ecx
//  __asm mov ds:[0x90c], edx
//  __asm mov ds:[0x910], esp
//  __asm mov ds:[0x914], ebp
//  __asm mov ds:[0x918], esi
//  __asm mov ds:[0x91c], edi
//  __asm mov ds:[0x920], cs
//  __asm mov ds:[0x924], ds
//  __asm mov ds:[0x928], es
//  __asm mov ds:[0x92c], fs
//  __asm mov ds:[0x930], gs
//  __asm mov ds:[0x934], ss
