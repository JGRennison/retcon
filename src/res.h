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

#ifndef HGUARD_SRC_RES
#define HGUARD_SRC_RES

#include "univdefs.h"
#include <wx/image.h>
#include <wx/bitmap.h>

#define IMG_EXTERN(obj) \
	extern "C" unsigned char res_##obj##_png_start[] asm("_binary_src_res_" #obj "_png_start"); \
	extern "C" unsigned char res_##obj##_png_end[] asm("_binary_src_res_" #obj "_png_end");
#define IMG_MACRO_DIM(name, obj) \
	IMG_EXTERN(obj) \
	inline wxBitmap Get##name##IconDim(int dim) { \
		return MkScaledBitmap(MkImage(res_##obj##_png_start, res_##obj##_png_end-res_##obj##_png_start), dim, dim); \
	}
#define IMG_MACRO(name, obj) \
	IMG_EXTERN(obj) \
	inline void Get##name##Icon(wxBitmap *bmp, wxImage *img) { \
		MkImgBitmap(MkImage(res_##obj##_png_start, res_##obj##_png_end-res_##obj##_png_start), bmp, img); \
	}

wxBitmap MkScaledBitmap(const wxImage &img, int maxx, int maxy);
void MkImgBitmap(const wxImage &inimg, wxBitmap *bmp, wxImage *img);
wxImage MkImage(unsigned char *data, size_t len);

IMG_MACRO_DIM(Arrow, dmarrow)
IMG_MACRO(Bird, blue_bird_32)
IMG_MACRO(Fav, favorite)
IMG_MACRO(FavOn, favorite_on)
IMG_MACRO(Info, info)
IMG_MACRO(Lock, Lock)
IMG_MACRO(Reply, reply)
IMG_MACRO(Retweet, retweet)
IMG_MACRO(RetweetOn, retweet_on)
IMG_MACRO(DMreply, dm_reply)
IMG_MACRO(Verified, verified)
IMG_MACRO(Close, close)
IMG_MACRO(MultiUnread, multi_unread)

#endif
