#include <wx/image.h>
#include <wx/bitmap.h>

#define IMG_MACRO_DIM(name, obj) \
	extern "C" unsigned char binary_res_##obj##_png_start[]; \
	extern "C" unsigned char binary_res_##obj##_png_end[]; \
	inline wxBitmap Get##name##IconDim(int dim) { \
		return MkScaledBitmap(MkImage(binary_res_##obj##_png_start, binary_res_##obj##_png_end-binary_res_##obj##_png_start), dim, dim); \
	}
#define IMG_MACRO(name, obj) \
	extern "C" unsigned char binary_res_##obj##_png_start[]; \
	extern "C" unsigned char binary_res_##obj##_png_end[]; \
	inline void Get##name##Icon(wxBitmap *bmp, wxImage *img) { \
		MkImgBitmap(MkImage(binary_res_##obj##_png_start, binary_res_##obj##_png_end-binary_res_##obj##_png_start), bmp, img); \
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
