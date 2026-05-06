    .section .rodata
    .global g_eep_default_image
    .global g_eep_default_image_end
    .balign 4

g_eep_default_image:
    .incbin "../lib/soes-esi/eeprom.bin"
g_eep_default_image_end:
