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
//  2013 - Jonathan G Rennison <j.g.rennison@gmail.com>
//==========================================================================

#ifndef HGUARD_SRC_MEDIAWIN
#define HGUARD_SRC_MEDIAWIN

#include "univdefs.h"
#include "media_id_type.h"
#include <wx/frame.h>
#include <memory>
#include <string>

struct media_display_win_pimpl;

struct media_display_win : public wxFrame {
	private:
	std::unique_ptr<media_display_win_pimpl> pimpl;

	public:
	media_display_win(wxWindow *parent, media_id_type media_id_, optional_tweet_ptr_p src_tweet_);
	~media_display_win();
	void UpdateImage();
	void NotifyVideoLoadSuccess(const std::string &url);
	void NotifyVideoLoadFailure(const std::string &url);
};

#endif
