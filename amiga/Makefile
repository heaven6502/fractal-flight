# Amiga fractal flight: Bartman toolchain (m68k-amiga-elf-gcc + elf2hunk).
#   gnumake                 builds out/flight320.exe (default)
#   gnumake PROG=flight     builds out/flight.exe
#   gnumake PROG=flight16   builds out/flight16.exe  (the same with the 16 bit fractal line, -DF_WIDELINE)
#   gnumake PROG=flight320  builds out/flight320.exe (320 x 200 single pixels, the FAR fan (55 vertices / 90 edges) like flight, shaded (add -DFLIGHT_LEGACY for the game's 45 / 72): src/flight320_main.c + the FLIGHT320 part of flight_core.c)
#   gnumake PROFILE=1 ...   adds stage timing (out/<prog>_prof.exe, see src/prof.h and tools/readprof.py)
# usually run through tools/run.sh <flight320|flight> [gdb]
ifdef OS
	WINDOWS = 1
	SHELL = cmd.exe
endif

PROG ?= flight320
# the byte line routine of the flight: flight_line_k = with the unrolled kernels for the small pieces (tools/mkline_k.py, generated from flight_line.asm), flight_line = the plain recursion (make LINE=flight_line)
LINE ?= flight_line_k
TAG =
ifdef PROFILE
	TAG = _prof
endif
ifdef E9K
	TAG = _e9k
endif
ifdef PUAE
	TAG = _puae
endif
ifdef OLD
	TAG = _old
endif
ifdef LERPCHECK
	TAG = _lerp
endif
ifdef PROJCHECK
	TAG = _proj
endif
ifdef NODOTS
	TAG := $(TAG)_nd
endif
ifdef PIPE
	TAG := $(TAG)_p$(PIPE)
endif
ifdef WOBBLE
	TAG := $(TAG)_wb
endif
ifdef PLANET
	TAG := $(TAG)_pl
endif
ifdef EARTH
	TAG := $(TAG)_ea
endif
ifdef HAZARDCHECK
	TAG := $(TAG)_haz
endif
ifdef CLRCHECK
	TAG := $(TAG)_clr
endif
ifdef LASER
	BUNK = 1
	TAG := $(TAG)_laser
else
ifdef BUNK
	TAG := $(TAG)_bk
endif
endif
program = out/$(PROG)$(TAG)
OUT = $(program)
CC = m68k-amiga-elf-gcc
AS = m68k-amiga-elf-as

ifdef WINDOWS
	SDKDIR = $(abspath $(dir $(shell where $(CC)))..\m68k-amiga-elf\sys-include)
else
	SDKDIR = $(abspath $(dir $(shell which $(CC)))../m68k-amiga-elf/sys-include)
endif

ifeq ($(PROG),flight320)
	c_sources = src/flight320_main.c src/font_antic.c src/planetgen.c src/palgen.c src/descent.c src/flight_core.c src/flight_data.c src/amiga_sys.c src/blitq.c support/gcc8_c_support.c
	asm_sources = src/flight_line320.asm src/flight_vtab.asm src/$(LINE).asm src/flight_edge.asm
else
ifneq ($(filter $(PROG),flight flight16),)
	c_sources = src/flight_main.c src/descent.c src/flight_core.c src/flight_data.c src/amiga_sys.c src/blitq.c support/gcc8_c_support.c
	asm_sources = src/$(LINE).asm src/flight_edge.asm src/flight_vtab.asm
else
ifeq ($(PROG),fselftest)
	c_sources = src/flight_selftest.c src/flight_core.c src/flight_data.c src/amiga_sys.c src/blitq.c support/gcc8_c_support.c
	asm_sources = src/$(LINE).asm src/flight_edge.asm src/flight_vtab.asm
else
$(error unknown PROG $(PROG))
endif
endif
endif
s_sources = support/gcc8_a_support.s
objdir = obj/$(PROG)$(TAG)
# the build folders (a fresh clone has none)
ifdef WINDOWS
$(shell if not exist $(subst /,\,$(objdir)) mkdir $(subst /,\,$(objdir)))
$(shell if not exist out mkdir out)
else
$(shell mkdir -p $(objdir) out)
endif
c_objects = $(addprefix $(objdir)/,$(patsubst %.c,%.o,$(notdir $(c_sources))))
s_objects = $(addprefix $(objdir)/,$(patsubst %.s,%.o,$(notdir $(s_sources))))
asm_objects = $(addprefix $(objdir)/,$(patsubst %.asm,%.o,$(notdir $(asm_sources))))
objects = $(c_objects) $(s_objects) $(asm_objects)

VPATH = src support
CCFLAGS   = -g -MP -MMD -m68000 -O2 -nostdlib -Wextra -Wno-unused-function -fomit-frame-pointer -fno-tree-loop-distribution -fno-exceptions -ffunction-sections -fdata-sections -DFLIGHT_AMIGA -Isrc -Isupport -I.
ifdef PROFILE
	CCFLAGS += -DPROFILE
endif
ifdef E9K
	CCFLAGS += -DE9K_PROF
endif
ifdef PUAE
	CCFLAGS += -DPUAE_FASTLOAD
endif
ifdef OLD
	CCFLAGS += -DOLD_OPT
endif
ifdef LERPCHECK
	CCFLAGS += -DLERPCHECK
endif
ifdef PROJCHECK
	CCFLAGS += -DPROJCHECK
endif
ifdef NODOTS
	CCFLAGS += -DNODOTS
endif
ifdef PIPE
	CCFLAGS += -DFORCE_PIPE=$(PIPE)
endif
ifdef WOBBLE
	CCFLAGS += -DSUN_WOBBLE=1
endif
ifdef PLANET
	CCFLAGS += -DSUN_PLANET=1
endif
ifdef EARTH
	CCFLAGS += -DSUN_EARTH=1
endif
ifdef HAZARDCHECK
	CCFLAGS += -DHAZARDCHECK
endif
ifdef CLRCHECK
	CCFLAGS += -DCLRCHECK
endif
ifdef BUNK
	CCFLAGS += -DBUNKERS -DFORCE_PIPE=0
endif
ifdef LASER
	CCFLAGS += -DLASER
endif
ifdef FASTRENDER
	CCFLAGS += -DFASTRENDER
endif
ifneq ($(filter $(PROG),flight flight16 fselftest),)
	CCFLAGS += -DPLOT_PLANAR -DFLIGHT_ASM
endif
ifeq ($(PROG),flight16)
	CCFLAGS += -DF_WIDELINE
endif
ifeq ($(PROG),flight320)
	CCFLAGS += -DFLIGHT320 -DF3_ASM -DFLIGHT_ASM -DPLOT_PLANAR
ifdef F3CHECK
	CCFLAGS += -DF3CHECK
endif
endif
VASM = vasmm68k_mot
VASMFLAGS = -m68000 -Felf -quiet -x -I.
ASFLAGS   = -mcpu=68000 -g --register-prefix-optional -I$(SDKDIR)
LDFLAGS   = -Wl,--emit-relocs,--gc-sections,-Ttext=0,-Map=$(OUT).map

all: $(OUT).exe

$(OUT).exe: $(OUT).elf
	$(info Elf2Hunk $(program).exe)
	@elf2hunk $(OUT).elf $(OUT).exe

$(OUT).elf: $(objects)
	$(info Linking $(program).elf)
	@$(CC) $(CCFLAGS) $(LDFLAGS) $(objects) -o $@
	@m68k-amiga-elf-objdump --disassemble --no-show-raw-ins --visualize-jumps -S $@ >$(OUT).s

clean:
	$(info Cleaning...)
ifdef WINDOWS
	@del /q obj\flight\* out\*
else
	@$(RM) obj/flight/* out/*
endif

-include $(objects:.o=.d)

$(c_objects) : $(objdir)/%.o : %.c
	$(info Compiling $<)
	@$(CC) $(CCFLAGS) -c -o $@ $<

$(s_objects): $(objdir)/%.o : %.s
	$(info Assembling $<)
	@$(AS) $(ASFLAGS) --MD $(@D)/$*.d -o $@ $<

$(asm_objects): $(objdir)/%.o : %.asm
	$(info Assembling $<)
	@$(VASM) $(VASMFLAGS) -o $@ $<
