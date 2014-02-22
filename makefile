#Variables which can be pre-set (non-exhaustive list)

#ARCH: value for the switch: -march=
#GCC: path to g++
#debug: set to true for a debug build
#gprof: set to true for a gprof build
#list: set to true to enable listings
#map: set to true to enable linker map
#strip: set to true to strip binary (using STRIPFLAGS)
#cross: set to true if building on Unix, but build target is Windows
#V: set to true to show full command lines
#nopch: disable use of pre-compiled header

#On Unixy platforms only
#WXCFGFLAGS: additional arguments for wx-config

#On windows only:
#x64: set to true to compile for x86_64/win64

#Note that to build on or for Windows, the include/lib search paths will need to be edited below and/or
#a number of libs/includes will need to be placed/built in a corresponding location where gcc can find them.


OBJS_SRC := retcon.cpp cfg.cpp optui.cpp parse.cpp socket.cpp socket-ops.cpp tpanel.cpp tpanel-data.cpp tpanel-aux.cpp twit.cpp db.cpp log.cpp cmdline.cpp userui.cpp mainui.cpp signal.cpp threadutil.cpp
OBJS_SRC += dispscr.cpp uiutil.cpp mediawin.cpp taccount.cpp util.cpp res.cpp version.cpp aboutwin.cpp twitcurlext.cpp filter/filter.cpp filter/filter-dlg.cpp bind_wxevt.cpp
TCOBJS_SRC:=libtwitcurl/base64.cpp libtwitcurl/HMAC_SHA1.cpp libtwitcurl/oauthlib.cpp libtwitcurl/SHA1.cpp libtwitcurl/twitcurl.cpp libtwitcurl/urlencode.cpp
COBJS_SRC:=utf8proc/utf8proc.c
OUTNAME:=retcon
COMMONCFLAGS=-Wall -Wextra -Wshadow -Wno-unused-parameter -Ideps
COBJS_CFLAGS=-Wno-missing-field-initializers -Wno-sign-compare
CFLAGS=-g -O3 $(COMMONCFLAGS)
AFLAGS=-g
CXXFLAGS=-std=gnu++0x -fno-exceptions
TCFLAGS=-DSHA1_NO_UTILITY_FUNCTIONS
GCC:=g++
LD:=ld
OBJDIR:=objs
DIRS=$(OBJDIR) $(OBJDIR)$(PATHSEP)libtwitcurl $(OBJDIR)$(PATHSEP)res $(OBJDIR)$(PATHSEP)deps$(PATHSEP)utf8proc $(OBJDIR)$(PATHSEP)filter $(OBJDIR)$(PATHSEP)pch

EXECPREFIX:=./
PATHSEP:=/
MKDIR:=mkdir -p

ifdef debug
CFLAGS=-g $(COMMONCFLAGS)
AFLAGS=-g
#AFLAGS:=-Wl,-d,--export-all-symbols
DEBUGPOSTFIX:=_debug
OBJDIR:=$(OBJDIR)$(DEBUGPOSTFIX)
WXCFGFLAGS:=--debug=yes
endif

ifdef gprof
CFLAGS+=-pg
AFLAGS+=-pg
GPROFPOSTFIX:=_gprof
OBJDIR:=$(OBJDIR)$(GPROFPOSTFIX)
endif

GCCMACHINE:=$(shell $(GCC) -dumpmachine)
ifeq (mingw, $(findstring mingw,$(GCCMACHINE)))
#WIN
PLATFORM:=WIN
AFLAGS+=-mwindows -s -static -Lwxlib -Llib
GFLAGS=-mthreads
CFLAGS+=-D CURL_STATICLIB
SUFFIX:=.exe
LIBS32=-lpcre -lcurl -lwxmsw28u_richtext -lwxmsw28u_aui -lwxbase28u_xml -lwxexpat -lwxmsw28u_html -lwxmsw28u_adv -lwxmsw28u_media -lwxmsw28u_core -lwxbase28u -lwxjpeg -lwxpng -lwxtiff -lrtmp -lssh2 -lidn -lssl -lz -lcrypto -leay32 -lwldap32 -lws2_32 -lgdi32 -lshell32 -lole32 -luuid -lcomdlg32 -lwinspool -lcomctl32 -loleaut32 -lwinmm
LIBS64=
GCC32=i686-w64-mingw32-g++
GCC64=x86_64-w64-mingw32-g++
MCFLAGS=-Icurl -isystem wxinclude -Isqlite -Izlib -Isrc -I.
TCFLAGS+=-Icurl
HDEPS:=
EXCOBJS_SRC+=sqlite/sqlite3.c
DIRS+=$(OBJDIR)$(PATHSEP)deps$(PATHSEP)sqlite

ifndef cross
EXECPREFIX:=
PATHSEP:=\\
MKDIR:=mkdir
HOST:=WIN
endif

ifdef x64
SIZEPOSTFIX:=64
OBJDIR:=$(OBJDIR)$(SIZEPOSTFIX)
GCC:=$(GCC64)
LIBS:=$(LIBS64)
CFLAGS2:=-mcx16
PACKER:=mpress -s
else
GCC:=$(GCC32)
LIBS:=$(LIBS32)
ARCH:=i686
PACKER:=upx -9
endif

else
#UNIX
PLATFORM:=UNIX
LIBS:=-lpcre -lrt `wx-config --libs $(WXCFGFLAGS)` -lcurl -lsqlite3 -lz -lcrypto
MCFLAGS:=$(patsubst -I/%,-isystem /%,$(shell wx-config --cxxflags $(WXCFGFLAGS)))
PACKER:=upx -9
GCC_MAJOR:=$(shell $(GCC) -dumpversion | cut -d'.' -f1)
GCC_MINOR:=$(shell $(GCC) -dumpversion | cut -d'.' -f2)
ARCH:=$(shell test $(GCC_MAJOR) -gt 4 -o \( $(GCC_MAJOR) -eq 4 -a $(GCC_MINOR) -ge 2 \) && echo native)

wxconf:=$(shell wx-config --selected-config)
ifeq (gtk, $(findstring gtk,$(wxconf)))
LIBS+=`pkg-config --libs glib-2.0` `pkg-config --libs gdk-2.0`
MCFLAGS+=`pkg-config --cflags glib-2.0` `pkg-config --cflags gdk-2.0`
endif

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

OUTNAME:=$(OUTNAME)$(SIZEPOSTFIX)$(DEBUGPOSTFIX)$(GPROFPOSTFIX)

GCCVER:=$(shell $(GCC) -dumpversion)

ifeq (4.7.0, $(GCCVER))
$(error GCC 4.7.0 has a nasty bug in std::unordered_multimap, this will cause problems)
endif

ifeq (4.7.1, $(GCCVER))
CXXFLAGS+=-Wno-type-limits -Wno-uninitialized -Wno-maybe-uninitialized
#these cannot be feasibly suppressed at the local level in gcc 4.7
endif

ifdef debug
ifeq (, $(filter 4.7.%, $(GCCVER)))
CFLAGS+=-Og
endif
endif

TARGS:=$(OUTNAME)$(SUFFIX)

all: $(TARGS)

ifdef list
CFLAGS+= -masm=intel -g --save-temps -Wa,-msyntax=intel,-aghlms=$*.lst
endif

ifdef map
AFLAGS+=-Wl,-Map=$(OUTNAME).map
$(OUTNAME).map: $(OUTNAME)$(SUFFIX)
all: $(OUTNAME).map
endif

OBJS:=$(patsubst src/%.cpp,$(OBJDIR)/%.o,$(addprefix src/,$(OBJS_SRC)))
TCOBJS:=$(patsubst src/%.cpp,$(OBJDIR)/%.o,$(addprefix src/,$(TCOBJS_SRC)))
COBJS:=$(patsubst deps/%.c,$(OBJDIR)/deps/%.o,$(addprefix deps/,$(COBJS_SRC)))
ROBJS:=$(patsubst src/res/%.png,$(OBJDIR)/res/%.o,$(wildcard src/res/*.png))
EXOBJS:=$(patsubst %.c,$(OBJDIR)/deps/%.o,$(EXCOBJS_SRC))

ALL_OBJS:=$(OBJS) $(TCOBJS) $(COBJS) $(SPOBJS) $(ROBJS) $(EXOBJS)

ifneq ($(ARCH),)
CFLAGS2 += -march=$(ARCH)
endif

.SUFFIXES:

-include $(ALL_OBJS:.o=.d) $(OBJDIR)/pch/pch.h.d

MAKEDEPS = -MMD -MP -MT '$@ $(patsubst %.o,%.d,$(patsubst %.gch,%.d,$@))'

ifndef nopch
MPCFLAGS:=-I $(OBJDIR)/pch -include pch.h -Winvalid-pch

#These cannot be reliably suppressed locally when using PCHs
MPCFLAGS += -Wno-strict-aliasing
COMMONCFLAGS:=$(filter-out -Wshadow,$(COMMONCFLAGS))

$(OBJDIR)/pch/pch.h.gch: src/pch.h | $(DIRS)
	@echo '    g++ PCH $<'
	$(call EXEC,$(GCC) -c src/pch.h -o $(OBJDIR)/pch/pch.h.gch $(CFLAGS) $(MCFLAGS) $(CFLAGS2) $(CXXFLAGS) $(GFLAGS) $(MAKEDEPS))

$(OBJS): $(OBJDIR)/pch/pch.h.gch
endif

#This is to avoid unpleasant side-effects of over-writing executable in-place if it is currently running
$(TARGS): $(ALL_OBJS)
	@echo '    Link    $(OUTNAME)$(SUFFIX)'
ifeq "$(HOST)" "WIN"
	$(call EXEC,$(GCC) $(ALL_OBJS) -o $(OUTNAME)$(SUFFIX) $(LIBS) $(AFLAGS) $(GFLAGS))
else
	$(call EXEC,$(GCC) $(ALL_OBJS) -o $(OUTNAME)$(SUFFIX).tmp $(LIBS) $(AFLAGS) $(GFLAGS))
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
LINKRES=$(GCC) -Wl,-r -Wl,-b,binary $< -o $@ -nostdlib
else
LINKRES=$(LD) -r -b binary $< -o $@
endif
OBJCOPYRES=objcopy --rename-section .data=.rodata,alloc,load,readonly,data,contents $@ $@

$(ROBJS): $(OBJDIR)/%.o: src/%.png
	@echo '    Res     $<'
	$(call EXEC,$(LINKRES))
	$(call EXEC,$(OBJCOPYRES))

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
	$(call EXEC,rm -f $(ROBJS) $(ROBJS:.o=.d))

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
