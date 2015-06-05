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

#include "univdefs.h"
#include "uiutil.h"
#include "version.h"
#include "twit.h"
#include "twitcurlext.h"
#include "taccount.h"
#include "tpanel.h"
#include "tpanel-data.h"
#include "tpanel-aux.h"
#include "userui.h"
#include "mediawin.h"
#include "mainui.h"
#include "alldata.h"
#include "util.h"
#include "log.h"
#include "retcon.h"
#include "dispscr.h"
#include "tpg.h"
#include "log-util.h"
#include <wx/colour.h>
#include <wx/clipbrd.h>
#include <wx/dataobj.h>
#include <wx/dc.h>
#include <wx/dcclient.h>
#include <algorithm>

tweetactmenudata tamd;

void SaveWindowLayout() {
	if(ad.twinlayout_final) return;

	ad.mflayout.clear();
	ad.twinlayout.clear();
	unsigned int mainframeindex = 0;
	for(auto &mf : mainframelist) {
		mf->auib->FillWindowLayout(mainframeindex);
		ad.mflayout.emplace_back();
		mf_layout_desc &mfld = ad.mflayout.back();
		mfld.mainframeindex = mainframeindex;
		mfld.pos = mf->nominal_pos;
		mfld.size = mf->nominal_size;
		mfld.maximised = mf->IsMaximized();
		mainframeindex++;
	}
}

void RestoreWindowLayout() {
	FreezeAll();
	for(auto &mfld : ad.mflayout) {
		mainframe *mft = new mainframe(appversionname, mfld.pos, mfld.size);
		if(mfld.maximised) mft->Maximize(true);
	}
	unsigned int lastsplitindex = 0;
	for(auto &twld : ad.twinlayout) {
		while(twld.mainframeindex >= mainframelist.size()) {
			mainframe *mft = new mainframe(appversionname, wxPoint(50, 50), wxSize(450, 340));
			mft->Freeze();
			lastsplitindex = 0;
		}
		mainframe *mf = mainframelist[twld.mainframeindex];
		auto tp=tpanel::MkTPanel(twld.name, twld.dispname, twld.flags, twld.tpautos, twld.tpudcautos);
		tpanelparentwin *tpw = tp->MkTPanelWin(mf, (twld.splitindex > lastsplitindex));

		if(twld.splitindex > lastsplitindex) {
			mf->auib->Split(mf->auib->GetPageIndex(tpw), wxRIGHT);
			lastsplitindex = twld.splitindex;
		}
	}
	ThawAll();
}



media_id_type ParseMediaID(wxString url) {
	unsigned int i = 1;
	media_id_type media_id;
	for(; i < url.Len(); i++) {
		if(url[i] >= '0' && url[i] <= '9') {
			media_id.m_id *= 10;
			media_id.m_id += url[i] - '0';
		}
		else break;
	}
	if(url[i] != '_') return media_id;
	for(i++; i < url.Len(); i++) {
		if(url[i] >= '0' && url[i] <= '9') {
			media_id.t_id *= 10;
			media_id.t_id += url[i] - '0';
		}
		else break;
	}
	return media_id;
}

uint64_t ParseUrlID(wxString url) {
	uint64_t id = 0;
	for(unsigned int i = 1; i < url.Len(); i++) {
		if(url[i] >= '0' && url[i] <= '9') {
			id *= 10;
			id += url[i] - '0';
		}
		else break;
	}
	return id;
}

void AppendToTAMIMenuMap(tweetactmenudata &map, int &nextid, TAMI_TYPE type, tweet_ptr tw, unsigned int dbindex, udc_ptr_p user,
		flagwrapper<TPF> flags, wxString extra, panelparentwin_base *ppwb) {
	map[nextid] = {tw, user, type, dbindex, flags, extra, ppwb};
	nextid++;
}

void MakeRetweetMenu(wxMenu *menuP, tweetactmenudata &map, int &nextid, tweet_ptr_p tw) {
	for(auto &it : alist) {
		wxMenuItem *menuitem = menuP->Append(nextid, it->dispname);
		menuitem->Enable(it->enabled);
		AppendToTAMIMenuMap(map, nextid, TAMI_RETWEET, tw, it->dbindex);
	}
}

void MakeFavMenu(wxMenu *menuP, tweetactmenudata &map, int &nextid, tweet_ptr_p tw) {
	for(auto &it : alist) {
		tweet_perspective *tp = tw->GetTweetTP(it);
		bool known = (tp != nullptr);
		bool faved = false;
		if(tp && tp->IsFavourited()) faved = true;

		wxMenu *submenu = new wxMenu;
		menuP->AppendSubMenu(submenu, (known ? (faved ? wxT("\x2713 ") : wxT("\x2715 ")) : wxT("? ")) + it->dispname);
		submenu->SetTitle(known?(faved ? wxT("Favourited") : wxT("Not Favourited")) : wxT("Unknown"));

		wxMenuItem *menuitem = submenu->Append(nextid, wxT("Favourite"));
		menuitem->Enable(it->enabled && (!known || !faved));
		AppendToTAMIMenuMap(map, nextid, TAMI_FAV, tw, it->dbindex);

		menuitem=submenu->Append(nextid, wxT("Remove Favourite"));
		menuitem->Enable(it->enabled && (!known || faved));
		AppendToTAMIMenuMap(map, nextid, TAMI_UNFAV, tw, it->dbindex);
	}
}

void MakeCopyMenu(wxMenu *menuP, tweetactmenudata &map, int &nextid, tweet_ptr_p tw) {
	if(tw->flags.Get('R')) {
		menuP->Append(nextid, wxT("Copy Text"));
		AppendToTAMIMenuMap(map, nextid, TAMI_COPYRTTEXT, tw);
		menuP->Append(nextid, wxT("Copy Retweet Text"));
		AppendToTAMIMenuMap(map, nextid, TAMI_COPYTEXT, tw);
	}
	else {
		menuP->Append(nextid, wxT("Copy Text"));
		AppendToTAMIMenuMap(map, nextid, TAMI_COPYTEXT, tw);
	}
	menuP->Append(nextid, wxT("Copy Link to Tweet"));
	AppendToTAMIMenuMap(map, nextid, TAMI_COPYLINK, tw);
	menuP->Append(nextid, wxString::Format(wxT("Copy ID (%" wxLongLongFmtSpec "d)"), tw->id));
	AppendToTAMIMenuMap(map, nextid, TAMI_COPYID, tw);
}

void MakeMarkMenu(wxMenu *menuP, tweetactmenudata &map, int &nextid, tweet_ptr_p tw) {
	wxMenuItem *wmi1 = menuP->Append(nextid, wxT("Read"), wxT(""), wxITEM_RADIO);
	wmi1->Check(tw->flags.Get('r'));
	AppendToTAMIMenuMap(map, nextid, TAMI_MARKREAD, tw);
	wxMenuItem *wmi2 = menuP->Append(nextid, wxT("Unread"), wxT(""), wxITEM_RADIO);
	wmi2->Check(tw->flags.Get('u'));
	AppendToTAMIMenuMap(map, nextid, TAMI_MARKUNREAD, tw);
	wxMenuItem *wmi3 = menuP->Append(nextid, wxT("Neither"), wxT(""), wxITEM_RADIO);
	wmi3->Check(!tw->flags.Get('r') && !tw->flags.Get('u'));
	AppendToTAMIMenuMap(map, nextid, TAMI_MARKNOREADSTATE, tw);
	menuP->AppendSeparator();
	wxMenuItem *wmi4 = menuP->Append(nextid, wxT("Highlighted"), wxT(""), wxITEM_CHECK);
	wmi4->Check(tw->flags.Get('H'));
	AppendToTAMIMenuMap(map, nextid, TAMI_TOGGLEHIGHLIGHT, tw);
	wxMenuItem *wmi6 = menuP->Append(nextid, wxT("Hidden"), wxT(""), wxITEM_CHECK);
	wmi6->Check(tw->flags.Get('h'));
	AppendToTAMIMenuMap(map, nextid, TAMI_TOGGLEHIDDEN, tw);
}

void MakeTPanelMarkMenu(wxMenu *menuP, tweetactmenudata &map, int &nextid, tweet_ptr_p tw, tpanelparentwin_nt *tppw) {
	size_t pos = menuP->GetMenuItemCount();

	uint64_t twid = tw->id;
	const std::shared_ptr<tpanel> &tp = tppw->GetTP();

	if(!tp->cids.unreadids.empty()) {
		if(twid < *(tp->cids.unreadids.begin())) {
			menuP->Append(nextid, wxT("Mark Newer Read \x21A5"));
			AppendToTAMIMenuMap(map, nextid, TAMI_MARKNEWERUNREAD, tw, 0, udc_ptr(), 0, wxT(""), tppw);
		}
		if(twid > *(tp->cids.unreadids.rbegin())) {
			menuP->Append(nextid, wxT("Mark Older Read \x21A7"));
			AppendToTAMIMenuMap(map, nextid, TAMI_MARKOLDERUNREAD, tw, 0, udc_ptr(), 0, wxT(""), tppw);
		}
	}
	if(!tp->cids.highlightids.empty()) {
		if(twid < *(tp->cids.highlightids.begin())) {
			menuP->Append(nextid, wxT("Unhighlight Newer \x21A5"));
			AppendToTAMIMenuMap(map, nextid, TAMI_MARKNEWERUNHIGHLIGHTED, tw, 0, udc_ptr(), 0, wxT(""), tppw);
		}
		if(twid > *(tp->cids.highlightids.rbegin())) {
			menuP->Append(nextid, wxT("Unhighlight Older \x21A7"));
			AppendToTAMIMenuMap(map, nextid, TAMI_MARKOLDERUNHIGHLIGHTED, tw, 0, udc_ptr(), 0, wxT(""), tppw);
		}
	}

	if(pos != menuP->GetMenuItemCount()) menuP->InsertSeparator(pos);
}

void MakeImageMenu(wxMenu *menuP, tweetactmenudata &map, int &nextid, tweet_ptr_p tw) {
	wxMenuItem *wmi5 = menuP->Append(nextid, wxT("Image Previews Hidden"), wxT(""), wxITEM_CHECK);
	wmi5->Check(tw->flags.Get('p'));
	AppendToTAMIMenuMap(map, nextid, TAMI_TOGGLEHIDEIMG, tw);
	wxMenuItem *wmi6 = menuP->Append(nextid, wxT("No Image Preview Auto-Download"), wxT(""), wxITEM_CHECK);
	wmi6->Check(tw->flags.Get('n'));
	AppendToTAMIMenuMap(map, nextid, TAMI_TOGGLEIMGPREVIEWNOAUTOLOAD, tw);

	std::vector<media_entity *> mes;
	tw->GetMediaEntities(mes, MEF::HAVE_THUMB | MEF::HAVE_FULL);
	if(!mes.empty()) {
		menuP->AppendSeparator();
		menuP->Append(nextid, wxT("Delete Cached Image data"));
		AppendToTAMIMenuMap(map, nextid, TAMI_DELETECACHEDIMG, tw);
	}
}

void MakeDebugMenu(wxMenu *menuP, tweetactmenudata &map, int &nextid, tweet_ptr_p tw) {
	if(tw->flags.Get('T')) {
		menuP->Append(nextid, wxT("Force reload"));
		AppendToTAMIMenuMap(map, nextid, TAMI_DBG_FORCERELOAD, tw);
		menuP->Append(nextid, wxT("Copy debug info"));
		AppendToTAMIMenuMap(map, nextid, TAMI_COPY_DEBUG_INFO, tw);
	}
}

void TweetActMenuAction(tweetactmenudata &map, int curid, mainframe *mainwin) {
	unsigned int dbindex = map[curid].dbindex;
	std::shared_ptr<taccount> *acc = nullptr;
	if(dbindex) {
		for(auto &it : alist) {
			if(it->dbindex == dbindex) {
				acc = &it;
				break;
			}
		}
	}

	using STYPE = twitcurlext_simple::CONNTYPE;
	auto simple_action = [&](STYPE type) {
		if(acc && *acc) {
			std::unique_ptr<twitcurlext_simple> twit = twitcurlext_simple::make_new(*acc, type);
			twit->extra_id = map[curid].tw->id;
			twitcurlext::QueueAsyncExec(std::move(twit));
		}
	};

	switch(map[curid].type) {
		case TAMI_REPLY:
			if(mainwin)
				mainwin->tpw->SetReplyTarget(map[curid].tw);
			break;
		case TAMI_DM:
			if(mainwin)
				mainwin->tpw->SetDMTarget(map[curid].user, map[curid].tw);
			break;
		case TAMI_RETWEET:
			simple_action(STYPE::RT);
			break;
		case TAMI_FAV:
			simple_action(STYPE::FAV);
			break;
		case TAMI_UNFAV:
			simple_action(STYPE::UNFAV);
			break;
		case TAMI_DELETE: {
			if(map[curid].tw->flags.Get('D'))
				simple_action(STYPE::DELETEDM);
			else
				simple_action(STYPE::DELETETWEET);
			break;
		}
		case TAMI_COPYLINK: {
			std::string url = map[curid].tw->GetPermalink();
			if(url.size()) {
				if(wxTheClipboard->Open()) {
					wxTheClipboard->SetData(new wxTextDataObject(wxstrstd(url)));
					wxTheClipboard->Close();
				}
			}
			break;
		}
		case TAMI_BROWSER: {
			std::string url = map[curid].tw->GetPermalink();
			if(url.size()) {
				::wxLaunchDefaultBrowser(wxstrstd(url));
			}
			break;
		}
		case TAMI_COPYTEXT: {
			if(wxTheClipboard->Open()) {
				wxTheClipboard->SetData(new wxTextDataObject(wxstrstd(map[curid].tw->text)));
				wxTheClipboard->Close();
			}
			break;
		}
		case TAMI_COPYRTTEXT: {
			if(wxTheClipboard->Open()) {
				wxTheClipboard->SetData(new wxTextDataObject(wxstrstd(map[curid].tw->rtsrc->text)));
				wxTheClipboard->Close();
			}
			break;
		}
		case TAMI_COPYID: {
			if(wxTheClipboard->Open()) {
				wxTheClipboard->SetData(new wxTextDataObject(wxString::Format(wxT("%") wxLongLongFmtSpec wxT("d"), map[curid].tw->id)));
				wxTheClipboard->Close();
			}
			break;
		}
		case TAMI_COPYEXTRA: {
			if(wxTheClipboard->Open()) {
				wxTheClipboard->SetData(new wxTextDataObject(map[curid].extra));
				wxTheClipboard->Close();
			}
			break;
		}
		case TAMI_BROWSEREXTRA: {
			::wxLaunchDefaultBrowser(map[curid].extra);
			break;
		}
		case TAMI_MEDIAWIN: {
			media_id_type media_id = ParseMediaID(map[curid].extra);
			if(ad.media_list[media_id]->win) {
				ad.media_list[media_id]->win->Raise();
			}
			else new media_display_win(mainwin, media_id);
			break;
		}
		case TAMI_USERWINDOW: {
			std::shared_ptr<taccount> acc_hint;
			if(map[curid].tw) map[curid].tw->GetUsableAccount(acc_hint);
			user_window::MkWin(map[curid].user->id, acc_hint);
			break;
		}
		case TAMI_TOGGLEHIGHLIGHT: {
			map[curid].tw->flags.Toggle('H');
			map[curid].tw->CheckFlagsUpdated(tweet::CFUF::SEND_DB_UPDATE | tweet::CFUF::UPDATE_TWEET);
			break;
		}
		case TAMI_MARKREAD: {
			map[curid].tw->MarkFlagsAsRead();
			map[curid].tw->CheckFlagsUpdated(tweet::CFUF::SEND_DB_UPDATE | tweet::CFUF::UPDATE_TWEET);
			break;
		}
		case TAMI_MARKUNREAD: {
			map[curid].tw->MarkFlagsAsUnread();
			map[curid].tw->CheckFlagsUpdated(tweet::CFUF::SEND_DB_UPDATE | tweet::CFUF::UPDATE_TWEET);
			break;
		}
		case TAMI_MARKNOREADSTATE: {
			map[curid].tw->flags.Set('r', false);
			map[curid].tw->flags.Set('u', false);
			map[curid].tw->CheckFlagsUpdated(tweet::CFUF::SEND_DB_UPDATE | tweet::CFUF::UPDATE_TWEET);
			break;
		}
		case TAMI_MARKNEWERUNREAD: {
			tpanelparentwin_nt *tppw = static_cast<tpanelparentwin_nt *>(map[curid].ppwb);
			if(tppw->GetTP()->cids.unreadids.empty()) break;
			tweetidset subset;
			SpliceTweetIDSet(tppw->GetTP()->cids.unreadids, subset, *(tppw->GetTP()->cids.unreadids.begin()), map[curid].tw->id + 1, true);
			tppw->MarkSetRead(std::move(subset), tppw->GetTP()->MakeUndoItem("mark newer unread"));
			break;
		}
		case TAMI_MARKOLDERUNREAD: {
			tpanelparentwin_nt *tppw = static_cast<tpanelparentwin_nt *>(map[curid].ppwb);
			if(tppw->GetTP()->cids.unreadids.empty()) break;
			tweetidset subset;
			SpliceTweetIDSet(tppw->GetTP()->cids.unreadids, subset, map[curid].tw->id - 1, 0, true);
			tppw->MarkSetRead(std::move(subset), tppw->GetTP()->MakeUndoItem("mark older unread"));
			break;
		}
		case TAMI_MARKNEWERUNHIGHLIGHTED: {
			tpanelparentwin_nt *tppw = static_cast<tpanelparentwin_nt *>(map[curid].ppwb);
			if(tppw->GetTP()->cids.highlightids.empty()) break;
			tweetidset subset;
			SpliceTweetIDSet(tppw->GetTP()->cids.highlightids, subset, *(tppw->GetTP()->cids.highlightids.begin()), map[curid].tw->id + 1, true);
			tppw->MarkSetUnhighlighted(std::move(subset), tppw->GetTP()->MakeUndoItem("unhighlighted newer"));
			break;
		}
		case TAMI_MARKOLDERUNHIGHLIGHTED: {
			tpanelparentwin_nt *tppw = static_cast<tpanelparentwin_nt *>(map[curid].ppwb);
			if(tppw->GetTP()->cids.highlightids.empty()) break;
			tweetidset subset;
			SpliceTweetIDSet(tppw->GetTP()->cids.highlightids, subset, map[curid].tw->id - 1, 0, true);
			tppw->MarkSetUnhighlighted(std::move(subset), tppw->GetTP()->MakeUndoItem("unhighlighted older"));
			break;
		}
		case TAMI_TOGGLEHIDEIMG: {
			map[curid].tw->flags.Toggle('p');
			map[curid].tw->CheckFlagsUpdated(tweet::CFUF::SEND_DB_UPDATE | tweet::CFUF::UPDATE_TWEET);
			break;
		}
		case TAMI_TOGGLEIMGPREVIEWNOAUTOLOAD: {
			map[curid].tw->flags.Toggle('n');
			map[curid].tw->CheckFlagsUpdated(tweet::CFUF::SEND_DB_UPDATE | tweet::CFUF::UPDATE_TWEET);
			break;
		}
		case TAMI_DELETECACHEDIMG: {
			std::vector<media_entity *> mes;
			map[curid].tw->GetMediaEntities(mes, MEF::HAVE_THUMB | MEF::HAVE_FULL);
			std::map<uint64_t, tweet_ptr> tweetupdates;
			for(auto &me : mes) {
				me->PurgeCache();
				for(auto &it : me->tweet_list) tweetupdates[it->id] = it;
			}
			for(auto &it : tweetupdates) {
				if(it.first != map[curid].tw->id) { //don't update current tweet twice
					UpdateTweet(*it.second);
				}
			}
			UpdateTweet(*(map[curid].tw), false);
			CheckClearNoUpdateFlag_All();
			break;
		}
		case TAMI_TOGGLEHIDDEN: {
			map[curid].tw->flags.Toggle('h');
			map[curid].tw->CheckFlagsUpdated(tweet::CFUF::SEND_DB_UPDATE | tweet::CFUF::UPDATE_TWEET);
			break;
		}
		case TAMI_ADDTOPANEL: {
			std::shared_ptr<tpanel> tp = ad.tpanels[stdstrwx(map[curid].extra)];
			if(tp) tp->PushTweet(map[curid].tw);
			break;
		}
		case TAMI_REMOVEFROMPANEL: {
			std::shared_ptr<tpanel> tp = ad.tpanels[stdstrwx(map[curid].extra)];
			if(tp) tp->RemoveTweet(map[curid].tw->id);
			break;
		}
		case TAMI_DMSETPANEL: {
			if(mainwin) {
				auto tp = tpanel::MkTPanel("", "", TPF::DELETEONWINCLOSE, {}, { { TPFU::DMSET, map[curid].user } });
				tp->MkTPanelWin(mainwin, true);
			}
			break;
		}
		case TAMI_DBG_FORCERELOAD: {
			std::shared_ptr<taccount> acc_hint;
			if(acc) acc_hint = *acc;
			if(map[curid].tw->GetUsableAccount(acc_hint, tweet::GUAF::CHECKEXISTING)) {
				std::unique_ptr<twitcurlext_simple> twit = twitcurlext_simple::make_new(acc_hint, STYPE::SINGLETWEET);
				twit->extra_id = map[curid].tw->id;
				twit->tc_flags |= twitcurlext::TCF::ALWAYSREPARSE;
				twitcurlext::QueueAsyncExec(std::move(twit));
			}
			else {
				LogMsgFormat(LOGT::OTHERERR, "TAMI_DBG_FORCERELOAD: Cannot lookup tweet: id: %" llFmtSpec "d.", map[curid].tw->id);
			}
			break;
		}
		case TAMI_COPY_DEBUG_INFO: {
			if(wxTheClipboard->Open()) {
				wxTheClipboard->SetData(new wxTextDataObject(wxstrstd(tweet_long_log_line(map[curid].tw.get()))));
				wxTheClipboard->Close();
			}
			break;
		}
		case TAMI_NULL: {
			break;
		}
	}
}

wxString getreltimestr(time_t timestamp, time_t &updatetime) {
	time_t nowtime = time(nullptr);
	if(timestamp > nowtime) {
		updatetime = 30 + timestamp-nowtime;
		return wxT("In the future");
	}
	time_t diff = nowtime-timestamp;
	if(diff < 60) {
		updatetime = nowtime + 60;
		return wxT("< 1 minute ago");
	}
	diff /= 60;
	if(diff < 120) {
		updatetime = nowtime + 60;
		return wxString::Format(wxT("%d minute%s ago"), diff, (diff != 1) ? wxT("s") : wxT(""));
	}
	diff /= 60;
	if(diff < 48) {
		updatetime = nowtime + 60 * 30;
		return wxString::Format(wxT("%d hour%s ago"), diff, (diff != 1) ? wxT("s") : wxT(""));
	}
	diff /= 24;
	if(diff < 30) {
		updatetime = nowtime + 60 * 60 * 2;
		return wxString::Format(wxT("%d day%s ago"), diff, (diff != 1) ? wxT("s") : wxT(""));
	}
	diff /= 30;
	if(diff < 12) {
		updatetime = nowtime + 60 * 60 * 24 * 2;
		return wxString::Format(wxT("%d month%s ago"), diff, (diff != 1) ? wxT("s") : wxT(""));
	}
	diff /= 12;
	updatetime = nowtime + 60 * 60 * 24 * 2;
	return wxString::Format(wxT("%d year%s ago"), diff, (diff != 1) ? wxT("s") : wxT(""));

}

wxColour ColourOp(const wxColour &in, const wxColour &delta, COLOUR_OP co) {

	if(co == CO_SET) return delta;
	if(co == CO_AND) {
		return wxColour(in.Red() & delta.Red(), in.Green() & delta.Green(), in.Blue() & delta.Blue());
	}
	if(co == CO_OR) {
		return wxColour(in.Red() | delta.Red(), in.Green() | delta.Green(), in.Blue() | delta.Blue());
	}

	double br = in.Red();
	double bg = in.Green();
	double bb = in.Blue();

	if(co == CO_ADD) {
		br += delta.Red();
		bg += delta.Green();
		bb += delta.Blue();
	}
	else if(co == CO_SUB) {
		br -= delta.Red();
		bg -= delta.Green();
		bb -= delta.Blue();
	}
	else if(co == CO_RSUB) {
		br = delta.Red() - br;
		bg = delta.Green() - bg;
		bb = delta.Blue() - bb;
	}

	return NormaliseColour(br, bg, bb);
}

wxColour NormaliseColour(double br, double bg, double bb) {
	double min = std::min({br, bg, bb});
	if(min < 0) {
		br -= min;
		bg -= min;
		bb -= min;
	}

	double max = std::max({br, bg, bb});
	if(max > 255) {
		double factor = 255.0 / max;
		br *= factor;
		bg *= factor;
		bb *= factor;
	}
	return wxColour((unsigned char) br, (unsigned char) bg, (unsigned char) bb);
}

wxColour ColourOp(const wxColour &in, const wxString &co_str) {
	COLOUR_OP co = CO_SET;
	size_t i = 0;
	size_t start = 0;
	wxColour out = in;
	auto flush = [&]() {
		if(i > start) {
			out = ColourOp(out, wxColour(co_str.Mid(start, i - start)), co);
		}
		start = i + 1;
	};
	for(; i < co_str.size(); i++) {
		wxChar c = co_str[i];
		switch(c) {
			case '=': {
				flush();
				co = CO_SET;
				break;
			}
			case '+': {
				flush();
				co = CO_ADD;
				break;
			}
			case '-': {
				flush();
				co = CO_SUB;
				break;
			}
			case '&': {
				flush();
				co = CO_AND;
				break;
			}
			case '|': {
				flush();
				co = CO_OR;
				break;
			}
			case '~': {
				flush();
				co = CO_RSUB;
				break;
			}
			default: {
				break;
			}
		}
	}
	flush();
	return out;
}

void GenericPopupWrapper(wxWindow *win, wxMenu *menu, const wxPoint& pos) {
	LogMsgFormat(LOGT::TPANELTRACE, "About to popup menu: %p, win: %p, recursion: %d", menu, win, wxGetApp().popuprecursion);
	wxGetApp().popuprecursion++;

	generic_popup_wrapper_hook *hook = dynamic_cast<generic_popup_wrapper_hook *>(win);
	if(hook) hook->BeforePopup();

	bool result = win->PopupMenu(menu, pos);

	if(hook) hook->AfterPopup();

	wxGetApp().popuprecursion--;
	LogMsgFormat(LOGT::TPANELTRACE, "Finished popup menu: %p, win: %p, recursion: %d, result: %d", menu, win, wxGetApp().popuprecursion, result);
}

void DestroyMenuContents(wxMenu *menu) {
	wxMenuItemList items = menu->GetMenuItems();    //make a copy to avoid memory issues if Destroy modifies the list
	for(auto &it : items) {
		menu->Destroy(it);
	}
}

extern wxArrayInt g_GlobalPartialTextExtents;
extern bool g_UseGlobalPartialTextExtents;

// This is a (very) cut down copy-and-paste version of wxRichTextImage
// It uses ref-counted wxBitmaps, instead of having ~3 representations of an image,
// per instance, which wastes a lot of memory and time
struct AltTextRichTextBitmap : public wxRichTextObject {
	wxString altText;
	wxBitmap bmp;

	AltTextRichTextBitmap(wxBitmap bmp_, wxRichTextObject* parent, wxTextAttrEx* charStyle, const wxString &altText_)
			: wxRichTextObject(parent), altText(altText_), bmp(bmp_) {
		if(charStyle)
			SetAttributes(*charStyle);
	}

	AltTextRichTextBitmap(const AltTextRichTextBitmap& obj)
			: wxRichTextObject() {
		Copy(obj);
		altText = obj.altText;
		bmp = obj.bmp;
	}

	virtual wxRichTextObject* Clone() const override {
		return new AltTextRichTextBitmap(*this);
	}

	virtual wxString GetTextForRange(const wxRichTextRange& range) const override {
		return altText;
	}

	virtual bool Draw(wxDC& dc, const wxRichTextRange& range, const wxRichTextRange& selectionRange, const wxRect& rect, int descent, int style) override {
		if(!bmp.Ok())
			return false;

		int y = rect.y + (rect.height - bmp.GetHeight());
		dc.DrawBitmap(bmp, rect.x, y, true);

		if(selectionRange.Contains(range.GetStart())) {
			dc.SetBrush(*wxBLACK_BRUSH);
			dc.SetPen(*wxBLACK_PEN);
			dc.SetLogicalFunction(wxINVERT);
			dc.DrawRectangle(rect);
			dc.SetLogicalFunction(wxCOPY);
		}

		return true;
	}

	virtual bool Layout(wxDC& dc, const wxRect& rect, int style) override {
		if(bmp.Ok()) {
			SetCachedSize(wxSize(bmp.GetWidth(), bmp.GetHeight()));
			SetPosition(rect.GetPosition());
		}
		return true;
	}

	virtual bool GetRangeSize(const wxRichTextRange& range, wxSize& size, int& descent, wxDC& dc, int flags, wxPoint position = wxPoint(0,0)) const override {
		if(!range.IsWithin(GetRange()))
			return false;

		if (g_UseGlobalPartialTextExtents)
		{
			// Now add this child's extents to the global extents
			int lastExtent = 0;
			if (g_GlobalPartialTextExtents.GetCount() > 0)
				lastExtent = g_GlobalPartialTextExtents[g_GlobalPartialTextExtents.GetCount()-1];

			int thisExtent;

			if (bmp.Ok())
				thisExtent = lastExtent + bmp.GetWidth();
			else
				thisExtent = lastExtent;

			g_GlobalPartialTextExtents.Add(thisExtent);
		}

		if(!bmp.Ok())
			return false;

		size.x = bmp.GetWidth();
		size.y = bmp.GetHeight();

		return true;
	}

	virtual bool IsEmpty() const override {
		return !bmp.Ok();
	}
};

BEGIN_EVENT_TABLE(commonRichTextCtrl, wxRichTextCtrl)
#if HANDLE_PRIMARY_CLIPBOARD
	EVT_LEFT_UP(commonRichTextCtrl::OnLeftUp)
	EVT_MIDDLE_DOWN(commonRichTextCtrl::OnMiddleClick)
#endif
END_EVENT_TABLE()

commonRichTextCtrl::commonRichTextCtrl(wxWindow *parent_, wxWindowID id, const wxString &text, long style)
	: wxRichTextCtrl(parent_, id, text, wxPoint(-1000, -1000), wxDefaultSize, style) { }

// This is based on wxRichTextBuffer::InsertImageWithUndo
void commonRichTextCtrl::WriteBitmapAltText(const wxBitmap& bmp, const wxString &altText) {
	long pos = GetCaretPosition() + 1;

	wxRichTextAction* action = new wxRichTextAction(NULL, wxT("Insert Image"), wxRICHTEXT_INSERT, &(GetBuffer()), this, false);

	wxTextAttrEx attr(GetDefaultStyle());
	wxRichTextParagraph* newPara = new wxRichTextParagraph(&(GetBuffer()), &attr);

	wxRichTextObject* imageObject = new AltTextRichTextBitmap(bmp, newPara, nullptr, altText);
	newPara->AppendChild(imageObject);
	action->GetNewParagraphs().AppendChild(newPara);
	action->GetNewParagraphs().UpdateRanges();

	action->GetNewParagraphs().SetPartialParagraph(true);

	action->SetPosition(pos);

	// Set the range we'll need to delete in Undo
	action->SetRange(wxRichTextRange(pos, pos));

	GetBuffer().SubmitAction(action);
}

void commonRichTextCtrl::EnableEmojiChecking(bool enabled) {
	if(!emojiCheckingEnabled && enabled) {
		Connect(wxEVT_COMMAND_RICHTEXT_CONTENT_INSERTED, wxRichTextEventHandler(commonRichTextCtrl::EmojiCheckContentInsertionEventHandler));
		Connect(wxEVT_COMMAND_RICHTEXT_CHARACTER, wxRichTextEventHandler(commonRichTextCtrl::EmojiCheckCharEventHandler));
	}
	if(emojiCheckingEnabled && !enabled) {
		Disconnect(wxEVT_COMMAND_RICHTEXT_CONTENT_INSERTED, wxRichTextEventHandler(commonRichTextCtrl::EmojiCheckContentInsertionEventHandler));
		Disconnect(wxEVT_COMMAND_RICHTEXT_CHARACTER, wxRichTextEventHandler(commonRichTextCtrl::EmojiCheckCharEventHandler));
	}

	emojiCheckingEnabled = enabled;
}

void commonRichTextCtrl::EmojiCheckContentInsertionEventHandler(wxRichTextEvent &event) {
	wxRichTextRange range = event.GetRange();
	EmojiCheckRange(range.GetStart(), range.GetEnd());
}

void commonRichTextCtrl::EmojiCheckCharEventHandler(wxRichTextEvent &event) {
	EmojiCheckRange(event.GetPosition(), event.GetPosition() + 1);
}

void commonRichTextCtrl::EmojiCheckRange(long start, long end) {
	if(gc.emoji_mode == EMOJI_MODE::OFF)
		return;

	bool needsUpdating = false;
	auto tpg = tpanelglobal::Get();
	EmojiParseString(
		stdstrwx(GetRange(start, end)),
		gc.emoji_mode,
		tpg->emoji,
		[&](std::string text) { },
		[&](wxBitmap img, std::string altText) {
			needsUpdating = true;
		}
	);
	// At least one emoji added, for simplicitly just refresh the whole contents
	// Set caret to end of inserted range
	if(needsUpdating) {
		MoveCaret(end - 1);
		ReplaceAllEmoji();
	}
}

void commonRichTextCtrl::ReplaceAllEmoji() {
	Freeze();
	long caret = GetCaretPosition();
	std::string text = stdstrwx(GetValue());
	Clear();
	WriteToRichTextCtrlWithEmojis(*this, text);
	MoveCaret(caret);
	Thaw();
}

#if HANDLE_PRIMARY_CLIPBOARD
// This is effectively a backport of http://trac.wxwidgets.org/changeset/70011

void commonRichTextCtrl::OnLeftUp(wxMouseEvent& event) {
	wxTheClipboard->UsePrimarySelection(true);
	Copy();
	wxTheClipboard->UsePrimarySelection(false);

	// Propagate
	event.Skip(true);
}

void commonRichTextCtrl::OnMiddleClick(wxMouseEvent& event) {
	if(IsEditable()) {
		wxTheClipboard->UsePrimarySelection(true);
		Paste();
		wxTheClipboard->UsePrimarySelection(false);
	}
	else {
		// Propagate
		event.Skip(true);
	}
}

#endif

settings_changed_notifier::settings_changed_notifier() {
	container.insert(this);
}

void settings_changed_notifier::NotifyAll() {
	for(auto &it : container) {
		it->NotifySettingsChanged();
	}
}

magic_ptr_container<settings_changed_notifier> settings_changed_notifier::container;

BEGIN_EVENT_TABLE(rounded_box_panel, wxPanel)
	EVT_PAINT(rounded_box_panel::OnPaint)
	EVT_SIZE(rounded_box_panel::OnSize)
	EVT_MOUSEWHEEL(rounded_box_panel::OnMouseWheel)
END_EVENT_TABLE()

rounded_box_panel::rounded_box_panel(wxWindow* parent, int border_radius_, int horiz_margins_, int vert_margins_)
		: wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxCLIP_CHILDREN),
		border_radius(border_radius_), horiz_margins(horiz_margins_), vert_margins(vert_margins_) {
	fillBrush = wxBrush(GetBackgroundColour());
	linePen = wxPen(GetForegroundColour());
}

void rounded_box_panel::OnPaint(wxPaintEvent &event) {
	wxPaintDC dc(this);
	dc.SetBrush(fillBrush);
	dc.SetPen(linePen);
	dc.DrawRoundedRectangle(horiz_margins, vert_margins,
			GetSize().GetWidth() - (2 * horiz_margins), GetSize().GetHeight() - (2 * vert_margins), border_radius);
}

void rounded_box_panel::OnSize(wxSizeEvent &event) {
	if(event.GetSize() != prev_size) {
		prev_size = event.GetSize();
		Refresh();
	}
	event.Skip();
}

void rounded_box_panel::OnMouseWheel(wxMouseEvent &event) {
	event.SetEventObject(GetParent());
	GetParent()->GetEventHandler()->ProcessEvent(event);
}
