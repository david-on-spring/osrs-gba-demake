# RuneScape 2007 demake for the Game Boy Advance.
# Toolchain: Arm GNU Toolchain (arm-none-eabi); see build.sh for the bundled copy.
PREFIX ?= arm-none-eabi-
CC := $(PREFIX)gcc
OBJCOPY := $(PREFIX)objcopy
PYTHON ?= python3
GBAFIX ?= $(firstword $(wildcard .toolchain/gbafix) $(shell command -v gbafix))

BUILD := build
TARGET := rs07demake
SRCS := $(wildcard src/*.c) $(BUILD)/assets.c $(BUILD)/music_data.c $(BUILD)/avatar_art.c $(BUILD)/quest_data.c
OBJS := $(patsubst %.c,$(BUILD)/%.o,$(notdir $(SRCS))) $(BUILD)/crt0.o $(BUILD)/mixer.o

ARCH := -mcpu=arm7tdmi -mtune=arm7tdmi -mthumb -mthumb-interwork
CFLAGS := $(ARCH) -O2 -std=gnu11 -Wall -Wextra -Wno-unused-parameter -ffreestanding \
          -fno-strict-aliasing -ffunction-sections -fdata-sections -Isrc -I$(BUILD)
LDFLAGS := $(ARCH) -nostartfiles -nostdlib -T src/gba.ld -Wl,--gc-sections -Wl,--no-warn-rwx-segments -Wl,-Map,$(BUILD)/$(TARGET).map

all: $(TARGET).gba

# pattern rule with two targets = one recipe run produces both (safe under make -j)
$(BUILD)/assets%c $(BUILD)/assets%h $(BUILD)/quest_data%c $(BUILD)/quest_data%h: tools/gen_assets.py tools/world_gen.py tools/world_cache.py tools/world_emit.py tools/hd_art.py tools/gen_quests.py tools/gen_title.py tools/quests.txt $(wildcard tools/zones/*.py tools/zones/*.ref)
	@mkdir -p $(BUILD)
	$(PYTHON) tools/gen_assets.py $(BUILD)

# music/local/tracks.txt (git-ignored) overrides the bundled manifest
MANIFEST := $(firstword $(wildcard music/local/tracks.txt) music/tracks.txt)
MIDIS := $(wildcard music/*.mid music/*.midi music/local/*.mid music/local/*.midi)
$(BUILD)/music_data.c: tools/midi2gba.py $(MANIFEST) $(MIDIS)
	@mkdir -p $(BUILD)
	$(PYTHON) tools/midi2gba.py $(MANIFEST) $(BUILD)

$(BUILD)/music_data.h: $(BUILD)/music_data.c

$(BUILD)/avatar_art.c: tools/gen_avatar.py
	@mkdir -p $(BUILD)
	$(PYTHON) tools/gen_avatar.py $(BUILD)

$(BUILD)/avatar_art.h: $(BUILD)/avatar_art.c

$(BUILD)/%.o: src/%.c src/game.h src/gba.h src/ui_internal.h src/world_internal.h $(BUILD)/assets.h $(BUILD)/music_data.h $(BUILD)/avatar_art.h $(BUILD)/quest_data.h
	$(CC) $(CFLAGS) -c $< -o $@

# version.h: the git tag (or tag-commits-hash between releases) and the commit date. Rewritten
# only when its contents change, so a rebuild recompiles menu.o only after a new commit or tag.
GIT_VERSION := $(shell git describe --tags --always --dirty 2>/dev/null || echo unknown)
GIT_DATE := $(shell git log -1 --format=%cs 2>/dev/null || echo unknown)
$(BUILD)/version.h: FORCE
	@mkdir -p $(BUILD)
	@printf '#define GAME_VERSION "%s"\n#define GAME_DATE "%s"\n' '$(GIT_VERSION)' '$(GIT_DATE)' > $@.tmp
	@cmp -s $@.tmp $@ && rm $@.tmp || mv $@.tmp $@

$(BUILD)/menu.o: $(BUILD)/version.h

$(BUILD)/assets.o: $(BUILD)/assets.c $(BUILD)/assets.h
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/quest_data.o: $(BUILD)/quest_data.c $(BUILD)/quest_data.h $(BUILD)/assets.h src/game.h
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/music_data.o: $(BUILD)/music_data.c $(BUILD)/music_data.h
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/mixer.o: src/mixer.s
	@mkdir -p $(BUILD)
	$(CC) $(ARCH) -marm -c $< -o $@

$(BUILD)/avatar_art.o: $(BUILD)/avatar_art.c $(BUILD)/avatar_art.h
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/crt0.o: src/crt0.s
	@mkdir -p $(BUILD)
	$(CC) $(ARCH) -marm -c $< -o $@

$(BUILD)/$(TARGET).elf: $(OBJS) src/gba.ld
	$(CC) $(LDFLAGS) $(OBJS) -lgcc -o $@

$(TARGET).gba: $(BUILD)/$(TARGET).elf tools/fixrom.py
	$(OBJCOPY) -O binary $< $@
	$(PYTHON) tools/fixrom.py $@
	$(if $(GBAFIX),$(GBAFIX) $@)

# headless test suite (needs build/harness from tools/build_harness.sh, or .toolchain/harness)
test: $(TARGET).gba
	sh tools/run_quest_tests.sh

clean:
	rm -rf $(BUILD) $(TARGET).gba

.PHONY: all clean test FORCE
