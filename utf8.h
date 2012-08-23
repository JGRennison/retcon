//  npipe
//
//  WEBSITE: http://npipe.sourceforge.net
//
//  NOTE: This software is licensed under the GPL. See: COPYING-GPL.txt
//
//  This program  is distributed in the  hope that it will  be useful, but
//  WITHOUT   ANY  WARRANTY;   without  even   the  implied   warranty  of
//  MERCHANTABILITY  or FITNESS  FOR A  PARTICULAR PURPOSE.   See  the GNU
//  General Public License for more details.
//
//  Jonathan Rennison (or anybody else) is in no way responsible, or liable
//  for this program or its use in relation to users, 3rd parties or to any
//  persons in any way whatsoever.
//
//  You  should have  received a  copy of  the GNU  General Public
//  License along  with this program; if  not, write to  the Free Software
//  Foundation, Inc.,  59 Temple Place,  Suite 330, Boston,  MA 02111-1307
//  USA
//
//  2012 - j.g.rennison@gmail.com
//==========================================================================

//UTF-8 functions all assume perfectly valid UTF-8 for the most part

inline int getcharfromstr_utf8(const char *str) {
	char c1=str[0];
	if(!(c1&0x80)) return c1;
	else if((c1&0xE0)==0xC0) return ((c1&0x1F)<<6) + ((str[1]&0x3F));
	else if((c1&0xF0)==0xE0) return ((c1&0x0F)<<12) + ((str[1]&0x3F)<<6) + ((str[2]&0x3F));
	else if((c1&0xF8)==0xF0) return ((c1&0x07)<<18) + ((str[1]&0x3F)<<12) + ((str[2]&0x3F)<<6) + ((str[3]&0x3F));
	else return 0xFFFD;
}

inline int getcharfromstr_utf8_ret(char **str) {
	char c1=(*str)[0];
	if(!(c1&0x80)) {
		(*str)++;
		return c1;
	}
	else if((c1&0xE0)==0xC0) {
		int r=((c1&0x1F)<<6) + (((*str)[1]&0x3F));
		(*str)+=2;
		return r;
	}
	else if((c1&0xF0)==0xE0) {
		int r=((c1&0x0F)<<12) + (((*str)[1]&0x3F)<<6) + (((*str)[2]&0x3F));
		(*str)+=3;
		return r;
	}
	else if((c1&0xF8)==0xF0) {
		int r=((c1&0x07)<<18) + (((*str)[1]&0x3F)<<12) + (((*str)[2]&0x3F)<<6) + (((*str)[3]&0x3F));
		(*str)+=4;
		return r;
	}
	else {
		(*str)++;
		return 0xFFFD;
	}
}

inline int getcharfromstr_offset_utf8(const char *str, int offset, int bytelength) {
	int i;
	for(i=0; i<bytelength; i++) {
		if((str[i]&0xC0)==0x80) continue;
		if(!offset) return getcharfromstr_utf8(&str[i]);		//this ignores the issue of a UTF-8 sequence running off the end of the string
		else offset--;
	}
	return -1;	//offset is beyond end of string
}

inline char *getstrfromstr_offset_utf8(char *str, int offset, int bytelength) {
	int i;
	for(i=0; i<bytelength; i++) {
		if((str[i]&0xC0)==0x80) continue;
		if(!offset) return str+i;		//this ignores the issue of a UTF-8 sequence running off the end of the string
		else offset--;
	}
	if(offset==0) return str+bytelength;	//end of string
	return 0;	//offset is beyond end of string
}

inline int utf8firsttonumbytes(char c1) {
	if(!(c1&0x80)) return 1;
	else if((c1&0xE0)==0xC0) return 2;
	else if((c1&0xF0)==0xE0) return 3;
	else if((c1&0xF8)==0xF0) return 4;
	else return 0;
}

inline int chartonumutf8bytes(int c) {
	if(c<0) return 0;
	else if(c<0x80) return 1;
	else if(c<0x800) return 2;
	else if(c<0x10000) return 3;
	else if(c<0x200000) return 4;
	else return 0;
}

inline void setstrfromchar_utf8(int c, char *str) {
	if(c<0) { }
	else if(c<0x80) {
		str[0]=c;
	}
	else if(c<0x800) {
		str[0]=0xC0|(c>>6);
		str[1]=0x80|(c&0x3F);
	}
	else if(c<0x10000) {
		str[0]=0xE0|(c>>12);
		str[1]=0x80|((c>>6)&0x3F);
		str[2]=0x80|(c&0x3F);
	}
	else if(c<0x200000) {
		str[0]=0xF0|(c>>18);
		str[1]=0x80|((c>>12)&0x3F);
		str[2]=0x80|((c>>6)&0x3F);
		str[3]=0x80|(c&0x3F);
	}
}

inline size_t strlen_utf8(const char *str) {
	size_t len=0;
	while(*str) {
		int add=utf8firsttonumbytes(*str);
		len++;
		if(add) str+=add;
		else break;
	}
	return len;
}

inline size_t strlen_utf8(const char *str, size_t bytes) {
	size_t len=0;
	const char *end=str+bytes;
	while(str<end && *str) {
		int add=utf8firsttonumbytes(*str);
		len++;
		if(add) str+=add;
		else break;
	}
	return len;
}
