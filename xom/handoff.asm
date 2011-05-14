        .486P

WinCodeSelector32   EQU       08h
WinDataSelector32   EQU       10h
HostCodeSelector    EQU       10h
HostDataSelector    EQU       18h
Host64kCodeSelector EQU       30h
Host64kDataSelector EQU       38h
HostTimerInterrupt  EQU       30h
HostTimerScaler     EQU       0DBh
KernelSegment       EQU       0E000h
Kernel32Ofs         EQU       00000h
Kernel32Size        EQU       02000h
Kernel16Ofs         EQU       Kernel32Ofs + Kernel32Size
Kernel16Size        EQU       02000h
RMFileOfs           EQU       Kernel16Ofs + Kernel16Size
KernelBase          EQU       KernelSegment SHL 4
Kernel32Base        EQU       KernelBase + Kernel32Ofs
Kernel16Base        EQU       KernelBase + Kernel16Ofs
RMFileBase          EQU       KernelBase + RMFileOfs

ArgCount            EQU      8

EXTERN _interruptHandler: DWORD

PUBLIC C krnClientJmp
PUBLIC C krnClientCall
PUBLIC C krnAbort
PUBLIC C krnIDT
PUBLIC C krnVesaXResolution
PUBLIC C krnVesaYResolution
PUBLIC C krnVesaBitsPerPixel
PUBLIC C krnVesaScanline
PUBLIC C krnVesa3Scanline
PUBLIC C krnVesaLFBStart
PUBLIC C krnVesaLFBEnd
PUBLIC C krnVesaLFBUKB

; Data tables
public c SysConf

RegFile STRUCT
    FOR c, <C, H>
      FOR   r, <eax, ecx, edx, ebx, esp, ebp, esi, edi, cs, ds, es, fs, gs, ss, eip, eflags, cr0, cr3>
        c&&r DWORD ?
      ENDM
      c&gdt FWORD ?
      c&idt FWORD ?
    ENDM
    interrupt DWORD ?
    counter   BYTE ?
RegFile ENDS

_TEXT SEGMENT PARA PUBLIC USE32 '_TEXT'
_TEXT ENDS
_TEXT32 SEGMENT PARA PUBLIC USE32 '_TEXT'
_TEXT32 ENDS
_TEXT16 SEGMENT PARA PUBLIC USE16 '_TEXT'
_TEXT16 ENDS

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;
; Macros to access the context file

; Used by real mode code to access the context file.
; Assumes CS holds the kernel segment and uses it to have r/w access to it.
RealModeContext MACRO
    Regs      TEXTEQU   <(RegFile PTR cs:[RMFileOfs])>
ENDM

; Used by protected mode code to access the context file.
; Assumes DS holds a flat address segment
; Protected mode can't use a code selector to write, therefore, DS must be used.
FlatPtrContext MACRO
    Regs      TEXTEQU   <(RegFile PTR ds:[RMFileBase])>
ENDM

; Used by protected mode code to access the context file.
; Assumes DS holds a 64k segment pointing at the Kernel
; Protected mode can't use a code selector to write, therefore, DS must be used.
NearPtrContext MACRO
    Regs      TEXTEQU   <(RegFile PTR ds:[RMFileOfs])>
ENDM

; Saves all registers, es, fs, gs, ss, gdt, idt, cr0 and cr3.
; NOTE: ds, cs, eip, eflags are not saved
; Also hardware state is restored
SaveContext MACRO ch:REQ
    FOR     r, <eax, ecx, edx, ebx, esp, ebp, esi, edi, es, fs, gs, ss>
      mov   Regs.&ch&&r, &r
    ENDM

    IF @WordSize EQ 2
      db    066h
      sgdt  Regs.&ch&gdt
      db    066h
      sidt  Regs.&ch&idt
    ELSEIF @WordSize EQ 4
      sgdt  Regs.&ch&gdt
      sidt  Regs.&ch&idt
    ELSE
      .ERR Unknown WordSize value (@WordSize)
    ENDIF

    mov     eax, cr0
    mov     Regs.ch&cr0, eax
    mov     eax, cr3
    mov     Regs.ch&cr3, eax

    IF @InStr(, ch, <H>)
      OUTB  20h, 11h
      OUTB  21h, 30h
      OUTB  21h, 04h
      OUTB  21h, 01h
    ELSE
      OUTB  20h, 11h
      OUTB  21h, 68h
      OUTB  21h, 04h
      OUTB  21h, 01h
    ENDIF
ENDM

OUTB MACRO port:REQ, val:REQ
    mov   al, val
    out   port, al
    jmp   $+2
ENDM

Debug MACRO color:REQ
    LOCAL loop1
    mov eax, color
    mov ecx,000010000h
loop1:
    mov gs:[080010000h+ecx*4], eax
    dec ecx
    jnz loop1
    Debug2
ENDM

Debug2 MACRO
    LOCAL loop1
    mov ecx, 100000h
loop1:
    in al, 0f0h
    dec ecx
    jnz loop1
ENDM

; Restores all general purpose registers and es, fs, gs, ss, gdt, idt, cr0 and cr3.
; NOTE: ds, cs, eip, eflags are not restored
RestoreContext MACRO ch:REQ
    RestoreControlRegisters &ch
    jmp     @F
@@: RestoreRegisters &ch
ENDM

; Restores all control registers: GDT, IDT, CR0 and CR3
RestoreControlRegisters MACRO ch:REQ
    IF @WordSize EQ 2
      db    066h
      lgdt  Regs.&ch&gdt
      db    066h
      lidt  Regs.&ch&idt
    ELSEIF @WordSize EQ 4
      lgdt  Regs.&ch&gdt
      lidt  Regs.ch&idt
    ENDIF
    mov     eax, Regs.ch&cr3
    mov     cr3, eax
    mov     eax, Regs.ch&cr0
    mov     cr0, eax
ENDM

; Restores all general purpose registers and es, fs, gs and ss.
; NOTE: ds and cs are not restored.
RestoreRegisters MACRO ch:REQ
    FOR     r, <eax, ecx, edx, ebx, esp, ebp, esi, edi, es, fs, gs, ss>
      mov   &r, Regs.&ch&&r
    ENDM
ENDM

_TEXT SEGMENT

FlatPtrContext

krnInitAsm proc c
    push    esi
    push    edi
    push    ebx

    ; Initialize real mode interrupt table
    mov     edi, 0
    mov     eax, (OFFSET rmInt0K16 - OFFSET _TEXT16 + Kernel16Ofs) or (KernelSegment SHL 16)
    mov     ecx, 100h
@@: stosd
    add     eax, 4
    loop    @B
    mov     eax, (OFFSET rmInt1CHandlerK16 - OFFSET _TEXT16 + Kernel16Ofs) or (KernelSegment SHL 16)
    mov     ds:[1Ch * 4], eax

    ; Copy bios data area
    mov     esi, OFFSET BDA
    mov     edi, 400h
    mov     ecx, 40h
    rep movsd

    ; Copy K32
    mov     esi, OFFSET _TEXT32
    mov     edi, Kernel32Base
    mov     ecx, Kernel32Size
    rep movsb

    ; Copy K16
    mov     esi, OFFSET _TEXT16
    mov     edi, Kernel16Base
    mov     ecx, Kernel16Size
    rep movsb

    ; Set diskette params interrupt pointer
    mov     eax, (OFFSET DisketteParams - OFFSET _TEXT16 + Kernel16Ofs) or (KernelSegment SHL 16)
    mov     ds:[01eh * 4], eax

    ; Init client file
    mov     edi, RMFileBase
    mov     ecx, SIZEOF RegFile
    xor     eax, eax
    rep stosb
    mov     Regs.Cesp, 7C00h
    mov     eax, cr0
    and     al, not 1
    mov     Regs.Ccr0, eax
    mov     eax, cr3
    mov     Regs.Ccr3, eax
    sgdt    Regs.Cgdt
    mov     WORD PTR Regs.Cidt, 03FFh
    pushfd
    pop     Regs.Ceflags

    pop     ebx
    pop     edi
    pop     esi
    ret
krnInitAsm endp

krnAdjustAddress proc c, p:ptr
    mov     eax, p
    sub     eax, OFFSET _TEXT16
    add     eax, Kernel16Base
    ret
krnAdjustAddress endp

krnClientCall:
    ; For debugging purposes
    mov   eax, HostDataSelector
    mov   gs, eax

    ASSUME ds:FLAT, es:FLAT, fs:FLAT, gs:FLAT, ss:FLAT
    ; Invoked by the host to switch to the client
    ; We know that DS has a flat data selector and we'll use it to
    ; access the context file
    FlatPtrContext

    ; Now, we create a fake callee stack (fake far return address,
    ; fake arguments). The return address is hardcoded to rmReturnK16
    ; while the arguments are copied from the caller's stack.
    mov   edi, Regs.Cesp
    sub   edi, 4 * ArgCount + 4
    mov   Regs.Cesp, edi          ; Make room in calle's stack for fake stack
    mov   eax, (OFFSET rmReturnK16 - OFFSET _TEXT16 + Kernel16Ofs) or (KernelSegment SHL 16)
    stosd                         ; Store the fake far return address
    lea   esi, [esp + 4]
    mov   ecx, ArgCount
    rep movsd                     ; Copy arguments from caller's stack

    ; Now we proceed to a normal krnClientJmp
    jmp   krnClientJmp

krnClientJmp:
    ASSUME ds:FLAT, es:FLAT, fs:FLAT, gs:FLAT, ss:FLAT
    ; Invoked by the host to jump to the client
    ; Just as with krnClientCall, DS holds a flat data selector, which
    ; we'll use to access the context file.
    FlatPtrContext

    ; 1. Disable interrupts.
    cli

    ; Save host registers: eflags, cs, ds. eip is not saved
    pushfd
    pop   Regs.hEFLAGS
    mov   Regs.hCS, cs
    mov   Regs.hDS, ds

    ; Now save all other registers.
    SaveContext <H>

    ; Hardware switch

    ; Determine if the cliaent switch is to PM or RM
    test  Regs.cCR0, 1
    jz    rmJmp

pmJmp:
    ; The client switch is to PM.
    ; Jump to the kernel in flat mode
    db    0EAh
    dd    OFFSET pmJmpK32 - OFFSET _TEXT32 + Kernel32Base
    dw    HostCodeSelector

rmJmp:
    ; The client switch is to Real Mode
    ; Keep going down the path to Real Mode...

    ; 2. If paging is enabled it must be disabled, but the host doesn't
    ;    use paging.

    ; 3. Transfer program control to a readable segment that
    ;    has a limit of 64KBytes (FFFFH). This operation loads
    ;    the CS register with the segment limit required in
    ;    real-address mode.
    ;    Note: The documentation doesn't mention that the code
    ;          selector must also be a 16-bit selector. But not
    ;          doing so crashes the jump to real mode.
    ;          see http://www.sudleyplace.com/pmtorm.html
    db    0EAh
    dd    OFFSET rmJmpK16 - OFFSET _TEXT16 + Kernel16Ofs
    dw    Host64kCodeSelector

krnAbort:
    ; Restore registers
    RestoreRegisters <H>

    ; Resume host operation
    ret

    ALIGN     16
krnIDT:
    i = 0
    handlerAddress = OFFSET pmInt0K32 - OFFSET _TEXT32 + Kernel32Base
    WHILE i LT 256
      DWORD (handlerAddress and 00000FFFFh) or (WinCodeSelector32 SHL 16)
      DWORD (handlerAddress and 0FFFF0000h) or (8E00h)
      handlerAddress = handlerAddress + 8
      i = i + 1
    ENDM

BDA:
    dw      8 dup(0)
    dw      000000000000011b
    db      0
    dw      027fh ; ram 639k, save 1k for top
    dw      0
    db      0    ; kbd shift flags 1
    db      0    ; kbd shift flags 2
    db      0
    dw      1eh  ; kbd buffer ptr1
    dw      1eh  ; kbd buffer ptr2
    db      32 dup(0)  ; kbd buffer
    db      0    ; floppy calibration
    db      0    ; floppy motor status
    db      0    ; floppy motor timeout
    db      0    ; floppy status
    db      20h  ; floppy controller status register 0
    db      0    ; floppy controller status register 1
    db      0    ; floppy controller status register 2
    db      0    ; floppy cylinder
    db      0    ; floppy head
    db      0    ; floppy sector
    db      0    ; floppy bytes written
    db      3    ; active video mode
    dw      80   ; video columns
    dw      1000h; size of video page in bytes
    dw      0    ; video page offset
    dw      8 dup(0) ; cursor positions
    dw      0    ; cursor shape
    db      0    ; active video page
    dw      3d4h ; i/o port address for video display adapter
    db      155 dup(0)

_TEXT ENDS

_TEXT32 SEGMENT

pmJmpK32:
    ASSUME ds:NOTHING, ss:ERROR, es:ERROR, fs:ERROR, gs:ERROR
    ; Client switch to protected mode.
    ; We do this from the comfort of the kernel to avoid being paged out
    ; CS holds a host flat code selector, DS holds a host flat code selector
    ; We'll use DS to access the context file
    FlatPtrContext

    ; Restore most registers (Paging may get enabled here)
    RestoreContext <C>

    ; Restore the missing registers: ds, cs, eip and eflags
    ; Once that's done, the client will take over
    push  Regs.cEFLAGS
    push  Regs.cCS
    push  Regs.cEIP
    mov   ds, Regs.cDS
    iretd

pmIntK32:
    ASSUME ds:ERROR, ss:ERROR, es:ERROR, fs:ERROR, gs:ERROR
    ; This is invoked whenever an interrupt occurs in the client's PM
    ; This means that CS holds a flat 32bit code selector
    ; We will use a flat DS 32bit data selector, but first, we save DS
    push  ds
    push  WinDataSelector32
    pop   ds
    ASSUME ds:NOTHING

    ; Now that we have a valid DS, we can save the client context
    ; First we setup the context address to use a flat pointer
    FlatPtrContext
    ; Then we save DS, which was temporarily saved on the stack
    pop   Regs.cDS

    ; Now we figure the interrupt number
    pop   Regs.interrupt
    sub   Regs.interrupt, OFFSET pmInt0K32 - OFFSET _TEXT32 + Kernel32Base + 5
    shr   Regs.interrupt, 3

    ; Then save the stuff that was pushed by the interrupt call
    pop   Regs.cEIP
    pop   Regs.cCS
    pop   Regs.cEFLAGS

    ; Finally save all the remaining registers.  The few things that
    ; SaveContext does not save (cs, ds, eip, eflags) are saved already.
    SaveContext <C>

    ; Now, mode switch. We are already in protected mode, but we
    ; need to restore the host context.

    ; 1. Disable interrupts (CLI/NMI) - Not really necessary, they already are
    ; cli

    ; *. If paging is enabled:

    ; - Transfer program control to linear addresses that are
    ;   identity mapped to physical addresses.
    ; DONE

    ; - Insure that the GDT and IDT are in identity mapped pages
    ; NOT DONE

    ; - Clear the PG bit in the CR0 register
    mov   eax, cr0
    and   eax, 7FFFFFFFh
    mov   cr0, eax

    ; - Move 0h into CR3 to flush the TLB
    xor   eax, eax
    mov   cr3, eax

    ; Restore host context
    ; This will not restore cs, ds, eip and eflags
    RestoreContext <H>

    ; We'll use an IRETD instruction to restore cs, eip and eflags.
    ; Push them into the new (host) stack, but first, push a return address.
    push  krnClientJmp
    push  Regs.hEFLAGS               ; eflags
    push  Regs.hCS                   ; cs
    push  OFFSET _interruptHandler   ; eip

    ; Finally restore the host DS.
    mov   ds, Regs.hDS
    ASSUME DS:ERROR

    ; After IRETD, eflags, cs and eip are restored and the host takes over.
    iretd


    ; Interrupt handlers.
    ; 256 call instructions that dispatch to the same place (pmIntK32)
    ; The callee determines the interrupt number by using the return
    ; address pushed by the call instruction. This return address is
    ; then discarded.

    ; It's important that the label to the first call instruction is aligned.
    ALIGN     4
pmInt0K32:
    REPEAT    256
      ALIGN   4
      call    pmIntK32
    ENDM

_TEXT32 ENDS

    ;;;;;;;;;;;;;;;;;;;;;;;;
    ;
    ; Real mode kernel.

_TEXT16 SEGMENT

    ; Luckily, in real mode, unlike in protected mode, the code segment
    ; is writeable. Therefore, no need to botch the DS just to access
    ; the context file.
    RealModeContext
    ; Therefore, DS, ES, SS, FS and GS are off limits
    ASSUME ds:ERROR, ss:ERROR, es:ERROR, fs:ERROR, gs:NOTHING

rmJmpK16:
    ; We are in the middle of jumping into real mode.
    ; CS now holds a 64kb 16bit code selector, but we're still in
    ; protected mode. Therefore, we cannot use CS to access the context
    ; file.

    ; 4. Load segment registers SS, DS, ES, FS and GS with a
    ;    selector for a descriptor containing the following values,
    ;    which are appropriate for real-address mode:
    ;    64 KBytes limit, byte granular, expand up, writable, present.
    mov   eax, Host64kDataSelector
    mov   ss, eax
    mov   ds, eax
    mov   es, eax
    mov   fs, eax
    ; mov   gs, eax
    ASSUME ds:NOTHING

    ; At this point, DS has a 64kB selector pointing to the kernel.
    ; We now switch to near pointer access to the context file
    NearPtrContext

    ; 5. Execute an LIDT instruction to point to a real-address
    ;    mode interrupt table that is within the 1-MByte real-address
    ;    mode address range.
    ; 6. Clear the PE flag in the CR0 register to switch to
    ;    real-address mode.
    RestoreControlRegisters <C>

    ; 7. Execute a far JMP to jump to a real-address mode program
    db    0EAh
    dw    OFFSET _TEXT16:@F - OFFSET _TEXT16 + Kernel16Ofs
    dw    KernelSegment

@@:
    ASSUME ds:ERROR, ss:ERROR, es:ERROR, fs:ERROR, gs:NOTHING
    ; Just entered real mode.
    ; CS holds the segment to the kernel. All other segment registers
    ; are in a quasi-useful state, but we'd rather rely on CS
    RealModeContext

    ; 8. Load registers
    RestoreRegisters <C>

    ; Restore the missing registers: ds, cs, eip and eflags
    mov   ds, Regs.cDS
    push  WORD PTR Regs.cEFLAGS
    push  WORD PTR Regs.cCS
    push  WORD PTR Regs.cEIP
    iret

rmIntK16:
    ASSUME ds:ERROR, ss:ERROR, es:ERROR, fs:ERROR, gs:NOTHING
    ; Invoked whenever an interrupt occurs while the client is in real mode.
    ; CS will hold the segment to the kernel. All other registers are unknown.
    ; We will use CS to access the register file.
    RealModeContext

    ; First figure out the interrupt number
    pop   WORD PTR Regs.interrupt
    and   Regs.interrupt, 0ffffh
    sub   Regs.interrupt, OFFSET rmInt0K16 - OFFSET _TEXT16 + Kernel16Ofs + 3
    shr   Regs.interrupt, 2

    ; Was this an int10 change mode call? Ignore if so
    ; This is because windows tries to go into graphics mode during startup.
    ; For some reason, our probing below "mov eax, cr0" is caught as a GPF
    ; by the HAL and windows doesn't start. So we ignore the interrupt
    ; whether we are in EFI mode or V86 mode.
    cmp   Regs.interrupt, 10h
    jnz   @F
    test  ah, ah
    jnz   @F
    iret
@@:

    ; Service int 1C if this was the timer interrupt
    cmp   Regs.interrupt, HostTimerInterrupt
    jnz   @F
    dec   Regs.counter
    jns   @F
    mov   Regs.counter, HostTimerScaler
    int   1Ch
@@:

    ; Now save the stuff that was pushed onto the stack by the interrupt call
    pop   WORD PTR Regs.cEIP
    and   Regs.cEIP, 0ffffh
    pop   WORD PTR Regs.cCS
    and   Regs.cCS, 0ffffh
    pop   WORD PTR Regs.cEFLAGS
    pushfd
    pop   WORD PTR Regs.cEFLAGS + 2
    pop   WORD PTR Regs.cEFLAGS + 2

    ; DS must be saved manually as SaveContext won't save it
    mov   Regs.cDS, ds

    ; Are we in protected mode? If so, that means we are running in V86 mode,
    ; a whole different set of interrupts are supported in this mode.
    push  eax
    mov   eax, cr0
    test  al, 1
    pop   eax
    jz    @F
    push  WORD PTR Regs.cEFLAGS
    push  WORD PTR Regs.cCS
    push  WORD PTR Regs.cEIP
    jmp   vm86IntK16
@@:

    ; Finally save all the remaining registers.
    SaveContext <C>

    ; Setup call to interrupt handler
    mov   eax, OFFSET _interruptHandler
    mov   Regs.hEIP, eax

    ; Jump to host
    jmp   hostJmpK16

rmReturnK16:
    ; After the client in real mode returns from a krnCall with a RETF,
    ; control is transferred here.

    ; We don't bother saving the client context (at least in this version)
    ; But we save the return value by injecting it into the host context
    mov   Regs.hEAX, eax
    mov   Regs.hEDX, edx

    ; We zero out the return address, to cause a simple return into the
    ; host calling code (where the krnClientCall took place)
    xor   eax, eax
    mov   Regs.hEIP, eax

hostJmpK16:
    ; Called whenever the client in real mode wants to transition to the host.
    ; CS holds the segment to the kernel. All other registers are off limits.

    ; Switch back to protected mode
    ; 1. Disable interrupts (CLI/NMI)
    cli

    ; 2. Load GDT
    ; 3. Set PE flag in CR0
    ; 4. jmp to serialize processor
    ; 5. Load LDT - LDT not used by EFI
    ; 6. Load the task register - TR not used by EFI
    ; 7. Reload segment registers
    ; 8. Load IDT
    ; 9. Enable interrupts STI/NMI
    ; sti

    ; First restore the host context.
    ; We'll be missing CS, DS, EIP and EFLAGS, we'll do those later...
    ; Btw, this will switch the cpu into protected mode.
    RestoreContext <H>

    ; In protected mode, we cannot use CS for write access anymore...
    ; Therefore we need to use DS for context file access
    push  HostDataSelector
    pop   ds
    ASSUME ds:NOTHING
    FlatPtrContext

    ; Transfer control to desired place, either a hostReturn or a hostCall
    ; If a host EIP is specified, it's a host call
    cmp   Regs.hEIP, 0
    jnz   hCall

    ; It's a host return. Retrieve the host EIP from the stack.
    pop   Regs.hEIP
    jmp   @F

hCall:
    ; It's a host call, push a return address
    push  OFFSET krnClientJmp

@@:
    ; Finally, restore CS, EIP and EFLAGS. We use iretd for such delicate task
    ; After this is done, the host takes over.
    push  Regs.hEFLAGS
    push  Regs.hCS
    push  Regs.hEIP

    ; But first, restore DS, which we no longer need to access the context file
    mov   ds, Regs.hDS
    ASSUME ds:ERROR
    iretd

rmInt1CHandlerK16:
    iret

vm86IntK16:
    ; Invoked whenever an interrupt occurs while the client is in virtual 86 mode.
    ; This happens late in the Windows boot process when key BIOS calls (int10 only,
    ; apparently) are necessary. Only a smal subset of all interrupt functionality
    ; is implemented. All unimplemented interrupts causes the kernel to lock up so
    ; that windbg can inspect the unimplemented interrupt.
    RealModeContext
    cmp   Regs.interrupt, 10h
    jz    vm86Int10K16

    ; Unimplemented interrupt. Freeze/reset for dev builds, otherwise, just ignore
vm86UnimplK16:
    stc
    iret
    jmp   $
    mov   dx, 0CF9h
    mov   al, 6
    out   dx, al
    jmp   $

vm86Int10K16:
    cmp   ah, 4fh
    jz    vm86Int104FK16
    jmp   vm86UnimplK16

K16Pointer MACRO addr:REQ
    dd    (OFFSET addr - OFFSET _TEXT16 + Kernel16Ofs) or (KernelSegment SHL 16)
ENDM

K16Copy    MACRO srcAddr:REQ, count:REQ
    mov   cx, count
    push  cs
    pop   ds
    mov   si, OFFSET srcAddr - OFFSET _TEXT16 + Kernel16Ofs
    rep movsb
ENDM

VesaSuccess MACRO
    mov   ax, 4fh
    iret
ENDM

VesaFailure MACRO
    mov   ax, 14fh
    iret
ENDM

vm86Int104FK16:
    cmp   al, 0
    jnz   @F
    K16Copy VesaInfo, VesaInfoSize
    VesaSuccess

@@: cmp   al, 1      ; Get Mode info
    jnz   @F
    K16Copy VesaModeInfo, VesaModeInfoSize
    VesaSuccess

@@: cmp   al, 2      ; Set Mode
    jnz   @F
    VesaSuccess

@@: cmp   al, 3      ; Get current video mode
    jnz   @F
    mov   bx, VesaMode
    VesaSuccess

@@: cmp   al, 4      ; Save/Restore video state
    jnz   @F
    mov   bx, 0
    VesaSuccess

@@: cmp   al, 9
    jnz   @F
    VesaSuccess

Vesa4F05:
@@: VesaFailure

VesaInfo  db          "VESA"
          dw          300h
          K16Pointer  VesaOemName
          dd          3
          K16Pointer  VesaModes
          dw          128 * 1024 / 64    ; 128 MB of Video RAM
          dw          300h               ; Oem Software Version
          K16Pointer  VesaVendorName
          K16Pointer  VesaProductName
          K16Pointer  VesaProductRevision
VesaInfoSize equ $ - OFFSET VesaInfo

VesaOemName         db "JLA", 0
VesaVendorName      db "ATI", 0
VesaProductName     db "Radeon Mobility X1600 (M56)", 0
VesaProductRevision db "1.0", 0
VesaModes           dw VesaMode, 0ffffh       ; Only one mode supported

VesaMode            equ 128h

VesaModeInfo        dw 0BBh               ; LFB, BankSw, !VGA, Graphics, Color, !BIOSOutput, ExtraInfo, Available
                    db 7                  ; win attr A
                    db 0                  ; win attr B
                    dw 64                 ; win gran
                    dw 64                 ; win size
                    dw 0a000h             ; start seg win A
                    dw 0a000h             ; start seg win B
                    K16Pointer Vesa4F05   ; fptr to win pos fn
krnVesaScanline     dw 1700h              ; bytes per scanline
krnVesaXResolution  dw 1440               ; x res
krnVesaYResolution  dw 900                ; y res
                    db 8                  ; char cell width
                    db 16                 ; char cell height
                    db 1                  ; # of memory planes
krnVesaBitsPerPixel db 32                 ; bits per pixel
                    db 1                  ; # of banks
                    db 6                  ; memory model type, 6 = rgb
                    db 0                  ; size of bank in KB
                    db 2                  ; number of image pages minus one that will fit in RAM
                    db 1                  ; reserved
                    db 8                  ; red mask size
                    db 10h                ; red field position
                    db 8                  ; green mask size
                    db 8                  ; green field position
                    db 8                  ; blue mask size
                    db 0                  ; blue field position
                    db 0                  ; reserved mask size
                    db 0                  ; reserved mask position
                    db 0                  ; direct color mode info
krnVesaLFBStart     dd 80010000h          ; start of linear video buffer
krnVesaLFBEnd       dd 8050DC00h          ; start of offscreen memory
krnVesaLFBUKB       dw 2bc9h              ; KB of offscreen memory
krnVesa3Scanline    dw 1700h              ; bytes per scan line in linear modes
VesaModeInfoSize    equ $ - OFFSET VesaModeInfo


    ; Interrupt handlers
    ; Same thing as the protected mode handler list. 256 call instructions
    ; that dispatch their interrupt to the same place (rmIntK16).
    ; The callee determines the interrupt number by inspecting, before
    ; discarding, the return address.
    ; Important: The label must be dword-aligned.
    ALIGN     4
rmInt0K16:
    REPEAT    256
      ALIGN   4
      call    rmIntK16
    ENDM

    ; Kernel ends
    ; Assorted data below.

public c Test16Bits
Test16Bits:
    mov     ax, 1234h
    mov     ds, ax
    mov     ax, 5678h
    mov     es, ax
    mov     ax, 9abch
    mov     fs, ax
    mov     ax, 0def0h
    mov     gs, ax
    mov     eax, 02468ACEh
    mov     ebx, 13579BDFh
    mov     ecx, 0ECA86420h
    mov     edx, 0FDB97531h
    mov     ebp, 0fedcba98h
    mov     esi, 76543210h
    mov     edi, 0f7e6d5c4h
    int     10h
    int     10h
    int     10h
    int     10h
    ret
    mov     eax, 0
    mov     ebx, 0
    sti
@@: cmp     ebx, 0
    jz      @B
@@: inc     eax
    cmp     ebx, 1
    jz      @B
    cli
    int     3

DisketteParams:
    db      0
    db      0
    db      0
    db      2
    db      12h
    db      0
    db      0ffh
    db      0
    db      0
    db      0
    db      0
    db      0

SysConf:
    db      8, 0, 0fch, 2, 74h, 70h, 0, 0, 0, 0

_TEXT16 ENDS

END

IF 0
    Debug 0ffh
    Debug 0ff00h
    Debug 0ffffh
    Debug 0ff0000h
    Debug 0ff00ffh
    Debug 0ffff00h
    Debug 0ffffffh
    Debug 0ff2020h
    Debug 0ff4040h
    Debug 0ff6060h
    Debug 0ff8080h
    Debug 0ffA0A0h
    Debug 0ffC0C0h
    Debug 0ffE0E0h
    Debug 0ffFFFFh
ENDIF