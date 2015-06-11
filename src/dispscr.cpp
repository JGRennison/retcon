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
//  2012 - Jonathan G Rennison <j.g.rennison@gmail.com>
//==========================================================================

#include "univdefs.h"
#include "dispscr.h"
#include "utf8.h"
#include "log.h"
#include "tpanel.h"
#include "tpanel-aux.h"
#include "tpg.h"
#include "uiutil.h"
#include "twit.h"
#include "util.h"
#include "cfg.h"
#include "taccount.h"
#include "alldata.h"
#include "mediawin.h"
#include "userui.h"
#include "mainui.h"
#include "retcon.h"
#include "tpanel-data.h"
#include "emoji/emoji.h"
#if HANDLE_PRIMARY_CLIPBOARD
#include <wx/clipbrd.h>
#endif
#define PCRE_STATIC
#include <pcre.h>

#ifndef DISPSCR_COPIOUS_LOGGING
#define DISPSCR_COPIOUS_LOGGING 0
#endif

DEFINE_EVENT_TYPE(wxextGDB_Popup_Evt)

BEGIN_EVENT_TABLE(generic_disp_base, commonRichTextCtrl)
	EVT_MOUSEWHEEL(generic_disp_base::mousewheelhandler)
	EVT_TEXT_URL(wxID_ANY, generic_disp_base::urleventhandler)
	EVT_COMMAND(wxID_ANY, wxextGDB_Popup_Evt, generic_disp_base::popupmenuhandler)
END_EVENT_TABLE()

generic_disp_base::generic_disp_base(wxWindow *parent, panelparentwin_base *tppw_, long extraflags, wxString thisname_)
		: commonRichTextCtrl(parent, wxID_ANY, wxEmptyString, wxRE_READONLY | wxRE_MULTILINE | wxBORDER_NONE | wxCLIP_CHILDREN | extraflags),
			tppw(tppw_), thisname(thisname_) {
	default_background_colour = GetBackgroundColour();
	default_foreground_colour = GetForegroundColour();
	#if DISPSCR_COPIOUS_LOGGING
		LogMsgFormat(LOGT::TPANELTRACE, "DCL: generic_disp_base::generic_disp_base constructor %s END", cstr(GetThisName()));
	#endif
}

void generic_disp_base::mousewheelhandler(wxMouseEvent &event) {
	#if DISPSCR_COPIOUS_LOGGING
		LogMsg(LOGT::TPANELTRACE, "DCL: MouseWheel");
	#endif
	event.SetEventObject(GetParent());
	GetParent()->GetEventHandler()->ProcessEvent(event);
}

void generic_disp_base::urleventhandler(wxTextUrlEvent &event) {
	long start=event.GetURLStart();
	wxRichTextAttr textattr;
	GetStyle(start, textattr);
	wxString url=textattr.GetURL();
	LogMsgFormat(LOGT::TPANELTRACE, "generic_disp_base::urleventhandler: %s, URL clicked, id: %s", cstr(GetThisName()), cstr(url));
	urlhandler(url);
}

bool generic_disp_base::IsFrozen() const {
	if(freezeflags & GDB_FF::FORCEFROZEN) return true;
	else if(freezeflags & GDB_FF::FORCEUNFROZEN) return false;
	else return wxRichTextCtrl::IsFrozen();
}

void generic_disp_base::ForceRefresh() {
	auto ffsave = freezeflags;
	freezeflags = GDB_FF::FORCEUNFROZEN;
	LayoutContent(false);
	freezeflags = ffsave;
}

void generic_disp_base::popupmenuhandler(wxCommandEvent &event) {
	if(menuptr) {
		//Action batching is to prevent commands which would destroy this being executed in the popup loop or in this's loop
		StartActionBatch();
		GenericPopupWrapper(this, menuptr.get());
		StopActionBatch();
	}
}

void generic_disp_base::StartActionBatch() {
	gdb_flags |= GDB_F::ACTIONBATCHMODE;
}

void generic_disp_base::StopActionBatch() {
	gdb_flags &= ~GDB_F::ACTIONBATCHMODE;

	for(auto &it : action_batch) {
		wxGetApp().EnqueuePending(std::move(it));
	}
	action_batch.clear();
}

void generic_disp_base::DoAction(std::function<void()> &&f) {
	if(gdb_flags & GDB_F::ACTIONBATCHMODE) {
		action_batch.push_back(std::move(f));
	}
	else {
		f();
	}
}

BEGIN_EVENT_TABLE(dispscr_mouseoverwin, generic_disp_base)
	EVT_ENTER_WINDOW(dispscr_mouseoverwin::mouseenterhandler)
	EVT_LEAVE_WINDOW(dispscr_mouseoverwin::mouseleavehandler)
	EVT_TIMER(wxID_HIGHEST + 1, dispscr_mouseoverwin::OnMouseEventTimer)
END_EVENT_TABLE()

dispscr_mouseoverwin::dispscr_mouseoverwin(wxWindow *parent, panelparentwin_base *tppw_, wxString thisname_)
: generic_disp_base(parent, tppw_, wxTE_DONTWRAP, thisname_), mouseevttimer(this, wxID_HIGHEST + 1), orig_parent(parent) {

	GetCaret()->Hide();
}

void dispscr_mouseoverwin::OnMagicPairedPtrChange(dispscr_base *targ, dispscr_base *prevtarg, bool targdestructing) {
	mouse_refcount = 0;
	if(prevtarg && !targdestructing) {
		prevtarg->Disconnect(wxEVT_SIZE, wxSizeEventHandler(dispscr_mouseoverwin::targsizehandler), 0, this);
	}
	if(targ) {
		Show(false);
		targ->Connect(wxEVT_SIZE, wxSizeEventHandler(dispscr_mouseoverwin::targsizehandler), 0, this);
		SetSize(targ->GetSize());
		Position(targ, targ->GetSize());

		Freeze();
		BeginSuppressUndo();
		bool show = RefreshContent();
		if(show) {
			SetSize(GetBuffer().GetCachedSize().x + GetBuffer().GetTopMargin() + GetBuffer().GetBottomMargin(),
					GetBuffer().GetCachedSize().y + GetBuffer().GetLeftMargin() + GetBuffer().GetRightMargin());
			Position(targ, targ->GetSize());
		}
		Show(show);
		EndSuppressUndo();
		Thaw();
	}
	else {
		Show(false);
		Reparent(orig_parent);
	}
}

void dispscr_mouseoverwin::targsizehandler(wxSizeEvent &event) {
	if(get()) {
		Position(get(), event.GetSize());
	}
}

void dispscr_mouseoverwin::Position(wxWindow *targ, const wxSize &targ_size) {
	if(GetParent() != targ) {
		Reparent(targ);
	}

	wxSize this_size = GetSize();
	wxPoint this_position(targ_size.x - this_size.x, 0);
	if(this_position != GetPosition()) {
		#if DISPSCR_COPIOUS_LOGGING
			LogMsgFormat(LOGT::TPANELTRACE, "DCL: dispscr_mouseoverwin::Position: %s, moving to: %d, %d, size: %d, %d",
					cstr(GetThisName()), this_position.x, this_position.y, this_size.x, this_size.y);
		#endif
		Move(this_position);
	}
}

void dispscr_mouseoverwin::SetScrollbars(int pixelsPerUnitX, int pixelsPerUnitY,
		int noUnitsX, int noUnitsY,
		int xPos, int yPos,
		bool noRefresh ) {

	#if DISPSCR_COPIOUS_LOGGING
		LogMsgFormat(LOGT::TPANELTRACE, "DCL: dispscr_mouseoverwin::SetScrollbars %s, %d, %d, %d, %d, %d, %d, %d",
				cstr(GetThisName()), pixelsPerUnitX, pixelsPerUnitY, noUnitsX, noUnitsY, xPos, yPos, noRefresh);
	#endif
	wxRichTextCtrl::SetScrollbars(0, 0, 0, 0, 0, 0, noRefresh);
}

void dispscr_mouseoverwin::mouseenterhandler(wxMouseEvent &event) {
	#if DISPSCR_COPIOUS_LOGGING
		LogMsgFormat(LOGT::TPANELTRACE, "DCL: dispscr_mouseoverwin::mouseenterhandler: %s", cstr(GetThisName()));
	#endif
	MouseEnterLeaveEvent(true);
}

void dispscr_mouseoverwin::mouseleavehandler(wxMouseEvent &event) {
	#if DISPSCR_COPIOUS_LOGGING
		LogMsgFormat(LOGT::TPANELTRACE, "DCL: dispscr_mouseoverwin::mouseleavehandler: %s", cstr(GetThisName()));
	#endif
	MouseEnterLeaveEvent(false);
}

void dispscr_mouseoverwin::MouseEnterLeaveEvent(bool enter) {
	if(enter) mouse_refcount++;
	else if(mouse_refcount) mouse_refcount--;
	if(mouse_refcount == 0) {
		mouseevttimer.Start(15, wxTIMER_ONE_SHOT);
	}
	else {
		mouseevttimer.Stop();
	}
}

void dispscr_mouseoverwin::OnMouseEventTimer(wxTimerEvent& event) {
	if(get() && mouse_refcount == 0) set(0, true);
}

BEGIN_EVENT_TABLE(dispscr_base, generic_disp_base)
	EVT_ENTER_WINDOW(dispscr_base::mouseenterhandler)
	EVT_LEAVE_WINDOW(dispscr_base::mouseleavehandler)
END_EVENT_TABLE()

dispscr_base::dispscr_base(wxWindow *parent, tpanel_item *item, panelparentwin_base *tppw_, wxBoxSizer *hbox_, wxString thisname_)
: generic_disp_base(parent, tppw_, 0, thisname_), tpi(item), hbox(hbox_) {
	GetCaret()->Hide();
	#if DISPSCR_COPIOUS_LOGGING
		LogMsgFormat(LOGT::TPANELTRACE, "DCL: dispscr_base::dispscr_base constructor %s END", cstr(GetThisName()));
	#endif
}

void dispscr_base::SetScrollbars(int pixelsPerUnitX, int pixelsPerUnitY,
		       int noUnitsX, int noUnitsY,
		       int xPos, int yPos,
		       bool noRefresh ) {
	#if DISPSCR_COPIOUS_LOGGING
		LogMsgFormat(LOGT::TPANELTRACE, "DCL: dispscr_base::SetScrollbars %s", cstr(GetThisName()));
	#endif
	wxRichTextCtrl::SetScrollbars(0, 0, 0, 0, 0, 0, noRefresh);
	int newheight = (pixelsPerUnitY * noUnitsY) + 4;
	hbox->SetItemMinSize(this, 10, newheight);

	tpi->NotifySizeChange();
}

void dispscr_base::mouseenterhandler(wxMouseEvent &event) {
	#if DISPSCR_COPIOUS_LOGGING
		LogMsgFormat(LOGT::TPANELTRACE, "DCL: dispscr_base::mouseenterhandler: %s", cstr(GetThisName()));
	#endif
	if(!get()) {
		set(MakeMouseOverWin());
	}
	if(get()) get()->MouseEnterLeaveEvent(true);
}

void dispscr_base::mouseleavehandler(wxMouseEvent &event) {
	#if DISPSCR_COPIOUS_LOGGING
		LogMsgFormat(LOGT::TPANELTRACE, "DCL: dispscr_base::mouseleavehandler: %s", cstr(GetThisName()));
	#endif
	if(get()) get()->MouseEnterLeaveEvent(false);
}

BEGIN_EVENT_TABLE(tweetdispscr, dispscr_base)
	EVT_MENU_RANGE(tweetactmenustartid, tweetactmenuendid, tweetdispscr::OnTweetActMenuCmd)
	EVT_RIGHT_DOWN(tweetdispscr::rightclickhandler)
	EVT_TIMER(TDS_WID_UNHIDEIMGOVERRIDETIMER, tweetdispscr::unhideimageoverridetimeouthandler)
END_EVENT_TABLE()

tweetdispscr::tweetdispscr(tweet_ptr_p td_, wxWindow *parent, tpanel_item *item, tpanelparentwin_nt *tppw_, wxBoxSizer *hbox_, wxString thisname_)
		: dispscr_base(parent, item, tppw_, hbox_, thisname_.empty() ? wxString::Format(wxT("tweetdispscr: %" wxLongLongFmtSpec "d for %s"),
			td_->id, tppw_->GetThisName().c_str()) : thisname_), td(td_), bm(0), bm2(0) {

	if(td_->rtsrc) rtid = td_->rtsrc->id;
	else rtid = 0;
	#if DISPSCR_COPIOUS_LOGGING
		LogMsgFormat(LOGT::TPANELTRACE, "DCL: tweetdispscr::tweetdispscr constructor %s END", cstr(GetThisName()));
	#endif
}

tweetdispscr::~tweetdispscr() {
}

void TweetReplaceStringSeq(std::function<void(const char *, size_t)> func, const std::string &str, int start, int end, int &track_byte, int &track_index) {
	while(str[track_byte]) {
		if(track_index == start) break;
		register int charsize = utf8firsttonumbytes(str[track_byte]);
		track_byte += charsize;
		track_index++;
	}
	int start_offset=track_byte;

	while(str[track_byte]) {
		if(track_index == end) break;
		if(str[track_byte] == '&') {
			char rep = 0;
			int delta = 0;
			if(str[track_byte+1] == 'l' && str[track_byte+2] == 't' && str[track_byte+3] == ';') {
				rep = '<';
				delta = 4;
			}
			else if(str[track_byte+1] == 'g' && str[track_byte+2] == 't' && str[track_byte+3] == ';') {
				rep = '>';
				delta = 4;
			}
			else if(str[track_byte+1] == 'q' && str[track_byte+2] == 'u' && str[track_byte+3] == 'o' && str[track_byte+4] == 't' && str[track_byte+5] == ';') {
				rep = '\'';
				delta = 6;
			}
			else if(str[track_byte+1] == '#' && str[track_byte+2] == '3' && str[track_byte+3] == '9' && str[track_byte+4] == ';') {
				rep = '"';
				delta = 5;
			}
			else if(str[track_byte+1] == 'a' && str[track_byte+2] == 'm' && str[track_byte+3] == 'p' && str[track_byte+4] == ';') {
				rep = '&';
				delta = 5;
			}
			if(rep) {
				func(&str[start_offset], track_byte - start_offset);
				track_index += delta;
				track_byte += delta;
				func(&rep, 1);
				start_offset = track_byte;
				continue;
			}
		}
		register int charsize = utf8firsttonumbytes(str[track_byte]);
		track_byte += charsize;
		track_index++;
	}
	int end_offset = track_byte;
	func(&str[start_offset], end_offset - start_offset);
}

//use -1 for end to run until end of string
static void DoWriteSubstr(commonRichTextCtrl &td, const std::string &str, int start, int end, int &track_byte, int &track_index, bool trim) {
	std::string output;
	TweetReplaceStringSeq([&](const char *s, size_t len) {
		output.append(s, len);
	}, str, start, end, track_byte, track_index);

	if(trim)
		rtrim(output);

	auto tpg = tpanelglobal::Get();
	if(!output.empty()) {
		EmojiParseString(
			output,
			gc.emoji_mode,
			tpg->emoji,
			[&](std::string text) {
				td.WriteText(wxstrstd(text));
			},
			[&](wxBitmap img, std::string altText) {
				td.WriteBitmapAltText(img, wxstrstd(altText));
			}
		);
	}
}

void WriteToRichTextCtrlWithEmojis(commonRichTextCtrl &td, const std::string &str) {
	int track_byte = 0;
	int track_index = 0;
	DoWriteSubstr(td, str, 0, str.size(), track_byte, track_index, true);
}

inline void GenFlush(generic_disp_base *obj, wxString &str) {
	if(str.size()) {
		obj->WriteText(str);
		str.clear();
	}
}

void GenUserFmt_OffsetDryRun(size_t &i, const wxString &format) {
	i++;
}

void GenUserFmt(generic_disp_base *obj, userdatacontainer *u, size_t &i, const wxString &format, wxString &str) {
	i++;
	if(i >= format.size()) return;
	#if DISPSCR_COPIOUS_LOGGING
		wxChar log_formatchar = format[i];
		LogMsgFormat(LOGT::TPANELTRACE, "DCL: GenUserFmt Start Format char: %c", log_formatchar);
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
			obj->BeginURL(wxString::Format("U%" wxLongLongFmtSpec "d", u->id));
			break;
		}
		case 'p':
			if(u->GetUser().u_flags & userdata::userdata::UF::ISPROTECTED) {
				GenFlush(obj, str);
				obj->WriteBitmap(tpanelglobal::Get()->proticon);
				obj->SetInsertionPointEnd();
			}
			break;
		case 'v':
			if(u->GetUser().u_flags & userdata::userdata::UF::ISVERIFIED) {
				GenFlush(obj, str);
				obj->WriteBitmap(tpanelglobal::Get()->verifiedicon);
				obj->SetInsertionPointEnd();
			}
			break;
		case 'd': {
			GenFlush(obj, str);
			long curpos = obj->GetInsertionPoint();
			obj->BeginURL(wxString::Format(wxT("Xd%" wxLongLongFmtSpec "d"), u->id));
			obj->WriteBitmap(tpanelglobal::Get()->dmreplyicon);
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
			str += wxstrstd(u->GetUser().description);
			break;
		}
		case 'l': {
			str += wxstrstd(u->GetUser().location);
			break;
		}
		default:
			break;
	}
	#if DISPSCR_COPIOUS_LOGGING
		LogMsgFormat(LOGT::TPANELTRACE, "DCL: GenUserFmt End Format char: %c", log_formatchar);
	#endif
}

void SkipOverFalseCond(size_t &i, const wxString &format) {
	size_t recursion = 1;
	for(i++; i < format.size(); i++) {
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
				wxChar quotechar = format[i];
				while(i < format.size()) {
					if(format[i] == quotechar) break;
					i++;
				}
				break;
			}
		}
	}
}

bool CondCodeProc(generic_disp_base *obj, size_t &i, const wxString &format, wxString &str) {
	i++;
	bool result = false;
	switch((wxChar) format[i]) {
		case 'F': {
			uint64_t any = 0;
			uint64_t all = 0;
			uint64_t none = 0;
			uint64_t missing = 0;

			uint64_t *current = &any;

			for(i++; i<format.size(); i++) {
				switch((wxChar) format[i]) {
					case '(': goto loopexit;
					case '+': current = &any; break;
					case '=': current = &all; break;
					case '-': current = &none; break;
					case '/': current = &missing; break;
					default: *current |= tweet_flags::GetFlagValue(format[i]);
				}
			}
			loopexit:

			tweet_ptr td = obj->GetTweet();
			if(!td) break;

			result = true;
			uint64_t curflags = td->flags.ToULLong();
			if(any && !(curflags & any)) result = false;
			if(all && (curflags & all) != all) result = false;
			if(none && (curflags & none)) result = false;
			if(missing && (curflags | missing) == curflags) result = false;

			break;
		}
		case 'm': {
			auto tds_flags = obj->GetTDSFlags();
			if(tds_flags & TDSF::CANLOADMOREREPLIES && gc.inlinereplyloadmorecount) {
				result = true;
			}
			break;
		}
	}
	if(format[i] != '(') return false;
	return result;
}

void ColourCodeProc(generic_disp_base *obj, size_t &i, const wxString &format, wxString &str) {
	size_t pos = i + 1;
	if(!(pos < format.size()) || format[pos] != '(') return;

	unsigned int bracketcount = 1;
	pos++;
	size_t start = pos;
	for(; pos < format.size(); pos++) {
		switch((wxChar) format[pos]) {
			case '(': {
				bracketcount++;
				break;
			}
			case ')': {
				bracketcount--;
				if(bracketcount == 0) {
					i = pos;
					obj->BeginTextColour(ColourOp(obj->default_foreground_colour, format.Mid(start, pos - start)));
					return;
				}
				break;
			}
		}
	}
}

void GenFmtCodeProc(generic_disp_base *obj, size_t &i, const wxString &format, wxString &str) {
	#if DISPSCR_COPIOUS_LOGGING
		wxChar log_formatchar = format[i];
		LogMsgFormat(LOGT::TPANELTRACE, "DCL: GenFmtCodeProc Start Format char: %c", log_formatchar);
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
		case 'K': {
			GenFlush(obj, str);
			ColourCodeProc(obj, i, format, str);
			break;
		}
		case 'k': {
			GenFlush(obj, str);
			obj->EndTextColour();
			break;
		}
		case '\'':
		case '"': {
			auto quotechar = format[i];
			i++;
			while(i<format.size()) {
				if(format[i] == quotechar) break;
				else str += format[i];
				i++;
			}
			break;
		}
		case ')': break;
		default:
			str += format[i];
			break;
	}
	#if DISPSCR_COPIOUS_LOGGING
		LogMsgFormat(LOGT::TPANELTRACE, "DCL: GenFmtCodeProc End Format char: %c", log_formatchar);
	#endif
}

void TweetFormatProc(generic_disp_base *obj, const wxString &format, tweet &tw, panelparentwin_base *tppw, flagwrapper<TDSF> tds_flags, std::vector<media_entity*> *me_list) {
	userdatacontainer *udc = tw.user.get();
	userdatacontainer *udc_recip = tw.user_recipient.get();

	tweetdispscr *td_obj = dynamic_cast<tweetdispscr *>(obj);

	wxString str = wxT("");
	auto flush = [&]() {
		GenFlush(obj, str);
	};
	auto userfmt = [&](userdatacontainer *u, size_t &i) {
		GenUserFmt(obj, u, i, format, str);
	};

	for(size_t i = 0; i < format.size(); i++) {
		#if DISPSCR_COPIOUS_LOGGING
			wxChar log_formatchar = format[i];
			LogMsgFormat(LOGT::TPANELTRACE, "DCL: TweetFormatProc Start Format char: %c", log_formatchar);
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
				str += wxstrstd(tw.flags.GetString());
				break;
			case 't':
				flush();
				if(td_obj) {
					td_obj->reltimestart = obj->GetInsertionPoint();
					obj->WriteText(getreltimestr(tw.createtime, td_obj->updatetime));
					td_obj->reltimeend = obj->GetInsertionPoint();
					auto tpg = tpanelglobal::Get();
					if(!tpg->minutetimer.IsRunning()) tpg->minutetimer.Start(60000, wxTIMER_CONTINUOUS);
				}
				break;
			case 'T':
				str += cfg_strftime(tw.createtime);
				break;

			case 'C':
			case 'c': {
				flush();
				if(me_list) {
					tweet &twgen = (format[i] == 'c' && tw.rtsrc && gc.rtdisp) ? *(tw.rtsrc) : tw;
					wxString urlcodeprefix = (format[i] == 'c' && tw.rtsrc && gc.rtdisp) ? wxT("R") : wxT("");
					unsigned int nextoffset = 0;
					unsigned int entnum = 0;
					int track_byte = 0;
					int track_index = 0;

					int last_start = -1;
					int last_end = -1;
					for(auto it = twgen.entlist.begin(); it != twgen.entlist.end(); it++, entnum++) {
						entity &et = *it;
						DoWriteSubstr(*obj, twgen.text, nextoffset, et.start, track_byte, track_index, false);

						// This is to de-duplicate entities which have the same start and end points
						// In particular this is the case for DMs with embedded media, which have
						// both URL and media entities with the same offsets and URL, and for tweets
						// with multiple attached media using extended_entities
						// Discard all but the first one.
						if(last_start != et.start || last_end != et.end) {
							obj->BeginUnderline();
							obj->BeginURL(urlcodeprefix + wxString::Format(wxT("%d"), entnum));
							obj->WriteText(wxstrstd(et.text));
							obj->EndURL();
							obj->EndUnderline();
							last_start = et.start;
							last_end = et.end;
						}
						nextoffset = et.end;

						if((et.type == ENT_MEDIA || et.type == ENT_URL_IMG) && et.media_id) {
							media_entity &me = *(ad.media_list[et.media_id]);

							// Test this here as well as in genjsonparser::DoEntitiesParse as this may be a media entity just loaded from the DB,
							// and acc <--> media entity links are not (currently) saved in the DB
							if(et.type == ENT_MEDIA && tw.flags.Get('D')) {
								// This is a media entity in a DM
								// This requires an oAuth token to access
								// Set the media entity dm_media_acc field to something sensible
								std::shared_ptr<taccount> acc = me.dm_media_acc.lock();
								tw.GetUsableAccount(acc, tweet::GUAF::CHECKEXISTING | tweet::GUAF::NOERR);
								me.dm_media_acc = acc;
							}

							me_list->push_back(&me);
						}
					}
					DoWriteSubstr(*obj, twgen.text, nextoffset, -1, track_byte, track_index, true);
				}
				break;
			}
			case 'X': {
				i++;
				if(i >= format.size()) break;
				flush();
				long curpos = obj->GetInsertionPoint();
				wxString url = wxString::Format(wxT("X%c"), (wxChar) format[i]);
				obj->BeginURL(url);
				bool imginserted = false;
				auto tpg = tpanelglobal::Get();
				switch((wxChar) format[i]) {
					case 'i':
						obj->WriteBitmap(tpg->infoicon);
						imginserted = true;
						break;
					case 'f': {
						if(tw.IsFavouritable()) {
							wxBitmap *icon = &tpg->favicon;
							tw.IterateTP([&](const tweet_perspective &tp) {
								if(tp.IsFavourited()) {
									icon = &tpg->favonicon;
								}
							});
							obj->WriteBitmap(*icon);
							imginserted = true;
						}
						break;
					}
					case 'r':
						obj->WriteBitmap(tpg->replyicon);
						imginserted = true;
						break;
					case 'd': {
						obj->EndURL();
						udc_ptr targ = tw.user_recipient;
						if(!targ || targ->udc_flags & UDC::THIS_IS_ACC_USER_HINT) targ=tw.user;
						url = wxString::Format(wxT("Xd%" wxLongLongFmtSpec "d"), targ->id);
						obj->BeginURL(url);
						obj->WriteBitmap(tpg->dmreplyicon);
						imginserted = true;
						break;
					}
					case 't': {
						if(tw.IsRetweetable()) {
							wxBitmap *icon = &tpg->retweeticon;
							tw.IterateTP([&](const tweet_perspective &tp) {
								if(tp.IsRetweeted()) {
									icon = &tpg->retweetonicon;
								}
							});
							obj->WriteBitmap(*icon);
							imginserted = true;
						}
						break;
					}
					case 'm': {
						if(tds_flags & TDSF::CANLOADMOREREPLIES && gc.inlinereplyloadmorecount) {
							obj->WriteText(wxString::Format(wxT("%d\x25BC"), gc.inlinereplyloadmorecount));
						}
						break;
					}
					default: break;
				}
				obj->EndURL();
				if(imginserted) {
					obj->SetInsertionPointEnd();
					wxTextAttrEx attr(obj->GetDefaultStyleEx());
					attr.SetURL(url);
					obj->SetStyleEx(curpos, obj->GetInsertionPoint(), attr, wxRICHTEXT_SETSTYLE_OPTIMIZE);
				}
				break;
			}
			case 'm': {
				i++;
				if(i >= format.size()) break;
				if(format[i] != '(' || tppw->IsSingleAccountWin() || tds_flags & TDSF::SUBTWEET) {
					SkipOverFalseCond(i, format);
				}
				break;
			}
			case 'A': {
				unsigned int ctr = 0;
				tw.IterateTP([&](const tweet_perspective &tp) {
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
			case 'J':
				str+=wxString::Format("%" wxLongLongFmtSpec "d", tw.id);
				break;
			case 'j':
				str+=wxString::Format("%" wxLongLongFmtSpec "d", (tw.rtsrc && gc.rtdisp) ? tw.rtsrc->id : tw.id);
				break;
			case 'S': {
				static pcre *pattern = 0;
				static pcre_extra *patextra = 0;
				static const char patsyntax[] = R"##(^(?:<a(?:\s+\w+="[^<>"]*")*\s+href="([^<>"]+)"(?:\s+\w+="[^<>"]*")*\s*>([^<>]*)</a>)|([^<>]*)$)##";

				std::string url;
				std::string name;
				std::string source = tw.source;

				auto parse = [&]() {
					if(!pattern) {
						const char *errptr;
						int erroffset;
						pattern = pcre_compile(patsyntax, PCRE_NO_UTF8_CHECK | PCRE_CASELESS | PCRE_UTF8, &errptr, &erroffset, 0);
						if(!pattern) {
							LogMsgFormat(LOGT::OTHERERR, "TweetFormatProc: case S: parse: pcre_compile failed: %s (%d)\n%s", cstr(errptr), erroffset, cstr(patsyntax));
							return;
						}
						patextra = pcre_study(pattern, 0, &errptr);
					}

					const int ovecsize = 60;
					int ovector[60];

					if(pcre_exec(pattern, patextra, source.c_str(), source.size(), 0, 0, ovector, ovecsize) >= 1) {
						if(ovector[2] >= 0) url.assign(source.c_str() + ovector[2], ovector[3] - ovector[2]);
						if(ovector[4] >= 0) name.assign(source.c_str() + ovector[4], ovector[5] - ovector[4]);
						else if(ovector[6] >= 0) name.assign(source.c_str() + ovector[6], ovector[7] - ovector[6]);
					}
				};

				bool next;
				do {
					next = false;
					i++;
					if(i >= format.size()) break;
					switch((wxChar) format[i]) {
						case 'w':
							if(source == "web") {
								source = "";
							}
							next = true;
							break;
						case 'r':
							str += wxstrstd(source);
							break;
						case 'n':
							parse();
							str += wxstrstd(name);
							break;
						case 'l':
						case 'L':
							parse();
							if(url.empty()) {
								str += wxstrstd(name);
							}
							else {
								flush();
								if((wxChar) format[i] == 'L') obj->BeginUnderline();
								obj->BeginURL(wxString::Format(wxT("W%s"), wxstrstd(url).c_str()));
								obj->WriteText(wxstrstd(name));
								obj->EndURL();
								if((wxChar) format[i] == 'L') obj->EndUnderline();
							}
							break;
						case 'p':
							i++;
							if(i>=format.size()) break;
							if(format[i] != '(' || source.empty()) {
								SkipOverFalseCond(i, format);
							}
					}
				} while(next);
				break;
			}
			case 'R':
			case 'f': {
				unsigned int value = 0;

				tweet & rttwt = (tw.rtsrc && gc.rtdisp) ? *tw.rtsrc : tw;

				bool next;
				i--;
				do {
					next = false;
					i++;
					if(i >= format.size()) break;
					switch((wxChar) format[i]) {
						case 'R':
							value += rttwt.retweet_count;
							next = true;
							break;
						case 'f':
							value += rttwt.favourite_count;
							next = true;
							break;
						case 'n':
							str += wxString::Format(wxT("%u"), value);
							break;
						case 'p':
						case 'P': {
							bool cond = value > 0;
							if(format[i] == 'P') cond = !cond;
							i++;
							if(i >= format.size()) break;
							if(!cond || format[i] != '(') {
								SkipOverFalseCond(i, format);
							}
							break;
						}
					}
				} while(next);
				break;
			}
			case 'y': { //debug codes
				i++;
				if(i >= format.size()) break;
				switch((wxChar) format[i]) {
					case 'F':
						str += wxstrstd(tw.GetFlagsAtPrevUpdate().GetString());
						break;
				}
				break;
			}
			default: {
				GenFmtCodeProc(obj, i, format, str);
				break;
			}
		}
		#if DISPSCR_COPIOUS_LOGGING
			LogMsgFormat(LOGT::TPANELTRACE, "DCL: TweetFormatProc End Format char: %c", log_formatchar);
		#endif
	}
	flush();
}

bool tweetdispscr::CheckHiddenState() {
	auto hideactions = [&](bool show) {
		Show(show);
		if(bm) bm->Show(show);
		if(bm2) bm2->Show(show);
		for(auto &it : rounded_box_panels) {
			it->Show(show);
		}
	};

	bool hidden;
	bool update_subtweets = false;

	if(parent_tweet && parent_tweet->tds_flags & TDSF::HIDDEN) {
		hidden = true;
	}
	else {
		hidden = (td->flags.Get('h') && !(tppw->GetTPPWFlags() & TPPWF::SHOWHIDDEN))
				|| (td->flags.Get('X') && !(tppw->GetTPPWFlags() & TPPWF::SHOWDELETED));
	}

	if(hidden && !(tds_flags & TDSF::HIDDEN)) {
		hideactions(false);
		tds_flags |= TDSF::HIDDEN;
		update_subtweets = true;
	}
	else if(!hidden && (tds_flags & TDSF::HIDDEN)) {
		hideactions(true);
		tds_flags &= ~TDSF::HIDDEN;
		update_subtweets = true;
	}
	if(update_subtweets) {
		for(auto &it : subtweets) {
			if(it)
				it->DisplayTweet();
		}
	}

	return hidden;
}

void tweetdispscr::DisplayTweet(bool redrawimg) {
	#if DISPSCR_COPIOUS_LOGGING
		LogMsgFormat(LOGT::TPANELTRACE, "DCL: tweetdispscr::DisplayTweet %s, START %" llFmtSpec "d, redrawimg: %d", cstr(GetThisName()), td->id, redrawimg);
	#endif

	Freeze();
	BeginSuppressUndo();

	updatetime = 0;
	std::vector<media_entity*> me_list;
	media_entity_updaters.clear();

	tweet &tw = *td;

	auto set_background = [&](const wxColour &colour) {
		SetBackgroundColour(colour);
		if(get())
			get()->SetBackgroundColour(colour);
		for(auto &it : rounded_box_panels) {
			it->SetBackgroundColour(colour);
		}
	};

	bool highlight = tw.flags.Get('H');
	if(highlight && !(tds_flags & TDSF::HIGHLIGHT)) {
		wxColour newcolour = ColourOp(default_background_colour, gc.gcfg.highlight_colourdelta.val);

		set_background(newcolour);
		tds_flags |= TDSF::HIGHLIGHT;
	}
	else if(!highlight && tds_flags & TDSF::HIGHLIGHT) {
		set_background(default_background_colour);
		tds_flags &= ~TDSF::HIGHLIGHT;
	}

	bool hidden = CheckHiddenState();

	if(redrawimg) {
		#if DISPSCR_COPIOUS_LOGGING
			LogMsgFormat(LOGT::TPANELTRACE, "DCL: tweetdispscr::DisplayTweet About to redraw images");
		#endif
		auto updateprofimg = [this](profimg_staticbitmap *b) {
			if(!b) return;
			udc_ptr udcp = b->udc;
			if(!udcp) return;
			if(b->pisb_flags & profimg_staticbitmap::PISBF::HALF) {
				udcp->ImgHalfIsReady(PENDING_REQ::PROFIMG_DOWNLOAD);
				b->SetBitmap(udcp->cached_profile_img_half);
			}
			else {
				b->SetBitmap(udcp->cached_profile_img);
			}
		};
		updateprofimg(bm);
		updateprofimg(bm2);
	}

	#if DISPSCR_COPIOUS_LOGGING
		LogMsgFormat(LOGT::TPANELTRACE, "DCL: tweetdispscr::DisplayTweet 1");
	#endif

	Clear();
	if(!hidden) {
		SetDefaultStyle(wxRichTextAttr());
		wxString format = wxT("");
		if(tw.flags.Get('R') && gc.rtdisp) format = gc.gcfg.rtdispformat.val;
		else if(tw.flags.Get('T')) format = gc.gcfg.tweetdispformat.val;
		else if(tw.flags.Get('D')) format = gc.gcfg.dmdispformat.val;

		TweetFormatProc(this, format, tw, tppw, tds_flags, &me_list);

		#if DISPSCR_COPIOUS_LOGGING
			LogMsgFormat(LOGT::TPANELTRACE, "DCL: tweetdispscr::DisplayTweet 2");
		#endif

		if(!me_list.empty()) {
			bool hidden_thumbnails = false;
			bool hidden_thumbnails_override = tds_flags & TDSF::IMGTHUMBHIDEOVERRIDE;
			bool shown_thumbnails = false;
			bool have_first = false;
			Newline();
			BeginAlignment(wxTEXT_ALIGNMENT_CENTRE);
			wxRichTextRange img_range = AddParagraph(wxT(""));
			SetInsertionPoint(img_range.GetEnd());
			for(auto &it : me_list) {
				media_entity_updaters.emplace_back(it);

				if(have_first) WriteText(wxT(" "));

				bool hide_thumb = (tw.flags.Get('p') || gc.hideallthumbs) && !hidden_thumbnails_override;

				if(gc.dispthumbs && !(!gc.loadhiddenthumbs && hide_thumb)) {
					flagwrapper<MELF> loadflags = MELF::DISPTIME;
					if(tw.flags.Get('n') || it->flags & MEF::MANUALLY_PURGED) loadflags |= MELF::NONETLOAD;
					it->CheckLoadThumb(loadflags);
				}

				wxString url = wxString::Format(wxT("M%" wxLongLongFmtSpec "d_%" wxLongLongFmtSpec "d"), (int64_t) it->media_id.m_id, (int64_t) it->media_id.t_id);
				BeginURL(url);
				wxString reload_text;
				if(!gc.dispthumbs) {
					BeginUnderline();
					WriteText(wxT("[Image]"));
					EndUnderline();
				}
				else if(it->flags & MEF::HAVE_THUMB && !hide_thumb) {
					long curpos = GetInsertionPoint();
					WriteBitmap(it->thumbimg);
					SetInsertionPointEnd();
					wxTextAttrEx attr(GetDefaultStyleEx());
					attr.SetURL(url);
					SetStyleEx(curpos, GetInsertionPoint(), attr, wxRICHTEXT_SETSTYLE_OPTIMIZE);
					shown_thumbnails = true;
				}
				else if(it->flags & MEF::THUMB_NET_INPROGRESS) {
					BeginUnderline();
					WriteText(wxT("[Image Loading...]"));
					EndUnderline();
				}
				else if(it->flags & MEF::THUMB_FAILED) {
					BeginUnderline();
					WriteText(wxT("[Failed to Load Image Thumbnail]"));
					EndUnderline();
					reload_text = wxT("Click to Try Again]");
				}
				else if(hide_thumb) {
					BeginUnderline();
					if(it->flags & MEF::HAVE_THUMB) WriteText(wxT("[Hidden Image Thumbnail]"));
					else WriteText(wxT("[Hidden Image Thumbnail Not Loaded]"));
					EndUnderline();
					hidden_thumbnails = true;
				}
				else {
					BeginUnderline();
					WriteText(wxT("[Image"));
					EndUnderline();
					reload_text = wxT("Click to Load Thumbnail]");
				}
				if(reload_text.size()) {
					EndURL();
					WriteText(wxT(" - "));
					BeginURL(wxString::Format(wxT("L%" wxLongLongFmtSpec "d_%" wxLongLongFmtSpec "d"), (int64_t) it->media_id.m_id, (int64_t) it->media_id.t_id));
					BeginUnderline();
					WriteText(reload_text);
					EndUnderline();
				}
				EndURL();
				have_first = true;
			}
			EndAlignment();
			if(hidden_thumbnails) {
				BeginAlignment(wxTEXT_ALIGNMENT_CENTRE);
				wxRichTextRange r = AddParagraph(wxT(""));
				SetInsertionPoint(r.GetEnd());
				if(!gc.hideallthumbs) { //Unhide link does nothing when hide all thumbnails option is set
					BeginURL(wxT("Xp"));
					BeginUnderline();
					WriteText(wxT("[Unhide]"));
					EndUnderline();
					EndURL();
					WriteText(wxT(" - "));
				}
				BeginURL(wxT("Xq"));
				BeginUnderline();
				WriteText(wxString::Format(wxT("[Unhide for %d seconds]"), gc.imgthumbunhidetime));
				EndUnderline();
				EndURL();
				EndAlignment();
			}
			if(shown_thumbnails && hidden_thumbnails_override) {
				BeginAlignment(wxTEXT_ALIGNMENT_CENTRE);
				BeginURL(wxT("XQ"));
				BeginUnderline();
				AddParagraph(wxT("[Hide images]"));
				EndUnderline();
				EndURL();
				EndAlignment();
			}
		}
	}

	#if DISPSCR_COPIOUS_LOGGING
		LogMsgFormat(LOGT::TPANELTRACE, "DCL: tweetdispscr::DisplayTweet 3");
	#endif

	if(!(tppw->GetTPPWFlags() & TPPWF::NOUPDATEONPUSH)) LayoutContent();

	if(!(tppw->GetTPPWFlags() & TPPWF::NOUPDATEONPUSH)) {
		#if DISPSCR_COPIOUS_LOGGING
			LogMsgFormat(LOGT::TPANELTRACE, "DCL: tweetdispscr::DisplayTweet 4 About to call tpi->NotifyLayoutNeeded()");
		#endif
		tpi->NotifyLayoutNeeded();
	}
	else {
		#if DISPSCR_COPIOUS_LOGGING
			LogMsgFormat(LOGT::TPANELTRACE, "DCL: tweetdispscr::DisplayTweet 4");
		#endif
	}

	EndAllStyles();
	EndSuppressUndo();

	dispscr_mouseoverwin *mouseover = get();
	if(mouseover) mouseover->RefreshContent();

	Thaw();

	#if DISPSCR_COPIOUS_LOGGING
		LogMsgFormat(LOGT::TPANELTRACE, "DCL: tweetdispscr::DisplayTweet %s, END %" llFmtSpec "d, redrawimg: %d", cstr(GetThisName()), td->id, redrawimg);
	#endif
}

void tweetdispscr::unhideimageoverridetimeouthandler(wxTimerEvent &event) {
	unhideimageoverridetimeoutexec();
}

void tweetdispscr::unhideimageoverridetimeoutexec() {
	if(imghideoverridetimer) imghideoverridetimer->Stop();
	if(tds_flags & TDSF::IMGTHUMBHIDEOVERRIDE) {
		tds_flags &= ~TDSF::IMGTHUMBHIDEOVERRIDE;
		DisplayTweet(false);
	}
}

void tweetdispscr::unhideimageoverridestarttimeout() {
	if(!imghideoverridetimer) imghideoverridetimer.reset(new wxTimer(this, TDS_WID_UNHIDEIMGOVERRIDETIMER));
	imghideoverridetimer->Start(gc.imgthumbunhidetime * 1000, wxTIMER_ONE_SHOT);
	if(! (tds_flags & TDSF::IMGTHUMBHIDEOVERRIDE)) {
		tds_flags |= TDSF::IMGTHUMBHIDEOVERRIDE;
		DisplayTweet(false);
	}
}

void tweetdispscr::PanelInsertEvt() {
	dispscr_base::PanelInsertEvt();
	if(!(tds_flags & TDSF::INSERTEDPANELIDREFS)) {
		static_cast<tpanelparentwin_nt *>(tppw)->IncTweetIDRefCounts(td->id, rtid);
		tds_flags |= TDSF::INSERTEDPANELIDREFS;
	}
}

void tweetdispscr::PanelRemoveEvt() {
	if(tds_flags & TDSF::INSERTEDPANELIDREFS) {
		static_cast<tpanelparentwin_nt *>(tppw)->DecTweetIDRefCounts(td->id, rtid);
		tds_flags &= ~TDSF::INSERTEDPANELIDREFS;
	}
	for(auto &it : subtweets) {
		tweetdispscr *tds = it.get();
		if(tds) tds->PanelRemoveEvt();
	}
	dispscr_base::PanelRemoveEvt();
}

/* Note on PopupMenu:
 * win may be a mouseover window, which could potentially disappear before the popup menu returns,
 * which could lead to issues such as the popup never being deleted, and its event loop lingering, preventing termination
 * Hence use associated tweetdispscr instead if available
 * Creating a popup during a URL click handler has dubious results on GTK, use a pending event instead
 * Also log before and after to make debugging lingering popups easier
 */
void TweetURLHandler(wxWindow *win, wxString url, tweet_ptr_p td, panelparentwin_base *tppw) {

	auto dopopupmenu = [&](std::shared_ptr<wxMenu> menu) {
		generic_disp_base *gdb = dynamic_cast<generic_disp_base *>(win);
		if(gdb) {
			generic_disp_base *popwin = gdb;
			tweetdispscr *tds = gdb->GetTDS();
			if(tds) popwin = tds;
			LogMsgFormat(LOGT::TPANELTRACE, "Sending popup menu message: %p, to window: %s (%p), win: %s (%p), tppw: %p",
					menu.get(), cstr(popwin->thisname), popwin, cstr(gdb->thisname), gdb, tppw);
			popwin->menuptr = menu;
			wxCommandEvent event(wxextGDB_Popup_Evt, win->GetId());
			popwin->GetEventHandler()->AddPendingEvent(event);
		}
		else GenericPopupWrapper(win, menu.get());
	};

	if(url[0] == 'M') {
		media_id_type media_id = ParseMediaID(url);

		LogMsgFormat(LOGT::TPANELTRACE, "Media image clicked, str: %s, id: %" llFmtSpec "d/%" llFmtSpec "d", cstr(url.Mid(1)), media_id.m_id, media_id.t_id);
		if(ad.media_list[media_id]->win) {
			ad.media_list[media_id]->win->Raise();
		}
		else {
			mainframe *parent_mf = GetMainframeAncestor(win, false);
			if(parent_mf) win = parent_mf;
			new media_display_win(win, media_id);
		}
	}
	else if(url[0] == 'L') {
		media_id_type media_id = ParseMediaID(url);

		// User has requested a (re)load, clear failed flag
		ad.media_list[media_id]->flags &= ~MEF::THUMB_FAILED;

		ad.media_list[media_id]->CheckLoadThumb(MELF::FORCE);
		for(auto &it : ad.media_list[media_id]->tweet_list) {
			UpdateTweet(*it);
		}
	}
	else if(url[0] == 'U') {
		uint64_t userid = ParseUrlID(url);
		if(userid) {
			std::shared_ptr<taccount> acc_hint;
			td->GetUsableAccount(acc_hint);
			user_window::MkWin(userid, acc_hint);
		}
	}
	else if(url[0] == 'W') {
		::wxLaunchDefaultBrowser(url.Mid(1));
	}
	else if(url[0] == 'X') {
		switch((wxChar) url[1]) {
			case 'd': { //send dm
				uint64_t userid;
				if(!ownstrtonum(userid, (wxChar*) &url[2], -1))
					break;
				mainframe *mf = tppw->GetMainframe();
				if(!mf && mainframelist.size()) mf = mainframelist.front();
				if(mf) {
					mf->tpw->SetDMTarget(ad.GetUserContainerById(userid), td);
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
				int nextid = tweetactmenustartid;
				auto menu = std::make_shared<wxMenu>();
				menu->SetTitle(wxT("Favourite:"));
				MakeFavMenu(menu.get(), tamd, nextid, td);
				dopopupmenu(menu);  // See note above on PopupMenu
				break;
			}
			case 't': {//retweet
				tamd.clear();
				int nextid = tweetactmenustartid;
				auto menu = std::make_shared<wxMenu>();
				menu->SetTitle(wxT("Retweet:"));
				MakeRetweetMenu(menu.get(), tamd, nextid, td);
				dopopupmenu(menu);  // See note above on PopupMenu
				break;
			}
			case 'i': {//info
				tamd.clear();
				int nextid = tweetactmenustartid;
				auto menuptr = std::make_shared<wxMenu>();
				wxMenu &menu = *menuptr;

				generic_disp_base *gdb = dynamic_cast<generic_disp_base *>(win);
				tweetdispscr *tds = nullptr;
				if(gdb) tds = gdb->GetTDS();

				menu.Append(nextid, wxT("Reply"));
				AppendToTAMIMenuMap(tamd, nextid, TAMI_REPLY, td);

				if(td->rtsrc) {
					menu.Append(nextid, wxT("Reply to Original Tweet"));
					AppendToTAMIMenuMap(tamd, nextid, TAMI_REPLY, td->rtsrc);
				}

				auto dosenddmmenuitem = [&](tweet_ptr_p t) {
					udc_ptr targ = t->user_recipient;
					if(!targ || targ->udc_flags & UDC::THIS_IS_ACC_USER_HINT) targ = t->user;
					menu.Append(nextid, wxT("Send DM to @") + wxstrstd(targ->user.screen_name));
					AppendToTAMIMenuMap(tamd, nextid, TAMI_DM, td, 0, targ);
				};
				dosenddmmenuitem(td);
				if(td->rtsrc) {
					dosenddmmenuitem(td->rtsrc);
				}

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
					tpanelparentwin_nt *tppw_nt = dynamic_cast<tpanelparentwin_nt *>(tppw);
					if(tppw_nt && tds && !(tds->tds_flags & TDSF::SUBTWEET)) {
						MakeTPanelMarkMenu(marksubmenu, tamd, nextid, td, tppw_nt);
					}
				}
				if(td->flags.Get('I')) {
					wxMenu *imagesubmenu = new wxMenu();
					menu.AppendSubMenu(imagesubmenu, wxT("Image"));
					MakeImageMenu(imagesubmenu, tamd, nextid, td);
				}
				{
					std::vector<std::shared_ptr<tpanel> > manual_tps;
					std::vector<std::shared_ptr<tpanel> > manual_tps_already_in;
					for(auto &it : ad.tpanels) {
						if(it.second->flags & TPF::MANUAL) {
							if(it.second->tweetlist.find(td->id) != it.second->tweetlist.end()) {
								manual_tps_already_in.push_back(it.second);
							}
							else {
								manual_tps.push_back(it.second);
							}
						}
					}
					if(!manual_tps.empty()) {
						wxMenu *panelsubmenu = new wxMenu();
						menu.AppendSubMenu(panelsubmenu, wxT("Add to Panel"));
						for(auto &it : manual_tps) {
							panelsubmenu->Append(nextid, wxstrstd(it->dispname));
							AppendToTAMIMenuMap(tamd, nextid, TAMI_ADDTOPANEL, td, 0, udc_ptr(), 0, wxstrstd(it->name));
						}
					}
					if(!manual_tps_already_in.empty()) {
						wxMenu *panelsubmenu = new wxMenu();
						menu.AppendSubMenu(panelsubmenu, wxT("Remove from Panel"));
						for(auto &it : manual_tps_already_in) {
							panelsubmenu->Append(nextid, wxstrstd(it->dispname));
							AppendToTAMIMenuMap(tamd, nextid, TAMI_REMOVEFROMPANEL, td, 0, udc_ptr(), 0, wxstrstd(it->name));
						}
					}
				}

				bool deletable = false;
				bool deletable2 = false;
				unsigned int delcount = 0;
				unsigned int deldbindex = 0;
				unsigned int deldbindex2 = 0;
				std::shared_ptr<taccount> cacc;
				std::shared_ptr<taccount> cacc2;
				if(td->flags.Get('D')) {
					cacc=td->user_recipient->GetAccountOfUser();
					if(cacc) delcount++;
					if(cacc && cacc->enabled) {
						deletable = true;
						deldbindex = cacc->dbindex;
					}
				}
				cacc2 = td->user->GetAccountOfUser();
				if(cacc2) delcount++;
				if(cacc2 && cacc2->enabled) {
					deletable2 = true;
					deldbindex2 = cacc2->dbindex;
				}
				if(delcount > 1) {    //user has DMd another of their own accounts :/
					wxMenuItem *delmenuitem = menu.Append(nextid, wxT("Delete: ") + cacc->dispname);
					delmenuitem->Enable(deletable);
					AppendToTAMIMenuMap(tamd, nextid, TAMI_DELETE, td, deldbindex);
					delmenuitem = menu.Append(nextid, wxT("Delete: ") + cacc2->dispname);
					delmenuitem->Enable(deletable2);
					AppendToTAMIMenuMap(tamd, nextid, TAMI_DELETE, td, deldbindex2);
				}
				else {
					wxMenuItem *delmenuitem = menu.Append(nextid, wxT("Delete"));
					delmenuitem->Enable(deletable|deletable2);
					AppendToTAMIMenuMap(tamd, nextid, TAMI_DELETE, td, deldbindex2?deldbindex2:deldbindex);
				}

				if(gc.tweetdebugactions) {
					wxMenu *debugsubmenu = new wxMenu();
					menu.AppendSubMenu(debugsubmenu, wxT("Debug"));
					MakeDebugMenu(debugsubmenu, tamd, nextid, td);
				}

				dopopupmenu(menuptr);  // See note above on PopupMenu
				break;
			}
			case 'p': {
				td->flags.Set('p', false);
				td->CheckFlagsUpdated(tweet::CFUF::SEND_DB_UPDATE | tweet::CFUF::UPDATE_TWEET);
				break;
			}
			case 'q': {
				tweetdispscr *tds = dynamic_cast<tweetdispscr *>(win);
				if(tds) tds->unhideimageoverridestarttimeout();
				break;
			}
			case 'Q': {
				tweetdispscr *tds = dynamic_cast<tweetdispscr *>(win);
				if(tds) tds->unhideimageoverridetimeoutexec();
				break;
			}
			case 'm': {
				tweetdispscr *tds = nullptr;
				generic_disp_base *gdb = dynamic_cast<generic_disp_base *>(win);
				if(gdb) tds = gdb->GetTDS();
				if(!tds) break;
				if(tds->loadmorereplies) {
					tds->loadmorereplies();
					tds->loadmorereplies = nullptr;
				}
				tds->tds_flags &= ~TDSF::CANLOADMOREREPLIES;
				tds->DisplayTweet(false);
				tweetdispscr_mouseoverwin *tdsmow = dynamic_cast<tweetdispscr_mouseoverwin *>(gdb);
				if(tdsmow) {
					tdsmow->tds_flags &= ~TDSF::CANLOADMOREREPLIES;
					tdsmow->RefreshContent();
				}
				break;
			}
		}
	}
	else {
		tweet *targtw;
		if(url[0] == 'R') {
			targtw = td->rtsrc.get();
			url = url.Mid(1);
		}
		else {
			targtw = td.get();
		}
		unsigned long counter;
		url.ToULong(&counter);
		auto it = targtw->entlist.begin();
		while(it != targtw->entlist.end()) {
			if(!counter) {
				//got entity
				entity &et = *it;
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

void tweetdispscr::urlhandler(wxString url) {
	TweetURLHandler(this, url, td, tppw);
}

void AppendUserMenuItems(wxMenu &menu, tweetactmenudata &map, int &nextid, udc_ptr user, tweet_ptr tw) {
	menu.Append(nextid, wxT("Open User Window"));
	AppendToTAMIMenuMap(map, nextid, TAMI_USERWINDOW, tw, 0, user);

	menu.Append(nextid, wxT("Open Twitter Profile in Browser"));
	bool https_link;
	if(tw) https_link = tw->flags.Get('s');
	else https_link = gc.cfg.ssl.val == wxT("1");
	AppendToTAMIMenuMap(map, nextid, TAMI_BROWSEREXTRA, tw, 0, user, 0, wxstrstd(user->GetPermalink(https_link)));

	menu.Append(nextid, wxT("Send DM"));
	AppendToTAMIMenuMap(map, nextid, TAMI_DM, tw, 0, user);

	optional_observer_ptr<user_dm_index> dm_index = ad.GetExistingUserDMIndexById(user->id);
	if(dm_index && !dm_index->ids.empty()) {
		menu.Append(nextid, wxT("Open DM Conversation Panel"));
		AppendToTAMIMenuMap(map, nextid, TAMI_DMSETPANEL, tw, 0, user);
	}

	wxString username = wxstrstd(user->GetUser().screen_name);
	menu.Append(nextid, wxString::Format(wxT("Copy User Name (%s) to Clipboard"), username.c_str()));
	AppendToTAMIMenuMap(map, nextid, TAMI_COPYEXTRA, tw, 0, user, 0, username);

	wxString useridstr = wxString::Format(wxT("%" wxLongLongFmtSpec "d"), user->id);
	menu.Append(nextid, wxString::Format(wxT("Copy User ID (%s) to Clipboard"), useridstr.c_str()));
	AppendToTAMIMenuMap(map, nextid, TAMI_COPYEXTRA, tw, 0, user, 0, useridstr);
}

void TweetRightClickHandler(generic_disp_base *win, wxMouseEvent &event, tweet_ptr_p td) {
	wxPoint mousepoint = event.GetPosition();
	long textpos = 0;
	win->HitTest(mousepoint, &textpos);
	wxRichTextAttr style;
	win->GetStyle(textpos, style);
	if(style.HasURL()) {
		wxString url = style.GetURL();
		int nextid = tweetactmenustartid;
		wxMenu menu;

		auto urlmenupopup = [&](const wxString &urlstr) {
			wxMenu *submenu = new wxMenu;
			submenu->Append(nextid, urlstr);
			AppendToTAMIMenuMap(tamd, nextid, TAMI_NULL, td, 0, udc_ptr(), 0, urlstr);
			submenu->AppendSeparator();
			submenu->Append(nextid, wxT("Copy to Clipboard"));
			AppendToTAMIMenuMap(tamd, nextid, TAMI_COPYEXTRA, td, 0, udc_ptr(), 0, urlstr);
			menu.AppendSubMenu(submenu, wxT("URL"));
		};

		if(url[0] == 'M') {
			media_id_type media_id=ParseMediaID(url);
			menu.Append(nextid, wxT("Open Media in Window"));
			AppendToTAMIMenuMap(tamd, nextid, TAMI_MEDIAWIN, td, 0, udc_ptr(), 0, url);
			urlmenupopup(wxstrstd(ad.media_list[media_id]->media_url));
			GenericPopupWrapper(win, &menu);
		}
		else if(url[0] == 'U') {
			uint64_t userid = ParseUrlID(url);
			if(userid) {
				udc_ptr user = ad.GetUserContainerById(userid);
				AppendUserMenuItems(menu, tamd, nextid, user, td);
				GenericPopupWrapper(win, &menu);
			}
		}
		else if(url[0] == 'W') {
			menu.Append(nextid, wxT("Open URL in Browser"));
			AppendToTAMIMenuMap(tamd, nextid, TAMI_BROWSEREXTRA, td, 0, udc_ptr(), 0, url.Mid(1));
			urlmenupopup(url.Mid(1));
			GenericPopupWrapper(win, &menu);
		}
		else if(url[0] == 'X') {
			return;
		}
		else {
			tweet *targtw;
			if(url[0] == 'R') {
				targtw = td->rtsrc.get();
				url = url.Mid(1);
			}
			else {
				targtw = td.get();
			}
			unsigned long counter;
			url.ToULong(&counter);
			auto it = targtw->entlist.begin();
			while(it != targtw->entlist.end()) {
				if(!counter) {
					//got entity
					entity &et = *it;
					switch(et.type) {
						case ENT_HASHTAG:
						break;
						case ENT_URL:
						case ENT_URL_IMG:
						case ENT_MEDIA:
							menu.Append(nextid, wxT("Open URL in Browser"));
							AppendToTAMIMenuMap(tamd, nextid, TAMI_BROWSEREXTRA, td, 0, udc_ptr(), 0, wxstrstd(et.fullurl));
							urlmenupopup(wxstrstd(et.fullurl));
							GenericPopupWrapper(win, &menu);
						break;
						case ENT_MENTION: {
							AppendUserMenuItems(menu, tamd, nextid, et.user, td);
							GenericPopupWrapper(win, &menu);
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

void tweetdispscr::rightclickhandler(wxMouseEvent &event) {
	TweetRightClickHandler(this, event, td);
}

void tweetdispscr::OnTweetActMenuCmd(wxCommandEvent &event) {
	mainframe *mf = tppw->GetMainframe();
	if(!mf && mainframelist.size()) mf = mainframelist.front();
	auto id = event.GetId();
	DoAction([mf, id]() {
		TweetActMenuAction(tamd, id, mf);
	});
}

// These are to stop the mouseover disappearing during a popup on Windows
void tweetdispscr::BeforePopup() {
	if(dispscr_mouseoverwin *m = get())
		m->MouseEnterLeaveEvent(true);
}
void tweetdispscr::AfterPopup() {
	if(dispscr_mouseoverwin *m = get())
		m->MouseEnterLeaveEvent(false);
}

BEGIN_EVENT_TABLE(tweetdispscr_mouseoverwin, dispscr_mouseoverwin)
	EVT_MENU_RANGE(tweetactmenustartid, tweetactmenuendid, tweetdispscr_mouseoverwin::OnTweetActMenuCmd)
	EVT_RIGHT_DOWN(tweetdispscr_mouseoverwin::rightclickhandler)
END_EVENT_TABLE()

tweetdispscr_mouseoverwin *tweetdispscr::MakeMouseOverWin() {
	tweetdispscr_mouseoverwin *mw = static_cast<tpanelparentwin_nt *>(tppw)->MakeMouseOverWin();
	mw->td = td;
	mw->tds_flags = tds_flags;
	mw->current_tds.set(this);
	mw->SetBackgroundColour(GetBackgroundColour());
	return mw;
}

tweetdispscr_mouseoverwin::tweetdispscr_mouseoverwin(wxWindow *parent, panelparentwin_base *tppw_, wxString thisname_)
	: dispscr_mouseoverwin(parent, tppw_, thisname_.empty() ? wxT("tweetdispscr_mouseoverwin for ") + tppw_->GetThisName() : thisname_) {
}

bool tweetdispscr_mouseoverwin::RefreshContent() {
	wxRichTextAttr attr;
	attr.SetAlignment(wxTEXT_ALIGNMENT_RIGHT);
	SetDefaultStyle(attr);
	Clear();

	dispscr_base *targwin = get();
	if(targwin) SetBackgroundColour(targwin->GetBackgroundColour());

	BeginAlignment(wxTEXT_ALIGNMENT_RIGHT);
	wxString format;
	if(td->flags.Get('R') && gc.rtdisp) format = gc.gcfg.mouseover_rtdispformat.val;
	else if(td->flags.Get('T')) format = gc.gcfg.mouseover_tweetdispformat.val;
	else if(td->flags.Get('D')) format = gc.gcfg.mouseover_dmdispformat.val;

	if(format.IsEmpty()) return false;

	TweetFormatProc(this, format, *td, tppw, tds_flags, 0);

	EndAlignment();
	LayoutContent();
	return true;
}

void tweetdispscr_mouseoverwin::urlhandler(wxString url) {
	TweetURLHandler(this, url, td, tppw);
}

void tweetdispscr_mouseoverwin::OnTweetActMenuCmd(wxCommandEvent &event) {
	mainframe *mf = tppw->GetMainframe();
	if(!mf && mainframelist.size()) mf = mainframelist.front();
	auto id = event.GetId();
	DoAction([mf, id]() {
		TweetActMenuAction(tamd, id, mf);
	});
}

void tweetdispscr_mouseoverwin::rightclickhandler(wxMouseEvent &event) {
	TweetRightClickHandler(this, event, td);
}

BEGIN_EVENT_TABLE(userdispscr, dispscr_base)
	EVT_TEXT_URL(wxID_ANY, userdispscr::urleventhandler)
END_EVENT_TABLE()

userdispscr::userdispscr(udc_ptr_p u_, tpanel_item *parent, tpanelparentwin_user *tppw_, wxBoxSizer *hbox_, wxString thisname_)
: dispscr_base(parent, parent, tppw_, hbox_, thisname_.empty() ? wxString::Format(wxT("userdispscr: %" wxLongLongFmtSpec "d for %s"), u_->id, tppw_->GetThisName().c_str()) : thisname_), u(u_), bm(0) {
}

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
	SetDefaultStyle(wxRichTextAttr());
	wxString format = gc.gcfg.userdispformat.val;
	wxString str = wxT("");

	for(size_t i = 0; i < format.size(); i++) {
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
	if(!(tppw->GetTPPWFlags() & TPPWF::NOUPDATEONPUSH)) {
		tpi->NotifyLayoutNeeded();
	}

	EndAllStyles();
	EndSuppressUndo();
	Thaw();

}

void userdispscr::urleventhandler(wxTextUrlEvent &event) {
	long start = event.GetURLStart();
	wxRichTextAttr textattr;
	GetStyle(start, textattr);
	wxString url = textattr.GetURL();
	LogMsgFormat(LOGT::TPANELTRACE, "URL clicked, id: %s", cstr(url));
	if(url[0] == 'U') {
		uint64_t userid = 0;
		for(unsigned int i = 1; i < url.Len(); i++) {
			if(url[i] >= '0' && url[i] <= '9') {
				userid *= 10;
				userid += url[i] - '0';
			}
			else break;
		}
		if(userid) {
			std::shared_ptr<taccount> acc_hint;
			u->GetUsableAccount(acc_hint);
			user_window::MkWin(userid, acc_hint);
		}
	}
	else if(url[0] == 'W') {
		::wxLaunchDefaultBrowser(url.Mid(1));
	}
}
