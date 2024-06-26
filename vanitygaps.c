#if PATCH_VANITY_GAPS
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
#else // NO PATCH_VANITY_GAPS
void
tilecount(Monitor *m, unsigned int *nc)
{
	unsigned n;
	Client *c;
	for (n = 0, c = nexttiled(m->clients); c; c = nexttiled(c->next), n++);
	*nc = n;
}
#endif // PATCH_VANITY_GAPS

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

#if PATCH_LAYOUT_BSTACK
/*
 * Bottomstack layout + gaps
 * https://dwm.suckless.org/patches/bottomstack/
 */
static void
bstack(Monitor *m)
{
	unsigned int i, n;
	#if PATCH_VANITY_GAPS
	int oh, ov, ih, iv;
	#endif // PATCH_VANITY_GAPS
	int mx = 0, my = 0, mh = 0, mw = 0;
	int sx = 0, sy = 0, sh = 0, sw = 0;
	float mfacts, sfacts;
	int mrest, srest;
	Client *c;

	#if PATCH_VANITY_GAPS
	getgaps(m, &oh, &ov, &ih, &iv, &n);
	#else // NO PATCH_VANITY_GAPS
	tilecount(m, &n);
	#endif // PATCH_VANITY_GAPS
	if (n == 0)
		return;

	sx = mx = m->wx
		#if PATCH_VANITY_GAPS
		+ oh
		#endif // PATCH_VANITY_GAPS
	;
	sy = my = m->wy
		#if PATCH_VANITY_GAPS
		+ ov
		#endif // PATCH_VANITY_GAPS
	;
	sh = mh = m->wh
		#if PATCH_VANITY_GAPS
		- 2*ov
		#endif // PATCH_VANITY_GAPS
	;
	mw = m->ww
		#if PATCH_VANITY_GAPS
		- 2*oh - ih * (MIN(n, m->nmaster) - 1)
		#endif // PATCH_VANITY_GAPS
	;
	sw = m->ww
		#if PATCH_VANITY_GAPS
		- 2*oh - ih * (n - m->nmaster - 1)
		#endif // PATCH_VANITY_GAPS
	;

	if (m->nmaster && n > m->nmaster) {
		sh = (
			mh
			#if PATCH_VANITY_GAPS
			- iv
			#endif // PATCH_VANITY_GAPS
		) * (1 - m->mfact);
		mh = mh - sh
			#if PATCH_VANITY_GAPS
			- iv
			#endif // PATCH_VANITY_GAPS
		;
		sx = mx;
		sy = my + mh
			#if PATCH_VANITY_GAPS
			+ iv
			#endif // PATCH_VANITY_GAPS
		;
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
			mx += WIDTH(c)
				#if PATCH_VANITY_GAPS
				+ ih
				#endif // PATCH_VANITY_GAPS
			;
		} else {
			resize(
				c, sx,
				#if PATCH_MIRROR_LAYOUT
				m->mirror ? (m->wy + m->wh - (sy - m->wy) - (sh - (2*c->bw))) :
				#endif // PATCH_MIRROR_LAYOUT
				sy,
				sw * (c->cfact / sfacts) + ((i - m->nmaster) < srest ? 1 : 0) - (2*c->bw), sh - (2*c->bw), 0
			);
			sx += WIDTH(c)
				#if PATCH_VANITY_GAPS
				+ ih
				#endif // PATCH_VANITY_GAPS
			;
		}
	}
}
#endif // PATCH_LAYOUT_BSTACK

#if PATCH_LAYOUT_BSTACKHORIZ
static void
bstackhoriz(Monitor *m)
{
	unsigned int i, n;
	#if PATCH_VANITY_GAPS
	int oh, ov, ih, iv;
	#endif // PATCH_VANITY_GAPS
	int mx = 0, my = 0, mh = 0, mw = 0;
	int sx = 0, sy = 0, sh = 0, sw = 0;
	float mfacts, sfacts;
	int mrest, srest;
	Client *c;

	#if PATCH_VANITY_GAPS
	getgaps(m, &oh, &ov, &ih, &iv, &n);
	#else // NO PATCH_VANITY_GAPS
	tilecount(m, &n);
	#endif // PATCH_VANITY_GAPS
	if (n == 0)
		return;

	sx = mx = m->wx
		#if PATCH_VANITY_GAPS
		+ oh
		#endif // PATCH_VANITY_GAPS
	;
	sy = my = m->wy
		#if PATCH_VANITY_GAPS
		+ ov
		#endif // PATCH_VANITY_GAPS
	;
	mh = m->wh
		#if PATCH_VANITY_GAPS
		- 2*ov
		#endif // PATCH_VANITY_GAPS
	;
	sh = m->wh
		#if PATCH_VANITY_GAPS
		- 2*ov - iv * (n - m->nmaster - 1)
		#endif // PATCH_VANITY_GAPS
	;
	mw = m->ww
		#if PATCH_VANITY_GAPS
		- 2*oh - ih * (MIN(n, m->nmaster) - 1)
		#endif // PATCH_VANITY_GAPS
	;
	sw = m->ww
		#if PATCH_VANITY_GAPS
		- 2*oh
		#endif // PATCH_VANITY_GAPS
	;

	if (m->nmaster && n > m->nmaster) {
		sh = (
			mh
			#if PATCH_VANITY_GAPS
			- iv
			#endif // PATCH_VANITY_GAPS
		) * (1 - m->mfact);
		mh = mh - sh
			#if PATCH_VANITY_GAPS
			- iv
			#endif // PATCH_VANITY_GAPS
		;
		sy = my + mh
			#if PATCH_VANITY_GAPS
			+ iv
			#endif // PATCH_VANITY_GAPS
		;
		sh = m->wh - mh
			#if PATCH_VANITY_GAPS
			- 2*ov - iv * (n - m->nmaster)
			#endif // PATCH_VANITY_GAPS
		;
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
			mx += WIDTH(c)
				#if PATCH_VANITY_GAPS
				+ ih
				#endif // PATCH_VANITY_GAPS
			;
		} else {
			resize(
				c, sx,
				#if PATCH_MIRROR_LAYOUT
				m->mirror ? (m->wy + m->wh - (sy - m->wy) - (sh * (c->cfact / sfacts) + ((i - m->nmaster) < srest ? 1 : 0) - (2*c->bw))) :
				#endif // PATCH_MIRROR_LAYOUT
				sy,
				sw - (2*c->bw), sh * (c->cfact / sfacts) + ((i - m->nmaster) < srest ? 1 : 0) - (2*c->bw), 0
			);
			sy += HEIGHT(c)
				#if PATCH_VANITY_GAPS
				+ iv
				#endif // PATCH_VANITY_GAPS
			;
		}
	}
}
#endif // PATCH_LAYOUT_BSTACKHORIZ

#if PATCH_LAYOUT_CENTREDMASTER
/*
 * Centred master layout + gaps
 * https://dwm.suckless.org/patches/centeredmaster/
 */
void
centredmaster(Monitor *m)
{
	unsigned int i, n;
	#if PATCH_VANITY_GAPS
	int oh, ov, ih, iv;
	#endif // PATCH_VANITY_GAPS
	int mx = 0, my = 0, mh = 0, mw = 0;
	int lx = 0, ly = 0, lw = 0, lh = 0;
	int rx = 0, ry = 0, rw = 0, rh = 0;
	float mfacts = 0, lfacts = 0, rfacts = 0;
	int mtotal = 0, ltotal = 0, rtotal = 0;
	int mrest = 0, lrest = 0, rrest = 0;
	Client *c;

	#if PATCH_VANITY_GAPS
	getgaps(m, &oh, &ov, &ih, &iv, &n);
	#else // NO PATCH_VANITY_GAPS
	tilecount(m, &n);
	#endif // PATCH_VANITY_GAPS
	if (n == 0)
		return;

	/* initialize areas */
	mx = m->wx
		#if PATCH_VANITY_GAPS
		+ oh
		#endif // PATCH_VANITY_GAPS
	;
	my = m->wy
		#if PATCH_VANITY_GAPS
		+ ov
		#endif // PATCH_VANITY_GAPS
	;
	mh = m->wh
		#if PATCH_VANITY_GAPS
		- 2*ov - iv * ((!m->nmaster ? n : MIN(n, m->nmaster)) - 1)
		#endif // PATCH_VANITY_GAPS
	;
	mw = m->ww
		#if PATCH_VANITY_GAPS
		- 2*oh
		#endif // PATCH_VANITY_GAPS
	;
	lh = m->wh
		#if PATCH_VANITY_GAPS
		- 2*ov - iv * (((n - m->nmaster) / 2) - 1)
		#endif // PATCH_VANITY_GAPS
	;
	rh = m->wh
		#if PATCH_VANITY_GAPS
		- 2*ov - iv * (((n - m->nmaster) / 2) - ((n - m->nmaster) % 2 ? 0 : 1))
		#endif // PATCH_VANITY_GAPS
	;

	if (m->nmaster && n > m->nmaster) {
		/* go mfact box in the centre if more than nmaster clients */
		if (n - m->nmaster > 1) {
			/* ||<-S->|<---M--->|<-S->|| */
			mw = (m->ww
				#if PATCH_VANITY_GAPS
				- 2*oh - 2*ih
				#endif // PATCH_VANITY_GAPS
			) * m->mfact;
			lw = (m->ww - mw
				#if PATCH_VANITY_GAPS
				- 2*oh - 2*ih
				#endif // PATCH_VANITY_GAPS
			) / 2;
			rw = (m->ww - mw
				#if PATCH_VANITY_GAPS
				- 2*oh - 2*ih
				#endif // PATCH_VANITY_GAPS
			) - lw;
			mx += lw
				#if PATCH_VANITY_GAPS
				+ ih
				#endif // PATCH_VANITY_GAPS
			;
		} else {
			/* ||<---M--->|<-S->|| */
			mw = (mw
				#if PATCH_VANITY_GAPS
				- ih
				#endif // PATCH_VANITY_GAPS
			) * m->mfact;
			lw = 0;
			rw = m->ww - mw
				#if PATCH_VANITY_GAPS
				- ih - 2*oh
				#endif // PATCH_VANITY_GAPS
			;
		}
		lx = m->wx
			#if PATCH_VANITY_GAPS
			+ oh
			#endif // PATCH_VANITY_GAPS
		;
		ly = m->wy
			#if PATCH_VANITY_GAPS
			+ ov
			#endif // PATCH_VANITY_GAPS
		;
		rx = mx + mw
			#if PATCH_VANITY_GAPS
			+ ih
			#endif // PATCH_VANITY_GAPS
		;
		ry = m->wy
			#if PATCH_VANITY_GAPS
			+ ov
			#endif // PATCH_VANITY_GAPS
		;
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
			my += HEIGHT(c)
				#if PATCH_VANITY_GAPS
				+ iv
				#endif // PATCH_VANITY_GAPS
			;
		} else {
			/* stack clients are stacked vertically */
			if ((i - m->nmaster) % 2 ) {
				resize(c, lx, ly, lw - (2*c->bw), lh * (c->cfact / lfacts) + ((i - 2*m->nmaster) < 2*lrest ? 1 : 0) - (2*c->bw), 0);
				ly += HEIGHT(c)
					#if PATCH_VANITY_GAPS
					+ iv
					#endif // PATCH_VANITY_GAPS
				;
			} else {
				resize(c, rx, ry, rw - (2*c->bw), rh * (c->cfact / rfacts) + ((i - 2*m->nmaster) < 2*rrest ? 1 : 0) - (2*c->bw), 0);
				ry += HEIGHT(c)
					#if PATCH_VANITY_GAPS
					+ iv
					#endif // PATCH_VANITY_GAPS
				;
			}
		}
	}
}
#endif // PATCH_LAYOUT_CENTREDMASTER

#if PATCH_LAYOUT_CENTREDFLOATINGMASTER
void
centredfloatingmaster(Monitor *m)
{
	unsigned int i, n;
	float mfacts, sfacts;
	#if PATCH_VANITY_GAPS
	float mihf = 1.0; // master inner horizontal gap factor
	int oh, ov, ih, iv;
	#endif // PATCH_VANITY_GAPS
	int mrest, srest;
	int mx = 0, my = 0, mh = 0, mw = 0;
	int sx = 0, sy = 0, sh = 0, sw = 0;
	Client *c;

	#if PATCH_VANITY_GAPS
	getgaps(m, &oh, &ov, &ih, &iv, &n);
	#else // NO PATCH_VANITY_GAPS
	tilecount(m, &n);
	#endif // PATCH_VANITY_GAPS
	if (n == 0)
		return;

	sx = mx = m->wx
		#if PATCH_VANITY_GAPS
		+ oh
		#endif // PATCH_VANITY_GAPS
	;
	sy = my = m->wy
		#if PATCH_VANITY_GAPS
		+ ov
		#endif // PATCH_VANITY_GAPS
	;
	sh = mh = m->wh
		#if PATCH_VANITY_GAPS
		- 2*ov
		#endif // PATCH_VANITY_GAPS
	;
	mw = m->ww
		#if PATCH_VANITY_GAPS
		- 2*oh - ih*(n - 1)
		#endif // PATCH_VANITY_GAPS
	;
	sw = m->ww
		#if PATCH_VANITY_GAPS
		- 2*oh - ih*(n - m->nmaster - 1)
		#endif // PATCH_VANITY_GAPS
	;

	if (m->nmaster && n > m->nmaster) {
		#if PATCH_VANITY_GAPS
		mihf = 0.8;
		#endif // PATCH_VANITY_GAPS
		/* go mfact box in the centre if more than nmaster clients */
		if (m->ww > m->wh) {
			mw = m->ww * m->mfact
				#if PATCH_VANITY_GAPS
				- ih*mihf*(MIN(n, m->nmaster) - 1)
				#endif // PATCH_VANITY_GAPS
			;
			mh = m->wh * 0.9;
		} else {
			mw = m->ww * 0.9
				#if PATCH_VANITY_GAPS
				- ih*mihf*(MIN(n, m->nmaster) - 1)
				#endif // PATCH_VANITY_GAPS
			;
			mh = m->wh * m->mfact;
		}
		mx = m->wx + (m->ww - mw) / 2;
		my = m->wy + (m->wh - mh) / 2;

		sx = m->wx
			#if PATCH_VANITY_GAPS
			+ oh
			#endif // PATCH_VANITY_GAPS
		;
		sy = m->wy
			#if PATCH_VANITY_GAPS
			+ ov
			#endif // PATCH_VANITY_GAPS
		;
		sh = m->wh
			#if PATCH_VANITY_GAPS
			- 2*ov
			#endif // PATCH_VANITY_GAPS
		;
	}

	getfacts(m, mw, sw, &mfacts, &sfacts, &mrest, &srest);

	for (i = 0, c = nexttiled(m->clients); c; c = nexttiled(c->next), i++)
		if (i < m->nmaster) {
			/* nmaster clients are stacked horizontally, in the centre of the screen */
			resize(c, mx, my, mw * (c->cfact / mfacts) + (i < mrest ? 1 : 0) - (2*c->bw), mh - (2*c->bw), 0);
			mx += WIDTH(c)
				#if PATCH_VANITY_GAPS
				+ ih*mihf
				#endif // PATCH_VANITY_GAPS
			;
		} else {
			/* stack clients are stacked horizontally */
			resize(c, sx, sy, sw * (c->cfact / sfacts) + ((i - m->nmaster) < srest ? 1 : 0) - (2*c->bw), sh - (2*c->bw), 0);
			sx += WIDTH(c)
				#if PATCH_VANITY_GAPS
				+ ih
				#endif // PATCH_VANITY_GAPS
			;
		}
}
#endif // PATCH_LAYOUT_CENTREDFLOATINGMASTER

#if PATCH_LAYOUT_DECK
/*
 * Deck layout + gaps
 * https://dwm.suckless.org/patches/deck/
 */
void
deck(Monitor *m)
{
	unsigned int i, n;
	#if PATCH_VANITY_GAPS
	int oh, ov, ih, iv;
	#endif // PATCH_VANITY_GAPS
	int mx = 0, my = 0, mh = 0, mw = 0;
	int sx = 0, sy = 0, sh = 0, sw = 0;
	float mfacts, sfacts;
	int mrest, srest;
	Client *c;

	#if PATCH_VANITY_GAPS
	getgaps(m, &oh, &ov, &ih, &iv, &n);
	#else // NO PATCH_VANITY_GAPS
	tilecount(m, &n);
	#endif // PATCH_VANITY_GAPS
	if (n == 0)
		return;

	sx = mx = m->wx
		#if PATCH_VANITY_GAPS
		+ oh
		#endif // PATCH_VANITY_GAPS
	;
	sy = my = m->wy
		#if PATCH_VANITY_GAPS
		+ ov
		#endif // PATCH_VANITY_GAPS
	;
	sh = mh = m->wh
		#if PATCH_VANITY_GAPS
		- 2*ov - iv * (MIN(n, m->nmaster) - 1)
		#endif // PATCH_VANITY_GAPS
	;
	sw = mw = m->ww
		#if PATCH_VANITY_GAPS
		- 2*oh
		#endif // PATCH_VANITY_GAPS
	;

	if (m->nmaster && n > m->nmaster) {
		sw = (mw
			#if PATCH_VANITY_GAPS
			- ih
			#endif // PATCH_VANITY_GAPS
		) * (1 - m->mfact);
		mw = mw - sw
			#if PATCH_VANITY_GAPS
			- ih
			#endif // PATCH_VANITY_GAPS
		;
		sh = m->wh
			#if PATCH_VANITY_GAPS
			- 2*ov
			#endif // PATCH_VANITY_GAPS
		;
		#if PATCH_MIRROR_LAYOUT
		if (m->mirror) {
			mx += sw
				#if PATCH_VANITY_GAPS
				+ ih
				#endif // PATCH_VANITY_GAPS
			;
			sx = m->wx
				#if PATCH_VANITY_GAPS
				+ oh
				#endif // PATCH_VANITY_GAPS
			;
		}
		else
		#endif // PATCH_MIRROR_LAYOUT
		sx = mx + mw
			#if PATCH_VANITY_GAPS
			+ ih
			#endif // PATCH_VANITY_GAPS
		;
	}

	getfacts(m, mh, sh, &mfacts, &sfacts, &mrest, &srest);

	if (n - m->nmaster > 0) /* override layout symbol */
		snprintf(m->ltsymbol, sizeof m->ltsymbol, "D [%d]", n - m->nmaster);

	for (i = 0, c = nexttiled(m->clients); c; c = nexttiled(c->next), i++)
		if (i < m->nmaster) {
			resize(c, mx, my, mw - (2*c->bw), mh * (c->cfact / mfacts) + (i < mrest ? 1 : 0) - (2*c->bw), 0);
			my += HEIGHT(c)
				#if PATCH_VANITY_GAPS
				+ iv
				#endif // PATCH_VANITY_GAPS
			;
		} else {
			resize(c, sx, sy, sw - (2*c->bw), sh - (2*c->bw), 0);
		}
}
#endif // PATCH_LAYOUT_DECK

#if PATCH_LAYOUT_DWINDLE || PATCH_LAYOUT_SPIRAL
/*
 * Fibonacci layout + gaps
 * https://dwm.suckless.org/patches/fibonacci/
 */
void
fibonacci(Monitor *m, int s)
{
	unsigned int i, n;
	int nx, ny, nw, nh;
	#if PATCH_VANITY_GAPS
	int oh, ov, ih, iv;
	#endif // PATCH_VANITY_GAPS
	int nv, hrest = 0, wrest = 0, r = 1;
	Client *c;

	#if PATCH_VANITY_GAPS
	getgaps(m, &oh, &ov, &ih, &iv, &n);
	#else // NO PATCH_VANITY_GAPS
	tilecount(m, &n);
	#endif // PATCH_VANITY_GAPS
	if (n == 0)
		return;

	nx = m->wx
		#if PATCH_VANITY_GAPS
		+ oh
		#endif // PATCH_VANITY_GAPS
	;
	ny = m->wy
		#if PATCH_VANITY_GAPS
		+ ov
		#endif // PATCH_VANITY_GAPS
	;
	nw = m->ww
		#if PATCH_VANITY_GAPS
		- 2*oh
		#endif // PATCH_VANITY_GAPS
	;
	nh = m->wh
		#if PATCH_VANITY_GAPS
		- 2*ov
		#endif // PATCH_VANITY_GAPS
	;

	for (i = 0, c = nexttiled(m->clients); c; c = nexttiled(c->next)) {
		if (r) {
			if ((i % 2 && (nh
				#if PATCH_VANITY_GAPS
				- iv
				#endif // PATCH_VANITY_GAPS
				) / 2 <= (bh + 2*c->bw)) || (!(i % 2) && (nw
				#if PATCH_VANITY_GAPS
				- ih
				#endif // PATCH_VANITY_GAPS
				) / 2 <= (bh + 2*c->bw))) {
				r = 0;
			}
			if (r && i < n - 1) {
				if (i % 2) {
					nv = (nh
						#if PATCH_VANITY_GAPS
						- iv
						#endif // PATCH_VANITY_GAPS
					) / 2;
					hrest = nh - 2*nv
						#if PATCH_VANITY_GAPS
						- iv
						#endif // PATCH_VANITY_GAPS
					;
					nh = nv;
				} else {
					nv = (nw
						#if PATCH_VANITY_GAPS
						- ih
						#endif // PATCH_VANITY_GAPS
					) / 2;
					wrest = nw - 2*nv
						#if PATCH_VANITY_GAPS
						- ih
						#endif // PATCH_VANITY_GAPS
					;
					nw = nv;
				}

				if ((i % 4) == 2 && !s)
					nx += nw
						#if PATCH_VANITY_GAPS
						+ ih
						#endif // PATCH_VANITY_GAPS
					;
				else if ((i % 4) == 3 && !s)
					ny += nh
						#if PATCH_VANITY_GAPS
						+ iv
						#endif // PATCH_VANITY_GAPS
					;
			}

			if ((i % 4) == 0) {
				if (s) {
					ny += nh
						#if PATCH_VANITY_GAPS
						+ iv
						#endif // PATCH_VANITY_GAPS
					;
					nh += hrest;
				}
				else {
					nh -= hrest;
					ny -= nh
						#if PATCH_VANITY_GAPS
						+ iv
						#endif // PATCH_VANITY_GAPS
					;
				}
			}
			else if ((i % 4) == 1) {
				nx += nw
					#if PATCH_VANITY_GAPS
					+ ih
					#endif // PATCH_VANITY_GAPS
				;
				nw += wrest;
			}
			else if ((i % 4) == 2) {
				ny += nh
					#if PATCH_VANITY_GAPS
					+ iv
					#endif // PATCH_VANITY_GAPS
				;
				nh += hrest;
				if (i < n - 1)
					nw += wrest;
			}
			else if ((i % 4) == 3) {
				if (s) {
					nx += nw
						#if PATCH_VANITY_GAPS
						+ ih
						#endif // PATCH_VANITY_GAPS
					;
					nw -= wrest;
				} else {
					nw -= wrest;
					nx -= nw
						#if PATCH_VANITY_GAPS
						+ ih
						#endif // PATCH_VANITY_GAPS
					;
					nh += hrest;
				}
			}
			if (i == 0)	{
				if (n != 1) {
					nw = (m->ww
						#if PATCH_VANITY_GAPS
						- ih - 2*oh
						#endif // PATCH_VANITY_GAPS
					) - (m->ww
						#if PATCH_VANITY_GAPS
						- ih - 2*oh
						#endif // PATCH_VANITY_GAPS
					) * (1 - m->mfact);
					wrest = 0;
				}
				ny = m->wy
					#if PATCH_VANITY_GAPS
					+ ov
					#endif // PATCH_VANITY_GAPS
				;
			}
			else if (i == 1)
				nw = m->ww - nw
					#if PATCH_VANITY_GAPS
					- ih - 2*oh
					#endif // PATCH_VANITY_GAPS
				;
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
#endif // PATCH_LAYOUT_DWINDLE || PATCH_LAYOUT_SPIRAL

#if PATCH_LAYOUT_DWINDLE
void
dwindle(Monitor *m)
{
	fibonacci(m, 1);
}
#endif // PATCH_LAYOUT_DWINDLE

#if PATCH_LAYOUT_SPIRAL
void
spiral(Monitor *m)
{
	fibonacci(m, 0);
}
#endif // PATCH_LAYOUT_SPIRAL

#if PATCH_LAYOUT_GAPLESSGRID
/*
 * Gappless grid layout + gaps (ironically)
 * https://dwm.suckless.org/patches/gaplessgrid/
 */
void
gaplessgrid(Monitor *m)
{
	unsigned int i, n;
	int x, y, cols, rows, ch, cw, cn, rn, rrest, crest; // counters
	#if PATCH_VANITY_GAPS
	int oh, ov, ih, iv;
	#endif // PATCH_VANITY_GAPS
	Client *c;

	#if PATCH_VANITY_GAPS
	getgaps(m, &oh, &ov, &ih, &iv, &n);
	#else // NO PATCH_VANITY_GAPS
	tilecount(m, &n);
	#endif // PATCH_VANITY_GAPS
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

	ch = (m->wh
		#if PATCH_VANITY_GAPS
		- 2*ov - iv * (rows - 1)
		#endif // PATCH_VANITY_GAPS
	) / rows;
	cw = (m->ww
		#if PATCH_VANITY_GAPS
		- 2*oh - ih * (cols - 1)
		#endif // PATCH_VANITY_GAPS
	) / cols;
	rrest = (m->wh
		#if PATCH_VANITY_GAPS
		- 2*ov - iv * (rows - 1)
		#endif // PATCH_VANITY_GAPS
	) - ch * rows;
	crest = (m->ww
		#if PATCH_VANITY_GAPS
		- 2*oh - ih * (cols - 1)
		#endif // PATCH_VANITY_GAPS
	) - cw * cols;
	x = m->wx
		#if PATCH_VANITY_GAPS
		+ oh
		#endif // PATCH_VANITY_GAPS
	;
	y = m->wy
		#if PATCH_VANITY_GAPS
		+ ov
		#endif // PATCH_VANITY_GAPS
	;

	for (i = 0, c = nexttiled(m->clients); c; i++, c = nexttiled(c->next)) {
		if (i/rows + 1 > cols - n%cols) {
			rows = n/cols + 1;
			ch = (m->wh
				#if PATCH_VANITY_GAPS
				- 2*ov - iv * (rows - 1)
				#endif // PATCH_VANITY_GAPS
			) / rows;
			rrest = (m->wh
				#if PATCH_VANITY_GAPS
				- 2*ov - iv * (rows - 1)
				#endif // PATCH_VANITY_GAPS
			) - ch * rows;
		}
		resize(c,
			x,
			y + rn*(ch
				#if PATCH_VANITY_GAPS
				+ iv
				#endif // PATCH_VANITY_GAPS
			) + MIN(rn, rrest),
			cw + (cn < crest ? 1 : 0) - 2*c->bw,
			ch + (rn < rrest ? 1 : 0) - 2*c->bw,
			0);
		rn++;
		if (rn >= rows) {
			rn = 0;
			x += cw
				#if PATCH_VANITY_GAPS
				+ ih
				#endif // PATCH_VANITY_GAPS
				+ (cn < crest ? 1 : 0);
			cn++;
		}
	}
}
#endif // PATCH_LAYOUT_GAPLESSGRID

#if PATCH_LAYOUT_GRID
/*
 * Gridmode layout + gaps
 * https://dwm.suckless.org/patches/gridmode/
 */
void
grid(Monitor *m)
{
	unsigned int i, n;
	int cx, cy, cw, ch, cc, cr, chrest, cwrest, cols, rows;
	#if PATCH_VANITY_GAPS
	int oh, ov, ih, iv;
	#endif // PATCH_VANITY_GAPS
	Client *c;

	#if PATCH_VANITY_GAPS
	getgaps(m, &oh, &ov, &ih, &iv, &n);
	#else // NO PATCH_VANITY_GAPS
	tilecount(m, &n);
	#endif // PATCH_VANITY_GAPS

	/* grid dimensions */
	for (rows = 0; rows <= n/2; rows++)
		if (rows*rows >= n)
			break;
	cols = (rows && (rows - 1) * rows >= n) ? rows - 1 : rows;

	/* window geoms (cell height/width) */
	ch = (m->wh
		#if PATCH_VANITY_GAPS
		- 2*ov - iv * (rows - 1)
		#endif // PATCH_VANITY_GAPS
	) / (rows ? rows : 1);
	cw = (m->ww
		#if PATCH_VANITY_GAPS
		- 2*oh - ih * (cols - 1)
		#endif // PATCH_VANITY_GAPS
	) / (cols ? cols : 1);
	chrest = (m->wh
		#if PATCH_VANITY_GAPS
		- 2*ov - iv * (rows - 1)
		#endif // PATCH_VANITY_GAPS
	) - ch * rows;
	cwrest = (m->ww
		#if PATCH_VANITY_GAPS
		- 2*oh - ih * (cols - 1)
		#endif // PATCH_VANITY_GAPS
	) - cw * cols;
	for (i = 0, c = nexttiled(m->clients); c; c = nexttiled(c->next), i++) {
		cc = i / rows;
		cr = i % rows;
		cx = m->wx
			#if PATCH_VANITY_GAPS
			+ oh
			#endif // PATCH_VANITY_GAPS
			+ cc * (cw
				#if PATCH_VANITY_GAPS
				+ ih
				#endif // PATCH_VANITY_GAPS
			) + MIN(cc, cwrest)
		;
		cy = m->wy
			#if PATCH_VANITY_GAPS
			+ ov
			#endif // PATCH_VANITY_GAPS
			+ cr * (ch
				#if PATCH_VANITY_GAPS
				+ iv
				#endif // PATCH_VANITY_GAPS
			) + MIN(cr, chrest)
		;
		resize(c, cx, cy, cw + (cc < cwrest ? 1 : 0) - 2*c->bw, ch + (cr < chrest ? 1 : 0) - 2*c->bw, False);
	}
}
#endif // PATCH_LAYOUT_GRID

#if PATCH_LAYOUT_HORIZGRID
/*
 * Horizontal grid layout + gaps
 * https://dwm.suckless.org/patches/horizgrid/
 */
void
horizgrid(Monitor *m) {
	Client *c;
	unsigned int n, i;
	#if PATCH_VANITY_GAPS
	int oh, ov, ih, iv;
	#endif // PATCH_VANITY_GAPS
	int mx = 0, my = 0, mh = 0, mw = 0;
	int sx = 0, sy = 0, sh = 0, sw = 0;
	int ntop;
	#if PATCH_VANITY_GAPS
	int nbottom = 1;
	#endif // PATCH_VANITY_GAPS
	float mfacts = 0, sfacts = 0;
	int mrest, srest, mtotal = 0, stotal = 0;

	/* Count windows */
	#if PATCH_VANITY_GAPS
	getgaps(m, &oh, &ov, &ih, &iv, &n);
	#else // NO PATCH_VANITY_GAPS
	tilecount(m, &n);
	#endif // PATCH_VANITY_GAPS
	if (n == 0)
		return;

	if (n <= 2)
		ntop = n;
	else {
		ntop = n / 2;
		#if PATCH_VANITY_GAPS
		nbottom = n - ntop;
		#endif // PATCH_VANITY_GAPS
	}
	sx = mx = m->wx
		#if PATCH_VANITY_GAPS
		+ oh
		#endif // PATCH_VANITY_GAPS
	;
	sy = my = m->wy
		#if PATCH_VANITY_GAPS
		+ ov
		#endif // PATCH_VANITY_GAPS
	;
	sh = mh = m->wh
		#if PATCH_VANITY_GAPS
		- 2*ov
		#endif // PATCH_VANITY_GAPS
	;
	sw = mw = m->ww
		#if PATCH_VANITY_GAPS
		- 2*oh
		#endif // PATCH_VANITY_GAPS
	;

	if (n > ntop) {
		sh = (mh
			#if PATCH_VANITY_GAPS
			- iv
			#endif // PATCH_VANITY_GAPS
			) / 2
		;
		mh = mh - sh
			#if PATCH_VANITY_GAPS
			- iv
			#endif // PATCH_VANITY_GAPS
		;
		sy = my + mh
			#if PATCH_VANITY_GAPS
			+ iv
			#endif // PATCH_VANITY_GAPS
		;
		mw = m->ww
			#if PATCH_VANITY_GAPS
			- 2*oh - ih * (ntop - 1)
			#endif // PATCH_VANITY_GAPS
		;
		sw = m->ww
			#if PATCH_VANITY_GAPS
			- 2*oh - ih * (nbottom - 1)
			#endif // PATCH_VANITY_GAPS
		;
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
			mx += WIDTH(c)
				#if PATCH_VANITY_GAPS
				+ ih
				#endif // PATCH_VANITY_GAPS
			;
		} else {
			resize(c, sx, sy, sw * (c->cfact / sfacts) + ((i - ntop) < srest ? 1 : 0) - (2*c->bw), sh - (2*c->bw), 0);
			sx += WIDTH(c)
				#if PATCH_VANITY_GAPS
				+ ih
				#endif // PATCH_VANITY_GAPS
			;
		}
}
#endif // PATCH_LAYOUT_HORIZGRID

#if PATCH_LAYOUT_NROWGRID
/*
 * nrowgrid layout + gaps
 * https://dwm.suckless.org/patches/nrowgrid/
 */
void
nrowgrid(Monitor *m)
{
	unsigned int n;
	int ri = 0, ci = 0;  /* counters */
	#if PATCH_VANITY_GAPS
	int oh, ov, ih, iv;
	#endif // PATCH_VANITY_GAPS
	unsigned int cx, cy, cw, ch;                /* client geometry */
	unsigned int uw = 0, uh = 0, uc = 0;        /* utilization trackers */
	unsigned int cols, rows = m->nmaster + 1;
	Client *c;

	/* count clients */
	#if PATCH_VANITY_GAPS
	getgaps(m, &oh, &ov, &ih, &iv, &n);
	#else // NO PATCH_VANITY_GAPS
	tilecount(m, &n);
	#endif // PATCH_VANITY_GAPS

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
	cy = m->wy
		#if PATCH_VANITY_GAPS
		+ ov
		#endif // PATCH_VANITY_GAPS
	;
	ch = (m->wh
		#if PATCH_VANITY_GAPS
		- 2*ov - iv*(rows - 1)
		#endif // PATCH_VANITY_GAPS
	) / rows;
	uh = ch;

	for (c = nexttiled(m->clients); c; c = nexttiled(c->next), ci++) {
		if (ci == cols) {
			uw = 0;
			ci = 0;
			ri++;

			/* next row */
			cols = (n - uc) / (rows - ri);
			uc += cols;
			cy = m->wy + uh
				#if PATCH_VANITY_GAPS
				+ ov + iv
				#endif // PATCH_VANITY_GAPS
			;
			uh += ch
				#if PATCH_VANITY_GAPS
				+ iv
				#endif // PATCH_VANITY_GAPS
			;
		}

		cx = m->wx + uw
			#if PATCH_VANITY_GAPS
			+ oh
			#endif // PATCH_VANITY_GAPS
		;
		cw = (m->ww
			#if PATCH_VANITY_GAPS
			- 2*oh
			#endif // PATCH_VANITY_GAPS
			- uw) / (cols - ci);
		uw += cw
			#if PATCH_VANITY_GAPS
			+ ih
			#endif // PATCH_VANITY_GAPS
		;

		resize(c, cx, cy, cw - (2*c->bw), ch - (2*c->bw), 0);
	}
}
#endif // PATCH_LAYOUT_NROWGRID

/*
 * Default tile layout + gaps
 */
static void
tile(Monitor *m)
{
	unsigned int i, n;
	#if PATCH_VANITY_GAPS
	int oh, ov, ih, iv;
	#endif // PATCH_VANITY_GAPS
	int mx = 0, my = 0, mh = 0, mw = 0;
	int sx = 0, sy = 0, sh = 0, sw = 0;
	float mfacts, sfacts;
	int mrest, srest;
	Client *c;

	#if PATCH_VANITY_GAPS
	getgaps(m, &oh, &ov, &ih, &iv, &n);
	#else // NO PATCH_VANITY_GAPS
	tilecount(m, &n);
	#endif // PATCH_VANITY_GAPS
	if (n == 0)
		return;

	sx = mx = m->wx
		#if PATCH_VANITY_GAPS
		+ oh
		#endif // PATCH_VANITY_GAPS
	;
	sy = my = m->wy
		#if PATCH_VANITY_GAPS
		+ ov
		#endif // PATCH_VANITY_GAPS
	;
	mh = m->wh
		#if PATCH_VANITY_GAPS
		- 2*ov - iv * (MIN(n, m->nmaster) - 1)
		#endif // PATCH_VANITY_GAPS
	;
	sh = m->wh
		#if PATCH_VANITY_GAPS
		- 2*ov - iv * (n - m->nmaster - 1)
		#endif // PATCH_VANITY_GAPS
	;
	sw = mw = m->ww
		#if PATCH_VANITY_GAPS
		- 2*oh
		#endif // PATCH_VANITY_GAPS
	;

	if (m->nmaster && n > m->nmaster) {
		sw = (
			mw
			#if PATCH_VANITY_GAPS
			- ih
			#endif // PATCH_VANITY_GAPS
		) * (1 - m->mfact);
		mw = mw - sw
			#if PATCH_VANITY_GAPS
			- ih
			#endif // PATCH_VANITY_GAPS
		;
		#if PATCH_MIRROR_LAYOUT
		if (m->mirror) {
			mx = m->wx + m->ww - mw
				#if PATCH_VANITY_GAPS
				- oh
				#endif // PATCH_VANITY_GAPS
			;
			sx = m->wx
				#if PATCH_VANITY_GAPS
				+ oh
				#endif // PATCH_VANITY_GAPS
			;
		}
		else
		#endif // PATCH_MIRROR_LAYOUT
		sx = mx + mw
			#if PATCH_VANITY_GAPS
			+ ih
			#endif // PATCH_VANITY_GAPS
		;
	}

	getfacts(m, mh, sh, &mfacts, &sfacts, &mrest, &srest);

	for (i = 0, c = nexttiled(m->clients); c; c = nexttiled(c->next), i++)
		if (i < m->nmaster) {
			resize(c, mx, my, mw - (2*c->bw), mh * (c->cfact / mfacts) + (i < mrest ? 1 : 0) - (2*c->bw), 0);
			my += HEIGHT(c)
				#if PATCH_VANITY_GAPS
				+ iv
				#endif // PATCH_VANITY_GAPS
			;
		} else {
			resize(c, sx, sy, sw - (2*c->bw), sh * (c->cfact / sfacts) + ((i - m->nmaster) < srest ? 1 : 0) - (2*c->bw), 0);
			sy += HEIGHT(c)
				#if PATCH_VANITY_GAPS
				+ iv
				#endif // PATCH_VANITY_GAPS
			;
		}
}