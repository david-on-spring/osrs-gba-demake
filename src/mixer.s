@ Software mixer inner loops (ARM code in IWRAM). See sound.c.
@
@ u32 mix_wave(s32 *acc, const s8 *wave256, u32 pos, u32 step, s32 volq8, s32 dvol4, int n)
@   acc[i] += wave[pos >> 24] * (vol >> 8), pos += step; vol += dvol4 every 4 samples.
@   n must be a multiple of 4. Returns the new pos.
@ u32 mix_oneshot(s32 *acc, const s8 *sample, u32 pos, u32 step, s32 volq8, s32 dvol4, int n)
@   Same with a 20.12 position into a one-shot sample (caller limits n to the sample end).
    .section .iwram, "ax", %progbits
    .arm
    .align 2

    .global mix_wave
mix_wave:
    push    {r4-r11, lr}
    ldr     r4, [sp, #36]           @ vol (Q8)
    ldr     r5, [sp, #40]           @ dvol per 4 samples
    ldr     r6, [sp, #44]           @ n
    add     r6, r0, r6, lsl #2      @ end of acc
1:  ldmia   r0, {r7-r10}
    mov     r11, r4, asr #8
    mov     r12, r2, lsr #24
    ldrsb   r12, [r1, r12]
    add     r2, r2, r3
    mla     r7, r12, r11, r7
    mov     r12, r2, lsr #24
    ldrsb   r12, [r1, r12]
    add     r2, r2, r3
    mla     r8, r12, r11, r8
    mov     r12, r2, lsr #24
    ldrsb   r12, [r1, r12]
    add     r2, r2, r3
    mla     r9, r12, r11, r9
    mov     r12, r2, lsr #24
    ldrsb   r12, [r1, r12]
    add     r2, r2, r3
    mla     r10, r12, r11, r10
    stmia   r0!, {r7-r10}
    add     r4, r4, r5
    cmp     r0, r6
    blt     1b
    mov     r0, r2
    pop     {r4-r11, lr}
    bx      lr

    .global mix_oneshot
mix_oneshot:
    push    {r4-r11, lr}
    ldr     r4, [sp, #36]
    ldr     r5, [sp, #40]
    ldr     r6, [sp, #44]
    cmp     r6, #0
    ble     3f
    add     r6, r0, r6, lsl #2
    mov     r8, #0
2:  ldr     r7, [r0]
    mov     r11, r4, asr #8
    mov     r12, r2, lsr #12
    ldrsb   r12, [r1, r12]
    add     r2, r2, r3
    mla     r7, r12, r11, r7
    str     r7, [r0], #4
    add     r8, r8, #1
    tst     r8, #3
    addeq   r4, r4, r5
    cmp     r0, r6
    blt     2b
3:  mov     r0, r2
    pop     {r4-r11, lr}
    bx      lr

@ void mix_out(s8 *out, s32 *acc, const s8 *clip_mid, int n)
@   out[i] = clip_mid[clamp(acc[i] >> 9, -512, 511)]; acc[i] = 0.  n multiple of 4.
    .global mix_out
mix_out:
    push    {r4-r10, lr}
    mov     r10, #0
    ldr     r9, =511
    mvn     r8, r9                  @ -512
1:  ldmia   r1, {r4-r7}
    .irp    reg, r4, r5, r6, r7
    movs    \reg, \reg, asr #9
    cmp     \reg, r9
    movgt   \reg, r9
    cmp     \reg, r8
    movlt   \reg, r8
    ldrsb   \reg, [r2, \reg]
    strb    \reg, [r0], #1
    .endr
    mov     r4, #0
    mov     r5, #0
    mov     r6, #0
    mov     r7, #0
    stmia   r1!, {r4-r7}
    subs    r3, r3, #4
    bgt     1b
    pop     {r4-r10, lr}
    bx      lr
    .pool
