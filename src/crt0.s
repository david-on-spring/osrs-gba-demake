@ GBA startup: cartridge header, stacks, section init, IRQ handler.
    .section .crt0, "ax"
    .arm
    .global _start
_start:
    b       start_vector
    .fill   156, 1, 0               @ Nintendo logo area (filled by tools/fixrom.py)
    .ascii  "RS07 DEMAKE "          @ game title (12)
    .ascii  "BRSE"                  @ game code
    .ascii  "01"                    @ maker code
    .byte   0x96                    @ fixed value
    .byte   0x00                    @ main unit code
    .byte   0x00                    @ device type
    .fill   7, 1, 0                 @ reserved
    .byte   0x00                    @ software version
    .byte   0x00                    @ header checksum (fixed by tools/fixrom.py)
    .fill   2, 1, 0

start_vector:
    mov     r0, #0x12               @ IRQ mode
    msr     cpsr_c, r0
    ldr     sp, =0x03007FA0
    mov     r0, #0x1F               @ System mode
    msr     cpsr_c, r0
    ldr     sp, =0x03007F00

    ldr     r0, =__data_lma         @ copy .data/.iwram to IWRAM
    ldr     r1, =__data_start
    ldr     r2, =__data_end
1:  cmp     r1, r2
    ldrlt   r3, [r0], #4
    strlt   r3, [r1], #4
    blt     1b

    mov     r3, #0
    ldr     r1, =__bss_start        @ clear .bss
    ldr     r2, =__bss_end
2:  cmp     r1, r2
    strlt   r3, [r1], #4
    blt     2b
    ldr     r1, =__ewram_start      @ clear .ewram
    ldr     r2, =__ewram_end
3:  cmp     r1, r2
    strlt   r3, [r1], #4
    blt     3b

    ldr     r0, =irq_handler
    ldr     r1, =0x03007FFC
    str     r0, [r1]

    ldr     r0, =main
    mov     lr, pc
    bx      r0
4:  b       4b

    .global irq_handler
irq_handler:
    mov     r0, #0x04000000
    add     r0, r0, #0x200
    ldr     r1, [r0]                @ IE | (IF << 16)
    and     r1, r1, r1, lsr #16     @ IE & IF
    strh    r1, [r0, #2]            @ acknowledge
    ldr     r2, =0x03007FF8         @ BIOS IRQ flags for IntrWait
    ldrh    r3, [r2]
    orr     r3, r3, r1
    strh    r3, [r2]
    tst     r1, #1                  @ VBlank: re-arm the panel gradient HBlank DMA
    bxeq    lr
    ldr     r2, =snd_play_frame     @ music: count frames; every 4th, rewind DMA1 to the
    ldr     r3, [r2]                @ start of the 4-frame DirectSound ring (sound.c)
    add     r3, r3, #1
    str     r3, [r2]
    tst     r3, #3
    bne     5f
    ldr     r2, =0x040000BC         @ DMA1SAD
    mov     r3, #0
    str     r3, [r2, #8]            @ DMA1CNT = 0
    ldr     r3, =snd_ring
    str     r3, [r2]
    ldr     r3, =0xB6400004         @ enable, FIFO timing, repeat, 32-bit, dst fixed
    str     r3, [r2, #8]
5:
    ldr     r2, =0x040000B0
    mov     r3, #0
    str     r3, [r2, #8]            @ DMA0CNT = 0
    ldr     r0, =panel_grad
    ldrh    r3, [r0], #2
    ldr     r12, =0x05000002
    strh    r3, [r12]               @ line 0 colour
    str     r0, [r2]                @ SAD = &panel_grad[1]
    str     r12, [r2, #4]           @ DAD = palette entry 1
    ldr     r3, =0xA2400001         @ enable, HBlank, repeat, dst fixed, 16-bit, 1 unit
    str     r3, [r2, #8]
    bx      lr
    .pool
