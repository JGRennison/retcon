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

#ifndef HGUARD_SRC_EMOJI_EMOJI
#define HGUARD_SRC_EMOJI_EMOJI

#include "../univdefs.h"
#include "../cfg.h"
#include <utility>
#include <wx/image.h>

class emoji_cache {
	struct cache_item {
		wxImage size_16;
		wxImage size_36;
	};

	std::map <std::pair<uint32_t, uint32_t>, cache_item> img_map;

	public:
	wxImage GetEmojiImg(EMOJI_MODE mode, uint32_t first, uint32_t second);
};

void EmojiParseString(const std::string &input, EMOJI_MODE mode, emoji_cache &cache, std::function<void(std::string)> string_out, std::function<void(wxImage, std::string)> img_out);

#endif
