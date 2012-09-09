//  retcon
//
//  WEBSITE: http://retcon.sourceforge.net
//
//  NOTE: This software is licensed under the GPL. See: COPYING-GPL.txt
//
//  This program  is distributed in the  hope that it will  be useful, but
//  WITHOUT   ANY  WARRANTY;   without  even   the  implied   warranty  of
//  MERCHANTABILITY  or FITNESS  FOR A  PARTICULAR PURPOSE.   See  the GNU
//  General Public License for more details.
//
//  Jonathan Rennison (or anybody else) is in no way responsible, or liable
//  for this program or its use in relation to users, 3rd parties or to any
//  persons in any way whatsoever.
//
//  You  should have  received a  copy of  the GNU  General Public
//  License along  with this program; if  not, write to  the Free Software
//  Foundation, Inc.,  59 Temple Place,  Suite 330, Boston,  MA 02111-1307
//  USA
//
//  2012 - j.g.rennison@gmail.com
//==========================================================================

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

void MkImgBitmap(const wxImage &inimg, wxBitmap *bmp, wxImage *img) {
	if(img) *img=inimg;
	if(bmp) *bmp=wxBitmap(inimg);
}

wxImage MkImage(unsigned char *data, size_t len) {
	wxMemoryInputStream memstream(data, len);
	wxImage img(memstream);
	return img;
}
