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
#include "fileutil.h"
#include "util.h"
#include "hash.h"
#include "libtwitcurl/SHA1.h"
#include <wx/file.h>
#include <wx/mstream.h>
#include <wx/image.h>
#include <wx/filename.h>
#include <stdio.h>
#include <random>


bool LoadFromFileAndCheckHash(const wxString &filename, shb_iptr hash, std::string &out) {
	if(!hash) return false;
	wxFile file;
	bool opened = file.Open(filename);
	if(opened) {
		wxFileOffset len = file.Length();
		if(len >= 0 && len < (50 << 20)) {    //don't load empty or absurdly large files
			out.resize(len);
			size_t size = file.Read(&out[0], len);
			if(size == (size_t) len) {
				CSHA1 hashblk;
				hashblk.Update(reinterpret_cast<const unsigned char*>(out.data()), len);
				hashblk.Final();
				if(memcmp(hashblk.GetHashPtr(), hash->hash_sha1, 20) == 0) {
					return true;
				}
			}
			out.clear();
		}
	}
	return false;
}

bool LoadImageFromFileAndCheckHash(const wxString &filename, shb_iptr hash, wxImage &img) {
	if(!hash) return false;
	std::string data;
	bool success = false;
	if(LoadFromFileAndCheckHash(filename, hash, data)) {
		wxMemoryInputStream memstream(data.data(), data.size());
		if(img.LoadFile(memstream, wxBITMAP_TYPE_ANY)) {
			success = true;
		}
	}
	return success;
}

temp_file_holder::temp_file_holder() {
}

temp_file_holder::temp_file_holder(const std::string &name) {
	filename = name;
}

temp_file_holder::temp_file_holder(temp_file_holder &&other) {
	*this = std::move(other);
}

temp_file_holder::~temp_file_holder() {
	Reset();
}

temp_file_holder& temp_file_holder::operator=(temp_file_holder &&other) {
	Reset();
	filename = std::move(other.filename);
	other.filename.clear();
	return *this;
}

void temp_file_holder::AddToSet(magic_ptr_container<temp_file_holder> &fileset) {
	fileset.insert(this);
}

void temp_file_holder::Init(const std::string &name) {
	Reset();
	filename = name;
}

void temp_file_holder::Reset() {
	if(!filename.empty()) {
		remove(filename.c_str());
	}
}

std::string make_temp_dir(const std::string &prefix) {
	std::random_device rd;
	std::mt19937 gen(rd());
	std::uniform_int_distribution<> dis(0, 255);

	for(unsigned int i = 0; i < 256; i++) {
		std::string lastpart = prefix;
		for (int n = 0; n < 15; n++)
			hexify_char(lastpart, (unsigned char) dis(gen));

		wxFileName filename;
		filename.AssignDir(wxFileName::GetTempDir());
		filename.AppendDir(wxstrstd(lastpart));
		filename.MakeAbsolute();
		if(filename.Mkdir(0700))
			return stdstrwx(filename.GetFullPath());
	}
	return stdstrwx(wxFileName::GetTempDir());
}
