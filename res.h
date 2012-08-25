#include <wx/image.h>
#include <wx/bitmap.h>

#define IMG_MACRO(name, obj) \
	extern "C" unsigned char binary_res_##obj##_png_start[]; \
	extern "C" unsigned char binary_res_##obj##_png_end[]; \
	inline wxBitmap Get##name##Icon(int dim) { \
		return MkScaledBitmap(MkImage(binary_res_##obj##_png_start, binary_res_##obj##_png_end-binary_res_##obj##_png_start), dim, dim); \
	}

wxBitmap MkScaledBitmap(const wxImage &img, int maxx, int maxy);
wxImage MkImage(unsigned char *data, size_t len);

IMG_MACRO(Arrow, dmarrow)
IMG_MACRO(Bird, blue_bird_32)
IMG_MACRO(Fav, favorite)
IMG_MACRO(FavOn, favorite_on)
IMG_MACRO(Info, info)
IMG_MACRO(Lock, Lock)
IMG_MACRO(Reply, reply)
IMG_MACRO(Retweet, retweet)
IMG_MACRO(RetweetOn, retweet_on)
