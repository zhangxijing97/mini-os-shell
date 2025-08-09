# mini-os-shell

## 00-environment

### QEMU
**QEMU** (Quick Emulator) is an open-source machine emulator and virtualizer.  
In this project, QEMU is used to **simulate a computer** so we can run and test our boot sector code without using real hardware. It loads the disk image (our compiled boot sector) and executes it just like a real BIOS would during the boot process.

### NASM
**NASM** (Netwide Assembler) is an assembler for the **x86 architecture**.  
In this project, NASM is used to **assemble the boot sector source code** written in x86 assembly (`.asm`) into a **raw binary format** (`.bin`) that the BIOS can load and execute.

## 01-bootsector-barebones

### Boot sector program
```
; Infinite loop (e9 fd ff)
loop:
    jmp loop

; Fill with 510 zeros minus the size of the previous code
times 510-($-$$) db 0
; Magic number
dw 0xaa55
```

### Binary of Boot sector program
```
e9 fd ff 00 00 00 00 00 00 00 00 00 00 00 00 00
00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
[ 29 more lines with sixteen zero-bytes each ]
00 00 00 00 00 00 00 00 00 00 00 00 00 00 55 aa
```
1. **`e9 fd ff`** – Machine code for:
   ```asm
   jmp loop
   ```
   This is an infinite jump instruction, telling the CPU to loop forever.

2. **`00 ... 00`** – Padding bytes.  
   BIOS requires the boot sector to be **exactly 512 bytes**.  
   We use:
   ```asm
   times 510-($-$$) db 0
   ```
   to fill the unused space with zeros up to byte 510.

3. **`55 aa`** – Boot sector signature (magic number).  
   The last two bytes of a valid boot sector must be `0x55 0xAA`.  
   The BIOS checks for this value to confirm the sector is bootable.

### Assembler
An **assembler** is a program that converts assembly language (human-readable CPU instructions) into machine code (binary) that the CPU can actually execute.

**Example:**
- You write: `mov al, 'H'` (assembly language)
- The assembler turns it into: `B0 48` (binary bytes in hex)
- The CPU can read and run those bytes directly.

### BIOS (Basic Input/Output System)
The **BIOS** is a small program stored in the computer's motherboard.  
When you power on the computer, the BIOS is the first code that runs. It initializes hardware (keyboard, screen, storage, etc.) and loads the boot sector from the selected boot device into memory, then starts executing it.

**Startup process:**
1. Check and initialize hardware (keyboard, screen, storage, etc.).
2. Read the first sector (boot sector) from the boot device into memory.
3. Pass control to the boot sector code to continue execution.

### Build and Run

```bash
# Assemble the boot sector source file (boot_sect.asm) into a raw binary (boot_sect.bin)
# - /opt/homebrew/bin/nasm : explicitly use Homebrew's NASM assembler (Apple Silicon default path)
# - -f bin                 : output format is raw binary (no headers, exactly what BIOS loads)
# - boot_sect.asm          : input assembly source file
# - -o boot_sect.bin       : output file name for the compiled binary
/opt/homebrew/bin/nasm -f bin boot_sect.asm -o boot_sect.bin
```

```bash
# Run the compiled boot sector binary in QEMU, emulating a 32-bit x86 PC
# - qemu-system-i386       : QEMU binary that emulates an i386 (32-bit) machine
# - -drive file=...        : specifies the disk image file to use
# - format=raw             : treat the file as a raw binary image
# - if=floppy              : emulate it as a floppy disk for the BIOS to boot from
qemu-system-i386 -drive file=boot_sect.bin,format=raw,if=floppy
```

## 02-bootsector-print

### Interrupts
An **interrupt** is a signal that tells the CPU to pause current tasks and run a special routine (Interrupt Service Routine, or ISR).  
In `02-bootsector-print`, `int 0x10` triggers a BIOS video service to print a character on the screen.

### CPU Registers
Registers are tiny super-fast storage inside the CPU. In this lesson:  
- **AH**: Specifies the function code for BIOS (e.g., `0x0E` means "print character").  
- **AL**: Holds the character to be printed (e.g., `'H'`).  

## 03-bootsector-print

###  Programming Conventions
Although we can technically use General-purpose registers whatever we want, following common conventions makes the code easier to read.
- AX: Arithmetic / results
- BX: Base memory address
- CX: Loop counter
- DX: I/O or high-order calculations

### Memory Offsets
A **memory offset** is how far (in bytes) a location is from the start of a segment.  
The CPU uses it with the segment address (e.g., `DS`) to find the exact memory location.

### Pointers
A **pointer** stores a memory address, not the actual data.  

### Comparing three ways to display letters

#### Direct character in AL
```asm
[org 0x7C00]
mov ah, 0x0e     ; BIOS "print character" function
mov al, 'H'      ; put 'H' directly into AL
int 0x10         ; print 'H'
jmp $            ; infinite loop
times 510-($-$$) db 0
dw 0xAA55        ; boot signature
```

#### From label (symbolic address)
```asm
[org 0x7C00]
mov ah, 0x0e     ; BIOS "print character" function
mov al, 'H'      ; put 'H' directly into AL
int 0x10         ; print 'H'

jmp $            ; infinite loop
times 510-($-$$) db 0
dw 0xAA55        ; boot signature
```

#### Direct character in AL
```asm
[org 0x7C00]
mov ah, 0x0e     ; BIOS "print character" function
mov al, [msg]    ; load the byte stored at label 'msg'
int 0x10         ; print the character

jmp $            ; infinite loop

msg:
    db 'X'       ; store 'X' at label 'msg'

times 510-($-$$) db 0
dw 0xAA55        ; boot signature
```

#### From absolute memory address
```
[org 0x7C00]
mov ah, 0x0e       ; BIOS "print character" function
mov al, [0x7C1B]   ; load byte from exact memory address (0x7C1B)
int 0x10           ; print the character

jmp $              ; infinite loop
times 510-($-$$) db 0
dw 0xAA55          ; boot signature
```
Need to use `xxd -g1 -c16 boot_sect.bin | grep ' 58 '` to get exact memory address.

### Without `[org 0x7C00]`
- `mov al, the_secret` → Loads the **address** of `the_secret` (wrong if you want the value).  
- `mov al, [the_secret]` → Tries to load the value from `the_secret`, but uses a **wrong base address** (because BIOS loads code at 0x7C00, not 0).  
- `mov al, the_secret + 0x7C00` → After adding 0x7C00, BX contains the real location of 'X' in RAM, so it fetches 0x58 (the ASCII code for 'X') and prints it.
- `mov al, 0x2D + 0x7C00` → Loads the value directly from **absolute address** (works but you must manually count the offset every time).  

### With `[org 0x7C00]`
- `mov al, the_secret` → Still loads the **address** of `the_secret` (not the value).  
- `mov al, [the_secret]` → ✅ Correct! Loads the value from `the_secret` automatically with the right offset.  
- `mov al, the_secret + 0x7C00` → ❌ Wrong now, because the offset is already handled by `org` (you’re adding 0x7C00 twice).  
- `mov al, 0x2D + 0x7C00` → Works, but manual counting is still required (not flexible).  

## 04-bootsector-stack

