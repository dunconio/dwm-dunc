
// Core functionality
#define PATCH_ALPHA_CHANNEL					1
#define PATCH_ALT_TAGS						1
#define PATCH_ALTTAB						1
#define PATCH_ALTTAB_HIGHLIGHT				1	// Support for highlighting clients during alt-tab
#define PATCH_ATTACH_BELOW_AND_NEWMASTER	1
#define PATCH_CFACTS						1
#define PATCH_CLIENT_INDICATORS				1
#define PATCH_CONSTRAIN_MOUSE				1	// Toggle constraining mouse pointer within monitor bounds
#define PATCH_DRAG_FACTS					1
#define PATCH_SHOW_DESKTOP					1
#define PATCH_SHOW_DESKTOP_ONLY_WHEN_ACTIVE	1	// Only enable show desktop while a desktop client is available;
#define PATCH_SHOW_DESKTOP_UNMANAGED		1	// Don't manage desktop clients;
#define PATCH_SHOW_DESKTOP_WITH_FLOATING	1	// Allow floating clients to be visible while the desktop is;
#define PATCH_EWMH_TAGS						0	// Supports reporting workspace(tag) changes
#define PATCH_EXTERNAL_WINDOW_ACTIVATION	1	// Behaves like spawn except will allow focus stealing for the life of the spawnee
#define PATCH_FOCUS_BORDER					0
#define PATCH_FOCUS_FOLLOWS_MOUSE			1
#define PATCH_HIDE_VACANT_TAGS				1
#define PATCH_IPC							1	// socket-based message handling
#define PATCH_LOG_DIAGNOSTICS				1
#define PATCH_MIRROR_LAYOUT					1	// Swap the master and stack areas;
#define PATCH_MODAL_SUPPORT					1	// Ensures floating modal child clients get focused appropriately
#define PATCH_MOUSE_POINTER_HIDING			1	// Support for hiding the mouse pointer when idle or typing
#define PATCH_MOUSE_POINTER_WARPING			1	// See mousewarp_disable in config.def.h
#define PATCH_MOUSE_POINTER_WARPING_SMOOTH	1	// enable smooth warping in some instances;
#define PATCH_MOVE_FLOATING_WINDOWS			1
#define PATCH_MOVE_TILED_WINDOWS			1
#define PATCH_PERSISTENT_METADATA			1	// Store client metadata, restore metadata after a restart
#define PATCH_PERTAG						1
#define PATCH_SCAN_OVERRIDE_REDIRECTS		0	// Not usually necessary - only picks up windows during scan();
#define PATCH_SHOW_MASTER_CLIENT_ON_TAG		1
#define PATCH_STATUSCMD						0
#define PATCH_SWITCH_TAG_ON_EMPTY			1	// Enables tag switch if there are no visible clients;
#define PATCH_SYSTRAY						1
#define PATCH_TERMINAL_SWALLOWING			1
#define PATCH_TORCH							1
#define PATCH_TWO_TONE_TITLE				1
#define PATCH_VANITY_GAPS					1
#define PATCH_WINDOW_ICONS					1
#define PATCH_WINDOW_ICONS_CUSTOM_ICONS		1	// Enable per-client a user-specified icon;
#define PATCH_WINDOW_ICONS_DEFAULT_ICON		1	// Set a default icon for clients without;
#define PATCH_WINDOW_ICONS_LEGACY_ICCCM		1	// Kludge to populate window icons the old way;

// Layouts
#define PATCH_LAYOUT_BSTACK					1
#define PATCH_LAYOUT_BSTACKHORIZ			1
#define PATCH_LAYOUT_CENTREDFLOATINGMASTER	1
#define PATCH_LAYOUT_CENTREDMASTER			1
#define PATCH_LAYOUT_DECK					1
#define PATCH_LAYOUT_DWINDLE				1
#define PATCH_LAYOUT_GAPLESSGRID			1
#define PATCH_LAYOUT_GRID					1
#define PATCH_LAYOUT_HORIZGRID				1
#define PATCH_LAYOUT_NROWGRID				1
#define PATCH_LAYOUT_SPIRAL					1

// Workaraounds
#define PATCH_XFTLIB_EMOJI_WORKAROUND		0	// workaround for a crash with colour emojis on some systems (set to 0 when XftLib >= 2.3.5)

// Additional client flags
#define PATCH_FLAG_ALWAYSONTOP				1
#define PATCH_FLAG_CENTRED					1
#define PATCH_FLAG_FAKEFULLSCREEN			1
#define PATCH_FLAG_FLOAT_ALIGNMENT			1	// Useful with PATCH_FLAG_PANEL to nail external panels in position
#define PATCH_FLAG_FOLLOW_PARENT			1	// client will follow its parent
#define PATCH_FLAG_GAME						1
#define PATCH_FLAG_GAME_STRICT				1	// game clients will by default not minimize on focus loss to a different monitor
#define PATCH_FLAG_GREEDY_FOCUS				1	// prevents a client from losing focus due to mouse movement, depends on PATCH_FOCUS_FOLLOWS_MOUSE
#define PATCH_FLAG_HIDDEN					1
#define PATCH_FLAG_IGNORED					1
#define PATCH_FLAG_NEVER_FOCUS				1	// set-never-focus rule enables override of neverfocus wm hint;
#define PATCH_FLAG_NEVER_FULLSCREEN			1	// set-never-fullscreen rule prevent clients from setting themselves fullscreen;
#define PATCH_FLAG_NEVER_MOVE				1	// set-never-move rule prevent clients from moving themselves;
#define PATCH_FLAG_NEVER_RESIZE				1	// set-never-resize rule prevent clients from resizing themselves;
#define PATCH_FLAG_PANEL					1
#define PATCH_FLAG_PAUSE_ON_INVISIBLE		1
#define PATCH_FLAG_PARENT					1	// treat client as if its parent is the specified window (of same class if rule is deferred)
#define PATCH_FLAG_STICKY					1
#define PATCH_FLAG_TITLE					1	// displayed title is specified by user;

#define DEBUGGING							0	// include debugging code
