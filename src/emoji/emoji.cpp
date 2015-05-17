//  retcon
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
//  2015 - Jonathan G Rennison <j.g.rennison@gmail.com>
//==========================================================================

#include "../univdefs.h"
#include "emoji.h"
#include "emoji-list.h"
#include "../utf8.h"
#include "../log.h"
#include "../util.h"
#include <algorithm>
#include <iterator>
#include <wx/mstream.h>
#include <wx/image.h>
#include <pcre.h>

//This is such that PCRE_STUDY_JIT_COMPILE can be used pre PCRE 8.20
#ifndef PCRE_STUDY_JIT_COMPILE
#define PCRE_STUDY_JIT_COMPILE 0
#endif

wxBitmap emoji_cache::GetEmojiImg(EMOJI_MODE mode, uint32_t first, uint32_t second) {
	cache_item &item = img_map[std::make_pair(first, second)];

	// exclude emoji for (c), (r) and TM, default fonts can do these just fine
	if(first == 0xa9 || first == 0xae || first == 0x2122)
		return wxBitmap();

	wxBitmap *img = nullptr;
	switch(mode) {
		case EMOJI_MODE::OFF:
			return wxBitmap();
		case EMOJI_MODE::SIZE_16:
			img = &(item.size_16);
			break;
		case EMOJI_MODE::SIZE_36:
			img = &(item.size_36);
			break;
	}

	if(!img->IsOk()) {
		auto cmp = [&](const emoji_item &item, const std::pair<uint32_t, uint32_t> &needle) {
			return std::make_pair(item.first, item.second) < needle;
		};
		auto it = std::lower_bound(emoji_map, emoji_map + emoji_map_size, std::make_pair(first, second), cmp);
		if(it != (emoji_map + emoji_map_size) && !cmp(*it, std::make_pair(first, second))) {
			// found emoji entry

			const std::pair<const unsigned char *, const unsigned char *> *ptrs = nullptr;
			switch(mode) {
				case EMOJI_MODE::OFF:
					break;
				case EMOJI_MODE::SIZE_16:
					ptrs = &(it->ptrs_16);
					break;
				case EMOJI_MODE::SIZE_36:
					ptrs = &(it->ptrs_36);
					break;
			}

			if(ptrs) {
				wxMemoryInputStream memstream(ptrs->first, ptrs->second - ptrs->first);
				wxImage image;
				image.LoadFile(memstream, wxBITMAP_TYPE_PNG);
				*img = wxBitmap(image);
			}
		}
	}

	return *img;
}

void EmojiParseString(const std::string &input, EMOJI_MODE mode, emoji_cache &cache, std::function<void(std::string)> string_out, std::function<void(wxBitmap, std::string)> img_out) {
	static pcre *pattern = nullptr;
	static pcre_extra *patextra = nullptr;

	if(mode == EMOJI_MODE::OFF) {
		string_out(input);
		return;
	}

	if(!pattern) {
		const char *errptr;
		int erroffset;
		pattern = pcre_compile(emoji_regex.c_str(), PCRE_NO_UTF8_CHECK | PCRE_UTF8, &errptr, &erroffset, 0);
		if(!pattern) {
			LogMsgFormat(LOGT::OTHERERR, "EmojiParseString: pcre_compile failed: %s (%d)\n%s", cstr(errptr), erroffset, cstr(emoji_regex));
			return;
		}
		patextra = pcre_study(pattern, PCRE_STUDY_JIT_COMPILE, &errptr);
	}

	auto output_string = [&](std::string out) {
		if(!out.empty())
			string_out(std::move(out));
	};

	int startoffset = 0;
	while(true) {
		int ovector[30];
		int rc = pcre_exec(pattern, patextra, input.data(), input.size(), startoffset, 0, ovector, 30);
		if(rc <= 0)
			break;
		output_string(std::string(input.data() + startoffset, input.data() + ovector[0]));
		startoffset = ovector[1];

		uint32_t first = 0;
		uint32_t second = 0;
		uint32_t variant = 0;
		if(rc >= 2) {
			const char *str = input.data() + ovector[2];
			if(str < input.data() + ovector[3])
				first = getcharfromstr_utf8_ret(&str);
			if(str < input.data() + ovector[3])
				second = getcharfromstr_utf8_ret(&str);
		}
		if(rc >= 3) {
			if(ovector[4] < ovector[5])
				variant = getcharfromstr_utf8(input.data() + ovector[4]);
		}

		if(second == 0xFE0F)
			second = 0;

		wxBitmap img;
		if(variant != 0xFE0E) {
			img = cache.GetEmojiImg(mode, first, second);
		}

		std::string out_text(input.data() + ovector[0], input.data() + ovector[1]);
		if(img.IsOk())
			img_out(std::move(img), std::move(out_text));
		else
			output_string(std::move(out_text));
	}
	output_string(std::string(input.data() + startoffset, input.data() + input.size()));
}
