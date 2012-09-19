#variables which can be pre-set (non-exhaustive list)
#ARCH: value for the switch: -march=
#GCC: path to g++
#debug: set to true to for a debug build
#list: set to true to enable listings
#On windows only:
#x64: set to true to compile for x86_64/win64


OBJS:=retcon.o cfg.o optui.o parse.o socket.o tpanel.o twit.o db.o log.o cmdline.o userui.o mainui.o
TCOBJS:=libtwitcurl/base64.o libtwitcurl/HMAC_SHA1.o libtwitcurl/oauthlib.o libtwitcurl/SHA1.o libtwitcurl/twitcurl.o libtwitcurl/urlencode.o
SPOBJS:=res.o
ROBJS:=$(patsubst %.png,%.o,$(wildcard res/*.png))
EXOBJS:=utf8proc/utf8proc.o
OUTNAME:=retcon
CFLAGS:=-O3 -Wextra -Wall -Wno-unused-parameter
#-Wno-missing-braces -Wno-unused-parameter
CXXFLAGS:=-std=gnu++0x
GCC:=g++
LD:=ld

ifdef debug
CFLAGS:=-g -Wextra -Wall -Wno-unused-parameter
#AFLAGS:=-Wl,-d,--export-all-symbols
DEBUGPOSTFIX:=_debug
endif

GCCMACHINE:=$(shell $(GCC) -dumpmachine)
ifeq (mingw, $(findstring mingw,$(GCCMACHINE)))
#WIN
PLATFORM:=WIN
AFLAGS+=-mwindows -s -static -LC:/SourceCode/Libraries/wxWidgets2.8/lib/gcc_lib -L.
#-LC:/SourceCode/wxwidgets/source/lib/gcc_lib
CFLAGS+=-D CURL_STATICLIB
#CXXFLAGS+=
SUFFIX:=.exe
LIBS32=-lpcre -lcurl -lwxmsw28u_richtext -lwxmsw28u_aui -lwxbase28u_xml -lwxexpat -lwxmsw28u_html -lwxmsw28u_adv -lwxmsw28u_media -lwxmsw28u_core -lwxbase28u -lwxjpeg -lwxpng -lwxtiff -lrtmp -lssh2 -lidn -lssl -lz -lcrypto -leay32 -lwldap32 -lws2_32 -lgdi32 -lshell32 -lole32 -luuid -lcomdlg32 -lwinspool -lcomctl32 -loleaut32 -lwinmm
LIBS64=
GCC32=i686-w64-mingw32-g++
GCC64=x86_64-w64-mingw32-g++
MCFLAGS= -Icurl -IC:/SourceCode/Libraries/wxWidgets2.8/include -Isqlite -Izlib -I.
#-IC:/SourceCode/wxwidgets/source/include
HDEPS:=
EXECPREFIX:=
EXOBJS+=sqlite/sqlite3.o

ifdef x64
SIZEPOSTFIX:=64
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
LIBS:=-lpcre -lrt `wx-config --libs` -lcurl -lsqlite3
MCFLAGS:= `wx-config --cxxflags`
PACKER:=upx -9
#HDEPS:=
GCC_MAJOR:=$(shell $(GCC) -dumpversion | cut -d'.' -f1)
GCC_MINOR:=$(shell $(GCC) -dumpversion | cut -d'.' -f2)
ARCH:=$(shell test $(GCC_MAJOR) -gt 4 -o \( $(GCC_MAJOR) -eq 4 -a $(GCC_MINOR) -ge 2 \) && echo native)
EXECPREFIX:=./

wxconf:=$(shell wx-config --selected-config)
ifeq (gtk, $(findstring gtk,$(wxconf)))
LIBS+=`pkg-config --libs glib-2.0`
MCFLAGS+=`pkg-config --cflags glib-2.0`
endif

endif

POSTFIX:=$(SIZEPOSTFIX)$(DEBUGPOSTFIX)
ND_POSTFIX:=$(SIZEPOSTFIX)
OUTNAME:=$(OUTNAME)$(POSTFIX)

GCCVER:=$(shell $(GCC) -dumpversion)

ifeq (4.7.0, $(GCCVER))
$(error GCC 4.7.0 has a nasty bug in std::unordered_multimap, this will cause problems)
endif

ifeq (4.7.1, $(GCCVER))
CXXFLAGS+=-Wno-type-limits -Wno-uninitialized -Wno-maybe-uninitialized
#these cannot be feasibly suppressed at the local level in gcc 4.7
endif


OBJS:=$(OBJS:.o=.o$(POSTFIX))
TCOBJS:=$(TCOBJS:.o=.o$(POSTFIX))
EXOBJS:=$(EXOBJS:.o=.o$(POSTFIX))
ROBJS:=$(ROBJS:.o=.o$(ND_POSTFIX))
SPOBJS:=$(SPOBJS:.o=.o$(POSTFIX))

TARGS:=$(OUTNAME)$(SUFFIX)

ifdef list
CFLAGS+= -masm=intel -g --save-temps -Wa,-msyntax=intel,-aghlms=$*$(POSTFIX).lst
AFLAGS:=$(AFLAGS) -Wl,-Map=$(OUTNAME)$(POSTFIX).map
endif

ALL_OBJS:=$(OBJS) $(TCOBJS) $(EXOBJS) $(ROBJS) $(SPOBJS)

ifneq ($(ARCH),)
CFLAGS2 += -march=$(ARCH)
endif

.SUFFIXES:

all: $(TARGS)

$(TARGS): $(ALL_OBJS)
	$(GCC) $(ALL_OBJS) -o $(OUTNAME)$(SUFFIX) $(LIBS) $(AFLAGS)


.SUFFIXES: .cpp .c .o$(POSTFIX)

.cpp.o$(POSTFIX):
	$(GCC) -c $< -o $@ $(CFLAGS) $(MCFLAGS) $(CFLAGS2) $(CXXFLAGS) $(AFLAGS)

.c.o$(POSTFIX):
	$(GCC:++=cc) -c $< -o $@ $(CFLAGS) $(MCFLAGS) $(CFLAGS2) $(AFLAGS)

$(TCOBJS): %.o$(POSTFIX): %.cpp
	$(GCC) -c $< -o $@ $(CFLAGS) $(CFLAGS2) $(CXXFLAGS)

$(ROBJS): %.o$(ND_POSTFIX): %.png
ifeq "$(PLATFORM)" "WIN"
	$(GCC) -Wl,-r -Wl,-b,binary $< -o $@ -nostdlib
else
	$(LD) -r -b binary $< -o $@
endif
	objcopy --rename-section .data=.rodata,alloc,load,readonly,data,contents $@ $@

retcon.h.gch:
	$(GCC) -c retcon.h -o retcon.h.gch $(CFLAGS) $(MCFLAGS) $(CFLAGS2) $(CXXFLAGS) $(AFLAGS)

HEADERS:=retcon.h socket.h cfg.h parse.h twit.h tpanel.h optui.h libtwitcurl/twitcurl.h db.h log.h cmdline.h userui.h mainui.h magic_ptr.h

retcon.h.gch: $(HEADERS)
$(OBJS): $(HEADERS) retcon.h.gch
res.o$(POSTFIX) tpanel.o$(POSTFIX): res.h
$(TCOBJS): libtwitcurl/*.h
utf8proc/utf8proc.o$(POSTFIX) twit.o$(POSTFIX): utf8proc/utf8proc.h
ifeq ($(PLATFORM),WIN)
twit.o$(POSTFIX): strptime.cpp
endif


.PHONY: clean mostlyclean quickclean install uninstall all

quickclean:
	rm -f $(OBJS) $(OBJS:.o=.o64) $(OBJS:.o=.ii) $(OBJS:.o=.lst) $(OBJS:.o=.s) $(OBJS:.o=.o_debug) $(OBJS:.o=.o64_debug) $(OUTNAME)$(SUFFIX) $(OUTNAME)_debug$(SUFFIX) retcon.h.gch

mostlyclean: quickclean
	rm -f $(TCOBJS) $(TCOBJS:.o=.o64) $(TCOBJS:.o=.ii) $(TCOBJS:.o=.lst) $(TCOBJS:.o=.s) $(TCOBJS:.o=.o_debug) $(TCOBJS:.o=.o64_debug)
	rm -f $(SPOBJS) $(SPOBJS:.o=.o64) $(SPOBJS:.o=.ii) $(SPOBJS:.o=.lst) $(SPOBJS:.o=.s) $(SPOBJS:.o=.o_debug) $(SPOBJS:.o=.o64_debug)

clean: mostlyclean
	rm -f $(EXOBJS) $(EXOBJS:.o=.o64) $(EXOBJS:.o=.ii) $(EXOBJS:.o=.lst) $(EXOBJS:.o=.s) $(EXOBJS:.o=.o_debug) $(EXOBJS:.o=.o64_debug)
	rm -f $(ROBJS) $(ROBJS:.o=.o64)

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
