# mini-os-shell

## Tools Used

### QEMU
**QEMU** (Quick Emulator) is an open-source machine emulator and virtualizer.  
In this project, QEMU is used to **simulate a computer** so we can run and test our boot sector code without using real hardware. It loads the disk image (our compiled boot sector) and executes it just like a real BIOS would during the boot process.

### NASM
**NASM** (Netwide Assembler) is an assembler for the **x86 architecture**.  
In this project, NASM is used to **assemble the boot sector source code** written in x86 assembly (`.asm`) into a **raw binary format** (`.bin`) that the BIOS can load and execute.

## Build and Run

```bash
/opt/homebrew/bin/nasm -f bin boot_sect.asm -o boot_sect.bin
```

```
qemu-system-i386 -drive file=boot_sect.bin,format=raw,if=floppy
```