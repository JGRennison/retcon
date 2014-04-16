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
#include "version.h"
#include "aboutwin.h"

#include <wx/aboutdlg.h>


void OpenAboutWindow() {
	wxAboutDialogInfo info;
	info.SetName(appname);
	info.SetVersion(wxT(RETCON_VERSION_STR));
	info.SetDescription(wxT("A Twitter client."));
	info.AddDeveloper(wxT("Jonathan Rennison <j.g.rennison@gmail.com>"));
	info.SetLicence(wxT("GNU General Public Licence version 2"));
	info.SetWebSite(wxT("https://github.com/JGRennison/retcon"));
	wxAboutBox(info);
}
