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

#ifndef HGUARD_SRC_FILEUTIL
#define HGUARD_SRC_FILEUTIL

#include "univdefs.h"
#include "hash.h"
#include "safe_observer_ptr.h"
#include <wx/string.h>
#include <string>
#include <memory>

class wxImage;

bool LoadFromFile(const wxString &filename, std::string &out);
bool LoadImageFromFileAndCheckHash(const wxString &filename, shb_iptr hash, wxImage &img);
bool LoadFromFileAndCheckHash(const wxString &filename, shb_iptr hash, std::string &out);

struct temp_file_holder : public safe_observer_ptr_contained<temp_file_holder> {
	private:
	std::string filename;

	public:
	temp_file_holder();
	temp_file_holder(const std::string &name);
	temp_file_holder(temp_file_holder &&other);
	temp_file_holder(const temp_file_holder &other) = delete;
	~temp_file_holder();
	temp_file_holder& operator=(temp_file_holder &&other);
	temp_file_holder& operator=(const temp_file_holder &other) = delete;
	void AddToSet(safe_observer_ptr_container<temp_file_holder> &fileset);
	void Init(const std::string &name);
	void Reset();
	const std::string &GetFilename() { return filename; }
	bool IsValid() { return !filename.empty(); }
};

std::string make_temp_dir(const std::string &prefix);

#endif
