#include "addr.h"
#include "txt.h"

Address addrRealFromPointer(void* ptr, UINT16 segment) {
  return addrToReal(addrFlatFromPointer(ptr), segment);
}

Address addrFlatFromPointer(void* ptr) {
  return addrFlatFromOffset((UINT32)ptr);
}

Address addrRealFromSegOfs(UINT16 segment, UINT16 offset) {
  Address address;
  address.segment = segment;
  address.offset = offset;
  address.type = ADDRT_REAL;
  return address;
}

Address addrRealFromOffset(UINT32 offset, UINT16 segment) {
  return addrToReal(addrFlatFromOffset(offset), segment);
}

Address addrFlatFromOffset(UINT32 offset) {
  Address address;
  address.segment = 0;
  address.offset = offset;
  address.type = ADDRT_FLAT;
  return address;
}

Address addrToReal(Address address, UINT16 segment) {
  if (address.type == ADDRT_REAL) {
    if (address.segment == segment)
      return address;

    address.offset += address.segment << 4;
  }
  address.type = ADDRT_REAL;
  address.segment = segment;
  address.offset -= segment << 4;
  if (address.offset > 0x10000)
    address.offset = address.segment = 0;
  return address;
}

Address addrToFlat(Address address) {
  if (address.type == ADDRT_FLAT) {
    return address;
  }

  address.type = ADDRT_FLAT;
  address.offset += address.segment << 4;
  address.segment = 0;
  return address;
}

Address addrNull() {
  Address address;
  address.type = 0xff;
  return address;
}

UINTN addrIsNull(Address address) {
  return address.type == 0xff;
}

Address addrCreate(UINT32 offset, UINT16 segment, UINT8 type) {
  if (type == ADDRT_FLAT)
    return addrFlatFromOffset(offset);
  if (type == ADDRT_REAL)
    return addrRealFromOffset(offset, segment);
  return addrNull();
}

UINT32  addrToOffsetInternal(Address address) {
  if (address.type == ADDRT_REAL)
    return (address.segment << 4) + address.offset;
  if (address.type == ADDRT_FLAT)
    return address.offset;
  return 0;
}

Address addrAdd(Address address, INT32 amount) {
  UINT32 t = addrToOffsetInternal(address) + amount;
  return addrCreate(t, address.segment, address.type);
}

UINT32 addrDiff(Address a1, Address a2) {
  return addrToOffsetInternal(a1) - addrToOffsetInternal(a2);
}

CHAR16* addrToString(Address address) {
  static CHAR16 buffer[32];
  if (address.type == ADDRT_REAL)
    SPrint(buffer, 32, L"%04x:%04x", address.segment, address.offset);
  else if (address.type == ADDRT_FLAT)
    SPrint(buffer, 32, L"%08x", address.offset);
  else
    SPrint(buffer, 32, L"( NULL )");
  return buffer;
}

void*   addrToPointer(Address address) {
  return (UINT8*)addrToOffset(address);
}

UINT32  addrToOffset(Address address) {
  if (address.type == ADDRT_REAL)
    return (address.segment << 4) + address.offset;
  if (address.type == ADDRT_FLAT)
    return addrIsPagingEnabled() ? address.offset & 0x7FFFFFFF : address.offset;
  return 0;
}

UINTN addrPagingEnable;

UINTN addrIsPagingEnabled() {
  return addrPagingEnable;
}

void addrEnablePaging(UINTN f) {
  addrPagingEnable = f;
}