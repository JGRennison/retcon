#variables which can be pre-set (non-exhaustive list)
#ARCH: value for the switch: -march=
#GCC: path to g++
#debug: set to true to for a debug build
#list: set to true to enable listings
#map: set to true to enable linker map
#cross: set to true if building on Unix, but build target is Windows
#On windows only:
#x64: set to true to compile for x86_64/win64

#Note that to build on or for Windows, the include/lib search paths will need to be edited below and/or
#a number of libs/includes will need to be placed/built in a corresponding location where gcc can find them.


OBJS_SRC:=retcon.cpp cfg.cpp optui.cpp parse.cpp socket.cpp tpanel.cpp twit.cpp db.cpp log.cpp cmdline.cpp userui.cpp mainui.cpp signal.cpp dispscr.cpp uiutil.cpp mediawin.cpp
TCOBJS_SRC:=libtwitcurl/base64.cpp libtwitcurl/HMAC_SHA1.cpp libtwitcurl/oauthlib.cpp libtwitcurl/SHA1.cpp libtwitcurl/twitcurl.cpp libtwitcurl/urlencode.cpp
SPOBJS_SRC:=res.cpp version.cpp aboutwin.cpp
COBJS_SRC:=utf8proc/utf8proc.c
OUTNAME:=retcon
COMMONCFLAGS=-Wall -Wno-unused-parameter -Winvalid-pch -Wno-unused-local-typedefs -I$(OBJDIR)/pch
CFLAGS=-g -O3 $(COMMONCFLAGS)
AFLAGS=-g
CXXFLAGS=-std=gnu++0x -fno-exceptions
GCC:=g++
LD:=ld
OBJDIR:=objs
DIRS=$(OBJDIR) $(OBJDIR)$(PATHSEP)pch $(OBJDIR)$(PATHSEP)libtwitcurl $(OBJDIR)$(PATHSEP)res $(OBJDIR)$(PATHSEP)utf8proc

EXECPREFIX:=./
PATHSEP:=/
MKDIR:=mkdir -p

ifdef debug
CFLAGS=-g $(COMMONCFLAGS)
AFLAGS=-g
#AFLAGS:=-Wl,-d,--export-all-symbols
DEBUGPOSTFIX:=_debug
OBJDIR:=$(OBJDIR)$(DEBUGPOSTFIX)
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
HDEPS:=
EXCOBJS_SRC+=sqlite/sqlite3.c
DIRS+=$(OBJDIR)$(PATHSEP)deps$(PATHSEP)sqlite

ifndef cross
EXECPREFIX:=
PATHSEP:=\\
MKDIR:=mkdir
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
LIBS:=-lpcre -lrt `wx-config --libs` -lcurl -lsqlite3 -lz -lcrypto
MCFLAGS:=$(patsubst -I/%,-isystem /%,$(shell wx-config --cxxflags))
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

OUTNAME:=$(OUTNAME)$(SIZEPOSTFIX)$(DEBUGPOSTFIX)

GCCVER:=$(shell $(GCC) -dumpversion)

ifeq (4.7.0, $(GCCVER))
$(error GCC 4.7.0 has a nasty bug in std::unordered_multimap, this will cause problems)
endif

ifeq (4.7.1, $(GCCVER))
CXXFLAGS+=-Wno-type-limits -Wno-uninitialized -Wno-maybe-uninitialized
#these cannot be feasibly suppressed at the local level in gcc 4.7
endif

TARGS:=$(OUTNAME)$(SUFFIX)

ifdef list
CFLAGS+= -masm=intel -g --save-temps -Wa,-msyntax=intel,-aghlms=$*.lst
endif

ifdef map
AFLAGS+=-Wl,-Map=$(OUTNAME).map
TARGS+=$(OUTNAME).map
endif

OBJS:=$(patsubst src/%.cpp,$(OBJDIR)/%.o,$(addprefix src/,$(OBJS_SRC)))
TCOBJS:=$(patsubst src/%.cpp,$(OBJDIR)/%.o,$(addprefix src/,$(TCOBJS_SRC)))
COBJS:=$(patsubst src/%.c,$(OBJDIR)/%.o,$(addprefix src/,$(COBJS_SRC)))
SPOBJS:=$(patsubst src/%.cpp,$(OBJDIR)/%.o,$(addprefix src/,$(SPOBJS_SRC)))
ROBJS:=$(patsubst src/res/%.png,$(OBJDIR)/res/%.o,$(wildcard src/res/*.png))
EXOBJS:=$(patsubst %.c,$(OBJDIR)/deps/%.o,$(EXCOBJS_SRC))

ALL_OBJS:=$(OBJS) $(TCOBJS) $(COBJS) $(SPOBJS) $(ROBJS) $(EXOBJS)

ifneq ($(ARCH),)
CFLAGS2 += -march=$(ARCH)
endif

.SUFFIXES:

all: $(TARGS)

$(TARGS): $(ALL_OBJS)
	$(GCC) $(ALL_OBJS) -o $(OUTNAME)$(SUFFIX) $(LIBS) $(AFLAGS) $(GFLAGS)

$(OBJDIR)/%.o: src/%.cpp
	$(GCC) -c $< -o $@ $(CFLAGS) $(MCFLAGS) $(CFLAGS2) $(CXXFLAGS) $(GFLAGS)

$(OBJDIR)/%.o: src/%.c
	$(GCC:++=cc) -c $< -o $@ $(CFLAGS) $(MCFLAGS) $(CFLAGS2) $(GFLAGS)

$(TCOBJS): $(OBJDIR)/%.o: src/%.cpp
	$(GCC) -c $< -o $@ $(CFLAGS) $(CFLAGS2) $(CXXFLAGS) $(GFLAGS)

$(ROBJS): $(OBJDIR)/%.o: src/%.png
ifeq "$(PLATFORM)" "WIN"
	$(GCC) -Wl,-r -Wl,-b,binary $< -o $@ -nostdlib
else
	$(LD) -r -b binary $< -o $@
endif
	objcopy --rename-section .data=.rodata,alloc,load,readonly,data,contents $@ $@

$(EXOBJS): $(OBJDIR)/deps/%.o: %.c
	$(GCC:++=cc) -c $< -o $@ $(CFLAGS) $(CFLAGS2) $(GFLAGS)

$(OBJDIR)/pch/retcon.h.gch:
	$(GCC) -c src/retcon.h -o $(OBJDIR)/pch/retcon.h.gch $(CFLAGS) $(MCFLAGS) $(CFLAGS2) $(CXXFLAGS) $(GFLAGS)

$(ALL_OBJS) src/pch/retcon.h.gch: | $(DIRS)

$(DIRS):
	-$(MKDIR) $@

HEADERS:=src/retcon.h src/socket.h src/cfg.h src/parse.h src/twit.h src/tpanel.h src/optui.h src/libtwitcurl/twitcurl.h src/db.h src/log.h
HEADERS+=src/cmdline.h src/userui.h src/mainui.h src/magic_ptr.h src/univdefs.h src/signal.h src/dispscr.h src/uiutil.h src/mediawin.h src/raii.h

$(OBJDIR)/pch/retcon.h.gch: $(HEADERS)
$(OBJS): $(HEADERS) $(OBJDIR)/pch/retcon.h.gch
$(OBJDIR)/res.o $(OBJDIR)/tpanel.o: src/res.h
$(OBJDIR)/retcon.o $(OBJDIR)/tpanel.o $(OBJDIR)/version.o: src/version.h src/univdefs.h
$(OBJDIR)/aboutwin.o: src/aboutwin.h src/version.h src/univdefs.h
$(OBJDIR)/mainui.o: src/aboutwin.h
$(TCOBJS): src/libtwitcurl/*.h
$(OBJDIR)/utf8proc/utf8proc.o $(OBJDIR)/twit.o: src/utf8proc/utf8proc.h
ifeq ($(PLATFORM),WIN)
$(OBJDIR)/twit.o: src/strptime.cpp
endif


.PHONY: clean mostlyclean quickclean install uninstall all

quickclean:
	rm -f $(OBJS) $(OBJS:.o=.ii) $(OBJS:.o=.lst) $(OBJS:.o=.s) $(OUTNAME)$(SUFFIX) $(OUTNAME)_debug$(SUFFIX) $(OBJDIR)/pch/retcon.h.gch

mostlyclean: quickclean
	rm -f $(TCOBJS) $(TCOBJS:.o=.ii) $(TCOBJS:.o=.lst) $(TCOBJS:.o=.s)
	rm -f $(SPOBJS) $(SPOBJS:.o=.ii) $(SPOBJS:.o=.lst) $(SPOBJS:.o=.s)

clean: mostlyclean
	rm -f $(COBJS) $(COBJS:.o=.ii) $(COBJS:.o=.lst) $(COBJS:.o=.s)
	rm -f $(ROBJS)

install:
ifeq "$(PLATFORM)" "WIN"
	@echo Install only supported on Unixy platforms
else
	cp $(OUTNAME)$(SUFFIX) /usr/bin/$(OUTNAME)$(SUFFIX)
endif

uninstall:
ifeq "$(PLATFORM)" "WIN"
	@echo Uninstall only supported on Unixy platforms
else
	rm /usr/bin/$(OUTNAME)$(SUFFIX)
endif
