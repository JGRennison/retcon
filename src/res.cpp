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
#include "res.h"
#include <wx/mstream.h>
#include <algorithm>

wxBitmap MkScaledBitmap(const wxImage &img, int maxx, int maxy) {
	if(img.GetHeight() > maxy || img.GetWidth() > maxx) {
		double scalefactor = std::min((double) maxx / (double) img.GetWidth(), (double) maxy / (double) img.GetHeight());
		int newwidth = (double) img.GetWidth() * scalefactor;
		int newheight = (double) img.GetHeight() * scalefactor;
		return wxBitmap(img.Scale(std::lround(newwidth), std::lround(newheight), wxIMAGE_QUALITY_HIGH));
	}
	else return wxBitmap(img);
}

void MkImgBitmap(const wxImage &inimg, wxBitmap *bmp, wxImage *img) {
	if(img) *img = inimg;
	if(bmp) *bmp = wxBitmap(inimg);
}

wxImage MkImage(unsigned char *data, size_t len) {
	wxMemoryInputStream memstream(data, len);
	wxImage img(memstream);
	return img;
}
