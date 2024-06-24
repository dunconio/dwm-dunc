
void
setgaps(int oh, int ov, int ih, int iv)
{
	if (oh < 0) oh = 0;
	if (ov < 0) ov = 0;
	if (ih < 0) ih = 0;
	if (iv < 0) iv = 0;

	selmon->gappoh = oh;
	selmon->gappov = ov;
	selmon->gappih = ih;
	selmon->gappiv = iv;
	arrange(selmon);
}

void
togglegaps(const Arg *arg)
{
	#if PATCH_PERTAG
	selmon->pertag->enablegaps[selmon->pertag->curtag] = !selmon->pertag->enablegaps[selmon->pertag->curtag];
	#else
	selmon->enablegaps = !selmon->enablegaps;
	#endif // PATCH_PERTAG
	arrange(NULL);
}

void
defaultgaps(const Arg *arg)
{
	setgaps(gappoh, gappov, gappih, gappiv);
}

void
incrgaps(const Arg *arg)
{
	setgaps(
		selmon->gappoh + arg->i,
		selmon->gappov + arg->i,
		selmon->gappih + arg->i,
		selmon->gappiv + arg->i
	);
}

void
incrigaps(const Arg *arg)
{
	setgaps(
		selmon->gappoh,
		selmon->gappov,
		selmon->gappih + arg->i,
		selmon->gappiv + arg->i
	);
}

void
incrogaps(const Arg *arg)
{
	setgaps(
		selmon->gappoh + arg->i,
		selmon->gappov + arg->i,
		selmon->gappih,
		selmon->gappiv
	);
}

void
incrohgaps(const Arg *arg)
{
	setgaps(
		selmon->gappoh + arg->i,
		selmon->gappov,
		selmon->gappih,
		selmon->gappiv
	);
}

void
incrovgaps(const Arg *arg)
{
	setgaps(
		selmon->gappoh,
		selmon->gappov + arg->i,
		selmon->gappih,
		selmon->gappiv
	);
}

void
incrihgaps(const Arg *arg)
{
	setgaps(
		selmon->gappoh,
		selmon->gappov,
		selmon->gappih + arg->i,
		selmon->gappiv
	);
}

void
incrivgaps(const Arg *arg)
{
	setgaps(
		selmon->gappoh,
		selmon->gappov,
		selmon->gappih,
		selmon->gappiv + arg->i
	);
}

void
getgaps(Monitor *m, int *oh, int *ov, int *ih, int *iv, unsigned int *nc)
{
	unsigned int n, oe, ie;
	#if PATCH_PERTAG
	oe = ie = m->pertag->enablegaps[m->pertag->curtag];
	#else // NO PATCH_PERTAG
	oe = ie = m->enablegaps;
	#endif // PATCH_PERTAG
	Client *c;

	for (n = 0, c = nexttiled(m->clients); c; c = nexttiled(c->next), n++);
	if (smartgaps && n == 1) {
		oe = 0; // outer gaps disabled when only one client
	}

	*oh = m->gappoh*oe; // outer horizontal gap
	*ov = m->gappov*oe; // outer vertical gap
	*ih = m->gappih*ie; // inner horizontal gap
	*iv = m->gappiv*ie; // inner vertical gap
	*nc = n;            // number of clients
}

void
getfacts(Monitor *m, int msize, int ssize, float *mf, float *sf, int *mr, int *sr)
{
	unsigned int n;
	float mfacts = 0, sfacts = 0;
	int mtotal = 0, stotal = 0;
	Client *c;

	for (n = 0, c = nexttiled(m->clients); c; c = nexttiled(c->next), n++)
		if (n < m->nmaster)
			mfacts += c->cfact;
		else
			sfacts += c->cfact;

	for (n = 0, c = nexttiled(m->clients); c; c = nexttiled(c->next), n++)
		if (n < m->nmaster)
			mtotal += msize * (c->cfact / mfacts);
		else
			stotal += ssize * (c->cfact / sfacts);

	*mf = mfacts; // total factor of master area
	*sf = sfacts; // total factor of stack area
	*mr = msize - mtotal; // the remainder (rest) of pixels after a cfacts master split
	*sr = ssize - stotal; // the remainder (rest) of pixels after a cfacts stack split
}

/***
 * Layouts
 */

/*
 * Bottomstack layout + gaps
 * https://dwm.suckless.org/patches/bottomstack/
 */
static void
bstack(Monitor *m)
{
	unsigned int i, n;
	int oh, ov, ih, iv;
	int mx = 0, my = 0, mh = 0, mw = 0;
	int sx = 0, sy = 0, sh = 0, sw = 0;
	float mfacts, sfacts;
	int mrest, srest;
	Client *c;

	getgaps(m, &oh, &ov, &ih, &iv, &n);
	if (n == 0)
		return;

	sx = mx = m->wx + oh;
	sy = my = m->wy + ov;
	sh = mh = m->wh - 2*ov;
	mw = m->ww - 2*oh - ih * (MIN(n, m->nmaster) - 1);
	sw = m->ww - 2*oh - ih * (n - m->nmaster - 1);

	if (m->nmaster && n > m->nmaster) {
		sh = (mh - iv) * (1 - m->mfact);
		mh = mh - iv - sh;
		sx = mx;
		sy = my + mh + iv;
	}

	getfacts(m, mw, sw, &mfacts, &sfacts, &mrest, &srest);

	for (i = 0, c = nexttiled(m->clients); c; c = nexttiled(c->next), i++) {
		if (i < m->nmaster) {
			resize(
				c, mx,
				#if PATCH_MIRROR_LAYOUT
				m->mirror ? (m->wy + m->wh - (my - m->wy) - (mh - (2*c->bw))) :
				#endif // PATCH_MIRROR_LAYOUT
				my,
				mw * (c->cfact / mfacts) + (i < mrest ? 1 : 0) - (2*c->bw), mh - (2*c->bw), 0
			);
			mx += WIDTH(c) + ih;
		} else {
			resize(
				c, sx,
				#if PATCH_MIRROR_LAYOUT
				m->mirror ? (m->wy + m->wh - (sy - m->wy) - (sh - (2*c->bw))) :
				#endif // PATCH_MIRROR_LAYOUT
				sy,
				sw * (c->cfact / sfacts) + ((i - m->nmaster) < srest ? 1 : 0) - (2*c->bw), sh - (2*c->bw), 0
			);
			sx += WIDTH(c) + ih;
		}
	}
}

static void
bstackhoriz(Monitor *m)
{
	unsigned int i, n;
	int oh, ov, ih, iv;
	int mx = 0, my = 0, mh = 0, mw = 0;
	int sx = 0, sy = 0, sh = 0, sw = 0;
	float mfacts, sfacts;
	int mrest, srest;
	Client *c;

	getgaps(m, &oh, &ov, &ih, &iv, &n);
	if (n == 0)
		return;

	sx = mx = m->wx + oh;
	sy = my = m->wy + ov;
	mh = m->wh - 2*ov;
	sh = m->wh - 2*ov - iv * (n - m->nmaster - 1);
	mw = m->ww - 2*oh - ih * (MIN(n, m->nmaster) - 1);
	sw = m->ww - 2*oh;

	if (m->nmaster && n > m->nmaster) {
		sh = (mh - iv) * (1 - m->mfact);
		mh = mh - iv - sh;
		sy = my + mh + iv;
		sh = m->wh - mh - 2*ov - iv * (n - m->nmaster);
	}

	getfacts(m, mw, sh, &mfacts, &sfacts, &mrest, &srest);

	for (i = 0, c = nexttiled(m->clients); c; c = nexttiled(c->next), i++) {
		if (i < m->nmaster) {
			resize(
				c, mx,
				#if PATCH_MIRROR_LAYOUT
				m->mirror ? (m->wy + m->wh - (my - m->wy) - (mh - (2*c->bw))) :
				#endif // PATCH_MIRROR_LAYOUT
				my,
				mw * (c->cfact / mfacts) + (i < mrest ? 1 : 0) - (2*c->bw), mh - (2*c->bw), 0
			);
			mx += WIDTH(c) + ih;
		} else {
			resize(
				c, sx,
				#if PATCH_MIRROR_LAYOUT
				m->mirror ? (m->wy + m->wh - (sy - m->wy) - (sh * (c->cfact / sfacts) + ((i - m->nmaster) < srest ? 1 : 0) - (2*c->bw))) :
				#endif // PATCH_MIRROR_LAYOUT
				sy,
				sw - (2*c->bw), sh * (c->cfact / sfacts) + ((i - m->nmaster) < srest ? 1 : 0) - (2*c->bw), 0
			);
			sy += HEIGHT(c) + iv;
		}
	}
}

/*
 * Centred master layout + gaps
 * https://dwm.suckless.org/patches/centeredmaster/
 */
void
centredmaster(Monitor *m)
{
	unsigned int i, n;
	int oh, ov, ih, iv;
	int mx = 0, my = 0, mh = 0, mw = 0;
	int lx = 0, ly = 0, lw = 0, lh = 0;
	int rx = 0, ry = 0, rw = 0, rh = 0;
	float mfacts = 0, lfacts = 0, rfacts = 0;
	int mtotal = 0, ltotal = 0, rtotal = 0;
	int mrest = 0, lrest = 0, rrest = 0;
	Client *c;

	getgaps(m, &oh, &ov, &ih, &iv, &n);
	if (n == 0)
		return;

	/* initialize areas */
	mx = m->wx + oh;
	my = m->wy + ov;
	mh = m->wh - 2*ov - iv * ((!m->nmaster ? n : MIN(n, m->nmaster)) - 1);
	mw = m->ww - 2*oh;
	lh = m->wh - 2*ov - iv * (((n - m->nmaster) / 2) - 1);
	rh = m->wh - 2*ov - iv * (((n - m->nmaster) / 2) - ((n - m->nmaster) % 2 ? 0 : 1));

	if (m->nmaster && n > m->nmaster) {
		/* go mfact box in the centre if more than nmaster clients */
		if (n - m->nmaster > 1) {
			/* ||<-S->|<---M--->|<-S->|| */
			mw = (m->ww - 2*oh - 2*ih) * m->mfact;
			lw = (m->ww - mw - 2*oh - 2*ih) / 2;
			rw = (m->ww - mw - 2*oh - 2*ih) - lw;
			mx += lw + ih;
		} else {
			/* ||<---M--->|<-S->|| */
			mw = (mw - ih) * m->mfact;
			lw = 0;
			rw = m->ww - mw - ih - 2*oh;
		}
		lx = m->wx + oh;
		ly = m->wy + ov;
		rx = mx + mw + ih;
		ry = m->wy + ov;
	}

	/* calculate facts */
	for (n = 0, c = nexttiled(m->clients); c; c = nexttiled(c->next), n++) {
		if (!m->nmaster || n < m->nmaster)
			mfacts += c->cfact;
		else if ((n - m->nmaster) % 2)
			lfacts += c->cfact; // total factor of left hand stack area
		else
			rfacts += c->cfact; // total factor of right hand stack area
	}

	for (n = 0, c = nexttiled(m->clients); c; c = nexttiled(c->next), n++)
		if (!m->nmaster || n < m->nmaster)
			mtotal += mh * (c->cfact / mfacts);
		else if ((n - m->nmaster) % 2)
			ltotal += lh * (c->cfact / lfacts);
		else
			rtotal += rh * (c->cfact / rfacts);

	mrest = mh - mtotal;
	lrest = lh - ltotal;
	rrest = rh - rtotal;

	for (i = 0, c = nexttiled(m->clients); c; c = nexttiled(c->next), i++) {
		if (!m->nmaster || i < m->nmaster) {
			/* nmaster clients are stacked vertically, in the centre of the screen */
			resize(c, mx, my, mw - (2*c->bw), mh * (c->cfact / mfacts) + (i < mrest ? 1 : 0) - (2*c->bw), 0);
			my += HEIGHT(c) + iv;
		} else {
			/* stack clients are stacked vertically */
			if ((i - m->nmaster) % 2 ) {
				resize(c, lx, ly, lw - (2*c->bw), lh * (c->cfact / lfacts) + ((i - 2*m->nmaster) < 2*lrest ? 1 : 0) - (2*c->bw), 0);
				ly += HEIGHT(c) + iv;
			} else {
				resize(c, rx, ry, rw - (2*c->bw), rh * (c->cfact / rfacts) + ((i - 2*m->nmaster) < 2*rrest ? 1 : 0) - (2*c->bw), 0);
				ry += HEIGHT(c) + iv;
			}
		}
	}
}

void
centredfloatingmaster(Monitor *m)
{
	unsigned int i, n;
	float mfacts, sfacts;
	float mihf = 1.0; // master inner horizontal gap factor
	int oh, ov, ih, iv, mrest, srest;
	int mx = 0, my = 0, mh = 0, mw = 0;
	int sx = 0, sy = 0, sh = 0, sw = 0;
	Client *c;

	getgaps(m, &oh, &ov, &ih, &iv, &n);
	if (n == 0)
		return;

	sx = mx = m->wx + oh;
	sy = my = m->wy + ov;
	sh = mh = m->wh - 2*ov;
	mw = m->ww - 2*oh - ih*(n - 1);
	sw = m->ww - 2*oh - ih*(n - m->nmaster - 1);

	if (m->nmaster && n > m->nmaster) {
		mihf = 0.8;
		/* go mfact box in the centre if more than nmaster clients */
		if (m->ww > m->wh) {
			mw = m->ww * m->mfact - ih*mihf*(MIN(n, m->nmaster) - 1);
			mh = m->wh * 0.9;
		} else {
			mw = m->ww * 0.9 - ih*mihf*(MIN(n, m->nmaster) - 1);
			mh = m->wh * m->mfact;
		}
		mx = m->wx + (m->ww - mw) / 2;
		my = m->wy + (m->wh - mh) / 2;

		sx = m->wx + oh;
		sy = m->wy + ov;
		sh = m->wh - 2*ov;
	}

	getfacts(m, mw, sw, &mfacts, &sfacts, &mrest, &srest);

	for (i = 0, c = nexttiled(m->clients); c; c = nexttiled(c->next), i++)
		if (i < m->nmaster) {
			/* nmaster clients are stacked horizontally, in the centre of the screen */
			resize(c, mx, my, mw * (c->cfact / mfacts) + (i < mrest ? 1 : 0) - (2*c->bw), mh - (2*c->bw), 0);
			mx += WIDTH(c) + ih*mihf;
		} else {
			/* stack clients are stacked horizontally */
			resize(c, sx, sy, sw * (c->cfact / sfacts) + ((i - m->nmaster) < srest ? 1 : 0) - (2*c->bw), sh - (2*c->bw), 0);
			sx += WIDTH(c) + ih;
		}
}


/*
 * Deck layout + gaps
 * https://dwm.suckless.org/patches/deck/
 */
void
deck(Monitor *m)
{
	unsigned int i, n;
	int oh, ov, ih, iv;
	int mx = 0, my = 0, mh = 0, mw = 0;
	int sx = 0, sy = 0, sh = 0, sw = 0;
	float mfacts, sfacts;
	int mrest, srest;
	Client *c;

	getgaps(m, &oh, &ov, &ih, &iv, &n);
	if (n == 0)
		return;

	sx = mx = m->wx + oh;
	sy = my = m->wy + ov;
	sh = mh = m->wh - 2*ov - iv * (MIN(n, m->nmaster) - 1);
	sw = mw = m->ww - 2*oh;

	if (m->nmaster && n > m->nmaster) {
		sw = (mw - ih) * (1 - m->mfact);
		mw = mw - ih - sw;
		sh = m->wh - 2*ov;
		#if PATCH_MIRROR_LAYOUT
		if (m->mirror) {
			mx += sw + ih;
			sx = m->wx + oh;
		}
		else
		#endif // PATCH_MIRROR_LAYOUT
		sx = mx + mw + ih;
	}

	getfacts(m, mh, sh, &mfacts, &sfacts, &mrest, &srest);

	if (n - m->nmaster > 0) /* override layout symbol */
		snprintf(m->ltsymbol, sizeof m->ltsymbol, "D [%d]", n - m->nmaster);

	for (i = 0, c = nexttiled(m->clients); c; c = nexttiled(c->next), i++)
		if (i < m->nmaster) {
			resize(c, mx, my, mw - (2*c->bw), mh * (c->cfact / mfacts) + (i < mrest ? 1 : 0) - (2*c->bw), 0);
			my += HEIGHT(c) + iv;
		} else {
			resize(c, sx, sy, sw - (2*c->bw), sh - (2*c->bw), 0);
		}
}


/*
 * Fibonacci layout + gaps
 * https://dwm.suckless.org/patches/fibonacci/
 */
void
fibonacci(Monitor *m, int s)
{
	unsigned int i, n;
	int nx, ny, nw, nh;
	int oh, ov, ih, iv;
	int nv, hrest = 0, wrest = 0, r = 1;
	Client *c;

	getgaps(m, &oh, &ov, &ih, &iv, &n);
	if (n == 0)
		return;

	nx = m->wx + oh;
	ny = m->wy + ov;
	nw = m->ww - 2*oh;
	nh = m->wh - 2*ov;

	for (i = 0, c = nexttiled(m->clients); c; c = nexttiled(c->next)) {
		if (r) {
			if ((i % 2 && (nh - iv) / 2 <= (bh + 2*c->bw))
			   || (!(i % 2) && (nw - ih) / 2 <= (bh + 2*c->bw))) {
				r = 0;
			}
			if (r && i < n - 1) {
				if (i % 2) {
					nv = (nh - iv) / 2;
					hrest = nh - 2*nv - iv;
					nh = nv;
				} else {
					nv = (nw - ih) / 2;
					wrest = nw - 2*nv - ih;
					nw = nv;
				}

				if ((i % 4) == 2 && !s)
					nx += nw + ih;
				else if ((i % 4) == 3 && !s)
					ny += nh + iv;
			}

			if ((i % 4) == 0) {
				if (s) {
					ny += nh + iv;
					nh += hrest;
				}
				else {
					nh -= hrest;
					ny -= nh + iv;
				}
			}
			else if ((i % 4) == 1) {
				nx += nw + ih;
				nw += wrest;
			}
			else if ((i % 4) == 2) {
				ny += nh + iv;
				nh += hrest;
				if (i < n - 1)
					nw += wrest;
			}
			else if ((i % 4) == 3) {
				if (s) {
					nx += nw + ih;
					nw -= wrest;
				} else {
					nw -= wrest;
					nx -= nw + ih;
					nh += hrest;
				}
			}
			if (i == 0)	{
				if (n != 1) {
					nw = (m->ww - ih - 2*oh) - (m->ww - ih - 2*oh) * (1 - m->mfact);
					wrest = 0;
				}
				ny = m->wy + ov;
			}
			else if (i == 1)
				nw = m->ww - nw - ih - 2*oh;
			i++;
		}

		resize(c,
			#if PATCH_MIRROR_LAYOUT
			m->mirror ? (m->wx + m->ww - (nx - m->wx) - nw - (2*c->bw)) :
			#endif // PATCH_MIRROR_LAYOUT
			nx, ny, nw - (2*c->bw), nh - (2*c->bw), False
		);
	}
}

void
dwindle(Monitor *m)
{
	fibonacci(m, 1);
}

void
spiral(Monitor *m)
{
	fibonacci(m, 0);
}

/*
 * Gappless grid layout + gaps (ironically)
 * https://dwm.suckless.org/patches/gaplessgrid/
 */
void
gaplessgrid(Monitor *m)
{
	unsigned int i, n;
	int x, y, cols, rows, ch, cw, cn, rn, rrest, crest; // counters
	int oh, ov, ih, iv;
	Client *c;

	getgaps(m, &oh, &ov, &ih, &iv, &n);
	if (n == 0)
		return;

	/* grid dimensions */
	for (cols = 0; cols <= n/2; cols++)
		if (cols*cols >= n)
			break;
	if (n == 5) /* set layout against the general calculation: not 1:2:2, but 2:3 */
		cols = 2;
	rows = n/cols;
	cn = rn = 0; // reset column no, row no, client count

	ch = (m->wh - 2*ov - iv * (rows - 1)) / rows;
	cw = (m->ww - 2*oh - ih * (cols - 1)) / cols;
	rrest = (m->wh - 2*ov - iv * (rows - 1)) - ch * rows;
	crest = (m->ww - 2*oh - ih * (cols - 1)) - cw * cols;
	x = m->wx + oh;
	y = m->wy + ov;

	for (i = 0, c = nexttiled(m->clients); c; i++, c = nexttiled(c->next)) {
		if (i/rows + 1 > cols - n%cols) {
			rows = n/cols + 1;
			ch = (m->wh - 2*ov - iv * (rows - 1)) / rows;
			rrest = (m->wh - 2*ov - iv * (rows - 1)) - ch * rows;
		}
		resize(c,
			x,
			y + rn*(ch + iv) + MIN(rn, rrest),
			cw + (cn < crest ? 1 : 0) - 2*c->bw,
			ch + (rn < rrest ? 1 : 0) - 2*c->bw,
			0);
		rn++;
		if (rn >= rows) {
			rn = 0;
			x += cw + ih + (cn < crest ? 1 : 0);
			cn++;
		}
	}
}

/*
 * Gridmode layout + gaps
 * https://dwm.suckless.org/patches/gridmode/
 */
void
grid(Monitor *m)
{
	unsigned int i, n;
	int cx, cy, cw, ch, cc, cr, chrest, cwrest, cols, rows;
	int oh, ov, ih, iv;
	Client *c;

	getgaps(m, &oh, &ov, &ih, &iv, &n);

	/* grid dimensions */
	for (rows = 0; rows <= n/2; rows++)
		if (rows*rows >= n)
			break;
	cols = (rows && (rows - 1) * rows >= n) ? rows - 1 : rows;

	/* window geoms (cell height/width) */
	ch = (m->wh - 2*ov - iv * (rows - 1)) / (rows ? rows : 1);
	cw = (m->ww - 2*oh - ih * (cols - 1)) / (cols ? cols : 1);
	chrest = (m->wh - 2*ov - iv * (rows - 1)) - ch * rows;
	cwrest = (m->ww - 2*oh - ih * (cols - 1)) - cw * cols;
	for (i = 0, c = nexttiled(m->clients); c; c = nexttiled(c->next), i++) {
		cc = i / rows;
		cr = i % rows;
		cx = m->wx + oh + cc * (cw + ih) + MIN(cc, cwrest);
		cy = m->wy + ov + cr * (ch + iv) + MIN(cr, chrest);
		resize(c, cx, cy, cw + (cc < cwrest ? 1 : 0) - 2*c->bw, ch + (cr < chrest ? 1 : 0) - 2*c->bw, False);
	}
}

/*
 * Horizontal grid layout + gaps
 * https://dwm.suckless.org/patches/horizgrid/
 */
void
horizgrid(Monitor *m) {
	Client *c;
	unsigned int n, i;
	int oh, ov, ih, iv;
	int mx = 0, my = 0, mh = 0, mw = 0;
	int sx = 0, sy = 0, sh = 0, sw = 0;
	int ntop, nbottom = 1;
	float mfacts = 0, sfacts = 0;
	int mrest, srest, mtotal = 0, stotal = 0;

	/* Count windows */
	getgaps(m, &oh, &ov, &ih, &iv, &n);
	if (n == 0)
		return;

	if (n <= 2)
		ntop = n;
	else {
		ntop = n / 2;
		nbottom = n - ntop;
	}
	sx = mx = m->wx + oh;
	sy = my = m->wy + ov;
	sh = mh = m->wh - 2*ov;
	sw = mw = m->ww - 2*oh;

	if (n > ntop) {
		sh = (mh - iv) / 2;
		mh = mh - iv - sh;
		sy = my + mh + iv;
		mw = m->ww - 2*oh - ih * (ntop - 1);
		sw = m->ww - 2*oh - ih * (nbottom - 1);
	}

	/* calculate facts */
	for (i = 0, c = nexttiled(m->clients); c; c = nexttiled(c->next), i++)
		if (i < ntop)
			mfacts += c->cfact;
		else
			sfacts += c->cfact;

	for (i = 0, c = nexttiled(m->clients); c; c = nexttiled(c->next), i++)
		if (i < ntop)
			mtotal += mh * (c->cfact / mfacts);
		else
			stotal += sw * (c->cfact / sfacts);

	mrest = mh - mtotal;
	srest = sw - stotal;

	for (i = 0, c = nexttiled(m->clients); c; c = nexttiled(c->next), i++)
		if (i < ntop) {
			resize(c, mx, my, mw * (c->cfact / mfacts) + (i < mrest ? 1 : 0) - (2*c->bw), mh - (2*c->bw), 0);
			mx += WIDTH(c) + ih;
		} else {
			resize(c, sx, sy, sw * (c->cfact / sfacts) + ((i - ntop) < srest ? 1 : 0) - (2*c->bw), sh - (2*c->bw), 0);
			sx += WIDTH(c) + ih;
		}
}

/*
 * nrowgrid layout + gaps
 * https://dwm.suckless.org/patches/nrowgrid/
 */
void
nrowgrid(Monitor *m)
{
	unsigned int n;
	int ri = 0, ci = 0;  /* counters */
	int oh, ov, ih, iv;                         /* vanitygap settings */
	unsigned int cx, cy, cw, ch;                /* client geometry */
	unsigned int uw = 0, uh = 0, uc = 0;        /* utilization trackers */
	unsigned int cols, rows = m->nmaster + 1;
	Client *c;

	/* count clients */
	getgaps(m, &oh, &ov, &ih, &iv, &n);

	/* nothing to do here */
	if (n == 0)
		return;

	/* force 2 clients to always split vertically */
	if (FORCE_VSPLIT && n == 2)
		rows = 1;

	/* never allow empty rows */
	if (n < rows)
		rows = n;

	/* define first row */
	cols = n / rows;
	uc = cols;
	cy = m->wy + ov;
	ch = (m->wh - 2*ov - iv*(rows - 1)) / rows;
	uh = ch;

	for (c = nexttiled(m->clients); c; c = nexttiled(c->next), ci++) {
		if (ci == cols) {
			uw = 0;
			ci = 0;
			ri++;

			/* next row */
			cols = (n - uc) / (rows - ri);
			uc += cols;
			cy = m->wy + ov + uh + iv;
			uh += ch + iv;
		}

		cx = m->wx + oh + uw;
		cw = (m->ww - 2*oh - uw) / (cols - ci);
		uw += cw + ih;

		resize(c, cx, cy, cw - (2*c->bw), ch - (2*c->bw), 0);
	}
}

/*
 * Default tile layout + gaps
 */
static void
tile(Monitor *m)
{
	unsigned int i, n;
	int oh, ov, ih, iv;
	int mx = 0, my = 0, mh = 0, mw = 0;
	int sx = 0, sy = 0, sh = 0, sw = 0;
	float mfacts, sfacts;
	int mrest, srest;
	Client *c;

	getgaps(m, &oh, &ov, &ih, &iv, &n);
	if (n == 0)
		return;

	sx = mx = m->wx + oh;
	sy = my = m->wy + ov;
	mh = m->wh - 2*ov - iv * (MIN(n, m->nmaster) - 1);
	sh = m->wh - 2*ov - iv * (n - m->nmaster - 1);
	sw = mw = m->ww - 2*oh;

	if (m->nmaster && n > m->nmaster) {
		sw = (mw - ih) * (1 - m->mfact);
		mw = mw - ih - sw;
		#if PATCH_MIRROR_LAYOUT
		if (m->mirror) {
			mx = m->wx + m->ww - oh - mw;
			sx = m->wx + oh;
		}
		else
		#endif // PATCH_MIRROR_LAYOUT
		sx = mx + mw + ih;
	}

	getfacts(m, mh, sh, &mfacts, &sfacts, &mrest, &srest);

	for (i = 0, c = nexttiled(m->clients); c; c = nexttiled(c->next), i++)
		if (i < m->nmaster) {
			resize(c, mx, my, mw - (2*c->bw), mh * (c->cfact / mfacts) + (i < mrest ? 1 : 0) - (2*c->bw), 0);
			my += HEIGHT(c) + iv;
		} else {
			resize(c, sx, sy, sw - (2*c->bw), sh * (c->cfact / sfacts) + ((i - m->nmaster) < srest ? 1 : 0) - (2*c->bw), 0);
			sy += HEIGHT(c) + iv;
		}
}