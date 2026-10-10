// Minimal GBA hardware definitions.
#pragma once
#include <stdint.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef volatile u16 vu16;
typedef volatile u32 vu32;

#define EWRAM_BSS __attribute__((section(".ewram")))
#define IWRAM_CODE __attribute__((section(".iwram"), long_call, target("arm")))

#define REG16(a) (*(vu16 *)(a))
#define REG32(a) (*(vu32 *)(a))

#define REG_DISPCNT REG16(0x04000000)
#define REG_DISPSTAT REG16(0x04000004)
#define REG_VCOUNT REG16(0x04000006)
#define REG_BGCNT(n) REG16(0x04000008 + 2 * (n))
#define REG_BGHOFS(n) REG16(0x04000010 + 4 * (n))
#define REG_BGVOFS(n) REG16(0x04000012 + 4 * (n))
#define REG_MOSAIC REG16(0x0400004C)
#define REG_BLDCNT REG16(0x04000050)
#define REG_BLDALPHA REG16(0x04000052)
#define REG_BLDY REG16(0x04000054)

#define REG_SOUND1CNT_L REG16(0x04000060)
#define REG_SOUND1CNT_H REG16(0x04000062)
#define REG_SOUND1CNT_X REG16(0x04000064)
#define REG_SOUND2CNT_L REG16(0x04000068)
#define REG_SOUND2CNT_H REG16(0x0400006C)
#define REG_SOUND4CNT_L REG16(0x04000078)
#define REG_SOUND4CNT_H REG16(0x0400007C)
#define REG_SOUNDCNT_L REG16(0x04000080)
#define REG_SOUNDCNT_H REG16(0x04000082)
#define REG_SOUNDCNT_X REG16(0x04000084)

#define REG_DMA0SAD REG32(0x040000B0)
#define REG_DMA0DAD REG32(0x040000B4)
#define REG_DMA0CNT REG32(0x040000B8)
#define REG_DMA1SAD REG32(0x040000BC)
#define REG_DMA1DAD REG32(0x040000C0)
#define REG_DMA1CNT REG32(0x040000C4)
#define REG_DMA3SAD REG32(0x040000D4)
#define REG_DMA3DAD REG32(0x040000D8)
#define REG_DMA3CNT REG32(0x040000DC)

#define REG_TM0CNT_L REG16(0x04000100)
#define REG_TM0CNT_H REG16(0x04000102)
#define REG_FIFO_A REG32(0x040000A0)

#define REG_KEYINPUT REG16(0x04000130)
#define REG_IE REG16(0x04000200)
#define REG_IF REG16(0x04000202)
#define REG_WAITCNT REG16(0x04000204)
#define REG_IME REG16(0x04000208)

#define PAL_BG ((vu16 *)0x05000000)
#define PAL_OBJ ((vu16 *)0x05000200)
#define VRAM ((vu16 *)0x06000000)
#define VRAM_OBJ ((vu16 *)0x06010000)
#define OAM_MEM ((vu16 *)0x07000000)
#define SRAM ((volatile u8 *)0x0E000000)

#define DMA_ENABLE (1u << 31)
#define DMA_32 (1u << 26)
#define DMA_16 0
#define DMA_SRC_FIXED (2u << 23)
#define DMA_DST_FIXED (2u << 21)

#define KEY_A 0x0001
#define KEY_B 0x0002
#define KEY_SELECT 0x0004
#define KEY_START 0x0008
#define KEY_RIGHT 0x0010
#define KEY_LEFT 0x0020
#define KEY_UP 0x0040
#define KEY_DOWN 0x0080
#define KEY_R 0x0100
#define KEY_L 0x0200

static inline void dma3_copy32(void *dst, const void *src, u32 words) {
    __asm__ volatile("" ::: "memory");   // the DMA reads memory behind the compiler's back
    REG_DMA3SAD = (u32)src;
    REG_DMA3DAD = (u32)dst;
    REG_DMA3CNT = words | DMA_ENABLE | DMA_32;
    __asm__ volatile("" ::: "memory");
}

static inline void dma3_fill32(void *dst, u32 value, u32 words) {
    static volatile u32 v;
    v = value;
    REG_DMA3SAD = (u32)&v;
    REG_DMA3DAD = (u32)dst;
    REG_DMA3CNT = words | DMA_ENABLE | DMA_32 | DMA_SRC_FIXED;
    __asm__ volatile("" ::: "memory");
}

static inline void vblank_wait(void) { __asm__ volatile("swi 0x05" ::: "r0", "r1", "r2", "r3", "memory"); }
