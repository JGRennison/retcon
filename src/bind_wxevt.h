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

#ifndef HGUARD_SRC_BIND_WXEVT
#define HGUARD_SRC_BIND_WXEVT

#include "univdefs.h"
#include <functional>
#include <map>
#include <memory>
#include <utility>
#include <wx/event.h>

class bindwxevt : public wxEvtHandler {
	std::map<std::pair<wxEventType, int>, std::shared_ptr<std::function<void(wxEvent &)> > > evt_handlers;

	public:
	bindwxevt();
	~bindwxevt();

	static std::shared_ptr<std::function<void(wxEvent &)> > MakeSharedEvtHandler(std::function<void(wxEvent &)> functor);
	void BindEvtHandler(wxEventType type, int id, std::shared_ptr<std::function<void(wxEvent &)> > fptr);
	void BindEvtHandler(wxEventType type, int id, std::function<void(wxEvent &)> functor);
	void RemoveEvtHandler(wxEventType type, int id);

	//! dynamic_cast<E &> the event object. Return shared handler.
	template <typename E, typename S>
	static std::shared_ptr<std::function<void(wxEvent &)> > MakeSharedEvtHandlerDC(S &&functor) {
		return MakeSharedEvtHandler([functor](wxEvent &evt) {
			functor(dynamic_cast<E &>(evt));
		});
	}

	//! dynamic_cast<E &> the event object. Bind to *this.
	template <typename E, typename S>
	void BindEvtHandlerDC(wxEventType type, int id, S &&functor) {
		BindEvtHandler(type, id, MakeSharedEvtHandlerDC<E, S>(std::forward<S>(functor)));
	}

	//! static_cast<E &>s the event type, unless in debug mode, in which case dynamic_cast<E &> is used. Return shared handler.
	template <typename E, typename S>
	static std::shared_ptr<std::function<void(wxEvent &)> > MakeSharedEvtHandlerSC(S &&functor) {
		return MakeSharedEvtHandler([functor](wxEvent &evt) {
#ifdef __WXDEBUG__
			functor(dynamic_cast<E &>(evt));
#else
			functor(static_cast<E &>(evt));
#endif
		});
	}

	//! static_cast<E &>s the event type, unless in debug mode, in which case dynamic_cast<E &> is used. Bind to *this.
	template <typename E, typename S>
	void BindEvtHandlerSC(wxEventType type, int id, S &&functor) {
		BindEvtHandler(type, id, MakeSharedEvtHandlerSC<E, S>(std::forward<S>(functor)));
	}

	virtual bool ProcessEvent(wxEvent& event) override;

	protected:
	bool TryDynProcessEvent(wxEvent& event);
};

class bindwxevt_win : public bindwxevt {
	wxWindow *win;

	public:
	bindwxevt_win(wxWindow *win_);
	~bindwxevt_win();
};

#endif
