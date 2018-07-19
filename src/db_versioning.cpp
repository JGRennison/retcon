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

#include "univdefs.h"
#include "db.h"
#include "db-intl.h"
#include "map.h"
#include "log.h"
#include "twit-common.h"
#include "raii.h"
#include "json-util.h"
#include "parse.h"
#include <wx/msgdlg.h>

static const unsigned int db_version = 11;

static const char *update_sql[] = {
	"ALTER TABLE mediacache ADD COLUMN lastusedtimestamp INTEGER;"
	"UPDATE OR IGNORE mediacache SET lastusedtimestamp = strftime('%s','now');"
	"UPDATE OR IGNORE settings SET accid = 'G' WHERE (hex(accid) == '4700');"  //This is because previous versions of retcon accidentally inserted an embedded null when writing out the config
	"UPDATE OR IGNORE tweets SET medialist = NULL;"
	,
	"ALTER TABLE users ADD COLUMN profimglastusedtimestamp INTEGER;"
	"UPDATE OR IGNORE users SET profimglastusedtimestamp = strftime('%s','now');"
	,
	nullptr
	//"ALTER TABLE users ADD COLUMN dmindex BLOB;"
	,
	// special case: only run if version was previously 2
	"UPDATE OR IGNORE users SET dmindex = NULL;"
	// SyncDoUpdates_FillUserDMIndexes should be run here
	,
	nullptr
	// DB set compression changes
	,
	"INSERT OR REPLACE INTO staticsettings(name, value) SELECT name, value FROM settings WHERE name IN ('unreadids', 'highlightids', 'hiddenids', 'deletedids');"
	// move CIDS lists to staticsettings
	,
	"ALTER TABLE tpanelwins ADD COLUMN intersect_flags INTEGER;"
	"ALTER TABLE tpanelwins ADD COLUMN tppw_flags INTEGER;"
	"UPDATE OR IGNORE tpanelwins SET tppw_flags = 1;" // mark as not valid
	// add extra columns to tpanelwins
	,
	"ALTER TABLE acc ADD COLUMN blockedids BLOB;"
	"ALTER TABLE acc ADD COLUMN mutedids BLOB;"
	// add blocked and muted columns to acc table
	,
	"ALTER TABLE acc ADD COLUMN nortids BLOB;"
	// add no RT column to acc table
	,
	nullptr
	// SyncDoUpdates_FillTweetXrefTable should be run here
	,
	nullptr
	// added timelinehiddenids
};

// return false if all bets are off and DB should not be read
bool dbconn::SyncDoUpdates(sqlite3 *adb) {
	LogMsg(LOGT::DBINFO, "dbconn::SyncDoUpdates start");

	unsigned int current_db_version = 0;
	bool have_version = false;

	try {
		sqlite3_stmt *getstmt = cache.GetStmt(adb, DBPSC_SELSTATICSETTING);
		sqlite3_bind_text(getstmt, 1, "dbversion", -1, SQLITE_STATIC);
		DBRowExec(adb, getstmt, [&](sqlite3_stmt *stmt) {
			current_db_version = (unsigned int) sqlite3_column_int64(stmt, 0);
			have_version = true;
		}, db_throw_on_error("dbconn::SyncDoUpdates (get DB version)"));

		if (!have_version) {
			throw std::runtime_error("dbversion row seems to be missing from database.");
		}

		if (current_db_version < db_version) {
			LogMsgFormat(LOGT::DBINFO, "dbconn::SyncDoUpdates updating from %u to %u", current_db_version, db_version);
			DBExec(adb, "BEGIN EXCLUSIVE;", db_throw_on_error("dbconn::SyncDoUpdates (lock)"));

			auto finaliser = scope_guard([&]() {
				DBExec(adb, "ROLLBACK;", "dbconn::SyncDoUpdates (rollback)");
			});

			for (unsigned int i = current_db_version; i < db_version; i++) {
				if (i == 9) {
					SyncDoUpdates_FillTweetXrefTable(adb);
				}

				const char *sql = update_sql[i];
				if (!sql) continue;

				if (i == 3) {
					SyncDoUpdates_FillUserDMIndexes(adb);
					if (current_db_version != 2) continue; // see special case above
				}

				DBExecStringMulti(adb, sql, [&](sqlite3_stmt *stmt, int res) {
					DBDoErr(db_throw_on_error(string_format("dbconn::SyncDoUpdates %i", i)), adb, stmt, res);
				});
			}
			if (!SyncWriteDBVersion(adb)) {
				throw std::runtime_error(string_format("Failed to write DB version: %d, %s", sqlite3_errcode(adb), cstr(sqlite3_errmsg(adb))));
			}
			DBExec(adb, "COMMIT;", db_throw_on_error("dbconn::SyncDoUpdates (unlock)"));
			finaliser.cancel();
		} else if (current_db_version > db_version) {
			LogMsgFormat(LOGT::DBERR, "dbconn::SyncDoUpdates current DB version %u > %u", current_db_version, db_version);

			wxMessageDialog(nullptr, wxString::Format(wxT("Sorry, this database cannot be read.\nIt is version %u, this program can only read up to version %u, please upgrade.\n"),
				current_db_version, db_version),
				wxT("Error: database too new"), wxOK | wxICON_ERROR).ShowModal();
			return false;
		}
	} catch (std::exception &e) {
		std::string msg;
		if (have_version) {
			msg = string_format("Database upgrade failed. Could not upgrade from version %u to %u.\n\n%s", current_db_version, db_version, cstr(e.what()));
		} else {
			msg = string_format("Database upgrade failed. Could not determine database version.\n\n%s", cstr(e.what()));
		}
		wxMessageDialog(nullptr, wxstrstd(msg), wxT("Error: database upgrade failed"), wxOK | wxICON_ERROR).ShowModal();
		return false;
	}

	LogMsg(LOGT::DBINFO, "dbconn::SyncDoUpdates end");
	return true;
}

bool dbconn::SyncCheckReadOnlyDBVersion(sqlite3 *adb) {
	LogMsg(LOGT::DBINFO, "dbconn::SyncCheckReadOnlyDBVersion start");

	unsigned int current_db_version = 0;

	sqlite3_stmt *getstmt = cache.GetStmt(adb, DBPSC_SELSTATICSETTING);
	sqlite3_bind_text(getstmt, 1, "dbversion", -1, SQLITE_STATIC);
	DBRowExec(adb, getstmt, [&](sqlite3_stmt *stmt) {
		current_db_version = (unsigned int) sqlite3_column_int64(stmt, 0);
	}, "dbconn::SyncCheckReadOnlyDBVersion (get DB version)");

	if (current_db_version > db_version) {
		LogMsgFormat(LOGT::DBERR, "dbconn::SyncCheckReadOnlyDBVersion current DB version %u > %u", current_db_version, db_version);

		wxMessageDialog(nullptr, wxString::Format(wxT("Sorry, this database cannot be read.\nIt is version %u, this program can only read up to version %u, please upgrade.\n"),
				current_db_version, db_version), wxT("Error: database too new"), wxOK | wxICON_ERROR).ShowModal();
		return false;
	} else if (current_db_version < db_version) {
		LogMsgFormat(LOGT::DBERR, "dbconn::SyncCheckReadOnlyDBVersion current DB version %u < %u", current_db_version, db_version);

		wxMessageDialog(nullptr, wxString::Format(wxT("Sorry, this database cannot be read.\nIt is version %u, and cannot be upgraded to version %u in read-only mode.\n"),
				current_db_version, db_version), wxT("Error: read-only database not upgradable"), wxOK | wxICON_ERROR).ShowModal();
		return false;
	}

	LogMsg(LOGT::DBINFO, "dbconn::SyncCheckReadOnlyDBVersion end");
	return true;
}

void dbconn::SyncDoUpdates_FillUserDMIndexes(sqlite3 *adb) {
	LogMsg(LOGT::DBINFO, "dbconn::SyncDoUpdates_FillUserDMIndexes start");

	container::map<uint64_t, std::deque<uint64_t> > dm_index_map;

	DBBindRowExec(adb, "SELECT id, userid, userrecipid FROM tweets WHERE flags & ?;",
		[&](sqlite3_stmt *stmt) {
			sqlite3_bind_int64(stmt, 1, tweet_flags::GetFlagValue('D'));
		},
		[&](sqlite3_stmt *stmt) {
			uint64_t id = (uint64_t) sqlite3_column_int64(stmt, 0);
			uint64_t userid = (uint64_t) sqlite3_column_int64(stmt, 1);
			uint64_t userrecipid = (uint64_t) sqlite3_column_int64(stmt, 2);
			dm_index_map[userid].push_back(id);
			dm_index_map[userrecipid].push_back(id);
		},
		db_throw_on_error("dbconn::SyncDoUpdates_FillUserDMIndexes (DM listing)"));

	DBRangeBindExec(adb, "INSERT OR REPLACE INTO userdmsets(userid, dmindex) VALUES (?, ?);",
		dm_index_map.begin(), dm_index_map.end(),
		[&](sqlite3_stmt *stmt, const std::pair<uint64_t, std::deque<uint64_t>> &it) {
			sqlite3_bind_int64(stmt, 1, it.first);
			bind_compressed(stmt, 2, settocompressedblob_zigzag(it.second));
		},
		db_throw_on_error("dbconn::SyncDoUpdates_FillUserDMIndexes (DM index write back)"));
	LogMsg(LOGT::DBINFO, "dbconn::SyncDoUpdates_FillUserDMIndexes end");
}

void dbconn::SyncDoUpdates_FillTweetXrefTable(sqlite3 *adb) {
	LogMsg(LOGT::DBINFO, "dbconn::SyncDoUpdates_FillTweetXrefTable start");

	sqlite3_stmt *insxrefstmt = cache.GetStmt(adb, DBPSC_INSERTTWEETXREF);

	std::vector<uint64_t> xref_ids;

	DBRowExec(adb, "SELECT id, statjson, rtid FROM tweets;", [&](sqlite3_stmt *getstmt) {
		xref_ids.clear();

		uint64_t id = (uint64_t) sqlite3_column_int64(getstmt, 0);
		uint64_t rtid = (uint64_t) sqlite3_column_int64(getstmt, 2);

		if (rtid) xref_ids.push_back(rtid);

		rapidjson::Document dc;
		db_bind_buffer<dbb_uncompressed> json = column_get_compressed_and_parse(getstmt, 1, dc);
		if (dc.IsObject()) {
			uint64_t replyid;
			if (parse_util::CheckTransJsonValueDef(replyid, dc, "in_reply_to_status_id", 0)) {
				if (replyid) xref_ids.push_back(replyid);
			}

			uint64_t quoted_status_id;
			if (parse_util::CheckTransJsonValueDef(quoted_status_id, dc, "quoted_status_id", 0)) {
				if (quoted_status_id) xref_ids.push_back(quoted_status_id);
			}

			const rapidjson::Value &entv = dc["entities"];
			if (entv.IsObject()) {
				auto &urls = entv["urls"];
				if (urls.IsArray()) {
					for (rapidjson::SizeType i = 0; i < urls.Size(); i++) {
						std::string url_str;
						if (parse_util::CheckTransJsonValue(url_str, urls[i], "expanded_url") || parse_util::CheckTransJsonValue(url_str, urls[i], "display_url")) {
							wxURI wxuri(wxstrstd(url_str));
							uint64_t quoted_tweet_id = genjsonparser::ParseQuotedTweetUrl(wxuri);
							if (quoted_tweet_id) {
								xref_ids.push_back(quoted_tweet_id);
							}
						}
					}
				}
			}
		}

		if (!xref_ids.empty()) {
			// remove duplicates
			std::sort(xref_ids.begin(), xref_ids.end());
			xref_ids.erase(std::unique(xref_ids.begin(), xref_ids.end()), xref_ids.end());

			DBRangeBindExec(adb, insxrefstmt, xref_ids.begin(), xref_ids.end(),
					[&](sqlite3_stmt *incstmt, uint64_t xref) {
						sqlite3_bind_int64(incstmt, 1, (sqlite3_int64) id);
						sqlite3_bind_int64(incstmt, 2, (sqlite3_int64) xref);
					},
					"dbconn::SyncDoUpdates_FillTweetXrefTable (replyid xref)");
		}
	}, db_throw_on_error("dbconn::SyncDoUpdates_FillTweetXrefTable (table scan)"));
	LogMsg(LOGT::DBINFO, "dbconn::SyncDoUpdates_FillTweetXrefTable end");
}

// returns true if OK
bool dbconn::SyncWriteDBVersion(sqlite3 *adb) {
	sqlite3_stmt *stmt = cache.GetStmt(adb, DBPSC_INSSTATICSETTING);
	sqlite3_bind_text(stmt, 1, "dbversion", -1, SQLITE_STATIC);
	sqlite3_bind_int64(stmt, 2, db_version);
	return DBExec(adb, stmt, "dbconn::SyncWriteDBVersion");
}
