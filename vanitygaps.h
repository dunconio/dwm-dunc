/* Key binding functions */
#if PATCH_VANITY_GAPS
static void defaultgaps(const Arg *arg);
static void incrgaps(const Arg *arg);
static void incrigaps(const Arg *arg);
static void incrogaps(const Arg *arg);
static void incrohgaps(const Arg *arg);
static void incrovgaps(const Arg *arg);
static void incrihgaps(const Arg *arg);
static void incrivgaps(const Arg *arg);
static void togglegaps(const Arg *arg);
#endif // PATCH_VANITY_GAPS
/* Layouts (delete the ones you do not need) */
#if PATCH_LAYOUT_BSTACK
static void bstack(Monitor *m);
#endif // PATCH_LAYOUT_BSTACK
#if PATCH_LAYOUT_BSTACKHORIZ
static void bstackhoriz(Monitor *m);
#endif // PATCH_LAYOUT_BSTACKHORIZ
#if PATCH_LAYOUT_CENTREDFLOATINGMASTER
static void centredfloatingmaster(Monitor *m);
#endif // PATCH_LAYOUT_CENTREDFLOATINGMASTER
#if PATCH_LAYOUT_CENTREDMASTER
static void centredmaster(Monitor *m);
#endif // PATCH_LAYOUT_CENTREDMASTER
#if PATCH_LAYOUT_DECK
static void deck(Monitor *m);
#endif // PATCH_LAYOUT_DECK
#if PATCH_LAYOUT_DWINDLE
static void dwindle(Monitor *m);
#endif // PATCH_LAYOUT_DWINDLE
#if PATCH_LAYOUT_DWINDLE || PATCH_LAYOUT_SPIRAL
static void fibonacci(Monitor *m, int s);
#endif // PATCH_LAYOUT_DWINDLE || PATCH_LAYOUT_SPIRAL
#if PATCH_LAYOUT_GAPLESSGRID
static void gaplessgrid(Monitor *m);
#endif // PATCH_LAYOUT_GAPLESSGRID
#if PATCH_LAYOUT_GRID
static void grid(Monitor *m);
#endif // PATCH_LAYOUT_GRID
#if PATCH_LAYOUT_HORIZGRID
static void horizgrid(Monitor *m);
#endif // PATCH_LAYOUT_HORIZGRID
#if PATCH_LAYOUT_NROWGRID
static void nrowgrid(Monitor *m);
#endif // PATCH_LAYOUT_NROWGRID
#if PATCH_LAYOUT_SPIRAL
static void spiral(Monitor *m);
#endif // PATCH_LAYOUT_SPIRAL
static void tile(Monitor *m);
/* Internals */
static void getfacts(Monitor *m, int msize, int ssize, float *mf, float *sf, int *mr, int *sr);
#if PATCH_VANITY_GAPS
static void getgaps(Monitor *m, int *oh, int *ov, int *ih, int *iv, unsigned int *nc);
static void setgaps(int oh, int ov, int ih, int iv);
#else // NO PATCH_VANITY_GAPS
static void tilecount(Monitor *m, unsigned int *nc);
#endif // PATCH_VANITY_GAPS

/* Settings */
