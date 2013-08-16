//  retcon
//
//  WEBSITE: http://retcon.sourceforge.net
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
//  2013 - j.g.rennison@gmail.com
//==========================================================================

#include "retcon.h"
#include "version.h"
#include <wx/clipbrd.h>

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
		auto tp=tpanel::MkTPanel(twld.name, twld.dispname, twld.flags, &twld.acc);
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

void AppendToTAMIMenuMap(tweetactmenudata &map, int &nextid, TAMI_TYPE type, std::shared_ptr<tweet> tw, unsigned int dbindex, std::shared_ptr<userdatacontainer> user, unsigned int flags, wxString extra) {
	map[nextid]={tw, user, type, dbindex, flags, extra};
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
			if(ad.media_list[media_id].win) {
				ad.media_list[media_id].win->Raise();
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
