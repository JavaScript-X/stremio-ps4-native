# OpenOrbis PS4 bring-up package. Run from this directory under Linux/WSL:
#
#   export OO_PS4_TOOLCHAIN=/opt/OpenOrbis/PS4Toolchain
#   make
#
# This builds the P1 bootstrap only. It is deliberately not named as a release
# and must not be distributed as a feature-complete Stremio client.

BUILD_MODE  ?= bootstrap
TITLE       := Stremio PS4 (Bring-up)
VERSION     := 0.03
TITLE_ID    := BREW00991
CONTENT_ID  := IV0000-BREW00991_00-STREMIOPS4000000
UI_ASSETS   := logo.png search.png \
	name.png cross.png circle.png triangle.png square.png options.png dpad.png l1.png r1.png play.png play_s.png pause.png check.png check_s.png chevron_right.png chevron_down.png logo_l.png \
	nav_board.png nav_board_o.png nav_discover.png nav_discover_o.png \
	nav_library.png nav_library_o.png nav_addons.png nav_addons_o.png \
	nav_settings.png nav_settings_o.png
UI_FONTS    := Inter-SemiBold.ttf

TOOLCHAIN   := $(OO_PS4_TOOLCHAIN)
ROOT        := $(abspath ..)
BUILD       := $(CURDIR)/build
OBJDIR      := $(BUILD)/obj
STAGE       := $(BUILD)/stage
DIST        := $(ROOT)/dist-ps4
SOURCE      := bootstrap/main.cpp
OBJECT      := $(OBJDIR)/main.o

LIBS        := -lc -lkernel -lc++ -lSceSysmodule \
	-lSceSystemService -lScePigletv2VSH \
	-lSceUserService -lScePad
EXTRAFLAGS  := -std=c++20 -O2 -fexceptions -fcxx-exceptions -DPLATFORM_PS4=1
AUTHINFO    := "000000000000000000000000001C004000FF000000000080000000000000000000000000000000000000008000400040000000000000008000000000000000080040FFFF000000F000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
FSELF_FLAGS := --paid 0x3800000000000035 --authinfo $(AUTHINFO)

ifeq ($(BUILD_MODE),probe)
TITLE       := Stremio PS4 (Loader Probe)
VERSION     := 0.05
SOURCE      := probe/main.c
OBJECT      := $(OBJDIR)/probe.o
LIBS        := -lc -lkernel -lSceSystemService
EXTRAFLAGS  := -O2 -DPLATFORM_PS4=1
FSELF_FLAGS := --paid 0x3800000000000011
endif

ifeq ($(BUILD_MODE),videoout)
TITLE       := Stremio PS4
VERSION     := 0.31
SOURCE      := videoout/main.c
OBJECT      := $(OBJDIR)/videoout.o
EXTRA_OBJECTS := $(OBJDIR)/protocol.o $(OBJDIR)/stremio.o $(OBJDIR)/util.o $(OBJDIR)/playback.o $(OBJDIR)/video_convert.o
LIBS        := -lc -lm -lkernel -lSceVideoOut -lSceSystemService -lScePad -lSceUserService \
	-lSceNet -lSceSsl -lSceHttp -lSceSysmodule -lSceAudioOut -lc++
EXTRAFLAGS  := -O2 -DPLATFORM_PS4=1
FSELF_FLAGS := --paid 0x3800000000000011
endif

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
	CC   := $(shell command -v clang 2>/dev/null || command -v clang-18 2>/dev/null)
	CCX  := $(shell command -v clang++ 2>/dev/null || command -v clang++-18 2>/dev/null)
	LD   := $(shell command -v ld.lld 2>/dev/null || command -v ld.lld-18 2>/dev/null)
HOST := linux
else ifeq ($(UNAME_S),Darwin)
	CC   := /usr/local/opt/llvm/bin/clang
CCX  := /usr/local/opt/llvm/bin/clang++
LD   := /usr/local/opt/llvm/bin/ld.lld
HOST := macos
else
$(error Build under Linux/WSL or macOS; Windows OpenOrbis packaging is not wired yet)
endif

TOOLS       := $(TOOLCHAIN)/bin/$(HOST)
CFLAGS      := --target=x86_64-pc-freebsd12-elf -fPIC -funwind-tables -c \
	$(EXTRAFLAGS) -isysroot $(TOOLCHAIN) -isystem $(TOOLCHAIN)/include -I$(ROOT) -I$(ROOT)/src
CXXFLAGS    := $(CFLAGS) -D_GNU_SOURCE -isystem $(TOOLCHAIN)/include/c++/v1
LDFLAGS     := -m elf_x86_64 -pie --gc-sections --script $(TOOLCHAIN)/link.x --eh-frame-hdr \
	-L$(TOOLCHAIN)/lib $(LIBS) $(TOOLCHAIN)/lib/crt1.o
PKG         := $(DIST)/$(CONTENT_ID).pkg

.PHONY: all check-toolchain clean
all: check-toolchain $(PKG)

check-toolchain:
	@test -n "$(TOOLCHAIN)" || (echo "OO_PS4_TOOLCHAIN is not set"; exit 1)
	@test -f "$(TOOLCHAIN)/link.x" || (echo "Invalid OpenOrbis toolchain: $(TOOLCHAIN)"; exit 1)

$(OBJDIR):
	mkdir -p $@

$(OBJECT): $(SOURCE) videoout/protocol.h videoout/playback.h | $(OBJDIR)
	$(if $(filter %.c,$<),$(CC),$(CCX)) $(if $(filter %.c,$<),$(CFLAGS),$(CXXFLAGS)) -o $@ $<

$(OBJDIR)/protocol.o: videoout/protocol.cpp videoout/protocol.h | $(OBJDIR)
	$(CCX) $(CXXFLAGS) -std=c++17 -fexceptions -ffunction-sections -fdata-sections -I$(ROOT)/third_party -o $@ $<

$(OBJDIR)/playback.o: videoout/playback.c videoout/playback.h videoout/avplayer_abi.h | $(OBJDIR)
	$(CC) $(CFLAGS) -o $@ $<

$(OBJDIR)/video_convert.o: videoout/video_convert.c videoout/playback.h | $(OBJDIR)
	$(CC) $(CFLAGS) -o $@ $<

$(OBJDIR)/stremio.o: $(ROOT)/src/stremio.cpp | $(OBJDIR)
	$(CCX) $(CXXFLAGS) -std=c++17 -fexceptions -ffunction-sections -fdata-sections -I$(ROOT)/third_party -o $@ $<

$(OBJDIR)/util.o: $(ROOT)/src/util.cpp | $(OBJDIR)
	$(CCX) $(CXXFLAGS) -std=c++17 -fexceptions -ffunction-sections -fdata-sections -I$(ROOT)/third_party -o $@ $<

$(BUILD)/bootstrap.elf: $(OBJECT) $(EXTRA_OBJECTS)
	$(LD) $^ -o $@ $(LDFLAGS)

$(BUILD)/eboot.bin: $(BUILD)/bootstrap.elf
	$(TOOLS)/create-fself -in=$< -out=$(BUILD)/bootstrap.oelf \
		--eboot "$@" $(FSELF_FLAGS)

$(STAGE)/sce_sys/param.sfo: Makefile
	mkdir -p $(STAGE)/sce_sys
	$(TOOLS)/PkgTool.Core sfo_new $@
	$(TOOLS)/PkgTool.Core sfo_setentry $@ APP_TYPE --type Integer --maxsize 4 --value 1
	$(TOOLS)/PkgTool.Core sfo_setentry $@ APP_VER --type Utf8 --maxsize 8 --value '$(VERSION)'
	$(TOOLS)/PkgTool.Core sfo_setentry $@ ATTRIBUTE --type Integer --maxsize 4 --value 0
	$(TOOLS)/PkgTool.Core sfo_setentry $@ CATEGORY --type Utf8 --maxsize 4 --value 'gde'
	$(TOOLS)/PkgTool.Core sfo_setentry $@ FORMAT --type Utf8 --maxsize 4 --value 'obs'
	$(TOOLS)/PkgTool.Core sfo_setentry $@ CONTENT_ID --type Utf8 --maxsize 48 --value '$(CONTENT_ID)'
	$(TOOLS)/PkgTool.Core sfo_setentry $@ DOWNLOAD_DATA_SIZE --type Integer --maxsize 4 --value 0
	$(TOOLS)/PkgTool.Core sfo_setentry $@ SYSTEM_VER --type Integer --maxsize 4 --value 1020
	$(TOOLS)/PkgTool.Core sfo_setentry $@ TITLE --type Utf8 --maxsize 128 --value '$(TITLE)'
	$(TOOLS)/PkgTool.Core sfo_setentry $@ TITLE_ID --type Utf8 --maxsize 12 --value '$(TITLE_ID)'
	$(TOOLS)/PkgTool.Core sfo_setentry $@ VERSION --type Utf8 --maxsize 8 --value '$(VERSION)'

$(STAGE)/.ready: $(BUILD)/eboot.bin $(STAGE)/sce_sys/param.sfo $(ROOT)/app/sce_sys/icon0.png
	mkdir -p $(STAGE)/sce_module $(STAGE)/sce_sys/about $(STAGE)/assets/fonts
	cp $(BUILD)/eboot.bin $(STAGE)/eboot.bin
	cp $(ROOT)/app/sce_sys/icon0.png $(STAGE)/sce_sys/icon0.png
	@if [ "$(BUILD_MODE)" = "videoout" ]; then for asset in $(UI_ASSETS); do cp $(ROOT)/app/assets/icons_4k/$$asset $(STAGE)/assets/; done; fi
	@if [ "$(BUILD_MODE)" = "videoout" ]; then for font in $(UI_FONTS); do cp $(ROOT)/app/fonts/$$font $(STAGE)/assets/fonts/; done; fi
	cp $(TOOLCHAIN)/samples/piglet/sce_module/libc.prx $(STAGE)/sce_module/libc.prx
	cp $(TOOLCHAIN)/samples/piglet/sce_module/libSceFios2.prx $(STAGE)/sce_module/libSceFios2.prx
	cp $(TOOLCHAIN)/samples/piglet/sce_sys/about/right.sprx $(STAGE)/sce_sys/about/right.sprx
	touch $@

$(PKG): $(STAGE)/.ready
	mkdir -p $(DIST)
	cd $(STAGE) && $(TOOLS)/create-gp4 -out pkg.gp4 --content-id=$(CONTENT_ID) \
		--files "eboot.bin sce_module/libc.prx sce_module/libSceFios2.prx sce_sys/about/right.sprx sce_sys/param.sfo sce_sys/icon0.png $(UI_ASSETS:%=assets/%) $(UI_FONTS:%=assets/fonts/%)"
	cd $(STAGE) && $(TOOLS)/PkgTool.Core pkg_build pkg.gp4 $(DIST)

clean:
	rm -rf $(BUILD) $(DIST)
