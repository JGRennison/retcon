#include <wx/image.h>
#include <wx/bitmap.h>

extern unsigned char arrow_icon_png[];
extern size_t arrow_icon_png_in_bytes;

wxBitmap MkScaledBitmap(const wxImage &img, int maxx, int maxy);
wxImage MkImage(unsigned char *data, size_t len);
wxBitmap GetArrowIcon(int dim);
