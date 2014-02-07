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
#include "uiutil.h"
#include "version.h"
#include "twit.h"
#include "twitcurlext.h"
#include "taccount.h"
#include "tpanel.h"
#include "tpanel-data.h"
#include "userui.h"
#include "mediawin.h"
#include "mainui.h"
#include "alldata.h"
#include "util.h"
#include "log.h"
#include "retcon.h"
#include <wx/colour.h>
#include <wx/clipbrd.h>
#include <wx/dataobj.h>
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
		auto tp=tpanel::MkTPanel(twld.name, twld.dispname, twld.flags, twld.tpautos);
		tpanelparentwin *tpw = tp->MkTPanelWin(mf, (twld.splitindex > lastsplitindex));

		if(twld.splitindex > lastsplitindex) {
			mf->auib->Split(mf->auib->GetPageIndex(tpw), wxRIGHT);
			lastsplitindex = twld.splitindex;
		}
	}
	ThawAll();
}



media_id_type ParseMediaID(wxString url) {
	unsigned int i=1;
	media_id_type media_id;
	for(; i<url.Len(); i++) {
		if(url[i]>='0' && url[i]<='9') {
			media_id.m_id*=10;
			media_id.m_id+=url[i]-'0';
		}
		else break;
	}
	if(url[i]!='_') return media_id;
	for(i++; i<url.Len(); i++) {
		if(url[i]>='0' && url[i]<='9') {
			media_id.t_id*=10;
			media_id.t_id+=url[i]-'0';
		}
		else break;
	}
	return media_id;
}

uint64_t ParseUrlID(wxString url) {
	uint64_t id=0;
	for(unsigned int i=1; i<url.Len(); i++) {
		if(url[i]>='0' && url[i]<='9') {
			id*=10;
			id+=url[i]-'0';
		}
		else break;
	}
	return id;
}

void AppendToTAMIMenuMap(tweetactmenudata &map, int &nextid, TAMI_TYPE type, std::shared_ptr<tweet> tw, unsigned int dbindex, std::shared_ptr<userdatacontainer> user,
		flagwrapper<TPF> flags, wxString extra, panelparentwin_base *ppwb) {
	map[nextid]={tw, user, type, dbindex, flags, extra, ppwb};
	nextid++;
}

void MakeRetweetMenu(wxMenu *menuP, tweetactmenudata &map, int &nextid, const std::shared_ptr<tweet> &tw) {
	for(auto it=alist.begin(); it!=alist.end(); ++it) {
		wxMenuItem *menuitem=menuP->Append(nextid, (*it)->dispname);
		menuitem->Enable((*it)->enabled);
		AppendToTAMIMenuMap(map, nextid, TAMI_RETWEET, tw, (*it)->dbindex);
	}
}

void MakeFavMenu(wxMenu *menuP, tweetactmenudata &map, int &nextid, const std::shared_ptr<tweet> &tw) {
	for(auto it=alist.begin(); it!=alist.end(); ++it) {
		tweet_perspective *tp=tw->GetTweetTP(*it);
		bool known=(tp!=0);
		bool faved=false;
		if(tp && tp->IsFavourited()) faved=true;

		wxMenu *submenu = new wxMenu;
		menuP->AppendSubMenu(submenu, (known?(faved?wxT("\x2713 "):wxT("\x2715 ")):wxT("? ")) + (*it)->dispname);
		submenu->SetTitle(known?(faved?wxT("Favourited"):wxT("Not Favourited")):wxT("Unknown"));

		wxMenuItem *menuitem=submenu->Append(nextid, wxT("Favourite"));
		menuitem->Enable((*it)->enabled && (!known || !faved));
		AppendToTAMIMenuMap(map, nextid, TAMI_FAV, tw, (*it)->dbindex);

		menuitem=submenu->Append(nextid, wxT("Remove Favourite"));
		menuitem->Enable((*it)->enabled && (!known || faved));
		AppendToTAMIMenuMap(map, nextid, TAMI_UNFAV, tw, (*it)->dbindex);
	}
}

void MakeCopyMenu(wxMenu *menuP, tweetactmenudata &map, int &nextid, const std::shared_ptr<tweet> &tw) {
	menuP->Append(nextid, wxT("Copy Text"));
	AppendToTAMIMenuMap(map, nextid, TAMI_COPYTEXT, tw);
	menuP->Append(nextid, wxT("Copy Link to Tweet"));
	AppendToTAMIMenuMap(map, nextid, TAMI_COPYLINK, tw);
	menuP->Append(nextid, wxString::Format(wxT("Copy ID (%" wxLongLongFmtSpec "d)"), tw->id));
	AppendToTAMIMenuMap(map, nextid, TAMI_COPYID, tw);
}

void MakeMarkMenu(wxMenu *menuP, tweetactmenudata &map, int &nextid, const std::shared_ptr<tweet> &tw) {
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

void MakeTPanelMarkMenu(wxMenu *menuP, tweetactmenudata &map, int &nextid, const std::shared_ptr<tweet> &tw, tpanelparentwin_nt *tppw) {
	size_t pos = menuP->GetMenuItemCount();

	uint64_t twid = tw->id;
	const std::shared_ptr<tpanel> &tp = tppw->tp;

	if(!tp->cids.unreadids.empty()) {
		if(twid < *(tp->cids.unreadids.begin())) {
			menuP->Append(nextid, wxT("Mark Newer Read \x21A5"));
			AppendToTAMIMenuMap(map, nextid, TAMI_MARKNEWERUNREAD, tw, 0, std::shared_ptr<userdatacontainer>(), 0, wxT(""), tppw);
		}
		if(twid > *(tp->cids.unreadids.rbegin())) {
			menuP->Append(nextid, wxT("Mark Older Read \x21A7"));
			AppendToTAMIMenuMap(map, nextid, TAMI_MARKOLDERUNREAD, tw, 0, std::shared_ptr<userdatacontainer>(), 0, wxT(""), tppw);
		}
	}
	if(!tp->cids.highlightids.empty()) {
		if(twid < *(tp->cids.highlightids.begin())) {
			menuP->Append(nextid, wxT("Unhighlight Newer \x21A5"));
			AppendToTAMIMenuMap(map, nextid, TAMI_MARKNEWERUNHIGHLIGHTED, tw, 0, std::shared_ptr<userdatacontainer>(), 0, wxT(""), tppw);
		}
		if(twid > *(tp->cids.highlightids.rbegin())) {
			menuP->Append(nextid, wxT("Unhighlight Older \x21A7"));
			AppendToTAMIMenuMap(map, nextid, TAMI_MARKOLDERUNHIGHLIGHTED, tw, 0, std::shared_ptr<userdatacontainer>(), 0, wxT(""), tppw);
		}
	}

	if(pos != menuP->GetMenuItemCount()) menuP->InsertSeparator(pos);
}

void MakeImageMenu(wxMenu *menuP, tweetactmenudata &map, int &nextid, const std::shared_ptr<tweet> &tw) {
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

void TweetActMenuAction(tweetactmenudata &map, int curid, mainframe *mainwin) {
	unsigned int dbindex=map[curid].dbindex;
	std::shared_ptr<taccount> *acc=0;
	if(dbindex) {
		for(auto it=alist.begin(); it!=alist.end(); ++it) {
			if((*it)->dbindex==dbindex) {
				acc=&(*it);
				break;
			}
		}
	}

	CS_ENUMTYPE type=CS_NULL;
	switch(map[curid].type) {
		case TAMI_REPLY: if(mainwin) mainwin->tpw->SetReplyTarget(map[curid].tw); break;
		case TAMI_DM: if(mainwin) mainwin->tpw->SetDMTarget(map[curid].user); break;
		case TAMI_RETWEET: type=CS_RT; break;
		case TAMI_FAV: type=CS_FAV; break;
		case TAMI_UNFAV: type=CS_UNFAV; break;
		case TAMI_DELETE: {
			if(map[curid].tw->flags.Get('D')) type=CS_DELETEDM;
			else type=CS_DELETETWEET;
			break;
		}
		case TAMI_COPYLINK: {
			std::string url=map[curid].tw->GetPermalink();
			if(url.size()) {
				if(wxTheClipboard->Open()) {
					wxTheClipboard->SetData(new wxTextDataObject(wxstrstd(url)));
					wxTheClipboard->Close();
				}
			}
			break;
		}
		case TAMI_BROWSER: {
			std::string url=map[curid].tw->GetPermalink();
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
			media_id_type media_id=ParseMediaID(map[curid].extra);
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
			UpdateSingleTweetHighlightState(map[curid].tw);
			break;
		}
		case TAMI_MARKREAD: {
			map[curid].tw->flags.Set('r', true);
			map[curid].tw->flags.Set('u', false);
			UpdateSingleTweetUnreadState(map[curid].tw);
			break;
		}
		case TAMI_MARKUNREAD: {
			map[curid].tw->flags.Set('r', false);
			map[curid].tw->flags.Set('u', true);
			UpdateSingleTweetUnreadState(map[curid].tw);
			break;
		}
		case TAMI_MARKNOREADSTATE: {
			map[curid].tw->flags.Set('r', false);
			map[curid].tw->flags.Set('u', false);
			UpdateSingleTweetUnreadState(map[curid].tw);
			break;
		}
		case TAMI_MARKNEWERUNREAD: {
			tpanelparentwin_nt *tppw = static_cast<tpanelparentwin_nt *>(map[curid].ppwb);
			if(tppw->tp->cids.unreadids.empty()) break;
			tweetidset subset;
			SpliceTweetIDSet(tppw->tp->cids.unreadids, subset, *(tppw->tp->cids.unreadids.begin()), map[curid].tw->id + 1, true);
			tppw->MarkSetRead(std::move(subset));
			break;
		}
		case TAMI_MARKOLDERUNREAD: {
			tpanelparentwin_nt *tppw = static_cast<tpanelparentwin_nt *>(map[curid].ppwb);
			if(tppw->tp->cids.unreadids.empty()) break;
			tweetidset subset;
			SpliceTweetIDSet(tppw->tp->cids.unreadids, subset, map[curid].tw->id - 1, 0, true);
			tppw->MarkSetRead(std::move(subset));
			break;
		}
		case TAMI_MARKNEWERUNHIGHLIGHTED: {
			tpanelparentwin_nt *tppw = static_cast<tpanelparentwin_nt *>(map[curid].ppwb);
			if(tppw->tp->cids.highlightids.empty()) break;
			tweetidset subset;
			SpliceTweetIDSet(tppw->tp->cids.highlightids, subset, *(tppw->tp->cids.highlightids.begin()), map[curid].tw->id + 1, true);
			tppw->MarkSetUnhighlighted(std::move(subset));
			break;
		}
		case TAMI_MARKOLDERUNHIGHLIGHTED: {
			tpanelparentwin_nt *tppw = static_cast<tpanelparentwin_nt *>(map[curid].ppwb);
			if(tppw->tp->cids.highlightids.empty()) break;
			tweetidset subset;
			SpliceTweetIDSet(tppw->tp->cids.highlightids, subset, map[curid].tw->id - 1, 0, true);
			tppw->MarkSetUnhighlighted(std::move(subset));
			break;
		}
		case TAMI_TOGGLEHIDEIMG: {
			map[curid].tw->flags.Toggle('p');
			UpdateSingleTweetFlagState(map[curid].tw, tweet_flags::GetFlagValue('p'));
			break;
		}
		case TAMI_TOGGLEIMGPREVIEWNOAUTOLOAD: {
			map[curid].tw->flags.Toggle('n');
			UpdateSingleTweetFlagState(map[curid].tw, tweet_flags::GetFlagValue('n'));
			break;
		}
		case TAMI_DELETECACHEDIMG: {
			std::vector<media_entity *> mes;
			map[curid].tw->GetMediaEntities(mes, MEF::HAVE_THUMB | MEF::HAVE_FULL);
			std::map<uint64_t, std::shared_ptr<tweet> > tweetupdates;
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
			UpdateSingleTweetFlagState(map[curid].tw, tweet_flags::GetFlagValue('h'));
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
		case TAMI_NULL: {
			break;
		}
	}
	if(type!=CS_NULL && acc && *acc) {
		twitcurlext *twit=(*acc)->GetTwitCurlExt();
		twit->connmode=type;
		twit->extra_id=map[curid].tw->id;
		twit->QueueAsyncExec();
	}
}

wxString getreltimestr(time_t timestamp, time_t &updatetime) {
	time_t nowtime=time(0);
	if(timestamp>nowtime) {
		updatetime=30+timestamp-nowtime;
		return wxT("In the future");
	}
	time_t diff=nowtime-timestamp;
	if(diff<60) {
		updatetime=nowtime+60;
		return wxT("< 1 minute ago");
	}
	diff/=60;
	if(diff<120) {
		updatetime=nowtime+60;
		return wxString::Format(wxT("%d minute%s ago"), diff, (diff!=1)?wxT("s"):wxT(""));
	}
	diff/=60;
	if(diff<48) {
		updatetime=nowtime+60*30;
		return wxString::Format(wxT("%d hour%s ago"), diff, (diff!=1)?wxT("s"):wxT(""));
	}
	diff/=24;
	if(diff<30) {
		updatetime=nowtime+60*60*2;
		return wxString::Format(wxT("%d day%s ago"), diff, (diff!=1)?wxT("s"):wxT(""));
	}
	diff/=30;
	if(diff<12) {
		updatetime=nowtime+60*60*24*2;
		return wxString::Format(wxT("%d month%s ago"), diff, (diff!=1)?wxT("s"):wxT(""));
	}
	diff/=12;
	updatetime=nowtime+60*60*24*2;
	return wxString::Format(wxT("%d year%s ago"), diff, (diff!=1)?wxT("s"):wxT(""));

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

	double min = std::min({br, bg, bb});
	if(min < 0) {
		br -= min;
		bg -= min;
		bb -= min;
	}

	double max = std::max({br, bg, bb});
	if(max > 255) {
		double factor = 255.0/max;
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
	LogMsgFormat(LOGT::TPANEL, wxT("About to popup menu: %p, win: %p, recursion: %d"), menu, win, wxGetApp().popuprecursion);
	wxGetApp().popuprecursion++;
	bool result = win->PopupMenu(menu, pos);
	wxGetApp().popuprecursion--;
	LogMsgFormat(LOGT::TPANEL, wxT("Finished popup menu: %p, win: %p, recursion: %d, result: %d"), menu, win, wxGetApp().popuprecursion, result);
}
