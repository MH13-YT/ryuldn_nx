#---------------------------------------------------------------------------------
# ryuldn_nx Tesla Overlay (.ovl) - COMPLÈTEMENT FONCTIONNEL
#---------------------------------------------------------------------------------

.SUFFIXES:

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=/opt/devkitpro")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITPRO)/libnx/switch_rules

# Auto-detect Tesla
LIBTESLA ?= $(DEVKITPRO)/libtesla
ifeq ($(wildcard $(LIBTESLA)),)
    LIBTESLA := ../libtesla
endif

TARGET_VERSION := $(shell git describe --tags --dirty 2>/dev/null || echo "v1.0.0-dev")
VERSION_DEFINES := -DGITDESCVER="\"${TARGET_VERSION}\""

TARGET      := ovlRyuLDN
BUILD       := build
SOURCES     := source
INCLUDES    := include ../common $(LIBTESLA)/include

NO_ICON     := 1

#---------------------------------------------------------------------------------
ARCH        := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE

CFLAGS      := -g -Wall -O2 -ffunction-sections \
               $(ARCH) $(DEFINES) $(VERSION_DEFINES)

CFLAGS      += $(INCLUDE) -D__SWITCH__

# TESLA OVERLAY FIX - CRUCIAL
CFLAGS      += -DTSL_FB_WIDTH=1280 -DTSL_FB_HEIGHT=720 \
               -DTSL_LAYER_WIDTH=1280 -DTSL_LAYER_HEIGHT=720 \
               -DTSL_LAYER_X=0 -DTSL_LAYER_Y=0 -DTSL_USE_SINGLE_FB=1

CXXFLAGS    := $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++17 -fpermissive

ASFLAGS     := -g $(ARCH)
LDFLAGS     := -specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) \
               -Wl,--section-start,.text=0x40010000 -Wl,--build-id=sha1

# LIBS TESLA + STB + NX
LIBDIRS     := $(PORTLIBS) $(LIBNX) $(LIBTESLA)/lib $(ATMOSPHERELIBSDIR)/lib
LIBS        := -ltsl -lnx -lmesosphere -lfs -lsf

#---------------------------------------------------------------------------------
# Standard template reste identique
#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT  := $(CURDIR)/$(TARGET)
export TOPDIR  := $(CURDIR)

export VPATH   := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
             $(foreach dir,$(DATA),$(CURDIR)/$(dir))

export DEPSDIR := $(CURDIR)/$(BUILD)

CFILES      := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES    := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES      := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
BINFILES    := $(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

ifeq ($(strip $(CPPFILES)),)
    export LD := $(CC)
else
    export LD := $(CXX)
endif

export OFILES_BIN  := $(addsuffix .o,$(BINFILES))
export OFILES_SRC  := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES      := $(OFILES_BIN) $(OFILES_SRC)
export HFILES_BIN  := $(addsuffix .h,$(subst .,_,$(BINFILES)))

export INCLUDE     := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                  $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
                  -I$(CURDIR)/$(BUILD)

export LIBPATHS    := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

.PHONY: $(BUILD) clean all

all: $(BUILD)

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).*.ovl $(TARGET).*.elf $(TARGET).*~ 

else

DEPENDS	:= $(OFILES:.o=.d)

#---------------------------------------------------------------------------------
all: $(OUTPUT).ovl

$(OUTPUT).ovl: $(OUTPUT).elf
	@$(DEVKITPRO)/devkitA64/bin/elf2nro $< $@ --singleNsoTarget 1
	@echo "built ... $(notdir $@)"

$(OUTPUT).elf: $(OFILES)

$(OFILES_SRC): $(HFILES_BIN)

%.bin.o %_bin.h : %.bin
	@echo $(notdir $<)
	@$(bin2o)

-include $(DEPENDS)

endif
#---------------------------------------------------------------------------------
