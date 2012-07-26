#variables which can be pre-set (non-exhaustive list)
#ARCH: value for the switch: -march=
#GCC: path to g++
#PACKER: sets the executable packer used
#debug: set to true to for a debug build
#list: set to true to enable listings
#unpacked: set to true to also save an unpacked executable
#On windows only:
#x64: set to true to compile for x86_64/win64


OBJS:=retcon.o cfg.o optui.o parse.o socket.o tpanel.o twit.o
TCOBJS:=libtwitcurl/base64.o libtwitcurl/HMAC_SHA1.o libtwitcurl/oauthlib.o libtwitcurl/SHA1.o libtwitcurl/twitcurl.o libtwitcurl/urlencode.o
OUTNAME:=retcon
CFLAGS:=-O3 -Wextra -Wall -Wno-unused-parameter
#-Wno-missing-braces -Wno-unused-parameter
CXXFLAGS:=-fno-rtti -std=gnu++0x

gccver:=$(shell gcc -dumpmachine)
ifeq (mingw, $(findstring mingw,$(gccver)))
#WIN
PLATFORM:=WIN
AFLAGS:=-mwindows -s -static -LC:/SourceCode/Libraries/wxWidgets2.8/lib/gcc_lib -L.
CFLAGS+=-D CURL_STATICLIB
SUFFIX:=.exe
LIBS32=-lcurl -lwxmsw28u -lwxjpeg -lwxpng -lwxtiff -lrtmp -lssh2 -lidn -lssl -lz -lcrypto -leay32 -lwldap32 -lws2_32 -lgdi32 -lshell32 -lole32 -luuid -lcomdlg32 -lwinspool -lcomctl32 -loleaut32 -lwinmm
LIBS64=
GCC32=mingw32-g++
GCC64=x86_64-w64-mingw32-g++
MCFLAGS= -Icurl -IC:/SourceCode/Libraries/wxWidgets2.8/include
#-isystem .
OBJS+=
HDEPS:=
EXECPREFIX:=
twit.o$(POSTFIX): timegm.cpp

ifdef x64
GCC:=$(GCC64)
LIBS:=$(LIBS64)
OUTNAME:=$(OUTNAME)64
OBJS:=$(OBJS:.o=.o64)
TCOBJS:=$(TCOBJS:.o=.o64)
POSTFIX:=64
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
GCC_MAJOR:=$(shell gcc -dumpversion | cut -d'.' -f1)
GCC_MINOR:=$(shell gcc -dumpversion | cut -d'.' -f2)
PLATFORM:=UNIX
AFLAGS=
LIBS:=-lpcre -lrt `wx-config --libs` -lcurl
MCFLAGS= `wx-config --cxxflags`
GCC:=g++
PACKER:=upx -9
#OBJS+=
#HDEPS:=
ARCH:=$(shell test $(GCC_MAJOR) -gt 4 -o \( $(GCC_MAJOR) -eq 4 -a $(GCC_MINOR) -ge 2 \) && echo native)
EXECPREFIX:=./
endif

ALL_OBJS:=$(OBJS) $(TCOBJS)

ifneq ($(ARCH),)
CFLAGS2 += -march=$(ARCH)
endif

.SUFFIXES:

NTEMP:=$(OUTNAME)

TARGS:=$(OUTNAME)$(SUFFIX)

ifdef debug
CFLAGS=-g -W -Wall -isystem .
AFLAGS:=-static -Wl,-d,--export-all-symbols
endif

ifdef list
CFLAGS+= -masm=intel -g --save-temps -Wa,-msyntax=intel,-aghlms=$*$(POSTFIX).lst
AFLAGS:=$(AFLAGS) -Wl,-Map=$(OUTNAME)$(POSTFIX).map
endif

ifdef unpacked
NTEMP:=$(OUTNAME)_defl$(POSTFIX)
TARGS+=$(NTEMP)$(SUFFIX)
endif

all: $(TARGS)

$(TARGS): $(ALL_OBJS)
	$(GCC) $(ALL_OBJS) -o $(NTEMP)$(SUFFIX) $(LIBS) $(AFLAGS)
ifneq "$(NTEMP)" "$(OUTNAME)"
	cp $(NTEMP)$(SUFFIX) $(OUTNAME)$(SUFFIX)
endif
	-$(PACKER) $(OUTNAME)$(SUFFIX)


.SUFFIXES: .cpp .c .o$(POSTFIX)

.cpp.o$(POSTFIX):
	$(GCC) -c $< -o $@ $(CFLAGS) $(MCFLAGS) $(CFLAGS2) $(CXXFLAGS) $(AFLAGS)

.c.o$(POSTFIX):
	$(GCC:++=cc) -c $< -o $@ $(CFLAGS) $(MCFLAGS) $(CFLAGS2) $(AFLAGS)

$(TCOBJS): %.o$(POSTFIX): %.cpp
	$(GCC) -c $< -o $@ $(CFLAGS) $(CFLAGS2) $(CXXFLAGS)

$(OBJS): retcon.h socket.h cfg.h parse.h twit.h tpanel.h optui.h libtwitcurl/twitcurl.h
$(TCOBJS): libtwitcurl/*.h
twit.o$(POSTFIX): strptime.cpp


.PHONY: clean install uninstall all

clean:
	rm -f $(ALL_OBJS) $(ALL_OBJS:.o=.o64) $(ALL_OBJS:.o=.ii) $(ALL_OBJS:.o=.lst) $(ALL_OBJS:.o=.s) $(OUTNAME)$(SUFFIX)

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
