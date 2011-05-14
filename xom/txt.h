#ifndef __TXT_H
#define __TXT_H

#include "efi.h"
#include "efilib.h"

void eatWhitespace(CHAR16** ptr);
CHAR16* txtGetString(CHAR16** ptr, CHAR16* delims, UINTN* pDelimiter);
UINT32 txtGetHex(CHAR16** ptr, UINTN* pDelimiter);

#endif // __TXT_H