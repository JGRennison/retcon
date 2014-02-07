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

#ifndef HGUARD_SRC_DB_CFG
#define HGUARD_SRC_DB_CFG

#include "univdefs.h"
#include <wx/string.h>

struct sqlite3;
struct sqlite3_stmt;

struct DBGenConfig {
	void SetDBIndexGlobal();
	void SetDBIndex(unsigned int id);
	DBGenConfig(sqlite3 *db_);

	protected:
	unsigned int dbindex;
	bool dbindex_global;
	sqlite3 *db;
	void bind_accid_name(sqlite3_stmt *stmt, const char *name);
};

struct DBWriteConfig : public DBGenConfig {
	void WriteUTF8(const char *name, const char *strval);
	void WriteWX(const char *name, const wxString &strval) { WriteUTF8(name, strval.ToUTF8()); }
	void WriteInt64(const char *name, int64_t val);
	void Delete(const char *name);
	void DeleteAll();
	DBWriteConfig(sqlite3 *db);
	~DBWriteConfig();

	protected:
	sqlite3_stmt *stmt;
	sqlite3_stmt *delstmt;
	void exec(sqlite3_stmt *stmt);
};

struct DBReadConfig : public DBGenConfig {
	bool Read(const char *name, wxString *strval, const wxString &defval);
	bool ReadInt64(const char *name, int64_t *strval, int64_t defval);
	bool ReadBool(const char *name, bool *strval, bool defval);
	bool ReadUInt64(const char *name, uint64_t *strval, uint64_t defval);
	DBReadConfig(sqlite3 *db);
	~DBReadConfig();

	protected:
	sqlite3_stmt *stmt;
	bool exec(sqlite3_stmt *stmt);
};

#endif

