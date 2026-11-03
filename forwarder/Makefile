#---------------------------------------------------------------------------------
# Loader used by the HOME menu shortcut (a forwarder). It is nx-hbloader as modified by
# Sphaira (https://github.com/ITotalJustice/sphaira, hbl/): it reads /nextNroPath and /nextArgv from
# the romfs of the installed title and starts that .nro.
#
#   make   ->  ../romfs/forwarder/main and ../romfs/forwarder/main.npdm (embedded in pokedex.nro)
#---------------------------------------------------------------------------------
.SUFFIXES:

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>/devkitpro")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITPRO)/libnx/switch_rules

TARGET   := hbl
BUILD    := build
SOURCES  := source
APP_JSON := hbl.json
OUTDIR   := $(CURDIR)/../romfs/forwarder

ARCH     := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE
CFLAGS   := -g -Wall -O2 -ffunction-sections $(ARCH) $(INCLUDE) -D__SWITCH__ -DVERSION=\"3.0.0\"
ASFLAGS  := -g $(ARCH)
LDFLAGS   = -specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) -Wl,-wrap,exit -Wl,-Map,$(notdir $*.map)
LIBS     := -lnx
LIBDIRS  := $(LIBNX)

ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT  := $(CURDIR)/$(TARGET)
export TOPDIR  := $(CURDIR)
export VPATH   := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export DEPSDIR := $(CURDIR)/$(BUILD)
CFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
SFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
export LD      := $(CC)
export OFILES  := $(CFILES:.c=.o) $(SFILES:.s=.o)
export INCLUDE := $(foreach dir,$(LIBDIRS),-I$(dir)/include) -I$(CURDIR)/$(BUILD)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)
export APP_JSON := $(TOPDIR)/$(APP_JSON)
export OUTDIR

.PHONY: $(BUILD) clean all
all: $(BUILD)

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile
	@mkdir -p $(OUTDIR)
	@cp $(OUTPUT).nso $(OUTDIR)/main
	@cp $(OUTPUT).npdm $(OUTDIR)/main.npdm
	@echo "forwarder: $(OUTDIR)"

clean:
	@rm -fr $(BUILD) $(TARGET).nso $(TARGET).npdm $(TARGET).nsp $(TARGET).elf

else
.PHONY: all
DEPENDS := $(OFILES:.o=.d)

all: $(OUTPUT).nso $(OUTPUT).npdm
$(OUTPUT).nso: $(OUTPUT).elf
$(OUTPUT).elf: $(OFILES)

-include $(DEPENDS)
endif
