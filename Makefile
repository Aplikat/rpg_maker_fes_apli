.SUFFIXES:
ifeq ($(strip $(DEVKITARM)),)
$(error "Define DEVKITARM")
endif
include $(DEVKITARM)/3ds_rules

TARGET  := FesLoader
ARCH    := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
CFLAGS  := -g -Wall -O2 -mword-relocations -ffunction-sections $(ARCH) -I$(CTRULIB)/include -D__3DS__
LDFLAGS := -specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(TARGET).map
LIBS    := -L$(CTRULIB)/lib -lcitro2d -lcitro3d -lctru -lm

all: $(TARGET).3dsx

$(TARGET).elf: source/main.c
	$(CC) $(CFLAGS) $< $(LDFLAGS) $(LIBS) -o $@

$(TARGET).smdh:
	smdhtool --create "FesLoader" "Catalogo de mundos de RPG Maker Fes" "muysimple001" $(CTRULIB)/default_icon.png $@

$(TARGET).3dsx: $(TARGET).elf $(TARGET).smdh
	3dsxtool $< $@ --smdh=$(TARGET).smdh

clean:
	rm -f *.elf *.3dsx *.smdh *.map
