ENTRY(_start)

SECTIONS
{
    . = 0x80000000;

    .text.init : { *(.text.init) }
    .text      : { *(.text .text.*) }
    .rodata    : { *(.rodata .rodata.*) }
    .data      : { *(.data .data.*) }
    .got       : { *(.got .got.*) }
    .got.plt   : { *(.got.plt) }

    __bss_start = .;
    .bss : { *(.bss .bss.*) *(COMMON) }
    __bss_end = .;

    /* Ensure nothing runs past 16 MB */
    ASSERT(. <= 0x81000000, "Firmware too large for 16 MB RAM")
}

/* Stack placed just below the top of 16 MB */
_stack_top = 0x80FE0000;
