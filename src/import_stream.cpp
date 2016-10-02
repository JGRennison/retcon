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
//  2015 - Jonathan G Rennison <j.g.rennison@gmail.com>
//==========================================================================

#include "univdefs.h"
#include "import_stream.h"
#include "fileutil.h"
#include "parse.h"
#include "taccount.h"
#include "db.h"
#include "log.h"
#include <wx/msgdlg.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/arrstr.h>
#include <wx/choicdlg.h>

static std::shared_ptr<taccount> GetAccountByFilename(const wxString &filename) {
	for (auto &it : alist) {
		if (filename.Find(wxString::Format(wxT("%" wxLongLongFmtSpec "u"), it->usercont->id)) != wxNOT_FOUND) {
			// filename has account ID in the name, use this account
			return it;
		}
	}
	return {};
}

void StreamImportUserAction(wxWindow *parent) {
	static wxString file_path;

	wxFileDialog dlg(parent, wxT("Choose streaming API recording file(s)"), file_path, wxT(""),
			wxT("Stream Recording Files (twitter-stream-*.log)|twitter-stream-*.log|All Files (*)|*"), wxFD_OPEN | wxFD_FILE_MUST_EXIST | wxFD_MULTIPLE);

	int result = dlg.ShowModal();
	if (result != wxID_OK) return;

	wxArrayString paths;
	dlg.GetPaths(paths);
	size_t items = paths.GetCount();

	struct acc_import_set {
		std::shared_ptr<taccount> acc;
		std::vector<wxString> paths;
	};
	std::map<unsigned int, acc_import_set> acc_imports;

	for (size_t i = 0; i < items; i++) {
		const wxString &file = paths[i];
		if (file.IsEmpty()) continue;

		file_path = wxPathOnly(file);

		std::shared_ptr<taccount> acc;
		if (alist.size() == 0) {
			return;
		} else if (alist.size() == 1) {
			acc = alist.front();
		} else {
			acc = GetAccountByFilename(file);
		}
		if (!acc) {
			// ask the user which account to use

			wxArrayString account_names;
			std::vector<std::shared_ptr<taccount>> account_ptrs;
			for (auto &it : alist) {
				account_names.Add(wxString::Format(wxT("%s (@%s)"), wxstrstd(it->usercont->GetUser().name).c_str(), wxstrstd(it->usercont->GetUser().screen_name).c_str()));
				account_ptrs.push_back(it);
			}

			wxString select_message = wxString::Format(wxT("Select account to import stream recording data from file: %s, into"), file_path.c_str());
			wxSingleChoiceDialog dlg(parent, select_message, wxT("Select account"), account_names);
			if (dlg.ShowModal() != wxID_OK) {
				return;
			}

			acc = account_ptrs[dlg.GetSelection()];
		}

		acc_import_set &ais = acc_imports[acc->dbindex];
		if (!ais.acc) ais.acc = std::move(acc);
		ais.paths.push_back(file);
	}

	if (acc_imports.empty()) return;

	// confirm with user
	wxString message = wxT("About to import stream data from file(s):\n\n");
	for (auto &it : acc_imports) {
		for (auto &file : it.second.paths) {
			message += wxFileName(file).GetFullName();
			message += wxT("\n");
		}
		const std::shared_ptr<taccount> &acc = it.second.acc;
		message += wxString::Format(wxT("into account: %s (@%s)\n\n"),
				wxstrstd(acc->usercont->GetUser().name).c_str(), wxstrstd(acc->usercont->GetUser().screen_name).c_str());
	}
	message += wxT("This cannot be undone.");

	int confirm_result = ::wxMessageBox(message, wxT("Confirm import"), wxOK | wxCANCEL | wxICON_EXCLAMATION);
	if (confirm_result != wxOK) return;

	for (auto &it : acc_imports) {
		for (auto &file : it.second.paths) {
			StreamImport(it.second.acc, file);
		}
	}
}

void StreamImport(std::shared_ptr<taccount> acc, const wxString &filename) {
	std::string data;
	bool ok = LoadFromFile(filename, data);
	if (!ok) {
		::wxMessageBox(wxT("Failed to open file: ") + filename, wxT("Import Failed"));
		return;
	}

	std::unique_ptr<dbsendmsg_list> dbmsglist(new dbsendmsg_list());

	auto do_line = [&](size_t start, size_t end) {
		if (start == end) {
			return;
		}

		jsonparser jp(acc, nullptr);
		jp.dbmsglist = std::move(dbmsglist);
		try {
			bool ok = jp.ParseString(std::string(data.begin() + start, data.begin() + end));
			if (ok) {
				jp.ProcessStreamResponse(true);
			}
		} catch (std::exception &e) {
			LogMsgFormat(LOGT::PARSEERR, "Failed to parse line from stream import: %s\n%s", cstr(e.what()), cstr(jp.data->source_str));
		} catch (...) {
			LogMsgFormat(LOGT::PARSEERR, "Failed to parse line from stream import: %s", cstr(jp.data->source_str));
		}
		dbmsglist = std::move(jp.dbmsglist);
	};

	size_t line_start = 0;
	size_t line_end = 0;
	size_t max_size = data.size();
	for (; line_end < max_size; line_end++) {
		if (data[line_end] == '\n') {
			// found an EOL
			// cut here
			do_line(line_start, line_end);
			line_start = line_end + 1;
		}
	}
	do_line(line_start, max_size);

	if (dbmsglist && !dbmsglist->msglist.empty()) {
		DBC_SendMessage(std::move(dbmsglist));
	}
}
