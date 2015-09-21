#Variables which can be pre-set (non-exhaustive list)

#ARCH: value for the switch: -march=
#GCC: path to g++
#debug: set to true for a debug build
#gprof: set to true for a gprof build
#san: set to address, thread, leak, undefined, etc. for a -fsanitize= enabled build
#list: set to true to enable listings
#map: set to true to enable linker map
#strip: set to true to strip binary (using STRIPFLAGS)
#cross: set to true if building on Unix, but build target is Windows
#V: set to true to show full command lines
#nopch: disable use of pre-compiled header
#noflto: disable use of link-time optimisation

#On Unixy platforms only
#WXCFGFLAGS: arguments for wx-config

#Note that to build on or for Windows, the include/lib search paths will need to be edited below and/or
#a number of libs/includes will need to be placed/built in a corresponding location where gcc can find them.


OBJS_SRC := $(patsubst src/%,%,$(filter-out src/version.cpp,$(wildcard src/*.cpp)) $(wildcard src/filter/*.cpp) $(wildcard src/emoji/*.cpp))
TCOBJS_SRC := $(patsubst src/%,%,$(wildcard src/libtwitcurl/*.cpp))
COBJS_SRC := utf8proc/utf8proc.c
OTHER_SRC := version.cpp
EXCOBJS_SRC :=
RTROBJS_SRC :=

OUTNAME := retcon
COMMONCFLAGS := -Wall -Wextra -Wshadow -Wno-unused-parameter -Ideps
MCFLAGS := -Wcast-qual
COBJS_CFLAGS := -Wno-missing-field-initializers -Wno-sign-compare
OPTIMISE_FLAGS := -O3
CFLAGS = $(OPTIMISE_FLAGS) $(COMMONCFLAGS)
AFLAGS :=
CXXFLAGS := -std=c++11 -fno-exceptions
TCFLAGS := -DSHA1_NO_UTILITY_FUNCTIONS -DSHA1_NO_STL_FUNCTIONS
WXCFGFLAGS := --version=2.8
GCC := g++
LD := ld
OBJDIR := objs
DIRS = $(OBJDIR) $(OBJDIR)/libtwitcurl $(OBJDIR)/res $(OBJDIR)/res/twemoji/16x16 $(OBJDIR)/res/twemoji/36x36 $(OBJDIR)/deps/utf8proc $(OBJDIR)/filter $(OBJDIR)/emoji $(OBJDIR)/pch

EXECPREFIX := ./
MKDIR := mkdir -p

VERSION_STRING := $(shell git describe --tags --always --dirty=-m 2>/dev/null)
ifdef VERSION_STRING
BVCFLAGS += -DRETCON_BUILD_VERSION='"${VERSION_STRING}"'
endif

OUTNAMEPOSTFIX :=
RESCLEAN :=

ifdef debug

OPTIMISE_FLAGS :=
CFLAGS += -g
AFLAGS += -g
DEBUGPOSTFIX := _debug
OBJDIR := $(OBJDIR)$(DEBUGPOSTFIX)
OUTNAMEPOSTFIX := $(OUTNAMEPOSTFIX)$(DEBUGPOSTFIX)
WXCFGFLAGS += --debug=yes

else ifndef noflto

CFLAGS += -flto
AFLAGS += -flto=jobserver $(OPTIMISE_FLAGS)

#Don't use flto jobserver in dry-run mode
ifneq (n, $(findstring n,$(filter-out --%,$(MFLAGS))))
LINK_PREFIX := +
endif

endif

ifdef gprof
CFLAGS += -pg
AFLAGS += -pg
GPROFPOSTFIX := _gprof
OBJDIR := $(OBJDIR)$(GPROFPOSTFIX)
OUTNAMEPOSTFIX := $(OUTNAMEPOSTFIX)$(GPROFPOSTFIX)
endif

ifdef san
ifeq ($(san), thread)
CFLAGS += -fPIE -pie
AFLAGS += -fPIE -pie -ltsan
endif
CFLAGS += -g -fsanitize=$(san) -fno-omit-frame-pointer
AFLAGS += -g -fsanitize=$(san)
SANPOSTFIX := _san_$(san)
OBJDIR := $(OBJDIR)$(SANPOSTFIX)
OUTNAMEPOSTFIX := $(OUTNAMEPOSTFIX)$(SANPOSTFIX)
endif

all:

GCCMACHINE := $(shell $(GCC) -dumpmachine)
ifeq (mingw, $(findstring mingw,$(GCCMACHINE)))
#WIN
PLATFORM := WIN
AFLAGS += -mwindows -s -static -Lwxlib -Llib
GFLAGS := -mthreads
CFLAGS += -D CURL_STATICLIB
SUFFIX := .exe
LIBS := -lpcre -lcurl -lwxmsw28u_richtext -lwxmsw28u_aui -lwxbase28u_xml -lwxexpat -lwxmsw28u_html -lwxmsw28u_adv -lwxmsw28u_media -lwxmsw28u_core -lwxbase28u -lwxjpeg -lwxpng -lwxtiff -lrtmp -lssh2 -lidn -lssl -lz -lcrypto -leay32 -lwldap32 -lws2_32 -lgdi32 -lshell32 -lole32 -luuid -lcomdlg32 -lwinspool -lcomctl32 -loleaut32 -lwinmm
MCFLAGS += -Icurl -isystem wxinclude -Isqlite -Izlib -Isrc -I.
TCFLAGS += -Icurl
HDEPS :=
EXCOBJS_SRC += sqlite/sqlite3.c
DIRS += $(OBJDIR)/deps/sqlite
ARCH := i686

RTROBJS_SRC += cacert.pem.zlib
DIRS += $(OBJDIR)/rtres
RESCLEAN += cacert.pem.zlib
cacert.pem.zlib: cacert.pem
	@echo '    deflate $<'
	$(call EXEC,zpipe < cacert.pem > cacert.pem.zlib)

ifndef cross
EXECPREFIX :=
DIRS := $(subst /,\\,$(DIRS))
MKDIR := mkdir
HOST := WIN
endif

else
#UNIX
PLATFORM := UNIX
LIBS := -lpcre -lrt `wx-config --libs std $(WXCFGFLAGS)` -lcurl -lsqlite3 -lz
MCFLAGS += $(patsubst -I/%,-isystem /%,$(shell wx-config --cxxflags $(WXCFGFLAGS)))

wxconf := $(shell wx-config --selected-config)
LIBLIST := libvlc
ifeq (gtk, $(findstring gtk,$(wxconf)))
LIBLIST += glib-2.0 gdk-2.0 gtk+-2.0
endif
LIBS += `pkg-config --libs $(LIBLIST)`
MCFLAGS += $(patsubst -I/%,-isystem /%,$(shell pkg-config --cflags $(LIBLIST))) -D USE_LIBVLC

endif

ifdef V
define EXEC
	$1
endef
else
ifeq ($HOST, WIN)
define EXEC
	@$1
endef
else
define EXEC
	@$1 ; rv=$$? ; [ $${rv} -ne 0 ] && echo 'Failed: $(subst ','"'"',$(1))' ; exit $${rv}
endef
endif
endif

OUTNAME := $(OUTNAME)$(OUTNAMEPOSTFIX)

GCCVER := $(shell $(GCC) -dumpversion)

ifeq (4.7.0, $(GCCVER))
$(error GCC 4.7.0 has a nasty bug in std::unordered_multimap, this will cause problems)
endif

ifneq (, $(filter 4.7.%, $(GCCVER)))
CXXFLAGS += -Wno-type-limits -Wno-uninitialized -Wno-maybe-uninitialized
#these cannot be feasibly suppressed at the local level in gcc 4.7
endif

GCC_MAJOR := $(shell $(GCC) -dumpversion | cut -d'.' -f1)
GCC_MINOR := $(shell $(GCC) -dumpversion | cut -d'.' -f2)

ifdef debug
OPTIMISE_FLAGS += $(shell [ $(GCC_MAJOR) -gt 4 -o \( $(GCC_MAJOR) -eq 4 -a $(GCC_MINOR) -ge 8 \) ] && echo -Og)
endif

CFLAGS += $(shell [ -t 0 ] && [ $(GCC_MAJOR) -gt 4 -o \( $(GCC_MAJOR) -eq 4 -a $(GCC_MINOR) -ge 9 \) ] && echo -fdiagnostics-color)

TARGS := $(OUTNAME)$(SUFFIX)

all: $(TARGS)

ifdef list
CFLAGS += -masm=intel -g --save-temps -Wa,-msyntax=intel,-aghlms=$*.lst
endif

ifdef map
AFLAGS += -Wl,-Map=$(OUTNAME).map
$(OUTNAME).map: $(OUTNAME)$(SUFFIX)
all: $(OUTNAME).map
endif

OBJS := $(patsubst src/%.cpp,$(OBJDIR)/%.o,$(addprefix src/,$(OBJS_SRC)))
TCOBJS := $(patsubst src/%.cpp,$(OBJDIR)/%.o,$(addprefix src/,$(TCOBJS_SRC)))
COBJS := $(patsubst deps/%.c,$(OBJDIR)/deps/%.o,$(addprefix deps/,$(COBJS_SRC)))
ROBJS := $(patsubst src/res/%.png,$(OBJDIR)/res/%.o,$(wildcard src/res/*.png) $(wildcard src/res/twemoji/16x16/*.png) $(wildcard src/res/twemoji/36x36/*.png))
RTROBJS := $(patsubst %,$(OBJDIR)/rtres/%.o,$(RTROBJS_SRC))
EXOBJS := $(patsubst %.c,$(OBJDIR)/deps/%.o,$(EXCOBJS_SRC))

ALL_OBJS := $(OBJS) $(TCOBJS) $(COBJS) $(SPOBJS) $(ROBJS) $(RTROBJS) $(EXOBJS)

ifneq ($(ARCH),)
CFLAGS2 += -march=$(ARCH)
endif

.SUFFIXES:

-include $(ALL_OBJS:.o=.d) $(OBJDIR)/pch/pch.h.d

MAKEDEPS = -MMD -MP -MT '$@ $(patsubst %.o,%.d,$(patsubst %.gch,%.d,$@))'

ifndef nopch
MPCFLAGS := -I $(OBJDIR)/pch -include pch.h -Winvalid-pch

#These cannot be reliably suppressed locally when using PCHs
MPCFLAGS += -Wno-strict-aliasing
COMMONCFLAGS := $(filter-out -Wshadow,$(COMMONCFLAGS))

$(OBJDIR)/pch/pch.h.gch: src/pch.h | $(DIRS)
	@echo '    g++ PCH $<'
	$(call EXEC,$(GCC) -c src/pch.h -o $(OBJDIR)/pch/pch.h.gch $(CFLAGS) $(MCFLAGS) $(CFLAGS2) $(CXXFLAGS) $(GFLAGS) $(MAKEDEPS))

$(OBJS): $(OBJDIR)/pch/pch.h.gch
endif

#This is to avoid unpleasant side-effects of over-writing executable in-place if it is currently running
$(TARGS): $(ALL_OBJS) src/version.cpp
	@echo '    g++     src/version.cpp'
	$(call EXEC,$(GCC) -c src/version.cpp -o $(OBJDIR)/version.o $(CFLAGS) $(MCFLAGS) $(CFLAGS2) $(CXXFLAGS) $(BVCFLAGS) $(GFLAGS) $(MAKEDEPS))
	@echo '    Link    $(OUTNAME)$(SUFFIX)'
ifeq "$(HOST)" "WIN"
	$(call EXEC,$(LINK_PREFIX)$(GCC) $(ALL_OBJS) $(OBJDIR)/version.o -o $(OUTNAME)$(SUFFIX) $(LIBS) $(AFLAGS) $(GFLAGS))
else
	$(call EXEC,$(LINK_PREFIX)$(GCC) $(ALL_OBJS) $(OBJDIR)/version.o -o $(OUTNAME)$(SUFFIX).tmp $(LIBS) $(AFLAGS) $(GFLAGS))
	$(call EXEC,rm -f $(OUTNAME)$(SUFFIX))
	$(call EXEC,mv $(OUTNAME)$(SUFFIX).tmp $(OUTNAME)$(SUFFIX))
endif
ifdef strip
	@echo '    Strip   $(OUTNAME)$(SUFFIX)'
	$(call EXEC,strip $(STRIPFLAGS) $(OUTNAME)$(SUFFIX))
endif

$(OBJS): $(OBJDIR)/%.o: src/%.cpp
	@echo '    g++     $<'
	$(call EXEC,$(GCC) -c $< -o $@ $(CFLAGS) $(MCFLAGS) $(MPCFLAGS) $(CFLAGS2) $(CXXFLAGS) $(GFLAGS) $(MAKEDEPS))

$(COBJS): $(OBJDIR)/deps/%.o: deps/%.c
	@echo '    gcc     $<'
	$(call EXEC,$(GCC:++=cc) -c $< -o $@ $(CFLAGS) $(CFLAGS2) $(COBJS_CFLAGS) $(GFLAGS) $(MAKEDEPS))

$(TCOBJS): $(OBJDIR)/%.o: src/%.cpp
	@echo '    g++     $<'
	$(call EXEC,$(GCC) -c $< -o $@ $(CFLAGS) $(TCFLAGS) $(CFLAGS2) $(CXXFLAGS) $(GFLAGS) $(MAKEDEPS))

ifeq "$(PLATFORM)" "WIN"
LINKRES = $(GCC) -Wl,-r -Wl,-b,binary $< -o $@ -nostdlib
else
LINKRES = $(LD) -r -b binary $< -o $@
endif
OBJCOPYRES = objcopy --rename-section .data=.rodata,alloc,load,readonly,data,contents $@ $@

$(ROBJS): $(OBJDIR)/%.o: src/%.png
$(RTROBJS): $(OBJDIR)/rtres/%.o: %

$(ROBJS) $(RTROBJS):
	$(call EXEC,$(LINKRES))
	$(call EXEC,$(OBJCOPYRES))

$(TARGS): $(OBJDIR)/resdone
RESCLEAN += $(OBJDIR)/resdone
$(OBJDIR)/resdone: $(ROBJS) $(RTROBJS)
	@echo '    Res     $(words $?) assets'
	@touch '$@'

$(EXOBJS): $(OBJDIR)/deps/%.o: %.c
	@echo '    gcc     $<'
	$(call EXEC,$(GCC:++=cc) -c $< -o $@ $(CFLAGS) $(CFLAGS2) $(GFLAGS) $(MAKEDEPS))

$(ALL_OBJS): | $(DIRS)

$(DIRS):
	-$(call EXEC,$(MKDIR) $@)

.PHONY: clean mostlyclean quickclean install uninstall all

quickclean:
	@echo '    Clean main objects, target'
	$(call EXEC,rm -f  $(OBJDIR)/pch/pch.h.d  $(OBJDIR)/pch/pch.h.gch $(OBJS) $(OBJS:.o=.ii) $(OBJS:.o=.lst) $(OBJS:.o=.s) $(OBJS:.o=.d) $(OUTNAME)$(SUFFIX) $(OUTNAME)$(SUFFIX).tmp)

mostlyclean: quickclean
	@echo '    Clean libtwitcurl objects'
	$(call EXEC,rm -f $(TCOBJS) $(TCOBJS:.o=.ii) $(TCOBJS:.o=.lst) $(TCOBJS:.o=.s) $(TCOBJS:.o=.d))
ifneq "$(SPOBJS)" ""
	@echo '    Clean "special" objects'
	$(call EXEC,rm -f $(SPOBJS) $(SPOBJS:.o=.ii) $(SPOBJS:.o=.lst) $(SPOBJS:.o=.s) $(SPOBJS:.o=.d))
endif

clean: mostlyclean
	@echo '    Clean utf8proc objects'
	$(call EXEC,rm -f $(COBJS) $(COBJS:.o=.ii) $(COBJS:.o=.lst) $(COBJS:.o=.s) $(COBJS:.o=.d))
	@echo '    Clean res objects'
	$(call EXEC,rm -f $(ROBJS) $(ROBJS:.o=.d) $(RTROBJS) $(RTROBJS:.o=.d) $(RESCLEAN))

install:
ifeq "$(PLATFORM)" "WIN"
	@echo Install only supported on Unixy platforms
else
	@echo '    Install to /usr/local/bin/$(OUTNAME)$(SUFFIX)'
	$(call EXEC,cp --remove-destination $(OUTNAME)$(SUFFIX) /usr/local/bin/$(OUTNAME)$(SUFFIX))
endif

uninstall:
ifeq "$(PLATFORM)" "WIN"
	@echo Uninstall only supported on Unixy platforms
else
	@echo '    Delete from /usr/local/bin/$(OUTNAME)$(SUFFIX)'
	$(call EXEC,rm /usr/local/bin/$(OUTNAME)$(SUFFIX))
endif
