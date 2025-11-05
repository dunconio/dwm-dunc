/* See LICENSE file for copyright and license details. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <X11/Xlib.h>
#include <X11/Xft/Xft.h>
#include <Imlib2.h>

#include "patches.h"
#include "drw.h"
#include "util.h"
#include "cJSON/cJSON-dunc.c"

#define UTF_INVALID 0xFFFD
#define UTF_SIZ     4

static const unsigned char utfbyte[UTF_SIZ + 1] = {0x80,    0, 0xC0, 0xE0, 0xF0};
static const unsigned char utfmask[UTF_SIZ + 1] = {0xC0, 0x80, 0xE0, 0xF0, 0xF8};
static const long utfmin[UTF_SIZ + 1] = {       0,    0,  0x80,  0x800,  0x10000};
static const long utfmax[UTF_SIZ + 1] = {0x10FFFF, 0x7F, 0x7FF, 0xFFFF, 0x10FFFF};

static const char *ellipsis = "…";	// replaces "...";

static long
utf8decodebyte(const char c, size_t *i)
{
	for (*i = 0; *i < (UTF_SIZ + 1); ++(*i))
		if (((unsigned char)c & utfmask[*i]) == utfbyte[*i])
			return (unsigned char)c & ~utfmask[*i];
	return 0;
}

static size_t
utf8validate(long *u, size_t i)
{
	if (!BETWEEN(*u, utfmin[i], utfmax[i]) || BETWEEN(*u, 0xD800, 0xDFFF))
		*u = UTF_INVALID;
	for (i = 1; *u > utfmax[i]; ++i)
		;
	return i;
}

static size_t
utf8decode(const char *c, long *u, size_t clen)
{
	size_t i, j, len, type;
	long udecoded;

	*u = UTF_INVALID;
	if (!clen)
		return 0;
	udecoded = utf8decodebyte(c[0], &len);
	if (!BETWEEN(len, 1, UTF_SIZ))
		return 1;
	for (i = 1, j = 1; i < clen && j < len; ++i, ++j) {
		udecoded = (udecoded << 6) | utf8decodebyte(c[i], &type);
		if (type)
			return j;
	}
	if (j < len)
		return 0;
	*u = udecoded;
	utf8validate(u, len);

	return len;
}

Drw *
#if PATCH_ALPHA_CHANNEL
drw_create(Display *dpy, int screen, Window root, unsigned int w, unsigned int h, int useargb, Visual *visual, unsigned int depth, Colormap cmap)
#else // NO PATCH_ALPHA_CHANNEL
drw_create(Display *dpy, int screen, Window root, unsigned int w, unsigned int h)
#endif // PATCH_ALPHA_CHANNEL
{
	Drw *drw = ecalloc(1, sizeof(Drw));

	drw->dpy = dpy;
	drw->screen = screen;
	drw->root = root;
	drw->w = w;
	drw->h = h;
	#if PATCH_TWO_TONE_TITLE
	drw->bg2 = 0;
	#endif // PATCH_TWO_TONE_TITLE
	#if PATCH_ALPHA_CHANNEL
	drw->useargb = useargb;
	if (drw->useargb) {
		drw->visual = visual;
		drw->depth = depth;
		drw->cmap = cmap;
		drw->drawable = XCreatePixmap(dpy, root, w, h, depth);
		drw->gc = XCreateGC(dpy, drw->drawable, 0, NULL);
		#if PATCH_WINDOW_ICONS || PATCH_CUSTOM_TAG_ICONS
		drw->picture = XRenderCreatePicture(dpy, drw->drawable, XRenderFindVisualFormat(dpy, drw->visual), 0, NULL);
		#endif // PATCH_WINDOW_ICONS || PATCH_CUSTOM_TAG_ICONS
	}
	else
	#endif // PATCH_ALPHA_CHANNEL
	{
		drw->drawable = XCreatePixmap(dpy, root, w, h, DefaultDepth(dpy, screen));
		drw->gc = XCreateGC(dpy, root, 0, NULL);
		#if PATCH_WINDOW_ICONS || PATCH_CUSTOM_TAG_ICONS
		drw->picture = XRenderCreatePicture(dpy, drw->drawable, XRenderFindVisualFormat(dpy, DefaultVisual(dpy, screen)), 0, NULL);
		#endif // PATCH_WINDOW_ICONS || PATCH_CUSTOM_TAG_ICONS
	}
	XSetLineAttributes(dpy, drw->gc, 1, LineSolid, CapButt, JoinMiter);

	return drw;
}

void
drw_resize(Drw *drw, unsigned int w, unsigned int h)
{
	if (!drw)
		return;

	drw->w = w;
	drw->h = h;
	#if PATCH_WINDOW_ICONS || PATCH_CUSTOM_TAG_ICONS
	if (drw->picture)
		XRenderFreePicture(drw->dpy, drw->picture);
	#endif // PATCH_WINDOW_ICONS || PATCH_CUSTOM_TAG_ICONS
	if (drw->drawable)
		XFreePixmap(drw->dpy, drw->drawable);
	#if PATCH_ALPHA_CHANNEL
	if (drw->useargb) {
		drw->drawable = XCreatePixmap(drw->dpy, drw->root, w, h, drw->depth);
		#if PATCH_WINDOW_ICONS || PATCH_CUSTOM_TAG_ICONS
		drw->picture = XRenderCreatePicture(drw->dpy, drw->drawable, XRenderFindVisualFormat(drw->dpy, drw->visual), 0, NULL);
		#endif // PATCH_WINDOW_ICONS || PATCH_CUSTOM_TAG_ICONS
	}
	else
	#endif // PATCH_ALPHA_CHANNEL
	{
		drw->drawable = XCreatePixmap(drw->dpy, drw->root, w, h, DefaultDepth(drw->dpy, drw->screen));
		#if PATCH_WINDOW_ICONS || PATCH_CUSTOM_TAG_ICONS
		drw->picture = XRenderCreatePicture(drw->dpy, drw->drawable, XRenderFindVisualFormat(drw->dpy, DefaultVisual(drw->dpy, drw->screen)), 0, NULL);
		#endif // PATCH_WINDOW_ICONS || PATCH_CUSTOM_TAG_ICONS
	}
}

#if PATCH_FONT_GROUPS
FntGrp *
drw_fontgroup_create_json(Drw *drw, cJSON *fontgroup)
{
	FntGrp *grp;
	cJSON *f, *g;

	if (!drw || !fontgroup || !cJSON_IsObject(fontgroup))
		return NULL;

	if (!(f = cJSON_GetObjectItemCaseSensitive(fontgroup, "name")) ||
		!cJSON_IsString(f) ||
		!(g = cJSON_GetObjectItemCaseSensitive(fontgroup, "fonts")) ||
		!cJSON_IsArray(g)
		)
		return NULL;

	grp = ecalloc(1, sizeof(FntGrp));
	grp->name = f->valuestring;
	grp->fonts = drw_fontset_create_json(drw, g);

	return grp;

}
#endif // PATCH_FONT_GROUPS

void
drw_free(Drw *drw)
{
	#if PATCH_WINDOW_ICONS || PATCH_CUSTOM_TAG_ICONS
	XRenderFreePicture(drw->dpy, drw->picture);
	#endif // PATCH_WINDOW_ICONS || PATCH_CUSTOM_TAG_ICONS
	XFreePixmap(drw->dpy, drw->drawable);
	XFreeGC(drw->dpy, drw->gc);
	drw_fontset_free(drw->fonts);
	#if PATCH_FONT_GROUPS
	if (drw->fontgroups) {
		for (int i = 0; i < drw->numfontgroups; i++)
			if (drw->fontgroups[i]) {
				drw_fontset_free(drw->fontgroups[i]->fonts);
				free(drw->fontgroups[i]);
			}
		free(drw->fontgroups);
	}
	#endif // PATCH_FONT_GROUPS
	free(drw);
}

#if PATCH_FONT_GROUPS
int
drw_populate_fontgroups(Drw *drw, cJSON *fontgroup_array)
{
	FntGrp *grp;
	cJSON *fg, *f, *g;
	int i, j = 0, n = 0;

	if (!drw || !fontgroup_array ||
		!((cJSON_IsArray(fontgroup_array) && (n = cJSON_GetArraySize(fontgroup_array)) > 0) ||
			cJSON_IsObject(fontgroup_array)
		)
	) {
		drw->fontgroups = NULL;
		return 0;
	}

	if (!n)
		n = 1;
	drw->numfontgroups = n;
	drw->fontgroups = (FntGrp **) malloc(n * sizeof(FntGrp *));

	for (i = 0; i < n; i++) {

		if (cJSON_IsArray(fontgroup_array))
			fg = cJSON_GetArrayItem(fontgroup_array, i);
		else
			fg = fontgroup_array;
		if (!(f = cJSON_GetObjectItemCaseSensitive(fg, "name")) ||
			!cJSON_IsString(f) ||
			!(g = cJSON_GetObjectItemCaseSensitive(fg, "fonts")) ||
			!cJSON_IsArray(g)
			)
			continue;

		grp = ecalloc(1, sizeof(FntGrp));
		grp->name = f->valuestring;
		grp->fonts = drw_fontset_create_json(drw, g);
		drw->fontgroups[j++] = grp;
	}

	drw->numfontgroups = j;
	return j;
}
#endif // PATCH_FONT_GROUPS

/* This function is an implementation detail. Library users should use
 * drw_fontset_create instead.
 */
static Fnt *
xfont_create(Drw *drw, const char *fontname, FcPattern *fontpattern)
{
	Fnt *font;
	XftFont *xfont = NULL;
	FcPattern *pattern = NULL;

	if (fontname) {
		/* Using the pattern found at font->xfont->pattern does not yield the
		 * same substitution results as using the pattern returned by
		 * FcNameParse; using the latter results in the desired fallback
		 * behaviour whereas the former just results in missing-character
		 * rectangles being drawn, at least with some fonts. */
		if (!(xfont = XftFontOpenName(drw->dpy, drw->screen, fontname))) {
			fprintf(stderr, "error, cannot load font from name: '%s'\n", fontname);
			return NULL;
		}
		if (!(pattern = FcNameParse((FcChar8 *) fontname))) {
			fprintf(stderr, "error, cannot parse font name to pattern: '%s'\n", fontname);
			XftFontClose(drw->dpy, xfont);
			return NULL;
		}
	} else if (fontpattern) {
		if (!(xfont = XftFontOpenPattern(drw->dpy, fontpattern))) {
			fprintf(stderr, "error, cannot load font from pattern.\n");
			return NULL;
		}
	} else {
		die("no font specified.");
	}

	#if PATCH_XFTLIB_EMOJI_WORKAROUND
	/* Do not allow using color fonts. This is a workaround for a BadLength
	 * error from Xft with color glyphs. Modelled on the Xterm workaround. See
	 * https://bugzilla.redhat.com/show_bug.cgi?id=1498269
	 * https://lists.suckless.org/dev/1701/30932.html
	 * https://bugs.debian.org/cgi-bin/bugreport.cgi?bug=916349
	 * and lots more all over the internet.
	 */
	FcBool iscol;
	if(FcPatternGetBool(xfont->pattern, FC_COLOR, 0, &iscol) == FcResultMatch && iscol) {
		XftFontClose(drw->dpy, xfont);
		return NULL;
	}
	#endif

	font = ecalloc(1, sizeof(Fnt));
	font->xfont = xfont;
	font->pattern = pattern;
	font->h = xfont->ascent + xfont->descent;
	#if PATCH_FONT_GROUPS
	font->lrpad = LRPAD(font);
	#endif // PATCH_FONT_GROUPS
	font->dpy = drw->dpy;

	return font;
}

static void
xfont_free(Fnt *font)
{
	if (!font)
		return;
	if (font->pattern)
		FcPatternDestroy(font->pattern);
	XftFontClose(font->dpy, font->xfont);
	free(font);
}

void
drw_ellipse(Drw *drw, int x, int y, unsigned int w, unsigned int h, int filled, int invert)
{
	if (!drw || !drw->scheme)
		return;
	XSetForeground(drw->dpy, drw->gc, invert ? drw->scheme[ColBg].pixel : drw->scheme[ColFg].pixel);
	if (filled)
		XFillArc(drw->dpy, drw->drawable, drw->gc, x, y, w, h, 0, 360*64);
	else
		XDrawArc(drw->dpy, drw->drawable, drw->gc, x, y, w, h, 0, 360*64);
}

#if PATCH_FONT_GROUPS
Fnt *
drw_get_fontgroup_fonts(Drw *drw, char *groupname)
{
	if (drw && drw->fontgroups && groupname)
		for (int i = 0; i < drw->numfontgroups; i++)
			if (strcmp(drw->fontgroups[i]->name, groupname) == 0)
				return drw->fontgroups[i]->fonts;

	return NULL;
}
#endif // PATCH_FONT_GROUPS

#if PATCH_TWO_TONE_TITLE
void
drw_gradient(Drw *drw, int x, int y, unsigned int w, unsigned int h, unsigned long col1, unsigned long col2, int invert)
{
	Picture targetPic;
	Picture gradPic;
	XLinearGradient linearGradient;
	XFixed gstops[3];
	XRenderColor gcolours[3];

	#if PATCH_ALPHA_CHANNEL
	if (drw->useargb)
		targetPic = XRenderCreatePicture(drw->dpy, drw->drawable, XRenderFindVisualFormat(drw->dpy, drw->visual), 0, 0);
	else
	#endif // PATCH_ALPHA_CHANNEL
	targetPic = XRenderCreatePicture(drw->dpy, drw->drawable, XRenderFindVisualFormat(drw->dpy, DefaultVisual(drw->dpy, drw->screen)), 0, 0);

	// coordinates for the start- and end-point of the linear gradient;
	linearGradient.p1.x = XDoubleToFixed (0.0f);
	linearGradient.p1.y = XDoubleToFixed (0.0f);
	linearGradient.p2.x = XDoubleToFixed ((double) w);
	linearGradient.p2.y = XDoubleToFixed (0.0f);

	// offsets for colour stops have in normalized form;
	gstops[0] = XDoubleToFixed (0.0f);
	gstops[1] = XDoubleToFixed (invert ? 0.75f : 0.25f);
	gstops[2] = XDoubleToFixed (1.0f);

	if (invert) {
		unsigned long t = col1;
		col1 = col2;
		col2 = t;
	}

	gcolours[0].alpha = (col1 & 0xFF000000u) >> 16;
	gcolours[0].red = (col1 & 0x00FF0000u) >> 8;
	gcolours[0].green = (col1 & 0x0000FF00u);
	gcolours[0].blue = (col1 & 0x000000FFu) << 8;

	gcolours[2].alpha = (col2 & 0xFF000000u) >> 16;
	gcolours[2].red = (col2 & 0x00FF0000u) >> 8;
	gcolours[2].green = (col2 & 0x0000FF00u);
	gcolours[2].blue = (col2 & 0x000000FFu) << 8;

	if (invert)
		gcolours[1] = gcolours[2];
	else
		gcolours[1] = gcolours[0];

	// create gradient fill;
	gradPic = XRenderCreateLinearGradient(drw->dpy, &linearGradient, gstops, gcolours, 3);

	XRenderComposite(drw->dpy, PictOpSrc, gradPic, None, targetPic, 0, 0, 0, 0, x, y, w, h);

	XRenderFreePicture(drw->dpy, targetPic);
	XRenderFreePicture(drw->dpy, gradPic);

}
#endif // PATCH_TWO_TONE_TITLE


Fnt*
drw_fontset_create(Drw* drw, const char *fonts[], size_t fontcount)
{
	Fnt *cur, *ret = NULL;
	size_t i;

	if (!drw || !fonts)
		return NULL;

	for (i = 1; i <= fontcount; i++) {
		if ((cur = xfont_create(drw, fonts[fontcount - i], NULL))) {
			cur->next = ret;
			ret = cur;
		}
	}
	return ret;
}

Fnt*
drw_fontset_create_json(Drw* drw, cJSON *fonts)
{
	Fnt *cur, *ret = NULL;
	cJSON *f;

	if (!drw || !fonts)
		return NULL;

	if (cJSON_IsArray(fonts)) {
		for (f = fonts->child; f && f->next; f = f->next);
		for (; f; f = (f == fonts->child ? NULL : f->prev)) {
			if ((cur = xfont_create(drw, f->valuestring, NULL))) {
				cur->next = ret;
				ret = cur;
			}
		}
	}
	else if ((cur = xfont_create(drw, fonts->valuestring, NULL))) {
		cur->next = ret;
		ret = cur;
	}

	return ret;
}

void
drw_fontset_free(Fnt *font)
{
	if (font) {
		drw_fontset_free(font->next);
		xfont_free(font);
	}
}

void
drw_clr_create(Drw *drw, Clr *dest, const char *clrname)
{
	if (!drw || !dest || !clrname)
		return;

	#if PATCH_ALPHA_CHANNEL
	// default alpha value;
	int v = 0xffU;
	#endif // PATCH_ALPHA_CHANNEL
	char buffer[32];
	size_t i, len = strlen(clrname);

	if (clrname[0] == '#' && len != 7) {
		if (len == 4) {
			// convert #rgb into #rrggbb;
			for (i = 1; i < 4; i++)
				buffer[2 + (i - 1) * 2] = buffer[1 + (i - 1) * 2] = clrname[i];
		}
		else if (len == 9) {
			// convert #rrggbbaa into #rrggbb and set new alpha value (v);
			#if PATCH_ALPHA_CHANNEL
			buffer[0] = clrname[7];
			buffer[1] = clrname[8];
			buffer[2] = '\0';
			v = strtol(buffer, NULL, 16);
			#endif // PATCH_ALPHA_CHANNEL
			for (i = 1; i < 7; i++)
				buffer[i] = clrname[i];
		}
		else {
			// copy whatever we have into the buffer, prepare for failure;
			for (i = 0; i < (len > 7 ? 7 : len); i++)
				buffer[i] = clrname[i];
			buffer[i] = '\0';
		}
		buffer[0] = '#';
		buffer[7] = '\0';
	}
	else {
		// copy the colour string into the buffer;
		for (i = 0; i < len; i++)
			buffer[i] = clrname[i];
		buffer[i] = '\0';
	}

	#if PATCH_ALPHA_CHANNEL
	if (drw->useargb) {
		if (!XftColorAllocName(drw->dpy, drw->visual, drw->cmap, buffer, dest));
			//die("error, cannot allocate color '%s'", clrname);
	}
	else
	#endif // PATCH_ALPHA_CHANNEL
	if (!XftColorAllocName(drw->dpy, DefaultVisual(drw->dpy, drw->screen),
	                       DefaultColormap(drw->dpy, drw->screen),
	                       buffer, dest));
		//die("error, cannot allocate color '%s'", clrname);

	#if PATCH_ALPHA_CHANNEL
	// set the alpha channel;
	dest->pixel = (dest->pixel & 0x00ffffffU) | ((v & 0xffU) << 24);
	#endif // PATCH_ALPHA_CHANNEL
}

/* Wrapper to create color schemes. The caller has to call free(3) on the
 * returned color scheme when done using it. */
Clr *
drw_scm_create(Drw *drw, char *clrnames[], size_t clrcount)
{
	size_t i;
	Clr *ret;

	/* need at least two colors for a scheme */
	if (!drw || !clrnames || clrcount < 2 || !(ret = ecalloc(clrcount, sizeof(XftColor))))
		return NULL;

	for (i = 0; i < clrcount; i++)
		drw_clr_create(drw, &ret[i], clrnames[i]);
	return ret;
}

#if PATCH_FONT_GROUPS
int
drw_select_fontgroup(Drw *drw, char *groupname)
{
	if (drw && drw->fontgroups && groupname)
		drw->selfonts = drw_get_fontgroup_fonts(drw, groupname);
	else
		drw->selfonts = NULL;

	return (drw->selfonts ? 1 : 0);
}
#endif // PATCH_FONT_GROUPS

void
drw_setfontset(Drw *drw, Fnt *set)
{
	if (drw)
		drw->fonts = set;
}

void
drw_setscheme(Drw *drw, Clr *scm)
{
	if (drw)
		drw->scheme = scm;
}

#if (PATCH_WINDOW_ICONS && (PATCH_WINDOW_ICONS_CUSTOM_ICONS || PATCH_WINDOW_ICONS_DEFAULT_ICON)) || PATCH_CUSTOM_TAG_ICONS
Picture
drw_picture_create_resized_from_file(Drw *drw, char *src_file, unsigned int *picw, unsigned int *pich, unsigned int iconsize) {
	Pixmap pm;
	Picture pic;
	GC gc;

	unsigned int dstw, dsth;

	Imlib_Image origin = imlib_load_image_immediately(src_file);
	if (!origin) return None;
	imlib_context_set_image(origin);
	unsigned int srcw = imlib_image_get_width();
	unsigned int srch = imlib_image_get_height();
	if (srcw <= srch) {
		dsth = iconsize; dstw = srcw * iconsize / srch;
		if (dstw == 0) dstw = 1;
	}
	else {
		dstw = iconsize; dsth = srch * iconsize / srcw;
		if (dsth == 0) dsth = 1;
	}
	*picw = dstw; *pich = dsth;
	imlib_image_set_has_alpha(1);
	Imlib_Image scaled = imlib_create_cropped_scaled_image(0, 0, srcw, srch, dstw, dsth);
	imlib_free_image_and_decache();
	if (!scaled) return None;
	imlib_context_set_image(scaled);
	imlib_image_set_has_alpha(1);

	XImage img = {
		dstw, dsth, 0, ZPixmap, (char *)imlib_image_get_data_for_reading_only(),
		ImageByteOrder(drw->dpy), BitmapUnit(drw->dpy), BitmapBitOrder(drw->dpy), 32,
		32, 0, 32,
		0, 0, 0
	};
	XInitImage(&img);

	pm = XCreatePixmap(drw->dpy, drw->root, dstw, dsth, 32);
	gc = XCreateGC(drw->dpy, pm, 0, NULL);
	XPutImage(drw->dpy, pm, gc, &img, 0, 0, 0, 0, dstw, dsth);
	imlib_free_image_and_decache();
	XFreeGC(drw->dpy, gc);

	pic = XRenderCreatePicture(drw->dpy, pm, XRenderFindStandardFormat(drw->dpy, PictStandardARGB32), 0, NULL);
	XFreePixmap(drw->dpy, pm);

	return pic;
}
#endif // (PATCH_WINDOW_ICONS && (PATCH_WINDOW_ICONS_CUSTOM_ICONS || PATCH_WINDOW_ICONS_DEFAULT_ICON)) || PATCH_CUSTOM_TAG_ICONS
#if PATCH_WINDOW_ICONS
Picture
drw_picture_create_resized(Drw *drw, char *src, unsigned int srcw, unsigned int srch, unsigned int dstw, unsigned int dsth) {
	Pixmap pm;
	Picture pic;
	GC gc;

	if (srcw <= (dstw << 1u) && srch <= (dsth << 1u)) {
		XImage img = {
			srcw, srch, 0, ZPixmap, src,
			ImageByteOrder(drw->dpy), BitmapUnit(drw->dpy), BitmapBitOrder(drw->dpy), 32,
			32, 0, 32,
			0, 0, 0
		};
		XInitImage(&img);

		pm = XCreatePixmap(drw->dpy, drw->root, srcw, srch, 32);
		gc = XCreateGC(drw->dpy, pm, 0, NULL);
		XPutImage(drw->dpy, pm, gc, &img, 0, 0, 0, 0, srcw, srch);
		XFreeGC(drw->dpy, gc);

		pic = XRenderCreatePicture(drw->dpy, pm, XRenderFindStandardFormat(drw->dpy, PictStandardARGB32), 0, NULL);
		XFreePixmap(drw->dpy, pm);

		XRenderSetPictureFilter(drw->dpy, pic, FilterBilinear, NULL, 0);
		XTransform xf;
		xf.matrix[0][0] = (srcw << 16u) / dstw; xf.matrix[0][1] = 0; xf.matrix[0][2] = 0;
		xf.matrix[1][0] = 0; xf.matrix[1][1] = (srch << 16u) / dsth; xf.matrix[1][2] = 0;
		xf.matrix[2][0] = 0; xf.matrix[2][1] = 0; xf.matrix[2][2] = 65536;
		XRenderSetPictureTransform(drw->dpy, pic, &xf);
	} else {
		Imlib_Image origin = imlib_create_image_using_data(srcw, srch, (DATA32 *)src);
		if (!origin) return None;
		imlib_context_set_image(origin);
		imlib_image_set_has_alpha(1);
		Imlib_Image scaled = imlib_create_cropped_scaled_image(0, 0, srcw, srch, dstw, dsth);
		imlib_free_image_and_decache();
		if (!scaled) return None;
		imlib_context_set_image(scaled);
		imlib_image_set_has_alpha(1);

		XImage img = {
		    dstw, dsth, 0, ZPixmap, (char *)imlib_image_get_data_for_reading_only(),
		    ImageByteOrder(drw->dpy), BitmapUnit(drw->dpy), BitmapBitOrder(drw->dpy), 32,
		    32, 0, 32,
		    0, 0, 0
		};
		XInitImage(&img);

		pm = XCreatePixmap(drw->dpy, drw->root, dstw, dsth, 32);
		gc = XCreateGC(drw->dpy, pm, 0, NULL);
		XPutImage(drw->dpy, pm, gc, &img, 0, 0, 0, 0, dstw, dsth);
		imlib_free_image_and_decache();
		XFreeGC(drw->dpy, gc);

		pic = XRenderCreatePicture(drw->dpy, pm, XRenderFindStandardFormat(drw->dpy, PictStandardARGB32), 0, NULL);
		XFreePixmap(drw->dpy, pm);
	}

	return pic;
}
#endif // PATCH_WINDOW_ICONS

void
drw_rect(Drw *drw, int x, int y, unsigned int w, unsigned int h, int filled, int invert)
{
	if (!drw || !drw->scheme)
		return;
	XSetForeground(drw->dpy, drw->gc, invert ? drw->scheme[ColBg].pixel : drw->scheme[ColFg].pixel);
	if (filled)
		XFillRectangle(drw->dpy, drw->drawable, drw->gc, x, y, w, h);
	else
		XDrawRectangle(drw->dpy, drw->drawable, drw->gc, x, y, w - 1, h - 1);
}

int
#if PATCH_CLIENT_INDICATORS
drw_text(Drw *drw, int x, int y, unsigned int w, unsigned int h, unsigned int lpad, unsigned int rpad, int tpad, int ellipsis_align, const char *text, int invert)
#else // NO PATCH_CLIENT_INDICATORS
drw_text(Drw *drw, int x, int y, unsigned int w, unsigned int h, unsigned int lpad, unsigned int rpad, int ellipsis_align, const char *text, int invert)
#endif // PATCH_CLIENT_INDICATORS
{
	int i, ty, ellipsis_x = 0;
	unsigned int tmpw, ew = strlen(text), ellipsis_w = 0, ellipsis_len;
	XftDraw *d = NULL;
	Fnt *usedfont, *curfont, *nextfont, *fonts;
	int utf8strlen, utf8charlen, render = x || y || w || h;
	long utf8codepoint = 0;
	const char *utf8str;
	FcCharSet *fccharset;
	FcPattern *fcpattern;
	FcPattern *match;
	XftResult result;
	int charexists = 0, overflow = 0;
	/* keep track of a couple codepoints for which we have no match. */
	enum { nomatches_len = 64 };
	static struct { long codepoint[nomatches_len]; unsigned int idx; } nomatches;
	#if PATCH_FONT_GROUPS
	#define ELLIPSIS_WIDTH	fonts->ellipsis_width
	#else // NO PATCH_FONT_GROUPS
	static unsigned int ellipsis_width = 0;
	#define ELLIPSIS_WIDTH	ellipsis_width
	#endif // PATCH_FONT_GROUPS

	if (!drw || (render && (!drw->scheme || !w)) || !text || !(fonts = drw->fonts))
		return 0;

	#if PATCH_FONT_GROUPS
	if (drw->selfonts)
		fonts = drw->selfonts;
	#endif // PATCH_FONT_GROUPS

	if (!render) {
		w = invert ? invert : ~invert;
		ellipsis_align = 0;
	} else {
		if (ellipsis_align != -1
			#if PATCH_TWO_TONE_TITLE
			&& !drw->bg2
			#endif // PATCH_TWO_TONE_TITLE
		) {
			XSetForeground(drw->dpy, drw->gc, drw->scheme[invert ? ColFg : ColBg].pixel);
			XFillRectangle(drw->dpy, drw->drawable, drw->gc, x, y, w, h);
		}
		#if PATCH_ALPHA_CHANNEL
		if (drw->useargb)
			d = XftDrawCreate(drw->dpy, drw->drawable, drw->visual, drw->cmap);
		else
		#endif // PATCH_ALPHA_CHANNEL
		d = XftDrawCreate(drw->dpy, drw->drawable,
		                  DefaultVisual(drw->dpy, drw->screen),
		                  DefaultColormap(drw->dpy, drw->screen));
		x += lpad;
		w -= (lpad + rpad);

		if (!ELLIPSIS_WIDTH)
			ELLIPSIS_WIDTH = drw_fontset_getwidth(drw, ellipsis);

		if (ellipsis_align > 0) {
			tmpw = drw_text(
				drw, 0, 0, 0, 0, 0, 0,
				#if PATCH_CLIENT_INDICATORS
				0,
				#endif // PATCH_CLIENT_INDICATORS
				0, text, 0
			);
			if (tmpw <= w)
				ellipsis_align = 0;
			else {
				if (ellipsis_align == 2) {

					w -= ELLIPSIS_WIDTH;
					for (i = ew - 1; i >= 0; i--)
						if (drw_text(
								drw, 0, 0, 0, 0, 0, 0,
								#if PATCH_CLIENT_INDICATORS
								0,
								#endif // PATCH_CLIENT_INDICATORS
								0, text + i, 0
							) > w
						) {
							if (++i <= ew - 1) {

								tmpw = drw_text(
									drw, 0, 0, 0, 0, 0, 0,
									#if PATCH_CLIENT_INDICATORS
									0,
									#endif // PATCH_CLIENT_INDICATORS
									0, text + i, 0
								);
								x += (w - tmpw);
								w = tmpw;

								drw_text(
									drw, x, y, ELLIPSIS_WIDTH, h, 0, 0,
									#if PATCH_CLIENT_INDICATORS
									tpad,
									#endif // PATCH_CLIENT_INDICATORS
									-1, ellipsis, invert
								);
								x += ELLIPSIS_WIDTH;

								drw_text(
									drw, x, y, w, h, 0, 0,
									#if PATCH_CLIENT_INDICATORS
									tpad,
									#endif // PATCH_CLIENT_INDICATORS
									-1, text + i, invert
								);
							}
							break;
						}

					return x + w;

				}
				else if (ellipsis_align == 1) {

					char buffer[ew + 1];
					strncpy(buffer, text, sizeof buffer);

					for (i = 1; i < ew; i++) {
						buffer[i - 1] = text[i - 1];
						buffer[i] = '\0';
						if ((tmpw = drw_text(
								drw, 0, 0, 0, 0, 0, 0,
								#if PATCH_CLIENT_INDICATORS
								0,
								#endif // PATCH_CLIENT_INDICATORS
								0, buffer, 0
							)) > (w / 2 - ELLIPSIS_WIDTH)
						) {
							buffer[--i] = '\0';
							tmpw = drw_text(
								drw, 0, 0, 0, 0, 0, 0,
								#if PATCH_CLIENT_INDICATORS
								0,
								#endif // PATCH_CLIENT_INDICATORS
								-1, buffer, 0
							);
							drw_text(
								drw, x, y, w, h, 0, 0,
								#if PATCH_CLIENT_INDICATORS
								tpad,
								#endif // PATCH_CLIENT_INDICATORS
								-1, buffer, invert
							);
							drw_text(
								drw, x + tmpw, y, w - tmpw, h, 0, 0,
								#if PATCH_CLIENT_INDICATORS
								tpad,
								#endif // PATCH_CLIENT_INDICATORS
								-1, ellipsis, invert
							);
							tmpw += ELLIPSIS_WIDTH;

							for (i = ew - 1; i >= 0; i--)
								if (tmpw + (drw_text(
										drw, 0, 0, 0, 0, 0, 0,
										#if PATCH_CLIENT_INDICATORS
										0,
										#endif // PATCH_CLIENT_INDICATORS
										0, text + i, 0
									)) > w
								) {
									if (++i <= ew - 1)
										drw_text(
											drw, x + tmpw, y, w - tmpw, h, 0, 0,
											#if PATCH_CLIENT_INDICATORS
											tpad,
											#endif // PATCH_CLIENT_INDICATORS
											-1, text + i, invert
										);
									break;
								}

							return x + w;
						}
					}

				}
				ellipsis_align = 0;

			}
		}
	}

	usedfont = fonts;
	while (1) {
		ew = ellipsis_len = utf8strlen = 0;
		utf8str = text;
		nextfont = NULL;
		while (*text) {
			utf8charlen = utf8decode(text, &utf8codepoint, UTF_SIZ);
			for (curfont = fonts; curfont; curfont = curfont->next) {
				charexists = charexists || XftCharExists(drw->dpy, curfont->xfont, utf8codepoint);
				if (charexists) {
					drw_font_getexts(curfont, text, utf8charlen, &tmpw, NULL);
					if (ew + ELLIPSIS_WIDTH <= w) {
						/* keep track where the ellipsis still fits */
						ellipsis_x = x + ew;
						ellipsis_w = w - ew;
						ellipsis_len = utf8strlen;
					}

					if (ew + tmpw > w) {
						overflow = 1;
						/* called from drw_fontset_getwidth_clamp():
						 * it wants the width AFTER the overflow
						 */
						if (!render)
							x += tmpw;
						else
							utf8strlen = ellipsis_len;
					} else if (curfont == usedfont) {
						utf8strlen += utf8charlen;
						text += utf8charlen;
						ew += tmpw;
					} else {
						nextfont = curfont;
					}
					break;
				}
			}

			if (overflow || !charexists || nextfont)
				break;
			else
				charexists = 0;
		}

		if (utf8strlen) {
			if (render) {
				ty = y + (h - usedfont->h) / 2 + usedfont->xfont->ascent
				#if PATCH_CLIENT_INDICATORS
				+ tpad
				#endif // PATCH_CLIENT_INDICATORS
				;
				XftDrawStringUtf8(d, &drw->scheme[invert ? ColBg : ColFg],
				                  usedfont->xfont, x, ty, (XftChar8 *)utf8str, utf8strlen);
			}
			x += ew;
			w -= ew;
		}
		if (render && overflow)
			drw_text(
				drw, ellipsis_x, y, ellipsis_w, h, 0, 0,
				#if PATCH_CLIENT_INDICATORS
				tpad,
				#endif // PATCH_CLIENT_INDICATORS
				ellipsis_align, ellipsis, invert
			);

		if (!*text || overflow) {
			break;
		} else if (nextfont) {
			charexists = 0;
			usedfont = nextfont;
		} else {
			/* Regardless of whether or not a fallback font is found, the
			 * character must be drawn. */
			charexists = 1;

			for (i = 0; i < nomatches_len; ++i) {
				/* avoid calling XftFontMatch if we know we won't find a match */
				if (utf8codepoint == nomatches.codepoint[i])
					goto no_match;
			}

			fccharset = FcCharSetCreate();
			FcCharSetAddChar(fccharset, utf8codepoint);

			if (!fonts->pattern) {
				/* Refer to the comment in xfont_create for more information. */
				die("the first font in the cache must be loaded from a font string.");
			}

			fcpattern = FcPatternDuplicate(fonts->pattern);
			FcPatternAddCharSet(fcpattern, FC_CHARSET, fccharset);
			FcPatternAddBool(fcpattern, FC_SCALABLE, FcTrue);
			#if PATCH_XFTLIB_EMOJI_WORKAROUND
			FcPatternAddBool(fcpattern, FC_COLOR, FcFalse);
			#endif

			FcConfigSubstitute(NULL, fcpattern, FcMatchPattern);
			FcDefaultSubstitute(fcpattern);
			match = XftFontMatch(drw->dpy, drw->screen, fcpattern, &result);

			FcCharSetDestroy(fccharset);
			FcPatternDestroy(fcpattern);

			if (match) {
				usedfont = xfont_create(drw, NULL, match);
				if (usedfont && XftCharExists(drw->dpy, usedfont->xfont, utf8codepoint)) {
					for (curfont = fonts; curfont->next; curfont = curfont->next)
						; /* NOP */
					curfont->next = usedfont;
				} else {
					xfont_free(usedfont);
					nomatches.codepoint[++nomatches.idx % nomatches_len] = utf8codepoint;
no_match:
					usedfont = fonts;
				}
			}
		}
	}
	if (d)
		XftDrawDestroy(d);

	return x + (render ? w : 0);
}

#if PATCH_WINDOW_ICONS || PATCH_CUSTOM_TAG_ICONS
void
drw_pic(Drw *drw, int x, int y, unsigned int w, unsigned int h, Picture pic)
{
	if (!drw)
		return;
	XRenderComposite(drw->dpy, PictOpOver, pic, None, drw->picture, 0, 0, 0, 0, x, y, w, h);
}
#endif // PATCH_WINDOW_ICONS || PATCH_CUSTOM_TAG_ICONS

void
drw_map(Drw *drw, Window win, int x, int y, unsigned int w, unsigned int h)
{
	if (!drw)
		return;

	XCopyArea(drw->dpy, drw->drawable, win, drw->gc, x, y, w, h, x, y);
	XSync(drw->dpy, False);
}

void
drw_maptrans(Drw *drw, Window win, int srcx, int srcy, unsigned int w, unsigned int h, int destx, int desty)
{
	if (!drw)
		return;

	XCopyArea(drw->dpy, drw->drawable, win, drw->gc, srcx, srcy, w, h, destx, desty);
	XSync(drw->dpy, False);
}

unsigned int
drw_fontset_getwidth(Drw *drw, const char *text)
{
	if (!drw || !drw->fonts || !text)
		return 0;
	return drw_text(
		drw, 0, 0, 0, 0, 0, 0,
		#if PATCH_CLIENT_INDICATORS
		0,
		#endif // PATCH_CLIENT_INDICATORS
		0, text, 0
	);
}

unsigned int
drw_fontset_getwidth_clamp(Drw *drw, const char *text, unsigned int n)
{
	unsigned int tmp = 0;
	if (drw && drw->fonts && text && n)
		tmp = drw_text(
			drw, 0, 0, 0, 0, 0, 0,
			#if PATCH_CLIENT_INDICATORS
			0,
			#endif // PATCH_CLIENT_INDICATORS
			0, text, n
		);
	return MIN(n, tmp);
}

void
drw_font_getexts(Fnt *font, const char *text, unsigned int len, unsigned int *w, unsigned int *h)
{
	XGlyphInfo ext;

	if (!font || !text)
		return;

	XftTextExtentsUtf8(font->dpy, font->xfont, (XftChar8 *)text, len, &ext);
	if (w)
		*w = ext.xOff;
	if (h)
		*h = font->h;
}

Cur *
drw_cur_create(Drw *drw, int shape)
{
	Cur *cur;

	if (!drw || !(cur = ecalloc(1, sizeof(Cur))))
		return NULL;

	#if PATCH_SLEEP || PATCH_TORCH
	if (shape < 0) {
		char curs[] = {0, 0, 0, 0, 0, 0, 0, 0};
		XColor colour;
		Pixmap pmap = XCreateBitmapFromData(drw->dpy, drw->root, curs, 8, 8);
		cur->cursor = XCreatePixmapCursor(drw->dpy, pmap, pmap, &colour, &colour, 0, 0);
	}
	else
	#endif // PATCH_SLEEP || PATCH_TORCH
	cur->cursor = XCreateFontCursor(drw->dpy, shape);

	return cur;
}

void
drw_cur_free(Drw *drw, Cur *cursor)
{
	if (!cursor)
		return;

	XFreeCursor(drw->dpy, cursor->cursor);
	free(cursor);
}
