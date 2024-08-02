/* See LICENSE file for copyright and license details. */
#include "cJSON/cJSON-dunc.h"

// lrpad formula based on Fnt object F;
#define LRPAD(F)		(4 * (F)->h / 5)

typedef struct {
	Cursor cursor;
} Cur;

typedef struct Fnt {
	Display *dpy;
	unsigned int h;
	XftFont *xfont;
	FcPattern *pattern;
	#if PATCH_FONT_GROUPS
	unsigned int ellipsis_width;
	unsigned int lrpad;
	#endif // PATCH_FONT_GROUPS
	struct Fnt *next;
} Fnt;

#if PATCH_FONT_GROUPS
typedef struct FntGrp {
	const char *name;
	Fnt *fonts;
} FntGrp;
#endif // PATCH_FONT_GROUPS

enum { ColFg, ColBg, ColBorder }; /* Clr scheme index */
typedef XftColor Clr;

typedef struct {
	unsigned int w, h;
	Display *dpy;
	int screen;
	Window root;
	#if PATCH_ALPHA_CHANNEL
	int useargb;
    Visual *visual;
    unsigned int depth;
    Colormap cmap;
	#endif // PATCH_ALPHA_CHANNEL
	Drawable drawable;
	#if PATCH_WINDOW_ICONS
	Picture picture;
	#endif // PATCH_WINDOW_ICONS
	GC gc;
	Clr *scheme;
	#if PATCH_TWO_TONE_TITLE
	int bg2;
	#endif // PATCH_TWO_TONE_TITLE
	Fnt *fonts;
	#if PATCH_FONT_GROUPS
	int numfontgroups;		// number of font groups;
	FntGrp **fontgroups;	// array of font groups;
	Fnt *selfonts;			// selected font group, NULL to use default fonts;
	#endif // PATCH_FONT_GROUPS
} Drw;

/* Drawable abstraction */
#if PATCH_ALPHA_CHANNEL
Drw *drw_create(Display *dpy, int screen, Window win, unsigned int w, unsigned int h, int useargb, Visual *visual, unsigned int depth, Colormap cmap);
#else // NO PATCH_ALPHA_CHANNEL
Drw *drw_create(Display *dpy, int screen, Window win, unsigned int w, unsigned int h);
#endif // PATCH_ALPHA_CHANNEL
void drw_resize(Drw *drw, unsigned int w, unsigned int h);
void drw_free(Drw *drw);

/* Fnt abstraction */
Fnt *drw_fontset_create(Drw* drw, const char *fonts[], size_t fontcount);
Fnt *drw_fontset_create_json(Drw* drw, cJSON *fonts);
void drw_fontset_free(Fnt* set);
unsigned int drw_fontset_getwidth(Drw *drw, const char *text);
unsigned int drw_fontset_getwidth_clamp(Drw *drw, const char *text, unsigned int n);
void drw_font_getexts(Fnt *font, const char *text, unsigned int len, unsigned int *w, unsigned int *h);

/* Colorscheme abstraction */
void drw_clr_create(Drw *drw, Clr *dest, const char *clrname);
Clr *drw_scm_create(Drw *drw, char *clrnames[], size_t clrcount);

/* Cursor abstraction */
Cur *drw_cur_create(Drw *drw, int shape);
void drw_cur_free(Drw *drw, Cur *cursor);

/* Drawing context manipulation */
void drw_setfontset(Drw *drw, Fnt *set);
void drw_setscheme(Drw *drw, Clr *scm);

#if PATCH_WINDOW_ICONS
Picture drw_picture_create_resized(Drw *drw, char *src, unsigned int src_w, unsigned int src_h, unsigned int dst_w, unsigned int dst_h);
#if PATCH_WINDOW_ICONS_CUSTOM_ICONS || PATCH_WINDOW_ICONS_DEFAULT_ICON
Picture drw_picture_create_resized_from_file(Drw *drw, char *src_file, unsigned int *picw, unsigned int *pich, unsigned int iconsize);
#endif // PATCH_WINDOW_ICONS_CUSTOM_ICONS || PATCH_WINDOW_ICONS_DEFAULT_ICON
#endif // PATCH_WINDOW_ICONS

/* Drawing functions */
void drw_ellipse(Drw *drw, int x, int y, unsigned int w, unsigned int h, int filled, int invert);
#if PATCH_TWO_TONE_TITLE
void drw_gradient(Drw *drw, int x, int y, unsigned int w, unsigned int h, unsigned long col1, unsigned long col2, int invert);
#endif // PATCH_TWO_TONE_TITLE
void drw_rect(Drw *drw, int x, int y, unsigned int w, unsigned int h, int filled, int invert);
#if PATCH_CLIENT_INDICATORS
int drw_text(Drw *drw, int x, int y, unsigned int w, unsigned int h, unsigned int lpad, unsigned int rpad, int tpad, const char *text, int invert);
#else // NO PATCH_CLIENT_INDICATORS
int drw_text(Drw *drw, int x, int y, unsigned int w, unsigned int h, unsigned int lpad, unsigned int rpad, const char *text, int invert);
#endif // PATCH_CLIENT_INDICATORS
#if PATCH_WINDOW_ICONS
void drw_pic(Drw *drw, int x, int y, unsigned int w, unsigned int h, Picture pic);
#endif // PATCH_WINDOW_ICONS

/* Map functions */
void drw_map(Drw *drw, Window win, int x, int y, unsigned int w, unsigned int h);
void drw_maptrans(Drw *drw, Window win, int srcx, int srcy, unsigned int w, unsigned int h, int destx, int desty);

#if PATCH_FONT_GROUPS
Fnt *drw_get_fontgroup_fonts(Drw *drw, char *groupname);
int drw_select_fontgroup(Drw *drw, char *groupname);
int drw_populate_fontgroups(Drw *drw, cJSON *fontgroup_array);
FntGrp *drw_fontgroup_create_json(Drw *drw, cJSON *fontgroup);
#endif // PATCH_FONT_GROUPS
