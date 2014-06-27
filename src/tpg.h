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
//  2014 - Jonathan G Rennison <j.g.rennison@gmail.com>
//==========================================================================

#ifndef HGUARD_SRC_TPG
#define HGUARD_SRC_TPG

#include "univdefs.h"
#include <wx/bitmap.h>
#include <wx/image.h>
#include <memory>

struct tpanelreltimeupdater : public wxTimer {
	void Notify() override;
};

struct tpanelglobal {
	wxBitmap arrow;
	int arrow_dim;
	tpanelreltimeupdater minutetimer;
	wxBitmap infoicon;
	wxImage infoicon_img;
	wxBitmap replyicon;
	wxImage replyicon_img;
	wxBitmap favicon;
	wxImage favicon_img;
	wxBitmap favonicon;
	wxImage favonicon_img;
	wxBitmap retweeticon;
	wxImage retweeticon_img;
	wxBitmap retweetonicon;
	wxImage retweetonicon_img;
	wxBitmap dmreplyicon;
	wxImage dmreplyicon_img;
	wxBitmap proticon;
	wxImage proticon_img;
	wxBitmap unlockicon;
	wxImage unlockicon_img;
	wxBitmap verifiedicon;
	wxImage verifiedicon_img;
	wxBitmap closeicon;
	wxBitmap multiunreadicon;
	wxBitmap photoicon;
	wxImage photoicon_img;

	static std::shared_ptr<tpanelglobal> Get();
	static std::weak_ptr<tpanelglobal> tpg_glob;

	tpanelglobal();	//use Get() instead
};

#endif
