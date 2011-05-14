#include "txt.h"

void eatWhitespace(CHAR16** ptr) {
  while (**ptr == ' ')
    (*ptr)++;
}

CHAR16* txtGetString(CHAR16** ptr, CHAR16* delims, UINTN* pDelimiter) {
  CHAR16* retPtr;
  eatWhitespace(ptr);
  retPtr = *ptr;
  while (1) {
    CHAR16 c = **ptr;
    UINTN i = 0;

    if (c && c != ' ' && delims)
      for (i = 0; delims[i] && c != delims[i]; ++i);

    if (!c || c == ' ' || delims && delims[i] == c) {
      if (pDelimiter)
        *pDelimiter = retPtr == *ptr ? 0 : c ? c : ' ';
      if (c)
        *(*ptr)++ = 0;
      return retPtr;
    }
    (*ptr)++;
  }
}

UINT32 txtGetHex(CHAR16** ptr, UINTN* pDelimiter) {
  CHAR16* str = txtGetString(ptr, 0, pDelimiter);
  return xtoi(str);
}