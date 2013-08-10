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
//  2012 - j.g.rennison@gmail.com
//==========================================================================

#include "retcon.h"
#include "utf8.h"

#ifndef TPANEL_COPIOUS_LOGGING
#define TPANEL_COPIOUS_LOGGING 0
#endif

BEGIN_EVENT_TABLE(dispscr_base, wxRichTextCtrl)
	EVT_MOUSEWHEEL(dispscr_base::mousewheelhandler)
END_EVENT_TABLE()

dispscr_base::dispscr_base(tpanelscrollwin *parent, panelparentwin_base *tppw_, wxBoxSizer *hbox_)
: wxRichTextCtrl(parent, wxID_ANY, wxEmptyString, wxPoint(-1000, -1000), wxDefaultSize, wxRE_READONLY | wxRE_MULTILINE | wxBORDER_NONE), tppw(tppw_), tpsw(parent), hbox(hbox_) {
	GetCaret()->Hide();
	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LFT_TPANEL, wxT("TCL: dispscr_base::dispscr_base constructor END"));
	#endif
}

void dispscr_base::SetScrollbars(int pixelsPerUnitX, int pixelsPerUnitY,
		       int noUnitsX, int noUnitsY,
		       int xPos, int yPos,
		       bool noRefresh ) {
	//LogMsgFormat(LFT_TPANEL, wxT("tweetdispscr::SetScrollbars, tweet id %" wxLongLongFmtSpec "d"), td->id);
	wxRichTextCtrl::SetScrollbars(0, 0, 0, 0, 0, 0, noRefresh);
	int newheight=(pixelsPerUnitY*noUnitsY)+4;
	hbox->SetItemMinSize(this, 10, newheight);
	//hbox->SetMinSize(10, newheight+4);
	//SetSize(wxDefaultCoord, wxDefaultCoord, wxDefaultCoord, newheight, wxSIZE_USE_EXISTING);
	tpsw->FitInside();
	if(!tpsw->resize_update_pending) {
		tpsw->resize_update_pending=true;
		wxCommandEvent event(wxextRESIZE_UPDATE_EVENT, GetId());
		tpsw->GetEventHandler()->AddPendingEvent(event);
	}
}

BEGIN_EVENT_TABLE(tweetdispscr, dispscr_base)
	EVT_TEXT_URL(wxID_ANY, tweetdispscr::urleventhandler)
	EVT_MENU_RANGE(tweetactmenustartid, tweetactmenuendid, tweetdispscr::OnTweetActMenuCmd)
	EVT_RIGHT_DOWN(tweetdispscr::rightclickhandler)
END_EVENT_TABLE()

tweetdispscr::tweetdispscr(const std::shared_ptr<tweet> &td_, tpanelscrollwin *parent, tpanelparentwin_nt *tppw_, wxBoxSizer *hbox_)
: dispscr_base(parent, tppw_, hbox_), td(td_), bm(0), bm2(0) {
	if(td_->rtsrc) rtid=td_->rtsrc->id;
	else rtid=0;
	default_background_colour = GetBackgroundColour();
	default_foreground_colour = GetForegroundColour();
	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LFT_TPANEL, wxT("TCL: tweetdispscr::tweetdispscr constructor END"));
	#endif
}

tweetdispscr::~tweetdispscr() {
	//tppw->currentdisp.remove_if([this](const std::pair<uint64_t, tweetdispscr *> &p){ return p.second==this; });
}

//use -1 for end to run until end of string
static void DoWriteSubstr(tweetdispscr &td, const std::string &str, int start, int end, int &track_byte, int &track_index, bool trim) {
	while(str[track_byte]) {
		if(track_index==start) break;
		register int charsize=utf8firsttonumbytes(str[track_byte]);
		track_byte+=charsize;
		track_index++;
	}
	int start_offset=track_byte;

	while(str[track_byte]) {
		if(track_index==end) break;
		if(str[track_byte]=='&') {
			char rep=0;
			int delta=0;
			if(str[track_byte+1]=='l' && str[track_byte+2]=='t' && str[track_byte+3]==';') {
				rep='<';
				delta=4;
			}
			else if(str[track_byte+1]=='g' && str[track_byte+2]=='t' && str[track_byte+3]==';') {
				rep='>';
				delta=4;
			}
			else if(str[track_byte+1]=='q' && str[track_byte+2]=='u' && str[track_byte+3]=='o' && str[track_byte+4]=='t' && str[track_byte+5]==';') {
				rep='\'';
				delta=6;
			}
			else if(str[track_byte+1]=='#' && str[track_byte+2]=='3' && str[track_byte+3]=='9' && str[track_byte+4]==';') {
				rep='"';
				delta=5;
			}
			else if(str[track_byte+1]=='a' && str[track_byte+2]=='m' && str[track_byte+3]=='p' && str[track_byte+4]==';') {
				rep='&';
				delta=5;
			}
			if(rep) {
				td.WriteText(wxString::FromUTF8(&str[start_offset], track_byte-start_offset));
				track_index+=delta;
				track_byte+=delta;
				td.WriteText(wxString((wxChar) rep));
				start_offset=track_byte;
				continue;
			}
		}
		register int charsize=utf8firsttonumbytes(str[track_byte]);
		track_byte+=charsize;
		track_index++;
	}
	int end_offset=track_byte;
	wxString wstr=wxString::FromUTF8(&str[start_offset], end_offset-start_offset);
	if(trim) wstr.Trim();
	if(wstr.Len()) td.WriteText(wstr);
}

inline void GenFlush(dispscr_base *obj, wxString &str) {
	if(str.size()) {
		obj->WriteText(str);
		str.clear();
	}
}

void GenUserFmt_OffsetDryRun(size_t &i, const wxString &format) {
	i++;
}

void GenUserFmt(dispscr_base *obj, userdatacontainer *u, size_t &i, const wxString &format, wxString &str) {
	i++;
	if(i>=format.size()) return;
	#if TPANEL_COPIOUS_LOGGING
		wxChar log_formatchar = format[i];
		LogMsgFormat(LFT_TPANEL, wxT("TCL: GenUserFmt Start Format char: %c"), log_formatchar);
	#endif
	switch((wxChar) format[i]) {
		case 'n':
			str+=wxstrstd(u->GetUser().screen_name);
			break;
		case 'N':
			str+=wxstrstd(u->GetUser().name);
			break;
		case 'i':
			str+=wxString::Format("%" wxLongLongFmtSpec "d", u->id);
			break;
		case 'Z': {
			GenFlush(obj, str);
			obj->BeginURL(wxString::Format("U%" wxLongLongFmtSpec "d", u->id), obj->GetDefaultStyleEx().GetCharacterStyleName());
			break;
		}
		case 'p':
			if(u->GetUser().u_flags&UF_ISPROTECTED) {
				GenFlush(obj, str);
				obj->WriteImage(obj->tppw->tpg->proticon_img);
				obj->SetInsertionPointEnd();
			}
			break;
		case 'v':
			if(u->GetUser().u_flags&UF_ISVERIFIED) {
				GenFlush(obj, str);
				obj->WriteImage(obj->tppw->tpg->verifiedicon_img);
				obj->SetInsertionPointEnd();
			}
			break;
		case 'd': {
			GenFlush(obj, str);
			long curpos=obj->GetInsertionPoint();
			obj->BeginURL(wxString::Format(wxT("Xd%" wxLongLongFmtSpec "d"), u->id));
			obj->WriteImage(obj->tppw->tpg->dmreplyicon_img);
			obj->EndURL();
			obj->SetInsertionPointEnd();
			wxTextAttrEx attr(obj->GetDefaultStyleEx());
			attr.SetURL(wxString::Format(wxT("Xd%" wxLongLongFmtSpec "d"), u->id));
			obj->SetStyleEx(curpos, obj->GetInsertionPoint(), attr, wxRICHTEXT_SETSTYLE_OPTIMIZE);
			break;
		}
		case 'w': {
			if(u->GetUser().userurl.size()) {
				GenFlush(obj, str);
				obj->BeginURL(wxT("W") + wxstrstd(u->GetUser().userurl));
				obj->WriteText(wxstrstd(u->GetUser().userurl));
				obj->EndURL();
			}
			break;
		}
		case 'D': {
			str+=wxstrstd(u->GetUser().description);
		}
		case 'l': {
			str+=wxstrstd(u->GetUser().location);
		}
		default:
			break;
	}
	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LFT_TPANEL, wxT("TCL: GenUserFmt End Format char: %c"), log_formatchar);
	#endif
}

void SkipOverFalseCond(size_t &i, const wxString &format) {
	size_t recursion=1;
	for(i++; i<format.size(); i++) {
		switch((wxChar) format[i]) {
			case '(': {
				recursion++;
				break;
			}
			case ')': {
				recursion--;
				if(!recursion) return;
				break;
			}
			case '\'':
			case '"': {
				wxChar quotechar=format[i];
				while(i<format.size()) {
					if(format[i]==quotechar) break;
					i++;
				}
			}
		}
	}
}

bool CondCodeProc(dispscr_base *obj, size_t &i, const wxString &format, wxString &str) {
	i++;
	bool result=false;
	switch((wxChar) format[i]) {
		case 'F': {
			uint64_t any=0;
			uint64_t all=0;
			uint64_t none=0;
			uint64_t missing=0;

			uint64_t *current=&any;

			for(i++; i<format.size(); i++) {
				switch((wxChar) format[i]) {
					case '(': goto loopexit;
					case '+': current=&any; break;
					case '=': current=&all; break;
					case '-': current=&none; break;
					case '/': current=&missing; break;
					default: *current|=tweet_flags::GetFlagValue(format[i]);
				}
			}
			loopexit:

			tweetdispscr *tds=dynamic_cast<tweetdispscr *>(obj);
			if(!tds) break;

			result=true;
			uint64_t curflags=tds->td->flags.Save();
			if(any && !(curflags&any)) result=false;
			if(all && (curflags&all)!=all) result=false;
			if(none && (curflags&none)) result=false;
			if(missing && (curflags|missing)==curflags) result=false;

			break;
		}
	}
	if(format[i]!='(') return false;
	return result;
}

void GenFmtCodeProc(dispscr_base *obj, size_t &i, const wxString &format, wxString &str) {
	#if TPANEL_COPIOUS_LOGGING
		wxChar log_formatchar = format[i];
		LogMsgFormat(LFT_TPANEL, wxT("TCL: GenFmtCodeProc Start Format char: %c"), log_formatchar);
	#endif
	switch((wxChar) format[i]) {
		case 'B': GenFlush(obj, str); obj->BeginBold(); break;
		case 'b': GenFlush(obj, str); obj->EndBold(); break;
		case 'L': GenFlush(obj, str); obj->BeginUnderline(); break;
		case 'l': GenFlush(obj, str); obj->EndUnderline(); break;
		case 'I': GenFlush(obj, str); obj->BeginItalic(); break;
		case 'i': GenFlush(obj, str); obj->EndItalic(); break;
		case 'z': GenFlush(obj, str); obj->EndURL(); break;
		case 'N': GenFlush(obj, str); obj->Newline(); break;
		case 'n': {
			GenFlush(obj, str);
			long y;
			obj->PositionToXY(obj->GetInsertionPoint(), 0, &y);
			if(obj->GetLineLength(y)) obj->Newline();
			break;
		}
		case 'Q': {
			if(!CondCodeProc(obj, i, format, str)) {
				SkipOverFalseCond(i, format);
			}
			break;
		}
		case '\'':
		case '"': {
			auto quotechar=format[i];
			i++;
			while(i<format.size()) {
				if(format[i]==quotechar) break;
				else str+=format[i];
				i++;
			}
			break;
		}
		case ')': break;
		default:
			str+=format[i];
			break;
	}
	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LFT_TPANEL, wxT("TCL: GenFmtCodeProc End Format char: %c"), log_formatchar);
	#endif
}

void tweetdispscr::DisplayTweet(bool redrawimg) {
	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LFT_TPANEL, wxT("TCL: tweetdispscr::DisplayTweet START %" wxLongLongFmtSpec "d, redrawimg: %d"), td->id, redrawimg);
	#endif

	Freeze();
	BeginSuppressUndo();

	updatetime=0;
	std::forward_list<media_entity*> me_list;
	auto last_me=me_list.before_begin();

	tweet &tw=*td;
	userdatacontainer *udc=tw.user.get();
	userdatacontainer *udc_recip=tw.user_recipient.get();

	bool highlight = tw.flags.Get('H');
	if(highlight && !(tds_flags&TDSF_HIGHLIGHT)) {
		double br = default_background_colour.Red();
		double bg = default_background_colour.Green();
		double bb = default_background_colour.Blue();

		br += 50;
		if(br > 255) {
			double factor = 255.0/br;
			br *= factor;
			bg *= factor;
			bb *= factor;
		}

		SetBackgroundColour(wxColour((unsigned char) br, (unsigned char) bg, (unsigned char) bb));
		tds_flags |= TDSF_HIGHLIGHT;
	}
	else if(!highlight && tds_flags&TDSF_HIGHLIGHT) {
		SetBackgroundColour(default_background_colour);
		tds_flags &= ~TDSF_HIGHLIGHT;
	}

	if(redrawimg) {
		#if TPANEL_COPIOUS_LOGGING
			LogMsgFormat(LFT_TPANEL, wxT("TCL: tweetdispscr::DisplayTweet About to redraw images"));
		#endif
		auto updateprofimg = [this](profimg_staticbitmap *b) {
			if(!b) return;
			auto udcp = ad.GetExistingUserContainerById(b->userid);
			if(!udcp) return;
			if(b->pisb_flags & PISBF_HALF) {
				(*udcp)->ImgHalfIsReady(UPDCF_DOWNLOADIMG);
				b->SetBitmap((*udcp)->cached_profile_img_half);
			}
			else {
				bm->SetBitmap((*udcp)->cached_profile_img);
			}
		};
		updateprofimg(bm);
		updateprofimg(bm2);
	}

	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LFT_TPANEL, wxT("TCL: tweetdispscr::DisplayTweet 1"));
	#endif

	Clear();
	wxString format=wxT("");
	wxString str=wxT("");
	if(tw.flags.Get('R') && gc.rtdisp) format=gc.gcfg.rtdispformat.val;
	else if(tw.flags.Get('T')) format=gc.gcfg.tweetdispformat.val;
	else if(tw.flags.Get('D')) format=gc.gcfg.dmdispformat.val;

	auto flush=[&]() {
		GenFlush(this, str);
	};
	auto userfmt=[&](userdatacontainer *u, size_t &i) {
		GenUserFmt(this, u, i, format, str);
	};

	for(size_t i=0; i<format.size(); i++) {
		#if TPANEL_COPIOUS_LOGGING
			wxChar log_formatchar = format[i];
			LogMsgFormat(LFT_TPANEL, wxT("TCL: tweetdispscr::DisplayTweet Start Format char: %c"), log_formatchar);
		#endif
		switch((wxChar) format[i]) {
			case 'u':
				userfmt(udc, i);
				break;
			case 'U':
				if(udc_recip) userfmt(udc_recip, i);
				else i++;
				break;
			case 'r':
				if(tw.rtsrc && gc.rtdisp) userfmt(tw.rtsrc->user.get(), i);
				else userfmt(udc, i);
				break;
			case 'F':
				str+=wxstrstd(tw.flags.GetString());
				break;
			case 't':
				flush();
				reltimestart=GetInsertionPoint();
				WriteText(getreltimestr(tw.createtime, updatetime));
				reltimeend=GetInsertionPoint();
				if(!tppw->tpg->minutetimer.IsRunning()) tppw->tpg->minutetimer.Start(60000, wxTIMER_CONTINUOUS);
				break;
			case 'T':
				str+=rc_wx_strftime(gc.gcfg.datetimeformat.val, localtime(&tw.createtime), tw.createtime, true);
				break;

			case 'C':
			case 'c': {
				flush();
				tweet &twgen=(format[i]=='c' && gc.rtdisp)?*(tw.rtsrc):tw;
				wxString urlcodeprefix=(format[i]=='c' && gc.rtdisp)?wxT("R"):wxT("");
				unsigned int nextoffset=0;
				unsigned int entnum=0;
				int track_byte=0;
				int track_index=0;
				for(auto it=twgen.entlist.begin(); it!=twgen.entlist.end(); it++, entnum++) {
					entity &et=*it;
					DoWriteSubstr(*this, twgen.text, nextoffset, et.start, track_byte, track_index, false);
					BeginUnderline();
					BeginURL(urlcodeprefix + wxString::Format(wxT("%d"), entnum));
					WriteText(wxstrstd(et.text));
					nextoffset=et.end;
					EndURL();
					EndUnderline();
					if((et.type==ENT_MEDIA || et.type==ENT_URL_IMG) && et.media_id) {
						media_entity &me=ad.media_list[et.media_id];
						last_me=me_list.insert_after(last_me, &me);
					}
				}
				DoWriteSubstr(*this, twgen.text, nextoffset, -1, track_byte, track_index, true);
				break;
			}
			case 'X': {
				i++;
				if(i>=format.size()) break;
				flush();
				long curpos=GetInsertionPoint();
				wxString url=wxString::Format(wxT("X%c"), (wxChar) format[i]);
				BeginURL(url);
				bool imginserted=false;
				switch((wxChar) format[i]) {
					case 'i': WriteImage(tppw->tpg->infoicon_img); imginserted=true; break;
					case 'f': {
						if(tw.IsFavouritable()) {
							wxImage *icon=&tppw->tpg->favicon_img;
							tw.IterateTP([&](const tweet_perspective &tp) {
								if(tp.IsFavourited()) {
									icon=&tppw->tpg->favonicon_img;
								}
							});
							WriteImage(*icon);
							imginserted=true;
						}
						break;
					}
					case 'r': WriteImage(tppw->tpg->replyicon_img); imginserted=true; break;
					case 'd': {
						EndURL();
						std::shared_ptr<userdatacontainer> targ=tw.user_recipient;
						if(!targ || targ->udc_flags&UDC_THIS_IS_ACC_USER_HINT) targ=tw.user;
						url=wxString::Format(wxT("Xd%" wxLongLongFmtSpec "d"), targ->id);
						BeginURL(url);
						WriteImage(tppw->tpg->dmreplyicon_img); imginserted=true; break;
					}
					case 't': {
						if(tw.IsRetweetable()) {
							wxImage *icon=&tppw->tpg->retweeticon_img;
							tw.IterateTP([&](const tweet_perspective &tp) {
								if(tp.IsRetweeted()) {
									icon=&tppw->tpg->retweetonicon_img;
								}
							});
							WriteImage(*icon);
							imginserted=true;
						}
						break;
					}
					default: break;
				}
				EndURL();
				if(imginserted) {
					SetInsertionPointEnd();
					wxTextAttrEx attr(GetDefaultStyleEx());
					attr.SetURL(url);
					SetStyleEx(curpos, GetInsertionPoint(), attr, wxRICHTEXT_SETSTYLE_OPTIMIZE);
				}
				break;
			}
			case 'm': {
				i++;
				if(format[i] != '(' || tppw->IsSingleAccountWin() || tds_flags & TDSF_SUBTWEET) {
					SkipOverFalseCond(i, format);
				}
				break;
			}
			case 'A': {
				unsigned int ctr = 0;
				td->IterateTP([&](const tweet_perspective &tp) {
					if(tp.IsArrivedHere()) {
						if(ctr) str += wxT(", ");
						size_t tempi = i;
						userfmt(tp.acc->usercont.get(), tempi);
						ctr++;
					}
				});
				if(!ctr) str += wxT("[No Account]");
				GenUserFmt_OffsetDryRun(i, format);
				break;
			}
			default: {
				GenFmtCodeProc(this, i, format, str);
				break;
			}
		}
		#if TPANEL_COPIOUS_LOGGING
			LogMsgFormat(LFT_TPANEL, wxT("TCL: tweetdispscr::DisplayTweet End Format char: %c"), log_formatchar);
		#endif
	}
	flush();

	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LFT_TPANEL, wxT("TCL: tweetdispscr::DisplayTweet 2"));
	#endif

	if(!me_list.empty()) {
		Newline();
		BeginAlignment(wxTEXT_ALIGNMENT_CENTRE);
		for(auto it=me_list.begin(); it!=me_list.end(); ++it) {
			BeginURL(wxString::Format(wxT("M%" wxLongLongFmtSpec "d_%" wxLongLongFmtSpec "d"), (int64_t) (*it)->media_id.m_id, (int64_t) (*it)->media_id.t_id));
			if((*it)->flags&ME_HAVE_THUMB) {
				AddImage((*it)->thumbimg);
			}
			else {
				BeginUnderline();
				WriteText(wxT("[Image]"));
				EndUnderline();
			}
			EndURL();
		}
		EndAlignment();
	}

	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LFT_TPANEL, wxT("TCL: tweetdispscr::DisplayTweet 3"));
	#endif

	LayoutContent();

	if(!(tppw->tppw_flags&TPPWF_NOUPDATEONPUSH)) {
		#if TPANEL_COPIOUS_LOGGING
			LogMsgFormat(LFT_TPANEL, wxT("TCL: tweetdispscr::DisplayTweet 4 About to call tpsw->FitInside()"));
		#endif
		tpsw->FitInside();
	}
	else {
		#if TPANEL_COPIOUS_LOGGING
			LogMsgFormat(LFT_TPANEL, wxT("TCL: tweetdispscr::DisplayTweet 4"));
		#endif
	}

	EndSuppressUndo();
	Thaw();

	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LFT_TPANEL, wxT("TCL: tweetdispscr::DisplayTweet END %" wxLongLongFmtSpec "d, redrawimg: %d"), td->id, redrawimg);
	#endif
}

void tweetdispscr::urleventhandler(wxTextUrlEvent &event) {
	long start=event.GetURLStart();
	wxRichTextAttr textattr;
	GetStyle(start, textattr);
	wxString url=textattr.GetURL();
	LogMsgFormat(LFT_TPANEL, wxT("URL clicked, id: %s"), url.c_str());
	urlhandler(url);
}

void tweetdispscr::urlhandler(wxString url) {
	if(url[0]=='M') {
		media_id_type media_id=ParseMediaID(url);

		LogMsgFormat(LFT_TPANEL, wxT("Media image clicked, str: %s, id: %" wxLongLongFmtSpec "d/%" wxLongLongFmtSpec "d"), url.Mid(1).c_str(), media_id.m_id, media_id.t_id);
		if(ad.media_list[media_id].win) {
			ad.media_list[media_id].win->Raise();
		}
		else new media_display_win(this, media_id);
	}
	else if(url[0]=='U') {
		uint64_t userid=ParseUrlID(url);
		if(userid) {
			std::shared_ptr<taccount> acc_hint;
			td->GetUsableAccount(acc_hint);
			user_window::MkWin(userid, acc_hint);
		}
	}
	else if(url[0]=='W') {
		::wxLaunchDefaultBrowser(url.Mid(1));
	}
	else if(url[0]=='X') {
		switch((wxChar) url[1]) {
			case 'd': { //send dm
				uint64_t userid;
				ownstrtonum(userid, (wxChar*) &url[2], -1);
				mainframe *mf = tppw->GetMainframe();
				if(!mf && mainframelist.size()) mf = mainframelist.front();
				if(mf) {
					mf->tpw->SetDMTarget(ad.GetUserContainerById(userid));
					if(td->flags.Get('D') && td->lflags & TLF_HAVEFIRSTTP) {
						mf->tpw->accc->TrySetSel(td->first_tp.acc.get());
					}
				}
				break;
			}
			case 'r': {//reply
				mainframe *mf = tppw->GetMainframe();
				if(!mf && mainframelist.size()) mf = mainframelist.front();
				if(mf) mf->tpw->SetReplyTarget(td);
				break;
			}
			case 'f': {//fav
				tamd.clear();
				int nextid=tweetactmenustartid;
				wxMenu menu;
				menu.SetTitle(wxT("Favourite:"));
				MakeFavMenu(&menu, tamd, nextid, td);
				PopupMenu(&menu);
				break;
			}
			case 't': {//retweet
				tamd.clear();
				int nextid=tweetactmenustartid;
				wxMenu menu;
				menu.SetTitle(wxT("Retweet:"));
				MakeRetweetMenu(&menu, tamd, nextid, td);
				PopupMenu(&menu);
				break;
			}
			case 'i': {//info
				tamd.clear();
				int nextid=tweetactmenustartid;
				wxMenu menu;

				menu.Append(nextid, wxT("Reply"));
				AppendToTAMIMenuMap(tamd, nextid, TAMI_REPLY, td);

				std::shared_ptr<userdatacontainer> targ=td->user_recipient;
				if(!targ || targ->udc_flags&UDC_THIS_IS_ACC_USER_HINT) targ=targ=td->user;
				menu.Append(nextid, wxT("Send DM"));
				AppendToTAMIMenuMap(tamd, nextid, TAMI_DM, td, 0, targ);

				menu.Append(nextid, wxT("Open in Browser"));
				AppendToTAMIMenuMap(tamd, nextid, TAMI_BROWSER, td);

				if(td->IsRetweetable()) {
					wxMenu *rtsubmenu = new wxMenu();
					menu.AppendSubMenu(rtsubmenu, wxT("Retweet"));
					MakeRetweetMenu(rtsubmenu, tamd, nextid, td);
				}
				if(td->IsFavouritable()) {
					wxMenu *favsubmenu = new wxMenu();
					menu.AppendSubMenu(favsubmenu, wxT("Favourite"));
					MakeFavMenu(favsubmenu, tamd, nextid, td);
				}
				{
					wxMenu *copysubmenu = new wxMenu();
					menu.AppendSubMenu(copysubmenu, wxT("Copy to Clipboard"));
					MakeCopyMenu(copysubmenu, tamd, nextid, td);
				}
				{
					wxMenu *marksubmenu = new wxMenu();
					menu.AppendSubMenu(marksubmenu, wxT("Mark"));
					MakeMarkMenu(marksubmenu, tamd, nextid, td);
				}

				bool deletable=false;
				bool deletable2=false;
				unsigned int delcount=0;
				unsigned int deldbindex=0;
				unsigned int deldbindex2=0;
				std::shared_ptr<taccount> cacc;
				std::shared_ptr<taccount> cacc2;
				if(td->flags.Get('D')) {
					cacc=td->user_recipient->GetAccountOfUser();
					if(cacc) delcount++;
					if(cacc && cacc->enabled) {
						deletable=true;
						deldbindex=cacc->dbindex;
					}
				}
				cacc2=td->user->GetAccountOfUser();
				if(cacc2) delcount++;
				if(cacc2 && cacc2->enabled) {
					deletable2=true;
					deldbindex2=cacc2->dbindex;
				}
				if(delcount>1) {	//user has DMd another of their own accounts :/
					wxMenuItem *delmenuitem=menu.Append(nextid, wxT("Delete: ") + cacc->dispname);
					delmenuitem->Enable(deletable);
					AppendToTAMIMenuMap(tamd, nextid, TAMI_DELETE, td, deldbindex);
					delmenuitem=menu.Append(nextid, wxT("Delete: ") + cacc2->dispname);
					delmenuitem->Enable(deletable2);
					AppendToTAMIMenuMap(tamd, nextid, TAMI_DELETE, td, deldbindex2);
				}
				else {
					wxMenuItem *delmenuitem=menu.Append(nextid, wxT("Delete"));
					delmenuitem->Enable(deletable|deletable2);
					AppendToTAMIMenuMap(tamd, nextid, TAMI_DELETE, td, deldbindex2?deldbindex2:deldbindex);
				}

				PopupMenu(&menu);
				break;
			}
		}
	}
	else {
		tweet *targtw;
		if(url[0] == 'R') {
			targtw=td->rtsrc.get();
			url=url.Mid(1);
		}
		else {
			targtw = td.get();
		}
		unsigned long counter;
		url.ToULong(&counter);
		auto it=targtw->entlist.begin();
		while(it!=targtw->entlist.end()) {
			if(!counter) {
				//got entity
				entity &et= *it;
				switch(et.type) {
					case ENT_HASHTAG:
						break;
					case ENT_URL:
					case ENT_URL_IMG:
					case ENT_MEDIA:
						::wxLaunchDefaultBrowser(wxstrstd(et.fullurl));
						break;
					case ENT_MENTION: {
						std::shared_ptr<taccount> acc_hint;
						td->GetUsableAccount(acc_hint);
						user_window::MkWin(et.user->id, acc_hint);
						break;
						}
				}
				return;
			}
			else {
				counter--;
				it++;
			}
		}
	}
}

void AppendUserMenuItems(wxMenu &menu, tweetactmenudata &map, int &nextid, std::shared_ptr<userdatacontainer> user, std::shared_ptr<tweet> tw) {
	menu.Append(nextid, wxT("Open User Window"));
	AppendToTAMIMenuMap(map, nextid, TAMI_USERWINDOW, tw, 0, user);

	menu.Append(nextid, wxT("Open Twitter Profile in Browser"));
	AppendToTAMIMenuMap(map, nextid, TAMI_BROWSEREXTRA, tw, 0, user, 0, wxstrstd(user->GetPermalink(tw->flags.Get('s'))));

	menu.Append(nextid, wxT("Send DM"));
	AppendToTAMIMenuMap(map, nextid, TAMI_DM, tw, 0, user);

	wxString username=wxstrstd(user->GetUser().screen_name);
	menu.Append(nextid, wxString::Format(wxT("Copy User Name (%s) to Clipboard"), username.c_str()));
	AppendToTAMIMenuMap(map, nextid, TAMI_COPYEXTRA, tw, 0, user, 0, username);

	wxString useridstr=wxString::Format(wxT("%" wxLongLongFmtSpec "d"), user->id);
	menu.Append(nextid, wxString::Format(wxT("Copy User ID (%s) to Clipboard"), useridstr.c_str()));
	AppendToTAMIMenuMap(map, nextid, TAMI_COPYEXTRA, tw, 0, user, 0, useridstr);
}

void tweetdispscr::rightclickhandler(wxMouseEvent &event) {
	wxPoint mousepoint=event.GetPosition();
	long textpos=0;
	HitTest(mousepoint, &textpos);
	wxRichTextAttr style;
	GetStyle(textpos, style);
	if(style.HasURL()) {
		wxString url=style.GetURL();
		int nextid=tweetactmenustartid;
		wxMenu menu;

		auto urlmenupopup = [&](const wxString &url) {
			wxMenu *submenu = new wxMenu;
			submenu->Append(nextid, url);
			AppendToTAMIMenuMap(tamd, nextid, TAMI_NULL, td, 0, std::shared_ptr<userdatacontainer>(), 0, url);
			submenu->AppendSeparator();
			submenu->Append(nextid, wxT("Copy to Clipboard"));
			AppendToTAMIMenuMap(tamd, nextid, TAMI_COPYEXTRA, td, 0, std::shared_ptr<userdatacontainer>(), 0, url);
			menu.AppendSubMenu(submenu, wxT("URL"));
		};

		if(url[0]=='M') {
			media_id_type media_id=ParseMediaID(url);
			menu.Append(nextid, wxT("Open Media in Window"));
			AppendToTAMIMenuMap(tamd, nextid, TAMI_MEDIAWIN, td, 0, std::shared_ptr<userdatacontainer>(), 0, url);
			urlmenupopup(wxstrstd(ad.media_list[media_id].media_url));
			PopupMenu(&menu);
		}
		else if(url[0]=='U') {
			uint64_t userid=ParseUrlID(url);
			if(userid) {
				std::shared_ptr<userdatacontainer> user=ad.GetUserContainerById(userid);
				AppendUserMenuItems(menu, tamd, nextid, user, td);
				PopupMenu(&menu);
			}
		}
		else if(url[0]=='W') {
			menu.Append(nextid, wxT("Open URL in Browser"));
			AppendToTAMIMenuMap(tamd, nextid, TAMI_BROWSEREXTRA, td, 0, std::shared_ptr<userdatacontainer>(), 0, url.Mid(1));
			urlmenupopup(url.Mid(1));
			PopupMenu(&menu);
		}
		else if(url[0]=='X') {
			return;
		}
		else {
			tweet *targtw;
			if(url[0] == 'R') {
				targtw=td->rtsrc.get();
				url=url.Mid(1);
			}
			else {
				targtw = td.get();
			}
			unsigned long counter;
			url.ToULong(&counter);
			auto it=targtw->entlist.begin();
			while(it!=targtw->entlist.end()) {
				if(!counter) {
					//got entity
					entity &et= *it;
					switch(et.type) {
						case ENT_HASHTAG:
						break;
						case ENT_URL:
						case ENT_URL_IMG:
						case ENT_MEDIA:
							menu.Append(nextid, wxT("Open URL in Browser"));
							AppendToTAMIMenuMap(tamd, nextid, TAMI_BROWSEREXTRA, td, 0, std::shared_ptr<userdatacontainer>(), 0, wxstrstd(et.fullurl));
							urlmenupopup(wxstrstd(et.fullurl));
							PopupMenu(&menu);
						break;
						case ENT_MENTION: {
							AppendUserMenuItems(menu, tamd, nextid, et.user, td);
							PopupMenu(&menu);
							break;
						}
					}
					return;
				}
				else {
					counter--;
					it++;
				}
			}
		}
	}
	else {
		event.Skip();
	}
}

void dispscr_base::mousewheelhandler(wxMouseEvent &event) {
	//LogMsg(LFT_TPANEL, wxT("MouseWheel"));
	event.SetEventObject(GetParent());
	GetParent()->GetEventHandler()->ProcessEvent(event);
}

void tweetdispscr::OnTweetActMenuCmd(wxCommandEvent &event) {
	mainframe *mf = tppw->GetMainframe();
	if(!mf && mainframelist.size()) mf = mainframelist.front();
	TweetActMenuAction(tamd, event.GetId(), mf);
}

BEGIN_EVENT_TABLE(userdispscr, dispscr_base)
	EVT_TEXT_URL(wxID_ANY, userdispscr::urleventhandler)
END_EVENT_TABLE()

userdispscr::userdispscr(const std::shared_ptr<userdatacontainer> &u_, tpanelscrollwin *parent, tpanelparentwin_user *tppw_, wxBoxSizer *hbox_)
: dispscr_base(parent, tppw_, hbox_), u(u_), bm(0) { }

userdispscr::~userdispscr() { }

void userdispscr::Display(bool redrawimg) {
	Freeze();
	BeginSuppressUndo();

	if(redrawimg) {
		if(bm) {
			bm->SetBitmap(u->cached_profile_img);
		}
	}

	Clear();
	wxString format=gc.gcfg.userdispformat.val;
	wxString str=wxT("");

	for(size_t i=0; i<format.size(); i++) {
		switch((wxChar) format[i]) {
			case 'u':
				GenUserFmt(this, u.get(), i, format, str);
				break;
			default: {
				GenFmtCodeProc(this, i, format, str);
				break;
			}
		}
	}
	GenFlush(this, str);

	LayoutContent();
	if(!(tppw->tppw_flags&TPPWF_NOUPDATEONPUSH)) {
		tpsw->FitInside();
	}

	EndSuppressUndo();
	Thaw();

}

void userdispscr::urleventhandler(wxTextUrlEvent &event) {
	long start=event.GetURLStart();
	wxRichTextAttr textattr;
	GetStyle(start, textattr);
	wxString url=textattr.GetURL();
	LogMsgFormat(LFT_TPANEL, wxT("URL clicked, id: %s"), url.c_str());
	if(url[0]=='U') {
		uint64_t userid=0;
		for(unsigned int i=1; i<url.Len(); i++) {
			if(url[i]>='0' && url[i]<='9') {
				userid*=10;
				userid+=url[i]-'0';
			}
			else break;
		}
		if(userid) {
			std::shared_ptr<taccount> acc_hint;
			u->GetUsableAccount(acc_hint);
			user_window::MkWin(userid, acc_hint);
		}
	}
	else if(url[0]=='W') {
		::wxLaunchDefaultBrowser(url.Mid(1));
	}
}
