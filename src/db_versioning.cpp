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
#include <wx/msgdlg.h>

static const unsigned int db_version = 5;

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
	"UPDATE OR IGNORE users SET dmindex = NULL;"
	// SyncDoUpdates_FillUserDMIndexes should be run here
	,
	nullptr
	// DB set compression changes
};

// return false if all bets are off and DB should not be read
bool dbconn::SyncDoUpdates(sqlite3 *adb) {
	LogMsg(LOGT::DBTRACE, "dbconn::DoUpdates start");

	unsigned int current_db_version = 0;

	sqlite3_stmt *getstmt = cache.GetStmt(adb, DBPSC_SELSTATICSETTING);
	sqlite3_bind_text(getstmt, 1, "dbversion", -1, SQLITE_STATIC);
	DBRowExec(adb, getstmt, [&](sqlite3_stmt *stmt) {
		current_db_version = (unsigned int) sqlite3_column_int64(stmt, 0);
	}, "dbconn::DoUpdates (get DB version)");

	if(current_db_version < db_version) {
		cache.BeginTransaction(adb);
		LogMsgFormat(LOGT::DBTRACE, "dbconn::DoUpdates updating from %u to %u", current_db_version, db_version);
		for(unsigned int i = current_db_version; i < db_version; i++) {
			const char *sql = update_sql[i];
			if(!sql) continue;

			int res = sqlite3_exec(adb, sql, 0, 0, 0);
			if(res != SQLITE_OK) {
				LogMsgFormat(LOGT::DBERR, "dbconn::DoUpdates %u got error: %d (%s)", i, res, cstr(sqlite3_errmsg(adb)));
			}
			if(i == 3) {
				SyncDoUpdates_FillUserDMIndexes(adb);
			}
		}
		SyncWriteDBVersion(adb);
		cache.EndTransaction(adb);
	}
	else if(current_db_version > db_version) {
		LogMsgFormat(LOGT::DBERR, "dbconn::DoUpdates current DB version %u > %u", current_db_version, db_version);

		wxMessageDialog(0, wxString::Format(wxT("Sorry, this database cannot be read.\nIt is version %u, this program can only read up to version %u, please upgrade.\n"),
			current_db_version, db_version),
			wxT("Error: database too new"), wxOK | wxICON_ERROR ).ShowModal();
		return false;
	}

	LogMsg(LOGT::DBTRACE, "dbconn::DoUpdates end");
	return true;
}

void dbconn::SyncDoUpdates_FillUserDMIndexes(sqlite3 *adb) {
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
		"dbconn::SyncDoUpdates_FillUserDMIndexes (DM listing)");

	DBRangeBindExec(adb, "INSERT OR REPLACE INTO userdmsets(userid, dmindex) VALUES (?, ?);",
		dm_index_map.begin(), dm_index_map.end(),
		[&](sqlite3_stmt *stmt, const std::pair<uint64_t, std::deque<uint64_t>> &it) {
			sqlite3_bind_int64(stmt, 1, it.first);
			bind_compressed(stmt, 2, settocompressedblob_zigzag(it.second));
		},
		"dbconn::SyncDoUpdates_FillUserDMIndexes (DM index write back)");
}

void dbconn::SyncWriteDBVersion(sqlite3 *adb) {
	sqlite3_stmt *stmt = cache.GetStmt(adb, DBPSC_INSSTATICSETTING);
	sqlite3_bind_text(stmt, 1, "dbversion", -1, SQLITE_STATIC);
	sqlite3_bind_int64(stmt, 2, db_version);
	DBExec(adb, stmt, "dbconn::SyncWriteDBVersion");
}
