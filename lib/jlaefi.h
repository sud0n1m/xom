#ifndef __JLAEFI_H
#define __JLAEFI_H

typedef struct {
#pragma pack(push, 1)
  UINT8  identSize;
  UINT8  colorMapType;
  UINT8  imageType;
  UINT16 colorMapStart;
  UINT16 colorMapLength;
  UINT8  colorMapBits;
  UINT16 xStart;
  UINT16 yStart;
  UINT16 width;
  UINT16 height;
  UINT8  bits;
  UINT8  descriptor;
  EFI_UGA_PIXEL data[];
#pragma pack(pop)
} TGA;

typedef enum {
  tgaVerticalMask = 3,
  tgaTop = 0,
  tgaBottom = 1,
  tgaCenteredVertically = 2,
  tgaHorizontalMask = 12,
  tgaLeft = 0,
  tgaRight = 4,
  tgaCenteredHorizontally = 8,
  tgaCentered = tgaCenteredVertically | tgaCenteredHorizontally,
} TGA_ALIGNMENT;

void InitJLAEFI(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE* SystemTable);
void exit(EFI_STATUS status);
CHAR16 inputChar(CHAR16* prompt, CHAR16 dflt);
UINT32 inputHex(CHAR16* prompt, UINT32 dflt);
UINT32 inputDec(CHAR16* prompt, UINT32 dflt);
void saveMemory(EFI_FILE* rootDir, UINT32 address, UINT32 length, UINTN copyFirst);
EFI_FILE* getFileSystem();
EFI_HANDLE getHandleById(UINTN id);
UINTN getHandleId(EFI_HANDLE handle);
VOID* getProtocolByHandleId(UINTN id, EFI_GUID* protocol);
CHAR16* getDeviceName(EFI_HANDLE handle);
void dumpHandles(VOID* protocol);
UINT32 getDefaultHexArg(CHAR16* id, UINTN arg, UINT32 dflt);
void dumpHex(VOID* buffer, UINTN bufferOffset, UINTN size, UINTN displayOffset);
EFI_FILE* fileOpen(EFI_HANDLE device, CHAR16* filename, UINTN writeable);
CHAR16 kbdGet();
CHAR16 kbdGetKey();
void consoleControl(UINTN graphicsMode);
EFI_STATUS exec(EFI_HANDLE device, CHAR16* filename);
void* decompress(void* ptr, UINT32 size);
void tgaDraw(TGA* tga, UINTN x, UINTN y, TGA_ALIGNMENT alignment);
CHAR16* StrStr(CHAR16* str, CHAR16* str2);
int StrEndsWith(CHAR16* str, CHAR16* str2);

extern UINTN argc;
extern CHAR16** argv;
extern EFI_STATUS status;
extern EFI_UGA_DRAW_PROTOCOL* pUgaDraw;

#endif // __JLAEFI_H