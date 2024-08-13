/* See LICENSE file for copyright and license details. */

#define DWM_VERSION_SUFFIX "dunc"

#define DWM_REVISION	"0"

#define WRAP_LENGTH		80		//	Wrap lines when printing supported json parameter details;

#if PATCH_LOG_DIAGNOSTICS
static const unsigned int log_ev_no_root	= 1;		// ignore root events in logdiagnostics_event();
#endif // PATCH_LOG_DIAGNOSTICS

#if PATCH_ALTTAB
/* alt-tab configuration */
static const char *monnumf = "[mon:%s] ";			// format of a monitor number identifier
static const unsigned int tabModKey 		= 0x40;	/* if this key is hold the alt-tab functionality stays acitve. This key must be the same as key that is used to active functin altTabStart `*/
static const unsigned int tabModBackKey		= 0x32;	/* if this key is hold the alt-tab functionality reverses direction. */
static const unsigned int tabCycleClassKey	= 0x31;	/* if this key is hit the alt-tab program moves one position forward in clients stack of the same class. */
static const unsigned int tabCycleKey 		= 0x17;	/* if this key is hit the alt-tab program moves one position forward in clients stack. This key must be the same as key that is used to active functin altTabStart */
static const unsigned int tabEndKey 		= 0x9;	/* if this key is hit the while you're in the alt-tab mode, you'll be returned to previous state (alt-tab mode turned off and your window of origin will be selected) */
static       unsigned int tabPosY 			= 1;	/* tab position on Y axis, 0 = bottom, 1 = centre, 2 = top */
static       unsigned int tabPosX 			= 1;	/* tab position on X axis, 0 = left, 1 = centre, 2 = right */
static 	     unsigned int tabMaxW 			= 600;	/* tab menu width */
static 	     unsigned int tabMaxH 			= 400;	/* tab menu maximum height */
static       unsigned int tabBW				= 4;	// default tab menu border width;
static       unsigned int tabTextAlign		= 0;	// default tab menu text alignment, 0 = left, 1 = centre, 2 = right;
static    unsigned int tabMenuNoCentreAlign	= 1;	// a centred WinTitle element will have a left-aligned dropdown;
static              float tabMenuVertFactor	= 1/3.0f;	// vertical padding scale factor for popup menu items;
static       unsigned int tabMenuVertGap	= 0;		// add vertical padding gap to popup menu items;
#if PATCH_ALTTAB_HIGHLIGHT
static               Bool tabHighlight      = True;	// highlight clients during switching;
#endif // PATCH_ALTTAB_HIGHLIGHT
#if PATCH_FLAG_HIDDEN
static               char *tabHidden		= "[Hidden]";	// string to append/prepend to hidden clients in the alt-tab switcher;
#endif // PATCH_FLAG_HIDDEN
#endif // PATCH_ALTTAB

#if PATCH_BORDERLESS_SOLITARY_CLIENTS
static Bool borderless_solitary = True;
#endif // PATCH_BORDERLESS_SOLITARY_CLIENTS

#if PATCH_HIDE_VACANT_TAGS
static Bool hidevacant = True;
#endif // PATCH_HIDE_VACANT_TAGS

#if PATCH_CLIENT_INDICATORS
static         Bool client_ind				= True;
static unsigned int client_ind_size			= 3;
static          int client_ind_top			= -1;	// -1 will cause client_ind_top to match topbar;
#endif // PATCH_CLIENT_INDICATORS

#if PATCH_TERMINAL_SWALLOWING
static Bool terminal_swallowing = True;
#endif // PATCH_TERMINAL_SWALLOWING

static Bool urgency = True;					// allow urgency flags and show urgency visually;

#if PATCH_MIRROR_LAYOUT
static Bool mirror_layout	= False;		// allow switching of master area and stack area for applicable layouts;
#endif // PATCH_MIRROR_LAYOUT

#if PATCH_MOUSE_POINTER_HIDING
static Bool cursorautohide	 = False;		// global setting, can be overridden per monitor, per tag, and per client rule;
static Bool cursorhideonkeys = False;		// global setting, can be overridden per monitor, per tag, and per client rule;
static unsigned int cursortimeout = 0;		// seconds, 0 to disable
#endif // PATCH_MOUSE_POINTER_HIDING

#if PATCH_MOUSE_POINTER_WARPING
#if PATCH_MOUSE_POINTER_WARPING_SMOOTH
static Bool mousewarp_smoothly = True;		// may want to disable smooth warping for non-local display/manager;
#endif // PATCH_MOUSE_POINTER_WARPING_SMOOTH
static Bool mousewarp_disable = False;
#endif // PATCH_MOUSE_POINTER_WARPING

#if PATCH_FLAG_FAKEFULLSCREEN
static Bool fakefullscreen_by_default = False;
#endif // PATCH_FLAG_FAKEFULLSCREEN

#if PATCH_SHOW_DESKTOP
static char *desktopsymbol = "Desktop";			// replace the layout symbol when desktop is showing;
static Bool showdesktop = True;
#if PATCH_SHOW_DESKTOP_BUTTON
static char *showdesktop_button = "💻";
#endif // PATCH_SHOW_DESKTOP_BUTTON
#if PATCH_SHOW_DESKTOP_UNMANAGED
static Bool showdesktop_unmanaged = True;
#endif // PATCH_SHOW_DESKTOP_UNMANAGED
#if PATCH_SHOW_DESKTOP_ONLY_WHEN_ACTIVE
static Bool showdesktop_when_active	= True;
#endif // PATCH_SHOW_DESKTOP_ONLY_WHEN_ACTIVE
#if PATCH_SHOW_DESKTOP_WITH_FLOATING
static Bool showdesktop_floating = True;
#endif // PATCH_SHOW_DESKTOP_WITH_FLOATING
#endif // PATCH_SHOW_DESKTOP

#if PATCH_MOVE_FLOATING_WINDOWS
#define MOVE_FLOATING_STEP		20
#define MOVE_FLOATING_STEP_BIG	100
#endif // PATCH_MOVE_FLOATING_WINDOWS

#if PATCH_WINDOW_ICONS
#if PATCH_WINDOW_ICONS_ON_TAGS
static Bool showiconsontags = True;			// show window icons on tags;
#endif // PATCH_WINDOW_ICONS_ON_TAGS
#if PATCH_WINDOW_ICONS_DEFAULT_ICON
// define default icon for clients without icons;
static char *default_icon = "";
#if PATCH_SHOW_DESKTOP
static char *desktop_icon = "";
#endif // PATCH_SHOW_DESKTOP
#endif // PATCH_WINDOW_ICONS_DEFAULT_ICON
#endif // PATCH_WINDOW_ICONS

/* appearance */
static       unsigned int borderpx		= 3;        /* border pixel of windows */
static       unsigned int titleborderpx	= 6;        /* border pixel of WinTitle bar element when monitor is active but no client is */
static const unsigned int snap			= 32;       /* snap pixel */

#if PATCH_VANITY_GAPS
static       Bool defgaps           = True;		/* vanity gaps on/off by default */
static       unsigned int gappih    = 7;        /* horiz inner gap between windows */
static       unsigned int gappiv    = 7;        /* vert inner gap between windows */
static       unsigned int gappoh    = 8;        /* horiz outer gap between windows and screen edge */
static       unsigned int gappov    = 8;        /* vert outer gap between windows and screen edge */
static       int smartgaps          = 1;        /* 1 means no outer gap when there is only one window */
#endif // PATCH_VANITY_GAPS
#if PATCH_SYSTRAY
static                int systraypinning = -1;  /* pin systray to monitor, or -1 for sloppy systray follows selected monitor */
static       unsigned int systrayonleft  = 1;   /* 0: systray in the right corner, >0: systray on left of status text */
static       unsigned int systrayspacing = 0;   /* systray spacing */
static const int systraypinningfailfirst = 1;   /* 1: if pinning fails, display systray on the first monitor, 0: display systray on the last monitor*/
static                int showsystray    = 1;   /* 0 means no systray */
#endif // PATCH_SYSTRAY
static const Bool showbar           = True;
static        int topbar            = 1;        /* 0 means bottom bar */
#if PATCH_FOCUS_BORDER || PATCH_FOCUS_PIXEL
static       unsigned int fh        = 5;        /* focus window height */
#endif // PATCH_FOCUS_BORDER || PATCH_FOCUS_PIXEL
#if PATCH_FOCUS_BORDER
static       unsigned int fbpos		= FOCUS_BORDER_N;
#elif PATCH_FOCUS_PIXEL
static       unsigned int fppos		= FOCUS_PIXEL_SE;
#endif // PATCH_FOCUS_BORDER || PATCH_FOCUS_PIXEL
#if PATCH_WINDOW_ICONS
static       unsigned int iconsize     = 16;	// icon size;
#if PATCH_ALTTAB
static       unsigned int iconsize_big = 64;	// big icon size;
#endif // PATCH_ALTTAB
static       unsigned int iconspacing  = 5;		// space between icon and title;
#endif // PATCH_WINDOW_ICONS

#if PATCH_CLIENT_OPACITY
static         Bool opacityenabled  = True;
static       double activeopacity   = 1.0f;     /* Window opacity when it's focused (0 <= opacity <= 1) */
static       double inactiveopacity = 0.9f;     /* Window opacity when it's inactive (0 <= opacity <= 1) */
#endif // PATCH_CLIENT_OPACITY

static const int focusedontoptiled  = 0;        /* 1 means focused tile client is always shown on top of floating windows */
static       Bool viewontag         = True;     /* Switch view on tag switch */
//static const char *fonts[]          = { "MesloLGS Nerd Font Mono:size=12:style=Nomral" };
static const char *fonts[]          = { "monospace:size=10" };
//static const char *monofonts[]      = { "MesloLGS Nerd Font Mono:size=10:style=Nomral" };
static const char dmenufont[]       = "monospace:size=10";
//static       char col_grey0[]       = "#101010";
static       char col_grey1[]       = "#222222";
//static       char col_grey2[]       = "#444444";
static       char col_grey3[]       = "#bbbbbb";
//static       char col_grey4[]       = "#eeeeee";
//static       char col_cyan[]        = "#005577";
static       char col_violet[]      = "#7a0aa3";
static       char col_violet2[]     = "#b32be5";
static       char col_white[]		= "#ffffffff";
static       char col_black[]		= "#000000ff";
static       char col_gold[]		= "#e6af38";
//static       char col_bright_gold[] = "#ffd30f";
static       char col_yellow[]		= "#ffff00";
static       char col_normbg[]		= "#222222d0";
static       char col_normbdr[]		= "#000000a0";

// colours can be of the form #rgb, #rrggbb, #rrggbbaa, or X colours by name;
static char *colours[][3] = {
	/*              	fg			bg				border   */
	[SchemeNorm]    = {	col_grey3,	col_normbg,		col_normbdr	},
	[SchemeSel]     = {	col_white,	col_violet,		col_violet2	},
	#if PATCH_TWO_TONE_TITLE
	[SchemeSel2]    = {	col_white,	col_normbg,		col_violet2	},
	#endif // PATCH_TWO_TONE_TITLE
	[SchemeTabNorm] = {	col_grey3,	col_grey1,		"#000000d0"	},
	[SchemeTabSel]  = {	col_white,	col_violet,		col_violet	},
	[SchemeTabUrg]  = {	col_grey1,	col_gold,		col_gold	},
	[SchemeUrg]     = {	col_grey1,	"#e6af38d0",	col_gold	},
	#if PATCH_FLAG_HIDDEN || PATCH_SHOW_DESKTOP
	[SchemeHide]    = {	col_white,	"#777777d0",	"#a0ffa080"	},
	#endif // PATCH_FLAG_HIDDEN || PATCH_SHOW_DESKTOP
	#if PATCH_FLAG_HIDDEN
	[SchemeTabHide] = {	col_white,	"#777777",		"#a0ffa0ff"	},
	#endif // PATCH_FLAG_HIDDEN
	#if PATCH_TORCH
	[SchemeTorch]   = {	col_black,	col_yellow,		col_yellow	},
	#endif // PATCH_TORCH
	#if PATCH_COLOUR_BAR
	[SchemeTagBar]	= { col_grey3,	col_normbg,		col_normbdr },
	[SchemeTagBarSel]={ NULL, NULL, NULL },
	[SchemeLayout]  = {	NULL, NULL, NULL },
	[SchemeTitle]	= { col_grey3,	col_normbg,		col_normbdr },
	[SchemeTitleSel]= { NULL, NULL, NULL },
	[SchemeStatus]	= { col_grey3,	col_normbg,		col_normbdr },
	#endif // PATCH_COLOUR_BAR
	#if PATCH_RAINBOW_TAGS
	// Border not used for rainbow tags;
	[SchemeTag1]	= {	col_white,	"#ff0000",		col_black	},
	[SchemeTag2]	= {	col_black,	"#ff8000",		col_black	},
	[SchemeTag3]	= {	col_black,	"#ffe020",		col_black	},
	[SchemeTag4]	= {	col_black,	"#40a020",		col_black	},
	[SchemeTag5]	= {	col_black,	"#00ff40",		col_black	},
	[SchemeTag6]	= {	col_white,	"#4040ff",		col_black	},
	[SchemeTag7]	= {	col_white,	"#2020a0",		col_black	},
	[SchemeTag8]	= {	col_white,	"#800080",		col_black	},
	[SchemeTag9]	= {	col_black,	"#c020c0",		col_black	},
	#endif // PATCH_RAINBOW_TAGS
	#if PATCH_STATUSCMD
	#if PATCH_STATUSCMD_COLOURS
	[SchemeStatC1]	= { "#e62222",	col_normbg,		col_normbdr },
	[SchemeStatC2]	= { "#731111",	col_normbg,		col_normbdr },
	[SchemeStatC3]	= { "#ff8000",	col_normbg,		col_normbdr },
	[SchemeStatC4]	= { "#a06000",	col_normbg,		col_normbdr },
	[SchemeStatC5]	= { "#cccc1d",	col_normbg,		col_normbdr },
	[SchemeStatC6]	= { "#8f8f15",	col_normbg,		col_normbdr },
	[SchemeStatC7]	= { "#32b699",	col_normbg,		col_normbdr },
	[SchemeStatC8]	= { "#24846f",	col_normbg,		col_normbdr },
	[SchemeStatC9]	= { "#e3ae20",	col_normbg,		col_normbdr },
	[SchemeStatC10]	= { "#a5e12e",	col_normbg,		col_normbdr },
	[SchemeStatC11]	= { "#e1e12e",	col_normbg,		col_normbdr },
	[SchemeStatC12]	= { "#8099fe",	col_normbg,		col_normbdr },
	[SchemeStatC13]	= { "#ad80fe",	col_normbg,		col_normbdr },
	[SchemeStatC14]	= { "#66d8ee",	col_normbg,		col_normbdr },
	[SchemeStatC15]	= { "#f7f7f1",	col_normbg,		col_normbdr },
	[SchemeStatusCmd] = { col_grey3, col_normbg,	col_normbdr	},
	#endif // PATCH_STATUSCMD_COLOURS
	#if PATCH_STATUSCMD_NONPRINTING
	[SchemeStatCNP] = { col_normbg, col_normbg,		col_normbdr	},
	#endif // PATCH_STATUSCMD_NONPRINTING
	#endif // PATCH_STATUSCMD
};

/* tagging */
static char *tags[] = { "1", "2", "3", "4", "5", "6", "7", "8", "9" };
//static char *tags[] = { "󾠮", "󾠯", "󾠰", "󾠱", "󾠲", "󾠳", "󾠴", "󾠵", "󾠶" };

#if PATCH_SHOW_MASTER_CLIENT_ON_TAG
static int showmaster = 1;				// show master client on tag bar;
static char *ptagf = "[%s %s]";	// format of a tag label
static char *etagf = "[%s]";		// format of an empty tag
static int lcaselbl = 1;				// 1 means make tag label lowercase
static int reverselbl = 0;				// 1 means reverse the order for ptagf;
#endif // PATCH_SHOW_MASTER_CLIENT_ON_TAG

/* layout(s) */
static const float mfact     = 0.65; /* factor of master area size [0.05..0.95] */
static const int nmaster     = 1;    /* number of clients in master area */
static const int resizehints = 0;    /* 1 means respect size hints in tiled resizals */

#define FORCE_VSPLIT 1  /* nrowgrid layout: force two clients to always split vertically */
#include "vanitygaps.h"

static unsigned int title_align = 0;	// active client title alignement: 0=left, 1=centre, 2=right;
static unsigned int barlayout[] = {
	TagBar,
	LtSymbol,
	WinTitle,
	StatusText,
	#if PATCH_SHOW_DESKTOP
	#if PATCH_SHOW_DESKTOP_BUTTON
	ShowDesktop,
	#endif // PATCH_SHOW_DESKTOP_BUTTON
	#endif // PATCH_SHOW_DESKTOP
};

static const Layout layouts[] = {
	/* symbol     arrange function */
	{ "[]=",      tile },    /* first entry is default */
	{ "[M]",      monocle },
	#if PATCH_LAYOUT_SPIRAL
	{ "[@]",      spiral },
	#endif // PATCH_LAYOUT_SPIRAL
	#if PATCH_LAYOUT_DWINDLE
	{ "[\\]",     dwindle },
	#endif // PATCH_LAYOUT_DWINDLE
	#if PATCH_LAYOUT_DECK
	{ "D[]",      deck },
	#endif // PATCH_LAYOUT_DECK
	#if PATCH_LAYOUT_BSTACK
	{ "TTT",      bstack },
	#endif // PATCH_LAYOUT_BSTACK
	#if PATCH_LAYOUT_BSTACKHORIZ
	{ "===",      bstackhoriz },
	#endif // PATCH_LAYOUT_BSTACKHORIZ
	#if PATCH_LAYOUT_GRID
	{ "HHH",      grid },
	#endif // PATCH_LAYOUT_GRID
	#if PATCH_LAYOUT_NROWGRID
	{ "###",      nrowgrid },
	#endif // PATCH_LAYOUT_NROWGRID
	#if PATCH_LAYOUT_HORIZGRID
	{ "---",      horizgrid },
	#endif // PATCH_LAYOUT_HORIZGRID
	#if PATCH_LAYOUT_GAPLESSGRID
	{ ":::",      gaplessgrid },
	#endif // PATCH_LAYOUT_GAPLESSGRID
	#if PATCH_LAYOUT_CENTREDMASTER
	{ "|M|",      centredmaster },
	#endif // PATCH_LAYOUT_CENTREDMASTER
	#if PATCH_LAYOUT_CENTREDFLOATINGMASTER
	{ ">M>",      centredfloatingmaster },
	#endif // PATCH_LAYOUT_CENTREDFLOATINGMASTER
	{ "><>",      NULL },    /* no layout function means floating behavior */
	{ NULL,       NULL },
};

#if PATCH_IPC
//static const char *socketpath = "${XDG_RUNTIME_DIR}/dwm/dwm.sock";	// sometimes we can't expand this - possibly not getting environment every time;
static const char *socketpath = "/tmp/dwm.sock";
static IPCCommand ipccommands[] = {
	IPCCOMMAND(  activate,            1,      {ARG_TYPE_STR}    ),
	IPCCOMMAND(  clearurgency,        1,      {ARG_TYPE_NONE}   ),
	#if PATCH_TERMINAL_SWALLOWING
	IPCCOMMAND(  enabletermswallow,   1,      {ARG_TYPE_UINT}   ),
	#endif // PATCH_TERMINAL_SWALLOWING
	IPCCOMMAND(  enableurgency,       1,      {ARG_TYPE_UINT}   ),
	IPCCOMMAND(  focusmon,            1,      {ARG_TYPE_SINT}   ),
	IPCCOMMAND(  focusstack,          1,      {ARG_TYPE_SINT}   ),
	IPCCOMMAND(  incnmaster,          1,      {ARG_TYPE_SINT}   ),
	IPCCOMMAND(  killclient,          1,      {ARG_TYPE_SINT}   ),
	#if PATCH_LOG_DIAGNOSTICS
	IPCCOMMAND(  logdiagnostics,      1,      {ARG_TYPE_UINT}   ),
	#endif // PATCH_LOG_DIAGNOSTICS
	IPCCOMMAND(  reload,              1,      {ARG_TYPE_NONE}   ),
//	IPCCOMMAND(  setlayoutsafe,       1,      {ARG_TYPE_PTR}    ),
	IPCCOMMAND(  setmfact,            1,      {ARG_TYPE_FLOAT}  ),
	IPCCOMMAND(  tag,                 1,      {ARG_TYPE_UINT}   ),
	IPCCOMMAND(  tagmon,              1,      {ARG_TYPE_UINT}   ),
	IPCCOMMAND(  togglefloating,      1,      {ARG_TYPE_NONE}   ),
	IPCCOMMAND(  toggletag,           1,      {ARG_TYPE_UINT}   ),
	IPCCOMMAND(  toggleview,          1,      {ARG_TYPE_UINT}   ),
	IPCCOMMAND(  view,                1,      {ARG_TYPE_UINT}   ),
	IPCCOMMAND(  quit,                1,      {ARG_TYPE_NONE}   ),
	IPCCOMMAND(  zoom,                1,      {ARG_TYPE_NONE}   ),
};
#endif // PATCH_IPC

/* key definitions */
#define MODKEY Mod4Mask
//#define MODKEY Mod1Mask
#define TAGKEYS(KEY,TAG) \
	{ MODKEY,                       KEY,      view,           {.ui = 1 << TAG} }, \
	{ MODKEY|ControlMask,           KEY,      toggleview,     {.ui = 1 << TAG} }, \
	{ MODKEY|ShiftMask,             KEY,      tag,            {.ui = 1 << TAG} }, \
	{ MODKEY|ControlMask|ShiftMask, KEY,      toggletag,      {.ui = 1 << TAG} },

/* helper for spawning shell commands in the pre dwm-5.0 fashion */
#define SHCMD(cmd) { .v = (const char*[]){ "/bin/bash", "-c", cmd, NULL } }

#if PATCH_STATUSCMD
#define STATUSBAR "dwmblocks"
#endif // PATCH_STATUSCMD

/* commands */
static char dmenumon[2] = "0"; /* component of dmenucmd, manipulated in spawn() */
static const char *dmenucmd[] = { "dmenu_run", "-m", dmenumon, "-fn", dmenufont, "-nb", col_grey1, "-nf", col_grey3, "-sb", col_violet, "-sf", col_white, NULL };
//static const char *termcmd[]  = { "konsole", NULL };

static const Key keys[] = {
	/* modifier                     key        function        argument */
	{ MODKEY,                       XK_r,      spawn,          SHCMD("rofi -show run -show-icons -modi drun,run,window >/dev/null 2>&1")},
	{ MODKEY|ShiftMask,             XK_r,      spawn,          SHCMD("rofi -show drun -show-icons -modi drun,run,window >/dev/null 2>&1")},
	{ Mod1Mask,                     XK_F1,     spawn,          SHCMD("rofi -show drun -show-icons -modi drun,run,window >/dev/null 2>&1")},
	{ Mod1Mask,                     XK_F2,     spawn,          SHCMD("rofi -show run -show-icons -modi drun,run,window >/dev/null 2>&1")},
	{ MODKEY,                       XK_e,      spawn,          SHCMD("thunar >/dev/null 2>&1")},
	{ MODKEY,                       XK_v,      spawn,          SHCMD("if ! pidof pavucontrol >/dev/null; then exec pavucontrol --tab=3 >/dev/null 2>&1; fi")},
	{ MODKEY,                       XK_v,      activate,       { .v = "Volume Control" } },
	{ MODKEY|ShiftMask,             XK_v,      spawn,          SHCMD("bash $XDG_RUNTIME_DIR/dwm/select-audio-output.sh -1")},
	{ MODKEY|ControlMask,           XK_v,      spawn,          SHCMD("bash $XDG_RUNTIME_DIR/dwm/select-audio-output.sh -2")},
	{ MODKEY,                       XK_t,      spawn,          SHCMD("konsole >/dev/null 2>&1")},
	{ MODKEY|ControlMask|ShiftMask, XK_t,      spawn,          SHCMD("bash $XDG_RUNTIME_DIR/dwm/xprop.sh")},
	{ MODKEY,                       XK_b,      togglebar,      {0} },
	{ MODKEY|ShiftMask,             XK_x,      spawn,          SHCMD("firefox >/dev/null 2>&1")},
	{ MODKEY|ShiftMask|ControlMask, XK_x,      spawn,          SHCMD("firefox --private-window >/dev/null 2>&1")},
	{ MODKEY|ShiftMask,             XK_c,      spawn,          SHCMD("thorium-browser >/dev/null 2>&1")},
	{ MODKEY|ShiftMask|ControlMask, XK_c,      spawn,          SHCMD("thorium-browser --incognito >/dev/null 2>&1")},
	{ MODKEY|ShiftMask,             XK_b,      spawn,          SHCMD("/opt/brave.com/brave/brave-browser >/dev/null 2>&1")},
	{ MODKEY|ShiftMask|ControlMask, XK_b,      spawn,          SHCMD("/opt/brave.com/brave/brave-browser --incognito --tor >/dev/null 2>&1")},
	{ 0,                            0x1008ff11, spawn,         SHCMD ("pamixer -d 5 >/dev/null 2>&1; pkill -RTMIN+8 dwmblocks")},
	{ 0,                            0x1008ff12, spawn,         SHCMD ("pamixer --toggle-mute >/dev/null 2>&1; pkill -RTMIN+8 dwmblocks")},
	{ 0,                            0x1008ff13, spawn,         SHCMD ("pamixer -i 5 >/dev/null 2>&1; pkill -RTMIN+8 dwmblocks")},
	{ 0,                            XK_Print,  spawn,          SHCMD ("flameshot gui")},
	{ ShiftMask,                    XK_Print,  spawn,          SHCMD ("scrot -M0 -d1 ~/Pictures/Screenshots/%Y-%m-%d-%T-screenshot.png")},
	{ ShiftMask|ControlMask,        XK_Print,  spawn,          SHCMD ("scrot -M0 -d1 ~/Pictures/Screenshots/%Y-%m-%d-%T-screenshot.png -e 'xdg-open \"$f\"'")},
	{ MODKEY,                       XK_j,      focusstack,     {.i = +1 } },
	{ MODKEY,                       XK_k,      focusstack,     {.i = -1 } },
	{ Mod1Mask,                     XK_Escape, focusstack,     {.i = +1 } },
	{ Mod1Mask|ShiftMask,           XK_Escape, focusstack,     {.i = -1 } },
	{ MODKEY,                       XK_i,      incnmaster,     {.i = +1 } },
	{ MODKEY,                       XK_d,      incnmaster,     {.i = -1 } },
#if PATCH_SHOW_DESKTOP
	{ MODKEY|ShiftMask,             XK_d,      toggledesktop,  {.i = -1 } },
#endif // PATCH_SHOW_DESKTOP
	{ MODKEY,                       XK_h,      setmfact,       {.f = -0.05} },
	{ MODKEY|ControlMask,           XK_h,      setmfact,       {.f = -0.01} },
	{ MODKEY,                       XK_l,      setmfact,       {.f = +0.05} },
	{ MODKEY|ControlMask,           XK_l,      setmfact,       {.f = +0.01} },
	{ MODKEY|ControlMask,           XK_j,      setmfact,       {.f =  0.00} },
#if PATCH_CFACTS
	{ MODKEY|ShiftMask|ControlMask, XK_h,      setcfact,       {.f = -0.25} },
	{ MODKEY|ShiftMask|ControlMask, XK_l,      setcfact,       {.f = +0.25} },
	{ MODKEY|ShiftMask|ControlMask, XK_j,      setcfact,       {.f =  0.00} },
#endif // PATCH_CFACTS
	{ MODKEY,                       XK_Return, zoom,           {0} },
	{ MODKEY,                       XK_KP_Enter,zoom,          {0} },
	{ MODKEY|ShiftMask,             XK_KP_Enter,swapmon,       {.ui = 1 } },
	{ MODKEY|ShiftMask,             XK_KP_Subtract,viewactiveprev, {.ui = 1 } },
	{ MODKEY|ShiftMask,             XK_KP_Add, viewactivenext, {.ui = 1 } },
	{ MODKEY,						XK_Tab,	   view,           {0} },
	{ MODKEY,                       XK_q,      killclient,     {0} },
	{ Mod1Mask,						XK_F4,     killclient,     {0} },
	{ MODKEY|ShiftMask,				XK_t,      setlayout,      {.v = "[]="} },
#if PATCH_LAYOUT_BSTACKHORIZ
	{ MODKEY|ShiftMask,				XK_h,      setlayout,      {.v = "==="} },
#endif // PATCH_LAYOUT_BSTACKHORIZ
	{ MODKEY|ShiftMask,				XK_m,      setlayout,      {.v = "[M]"} },
	{ MODKEY,                       XK_space,  setlayout,      {0} },
	{ MODKEY|ShiftMask,             XK_space,  togglefloating, {0} },
	{ MODKEY,                       XK_Left,   focusmon,       {.i = -1 } },
	{ MODKEY,                       XK_Right,  focusmon,       {.i = +1 } },
	{ MODKEY|ShiftMask,             XK_Escape, focusmon,       {.i = -1 } },
	{ MODKEY,                       XK_Escape, focusmon,       {.i = +1 } },
	{ MODKEY|ShiftMask,             XK_Left,   tagmon,         {.i = -1 } },
	{ MODKEY|ShiftMask,             XK_Right,  tagmon,         {.i = +1 } },
	TAGKEYS(                        XK_1,                      0)
	TAGKEYS(                        XK_2,                      1)
	TAGKEYS(                        XK_3,                      2)
	TAGKEYS(                        XK_4,                      3)
	TAGKEYS(                        XK_5,                      4)
	TAGKEYS(                        XK_6,                      5)
	TAGKEYS(                        XK_7,                      6)
	TAGKEYS(                        XK_8,                      7)
	TAGKEYS(                        XK_9,                      8)
	#if DEBUGGING
	{ MODKEY|ShiftMask|ControlMask, XK_a,      toggledebug,    {0} },
	#endif // DEBUGGING
	{ MODKEY|ShiftMask|ControlMask, XK_q,      quit,           {0} },
	{ MODKEY|ShiftMask|ControlMask|Mod1Mask, XK_m,    spawn,   SHCMD("bash $XDG_RUNTIME_DIR/dwm/pactl-mute-audio.sh -r")},
	{ MODKEY|ShiftMask|ControlMask|Mod1Mask, XK_l,    spawn,   SHCMD("touch $XDG_RUNTIME_DIR/dwm/dwm.quit; kill $(pidof dwm.running)")},
	{ MODKEY|ShiftMask|ControlMask|Mod1Mask, XK_Home, spawn,   SHCMD("touch $XDG_RUNTIME_DIR/dwm/dwm.restart; kill $(pidof dwm.running);")},
	{ MODKEY|ShiftMask|ControlMask|Mod1Mask, XK_End,  spawn,   SHCMD("touch $XDG_RUNTIME_DIR/dwm/dwm.shutdown; kill $(pidof dwm.running);")},
	{ MODKEY|ShiftMask|ControlMask, XK_Escape, spawn,          SHCMD("xkill >/dev/null 2>&1")},
	{ MODKEY|ShiftMask|ControlMask|Mod1Mask, XK_Escape, reload,{0} },
/* extra functionality from patches */
#if PATCH_ALT_TAGS
	{ 0,                            XK_Super_L,togglealttags,  {0} },
#endif // PATCH_ALT_TAGS
#if PATCH_MOUSE_POINTER_WARPING
	{ Mod4Mask,                     XK_Alt_L,  refocuspointer, {0} },
#endif // PATCH_MOUSE_POINTER_WARPING
#if PATCH_EXTERNAL_WINDOW_ACTIVATION
	{ Mod1Mask,                     XK_space,  window_switcher,SHCMD("rofi -show window >/dev/null 2>&1")},
#endif // PATCH_EXTERNAL_WINDOW_ACTIVATION
	{ MODKEY|ControlMask|ShiftMask, XK_p,      spawn,          SHCMD("bash $XDG_RUNTIME_DIR/dwm/toggle-pink-noise.sh")},
	{ MODKEY|ControlMask|ShiftMask|Mod1Mask, XK_t, spawn,      SHCMD("bash $XDG_RUNTIME_DIR/dwm/toggle-30s-tone.sh")},
	{ MODKEY|ControlMask|ShiftMask, XK_s,      rescan,         {0} },
#if DEBUGGING
	{ MODKEY|ControlMask|ShiftMask|Mod1Mask, XK_s, toggleskiprules, {0} },
#endif // DEBUGGING
	{ MODKEY|ControlMask|ShiftMask, XK_k,      spawn,          SHCMD("bash $XDG_RUNTIME_DIR/dwm/dwm-log.sh restart")},
	{ MODKEY|ControlMask|ShiftMask, XK_d,      spawn,          SHCMD("bash $XDG_RUNTIME_DIR/dwm/dwm-log.sh")},
#if PATCH_LOG_DIAGNOSTICS
	{ MODKEY|ControlMask|ShiftMask, XK_d,      logdiagnostics, {0} },
	{ MODKEY|ControlMask|ShiftMask|Mod1Mask, XK_d, spawn,          SHCMD("bash $XDG_RUNTIME_DIR/dwm/dwm-log.sh")},
	{ MODKEY|ControlMask|ShiftMask|Mod1Mask, XK_d, logdiagnostics, {.ui = 1} },
#endif // PATCH_LOG_DIAGNOSTICS
	{ MODKEY|ControlMask|ShiftMask, XK_r,      logrules,       {0} },		// for debugging
	{ MODKEY|ControlMask|ShiftMask|Mod1Mask, XK_r, logrules,   {.ui = 1} },	// for debugging
	{ MODKEY|ShiftMask,             XK_q,      killgroup,      {.ui = (KILLGROUP_BY_CLASS | KILLGROUP_BY_INSTANCE) } },
	{ MODKEY|ControlMask,           XK_Tab,    viewactive,     {.i = +1 } },
	{ MODKEY|ControlMask|ShiftMask, XK_Tab,    viewactive,     {.i = -1 } },
#if PATCH_CONSTRAIN_MOUSE
	{ MODKEY|ControlMask|ShiftMask, XK_m,      toggleconstrain, {0} },
#endif // PATCH_CONSTRAIN_MOUSE
#if PATCH_FLAG_GAME
	{ MODKEY|ControlMask|ShiftMask, XK_g,      toggleisgame,   {0} },
#endif // PATCH_FLAG_GAME
	{ MODKEY,                       XK_u,      clearurgency,   {0} },
#if PATCH_CLIENT_OPACITY
	{ MODKEY,                       XK_equal,  changefocusopacity,   {.f = +0.025}},
	{ MODKEY,                       XK_minus,  changefocusopacity,   {.f = -0.025}},
	{ MODKEY|ShiftMask,             XK_equal,  changeunfocusopacity, {.f = +0.025}},
	{ MODKEY|ShiftMask,             XK_minus,  changeunfocusopacity, {.f = -0.025}},
#endif // PATCH_CLIENT_OPACITY
#if PATCH_VANITY_GAPS
	{ MODKEY|ControlMask,           XK_u,      incrgaps,       {.i = +1 } },
	{ MODKEY|ControlMask|ShiftMask, XK_u,      incrgaps,       {.i = -1 } },
	{ MODKEY|ControlMask,           XK_i,      incrigaps,      {.i = +1 } },
	{ MODKEY|ControlMask|ShiftMask, XK_i,      incrigaps,      {.i = -1 } },
	{ MODKEY|ControlMask,           XK_o,      incrogaps,      {.i = +1 } },
	{ MODKEY|ControlMask|ShiftMask, XK_o,      incrogaps,      {.i = -1 } },
	{ MODKEY|ControlMask,           XK_6,      incrihgaps,     {.i = +1 } },
	{ MODKEY|ControlMask|ShiftMask, XK_6,      incrihgaps,     {.i = -1 } },
	{ MODKEY|ControlMask,           XK_7,      incrivgaps,     {.i = +1 } },
	{ MODKEY|ControlMask|ShiftMask, XK_7,      incrivgaps,     {.i = -1 } },
	{ MODKEY|ControlMask,           XK_8,      incrohgaps,     {.i = +1 } },
	{ MODKEY|ControlMask|ShiftMask, XK_8,      incrohgaps,     {.i = -1 } },
	{ MODKEY|ControlMask,           XK_9,      incrovgaps,     {.i = +1 } },
	{ MODKEY|ControlMask|ShiftMask, XK_9,      incrovgaps,     {.i = -1 } },
	{ MODKEY|ControlMask,           XK_0,      togglegaps,     {0} },
	{ MODKEY|ControlMask|ShiftMask, XK_0,      defaultgaps,    {0} },
#endif // PATCH_VANITY_GAPS
	{ MODKEY,		                XK_f,      togglefullscreen, {0} },
#if PATCH_FLAG_FAKEFULLSCREEN
	{ MODKEY|ShiftMask,             XK_f,      togglefakefullscreen, {0} },
#endif // PATCH_FLAG_FAKEFULLSCREEN
	{ MODKEY|ControlMask,			XK_comma,  cyclelayout,    {.i = -1 } },
	{ MODKEY|ControlMask,           XK_period, cyclelayout,    {.i = +1 } },
#if PATCH_MIRROR_LAYOUT
	{ MODKEY,                   XK_KP_Subtract,togglemirror,   {0} },
#endif // PATCH_MIRROR_LAYOUT
#if PATCH_MOVE_TILED_WINDOWS
	{ MODKEY,                      XK_Up,      movetiled,      {.i = -1 } },
	{ MODKEY,                      XK_KP_Up,   movetiled,      {.i = -1 } },
	{ MODKEY,                      XK_Down,    movetiled,      {.i = +1 } },
	{ MODKEY,                      XK_KP_Down, movetiled,      {.i = +1 } },
#endif // PATCH_MOVE_TILED_WINDOWS
#if PATCH_MOVE_FLOATING_WINDOWS
	{ MODKEY,		           XK_KP_Left,     movefloat,      {.ui = MOVE_FLOATING_LEFT } },
	{ MODKEY|ShiftMask,	       XK_KP_Left,     movefloat,      {.ui = MOVE_FLOATING_LEFT | MOVE_FLOATING_BIGGER } },
	{ MODKEY,		           XK_KP_Right,    movefloat,      {.ui = MOVE_FLOATING_RIGHT } },
	{ MODKEY|ShiftMask,        XK_KP_Right,    movefloat,      {.ui = MOVE_FLOATING_RIGHT | MOVE_FLOATING_BIGGER } },
	{ MODKEY,		           XK_KP_Up,       movefloat,      {.ui = MOVE_FLOATING_UP } },
	{ MODKEY|ShiftMask,        XK_KP_Up,       movefloat,      {.ui = MOVE_FLOATING_UP | MOVE_FLOATING_BIGGER } },
	{ MODKEY,		           XK_KP_Down,     movefloat,      {.ui = MOVE_FLOATING_DOWN } },
	{ MODKEY|ShiftMask,        XK_KP_Down,     movefloat,      {.ui = MOVE_FLOATING_DOWN | MOVE_FLOATING_BIGGER } },
	{ MODKEY,		           XK_KP_Home,     movefloat,      {.ui = MOVE_FLOATING_LEFT | MOVE_FLOATING_UP } },
	{ MODKEY|ShiftMask,        XK_KP_Home,     movefloat,      {.ui = MOVE_FLOATING_LEFT | MOVE_FLOATING_UP | MOVE_FLOATING_BIGGER } },
	{ MODKEY,		           XK_KP_End,      movefloat,      {.ui = MOVE_FLOATING_LEFT | MOVE_FLOATING_DOWN } },
	{ MODKEY|ShiftMask,        XK_KP_End,      movefloat,      {.ui = MOVE_FLOATING_LEFT | MOVE_FLOATING_DOWN | MOVE_FLOATING_BIGGER } },
	{ MODKEY,	               XK_KP_Page_Up,  movefloat,      {.ui = MOVE_FLOATING_UP | MOVE_FLOATING_RIGHT } },
	{ MODKEY|ShiftMask,        XK_KP_Page_Up,  movefloat,      {.ui = MOVE_FLOATING_UP | MOVE_FLOATING_RIGHT | MOVE_FLOATING_BIGGER } },
	{ MODKEY,		           XK_KP_Page_Down,movefloat,      {.ui = MOVE_FLOATING_RIGHT | MOVE_FLOATING_DOWN } },
	{ MODKEY|ShiftMask,        XK_KP_Page_Down,movefloat,      {.ui = MOVE_FLOATING_RIGHT | MOVE_FLOATING_DOWN | MOVE_FLOATING_BIGGER } },
#endif // PATCH_MOVE_FLOATING_WINDOWS
#if PATCH_PAUSE_PROCESS
	{ MODKEY,                       XK_Pause,  togglepause,    {0} },
#endif // PATCH_PAUSE_PROCESS
#if PATCH_FLAG_STICKY
	{ MODKEY,                       XK_s,      togglesticky,   {0} },
#endif // PATCH_FLAG_STICKY
#if PATCH_FLAG_ALWAYSONTOP
	{ MODKEY,                       XK_a,      togglealwaysontop, {0} },
#endif // PATCH_FLAG_ALWAYSONTOP
	{ MODKEY,                       XK_0,      view,           {.ui = ~0 } },
	{ MODKEY|ShiftMask,             XK_0,      tag,            {.ui = ~0 } },
#if PATCH_FLAG_HIDDEN
	{ MODKEY,                      XK_Scroll_Lock,hidewin,     {.ui = 0 } },
	{ MODKEY|ShiftMask,            XK_Scroll_Lock,hidewin,     {.ui = 1 } },
	{ MODKEY|ControlMask|ShiftMask,XK_Scroll_Lock,unhidewin,   {0} },
#endif // PATCH_FLAG_HIDDEN
#if PATCH_ALTTAB
	{ Mod1Mask,             		XK_Tab,    altTabStart,	   {.ui = (ALTTAB_SELMON_MASK | ALTTAB_NORMAL) } },
	{ Mod1Mask|ShiftMask,      		XK_Tab,    altTabStart,	   {.ui = (ALTTAB_SELMON_MASK | ALTTAB_NORMAL | ALTTAB_REVERSE) } },
	{ MODKEY|Mod1Mask,             	XK_Tab,    altTabStart,	   {.ui = (ALTTAB_SELMON_MASK | ALTTAB_ALL_TAGS) } },
	{ MODKEY|Mod1Mask|ShiftMask,    XK_Tab,    altTabStart,	   {.ui = (ALTTAB_SELMON_MASK | ALTTAB_ALL_TAGS | ALTTAB_REVERSE) } },
	{ Mod1Mask|ControlMask,         XK_Tab,    altTabStart,	   {.ui = (ALTTAB_SELMON_MASK | ALTTAB_ALL_TAGS | ALTTAB_ALL_MONITORS) } },
	{ Mod1Mask|ControlMask|ShiftMask,XK_Tab,   altTabStart,    {.ui = (ALTTAB_SELMON_MASK | ALTTAB_ALL_TAGS | ALTTAB_ALL_MONITORS | ALTTAB_REVERSE) } },
	{ Mod1Mask,                     XK_grave,  altTabStart,	   {.ui = (ALTTAB_SELMON_MASK | ALTTAB_SAME_CLASS) } },
	{ Mod1Mask|ShiftMask,      	    XK_grave,  altTabStart,	   {.ui = (ALTTAB_SELMON_MASK | ALTTAB_SAME_CLASS | ALTTAB_REVERSE) } },
	{ MODKEY|Mod1Mask,             	XK_grave,  altTabStart,	   {.ui = (ALTTAB_SELMON_MASK | ALTTAB_SAME_CLASS | ALTTAB_ALL_TAGS) } },
	{ MODKEY|Mod1Mask|ShiftMask,    XK_grave,  altTabStart,	   {.ui = (ALTTAB_SELMON_MASK | ALTTAB_SAME_CLASS | ALTTAB_ALL_TAGS | ALTTAB_REVERSE) } },
	{ Mod1Mask|ControlMask,         XK_grave,  altTabStart,	   {.ui = (ALTTAB_SELMON_MASK | ALTTAB_SAME_CLASS | ALTTAB_ALL_TAGS | ALTTAB_ALL_MONITORS) } },
	{ Mod1Mask|ControlMask|ShiftMask,XK_grave, altTabStart,    {.ui = (ALTTAB_SELMON_MASK | ALTTAB_SAME_CLASS | ALTTAB_ALL_TAGS | ALTTAB_ALL_MONITORS | ALTTAB_REVERSE) } },
	{ MODKEY|ControlMask|Mod1Mask,  XK_Tab,    altTabStart,	   {.ui = (1 | ALTTAB_ALL_TAGS) } },
#endif // PATCH_ALTTAB
#if PATCH_TORCH
	{ MODKEY,                       XK_grave,  toggletorch,    {.ui = 0 } },
	{ MODKEY|ShiftMask,             XK_grave,  toggletorch,    {.ui = 1 } },
#endif // PATCH_TORCH
};

/* button definitions */
/* click can be ClkTagBar, ClkLtSymbol, ClkStatusText, ClkWinTitle, ClkClientWin, or ClkRootWin */
static const Button buttons[] = {
	/* click                event mask      button          function        argument */
	{ ClkTagBar,            MODKEY,         Button1,        tag,            {0} },
	{ ClkTagBar,            MODKEY,         Button3,        toggletag,      {0} },
	{ ClkTagBar,            0,              Button4,        mouseview,      {.i = -1 *2 } }, /* being -2 indicates mouse invocation */
	{ ClkTagBar,            0,              Button5,        mouseview,      {.i = +1 *2 } }, /* being +2 indicates mouse invocation */
	{ ClkLtSymbol,          0,              Button4,       cyclelayoutmouse,{.i = -1 } },
	{ ClkLtSymbol,          0,              Button5,       cyclelayoutmouse,{.i = +1 } },
	{ ClkLtSymbol,          0,              Button1,        setlayoutmouse, {0} },
	{ ClkLtSymbol,          0,              Button3,        setlayoutmouse, {.v = "[M]"} },
	{ ClkWinTitle,          0,              Button2,        zoom,           {0} },
	{ ClkWinTitle,          MODKEY,         Button2,        killgroup,      {.ui = (KILLGROUP_BY_NAME | KILLGROUP_BY_CLASS | KILLGROUP_BY_INSTANCE) } },
#if PATCH_ALTTAB
	{ ClkWinTitle,          0,              Button1,        altTabStart,    {.ui = (ALTTAB_SELMON_MASK | ALTTAB_MOUSE) } },
	{ ClkWinTitle,          Mod1Mask,       Button1,        altTabStart,    {.ui = (ALTTAB_SELMON_MASK | ALTTAB_MOUSE | ALTTAB_SAME_CLASS ) } },
	{ ClkWinTitle,          ControlMask,    Button1,        altTabStart,    {.ui = (ALTTAB_SELMON_MASK | ALTTAB_MOUSE | ALTTAB_ALL_MONITORS ) } },
	{ ClkWinTitle, Mod1Mask|ControlMask,    Button1,        altTabStart,    {.ui = (ALTTAB_SELMON_MASK | ALTTAB_MOUSE | ALTTAB_SAME_CLASS | ALTTAB_ALL_MONITORS ) } },
	{ ClkWinTitle,          0,              Button3,        altTabStart,    {.ui = (ALTTAB_SELMON_MASK | ALTTAB_MOUSE | ALTTAB_ALL_TAGS ) } },
	{ ClkWinTitle,          Mod1Mask,       Button3,        altTabStart,    {.ui = (ALTTAB_SELMON_MASK | ALTTAB_MOUSE | ALTTAB_ALL_TAGS | ALTTAB_SAME_CLASS ) } },
	{ ClkWinTitle,          ControlMask,    Button3,        altTabStart,    {.ui = (ALTTAB_SELMON_MASK | ALTTAB_MOUSE | ALTTAB_ALL_TAGS | ALTTAB_ALL_MONITORS ) } },
	{ ClkWinTitle, Mod1Mask|ControlMask,    Button3,        altTabStart,    {.ui = (ALTTAB_SELMON_MASK | ALTTAB_MOUSE | ALTTAB_ALL_TAGS | ALTTAB_SAME_CLASS | ALTTAB_ALL_MONITORS ) } },
#if PATCH_FLAG_HIDDEN
	{ ClkWinTitle,          ShiftMask,      Button1,        altTabStart,    {.ui = (ALTTAB_SELMON_MASK | ALTTAB_MOUSE | ALTTAB_HIDDEN) } },
	{ ClkWinTitle, Mod1Mask|ShiftMask,      Button1,        altTabStart,    {.ui = (ALTTAB_SELMON_MASK | ALTTAB_MOUSE | ALTTAB_SAME_CLASS | ALTTAB_HIDDEN ) } },
	{ ClkWinTitle, ShiftMask|ControlMask,   Button1,        altTabStart,    {.ui = (ALTTAB_SELMON_MASK | ALTTAB_MOUSE | ALTTAB_ALL_MONITORS | ALTTAB_HIDDEN ) } },
	{ ClkWinTitle, Mod1Mask|ControlMask|ShiftMask,Button1,  altTabStart,    {.ui = (ALTTAB_SELMON_MASK | ALTTAB_MOUSE | ALTTAB_SAME_CLASS | ALTTAB_ALL_MONITORS | ALTTAB_HIDDEN ) } },
	{ ClkWinTitle,          ShiftMask,      Button3,        altTabStart,    {.ui = (ALTTAB_SELMON_MASK | ALTTAB_MOUSE | ALTTAB_ALL_TAGS | ALTTAB_HIDDEN ) } },
	{ ClkWinTitle, Mod1Mask|ShiftMask,      Button3,        altTabStart,    {.ui = (ALTTAB_SELMON_MASK | ALTTAB_MOUSE | ALTTAB_ALL_TAGS | ALTTAB_SAME_CLASS | ALTTAB_HIDDEN ) } },
	{ ClkWinTitle, ShiftMask|ControlMask,   Button3,        altTabStart,    {.ui = (ALTTAB_SELMON_MASK | ALTTAB_MOUSE | ALTTAB_ALL_TAGS | ALTTAB_ALL_MONITORS | ALTTAB_HIDDEN ) } },
	{ ClkWinTitle, Mod1Mask|ControlMask|ShiftMask,Button3,  altTabStart,    {.ui = (ALTTAB_SELMON_MASK | ALTTAB_MOUSE | ALTTAB_ALL_TAGS | ALTTAB_SAME_CLASS | ALTTAB_ALL_MONITORS | ALTTAB_HIDDEN ) } },
#endif // PATCH_FLAG_HIDDEN
#endif // PATCH_ALTTAB
	{ ClkWinTitle,          0,              Button4,        focusstack,     {.i = -2 } },
	{ ClkWinTitle,          0,              Button5,        focusstack,     {.i = +2 } },
#if PATCH_STATUSCMD
	{ ClkStatusText,        0,              Button1,        sigstatusbar,   {.i = 1} },
	{ ClkStatusText,        0,              Button2,        sigstatusbar,   {.i = 2} },
	{ ClkStatusText,        0,              Button3,        sigstatusbar,   {.i = 3} },
	{ ClkStatusText,        0,              Button4,        sigstatusbar,   {.i = 4} },
	{ ClkStatusText,        0,              Button5,        sigstatusbar,   {.i = 5} },
#else // NO PATCH_STATUSCMD
	{ ClkStatusText,        0,              Button2,        spawn,          SHCMD("xfce4-appfinder")},
#endif // PATCH_STATUSCMD
	/* placemouse options, choose which feels more natural:
	 *    0 - tiled position is relative to mouse cursor
	 *    1 - tiled postiion is relative to window centre
	 *    2 - mouse pointer warps to window centre
	 *
	 * The moveorplace uses movemouse or placemouse depending on the floating state
	 * of the selected client. Set up individual keybindings for the two if you want
	 * to control these separately (i.e. to retain the feature to move a tiled window
	 * into a floating position).
	 */
	{ ClkClientWin,         MODKEY,         Button1,        moveorplace,    {.i = 1} },
#if PATCH_CROP_WINDOWS
	{ ClkClientWin,     MODKEY|ControlMask, Button1,        movemouse,      {.i = 1} },
#endif // PATCH_CROP_WINDOWS
	{ ClkClientWin,         MODKEY,         Button2,        togglefloating, {0} },
#if PATCH_CFACTS
	{ ClkClientWin,       MODKEY|ShiftMask, Button2,        setcfact,       {.f =  0.00} },
#endif // PATCH_CFACTS
	{ ClkClientWin,MODKEY|ShiftMask|ControlMask,Button2,    setmfact,       {.f =  0.00} },
#if PATCH_DRAG_FACTS
	{ ClkClientWin,         MODKEY,         Button3,        resizeorfacts,  {0} },
	{ ClkClientWin,       MODKEY|ShiftMask, Button3,        resizemouse,    {0} },
#else // NO PATCH_DRAG_FACTS
	{ ClkClientWin,         MODKEY,         Button3,        resizemouse,    {0} },
#endif // PATCH_DRAG_FACTS
#if PATCH_CROP_WINDOWS
	{ ClkClientWin,     MODKEY|ControlMask, Button3,        resizemouse,    {.i = 1} },
#endif // PATCH_CROP_WINDOWS
#if PATCH_CLIENT_OPACITY
	{ ClkClientWin,         MODKEY,         Button4,    changefocusopacity, {.f = +0.025 }},
	{ ClkClientWin,         MODKEY,         Button5,    changefocusopacity, {.f = -0.025} },
	{ ClkClientWin,       MODKEY|ShiftMask, Button4,  changeunfocusopacity, {.f = +0.025} },
	{ ClkClientWin,       MODKEY|ShiftMask, Button5,  changeunfocusopacity, {.f = -0.025 }},
#endif // PATCH_CLIENT_OPACITY
	{ ClkTagBar,            0,              Button1,        view,           {0} },
	{ ClkTagBar,            0,              Button3,        toggleview,     {0} },
	{ ClkTagBar,            MODKEY,         Button1,        tag,            {0} },
	{ ClkTagBar,            MODKEY,         Button3,        toggletag,      {0} },
#if PATCH_SHOW_DESKTOP
#if PATCH_SHOW_DESKTOP_BUTTON
	{ ClkShowDesktop,       0,              Button1,        toggledesktop,  {.i = -1 } },
#endif // PATCH_SHOW_DESKTOP_BUTTON
#endif // PATCH_SHOW_DESKTOP
};
