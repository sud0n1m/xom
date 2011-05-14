/*++

Copyright (c) 2006 JLA

Module Name:

    jlaefi.c

Abstract:

    EFI helper routines

Revision History

--*/

#include "efi.h"
#include "efilib.h"
#include "../lib/jlaefi.h"

EFI_HANDLE imageHandle;
EFI_LOADED_IMAGE* pLoadedImage;
struct _EFI_CONSOLE_CONTROL_PROTOCOL* pConsoleControl;
SIMPLE_INPUT_INTERFACE* pKeyboard;
EFI_UGA_DRAW_PROTOCOL* pUgaDraw = 0;
EFI_DECOMPRESS_PROTOCOL* pDecompress;

UINTN argc;
CHAR16** argv;
EFI_STATUS status;

#define SHELL_INTERFACE_PROTOCOL \
  { 0x47c7b223, 0xc42a, 0x11d2, 0x8e, 0x57, 0x0, 0xa0, 0xc9, 0x69, 0x72, 0x3b }

EFI_GUID ShellInterfaceProtocol = SHELL_INTERFACE_PROTOCOL;

typedef struct _EFI_SHELL_INTERFACE {
  // Handle back to original image handle & image info
  EFI_HANDLE                  ImageHandle;
  EFI_LOADED_IMAGE            *Info;

  // Parsed arg list
  CHAR16                      **Argv;
  UINTN                       Argc;

  // Storage for file redirection args after parsing
  CHAR16                      **RedirArgv;
  UINTN                       RedirArgc;

  // A file style handle for console io
  EFI_FILE_HANDLE             StdIn;
  EFI_FILE_HANDLE             StdOut;
  EFI_FILE_HANDLE             StdErr;

} EFI_SHELL_INTERFACE;

EFI_GUID ConsoleControlProtocol = { 0xF42F7782, 0x012E, 0x4C12, 0x99, 0x56, 0x49, 0xf9, 0x43, 0x04, 0xf7, 0x21 };

typedef enum {
  EfiConsoleControlScreenText,
  EfiConsoleControlScreenGraphics,
  EfiConsoleControlScreenMax,
} EFI_CONSOLE_CONTROL_SCREEN_MODE;

typedef struct _EFI_CONSOLE_CONTROL_PROTOCOL {
  void (*GetMode)(struct _EFI_CONSOLE_CONTROL_PROTOCOL* pThis, EFI_CONSOLE_CONTROL_SCREEN_MODE* currentMode, UINT32 n0, UINT32 n1);
  void (*SetMode)(struct _EFI_CONSOLE_CONTROL_PROTOCOL* pThis, EFI_CONSOLE_CONTROL_SCREEN_MODE  currentMode);
} EFI_CONSOLE_CONTROL_PROTOCOL;

void InitJLAEFI(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE* SystemTable) {
  //
  // Initialize the Library.
  //
  InitializeLib (ImageHandle, SystemTable);
  pKeyboard = SystemTable->ConIn;
  imageHandle = ImageHandle;

  // Get loaded image interface to implement exit() function
  status = BS->HandleProtocol(imageHandle, &LoadedImageProtocol, &pLoadedImage);
  if (EFI_ERROR(status)) {
    Print(L"Unable to obtain LoadedImageProtocol interface: %r\n", status);
    exit(EFI_SUCCESS);
  }

  // Get console control interface to set text mode
  status = LibLocateProtocol(&ConsoleControlProtocol, &pConsoleControl);
  if (EFI_ERROR(status)) {
    Print(L"Unable to obtain ConsoleControl interface: %r\n", status);
    exit(EFI_SUCCESS);
  }

  // Get arg list
  {
    EFI_SHELL_INTERFACE* pShell;
    UINT32 handleCount, i;
    EFI_HANDLE* handles;

    LibLocateHandle(ByProtocol, &ShellInterfaceProtocol, NULL, &handleCount, &handles);
    for (i = 0; i < handleCount; ++i) {
      BS->HandleProtocol(handles[i], &ShellInterfaceProtocol, &pShell);
      if (pShell->Argc)
        break;
    }

    argc = pShell->Argc;
    argv = pShell->Argv;
  }

  // Locate UGA draw
  // My iMac 17" reports that two handles implement UgaDraw
  // But only one actually draws on the screen!!
  // Both report the same resolution, both implement TextOut.
  // And worse... Both TextOut->OutputString print to the screen!! (???)
  // So... in order to draw to the screen, which one should I choose?
  // What has worked so far is using the second one returned (HACK!)
  {
    UINTN NoHandles;
    UINTN Index;
    EFI_HANDLE* Handles;

    // Get list of UgaDraw implementors
    LibLocateHandle(ByProtocol, &UgaDrawProtocol, NULL, &NoHandles, &Handles);

    // Enumerate all but keep only the last one (HACK!)
    for (Index = 0; Index < NoHandles; Index++)
      BS->HandleProtocol(Handles[Index], &UgaDrawProtocol, &pUgaDraw);

    // Sanity
    if (!pUgaDraw)
      Print(L"jlaefi: Unable to find UgaDraw interface\n");
  }

  // Locate decompress algorithm
  status = LibLocateProtocol(&DecompressProtocol, &pDecompress);
  if (EFI_ERROR(status)) {
    Print(L"Unable to obtain Decompress interface: %r\n", status);
    exit(EFI_SUCCESS);
  }
}

void consoleControl(UINTN graphicsMode) {
  if (pConsoleControl)
    pConsoleControl->SetMode(pConsoleControl, graphicsMode ? EfiConsoleControlScreenGraphics : EfiConsoleControlScreenText);
}

EFI_STATUS exec(EFI_HANDLE device, CHAR16* filename) {
  EFI_DEVICE_PATH* filePath;
  EFI_HANDLE handle;

  if (device == NULL)
    device = pLoadedImage ? pLoadedImage->DeviceHandle : NULL;

  if (device == NULL) {
    Print(L"exec(%s): Invalid device\n", filename);
    return EFI_INVALID_PARAMETER;
  }

  filePath = FileDevicePath(device, filename);
  if (!filePath) {
    Print(L"exec(%s): Unable to create device path\n", filename);
    return EFI_OUT_OF_RESOURCES;
  }

  status = BS->LoadImage(FALSE, imageHandle, filePath, NULL, 0, &handle);
  if (EFI_ERROR(status)) {
    Print(L"exec(%s): Unable to load image: %r\n", filename, status);
    return EFI_NOT_FOUND;
  }

  status = BS->StartImage(handle, NULL, NULL);
  if (EFI_ERROR(status)) {
    Print(L"exec(%s): Unable to start image: %r\n", filename, status);
    return EFI_NOT_STARTED;
  }
  return EFI_SUCCESS;
}

void exit(EFI_STATUS status) {
  consoleControl(0);
  BS->Exit(imageHandle, status, 0, NULL);
}

CHAR16 inputChar(CHAR16* prompt, CHAR16 dflt) {
  CHAR16 buffer[16];

  Print(L"%s [%c] : ", prompt, dflt);
  Input(NULL, buffer, sizeof(buffer));
  Print(L"\n");
  if (buffer[0] == 'q')
    exit(EFI_SUCCESS);
  if (!buffer[0])
    return dflt;
  return buffer[0];
}

UINT32 inputHex(CHAR16* prompt, UINT32 dflt) {
  CHAR16 buffer[16];

  Print(L"%s [%04x] : ", prompt, dflt);
  Input(NULL, buffer, sizeof(buffer));
  Print(L"\n");
  if (buffer[0] == 'q')
    exit(EFI_SUCCESS);
  if (!buffer[0])
    return dflt;
  return xtoi(buffer);
}

UINT32 inputDec(CHAR16* prompt, UINT32 dflt) {
  CHAR16 buffer[16];

  Print(L"%s [%4d] : ", prompt, dflt);
  Input(NULL, buffer, sizeof(buffer));
  Print(L"\n");
  if (buffer[0] == 'q')
    exit(EFI_SUCCESS);
  if (!buffer[0])
    return dflt;
  return Atoi(buffer);
}

void saveMemory(EFI_FILE* rootDir, UINT32 address, UINT32 length, UINTN copyFirst) {

#define COPYBUFFERSIZE 0x100000

  EFI_STATUS status;
  EFI_FILE* datFile;
  CHAR16 filename[16];
  UINT32 sz = length;
  UINT8* copyBuffer;

  SPrint(filename, sizeof(filename), L"%05x.dat", address);
  status = rootDir->Open(rootDir, &datFile, filename, EFI_FILE_MODE_READ, 0);
  if (!EFI_ERROR(status)) {
    datFile->Delete(datFile);
    datFile->Close(datFile);
  }

  status = rootDir->Open(rootDir, &datFile, filename, EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, 0);
  if (EFI_ERROR(status)) {
    Print(L"Open(%s): %r\n", filename, status);
    exit(status);
  }

  if (copyFirst) {
    copyBuffer = AllocatePool(COPYBUFFERSIZE);
    if (!copyBuffer) {
      Print(L"Unable to allocate %x byte copy buffer\n", COPYBUFFERSIZE);
      exit(EFI_OUT_OF_RESOURCES);
    }
  }

  while (length) {
    UINT8* srcPtr;
    UINT32 sz;

    if (copyFirst) {
      sz = length > COPYBUFFERSIZE ? COPYBUFFERSIZE : length;
      __asm mov edi, copyBuffer
      __asm mov esi, address
      __asm mov ecx, sz
      __asm shr ecx, 2
      __asm rep movsd
      address += sz;
      srcPtr = copyBuffer;
      length -= sz;
    }
    else {
      sz = length;
      srcPtr = (UINT8*)address;
      length = 0;
    }
    Print(L"%X %X %X %X\n", srcPtr, sz, address, length);

    status = datFile->Write(datFile, &sz, srcPtr);
    if (EFI_ERROR(status)) {
      Print(L"Write(%s): %r\n", filename, status);
      exit(status);
    }
  }

  if (copyFirst) {
    FreePool(copyBuffer);
  }

  Print(L"%s, %d bytes\n", filename, length);

  datFile->Close(datFile);

}

EFI_FILE* fileOpen(EFI_HANDLE device, CHAR16* filename, UINTN writeable) {
  EFI_FILE* root;
  EFI_FILE* file;

  if (device == NULL)
    device = pLoadedImage ? pLoadedImage->DeviceHandle : NULL;

  if (device == NULL) {
    Print(L"Invalid device\n");
    return NULL;
  }

  root = LibOpenRoot(device);
  if (root == NULL)
    return root;

  status = root->Open(root, &file, filename, EFI_FILE_MODE_READ, 0);
  if (writeable) {
    if (!EFI_ERROR(status)) {
      file->Delete(file);
      file->Close(file);
    }

    status = root->Open(root, &file, filename, EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, 0);
  }

  if (EFI_ERROR(status))
    Print(L"Open(%s): %r\n", filename, status);
  root->Close(root);

  return file;
}

EFI_FILE* getFileSystem() {
  UINT32 i, j;
  UINTN handleCount;
  EFI_HANDLE* handles;
  EFI_HANDLE fs[64];
  EFI_FILE* fileSystem;

  // Display all potential storage devices
  LibLocateHandle(ByProtocol, &FileSystemProtocol, NULL, &handleCount, &handles);

  for (i = 0, j = 0; i < handleCount && j < sizeof(fs) / sizeof(EFI_HANDLE); ++i) {
    EFI_HANDLE handle = handles[i];
    EFI_DEVICE_PATH* dpath = DevicePathFromHandle(handle);

    if (!dpath)
      continue;

    Print(L"%x. %s\n", j + 1, DevicePathToStr(dpath));
    fs[j++] = handle;
  }
  if (!j) {
    Print(L"No filesystems available\n");
    exit(EFI_UNSUPPORTED);
  }

  // Get the user to choose the filesystem
  do {
    UINT32 i = inputHex(L"Please select the target filesystem ", 1);
  } while (i < 1 || i >= j + 1);

  // Open file system
  fileSystem = LibOpenRoot(fs[i - 1]);
  if (!fileSystem) {
    Print(L"Unable to open volume\n");
    exit(EFI_DEVICE_ERROR);
  }

  return fileSystem;
}

EFI_HANDLE* handleList;
UINTN maxHandles;

void initHandleList() {
  if (!handleList) {
    LibLocateHandle(AllHandles, NULL, NULL, &maxHandles, &handleList);
  }
}

EFI_HANDLE getHandleById(UINTN id) {
  initHandleList();

  if (id <= 0 || id > maxHandles)
    exit(EFI_INVALID_PARAMETER);

  return handleList[id - 1];
}

VOID* getProtocolByHandleId(UINTN id, EFI_GUID* protocol) {
  VOID* if0;
  EFI_HANDLE handle = getHandleById(id);

  status = BS->HandleProtocol(handle, protocol, &if0);
  if (EFI_ERROR(status))
    exit(status);
  return if0;
}

UINTN getHandleId(EFI_HANDLE handle) {
  UINTN i;

  initHandleList();

  for (i = 0; i < maxHandles; ++i) {
    if (handleList[i] == handle)
      return i + 1;
  }
  exit(EFI_INVALID_PARAMETER);
  return 0;
}

UINT32 getDefaultHexArg(CHAR16* id, UINTN arg, UINT32 dflt) {
  if (arg >= argc) {
    // TODO: Get variable named id and return it if present
    return dflt;
  }

  return xtoi(argv[arg]);
}

CHAR16* getDeviceName(EFI_HANDLE handle) {
  EFI_COMPONENT_NAME_PROTOCOL* pComponentName = NULL;
  CHAR16* pName = NULL;
  EFI_DEVICE_PATH* path;

  BS->HandleProtocol(handle, &ComponentNameProtocol, &pComponentName);
  if (pComponentName)
    pComponentName->GetControllerName(pComponentName, handle, NULL, "eng", &pName);
  if (pName)
    return pName;

  path = DevicePathFromHandle(handle);
  return path ? DevicePathToStr(path) : L"Unknown";
}

void dumpHandles(VOID* protocol) {
  UINTN i, handleCount;
  EFI_HANDLE* handles;

  LibLocateHandle(ByProtocol, protocol, NULL, &handleCount, &handles);
  for (i = 0; i < handleCount; ++i) {
    Print(L"%02x: %s\n", getHandleId(handles[i]), getDeviceName(handles[i]));
  }
}

void dumpHex(VOID* buffer, UINTN bufferOffset, UINTN size, UINTN displayOffset) {
  UINTN lines = (size + 15) / 16;
  UINT8* ptr = ((UINT8*)buffer) + bufferOffset;
  while (lines--) {
    UINTN i;

    Print(L"%X: ", displayOffset);
    for (i = 0; i < 15; ++i) {
      if (i < size)
        Print(L"%02x", ptr[i]);
      else
        Print(L"  ");
      Print(L"%c", i == 7 ? '-' : ' ');
    }

    Print(L" *");
    for (i = 0; i < 15 && i < size; ++i) {
      CHAR16 c = ptr[i];
      Print(L"%c", c < 0x20 || c > 0x7f ? '.' : c);
    }
    Print(L"*\n");

    displayOffset += 16;
    ptr += 16;
    size -= 16;
  }
}

CHAR16 kbdGet() {
  EFI_INPUT_KEY key;
  EFI_STATUS status;

  status = pKeyboard->ReadKeyStroke(pKeyboard, &key);
  if (status != EFI_SUCCESS)
    return 0;
  return key.UnicodeChar ? key.UnicodeChar : key.ScanCode | 0x8000;
}

CHAR16 kbdGetKey() {
  CHAR16 k;
  while (!(k = kbdGet()));
  return k;
}

void* decompress(void* ptr, UINT32 size) {
  UINT32 destSize, scratchSize;
  VOID* destBuffer;
  VOID* scratchBuffer;

  // Get buffer sizes
  status = pDecompress->GetInfo(pDecompress, ptr, size, &destSize, &scratchSize);
  if (EFI_ERROR(status)) {
    Print(L"decompress: Error getting decompress info for %X: %r\n", ptr, status);
    return 0;
  }

  // Allocate buffers
  destBuffer    = AllocatePool(destSize);
  if (!destBuffer) {
    Print(L"decompress: Unable to allocate %,d bytes for target decompress buffer\n", destSize);
    return 0;
  }

  scratchBuffer = AllocatePool(scratchSize);
  if (!scratchBuffer) {
    Print(L"decompress: Unable to allocate %,d bytes for scratch decompress buffer\n", scratchBuffer);
    FreePool(destBuffer);
    return 0;
  }

  // Decompress
  status = pDecompress->Decompress(pDecompress, ptr, size, destBuffer, destSize, scratchBuffer, scratchSize);
  if (EFI_ERROR(status)) {
    Print(L"decompress: Error decompressing file at %X: %r\n", ptr, status);
    FreePool(scratchBuffer);
    FreePool(destBuffer);
    return 0;
  }

  // Release scratch buffer
  FreePool(scratchBuffer);
  return destBuffer;
}

void tgaDraw(TGA* tga, UINTN x, UINTN y, TGA_ALIGNMENT alignment) {
  UINTN horizAlign = alignment & tgaHorizontalMask;
  UINTN vertAlign = alignment & tgaVerticalMask;

  // See if this is a valid TGA object
  if (tga->colorMapType != 0 || tga->imageType != 2 || tga->bits != 32 || tga->identSize != 0) {
    Print(L"Invalid TGA %X\n", tga);
    return;
  }
  if ((tga->descriptor & 0x20) == 0) {
    static EFI_UGA_PIXEL buffer[2048];
    UINTN half = tga->height / 2;
    UINTN slSize = tga->width * sizeof(EFI_UGA_PIXEL);
    UINTN y;

    for (y = 0; y < half; ++y) {
      void* sl1 = tga->data + y * tga->width;
      void* sl2 = tga->data + (tga->height - y - 1) * tga->width;
      CopyMem(buffer, sl1, slSize);
      CopyMem(sl1   , sl2, slSize);
      CopyMem(sl2, buffer, slSize);
    }
    tga->descriptor |= 0x20;
  }
  x = horizAlign == tgaLeft ? x :
      horizAlign == tgaRight ? x - tga->width : x - tga->width / 2;
  y = vertAlign  == tgaTop ? y :
      vertAlign  == tgaBottom ? y - tga->height : y - tga->height / 2;

  if (pUgaDraw)
    pUgaDraw->Blt(pUgaDraw, tga->data, EfiUgaBltBufferToVideo, 0, 0, x, y, tga->width, tga->height, 0);
}

CHAR16* StrStr(CHAR16* str, CHAR16* str2) {
  UINTN len, len2, i;

  len = StrLen(str);
  len2 = StrLen(str2);
  for (i = 0; i + len2 <= len; ++i) {
    if (!StrnCmp(str + i, str2, len2))
      return str + i;
  }
  return 0;
}

int StrEndsWith(CHAR16* str, CHAR16* str2) {
  UINTN len, len2;

  len = StrLen(str);
  len2 = StrLen(str2);
  if (len2 > len)
    return 0;

  return !StrCmp(str + len - len2, str2);
}
