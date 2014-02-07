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
//  2014 - Jonathan G Rennison <j.g.rennison@gmail.com>
//==========================================================================

#include "univdefs.h"
#include "bind_wxevt.h"
#include <wx/event.h>
#include <wx/window.h>

bindwxevt::bindwxevt() { }
bindwxevt::~bindwxevt() { }

std::shared_ptr<std::function<void(wxEvent &)> > bindwxevt::MakeSharedEvtHandler(std::function<void(wxEvent &)> functor) {
	return std::make_shared<std::function<void(wxEvent &)> >(std::move(functor));
}

void bindwxevt::BindEvtHandler(wxEventType type, int id, std::shared_ptr<std::function<void(wxEvent &)> > fptr) {
	evt_handlers[std::make_pair(type, id)] = std::move(fptr);
}

void bindwxevt::BindEvtHandler(wxEventType type, int id, std::function<void(wxEvent &)> functor) {
	BindEvtHandler(type, id, MakeSharedEvtHandler(std::move(functor)));
}

void bindwxevt::RemoveEvtHandler(wxEventType type, int id) {
	evt_handlers.erase(std::make_pair(type, id));
}

bool bindwxevt::ProcessEvent(wxEvent& event) {
	return TryDynProcessEvent(event) || wxEvtHandler::ProcessEvent(event);
}

bool bindwxevt::TryDynProcessEvent(wxEvent& event) {
	auto it = evt_handlers.find(std::make_pair(event.GetEventType(), event.GetId()));
	if(it != evt_handlers.end()) {
		(*it->second)(event);
		return !event.GetSkipped();
	}
	else return false;
}

bindwxevt_win::bindwxevt_win(wxWindow *win_) : win(win_) {
	win->PushEventHandler(this);
}

bindwxevt_win::~bindwxevt_win() {
	win->RemoveEventHandler(this);
}
