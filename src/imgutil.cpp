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
#include "imgutil.h"

#include <wx/image.h>

#ifdef USE_VIPS
#include <vips/vips.h>
#endif

wxImage ScaleImage(const wxImage &input, double scale) {
#ifdef USE_VIPS
	VipsImage *src = im_image(input.GetData(),
			input.GetWidth(), input.GetHeight(), 3, VIPS_FORMAT_UCHAR);

	int width = input.GetWidth() * scale;
	int height = input.GetHeight() * scale;
	wxImage output(width, height, false);

	VipsImage *dest = im_image(output.GetData(), output.GetWidth(), output.GetHeight(), 3, VIPS_FORMAT_UCHAR);

#if VIPS_MAJOR_VERSION > 7 || (VIPS_MAJOR_VERSION == 7 && VIPS_MINOR_VERSION >= 41)
	VipsImage *resized = nullptr;
	vips_resize(src, &resized, scale, nullptr);
	if(resized)
		vips_image_write(resized, dest);
	g_object_unref(resized);
#else
	im_resize_linear(src, dest, width, height);
#endif

	g_object_unref(src);
	g_object_unref(dest);

	return output;
#else
	return input.Scale(width, height, wxIMAGE_QUALITY_HIGH);
#endif
}
