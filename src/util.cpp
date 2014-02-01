//  retcon
//
//  WEBSITE: http://retcon.sourceforge.net
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation; either version 2 of the License, or
//  (at your option) any later version. See: COPYING-GPL.txt
//
//  This program  is distributed in the  hope that it will  be useful, but
//  WITHOUT   ANY  WARRANTY;   without  even   the  implied   warranty  of
//  MERCHANTABILITY  or FITNESS  FOR A  PARTICULAR PURPOSE.   See  the GNU
//  General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program. If not, see <http://www.gnu.org/licenses/>.
//
//  2013 - Jonathan G Rennison <j.g.rennison@gmail.com>
//==========================================================================

#include "univdefs.h"
#include "util.h"
#include <wx/file.h>
#include <wx/mstream.h>
#include <wx/image.h>
#include <openssl/sha.h>


std::string hexify(const std::string &in) {
	const char hex[]="0123456789ABCDEF";
	size_t len = in.length();
	std::string out;
	out.reserve(2*len);
	for(size_t i=0; i<len; i++) {
		const unsigned char c = (const unsigned char) in[i];
		out.push_back(hex[c>>4]);
		out.push_back(hex[c&15]);
	}
	return out;
}

wxString hexify_wx(const std::string &in) {
	const wxChar hex[]=wxT("0123456789ABCDEF");
	size_t len = in.length();
	wxString out;
	out.Alloc(2*len);
	for(size_t i=0; i<len; i++) {
		const unsigned char c = (const unsigned char) in[i];
		out.Append(hex[c>>4]);
		out.Append(hex[c&15]);
	}
	return out;
}

bool LoadFromFileAndCheckHash(const wxString &filename, shb_iptr hash, char *&data, size_t &size) {
	if(!hash) return false;
	wxFile file;
	bool opened = file.Open(filename);
	if(opened) {
		wxFileOffset len = file.Length();
		if(len >= 0 && len < (50<<20)) {    //don't load empty or absurdly large files
			data = (char*) malloc(len);
			size = file.Read(data, len);
			if(size == (size_t) len) {
				unsigned char curhash[20];
				SHA1((const unsigned char *) data, (unsigned long) len, curhash);
				if(memcmp(curhash, hash->hash_sha1, 20) == 0) {
					return true;
				}
			}
			free(data);
		}
	}
	data = 0;
	size = 0;
	return false;
}

bool LoadImageFromFileAndCheckHash(const wxString &filename, shb_iptr hash, wxImage &img) {
	if(!hash) return false;
	char *data = 0;
	size_t size;
	bool success = false;
	if(LoadFromFileAndCheckHash(filename, hash, data, size)) {
		wxMemoryInputStream memstream(data, size);
		if(img.LoadFile(memstream, wxBITMAP_TYPE_ANY)) {
			success = true;
		}
	}
	if(data) free(data);
	return success;
}

wxString rc_wx_strftime(const wxString &format, const struct tm *tm, time_t timestamp, bool localtime) {
	#ifdef __WINDOWS__	//%z is broken in MSVCRT, use a replacement
				//also add %F, %R, %T, %s
				//this is adapted from npipe var.cpp
	wxString newfmt;
	newfmt.Alloc(format.length());
	wxString &real_format=newfmt;
	const wxChar *ch=format.c_str();
	const wxChar *cur=ch;
	while(*ch) {
		if(ch[0]=='%') {
			wxString insert;
			if(ch[1]=='z') {
				int hh;
				int mm;
				if(localtime) {
					TIME_ZONE_INFORMATION info;
					DWORD res = GetTimeZoneInformation(&info);
					int bias = - info.Bias;
					if(res==TIME_ZONE_ID_DAYLIGHT) bias-=info.DaylightBias;
					hh = bias / 60;
					if(bias<0) bias=-bias;
					mm = bias % 60;
				}
				else {
					hh=mm=0;
				}
				insert.Printf(wxT("%+03d%02d"), hh, mm);
			}
			else if(ch[1]=='F') {
				insert=wxT("%Y-%m-%d");
			}
			else if(ch[1]=='R') {
				insert=wxT("%H:%M");
			}
			else if(ch[1]=='T') {
				insert=wxT("%H:%M:%S");
			}
			else if(ch[1]=='s') {
				insert.Printf(wxT("%" wxLongLongFmtSpec "d"), (long long int) timestamp);
			}
			else if(ch[1]) {
				ch++;
			}
			if(insert.length()) {
				real_format.Append(wxString(cur, ch-cur));
				real_format.Append(insert);
				cur=ch+2;
			}
		}
		ch++;
	}
	real_format.Append(cur);
	#else
	const wxString &real_format=format;
	#endif

	char timestr[256];
	strftime(timestr, sizeof(timestr), real_format.ToUTF8(), tm);
	return wxstrstd(timestr);
}

//from http://stackoverflow.com/questions/2342162/stdstring-formatting-like-sprintf#2342176
std::string string_format(const std::string &fmt, ...) {
    int size = 100;
    std::string str;
    va_list ap;
    while (1) {
        str.resize(size);
        va_start(ap, fmt);
        int n = vsnprintf((char *)str.c_str(), size, fmt.c_str(), ap);
        va_end(ap);
        if (n > -1 && n < size) {
            str.resize(n);
            return str;
        }
        if (n > -1)
            size = n + 1;
        else
            size *= 2;
    }
    return str;
}
