OUTPUT_FORMAT(elf32-i386)
ENTRY(driver_entry)

SECTIONS {
    . = 0x10000;
    
    .text : {
        *(.text)
        *(.text.*)
        *(.drv_entry)
    }
    
    .rodata : {
        *(.rodata)
        *(.rodata.*)
    }
    
    .data : {
        *(.data)
        *(.data.*)
    }
    
    .drv_meta : {
        *(.drv_meta)
    }
    
    .bss : {
        *(.bss)
        *(.bss.*)
        *(COMMON)
    }

    .drv_meta : {
        KEEP(*(.drv_meta))
    }
    
    /DISCARD/ : {
        *(.eh_frame)
        *(.comment)
        *(.note.*)
        *(.got)
        *(.got.plt)
        *(.eh_frame_hdr)
    }
}