#include "res.h"
#include <wx/mstream.h>
#include <algorithm>

wxBitmap MkScaledBitmap(const wxImage &img, int maxx, int maxy) {
	if(img.GetHeight()>maxy || img.GetWidth()>maxx) {
		double scalefactor=std::min((double) maxx / (double) img.GetWidth(), (double) maxy / (double) img.GetHeight());
		int newwidth = (double) img.GetWidth() * scalefactor;
		int newheight = (double) img.GetHeight() * scalefactor;
		return wxBitmap(img.Scale(std::lround(newwidth), std::lround(newheight), wxIMAGE_QUALITY_HIGH));
	}
	else return wxBitmap(img);
}

wxImage MkImage(unsigned char *data, size_t len) {
	wxMemoryInputStream memstream(data, len);
	wxImage img(memstream);
	return img;
}
