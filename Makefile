#!/usr/bin/make -f

# these can be overridden using make variables. e.g.
#   make CFLAGS=-O2
#   make install DESTDIR=$(CURDIR)/debian/avldrums.lv2 PREFIX=/usr
#
PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
MANDIR ?= $(PREFIX)/share/man/man1
# see http://lv2plug.in/pages/filesystem-hierarchy-standard.html, don't use libdir
LV2DIR ?= $(PREFIX)/lib/lv2
PKG_CONFIG ?= pkg-config

CFLAGS ?= -Wall -g -Wno-unused-function
STRIP  ?= strip

BUILDOPENGL?=yes

avldrums_VERSION?=$(shell git describe --tags HEAD 2>/dev/null | sed 's/-g.*$$//;s/^v//' || echo "LV2")
RW ?= robtk/

###############################################################################

MACHINE=$(shell uname -m)
ifneq (,$(findstring x64,$(MACHINE)))
  HAVE_SSE=yes
endif
ifneq (,$(findstring 86,$(MACHINE)))
  HAVE_SSE=yes
endif

ifeq ($(HAVE_SSE),yes)
  OPTIMIZATIONS ?= -msse -msse2 -mfpmath=sse -ffast-math -fomit-frame-pointer -O3 -fno-finite-math-only -DNDEBUG
else
  OPTIMIZATIONS ?= -fomit-frame-pointer -O3 -fno-finite-math-only -DNDEBUG
endif

###############################################################################

BUILDDIR = build/
APPBLD   = x42/

###############################################################################

LV2NAME=avldrums
LV2GUI=avldrumsUI_gl
BUNDLE=avldrums.lv2
targets=

LOADLIBES=-lm
LV2UIREQ=
GLUICFLAGS=-I.

UNAME=$(shell uname)
ifeq ($(UNAME),Darwin)
  LV2LDFLAGS=-dynamiclib
  LIB_EXT=.dylib
  EXE_EXT=
  UI_TYPE=ui:CocoaUI
  PUGL_SRC=$(RW)pugl/pugl_osx.mm
  PKG_GL_LIBS=
  GLUILIBS=-framework Cocoa -framework OpenGL -framework CoreFoundation
  STRIPFLAGS=-u -r -arch all -s $(RW)lv2syms
  EXTENDED_RE=-E
else
  LV2LDFLAGS=-Wl,-Bstatic -Wl,-Bdynamic -Wl,--as-needed -pthread
  LIB_EXT=.so
  EXE_EXT=
  UI_TYPE=ui:X11UI
  PUGL_SRC=$(RW)pugl/pugl_x11.c
  PKG_GL_LIBS=glu gl
  GLUILIBS=-lX11
  GLUICFLAGS+=`$(PKG_CONFIG) --cflags glu`
  STRIPFLAGS= -s
  EXTENDED_RE=-r
endif

ifneq ($(XWIN),)
  CC=$(XWIN)-gcc
  CXX=$(XWIN)-g++
  STRIP=$(XWIN)-strip
  LV2LDFLAGS=-Wl,-Bstatic -Wl,-Bdynamic -Wl,--as-needed -pthread
  LIB_EXT=.dll
  EXE_EXT=.exe
  PUGL_SRC=$(RW)pugl/pugl_win.cpp
  PKG_GL_LIBS=
  UI_TYPE=ui:WindowsUI
  GLUILIBS=-lws2_32 -lwinmm -lopengl32 -lglu32 -lgdi32 -lcomdlg32 -lpthread
  GLUICFLAGS=-I.
  override LDFLAGS += -static-libgcc -static-libstdc++
endif

ifeq ($(EXTERNALUI), yes)
  UI_TYPE=
endif

ifeq ($(UI_TYPE),)
  UI_TYPE=kx:Widget
  LV2UIREQ+=lv2:requiredFeature kx:Widget;
  override CFLAGS += -DXTERNAL_UI
endif

targets+=$(BUILDDIR)$(LV2NAME)$(LIB_EXT)

targets+=$(BUILDDIR)Red_Zeppelin_4_LV2.sf2
targets+=$(BUILDDIR)Black_Pearl_4_LV2.sf2
targets+=$(BUILDDIR)Blonde_Bop_LV2.sf2
targets+=$(BUILDDIR)Blonde_Bop_HR_LV2.sf2
targets+=$(BUILDDIR)Buskmans_Holiday_LV2.sf2

UITTL=
ifneq ($(BUILDOPENGL), no)
  targets+=$(BUILDDIR)$(LV2GUI)$(LIB_EXT)
  UITTL=ui:ui $(LV2NAME):ui_gl ;
endif

###############################################################################
# extract versions
LV2VERSION=$(avldrums_VERSION)
include git2lv2.mk

###############################################################################
# check for build-dependencies
ifeq ($(shell $(PKG_CONFIG) --exists lv2 || echo no), no)
  $(error "LV2 SDK was not found")
endif

ifeq ($(shell $(PKG_CONFIG) --atleast-version=1.6.0 lv2 || echo no), no)
  $(error "LV2 SDK needs to be version 1.6.0 or later")
endif

ifeq ($(shell $(PKG_CONFIG) --exists glib-2.0 || echo no), no)
  $(error "glib-2.0 was not found.")
endif

ifneq ($(BUILDOPENGL), no)
 ifeq ($(shell $(PKG_CONFIG) --exists pango cairo $(PKG_GL_LIBS) || echo no), no)
  $(error "This plugin requires cairo pango $(PKG_GL_LIBS)")
 endif
endif

# check for lv2_atom_forge_object  new in 1.8.1 deprecates lv2_atom_forge_blank
ifeq ($(shell $(PKG_CONFIG) --atleast-version=1.8.1 lv2 && echo yes), yes)
  override CFLAGS += -DHAVE_LV2_1_8
endif

ifeq ($(shell $(PKG_CONFIG) --atleast-version=1.18.6 lv2 && echo yes), yes)
  override CFLAGS += -DHAVE_LV2_1_18_6
endif

ifneq ($(BUILDOPENGL), no)
 ifneq ($(MAKECMDGOALS), submodules)
  ifeq ($(wildcard $(RW)robtk.mk),)
    $(warning "**********************************************************")
    $(warning This plugin needs https://github.com/x42/robtk)
    $(warning "**********************************************************")
    $(info )
    $(info set the RW environment variale to the location of the robtk headers)
    ifeq ($(wildcard .git),.git)
      $(info or run 'make submodules' to initialize robtk as git submodule)
    endif
    $(info )
    $(warning "**********************************************************")
    $(error robtk not found)
  endif
 endif
endif

# LV2 idle >= lv2-1.6.0
GLUICFLAGS+=-DHAVE_IDLE_IFACE
LV2UIREQ+=lv2:requiredFeature ui:idleInterface; lv2:extensionData ui:idleInterface;

# add library dependent flags and libs
override CFLAGS += $(OPTIMIZATIONS) -DVERSION="\"$(avldrums_VERSION)\""
override CFLAGS += `$(PKG_CONFIG) --cflags lv2 glib-2.0`
ifeq ($(XWIN),)
override CFLAGS += -fPIC -fvisibility=hidden
else
override CFLAGS += -DPTW32_STATIC_LIB
endif
override LOADLIBES += `$(PKG_CONFIG) $(PKG_UI_FLAGS) --libs glib-2.0`


GLUICFLAGS+=`$(PKG_CONFIG) --cflags cairo pango` $(CFLAGS)
GLUILIBS+=`$(PKG_CONFIG) $(PKG_UI_FLAGS) --libs cairo pango pangocairo $(PKG_GL_LIBS)`

ifneq ($(XWIN),)
GLUILIBS+=-lpthread -lusp10
endif

GLUICFLAGS+=$(LIC_CFLAGS)
GLUILIBS+=$(LIC_LOADLIBES)


ifneq ($(LIC_CFLAGS),)
  LV2SIGN=lv2:extensionData <http:\\/\\/harrisonconsoles.com\\/lv2\\/license\#interface>\\;
  override CFLAGS += -I$(RW)
endif

ROBGL+= Makefile

###############################################################################
# build target definitions
default: all

submodule_pull:
	-test -d .git -a .gitmodules -a -f Makefile.git && $(MAKE) -f Makefile.git submodule_pull

submodule_update:
	-test -d .git -a .gitmodules -a -f Makefile.git && $(MAKE) -f Makefile.git submodule_update

submodule_check:
	-test -d .git -a .gitmodules -a -f Makefile.git && $(MAKE) -f Makefile.git submodule_check

submodules:
	-test -d .git -a .gitmodules -a -f Makefile.git && $(MAKE) -f Makefile.git submodules

all: submodule_check $(BUILDDIR)manifest.ttl $(BUILDDIR)$(LV2NAME).ttl $(targets)

$(BUILDDIR)manifest.ttl: lv2ttl/manifest.ttl.in lv2ttl/manifest.gui.in Makefile
	@mkdir -p $(BUILDDIR)
	sed "s/@LV2NAME@/$(LV2NAME)/g;s/@LIB_EXT@/$(LIB_EXT)/" \
		lv2ttl/manifest.ttl.in > $(BUILDDIR)manifest.ttl
ifneq ($(BUILDOPENGL), no)
	sed "s/@LV2NAME@/$(LV2NAME)/g;s/@LIB_EXT@/$(LIB_EXT)/;s/@UI_TYPE@/$(UI_TYPE)/;s/@LV2GUI@/$(LV2GUI)/g" \
		lv2ttl/manifest.gui.in >> $(BUILDDIR)manifest.ttl
endif

$(BUILDDIR)$(LV2NAME).ttl: Makefile lv2ttl/$(LV2NAME).*.in
	@mkdir -p $(BUILDDIR)
	sed "s/@LV2NAME@/$(LV2NAME)/g;" \
		lv2ttl/$(LV2NAME).head.ttl.in > $(BUILDDIR)$(LV2NAME).ttl
	sed "s/@LV2NAME@/$(LV2NAME)/g;s/@NAME@/Black Pearl Drumkit/g;s/@VARIANT@/BlackPearl/g;s/@SIGNATURE@/$(LV2SIGN)/;s/@VERSION@/lv2:microVersion $(LV2MIC) ;lv2:minorVersion $(LV2MIN) ;/g;s/@UITTL@/$(UITTL)/" \
		lv2ttl/$(LV2NAME).ttl.in >> $(BUILDDIR)$(LV2NAME).ttl
	cat \
		lv2ttl/$(LV2NAME).stereo.ttl.in >> $(BUILDDIR)$(LV2NAME).ttl
	sed "s/@LV2NAME@/$(LV2NAME)/g;s/@NAME@/Red Zeppelin Drumkit/g;s/@VARIANT@/RedZeppelin/g;s/@SIGNATURE@/$(LV2SIGN)/;s/@VERSION@/lv2:microVersion $(LV2MIC) ;lv2:minorVersion $(LV2MIN) ;/g;s/@UITTL@/$(UITTL)/" \
		lv2ttl/$(LV2NAME).ttl.in >> $(BUILDDIR)$(LV2NAME).ttl
	cat \
		lv2ttl/$(LV2NAME).stereo.ttl.in >> $(BUILDDIR)$(LV2NAME).ttl
	sed "s/@LV2NAME@/$(LV2NAME)/g;s/@NAME@/Blonde Bop Drumkit/g;s/@VARIANT@/BlondeBop/g;s/@SIGNATURE@/$(LV2SIGN)/;s/@VERSION@/lv2:microVersion $(LV2MIC) ;lv2:minorVersion $(LV2MIN) ;/g;s/@UITTL@/$(UITTL)/" \
		lv2ttl/$(LV2NAME).ttl.in >> $(BUILDDIR)$(LV2NAME).ttl
	cat \
		lv2ttl/$(LV2NAME).stereo.ttl.in >> $(BUILDDIR)$(LV2NAME).ttl
	sed "s/@LV2NAME@/$(LV2NAME)/g;s/@NAME@/Blonde Bop HotRod Drumkit/g;s/@VARIANT@/BlondeBopHR/g;s/@SIGNATURE@/$(LV2SIGN)/;s/@VERSION@/lv2:microVersion $(LV2MIC) ;lv2:minorVersion $(LV2MIN) ;/g;s/@UITTL@/$(UITTL)/" \
		lv2ttl/$(LV2NAME).ttl.in >> $(BUILDDIR)$(LV2NAME).ttl
	cat \
		lv2ttl/$(LV2NAME).stereo.ttl.in >> $(BUILDDIR)$(LV2NAME).ttl
	sed "s/@LV2NAME@/$(LV2NAME)/g;s/@NAME@/Black Pearl Drumkit Multi/g;s/@VARIANT@/BlackPearlMulti/g;s/@SIGNATURE@/$(LV2SIGN)/;s/@VERSION@/lv2:microVersion $(LV2MIC) ;lv2:minorVersion $(LV2MIN) ;/g;s/@UITTL@/$(UITTL)/" \
		lv2ttl/$(LV2NAME).ttl.in >> $(BUILDDIR)$(LV2NAME).ttl
	sed "s/@LV2NAME@/$(LV2NAME)/g" \
		lv2ttl/$(LV2NAME).multi.ttl.in >> $(BUILDDIR)$(LV2NAME).ttl
	sed "s/@LV2NAME@/$(LV2NAME)/g;s/@NAME@/Red Zeppelin Drumkit Multi/g;s/@VARIANT@/RedZeppelinMulti/g;s/@SIGNATURE@/$(LV2SIGN)/;s/@VERSION@/lv2:microVersion $(LV2MIC) ;lv2:minorVersion $(LV2MIN) ;/g;s/@UITTL@/$(UITTL)/" \
		lv2ttl/$(LV2NAME).ttl.in >> $(BUILDDIR)$(LV2NAME).ttl
	sed "s/@LV2NAME@/$(LV2NAME)/g" \
		lv2ttl/$(LV2NAME).multi.ttl.in >> $(BUILDDIR)$(LV2NAME).ttl
	sed "s/@LV2NAME@/$(LV2NAME)/g;s/@NAME@/Blonde Bop Drumkit Multi/g;s/@VARIANT@/BlondeBopMulti/g;s/@SIGNATURE@/$(LV2SIGN)/;s/@VERSION@/lv2:microVersion $(LV2MIC) ;lv2:minorVersion $(LV2MIN) ;/g;s/@UITTL@/$(UITTL)/" \
		lv2ttl/$(LV2NAME).ttl.in >> $(BUILDDIR)$(LV2NAME).ttl
	sed "s/@LV2NAME@/$(LV2NAME)/g" \
		lv2ttl/$(LV2NAME).multi.ttl.in >> $(BUILDDIR)$(LV2NAME).ttl
	sed "s/@LV2NAME@/$(LV2NAME)/g;s/@NAME@/Blonde Bop HotRod Drumkit Multi/g;s/@VARIANT@/BlondeBopHRMulti/g;s/@SIGNATURE@/$(LV2SIGN)/;s/@VERSION@/lv2:microVersion $(LV2MIC) ;lv2:minorVersion $(LV2MIN) ;/g;s/@UITTL@/$(UITTL)/" \
		lv2ttl/$(LV2NAME).ttl.in >> $(BUILDDIR)$(LV2NAME).ttl
	sed "s/@LV2NAME@/$(LV2NAME)/g" \
		lv2ttl/$(LV2NAME).multi.ttl.in >> $(BUILDDIR)$(LV2NAME).ttl
	sed "s/@LV2NAME@/$(LV2NAME)/g;s/@NAME@/Buskman's Holiday Percussion/g;s/@VARIANT@/BuskmansHoliday/g;s/@SIGNATURE@/$(LV2SIGN)/;s/@VERSION@/lv2:microVersion $(LV2MIC) ;lv2:minorVersion $(LV2MIN) ;/g;s/@UITTL@/$(UITTL)/" \
		lv2ttl/$(LV2NAME).ttl.in >> $(BUILDDIR)$(LV2NAME).ttl
	cat \
		lv2ttl/$(LV2NAME).stereo.ttl.in >> $(BUILDDIR)$(LV2NAME).ttl
ifneq ($(BUILDOPENGL), no)
	sed "s/@LV2NAME@/$(LV2NAME)/g;s/@UI_TYPE@/$(UI_TYPE)/;s/@UI_REQ@/$(LV2UIREQ)/" \
	    lv2ttl/$(LV2NAME).gui.in >> $(BUILDDIR)$(LV2NAME).ttl
endif

$(BUILDDIR)%.sf2: sf2/%.sf2
	@mkdir -p $(BUILDDIR)
	cp -v sf2/$(*F).sf2 $@

FLUID_SRC = \
            fluidsynth/src/fluid_adsr_env.c \
            fluidsynth/src/fluid_chan.c \
            fluidsynth/src/fluid_chorus.c \
            fluidsynth/src/fluid_conv.c \
            fluidsynth/src/fluid_defsfont.c \
            fluidsynth/src/fluid_event.c \
            fluidsynth/src/fluid_gen.c \
            fluidsynth/src/fluid_hash.c \
            fluidsynth/src/fluid_iir_filter.c \
            fluidsynth/src/fluid_lfo.c \
            fluidsynth/src/fluid_list.c \
            fluidsynth/src/fluid_midi.c \
            fluidsynth/src/fluid_mod.c \
            fluidsynth/src/fluid_rev.c \
            fluidsynth/src/fluid_ringbuffer.c \
            fluidsynth/src/fluid_rvoice.c \
            fluidsynth/src/fluid_rvoice_dsp.c \
            fluidsynth/src/fluid_rvoice_event.c \
            fluidsynth/src/fluid_rvoice_mixer.c \
            fluidsynth/src/fluid_samplecache.c \
            fluidsynth/src/fluid_settings.c \
            fluidsynth/src/fluid_sffile.c \
            fluidsynth/src/fluid_sfont.c \
            fluidsynth/src/fluid_synth.c \
            fluidsynth/src/fluid_synth_monopoly.c \
            fluidsynth/src/fluid_sys.c \
            fluidsynth/src/fluid_tuning.c \
            fluidsynth/src/fluid_voice.c

CPPFLAGS += -Ifluidsynth -I fluidsynth/fluidsynth -DHAVE_CONFIG_H -D DEFAULT_SOUNDFONT=\"\"
DSP_SRC  = src/$(LV2NAME).c $(FLUID_SRC)
DSP_DEPS = $(DSP_SRC) src/$(LV2NAME).h
GUI_DEPS = gui/$(LV2NAME).c src/$(LV2NAME).h

$(BUILDDIR)$(LV2NAME)$(LIB_EXT): $(DSP_DEPS) Makefile
	@mkdir -p $(BUILDDIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LIC_CFLAGS) -std=gnu99 \
	  -o $(BUILDDIR)$(LV2NAME)$(LIB_EXT) $(DSP_SRC) \
	  -shared $(LV2LDFLAGS) $(LDFLAGS) $(LOADLIBES) $(LIC_LOADLIBES)
	$(STRIP) $(STRIPFLAGS) $(BUILDDIR)$(LV2NAME)$(LIB_EXT)

ifneq ($(BUILDOPENGL), no)
 -include $(RW)robtk.mk
endif

$(BUILDDIR)$(LV2GUI)$(LIB_EXT): gui/$(LV2NAME).c

###############################################################################
# install/uninstall/clean target definitions

install: install-bin

uninstall: uninstall-bin

install-bin: all
	install -d $(DESTDIR)$(LV2DIR)/$(BUNDLE)
	install -m644 $(BUILDDIR)manifest.ttl $(BUILDDIR)$(LV2NAME).ttl $(BUILDDIR)*.sf2 $(DESTDIR)$(LV2DIR)/$(BUNDLE)
	install -m755 $(BUILDDIR)$(LV2NAME)$(LIB_EXT) $(DESTDIR)$(LV2DIR)/$(BUNDLE)
ifneq ($(BUILDOPENGL), no)
	install -m755 $(BUILDDIR)$(LV2GUI)$(LIB_EXT) $(DESTDIR)$(LV2DIR)/$(BUNDLE)
endif

uninstall-bin:
	rm -f $(DESTDIR)$(LV2DIR)/$(BUNDLE)/manifest.ttl
	rm -f $(DESTDIR)$(LV2DIR)/$(BUNDLE)/$(LV2NAME).ttl
	rm -f $(DESTDIR)$(LV2DIR)/$(BUNDLE)/*.sf2
	rm -f $(DESTDIR)$(LV2DIR)/$(BUNDLE)/$(LV2NAME)$(LIB_EXT)
	rm -f $(DESTDIR)$(LV2DIR)/$(BUNDLE)/$(LV2GUI)$(LIB_EXT)
	-rmdir $(DESTDIR)$(LV2DIR)/$(BUNDLE)

install-man:

uninstall-man:

clean:
	rm -f $(BUILDDIR)manifest.ttl $(BUILDDIR)$(LV2NAME).ttl \
	  $(BUILDDIR)$(LV2NAME)$(LIB_EXT) \
	  $(BUILDDIR)$(LV2GUI)$(LIB_EXT) \
	  $(BUILDDIR)*.sf2
	rm -rf $(BUILDDIR)*.dSYM
	rm -rf $(APPBLD)x42-*
	-test -d $(APPBLD) && rmdir $(APPBLD) || true
	-test -d $(BUILDDIR) && rmdir $(BUILDDIR) || true

distclean: clean
	rm -f cscope.out cscope.files tags

.PHONY: clean all install uninstall distclean \
        install-bin uninstall-bin \
        submodule_check submodules submodule_update submodule_pull
