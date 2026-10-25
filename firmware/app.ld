/* Monohull's memory map: Felucca's (ext/Felucca/firmware/app.ld) with one RAM region for data and
 * bss (the SRAM from 0x01C08000 to the stacks), as fm1-sid's player firmware has it. Constant
 * tables (the BLEP tables, the font) stay in flash; core 1's waiting loop and the flash driver run from
 * the RAM below.
 * SPDX-License-Identifier: GPL-3.0-only */
MEMORY
{
    XIP    (rx)  : ORIGIN = 0x02000120, LENGTH = 0x8DFBC
    RAMTEXT (rwx): ORIGIN = 0x01C00000, LENGTH = 0x6000
    RAM    (rwx) : ORIGIN = 0x01C08000, LENGTH = 0x6C000
    NOINIT (rw)  : ORIGIN = 0x01C7C000, LENGTH = 0x3D50
}
SECTIONS
{
    .text : {
        KEEP(*(.text.crt0)) *(.text*) *(.rodata*)
        . = ALIGN(4);
        _etext = .;
    } > XIP
    /* core 1's waiting loop (fm1_cpu1.h), copied to RAM at boot: an idle core 1 never fetches from the flash */
    .c1_text : { _c1_start = .; KEEP(*(.c1_text*)) . = ALIGN(4); _c1_end = .; } > RAMTEXT AT > XIP
    _c1_load = LOADADDR(.c1_text);
    /* the flash driver (hal/fm1_flash.h): code that runs while the flash is busy, copied to RAM at boot */
    .ram_text : { _rt_start = .; KEEP(*(.ram_text*)) . = ALIGN(4); _rt_end = .; } > RAMTEXT AT > XIP
    _rt_load = LOADADDR(.ram_text);
    .data : { . = ALIGN(4); _data_start = .; *(.data*) *(.sdata*) . = ALIGN(4); _data_end = .; } > RAM AT > XIP
    _data_load = LOADADDR(.data);
    _end_image = ALIGN(_data_load + SIZEOF(.data), 4);
    .bss (NOLOAD) : { _bss_start = .; *(.bss*) *(.sbss*) *(COMMON) *(.pool*) . = ALIGN(4); _bss_end = .; } > RAM
    .noinit (NOLOAD) : { *(.noinit*) } > NOINIT
    _guard0 = 0x01C74000; _ustack_lo = 0x01C74100; _ustack_top = 0x01C7A000;
    _guard1 = 0x01C7A000; _sstack_lo = 0x01C7A100; _sstack_top = 0x01C7C000;
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
