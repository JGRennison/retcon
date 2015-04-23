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

#ifndef HGUARD_SRC_JSON_UTIL
#define HGUARD_SRC_JSON_UTIL

#include "univdefs.h"
#include "rapidjson-inc.h"
#include "json-common.h"
#include <string>

namespace parse_util {

	template <typename C> bool IsType(const rapidjson::Value &val);
	template <> inline bool IsType<bool>(const rapidjson::Value &val) { return val.IsBool(); }
	template <> inline bool IsType<unsigned int>(const rapidjson::Value &val) { return val.IsUint(); }
	template <> inline bool IsType<int>(const rapidjson::Value &val) { return val.IsInt(); }
	template <> inline bool IsType<uint64_t>(const rapidjson::Value &val) { return val.IsUint64(); }
	template <> inline bool IsType<int64_t>(const rapidjson::Value &val) { return val.IsInt64(); }
	template <> inline bool IsType<const char*>(const rapidjson::Value &val) { return val.IsString(); }
	template <> inline bool IsType<std::string>(const rapidjson::Value &val) { return val.IsString(); }

	template <typename C> C GetType(const rapidjson::Value &val);
	template <> inline bool GetType<bool>(const rapidjson::Value &val) { return val.GetBool(); }
	template <> inline unsigned int GetType<unsigned int>(const rapidjson::Value &val) { return val.GetUint(); }
	template <> inline int GetType<int>(const rapidjson::Value &val) { return val.GetInt(); }
	template <> inline uint64_t GetType<uint64_t>(const rapidjson::Value &val) { return val.GetUint64(); }
	template <> inline int64_t GetType<int64_t>(const rapidjson::Value &val) { return val.GetInt64(); }
	template <> inline const char* GetType<const char*>(const rapidjson::Value &val) { return val.GetString(); }
	template <> inline std::string GetType<std::string>(const rapidjson::Value &val) { return val.GetString(); }

	inline const rapidjson::Value &GetSubProp(const rapidjson::Value &val, const char *prop) {
		if(prop)
			return val[prop];
		else
			return val;
	}

	template <typename C> bool CheckTransJsonValue(C &var, const rapidjson::Value &val,
			const char *prop, Handler *handler = nullptr) {
		const rapidjson::Value &subval = GetSubProp(val, prop);
		bool res = IsType<C>(subval);
		if(res)
			var = GetType<C>(subval);
		if(res && handler) {
			handler->String(prop);
			subval.Accept(*handler);
		}
		return res;
	}

	template <typename C, typename D> bool CheckTransJsonValueDef(C &var, const rapidjson::Value &val,
			const char *prop, const D def, Handler *handler = nullptr) {
		bool res = CheckTransJsonValue<C>(var, val, prop, handler);
		if(!res)
			var = def;
		return res;
	}

	template <typename C, typename D> bool CheckTransJsonValueDefFlag(C &var, D flagmask, const rapidjson::Value &val,
			const char *prop, bool def, Handler *handler = nullptr) {
		const rapidjson::Value &subval = GetSubProp(val, prop);
		bool res = IsType<bool>(subval);
		bool flagval = res ? GetType<bool>(subval):def;
		if(flagval) var |= flagmask;
		else var &= ~flagmask;
		if(res && handler) {
			handler->String(prop);
			subval.Accept(*handler);
		}
		return res;
	}

	template <typename C, typename D> bool CheckTransJsonValueDefTrackChanges(bool &changeflag, C &var, const rapidjson::Value &val,
			const char *prop, const D def, Handler *handler = nullptr) {
		C oldvar = var;
		bool result = CheckTransJsonValueDef(var, val, prop, def, handler);
		if(var != oldvar) changeflag = true;
		return result;
	}

	template <typename C, typename D> bool CheckTransJsonValueDefFlagTrackChanges(bool &changeflag, C &var, D flagmask, const rapidjson::Value &val,
			const char *prop, bool def, Handler *handler = nullptr) {
		C oldvar = var;
		bool result = CheckTransJsonValueDefFlag(var, flagmask, val, prop, def, handler);
		if((var & flagmask) != (oldvar & flagmask)) changeflag = true;
		return result;
	}

	template <typename C, typename D> C CheckGetJsonValueDef(const rapidjson::Value &val, const char *prop, const D def,
			Handler *handler = nullptr, bool *hadval = nullptr) {
		const rapidjson::Value &subval = GetSubProp(val, prop);
		bool res = IsType<C>(subval);
		if(res && handler) {
			handler->String(prop);
			subval.Accept(*handler);
		}
		if(hadval) *hadval = res;
		return res ? GetType<C>(subval) : def;
	}

	bool ParseStringInPlace(rapidjson::Document &dc, char *mutable_string, const std::string &name);
	void DisplayParseErrorMsg(rapidjson::Document &dc, const std::string &name, const char *data);

};

#endif

