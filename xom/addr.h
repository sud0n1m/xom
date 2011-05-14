#ifndef __ADDR_H
#define __ADDR_H

#include "efi.h"
#include "efilib.h"

typedef struct Address_t {
  UINT32 offset;
  UINT16 segment;
  UINT8  type;
} Address;

#define ADDRT_REAL   0   // Segment:Offset (16:16)
#define ADDRT_FLAT   1   // Segment is 0, Offset is 32 bit flat address

Address addrNull();
UINTN   addrIsNull(Address address);
Address addrRealFromPointer(void* ptr, UINT16 segment); // create from pointer, setting segment
Address addrFlatFromPointer(void* ptr); // create from pointer
Address addrRealFromSegOfs(UINT16 segment, UINT16 offset);
Address addrRealFromOffset(UINT32 address, UINT16 segment);
Address addrFlatFromOffset(UINT32 address);
Address addrToReal(Address address, UINT16 segment);
Address addrToFlat(Address address);
Address addrAdd(Address address, INT32 amount);
UINT32  addrDiff(Address a1, Address a2);
CHAR16* addrToString(Address address);
void*   addrToPointer(Address address);
UINT32  addrToOffset(Address address);
UINTN   addrIsPagingEnabled();
void    addrEnablePaging(UINTN f);

static UINTN   addrLT (Address a1, Address a2) { return addrToOffset(a1) <  addrToOffset(a2); }
static UINTN   addrLTE(Address a1, Address a2) { return addrToOffset(a1) <= addrToOffset(a2); }
static UINTN   addrGT (Address a1, Address a2) { return addrToOffset(a1) >  addrToOffset(a2); }
static UINTN   addrGTE(Address a1, Address a2) { return addrToOffset(a1) >= addrToOffset(a2); }
static UINTN   addrEQ (Address a1, Address a2) { return addrToOffset(a1) == addrToOffset(a2); }

#define ADDR_FLAT_MAX      addrFlatFromOffset(0xFFFFFFFF)
#define ADDR_FLAT_MIN      addrFlatFromOffset(0x00000000)
#define ADDR_REAL_MAX      addrRealFromSegOfs(0xFFFF, 0xFFFF)
#define ADDR_REAL_MIN      addrRealFromSegOfs(0x0000, 0x0000)
#define ADDR_NULL          addrNull()

#endif // __ADDR_H