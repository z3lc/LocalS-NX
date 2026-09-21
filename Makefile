#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment")
endif

TOPDIR ?= $(CURDIR)

include $(DEVKITPRO)/libnx/switch_rules

#---------------------------------------------------------------------------------
# Project
#---------------------------------------------------------------------------------

TARGET      := LocalS-NX
BUILD       := build

SOURCES     := source source/ui
DATA        := data
INCLUDES    := include

ROMFS       := romfs

#---------------------------------------------------------------------------------
# Architecture
#---------------------------------------------------------------------------------

ARCH := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE

CFLAGS := \
    -g \
    -Wall \
    -Werror \
    -O2 \
    -ffunction-sections \
    -fdata-sections \
    $(ARCH) \
    -D__SWITCH__ \
    -I$(TOPDIR)/include \
    -I$(TOPDIR)/Plutonium/Plutonium/include \
    -I$(LIBNX)/include \
    -I$(PORTLIBS)/include

CXXFLAGS := \
    $(CFLAGS) \
    -fno-rtti \
    -fno-exceptions \
    -std=gnu++17

ASFLAGS := -g $(ARCH)

LDFLAGS := \
    -specs=$(DEVKITPRO)/libnx/switch.specs \
    -g \
    $(ARCH) \
    -Wl,-Map,$(notdir $*.map)

#---------------------------------------------------------------------------------
# Libraries
#---------------------------------------------------------------------------------

LIBS := \
    -lpu \
    -lSDL2_mixer \
    -lopusfile \
    -lopus \
    -lmodplug \
    -lmpg123 \
    -lvorbisidec \
    -logg \
    -lSDL2_ttf \
    -lSDL2_gfx \
    -lSDL2_image \
    -lSDL2 \
    -lEGL \
    -lGLESv2 \
    -lglapi \
    -ldrm_nouveau \
    -lwebp \
    -lpng \
    -ljpeg \
    `sdl2-config --libs` \
    -lfreetype \
    `$(PREFIX)pkg-config --cflags freetype2` \
    -lharfbuzz \
    -lz \
    -lbz2 \
    -lnx

#---------------------------------------------------------------------------------
# Plutonium
#---------------------------------------------------------------------------------

LIBDIRS := \
    $(PORTLIBS) \
    $(LIBNX) \
    $(TOPDIR)/Plutonium/Plutonium

#---------------------------------------------------------------------------------
# Build system
#---------------------------------------------------------------------------------

ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT := $(CURDIR)/$(TARGET)
export TOPDIR := $(CURDIR)

export VPATH := \
    $(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
    $(foreach dir,$(DATA),$(CURDIR)/$(dir))

CFILES := \
    $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))

CPPFILES := \
    $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))

SFILES := \
    $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))

BINFILES := \
    $(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

ifeq ($(strip $(CPPFILES)),)
export LD := $(CC)
else
export LD := $(CXX)
endif

export DEPSDIR := $(CURDIR)/$(BUILD)

export OFILES_BIN := \
    $(addsuffix .o,$(BINFILES))

export OFILES_SRC := \
    $(CPPFILES:.cpp=.o) \
    $(CFILES:.c=.o) \
    $(SFILES:.s=.o)

export OFILES := \
    $(OFILES_BIN) \
    $(OFILES_SRC)

export HFILES_BIN := \
    $(addsuffix .h,$(subst .,_,$(BINFILES)))

export INCLUDE := \
    $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
    $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
    -I$(CURDIR)/$(BUILD)

export LIBPATHS := \
    $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

export NROFLAGS += --romfsdir=$(CURDIR)/$(ROMFS)

export NROFLAGS += --nacp=$(CURDIR)/$(TARGET).nacp

export APP_TITLE := LocalS-NX
export APP_AUTHOR := Zel
export APP_VERSION := 0.1.0

.PHONY: all clean

all: $(BUILD)

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@echo clean ...
	@rm -fr $(BUILD)
	@rm -f $(TARGET).nro
	@rm -f $(TARGET).nacp
	@rm -f $(TARGET).elf

else

DEPENDS := $(OFILES:.o=.d)

$(OUTPUT).nro: $(OUTPUT).elf $(OUTPUT).nacp

$(OUTPUT).elf: $(OFILES)

$(OFILES_SRC): $(HFILES_BIN)

%.bin.o %_bin.h: %.bin
	@echo $(notdir $<)
	@$(bin2o)

-include $(DEPENDS)

endif