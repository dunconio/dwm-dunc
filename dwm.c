/* See LICENSE file for copyright and license details.
 *
 * dynamic window manager is designed like any other X client as well. It is
 * driven through handling X events. In contrast to other X clients, a window
 * manager selects for SubstructureRedirectMask on the root window, to receive
 * events about window (dis-)appearance. Only one X connection at a time is
 * allowed to select for this event mask.
 *
 * The event handlers of dwm are organized in an array which is accessed
 * whenever a new event has been fetched. This allows event dispatching
 * in O(1) time.
 *
 * Each child of the root window is called a client, except windows which have
 * set the override_redirect flag. Clients are organized in a linked client
 * list on each monitor, the focus history is remembered through a stack list
 * on each monitor. Each client contains a bit array to indicate the tags of a
 * client.
 *
 * Keys are organized as arrays and defined in config.h.
 *
 * Tagging rules and extra monitor/tag layout parameters are defined in JSON
 * files parsed at run-time.
 *
 * To understand everything else, start reading main().
 */
#include <ctype.h> /* for tolower function, very tiny standard library */
#include <errno.h>
#include <locale.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/XKBlib.h>
#include <X11/Xproto.h>
#include <X11/Xutil.h>

#include <xkbcommon/xkbcommon.h>

#ifdef XINERAMA
#include <X11/extensions/Xinerama.h>
#endif /* XINERAMA */
#include <X11/Xft/Xft.h>
#include <X11/Xlib-xcb.h>
#include <xcb/res.h>
#ifdef __OpenBSD__
#include <sys/sysctl.h>
#include <kvm.h>
#endif /* __OpenBSD */
#include <time.h>

#include "patches.h"
#include "drw.h"
#include "util.h"

#if PATCH_HANDLE_SIGNALS
#include <poll.h>
#endif // PATCH_HANDLE_SIGNALS

#if PATCH_BIDIRECTIONAL_TEXT
#include <fribidi.h>
#endif // PATCH_BIDIRECTIONAL_TEXT

#if PATCH_FLAG_GAME || PATCH_MOUSE_POINTER_HIDING
#include <X11/extensions/Xfixes.h>
#endif // PATCH_FLAG_GAME || PATCH_MOUSE_POINTER_HIDING
#include <X11/extensions/XInput.h>
#include <X11/extensions/XInput2.h>
#if PATCH_MOUSE_POINTER_HIDING
#include <X11/extensions/sync.h>
#endif // PATCH_MOUSE_POINTER_HIDING

#if PATCH_MOUSE_POINTER_WARPING
#include <math.h>
#else // NO PATCH_MOUSE_POINTER_WARPING
#include <math.h>
#endif // PATCH_MOUSE_POINTER_WARPING

#if PATCH_WINDOW_ICONS
#if PATCH_WINDOW_ICONS_DEFAULT_ICON || PATCH_WINDOW_ICONS_CUSTOM_ICONS
#include <Imlib2.h>
#endif // PATCH_WINDOW_ICONS_DEFAULT_ICON || PATCH_WINDOW_ICONS_CUSTOM_ICONS
#endif // PATCH_WINDOW_ICONS

#include "parse-simple-expression.c"
static cJSON *layout_json = NULL;
static cJSON *fonts_json = NULL;
static cJSON *monitors_json = NULL;
static cJSON *rules_json = NULL;
static const char *rules_filename = NULL;
#if PATCH_FLAG_TITLE || PATCH_SHOW_MASTER_CLIENT_ON_TAG || PATCH_ALTTAB || PATCH_WINDOW_ICONS_CUSTOM_ICONS || PATCH_FLAG_PARENT
static cJSON *rules_compost = NULL;
#endif // PATCH_FLAG_TITLE || PATCH_SHOW_MASTER_CLIENT_ON_TAG || PATCH_ALTTAB || PATCH_WINDOW_ICONS_CUSTOM_ICONS || PATCH_FLAG_PARENT
#if PATCH_FONT_GROUPS
static cJSON *fontgroups_json = NULL;
static cJSON *barelement_fontgroups_json = NULL;
#if PATCH_ALTTAB
static char *tabFontgroup = NULL;		// alt-tab switcher font group;
#endif // PATCH_ALTTAB
#endif // PATCH_FONT_GROUPS
static cJSON *badprocs = NULL;
static cJSON *procparents = NULL;

static char **coloursbackup = NULL;
#if PATCH_CUSTOM_TAG_ICONS
static char **tagiconpathsbackup = NULL;
#endif // PATCH_CUSTOM_TAG_ICONS

typedef struct {
	const char *name;
	const char *help;
} supported_json;

typedef struct {
	const int types;
	const char *name;
	const char *help;
} supported_rules_json;

static const supported_json supported_layout_global[] = {
	#if PATCH_ALTTAB
	{ "alt-tab-border",				"alt-tab switcher border width in pixels" },
	{ "alt-tab-dropdown-vpad-extra","alt-tab switcher dropdown menu item vertical padding extra gap in pixels" },
	{ "alt-tab-dropdown-vpad-factor","alt-tab switcher dropdown menu item vertical padding factor" },
	#if PATCH_FONT_GROUPS
	{ "alt-tab-font-group",			"alt-tab switcher will use the specified font group from \"font-groups\"" },
	#endif // PATCH_FONT_GROUPS
	#if PATCH_ALTTAB_HIGHLIGHT
	{ "alt-tab-highlight",			"alt-tab switcher highlights clients during selection" },
	#endif // PATCH_ALTTAB_HIGHLIGHT
	{ "alt-tab-monitor-format",		"printf style format of monitor identifier using %s as placeholder" },
	{ "alt-tab-no-centre-dropdown",	"true to make alt-tab dropdown left-aligned when WinTitle is centre-aligned" },
	{ "alt-tab-size",				"maximum size of alt-tab switcher (WxH)" },
	{ "alt-tab-text-align",			"alt-tab text alignment - 0:left, 1:centre, 2:right" },
	{ "alt-tab-x",					"alt-tab switcher position - 0:left, 1:centre, 2:right" },
	{ "alt-tab-y",					"alt-tab switcher position - 0:top, 1:middle, 2:bottom" },
	#endif // PATCH_ALTTAB
	#if PATCH_FONT_GROUPS
	{ "bar-element-font-groups",	"single object or array of objects containing \"bar-element\" string and \"font-group\" string" },
	#endif // PATCH_FONT_GROUPS
	{ "bar-layout",					"array of bar elements in order of appearance\n(TagBar, LtSymbol, WinTitle, StatusText"
		#if PATCH_SHOW_DESKTOP
		#if PATCH_SHOW_DESKTOP_BUTTON
		", ShowDesktop"
		#endif // PATCH_SHOW_DESKTOP_BUTTON
		#endif // PATCH_SHOW_DESKTOP
	")" },
	#if PATCH_SHOW_MASTER_CLIENT_ON_TAG
	{ "bar-tag-format-empty",	 	"printf style format of tag displayed when no client is assigned, using %s as placeholder" },
	{ "bar-tag-format-populated", 	"printf style format of tag displayed when one or more clients are assigned, using %s as placeholders" },
	{ "bar-tag-format-reversed", 	"true to reverse the order of tag number and master client class" },
	#endif // PATCH_SHOW_MASTER_CLIENT_ON_TAG
	{ "border-width",				"window border width in pixels" },
	#if PATCH_BORDERLESS_SOLITARY_CLIENTS
	{ "borderless-solitary",		"true to hide window borders for solitary tiled clients" },
	#endif // PATCH_BORDERLESS_SOLITARY_CLIENTS
	#if PATCH_CLASS_STACKING
	{ "class-stacking",				"true for visible tiled clients of the same class to occupy the same tile" },
	#endif // PATCH_CLASS_STACKING
	#if PATCH_CLIENT_INDICATORS
	{ "client-indicators",			"true to show indicators blobs on the edge of each tag to represent the number of clients present" },
	{ "client-indicator-size",		"size in pixels of client indicators" },
	{ "client-indicators-top",		"true to show indicators at the top of the bar, false to show indicators at the bottom" },
	#endif // PATCH_CLIENT_INDICATORS
	#if PATCH_CLIENT_OPACITY
	{ "client-opacity-active",		"opacity of active clients (between 0 and 1)" },
	{ "client-opacity-enabled",		"true to enable variable window opacity" },
	{ "client-opacity-inactive",	"opacity of inactive clients (between 0 and 1)" },
	#endif // PATCH_CLIENT_OPACITY
	{ "colours-layout",				"colour of layout indicator, in the form\n[<foreground>, <background>, <border>]" },
	#if PATCH_FLAG_HIDDEN || PATCH_SHOW_DESKTOP
	{ "colours-hidden",				"colour of hidden elements, in the form\n[<foreground>, <background>, <border>]" },
	#endif // PATCH_FLAG_HIDDEN || PATCH_SHOW_DESKTOP
	{ "colours-normal",				"colour of normal elements, in the form\n[<foreground>, <background>, <border>]" },
	{ "colours-selected",			"colour of selected elements, in the form\n[<foreground>, <background>, <border>]" },
	#if PATCH_TWO_TONE_TITLE
	{ "colour-selected-bg2",		"active client title background colour 2 (for the gradient fill)" },
	#endif // PATCH_TWO_TONE_TITLE
	#if PATCH_COLOUR_BAR
	{ "colours-status",				"status zone colours, in the form\n[<foreground>, <background>, <border>]" },
	{ "colours-tag-bar",			"tag bar zone colours, in the form\n[<foreground>, <background>, <border>]" },
	#if PATCH_FLAG_HIDDEN
	{ "colours-tag-bar-hidden",		"tag bar zone colours for tags with no visible and 1 or more hidden clients, in the form\n[<foreground>, <background>, <border>]" },
	#endif // PATCH_FLAG_HIDDEN
	{ "colours-tag-bar-selected",	"tag bar zone colours for selected elements, in the form\n[<foreground>, <background>, <border>]" },
	{ "colours-title",				"window title zone colours, in the form\n[<foreground>, <background>, <border>]" },
	{ "colours-title-selected",		"window title zone colours for selected elements, in the form\n[<foreground>, <background>, <border>]" },
	#endif // PATCH_COLOUR_BAR
	#if PATCH_TORCH
	{ "colours-torch",				"torch colours, in the form\n[<foreground>, <background>, <border>]" },
	#endif // PATCH_TORCH
	{ "colours-urgent",				"colour of urgent elements, in the form\n[<foreground>, <background>, <border>]" },
	#if PATCH_ALTTAB
	#if PATCH_FLAG_HIDDEN
	{ "colours-alt-tab-hidden",		"colour of alt-tab switcher hidden elements, in the form\n[<foreground>, <background>, <border>]" },
	#endif // PATCH_FLAG_HIDDEN
	{ "colours-alt-tab-normal",		"colour of alt-tab switcher elements, in the form\n[<foreground>, <background>, <border>]" },
	{ "colours-alt-tab-selected",	"colour of alt-tab switcher selected elements, in the form\n[<foreground>, <background>, <border>]" },
	{ "colours-alt-tab-urgent",		"colour of alt-tab switcher urgent elements, in the form\n[<foreground>, <background>, <border>]" },
	#endif // PATCH_ALTTAB
	#if PATCH_MOUSE_POINTER_HIDING
	{ "cursor-autohide",			"true to hide cursor when stationary or keys are pressed, for all clients" },
	{ "cursor-autohide-delay",		"the number of seconds before a stationary cursor can be hidden, 0 to disable" },
	#endif // PATCH_MOUSE_POINTER_HIDING
	#if PATCH_CUSTOM_TAG_ICONS
	{ "custom-tag-icons",			"array of paths to icon files to show in place of tag identifier (for each tag)" },
	#endif // PATCH_CUSTOM_TAG_ICONS
	#if PATCH_WINDOW_ICONS
	#if PATCH_WINDOW_ICONS_DEFAULT_ICON
	{ "default-icon",				"path to default icon file for clients without icons" },
	#if PATCH_SHOW_DESKTOP
	{ "desktop-icon",				"path to default icon file for desktop clients" },
	#endif // PATCH_SHOW_DESKTOP
	#endif // PATCH_WINDOW_ICONS_DEFAULT_ICON
	#endif // PATCH_WINDOW_ICONS
	{ "default-tags",				"array of single character strings for the default character for each tag" },
	#if PATCH_FOCUS_BORDER
	{ "focus-border-edge",			"determine to which edge the border is added - N:top, S:bottom, E:right, W:left" },
	{ "focus-border-size",			"height of border on focused client's edge, 0 to disable" },
	#elif PATCH_FOCUS_PIXEL
	{ "focus-pixel-corner",			"determine to which corner the box is added - NE:top-right, SE:bottom-right, SW:bottom-left, NW:top-left" },
	{ "focus-pixel-size",			"width/height of box on focused client's bottom right corner, 0 to disable" },
	#endif // PATCH_FOCUS_BORDER || PATCH_FOCUS_PIXEL
	#if PATCH_FONT_GROUPS
	{ "font-groups",				"single object or array of objects containing \"name\" string and \"fonts\" string or array of strings" },
	#endif // PATCH_FONT_GROUPS
	{ "fonts",						"font string or array of font strings to use by default" },
	#if PATCH_HIDE_VACANT_TAGS
	{ "hide-vacant-tags",			"hide tags with no clients" },
	#endif // PATCH_HIDE_VACANT_TAGS
	#if PATCH_WINDOW_ICONS
	{ "icon-size",					"size of window icons on the bar" },
	#if PATCH_ALTTAB
	{ "icon-size-big",				"size of large window icons in the alt-tab switcher" },
	#endif // PATCH_ALTTAB
	{ "icon-spacing",				"size of gap between icon and window title" },
	#endif // PATCH_WINDOW_ICONS
	#if PATCH_MIRROR_LAYOUT
	{ "mirror-layout",				"switch master area and stack area" },
	#endif // PATCH_MIRROR_LAYOUT
	{ "monitors",					"array of monitor objects (see \"monitor sections\")" },
	#if PATCH_MOUSE_POINTER_WARPING
	{ "mouse-warping-enabled",		"true to enable warping of the mouse pointer" },
	#if PATCH_MOUSE_POINTER_WARPING_SMOOTH
	{ "mouse-warping-smoothly",		"true to enable smooth warping of the mouse pointer when mouse-warping-enabled is true" },
	#endif // PATCH_MOUSE_POINTER_WARPING_SMOOTH
	#endif // PATCH_MOUSE_POINTER_WARPING
	{ "process-no-sigterm",			"array of process names that don't respect SIGTERM conventions" },
	{ "process-parents",			"array of objects with \"procname\" and \"parent\" string values" },
	#if PATCH_CUSTOM_TAG_ICONS
	{ "show-custom-tag-icons",		"true to show a custom icon in place of tag identifier (for each tag)" },
	#endif // PATCH_CUSTOM_TAG_ICONS
	#if PATCH_SHOW_DESKTOP
	{ "show-desktop",				"true to enable management of desktop clients, and toggle desktop" },
	#if PATCH_SHOW_DESKTOP_BUTTON
	{ "show-desktop-button-symbol",	"symbol to show on the clickable show desktop button (ShowDesktop bar element)" },
	#endif // PATCH_SHOW_DESKTOP_BUTTON
	{ "show-desktop-layout-symbol",	"symbol to show in place of layout when the desktop is visible" },
	#if PATCH_SHOW_DESKTOP_UNMANAGED
	{ "show-desktop-unmanaged",		"true to ignore NetWMWindowTypeDesktop windows (if the desktop manager expects to span all monitors)" },
	#endif // PATCH_SHOW_DESKTOP_UNMANAGED
	#if PATCH_SHOW_DESKTOP_ONLY_WHEN_ACTIVE
	{ "show-desktop-when-active",	"true to only allow switching to the desktop, when a desktop client exists" },
	#endif // PATCH_SHOW_DESKTOP_ONLY_WHEN_ACTIVE
	#if PATCH_SHOW_DESKTOP_WITH_FLOATING
	{ "show-desktop-with-floating",	"true to allow floating clients to be visible when showing the desktop" },
	#endif // PATCH_SHOW_DESKTOP_WITH_FLOATING
	#endif // PATCH_SHOW_DESKTOP
	#if PATCH_WINDOW_ICONS
	#if PATCH_WINDOW_ICONS_ON_TAGS
	{ "show-icons-on-tags",			"true to show primary master client's icon in place of tag identifier (for each tag)" },
	#endif // PATCH_WINDOW_ICONS_ON_TAGS
	#endif // PATCH_WINDOW_ICONS
	#if PATCH_SHOW_MASTER_CLIENT_ON_TAG
	{ "showmaster", 				"set to true if the master client class should be shown on each tag on the bar" },
	#endif // PATCH_SHOW_MASTER_CLIENT_ON_TAG
	#if PATCH_STATUS_ALLOW_FIXED_MONITOR
	{ "status-allow-fixed-monitor",	"true to enable rendering the status bar element whether or not the monitor is active (if only one monitor has showstatus set)" },
	#endif // PATCH_STATUS_ALLOW_FIXED_MONITOR
	#if PATCH_STATUSCMD
	#if PATCH_STATUSCMD_COLOURS
	{ "status-colour-1",			"status zone section colour 1" },
	{ "status-colour-2",			"status zone section colour 2" },
	{ "status-colour-3",			"status zone section colour 3" },
	{ "status-colour-4",			"status zone section colour 4" },
	{ "status-colour-5",			"status zone section colour 5" },
	{ "status-colour-6",			"status zone section colour 6" },
	{ "status-colour-7",			"status zone section colour 7" },
	{ "status-colour-8",			"status zone section colour 8" },
	{ "status-colour-9",			"status zone section colour 9" },
	{ "status-colour-10",			"status zone section colour 10" },
	{ "status-colour-11",			"status zone section colour 11" },
	{ "status-colour-12",			"status zone section colour 12" },
	{ "status-colour-13",			"status zone section colour 13" },
	{ "status-colour-14",			"status zone section colour 14" },
	{ "status-colour-15",			"status zone section colour 15" },
	#if PATCH_STATUSCMD_COLOURS_DECOLOURIZE
	{"status-decolourize-inactive",	"true to decolourize the status text when monitor is inactive" },
	#endif // PATCH_STATUSCMD_COLOURS_DECOLOURIZE
	#endif // PATCH_STATUSCMD_COLOURS
	#endif // PATCH_STATUSCMD
	#if PATCH_SYSTRAY
	{ "system-tray",				"true to enable system tray handling" },
	{ "system-tray-align",			"align the system tray to side of the status area:\n0:left, 1:right" },
	{ "system-tray-pinning",		"pin system tray to specific monitor, -1 to follow the active monitor" },
	{ "system-tray-spacing",		"number of pixels between system tray icons" },
	#endif // PATCH_SYSTRAY
	#if PATCH_TERMINAL_SWALLOWING
	{ "terminal-swallowing",		"true to enable terminal swallowing" },
	#endif // PATCH_TERMINAL_SWALLOWING
	{ "title-align",				"active client title alignment: 0:left, 1:centred, 2:right" },
	{ "title-border-width",			"WinTitle bar element border width in pixels, for when monitor is selected without a client selected" },
	{ "top-bar",					"true to show the bar at the top of each monitor" },
	{ "urgency-hinting",			"disable urgency hinting for clients (doesn't affect set-urgency rule functionality)" },
	{ "vanity-gaps",				"true for vanity gaps (default), false for no gaps between windows" },
	{ "vanity-gaps-inner-h",		"inner horizontal gap between windows in pixels" },
	{ "vanity-gaps-inner-v",		"inner vertical gap between windows in pixels" },
	{ "vanity-gaps-outer-h",		"outer horizontal gap between windows and monitor edges in pixels" },
	{ "vanity-gaps-outer-v",		"outer vertical gap between windows and monitor edges in pixels" },
	{ "view-on-tag",			    "switch view when tagging a client" },
};
static const supported_json supported_layout_mon[] = {
	{ "comment",				"ignored" },
	{ "log-rules",				"log all matching rules for this monitor" },
	{ "monitor",				"monitor number" },
	#if PATCH_ALTTAB
	{ "set-alt-tab-border",		"alt-tab switcher border width in pixels on this monitor" },
	{ "set-alt-tab-size",		"maximum size of alt-tab switcher (WxH) on this monitor" },
	{ "set-alt-tab-text-align",	"alt-tab text alignment on this monitor - 0:left, 1:centre, 2:right" },
	{ "set-alt-tab-x",			"alt-tab switcher position on this monitor - 0:left, 1:centre, 2:right" },
	{ "set-alt-tab-y",			"alt-tab switcher position on this monitor - 0:top, 1:middle, 2:bottom" },
	#endif // PATCH_ALTTAB
	#if PATCH_FONT_GROUPS
	{ "set-bar-element-font-groups","single object or array of objects containing \"bar-element\" string and \"font-group\" string, for this monitor" },
	#endif // PATCH_FONT_GROUPS
	{ "set-bar-layout",			"array of bar elements in order of appearance (TagBar, LtSymbol, WinTitle, StatusText)" },
	#if PATCH_CLASS_STACKING
	{ "set-class-stacking",		"true for visible tiled clients of the same class to occupy the same tile on this monitor" },
	#endif // PATCH_CLASS_STACKING
	#if PATCH_MOUSE_POINTER_HIDING
	{ "set-cursor-autohide",	"true to hide cursor when stationary on this monitor" },
	{ "set-cursor-hide-on-keys","true to hide cursor when keys are pressed on this monitor" },
	#endif // PATCH_MOUSE_POINTER_HIDING
	#if PATCH_CUSTOM_TAG_ICONS
	{ "set-custom-tag-icons",	"array of paths to icon files to show in place of tag identifier (for each tag) on this monitor" },
	#endif // PATCH_CUSTOM_TAG_ICONS
	{ "set-default",		    "set this monitor to be the default selected on startup" },
	#if PATCH_VANITY_GAPS
	{ "set-enable-gaps",		"set to true to enable vanity gaps between clients (default)" },
	{ "set-gap-inner-h",		"horizontal inner gap between clients" },
	{ "set-gap-inner-v",		"vertical inner gap between clients" },
	{ "set-gap-outer-h",		"horizontal outer gap between clients and the screen edges" },
	{ "set-gap-outer-v",		"vertical outer gap between clients and the screen edges" },
	#endif // PATCH_VANITY_GAPS
	#if PATCH_HIDE_VACANT_TAGS
	{ "set-hide-vacant-tags",	"hide tags with no clients on this monitor" },
	#endif // PATCH_HIDE_VACANT_TAGS
	#if PATCH_CLIENT_INDICATORS
	{ "set-indicators-top",		"set to true to show client indicators on the top edge of the bar" },
	#endif // PATCH_CLIENT_INDICATORS
	{ "set-layout",				"layout number or layout symbol" },
	{ "set-mfact",				"size of master client area for all tags on this monitor" },
	#if PATCH_MIRROR_LAYOUT
	{ "set-mirror-layout",		"switch master area and stack area on this monitor" },
	#endif // PATCH_MIRROR_LAYOUT
	#if PATCH_CLIENT_OPACITY
	{ "set-opacity-active",		"level of opacity for clients when active on this monitor" },
	{ "set-opacity-inactive",	"level of opacity for clients when inactive on this monitor" },
	#endif // PATCH_CLIENT_OPACITY
	{ "set-nmaster",			"number of master clients for all tags on this monitor" },
	#if PATCH_ALT_TAGS
	{ "set-quiet-alt-tags", 	"don't raise the bar or show over fullscreen clients on this monitor" },
	#endif // PATCH_ALT_TAGS
	#if PATCH_SHOW_MASTER_CLIENT_ON_TAG
	{ "set-reverse-master",		"set to true if the master client class should be shown before the tag indicator" },
	#endif // PATCH_SHOW_MASTER_CLIENT_ON_TAG
	#if PATCH_CUSTOM_TAG_ICONS
	{ "set-show-custom-tag-icons",	"true to show a custom icon in place of tag identifier (for each tag) on this monitor" },
	#endif // PATCH_CUSTOM_TAG_ICONS
	#if PATCH_WINDOW_ICONS
	#if PATCH_WINDOW_ICONS_ON_TAGS
	{ "set-show-icons-on-tags",	"true to show primary master client's icon in place of tag identifier (for each tag) on this monitor" },
	#endif // PATCH_WINDOW_ICONS_ON_TAGS
	#endif // PATCH_WINDOW_ICONS
	{ "set-showbar",			"whether to show the bar by default on this monitor" },
	#if PATCH_SHOW_MASTER_CLIENT_ON_TAG
	{ "set-showmaster", 		"set to true if the master client class should be shown on each tag on the bar" },
	#endif // PATCH_SHOW_MASTER_CLIENT_ON_TAG
	{ "set-showstatus",			"set to 1 if the status text should be displayed, -1 to ignore root window name changes" },
	#if PATCH_VIRTUAL_MONITORS
	{ "set-split-enabled",		"set to 1 to enable splitting the physical monitor into virtual monitors (no effect when set-split-type is 0)" },
	{ "set-split-type",			"set to 1 to split the screen horizontally, 2 to split vertically" },
	#endif // PATCH_VIRTUAL_MONITORS
	{ "set-start-tag",			"default tag to activate on startup" },
	#if PATCH_SWITCH_TAG_ON_EMPTY
	{ "set-switch-on-empty",	"switch to the specified tag when no more clients are visible under the active tag" },
	#endif // PATCH_SWITCH_TAG_ON_EMPTY
	#if PATCH_SHOW_MASTER_CLIENT_ON_TAG
	{ "set-tag-format-empty", 	"printf style format of tag displayed when no client is assigned, using %s as placeholder on this monitor" },
	{ "set-tag-format-populated","printf style format of tag displayed when one or more clients are assigned, using %s as placeholders on this monitor" },
	#endif // PATCH_SHOW_MASTER_CLIENT_ON_TAG
	{ "set-title-align",		"active client title alignment: 0:left, 1:centred, 2:right" },
	{ "set-topbar",				"set to true if the bar should be at the top of the screen for this monitor" },
	{ "tags",					"array of tag-specific settings (see \"tags sections (per monitor)\")" },
};
static const supported_json supported_layout_tag[] = {
	{ "comment", 			"ignored" },
	{ "index",				"tag index number, usually between 1 and 9" },
	#if PATCH_HIDE_VACANT_TAGS
	{ "set-always-visible",	"true to always show the tag even when there are no clients attached" },
	#endif // PATCH_HIDE_VACANT_TAGS
	#if PATCH_PERTAG
	#if PATCH_CLASS_STACKING
	{ "set-class-stacking",	"true for visible tiled clients of the same class to occupy the same tile on this tag" },
	#endif // PATCH_CLASS_STACKING
	#if PATCH_MOUSE_POINTER_HIDING
	{ "set-cursor-autohide","true to hide cursor when stationary on this tag" },
	{ "set-cursor-hide-on-keys","true to hide cursor when keys are pressed on this tag" },
	#endif // PATCH_MOUSE_POINTER_HIDING
	{ "set-enable-gaps",	"set to true to enable vanity gaps between clients" },
	{ "set-layout",			"layout number or layout symbol" },
	{ "set-mfact",			"size of master client area for this tag" },
	{ "set-nmaster",		"number of master clients on this tag" },
	#if PATCH_ALT_TAGS
	{ "set-quiet-alt-tags", "don't raise the bar or show over fullscreen clients on this tag" },
	#endif // PATCH_ALT_TAGS
	{ "set-showbar",		"whether to show the bar by default on this tag" },
	#if PATCH_SWITCH_TAG_ON_EMPTY
	{ "set-switch-on-empty","switch to the specified tag when no more clients are visible under this tag" },
	#endif // PATCH_SWITCH_TAG_ON_EMPTY
	#endif // PATCH_PERTAG
	#if PATCH_ALT_TAGS
	{ "set-tag-text",		"show this text instead of the default tag text" },
	#endif // PATCH_ALT_TAGS
};

#if PATCH_HANDLE_SIGNALS
#define SIGRELOAD_RESCAN	SIGRTMIN+0
#define SIGRELOAD_RULES		SIGRTMIN+1
#endif // PATCH_HANDLE_SIGNALS

#define R_IGNORE	0		// Ignored
#define R_A		(1 << 0)	// Array
#define R_BOOL	(1 << 1)	// Boolean
#define R_I		(1 << 2)	// Integer
#define R_N		(1 << 3)	// Number
#define R_S		(1 << 4)	// String

static const supported_rules_json supported_rules[] = {
	{ R_IGNORE,	"comment",						"ignored" },
	{ R_BOOL,	"defer-rule",					"if rule matches a client excluding its title, then wait until the title changes and reapply" },
	{ R_BOOL,	"exclusive",					"rule will be applied after non-exclusive rules, and other rules will not apply" },
	{ R_A|R_S,	"if-class-begins",				"substring matching from the start of class" },
	{ R_A|R_S,	"if-class-contains",			"substring matching on class" },
	{ R_A|R_S,	"if-class-ends",				"substring matching from the end of class" },
	{ R_A|R_S,	"if-class-is",					"exact full string matching on class" },
	#if PATCH_SHOW_DESKTOP
	{ R_BOOL,	"if-desktop",					"true if the client is a desktop window"},
	#endif // PATCH_SHOW_DESKTOP
	{ R_BOOL,	"if-fixed-size",				"false if the client is resizable or fullscreen, true if fixed size" },
	{ R_BOOL,	"if-has-parent",				"client has a parent" },
	#if PATCH_ACTIVE_CLIENT_CHECKS
	{ R_A|R_S,	"if-active-class-begins",			"substring matching from the start of the active client's class" },
	{ R_A|R_S,	"if-active-class-contains",			"substring matching on the active client's class" },
	{ R_A|R_S,	"if-active-class-ends",				"substring matching from the end of the active client's class" },
	{ R_A|R_S,	"if-active-class-is",				"exact full string matching on the active client's class" },
	{ R_A|R_S,	"if-active-instance-begins",		"substring matching from the start of the active client's instance" },
	{ R_A|R_S,	"if-active-instance-contains",		"substring matching on the active client's instance" },
	{ R_A|R_S,	"if-active-instance-ends",			"substring matching from the end of the active client's instance" },
	{ R_A|R_S,	"if-active-instance-is",			"exact full string matching on the active client's instance" },
	{ R_A|R_S,	"if-active-role-begins",			"substring matching from the start of the active client's role" },
	{ R_A|R_S,	"if-active-role-contains",			"substring matching on the active client's role" },
	{ R_A|R_S,	"if-active-role-ends",				"substring matching from the end of the active client's role" },
	{ R_A|R_S,	"if-active-role-is",				"exact full string matching on the active client's role" },
	{ R_A|R_S,	"if-active-title-begins",			"substring matching from the start of the active client's title" },
	{ R_A|R_S,	"if-active-title-contains",			"substring matching on the active client's title" },
	{ R_A|R_S,	"if-active-title-ends",				"substring matching from the end of the active client's title" },
	{ R_A|R_S,	"if-active-title-is",				"exact full string matching on the active client's title" },
	#endif // PATCH_ACTIVE_CLIENT_CHECKS
	{ R_A|R_S,	"if-instance-begins",				"substring matching from the start of instance" },
	{ R_A|R_S,	"if-instance-contains",				"substring matching on instance" },
	{ R_A|R_S,	"if-instance-ends",					"substring matching from the end of instance" },
	{ R_A|R_S,	"if-instance-is",					"exact full string matching on instance" },
	#if PATCH_ACTIVE_CLIENT_CHECKS
	{ R_A|R_S,	"if-not-active-class-begins",		"substring matching from the start of the active client's class (inverted)" },
	{ R_A|R_S,	"if-not-active-class-contains",		"substring matching on the active client's class (inverted)" },
	{ R_A|R_S,	"if-not-active-class-ends",			"substring matching from the end of the active client's class (inverted)" },
	{ R_A|R_S,	"if-not-active-class-is",			"exact full string matching on the active client's class (inverted)" },
	{ R_A|R_S,	"if-not-active-instance-begins",	"substring matching from the start of the active client's instance (inverted)" },
	{ R_A|R_S,	"if-not-active-instance-contains",	"substring matching on the active client's instance (inverted)" },
	{ R_A|R_S,	"if-not-active-instance-ends",		"substring matching from the end of the active client's instance (inverted)" },
	{ R_A|R_S,	"if-not-active-instance-is",		"exact full string matching on the active client's instance (inverted)" },
	{ R_A|R_S,	"if-not-active-role-begins",		"substring matching from the start of the active client's role (inverted)" },
	{ R_A|R_S,	"if-not-active-role-contains",		"substring matching on the active client's role (inverted)" },
	{ R_A|R_S,	"if-not-active-role-ends",			"substring matching from the end of the active client's role (inverted)" },
	{ R_A|R_S,	"if-not-active-role-is",			"exact full string matching on the active client's role (inverted)" },
	{ R_A|R_S,	"if-not-active-title-begins",		"substring matching from the start of the active client's title (inverted)" },
	{ R_A|R_S,	"if-not-active-title-contains",		"substring matching on the active client's title (inverted)" },
	{ R_A|R_S,	"if-not-active-title-ends",			"substring matching from the end of the active client's title (inverted)" },
	{ R_A|R_S,	"if-not-active-title-is",			"exact full string matching on the active client's title (inverted)" },
	#endif // PATCH_ACTIVE_CLIENT_CHECKS
	{ R_A|R_S,	"if-not-class-begins",				"substring matching from the start of class (inverted)" },
	{ R_A|R_S,	"if-not-class-contains",			"substring matching on class (inverted)" },
	{ R_A|R_S,	"if-not-class-ends",				"substring matching from the end of class (inverted)" },
	{ R_A|R_S,	"if-not-class-is",					"exact full string matching on class (inverted)" },
	{ R_A|R_S,	"if-not-instance-begins",			"substring matching from the start of instance (inverted)" },
	{ R_A|R_S,	"if-not-instance-contains",			"substring matching on instance (inverted)" },
	{ R_A|R_S,	"if-not-instance-ends",				"substring matching from the end of instance (inverted)" },
	{ R_A|R_S,	"if-not-instance-is",				"exact full string matching on instance (inverted)" },
	{ R_A|R_S,	"if-not-parent-class-begins",		"substring matching from the start of parent's class (inverted)" },
	{ R_A|R_S,	"if-not-parent-class-contains",		"substring matching on parent's class (inverted)" },
	{ R_A|R_S,	"if-not-parent-class-ends",			"substring matching from the end of parent's class (inverted)" },
	{ R_A|R_S,	"if-not-parent-class-is",			"exact full string matching on parent's class (inverted)" },
	{ R_A|R_S,	"if-not-parent-instance-begins",	"substring matching from the start of parent's instance (inverted)" },
	{ R_A|R_S,	"if-not-parent-instance-contains",	"substring matching on parent's instance (inverted)" },
	{ R_A|R_S,	"if-not-parent-instance-ends",		"substring matching from the end of parent's instance (inverted)" },
	{ R_A|R_S,	"if-not-parent-instance-is",		"exact full string matching on parent's instance (inverted)" },
	{ R_A|R_S,	"if-not-parent-role-begins",		"substring matching from the start of parent's role (inverted)" },
	{ R_A|R_S,	"if-not-parent-role-contains",		"substring matching on parent's role (inverted)" },
	{ R_A|R_S,	"if-not-parent-role-ends",			"substring matching from the end of parent's role (inverted)" },
	{ R_A|R_S,	"if-not-parent-role-is",			"exact full string matching on parent's role (inverted)" },
	{ R_A|R_S,	"if-not-parent-title-begins",		"substring matching from the start of parent's title (inverted)" },
	{ R_A|R_S,	"if-not-parent-title-contains",		"substring matching on parent's title (inverted)" },
	{ R_A|R_S,	"if-not-parent-title-ends",			"substring matching from the end of parent's title (inverted)" },
	{ R_A|R_S,	"if-not-parent-title-is",			"exact full string matching on parent's title (inverted)" },
	{ R_A|R_S,	"if-not-role-begins",				"substring matching from the start of role (inverted)" },
	{ R_A|R_S,	"if-not-role-contains",				"substring matching on role (inverted)" },
	{ R_A|R_S,	"if-not-role-ends",					"substring matching from the end of role (inverted)" },
	{ R_A|R_S,	"if-not-role-is",					"exact full string matching on role (inverted)" },
	{ R_A|R_S,	"if-not-title-begins",				"substring matching from the start of title (inverted)" },
	{ R_A|R_S,	"if-not-title-contains",			"substring matching on title (inverted)" },
	{ R_A|R_S,	"if-not-title-ends",				"substring matching from the end of title (inverted)" },
	{ R_A|R_S,	"if-not-title-is",					"exact full string matching on title (inverted)" },
	{ R_A|R_S,	"if-not-title-was",					"for deferred rule matching, the exact title prior to changing (inverted)" },
	{ R_A|R_S,	"if-parent-class-begins",			"substring matching from the start of parent's class" },
	{ R_A|R_S,	"if-parent-class-contains",			"substring matching on parent's class" },
	{ R_A|R_S,	"if-parent-class-ends",				"substring matching from the end of parent's class" },
	{ R_A|R_S,	"if-parent-class-is",				"exact full string matching on parent's class" },
	{ R_A|R_S,	"if-parent-instance-begins",		"substring matching from the start of parent's instance" },
	{ R_A|R_S,	"if-parent-instance-contains",		"substring matching on parent's instance" },
	{ R_A|R_S,	"if-parent-instance-ends",			"substring matching from the end of parent's instance" },
	{ R_A|R_S,	"if-parent-instance-is",			"exact full string matching on parent's instance" },
	{ R_A|R_S,	"if-parent-role-begins",			"substring matching from the start of parent's role" },
	{ R_A|R_S,	"if-parent-role-contains",			"substring matching on parent's role" },
	{ R_A|R_S,	"if-parent-role-ends",				"substring matching from the end of parent's role" },
	{ R_A|R_S,	"if-parent-role-is",				"exact full string matching on parent's role" },
	{ R_A|R_S,	"if-parent-title-begins",			"substring matching from the start of parent's title" },
	{ R_A|R_S,	"if-parent-title-contains",			"substring matching on parent's title" },
	{ R_A|R_S,	"if-parent-title-ends",				"substring matching from the end of parent's title" },
	{ R_A|R_S,	"if-parent-title-is",				"exact full string matching on parent's title" },
	{ R_A|R_S,	"if-role-begins",					"substring matching from the start of role" },
	{ R_A|R_S,	"if-role-contains",					"substring matching on role" },
	{ R_A|R_S,	"if-role-ends",						"substring matching from the end of role" },
	{ R_A|R_S,	"if-role-is",						"exact full string matching on role" },
	{ R_A|R_S,	"if-title-begins",					"substring matching from the start of title" },
	{ R_A|R_S,	"if-title-contains",				"substring matching on title" },
	{ R_A|R_S,	"if-title-ends",					"substring matching from the end of title" },
	{ R_A|R_S,	"if-title-is",						"exact full string matching on title" },
	{ R_A|R_S,	"if-title-was",						"for deferred rule matching, the exact title prior to changing" },
	{ R_BOOL,	"log-rule",							"log when a client matches the rule" },
	#if PATCH_FLAG_ACTIVATION_CLICK
	{ R_I,		"set-activation-click",				"send a mouse click of specified button on client activation" },
	#endif // PATCH_FLAG_ACTIVATION_CLICK
	#if PATCH_FLAG_ALWAYSONTOP
	{ R_BOOL,	"set-alwaysontop",					"this client will appear above others; if tiled: only while focused" },
	#endif // PATCH_FLAG_ALWAYSONTOP
	{ R_BOOL,	"set-autofocus",					"whether to auto focus the client (floating clients only), defaults to true" },
	#if PATCH_FLAG_GAME || PATCH_FLAG_HIDDEN || PATCH_FLAG_PANEL
	{ R_BOOL,	"set-autohide",						"whether to minimize/iconify the client when it shouldn't be visible" },
	#endif // PATCH_FLAG_GAME || PATCH_FLAG_HIDDEN || PATCH_FLAG_PANEL
	#if PATCH_FLAG_CAN_LOSE_FOCUS
	{ R_BOOL,	"set-can-lose-focus",				"allow the client to lose focus when active" },
	#endif // PATCH_FLAG_CAN_LOSE_FOCUS
	#if PATCH_FLAG_CENTRED
	{ R_I,		"set-centred",						"1:centre of monitor, 2:centre of parent client" },
	#endif // PATCH_FLAG_CENTRED
	#if PATCH_CFACTS
	{ R_N,		"set-cfact",						"client scale factor, value between 0.25 and 4.0" },
	#endif // PATCH_CFACTS
	#if PATCH_SHOW_MASTER_CLIENT_ON_TAG
	{ R_S,		"set-class-display",				"display this string instead of the class in tag bar" },
	#endif // PATCH_SHOW_MASTER_CLIENT_ON_TAG
	#if PATCH_ALTTAB
	{ R_S,		"set-class-group",					"use this string as class for alttab class switcher" },
	#endif // PATCH_ALTTAB
	#if PATCH_CLASS_STACKING
	{ R_S,		"set-class-stack",					"use this string as class for class stacking" },
	#endif // PATCH_CLASS_STACKING
	#if PATCH_MOUSE_POINTER_HIDING
	{ R_BOOL,	"set-cursor-autohide",				"true to hide cursor when stationary while this client is focused" },
	{ R_BOOL,	"set-cursor-hide-on-keys",			"true to hide cursor when keys are pressed while this client is focused" },
	#endif // PATCH_MOUSE_POINTER_HIDING
	#if PATCH_SHOW_DESKTOP
	{ R_BOOL,	"set-desktop",						"true to make the client a desktop window"},
	#endif // PATCH_SHOW_DESKTOP
	#if PATCH_FLAG_FAKEFULLSCREEN
	{ R_BOOL,	"set-fakefullscreen",				"when going fullscreen this client will be constrained to its tile" },
	#endif // PATCH_FLAG_FAKEFULLSCREEN
	{ R_BOOL,	"set-floating",						"override the default tiling/floating behaviour for this client" },
	{ R_I|R_N,	"set-floating-width",				"floating client width at creation, integer for absolute width, decimal fraction for relative width" },
	{ R_I|R_N,	"set-floating-height",				"floating client height at creation, integer for absolute height, decimal fraction for relative height" },
	#if PATCH_FLAG_FLOAT_ALIGNMENT
	{ R_N|R_I,	"set-floating-x",					"floating client initial position: decimal fraction between 0 and 1 for relative position, OR > 1 for absolute position" },
	{ R_N|R_I,	"set-floating-y",					"floating client initial position: decimal fraction between 0 and 1 for relative position, OR > 1 for absolute position" },
	{ R_N|R_I,	"set-float-align-x",				"floating client fixed alignment: -1:not aligned, decimal fraction between 0 and 1 for relative position" },
	{ R_N|R_I,	"set-float-align-y",				"floating client fixed alignment: -1:not aligned, decimal fraction between 0 and 1 for relative position" },
	#endif // PATCH_FLAG_FLOAT_ALIGNMENT
	#if PATCH_MOUSE_POINTER_WARPING
	{ R_BOOL,	"set-focus-origin-absolute",		"mouse warp locations correspond to absolute pixel coordinates" },
	{ R_N|R_I,	"set-focus-origin-dx",				"mouse warp relative to client centre - x (decimal fraction)" },
	{ R_N|R_I,	"set-focus-origin-dy",				"mouse warp relative to client centre - y (decimal fraction)" },
	#endif // PATCH_MOUSE_POINTER_WARPING
	#if PATCH_FLAG_FOLLOW_PARENT
	{ R_BOOL,	"set-follow-parent",				"true to ensure this client's tags match its parent's, and stays on the same monitor as its parent" },
	#endif // PATCH_FLAG_FOLLOW_PARENT
	#if PATCH_FLAG_GAME
	#if PATCH_FLAG_GAME_STRICT
	{ R_BOOL,	"set-game",							"fullscreen clients will be minimized and unminimized when they lose or gain focus (on the same monitor)" },
	{ R_BOOL,	"set-game-strict",					"fullscreen clients will be minimized and unminimized whenever they lose or gain focus" },
	#else // NO PATCH_FLAG_GAME_STRICT
	{ R_BOOL,	"set-game",							"fullscreen clients will be minimized and unminimized when they lose or gain focus" },
	#endif // PATCH_FLAG_GAME_STRICT
	#endif // PATCH_FLAG_GAME
	#if PATCH_FOCUS_FOLLOWS_MOUSE
	#if PATCH_FLAG_GREEDY_FOCUS
	{ R_BOOL,	"set-greedy-focus",					"client won't lose focus due to mouse movement" },
	#endif // PATCH_FLAG_GREEDY_FOCUS
	#endif // PATCH_FOCUS_FOLLOWS_MOUSE
	#if PATCH_FLAG_HIDDEN
	{ R_BOOL,	"set-hidden",						"client will be hidden by default" },
	#endif // PATCH_FLAG_HIDDEN
	#if PATCH_WINDOW_ICONS
	#if PATCH_WINDOW_ICONS_CUSTOM_ICONS
	{ R_S,		"set-icon",							"the icon image file will be loaded and used instead of the client's icon" },
	#endif // PATCH_WINDOW_ICONS_CUSTOM_ICONS
	#endif // PATCH_WINDOW_ICONS
	#if PATCH_FLAG_IGNORED
	{ R_BOOL,	"set-ignored",						"client will be ignored from stacking, focus, alt-tab, etc." },
	#endif // PATCH_FLAG_IGNORED
	#if PATCH_WINDOW_ICONS
	#if PATCH_WINDOW_ICONS_CUSTOM_ICONS
	{ R_S,		"set-missing-icon",					"the icon image file will be loaded and used for the client instead of no icon" },
	#endif // PATCH_WINDOW_ICONS_CUSTOM_ICONS
	#endif // PATCH_WINDOW_ICONS
	#if PATCH_MODAL_SUPPORT
	{ R_BOOL,	"set-modal",						"client will be marked as modal (for when clients implement modality improperly)" },
	#endif // PATCH_MODAL_SUPPORT
	{ R_I,		"set-monitor",						"set monitor number (0+) for this client" },
	{ R_BOOL,	"set-never-focus",					"prevent the client from being focused automatically" },
	#if PATCH_FLAG_NEVER_FULLSCREEN
	{ R_BOOL,	"set-never-fullscreen",				"prevent the client from being made fullscreen" },
	#endif // PATCH_FLAG_NEVER_FULLSCREEN
	#if PATCH_FLAG_NEVER_MOVE
	{ R_BOOL,	"set-never-move",					"prevent the application from moving the client" },
	#endif // PATCH_FLAG_NEVER_MOVE
	#if PATCH_FLAG_PARENT
	{ R_BOOL,	"set-never-parent",					"prevent the client from being treated as the parent to any other" },
	#endif // PATCH_FLAG_PARENT
	#if PATCH_FLAG_NEVER_RESIZE
	{ R_BOOL,	"set-never-resize",					"prevent the application from resizing the client" },
	#endif // PATCH_FLAG_NEVER_RESIZE
	#if PATCH_ATTACH_BELOW_AND_NEWMASTER
	{ R_BOOL,	"set-newmaster",					"client always created as a new master, otherwise client goes onto the stack" },
	#endif // PATCH_ATTACH_BELOW_AND_NEWMASTER
	#if PATCH_TERMINAL_SWALLOWING
	{ R_BOOL,	"set-noswallow",					"never swallow this client" },
	#endif // PATCH_TERMINAL_SWALLOWING
	#if PATCH_CLIENT_OPACITY
	{ R_N|R_I,	"set-opacity-active",				"level of opacity for client when active" },
	{ R_N|R_I,	"set-opacity-inactive",				"level of opacity for client when inactive" },
	#endif // PATCH_CLIENT_OPACITY
	{ R_BOOL,	"set-panel",						"client is a floating panel window, whose visibility will match the bar's; excluded from mouse warp focus, stacking, alt-tab" },
	#if PATCH_FLAG_PARENT
	{ R_A|R_S,	"set-parent-begins",				"treat client as if its parent is the specified window (same class if rule deferred) - substring match from the start" },
	{ R_A|R_S,	"set-parent-contains",				"treat client as if its parent is the specified window (same class if rule deferred) - substring match" },
	{ R_A|R_S,	"set-parent-ends",					"treat client as if its parent is the specified window (same class if rule deferred) - substring match from the end" },
	{ R_BOOL,	"set-parent-guess",					"treat client as if its parent is the client that was focused when it was mapped, or the most recently focused (use with caution)" },
	{ R_A|R_S,	"set-parent-is",					"treat client as if its parent is the specified window (same class if rule deferred) - exact name match" },
	#endif // PATCH_FLAG_PARENT
	#if PATCH_FLAG_PAUSE_ON_INVISIBLE
	{ R_BOOL,	"set-pause-on-invisible",			"client process will be sent SIGSTOP when not visible, and SIGCONT when visible, killed, or unmanaged" },
	#endif // PATCH_FLAG_PAUSE_ON_INVISIBLE
	#if PATCH_FLAG_STICKY
	{ R_BOOL,	"set-sticky",						"client appears on all tags" },
	#endif // PATCH_FLAG_STICKY
	{ R_I,		"set-tags-mask",					"sets the tag mask applied to the client" },
	#if PATCH_TERMINAL_SWALLOWING
	{ R_BOOL,	"set-terminal",						"true to indicate this client is a terminal" },
	#endif // PATCH_TERMINAL_SWALLOWING
	#if PATCH_FLAG_TITLE
	{ R_S,		"set-title",						"show the specified title in place of the client's" },
	#endif // PATCH_FLAG_TITLE
	{ R_BOOL,	"set-top-level",					"true to indicate this client should be treated as top level (ultimate parent)" },
	{ R_BOOL,	"set-urgent",						"clients will be focused when created, switching tag view if necessary" },
};


#if PATCH_MOUSE_POINTER_WARPING
	/* mouse warp */
	#define MOUSE_WARP_MILLISECONDS	1250
#endif // PATCH_MOUSE_POINTER_WARPING

#if PATCH_ALTTAB
	#define ALTTAB_SELMON_MASK		((1 << 8)-1)
	/* alt-tab styles */
	#define ALTTAB_NORMAL			(1 << 8)
	#define ALTTAB_REVERSE			(1 << 9)
	#define ALTTAB_SAME_CLASS		(1 << 10)
	#define ALTTAB_ALL_TAGS			(1 << 11)
	#define ALTTAB_ALL_MONITORS		(1 << 12)
	#define ALTTAB_MOUSE            (1 << 13)
	#define ALTTAB_BOTTOMBAR		(1 << 9)
#if PATCH_FLAG_HIDDEN
	#define ALTTAB_HIDDEN			(1 << 14)
#endif // PATCH_FLAG_HIDDEN
	#define ALTTAB_OFFSET_MENU		(1 << 15)
	#define ALTTAB_SORTED			(1 << 24)
	#define ALTTAB_SORTED_BY_MONITOR (1 << 25)
	#define ALTTAB_SYSTEM_RESERVED	(1 << 31)
#endif // PATCH_ALTTAB

#if PATCH_MOVE_FLOATING_WINDOWS
	#define MOVE_FLOATING_LEFT		(1 << 0)
	#define MOVE_FLOATING_RIGHT		(1 << 1)
	#define MOVE_FLOATING_UP		(1 << 2)
	#define MOVE_FLOATING_DOWN		(1 << 3)
	#define MOVE_FLOATING_BIGGER	(1 << 4)
#endif // PATCH_MOVE_FLOATING_WINDOWS


/* killgroup types */
#define KILLGROUP_BY_NAME		(1 << 0)
#define KILLGROUP_BY_CLASS		(1 << 1)
#define KILLGROUP_BY_INSTANCE	(1 << 2)

/* applyrules string match types */
#define APPLYRULES_STRING_EXACT  	1
#define APPLYRULES_STRING_CONTAINS	2
#define APPLYRULES_STRING_BEGINS	3
#define APPLYRULES_STRING_ENDS  	4

#if PATCH_FLAG_FLOAT_ALIGNMENT
#define FLOAT_ALIGNED_X			(1 << 0)
#define	FLOAT_ALIGNED_Y			(1 << 1)
#endif // PATCH_FLAG_FLOAT_ALIGNMENT

static int rc = EXIT_SUCCESS;

//#if PATCH_FOCUS_FOLLOWS_MOUSE || PATCH_MOUSE_POINTER_HIDING
#if PATCH_MOUSE_POINTER_HIDING
static int motion_type = -1;
static int device_change_type = -1;
static long last_device_change = -1;
#endif // PATCH_MOUSE_POINTER_HIDING
//#endif // PATCH_FOCUS_FOLLOWS_MOUSE || PATCH_MOUSE_POINTER_HIDING
#if PATCH_MOUSE_POINTER_HIDING
/*
typedef struct wp_data wp_data;
struct wp_data {
	int ;
};
*/
static int cursorhiding = 0;
static int cursor_always_hide = 0, ignore_scroll = 0;
static unsigned char cursor_ignore_mods = (ShiftMask | ControlMask | Mod1Mask | Mod4Mask | Mod5Mask);
static XSyncCounter counter_idletime = 0;
static XSyncAlarm cursor_idle_alarm = None;
static int timer_sync_event;
static int cursormove_x = -1, cursormove_y = -1;
#if 0
static int cursormove = 0, cursormove_custom_x, cursormove_custom_y, cursormove_custom_mask;
enum cursormove_types {
	MOVE_NW = 1,
	MOVE_NE,
	MOVE_SW,
	MOVE_SE,
	MOVE_WIN_NW,
	MOVE_WIN_NE,
	MOVE_WIN_SW,
	MOVE_WIN_SE,
	MOVE_CUSTOM,
};
#endif
static int button_press_type = -1;
static int button_release_type = -1;
static int key_release_type = -1;
#endif // PATCH_MOUSE_POINTER_HIDING

#define ModKeyNoRepeatMask	(1<<15)
#if PATCH_KEY_HOLD
#define ModKeyHoldMask		(1<<14)
#endif // PATCH_KEY_HOLD

#if DEBUGGING
static int skip_rules = 0;
static int debug_sensitivity_on = 0;
#define DEBUGIF if (debug_sensitivity_on) {
#define DEBUGENDIF }
#else // NO DEBUGGING
#define DEBUGIF if (0) {
#define DEBUGENDIF }
#endif // DEBUGGING

/* macros */
#define BUTTONMASK              (ButtonPressMask|ButtonReleaseMask)
#define CLEANMASK(mask)         (mask & ~(numlockmask|LockMask) & (ShiftMask|ControlMask|Mod1Mask|Mod2Mask|Mod3Mask|Mod4Mask|Mod5Mask))
#define INTERSECT(x,y,w,h,m)    (MAX(0, MIN((x)+(w),(m)->wx+(m)->ww) - MAX((x),(m)->wx)) \
                               * MAX(0, MIN((y)+(h),(m)->wy+(m)->wh) - MAX((y),(m)->wy)))
#define INTERSECTC(x,y,w,h,z)   (MAX(0, MIN((x)+(w),(z)->x+(z)->w) - MAX((x),(z)->x)) \
                               * MAX(0, MIN((y)+(h),(z)->y+(z)->h) - MAX((y),(z)->y)))
#define ISBOOLEAN(x)			(x == 0 || x == 1)

#if PATCH_FLAG_IGNORED
	#if PATCH_FLAG_STICKY
		#if PATCH_SHOW_DESKTOP
			#define ISVISIBLEONTAG(C, T)    (((C->tags & T) || C->issticky || C->isdesktop) && !C->isignored && !C->dormant)
		#else // NO PATCH_SHOW_DESKTOP
			#define ISVISIBLEONTAG(C, T)    (((C->tags & T) || C->issticky) && !C->isignored && !C->dormant)
		#endif // PATCH_SHOW_DESKTOP
	#else // NO PATCH_FLAG_STICKY
		#if PATCH_SHOW_DESKTOP
			#define ISVISIBLEONTAG(C, T)    (((C->tags & T) || C->isdesktop) && !C->isignored && !C->dormant)
		#else // NO PATCH_SHOW_DESKTOP
			#define ISVISIBLEONTAG(C, T)    ((C->tags & T) && !C->isignored && !C->dormant)
		#endif // PATCH_SHOW_DESKTOP
	#endif // PATCH_FLAG_STICKY
#else // NO PATCH_FLAG_IGNORED
	#if PATCH_FLAG_STICKY
		#if PATCH_SHOW_DESKTOP
			#define ISVISIBLEONTAG(C, T)    (((C->tags & T) || C->issticky || C->isdesktop) && !C->dormant)
		#else // NO PATCH_SHOW_DESKTOP
			#define ISVISIBLEONTAG(C, T)    (((C->tags & T) || C->issticky) && !C->dormant)
		#endif // PATCH_SHOW_DESKTOP
	#else // NO PATCH_FLAG_STICKY
		#if PATCH_SHOW_DESKTOP
			#define ISVISIBLEONTAG(C, T)    (((C->tags & T) || C->isdesktop) && !C->dormant)
		#else // NO PATCH_SHOW_DESKTOP
			#define ISVISIBLEONTAG(C, T)    ((C->tags & T) && !C->dormant)
		#endif // PATCH_SHOW_DESKTOP
	#endif // PATCH_FLAG_STICKY
#endif // PATCH_FLAG_IGNORED

#if PATCH_SHOW_DESKTOP
	#define ISVISIBLE(C)            desktopvalid(C)
#else // NO PATCH_SHOW_DESKTOP
	#define ISVISIBLE(C)            ISVISIBLEONTAG(C, C->mon->tagset[C->mon->seltags])
#endif // PATCH_SHOW_DESKTOP

#if PATCH_FLAG_IGNORED
	#define MINIMIZED(C)            ((getstate(C->win) == IconicState) && !C->isignored && !C->dormant)
#else // NO PATCH_FLAG_IGNORED
	#define MINIMIZED(C)            ((getstate(C->win) == IconicState) && !C->dormant)
#endif // PATCH_FLAG_IGNORED

#define LENGTH(X)               (sizeof X / sizeof X[0])
#define MOUSEMASK               (BUTTONMASK|PointerMotionMask)
#define WIDTH(X)                ((X)->w + 2 * (X)->bw)
#define HEIGHT(X)               ((X)->h + 2 * (X)->bw)
#define TAGMASK                 ((1 << LENGTH(tags)) - 1)
#define TEXTW(X)                (drw_fontset_getwidth(drw, (X)) + lrpad)
#define OPAQUE                  0xffU

#define NOT_STRINGMATCH(JSON,VARNAME,sz_VARNAME,TEXT)	( \
	(!cJSON_HasObjectItem(JSON, TEXT"-is") || napplyrules_stringtest(cJSON_GetObjectItemCaseSensitive(JSON, TEXT"-is"), VARNAME, sz_VARNAME, APPLYRULES_STRING_EXACT)) && \
	(!cJSON_HasObjectItem(JSON, TEXT"-contains") || napplyrules_stringtest(cJSON_GetObjectItemCaseSensitive(JSON, TEXT"-contains"), VARNAME, sz_VARNAME, APPLYRULES_STRING_CONTAINS)) && \
	(!cJSON_HasObjectItem(JSON, TEXT"-begins") || napplyrules_stringtest(cJSON_GetObjectItemCaseSensitive(JSON, TEXT"-begins"), VARNAME, sz_VARNAME, APPLYRULES_STRING_BEGINS)) && \
	(!cJSON_HasObjectItem(JSON, TEXT"-ends") || napplyrules_stringtest(cJSON_GetObjectItemCaseSensitive(JSON, TEXT"-ends"), VARNAME, sz_VARNAME, APPLYRULES_STRING_ENDS)) \
)
#define STRINGMATCH(JSON,VARNAME,sz_VARNAME,TEXT)	( \
	(!cJSON_HasObjectItem(JSON, TEXT"-is") || applyrules_stringtest(cJSON_GetObjectItemCaseSensitive(JSON, TEXT"-is"), VARNAME, sz_VARNAME, APPLYRULES_STRING_EXACT)) && \
	(!cJSON_HasObjectItem(JSON, TEXT"-contains") || applyrules_stringtest(cJSON_GetObjectItemCaseSensitive(JSON, TEXT"-contains"), VARNAME, sz_VARNAME, APPLYRULES_STRING_CONTAINS)) && \
	(!cJSON_HasObjectItem(JSON, TEXT"-begins") || applyrules_stringtest(cJSON_GetObjectItemCaseSensitive(JSON, TEXT"-begins"), VARNAME, sz_VARNAME, APPLYRULES_STRING_BEGINS)) && \
	(!cJSON_HasObjectItem(JSON, TEXT"-ends") || applyrules_stringtest(cJSON_GetObjectItemCaseSensitive(JSON, TEXT"-ends"), VARNAME, sz_VARNAME, APPLYRULES_STRING_ENDS)) \
)
#define STRINGMATCHABLE(JSON,TEXT)	( \
	cJSON_HasObjectItem(JSON, TEXT"-is") || \
	cJSON_HasObjectItem(JSON, TEXT"-contains") || \
	cJSON_HasObjectItem(JSON, TEXT"-begins") || \
	cJSON_HasObjectItem(JSON, TEXT"-ends") \
)



#define SYSTEM_TRAY_REQUEST_DOCK    0
/* XEMBED messages */
#define XEMBED_EMBEDDED_NOTIFY      0
#define XEMBED_WINDOW_ACTIVATE      1
#define XEMBED_FOCUS_IN             4
#define XEMBED_MODALITY_ON         10
#define XEMBED_MAPPED              (1 << 0)
#define XEMBED_WINDOW_ACTIVATE      1
#define XEMBED_WINDOW_DEACTIVATE    2
#define VERSION_MAJOR               0
#define VERSION_MINOR               0
#define XEMBED_EMBEDDED_VERSION (VERSION_MAJOR << 16) | VERSION_MINOR

/* enums */
enum {	CurResizeBR, CurResizeBL, CurResizeTR, CurResizeTL,
		CurNormal, CurResize, CurResizeH, CurResizeV, CurMove, CurDragFact, CurScroll,
		#if PATCH_ALTTAB
		CurBusy,
		#endif // PATCH_ALTTAB
		#if PATCH_TORCH
		CurInvisible,
		#endif // PATCH_TORCH
		CurLast }; /* cursor */
enum {	SchemeNorm, SchemeSel
		#if PATCH_TWO_TONE_TITLE
		, SchemeSel2
		#endif // PATCH_TWO_TONE_TITLE
		#if PATCH_ALTTAB
		, SchemeTabNorm, SchemeTabSel, SchemeTabUrg
		#endif // PATCH_ALTTAB
		, SchemeUrg
		#if PATCH_FLAG_HIDDEN || PATCH_SHOW_DESKTOP
		, SchemeHide
		#endif // PATCH_FLAG_HIDDEN || PATCH_SHOW_DESKTOP
		#if PATCH_ALTTAB
		#if PATCH_FLAG_HIDDEN
		, SchemeTabHide
		#endif // PATCH_FLAG_HIDDEN
		#endif // PATCH_ALTTAB
		#if PATCH_TORCH
		, SchemeTorch
		#endif // PATCH_TORCH
		#if PATCH_COLOUR_BAR
		, SchemeTagBar, SchemeTagBarSel
		#if PATCH_FLAG_HIDDEN
		, SchemeTagBarHide
		#endif // PATCH_FLAG_HIDDEN
		, SchemeLayout
		, SchemeTitle, SchemeTitleSel
		, SchemeStatus
		#endif // PATCH_COLOUR_BAR
		#if PATCH_RAINBOW_TAGS
		, SchemeTag1, SchemeTag2, SchemeTag3, SchemeTag4, SchemeTag5, SchemeTag6
		, SchemeTag7, SchemeTag8, SchemeTag9
		#endif // PATCH_RAINBOW_TAGS
		#if PATCH_STATUSCMD
		#if PATCH_STATUSCMD_COLOURS
		, SchemeStatC1, SchemeStatC2, SchemeStatC3, SchemeStatC4, SchemeStatC5
		, SchemeStatC6, SchemeStatC7, SchemeStatC8, SchemeStatC9, SchemeStatC10
		, SchemeStatC11, SchemeStatC12, SchemeStatC13, SchemeStatC14, SchemeStatC15
		, SchemeStatusCmd
		#endif // PATCH_STATUSCMD_COLOURS
		#if PATCH_STATUSCMD_NONPRINTING
		, SchemeStatCNP
		#endif // PATCH_STATUSCMD_NONPRINTING
		#endif // PATCH_STATUSCMD
}; // colour schemes;
enum {	NetSupported, NetWMName,
		#if PATCH_WINDOW_ICONS
		NetWMIcon,
		#endif // PATCH_WINDOW_ICONS
		NetWMCheck, NetWMState, NetWMAttention,
		#if PATCH_FLAG_ALWAYSONTOP
		NetWMStaysOnTop,
		#endif // PATCH_FLAG_ALWAYSONTOP
		#if PATCH_FLAG_HIDDEN
		NetWMHidden,
		#endif // PATCH_FLAG_HIDDEN
		#if PATCH_MODAL_SUPPORT
		NetWMModal,
		#endif // PATCH_MODAL_SUPPORT
		#if PATCH_FLAG_STICKY
		NetWMSticky,
		#endif // PATCH_FLAG_STICKY
		#if PATCH_LOG_DIAGNOSTICS
		NetWMAbove,
		NetWMBelow,
		NetWMMaximizedH,
		NetWMMaximizedV,
		NetWMShaded,
		NetWMSkipPager,
		NetWMSkipTaskbar,
		#endif // PATCH_LOG_DIAGNOSTICS
		// add _NET_WM_STATE flags here;
		NetWMFullscreen,
		NetSystemTray, NetSystemTrayOP, NetSystemTrayOrientation, NetSystemTrayOrientationHorz, NetSystemTrayVisual,
		NetActiveWindow, NetWMWindowType,
		#if PATCH_SHOW_DESKTOP
		NetWMWindowTypeDesktop,
		#endif // PATCH_SHOW_DESKTOP
		NetWMWindowTypeDialog, NetWMWindowTypeDock, NetWMWindowTypeSplash,
		#if PATCH_ALTTAB
		NetWMWindowTypeMenu, NetWMWindowTypePopupMenu,
		#endif // PATCH_ALTTAB
		#if PATCH_EWMH_TAGS
		NetDesktopNames, NetDesktopViewport, NetNumberOfDesktops, NetCurrentDesktop,
		#endif // PATCH_EWMH_TAGS
		#if PATCH_CLIENT_OPACITY
		NetWMWindowsOpacity,
		#endif // PATCH_CLIENT_OPACITY
		NetClientList, NetClientInfo, NetLast }; /* EWMH atoms */
enum {	Manager, Xembed, XembedInfo, XLast }; /* Xembed atoms */
enum {	WMProtocols, WMDelete, WMState, WMTakeFocus, WMWindowRole, WMLast }; /* default atoms */
enum {	ClkTagBar, ClkLtSymbol, ClkStatusText, ClkWinTitle,
		ClkClientWin, ClkRootWin,
		#if PATCH_SHOW_DESKTOP_BUTTON
		ClkShowDesktop,
		#endif // PATCH_SHOW_DESKTOP_BUTTON
		ClkLast }; /* clicks */

enum {	NoElement = 0, StatusText, LtSymbol, TagBar, WinTitle
		#if PATCH_SHOW_DESKTOP_BUTTON
		,ShowDesktop
		#endif // PATCH_SHOW_DESKTOP_BUTTON
		};	// bar structural element type;
typedef struct {
	const char *name;
	unsigned int type;
} BarElementType;
static const BarElementType BarElementTypes[] = {
	{ NULL,			NoElement },
	{ "TagBar",		TagBar },
	{ "LtSymbol",	LtSymbol },
	{ "WinTitle",	WinTitle },
	{ "StatusText",	StatusText },
	#if PATCH_SHOW_DESKTOP_BUTTON
	{ "ShowDesktop",ShowDesktop },
	#endif // PATCH_SHOW_DESKTOP_BUTTON
};
typedef struct BarElement BarElement;
struct BarElement {
	unsigned int type;
	int x;
	unsigned int w;
};

#if PATCH_FOCUS_BORDER
enum {	FOCUS_BORDER_N, FOCUS_BORDER_E, FOCUS_BORDER_S, FOCUS_BORDER_W };
#elif PATCH_FOCUS_PIXEL
enum {	FOCUS_PIXEL_SE = 1, FOCUS_PIXEL_SW, FOCUS_PIXEL_NW, FOCUS_PIXEL_NE };
#endif // PATCH_FOCUS_BORDER || PATCH_FOCUS_PIXEL


#if PATCH_IPC
typedef struct TagState TagState;
struct TagState {
	int selected;
	int occupied;
	int urgent;
};

typedef struct ClientState ClientState;
struct ClientState {
	int isfixed, isfloating, isurgent, neverfocus, oldstate, isfullscreen;
};
#endif // PATCH_IPC

typedef union {
	#if PATCH_IPC
	long i;
	unsigned long ui;
	#else // NO PATCH_IPC
	int i;
	unsigned int ui;
	#endif // PATCH_IPC
	float f;
	const void *v;
} Arg;

typedef struct {
	unsigned int click;
	unsigned int mask;
	unsigned int button;
	void (*func)(const Arg *arg);
	const Arg arg;
} Button;

typedef struct Monitor Monitor;
typedef struct Client Client;
struct Client {
	char name[256];
	#if PATCH_FLAG_TITLE
	char *displayname;
	#endif // PATCH_FLAG_TITLE
	int toplevel;
	#if PATCH_SHOW_MASTER_CLIENT_ON_TAG
	char *dispclass;
	#endif // PATCH_SHOW_MASTER_CLIENT_ON_TAG
	#if PATCH_ALTTAB
	char *grpclass;
	#endif // PATCH_ALTTAB
	#if PATCH_CLASS_STACKING
	char *stackclass;
	#endif // PATCH_CLASS_STACKING

	#if PATCH_MOUSE_POINTER_HIDING
	int cursorautohide;
	int cursorhideonkeys;
	#endif // PATCH_MOUSE_POINTER_HIDING
	long stackorder;
	float mina, maxa;
	#if PATCH_CFACTS
	float cfact;
	#endif // PATCH_CFACTS
	int x, y, w, h;
	int sfx, sfy, sfw, sfh; /* stored float geometry, used on mode revert */
	float sfxo, sfyo; /* float origin geometry relative to parent origin */
	int oldx, oldy, oldw, oldh;
	int basew, baseh, incw, inch, maxw, maxh, minw, minh, hintsvalid;
	int bw, oldbw;
	unsigned int tags;
	int dormant;
	int isfixed, isfloating, isurgent;
	#if PATCH_CLASS_STACKING
	Client *stackhead;		// tiled window whose position is to be matched;
	int isstackhead;
	#endif // PATCH_CLASS_STACKING
	#if PATCH_SHOW_DESKTOP
	int wasdesktop;		// started with NetWMWindowTypeDesktop;
	int isdesktop;		// desktop client
	int ondesktop;		// client's parent or ultimate parent is the desktop;
	#endif // PATCH_SHOW_DESKTOP
	#if PATCH_CLIENT_OPACITY
	double opacity;
	double unfocusopacity;
	#endif // PATCH_CLIENT_OPACITY
	#if PATCH_PAUSE_PROCESS
	int paused;
	#endif // PATCH_PAUSE_PROCESS
	int neverfocus;
	#if PATCH_FLAG_NEVER_FOCUS
	int neverfocus_override;
	#endif // PATCH_FLAG_NEVER_FOCUS
	int oldstate, isfullscreen, lostfullscreen
		#if PATCH_TERMINAL_SWALLOWING
		, isterminal , noswallow
		#endif // PATCH_TERMINAL_SWALLOWING
	;
	#if PATCH_FLAG_FLOAT_ALIGNMENT
	float floatingx; // -1=default, fractional between 0 and 1 for relative position;
	float floatingy; // -1=default, fractional between 0 and 1 for relative position;
	float floatalignx; // -1=default, fractional between 0 and 1 for relative position;
	float floataligny; // -1=default, fractional between 0 and 1 for relative position;
	#endif // PATCH_FLAG_FLOAT_ALIGNMENT
	int autofocus; // applies floating windows;
	#if PATCH_FLAG_GAME || PATCH_FLAG_HIDDEN || PATCH_FLAG_PANEL
	int autohide; // iconify when the client is hidden by showhide();
	#endif // PATCH_FLAG_GAME || PATCH_FLAG_HIDDEN || PATCH_FLAG_PANEL
	#if PATCH_FOCUS_FOLLOWS_MOUSE
	#if PATCH_FLAG_GREEDY_FOCUS
	int isgreedy;	// client doesn't want to lose focus due to mouse movement;
	#endif // PATCH_FLAG_GREEDY_FOCUS
	#endif // PATCH_FOCUS_FOLLOWS_MOUSE
	#if PATCH_MODAL_SUPPORT
	int ismodal;
	int ismodal_override;
	#endif // PATCH_MODAL_SUPPORT
	#if PATCH_FLAG_STICKY
	int issticky;
	#endif // PATCH_FLAG_STICKY
	#if PATCH_FLAG_CAN_LOSE_FOCUS
	int canlosefocus;
	#endif // PATCH_FLAG_CAN_LOSE_FOCUS
	#if PATCH_FLAG_CENTRED
	int iscentred;
	int iscentred_override;
	#endif // PATCH_FLAG_CENTRED
	#if PATCH_MOUSE_POINTER_WARPING
	float focusdx;
	float focusdy;
	int focusabs;
	#if PATCH_MOUSE_POINTER_WARPING_RECALL
	int lastdx;
	int lastdy;
	int nolastcoords;
	#endif // PATCH_MOUSE_POINTER_WARPING_RECALL
	#endif // PATCH_MOUSE_POINTER_WARPING
	#if PATCH_ATTACH_BELOW_AND_NEWMASTER
	int newmaster;
	#endif // PATCH_ATTACH_BELOW_AND_NEWMASTER
	#if PATCH_FLAG_NEVER_MOVE
	int nevermove;
	#endif // PATCH_FLAG_NEVER_MOVE
	#if PATCH_FLAG_NEVER_RESIZE
	int neverresize;
	#endif // PATCH_FLAG_NEVER_RESIZE
	#if PATCH_FLAG_NEVER_FULLSCREEN
	int neverfullscreen;
	#endif // PATCH_FLAG_NEVER_FULLSCREEN
	#if PATCH_FLAG_ACTIVATION_CLICK
	int activationclick;
	#endif // PATCH_FLAG_ACTIVATION_CLICK
	#if PATCH_FLAG_ALWAYSONTOP
	int alwaysontop;
	#endif // PATCH_FLAG_ALWAYSONTOP
	#if PATCH_FLAG_FAKEFULLSCREEN
	int fakefullscreen;
	#endif // PATCH_FLAG_FAKEFULLSCREEN
	int isfloating_override;
	#if PATCH_FLAG_GAME
	int isgame;
	#if PATCH_FLAG_GAME_STRICT
	int isgamestrict;
	#endif // PATCH_FLAG_GAME_STRICT
	#endif // PATCH_FLAG_GAME
	#if PATCH_FLAG_HIDDEN
	int ishidden;
	#endif // PATCH_FLAG_HIDDEN
	#if PATCH_FLAG_PANEL
	int ispanel;
	#endif // PATCH_FLAG_PANEL
	#if PATCH_FLAG_IGNORED
	int isignored;
	#endif // PATCH_FLAG_IGNORED
	int ruledefer; // reapply rules if/when the title changes;
	pid_t pid;
	int beingmoved;
	#if PATCH_HANDLE_SIGNALS
	int sigtermcount;
	#endif // PATCH_HANDLE_SIGNALS
	#if PATCH_WINDOW_ICONS
	unsigned int icw, ich;
	Picture icon;
	#if PATCH_WINDOW_ICONS_CUSTOM_ICONS
	//Imlib_Image *custom_icon;
	char *icon_file;
	int icon_replace;
	#endif // PATCH_WINDOW_ICONS_CUSTOM_ICONS
	#if PATCH_ALTTAB
	unsigned int alticw, altich;
	Picture alticon;
	#endif // PATCH_ALTTAB
	#endif // PATCH_WINDOW_ICONS
	#if (PATCH_WINDOW_ICONS && PATCH_WINDOW_ICONS_ON_TAGS) || PATCH_CUSTOM_TAG_ICONS
	unsigned int tagicw, tagich;
	Picture tagicon;
	#endif // (PATCH_WINDOW_ICONS && PATCH_WINDOW_ICONS_ON_TAGS) || PATCH_CUSTOM_TAG_ICONS
	#if PATCH_FLAG_PARENT
	int neverparent;
	int parent_late;	// -1, 0 (parent changed), 1 (awaiting parent client);
	cJSON *parent_condition_node;	// pointer to the rule node containing the parent_ condition elements;
	cJSON *parent_is;
	cJSON *parent_begins;
	cJSON *parent_contains;
	cJSON *parent_ends;
	#endif // PATCH_FLAG_PARENT
	#if PATCH_FLAG_FOLLOW_PARENT
	int followparent;
	#endif // PATCH_FLAG_FOLLOW_PARENT
	Client *parent;
	int fosterparent;
	Client *ultparent;
	Client *next;
	Client *sprev;
	Client *snext;
	Client *prevsel;
	#if PATCH_FLAG_PAUSE_ON_INVISIBLE
	int pauseinvisible;
	#endif // PATCH_FLAG_PAUSE_ON_INVISIBLE
	#if PATCH_TERMINAL_SWALLOWING
	Client *swallowing;
	#endif // PATCH_TERMINAL_SWALLOWING
	Monitor *mon;
	int monindex;	// keep the monitor number;
	Window win;
	#if PATCH_CROP_WINDOWS
	Client *crop;
	#endif // PATCH_CROP_WINDOWS
	int index;
	#if PATCH_IPC
	ClientState prevstate;
	#endif // PATCH_IPC
};

typedef struct SortNode SortNode;
struct SortNode {
	Client *client;
	struct SortNode *next;
};

typedef struct {
	unsigned int mod;
	KeySym keysym;
	void (*func)(const Arg *);
	const Arg arg;
	const char *description;
} Key;

typedef struct {
	const char *symbol;
	void (*arrange)(Monitor *);
} Layout;

/* tagging */
static char *tags[] = { "1", "2", "3", "4", "5", "6", "7", "8", "9" };
//static char *tags[] = { "󾠮", "󾠯", "󾠰", "󾠱", "󾠲", "󾠳", "󾠴", "󾠵", "󾠶" };
#if PATCH_CUSTOM_TAG_ICONS
static char *tagiconpaths[] = {
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL
};
#endif // PATCH_CUSTOM_TAG_ICONS

#if PATCH_PERTAG
typedef struct Pertag Pertag;
#endif // PATCH_PERTAG

#if PATCH_VIRTUAL_MONITORS
// physical monitor;
typedef struct PMonitor PMonitor;
struct PMonitor {
	int mx, my, mw, mh;		// physical geometry;
	int disappeared;		// 1 if the physical monitor has been removed;
	Monitor *mon1;			// first (virtual) monitor;
	Monitor *mon2;			// second (virtual) monitor;
	PMonitor *next;			// next physical monitor;
};

// virtual monitor;
#endif // PATCH_VIRTUAL_MONITORS
struct Monitor {
	#if PATCH_VIRTUAL_MONITORS
	int enablesplit;
	int split;				// 1 for horizontal, 2 for vertical;
	PMonitor *pmon;			// the physical monitor;
	#endif // PATCH_VIRTUAL_MONITORS
	#if PATCH_FONT_GROUPS
	int bh;					// bar height;
	int minbh;				// minimum bar height;
	cJSON *barelement_fontgroups_json;
	#endif // PATCH_FONT_GROUPS
	char ltsymbol[16];
	#if PATCH_ALTTAB
	char numstr[16];		// buffer for number formatted e.g. "[%#2d] "
	#endif // PATCH_ALTTAB

	BarElement bar[LENGTH(BarElementTypes)];			// keep each bar element's position and size;
	unsigned int barlayout[LENGTH(BarElementTypes)];	// the order of bar elements;
	unsigned int title_align;	// 0=left,1=centre,2=right alignment for WinTitle element;

	#if PATCH_SHOW_DESKTOP
	int showdesktop;		// 1 while desktop is visible;
	#if PATCH_ALTTAB
	int altTabDesktop;
	#endif // PATCH_ALTTAB
	#endif // PATCH_SHOW_DESKTOP
	#if PATCH_HIDE_VACANT_TAGS
	int hidevacant;
	int alwaysvisible[LENGTH(tags)];
	#endif // PATCH_HIDE_VACANT_TAGS
	#if PATCH_CLIENT_OPACITY
	double activeopacity;
	double inactiveopacity;
	#endif // PATCH_CLIENT_OPACITY
	#if PATCH_MOUSE_POINTER_HIDING
	int cursorautohide; 	// autohide the cursor when stationary;
	int cursorhideonkeys;	// autohide the cursor when keys are released;
	#endif // PATCH_MOUSE_POINTER_HIDING
	float mfact;
	float mfact_def;
	int nmaster;
	int num;
	int by;               /* bar geometry */
	int mx, my, mw, mh;   /* screen size */
	int wx, wy, ww, wh;   /* window area  */
	#if PATCH_CLASS_STACKING
	int class_stacking;
	#endif // PATCH_CLASS_STACKING
	#if PATCH_ALTTAB
	int altTabN;		  /* move that many clients forward */
	int altTabIndex;      /* currently highlighted index */
	int altTabVStart;     /* index of first visible entry */
	Client *highlight;    /* holds the currently highlighted client */
	int vTabs;            /* visible tabs */
	Client *altTabSel;    /* holds the selected client prior to alt tabbing */
	int tx, ty, tih;      /* alttab window coords, tabitem height */
	int nTabs;			  /* number of active clients in tag */
	int tabBW;
	int tabTextAlign;
	int tabMaxW;
	int tabMaxH;
	int tabPosX;
	int tabPosY;
	unsigned int isAlt;   /* ALTTAB_* flags */
	unsigned int altTabSelTags;
	int maxWTab;		// calculated to fit within the monitor;
	int maxHTab;		//
	Client ** altsnext; // array of all clients in the tag;
	Window tabwin;
	#endif // PATCH_ALTTAB
	int gappih;           /* horizontal gap between windows */
	int gappiv;           /* vertical gap between windows */
	int gappoh;           /* horizontal outer gaps */
	int gappov;           /* vertical outer gaps */
	#if PATCH_MIRROR_LAYOUT
	int mirror;
	#endif // PATCH_MIRROR_LAYOUT
	unsigned int seltags;
	unsigned int sellt;
	unsigned int tagset[2];
	int showbar;
	int topbar;
	#if PATCH_CLIENT_INDICATORS
	int client_ind_top;		// boolean, show client indicators at the top of the bar;
	#endif // PATCH_CLIENT_INDICATORS
	Client *clients;
	Client *sel;
	Client *stack;
	Monitor *next;
	Window barwin;
	int barvisible;
	const Layout *lt[2];
	#if PATCH_IPC
	Client *lastsel;
	const Layout *lastlt;
	char lastltsymbol[16];
	TagState tagstate;
	#endif // PATCH_IPC
	#if PATCH_SWITCH_TAG_ON_EMPTY
	unsigned int switchonempty;
	#endif // PATCH_SWITCH_TAG_ON_EMPTY
	#if PATCH_PERTAG
	Pertag *pertag;
	#endif // PATCH_PERTAG
	unsigned int tagw[9];
	#if PATCH_LOG_DIAGNOSTICS
	#if PATCH_FLAG_PANEL
	#if PATCH_FLAG_FLOAT_ALIGNMENT
	unsigned int offsetx;	// left-aligned panel width;
	unsigned int panelw;	// right-aligned panel width;
	#endif // PATCH_FLAG_FLOAT_ALIGNMENT
	#endif // PATCH_FLAG_PANEL
	#endif // PATCH_LOG_DIAGNOSTICS
	int showstatus;
	#if PATCH_SYSTRAY
	unsigned int stw;
	#endif // PATCH_SYSTRAY
	#if PATCH_SHOW_MASTER_CLIENT_ON_TAG
	int reversemaster;
	int showmaster;
	char *etagf;
	char *ptagf;
	#endif // PATCH_SHOW_MASTER_CLIENT_ON_TAG
	#if PATCH_WINDOW_ICONS
	#if PATCH_WINDOW_ICONS_ON_TAGS
	int showiconsontags;
	#endif // PATCH_WINDOW_ICONS_ON_TAGS
	#endif // PATCH_WINDOW_ICONS
	#if PATCH_CUSTOM_TAG_ICONS
	int showcustomtagicons;
	Picture tagicons[9];
	unsigned int tagicw[9];
	unsigned int tagich[9];
	char *tagiconpaths[9];
	#endif // PATCH_CUSTOM_TAG_ICONS
	int isdefault;
	unsigned int defaulttag;
	Client *focusontag[9];
	#if PATCH_ALT_TAGS
	char *tags[9];
	int alttags;
	int alttagsquiet;	// don't raise the bar or show over fullscreen clients;
	#endif // PATCH_ALT_TAGS
	#if PATCH_LOG_DIAGNOSTICS
	int logallrules;
	#endif // PATCH_LOG_DIAGNOSTICS
	int enablegaps;
};

#if PATCH_SYSTRAY
typedef struct Systray   Systray;
struct Systray {
	Window win;
	Client *icons;
};
#endif // PATCH_SYSTRAY

#if PATCH_IPC
#include "ipc-patch/ipc.h"
#endif // PATCH_IPC

/* function declarations */
static void activate(const Arg *arg);
static void activateclient(Client *c, int setfocus);
static void adjustfloatposition(Client *c);
#if PATCH_FLAG_FLOAT_ALIGNMENT
static int alignfloat(Client *c, float relX, float relY);
#endif // PATCH_FLAG_FLOAT_ALIGNMENT
#if PATCH_ALTTAB
static void altTabEnd(void);
static void altTabStart(const Arg *arg);
#endif // PATCH_ALTTAB
#if PATCH_FLAG_HIDDEN && PATCH_ALTTAB
static void appendhidden(Monitor *m, const char *text, char *buffer, size_t len_buffer);
#endif // PATCH_FLAG_HIDDEN && PATCH_ALTTAB
#if PATCH_FONT_GROUPS
static int apply_barelement_fontgroup(Monitor *m, int BarElementType);
#endif // PATCH_FONT_GROUPS
#if PATCH_BIDIRECTIONAL_TEXT
static void apply_fribidi(char *str);
#endif // PATCH_BIDIRECTIONAL_TEXT
static int applyrules(Client *c, int deferred, char *oldtitle);
static void applyrulesdeferred(Client *c, char *oldtitle);
static int applysizehints(Client *c, int *x, int *y, int *w, int *h, int interact);
static void arrange(Monitor *m);
static void arrangemon(Monitor *m);
#if PATCH_CLASS_STACKING
static void arrangemon_process_classstack(Client *c, int added_to_stack);
#endif // PATCH_CLASS_STACKING
static void attach(Client *c);
#if PATCH_CLASS_STACKING
static int attach_stackhead(Client *c);
#endif // PATCH_CLASS_STACKING
#if PATCH_ATTACH_BELOW_AND_NEWMASTER
static void attachBelow(Client *c);
static void attachstackBelow(Client *c);
#endif // PATCH_ATTACH_BELOW_AND_NEWMASTER
static void attachstack(Client *c);
static void attachstackex(Client *c);
static void buttonpress(XEvent *e);
#if PATCH_CLIENT_OPACITY
static void changefocusopacity(const Arg *arg);
static void changeunfocusopacity(const Arg *arg);
#endif // PATCH_CLIENT_OPACITY
#if PATCH_FOCUS_FOLLOWS_MOUSE
static void checkmouseoverclient(void);
static void checkmouseovermonitor(Monitor *m);
#endif // PATCH_FOCUS_FOLLOWS_MOUSE
static void checkotherwm(void);
#if DEBUGGING || PATCH_LOG_DIAGNOSTICS
static int checkstack(Monitor *mon);
#endif // DEBUGGING || PATCH_LOG_DIAGNOSTICS
static void cleanup(void);
static void cleanupmon(Monitor *mon);
static void clearurgency(const Arg *arg);
static void clientmessage(XEvent *e);
static void configure(Client *c);
static void configurenotify(XEvent *e);
static void configurerequest(XEvent *e);
#if PATCH_IPC
static int connect_to_socket();
#endif // PATCH_IPC
#if PATCH_FLAG_GAME
static void createbarrier(Client *c);
#endif // PATCH_FLAG_GAME
#if PATCH_CONSTRAIN_MOUSE
static void createbarriermon(Monitor *m);
#endif // PATCH_CONSTRAIN_MOUSE
#if PATCH_VIRTUAL_MONITORS
static Monitor *createmon(int index);
static PMonitor *createpmon(void);
#else // NO PATCH_VIRTUAL_MONITORS
static Monitor *createmon(void);
#endif // PATCH_VIRTUAL_MONITORS
#if PATCH_CROP_WINDOWS
static Client *cropwintoclient(Window w);
static void cropwindow(Client *c);
static void cropdelete(Client *c);
static void cropmove(Client *c);
static void cropresize(Client *c);
#endif // PATCH_CROP_WINDOWS
static void cyclelayout(const Arg *arg);
static void cyclelayoutmouse(const Arg *arg);
#if PATCH_SHOW_DESKTOP
static int desktopvalid(Client *c);
static int desktopvalidex(Client *c, unsigned int tagset, int show_desktop);
#endif // PATCH_SHOW_DESKTOP
#if PATCH_FLAG_GAME || PATCH_CONSTRAIN_MOUSE
static void destroybarrier(void);
#endif // PATCH_FLAG_GAME || PATCH_CONSTRAIN_MOUSE
#if PATCH_CONSTRAIN_MOUSE
static void destroybarriermon(void);
#endif // PATCH_CONSTRAIN_MOUSE
static void destroynotify(XEvent *e);
static void detach(Client *c);
#if PATCH_SCAN_OVERRIDE_REDIRECTS
static void detachor(Client *c);
#endif // PATCH_SCAN_OVERRIDE_REDIRECTS
static void detachstack(Client *c);
static void detachstackex(Client *c);
static Monitor *dirtomon(int dir);
#if PATCH_DRAG_FACTS
static void dragfact(const Arg *arg);
#endif // PATCH_DRAG_FACTS
static void drawbar(Monitor *m, int skiptags);
static int drawbar_elementvisible(Monitor *m, unsigned int element_type);
static void drawbars(void);
#if PATCH_FOCUS_BORDER || PATCH_FOCUS_PIXEL
static void drawfocusborder(int remove);
#endif // PATCH_FOCUS_BORDER || PATCH_FOCUS_PIXEL
#if PATCH_ALTTAB
static void drawTab(Monitor *m, int active, int first);
#endif // PATCH_ALTTAB
#if PATCH_IPC
#if PATCH_MOUSE_POINTER_WARPING
static void enablemousewarp(const Arg *arg);
#endif // PATCH_MOUSE_POINTER_WARPING
#if PATCH_TERMINAL_SWALLOWING
static void enabletermswallow(const Arg *arg);
#endif // PATCH_TERMINAL_SWALLOWING
#endif // PATCH_IPC
#if PATCH_IPC
static void enableurgency(const Arg *arg);
#endif // PATCH_IPC
static void enternotify(XEvent *e);
static void expose(XEvent *e);
#if PATCH_IPC
static void flush_socket_reply(void);
#endif // PATCH_IPC
static void focus(Client *c, int force);
static void focusin(XEvent *e);
static void focusmon(const Arg *arg);
static void focusmonex(Monitor *m);
static void focusstack(const Arg *arg);
#if 0 // original fullscreen (monocle + hide bar);
static void fullscreen(const Arg *arg);
#endif
#if PATCH_HANDLE_SIGNALS
#if PATCH_MODAL_SUPPORT
static Client *getfirstmodal(Monitor *m);
#endif // PATCH_MODAL_SUPPORT
#endif // PATCH_HANDLE_SIGNALS
#if PATCH_IPC
static int find_dwm_client(const char *name);
static int get_dwm_client(Window win);
static int get_layouts();
static int get_monitors();
static int get_tags();
#endif // PATCH_IPC
#if PATCH_FLAG_GAME
static Client *getactivegameclient(Monitor *m);
#endif // PATCH_FLAG_GAME
static Atom getatomprop(Client *c, Atom prop);
static Atom getatompropex(Window w, Atom prop);
static Client *getclientatcoords(int x, int y, int focusable);
static Client *getclientbyname(const char *name);
#if PATCH_SHOW_DESKTOP
#if PATCH_SHOW_DESKTOP_ONLY_WHEN_ACTIVE
static Client *getdesktopclient(Monitor *m, int *nondesktop_exists);
#endif // PATCH_SHOW_DESKTOP_ONLY_WHEN_ACTIVE
#endif // PATCH_SHOW_DESKTOP
static Client *getfocusable(Monitor *m, Client *c, int force);		// derive next valid focusable client from the stack;
#if PATCH_WINDOW_ICONS
static Picture geticonprop(
	#if PATCH_WINDOW_ICONS_DEFAULT_ICON || PATCH_WINDOW_ICONS_CUSTOM_ICONS
	Client *c,
	#endif // PATCH_WINDOW_ICONS_DEFAULT_ICON || PATCH_WINDOW_ICONS_CUSTOM_ICONS
	Window w, unsigned int *icw, unsigned int *ich, unsigned int iconsize
);
#endif // PATCH_WINDOW_ICONS
#if PATCH_FLAG_PANEL
#if PATCH_FLAG_FLOAT_ALIGNMENT
static int getpanelpadding(Monitor *m, unsigned int *px, unsigned int *pw);
#endif // PATCH_FLAG_FLOAT_ALIGNMENT
#endif // PATCH_FLAG_PANEL
static Client *getparentclient(Client *c);
static pid_t getparentprocess(pid_t p);
static pid_t getprocessid(const char *procname);
static int getprocname(pid_t pid, char *buffer, size_t buffer_size, char **procname, char **parameters);
#if PATCH_MOUSE_POINTER_WARPING || PATCH_FOCUS_FOLLOWS_MOUSE
static int getrelativeptr(Client *c, int *x, int *y);
static int getrelativeptrex(Client *c, int *x, int *y);
#endif // PATCH_MOUSE_POINTER_WARPING || PATCH_FOCUS_FOLLOWS_MOUSE
static int getrootptr(int *x, int *y);
static long getstate(Window w);
#if PATCH_STATUSCMD
static pid_t getstatusbarpid();
#endif // PATCH_STATUSCMD
#if PATCH_SYSTRAY
static unsigned int getsystraywidth();
#endif // PATCH_SYSTRAY
static int gettextprop(Window w, Atom atom, char *text, unsigned int size);
static Client *getmontopclient(Monitor *m);		// get the top client on the monitor from XQueryTree;
static Client *getultimateparentclient(Client *c);
static void grabbuttons(Client *c, int focused);
#if PATCH_ALTTAB || PATCH_TORCH
static int grabinputs(int keyboard, int mouse, Cursor cursor);
#endif // PATCH_ALTTAB || PATCH_TORCH
static void grabkeys(void);
#if PATCH_CLASS_STACKING
static void groupallclassstacks(Monitor *m);
#endif // PATCH_CLASS_STACKING
static Client *guessnextfocus(Client *c, Monitor *m);	// derive the next valid client to focus;
#if PATCH_IPC
static int handlexevent(struct epoll_event *ev);
#endif // PATCH_IPC
#if PATCH_ALPHA_CHANNEL
static int has_compositor(Display *dpy, int screen);
#endif // PATCH_ALPHA_CHANNEL
#if PATCH_MOUSE_POINTER_HIDING
static void hidecursor(void);
#endif // PATCH_MOUSE_POINTER_HIDING
#if PATCH_FLAG_HIDDEN
static void hidewin(const Arg *arg);
#endif // PATCH_FLAG_HIDDEN
#if PATCH_ALTTAB
static void highlight(Client *c);
#endif // PATCH_ALTTAB
static void incnmaster(const Arg *arg);
#if PATCH_DRAG_FACTS
static int ismaster(Client *c);
#endif // PATCH_DRAG_FACTS
#if PATCH_MODAL_SUPPORT
static int ismodalparent(Client *c);
#endif // PATCH_MODAL_SUPPORT
#if PATCH_FOCUS_FOLLOWS_MOUSE
static int ismouseoverclient(Client *c);
#endif // PATCH_FOCUS_FOLLOWS_MOUSE
static int isdescprocess(pid_t p, pid_t c);
#if PATCH_IPC
static int is_float(const char *s);
static int is_hex(const char *s);
static int is_signed_int(const char *s);
static int is_unsigned_int(const char *s);
#endif // PATCH_IPC
static int keycode_to_modifier(XModifierKeymap *modmap, KeyCode keycode);
static void keypress(XEvent *e);
#if PATCH_ALT_TAGS || PATCH_KEY_HOLD
static void keyrelease(XEvent *e);
#endif // PATCH_ALT_TAGS || PATCH_KEY_HOLD
static void killclient(const Arg *arg);
static void killclientex(Client *c, int sigterm);
static void killgroup(const Arg *arg);
static void killwin(Window w);
#if PATCH_MOUSE_POINTER_WARPING_RECALL
static void lastcoordsrecall(Client *c, int reset, int relative, int *px, int *py);
static void lastcoordsstore(Client *c);
#endif // PATCH_MOUSE_POINTER_WARPING_RECALL
static int layoutstringtoindex(const char *layout);
static int line_to_buffer(const char *text, char *buffer, size_t buffer_size, size_t line_length, size_t *index);
#if PATCH_LOG_DIAGNOSTICS
static void logdiagnostics(const Arg *arg);
#if PATCH_IPC
static int logdiagnostics_event(XEvent ev);
#endif // PATCH_IPC
static void logdiagnostics_stack(Monitor *m, const char *title, const char *indent);
static void logdiagnostics_stackfloating(Monitor *m, const char *title, const char *indent);
static void logdiagnostics_stacktiled(Monitor *m, const char *title, const char *indent);
static void logdiagnostics_client(Client *c, const char *indent);
static void logdiagnostics_client_common(Client *c, const char *indent1, const char *indent2);
#endif // PATCH_LOG_DIAGNOSTICS
static void logrules(const Arg *arg);
static void losefullscreen(Client *active, Client *next);
static void manage(Window w, XWindowAttributes *wa);
static void mappingnotify(XEvent *e);
static void maprequest(XEvent *e);
#if PATCH_FLAG_GAME || PATCH_FLAG_HIDDEN || PATCH_FLAG_PANEL
static void minimize(Client *c);
#endif // PATCH_FLAG_GAME || PATCH_FLAG_HIDDEN || PATCH_FLAG_PANEL
#if PATCH_MODAL_SUPPORT
static int modalgroupclients(Client *c);
#endif // PATCH_MODAL_SUPPORT
static void monocle(Monitor *m);
#if PATCH_FLAG_FOLLOW_PARENT || PATCH_MODAL_SUPPORT
static void monsatellites(Client *pp, Monitor *mon);
#endif // PATCH_FLAG_FOLLOW_PARENT || PATCH_MODAL_SUPPORT
static void motionnotify(XEvent *e);
static void mouseview(const Arg *arg);
#if PATCH_MOVE_FLOATING_WINDOWS
static void movefloat(const Arg *arg);
#endif // PATCH_MOVE_FLOATING_WINDOWS
static void movemouse(const Arg *arg);
static void moveorplace(const Arg *arg);
#if PATCH_MOVE_TILED_WINDOWS
static void movetiled(const Arg *arg);
#endif // PATCH_MOVE_TILED_WINDOWS
static Client *nextstack(Client *c, int isfloating);
#if PATCH_ATTACH_BELOW_AND_NEWMASTER
static Client *nexttaggedafter(Client *c, unsigned int tags);
#endif // PATCH_ATTACH_BELOW_AND_NEWMASTER
static Client *nexttiled(Client *c);
#if PATCH_CLASS_STACKING
static Client *nexttiledall(Client *c);
#endif // PATCH_CLASS_STACKING
#if PATCH_CLIENT_OPACITY
static void opacity(Client *c, int focused);
#endif // PATCH_CLIENT_OPACITY
static cJSON *parsejsonfile(const char *filename, const char *filetype);
static int parselayoutjson(cJSON *layout);
static void parsemon(Monitor *m, int index, int first);
static int parserulesjson(cJSON *json);
static void placemouse(const Arg *arg);
#if PATCH_MOUSE_POINTER_WARPING || PATCH_FOCUS_FOLLOWS_MOUSE
static int pointoverbar(Monitor *m, int x, int y, int check_clients);
#endif // PATCH_MOUSE_POINTER_WARPING || PATCH_FOCUS_FOLLOWS_MOUSE
static void populate_charcode_map(void);
#if PATCH_MOVE_TILED_WINDOWS || PATCH_FLAG_HIDDEN
static Client *prevtiled(Client *c);
#endif // PATCH_MOVE_TILED_WINDOWS || PATCH_FLAG_HIDDEN
#if PATCH_IPC
static void print_socket_reply(void);
#endif // PATCH_IPC
static void print_supported_json(FILE *f, const supported_json array[], const size_t len, const char *title, const char *indent);
static void print_supported_rules_json(FILE *f, const supported_rules_json array[], const size_t len, const char *title, const char *indent);
static void print_wrap(FILE *f, size_t line_length, const char *indent, size_t col1_size,
	const char *col1_text, const char *line1_gap, const char *normal_gap, const char *col2_text);
static void propertynotify(XEvent *e);
static void publishwindowstate(Client *c);
#if PATCH_ALTTAB
static void quietunmap(Window w);
#endif // PATCH_ALTTAB
static void quit(const Arg *arg);
static void raiseclient(Client *c);
static void raisewin(Monitor *m, Window w, int above_bar);
#if PATCH_IPC
static int read_socket(IPCMessageType *msg_type, uint32_t *msg_size, char **msg);
#endif // PATCH_IPC
static Client *recttoclient(int x, int y, int w, int h, int onlyfocusable);
static Monitor *recttomon(int x, int y, int w, int h);
#if PATCH_IPC
static int recv_message(uint8_t *msg_type, uint32_t *reply_size, uint8_t **reply);
#endif // PATCH_IPC
#if PATCH_MOUSE_POINTER_WARPING
static void refocuspointer(const Arg *arg);
#endif // PATCH_MOUSE_POINTER_WARPING
static void reload(const Arg *arg);
static int reload_rules(void);
static void reloadrules(const Arg *arg);
static void removelinks(Client *c);
#if PATCH_SYSTRAY
static void removesystrayicon(Client *i);
#endif // PATCH_SYSTRAY
#if PATCH_FOCUS_PIXEL && !PATCH_FOCUS_BORDER
static void repelfocusborder(void);
#endif // PATCH_FOCUS_PIXEL && !PATCH_FOCUS_BORDER
static void rescan(const Arg *arg);
static void resize(Client *c, int x, int y, int w, int h, int interact);
static void resizebarwin(Monitor *m);
static void resizeclient(Client *c, int x, int y, int w, int h, int save_old);
static void resizemouse(const Arg *arg);
#if PATCH_DRAG_FACTS
static void resizeorfacts(const Arg *arg);
#endif // PATCH_DRAG_FACTS
#if PATCH_SYSTRAY
static void resizerequest(XEvent *e);
#endif // PATCH_SYSTRAY
static void restack(Monitor *m);
static void run(void);
#if PATCH_IPC
static int run_command(char *name, char *args[], int argc);
#endif // PATCH_IPC
static void scan(void);
#if PATCH_IPC
static int send_message(IPCMessageType msg_type, uint32_t msg_size, uint8_t *msg);
#endif // PATCH_IPC
static int sendevent(Window w, Atom proto, int m, long d0, long d1, long d2, long d3, long d4);
static void sendmon(Client *c, Monitor *m, Client *leader, int force);
#if PATCH_MOUSE_POINTER_HIDING
static int set_idle_alarm(void);
#endif // PATCH_MOUSE_POINTER_HIDING
#if PATCH_FLAG_ALWAYSONTOP
static void setalwaysontop(Client *c, int alwaysontop);
#endif // PATCH_FLAG_ALWAYSONTOP
#if PATCH_CFACTS
static void setcfact(const Arg *arg);
#endif // PATCH_CFACTS
static void setclientstate(Client *c, long state);
#if PATCH_PERSISTENT_METADATA
static void setclienttagprop(Client *c);
static void setclienttagpropex(Client *c, int index);
#endif // PATCH_PERSISTENT_METADATA
#if PATCH_EWMH_TAGS
static void setcurrentdesktop(void);
#endif // PATCH_EWMH_TAGS
static void setdefaultcolours(char *colours[3], char *defaults[3]);
static void setdefaultvalues(Client *c);
#if PATCH_EWMH_TAGS
static void setdesktopnames(void);
#endif // PATCH_EWMH_TAGS
static void setfocus(Client *c);
static void setfullscreen(Client *c, int fullscreen);
#if PATCH_FLAG_HIDDEN
static void sethidden(Client *c, int hidden, int rearrange);
#endif // PATCH_FLAG_HIDDEN
static void setlayout(const Arg *arg);
static void setlayoutex(const Arg *arg);
static void setlayoutmouse(const Arg *arg);
static void setlayoutreplace(const Arg *arg);
static void setmfact(const Arg *arg);
#if PATCH_EWMH_TAGS
static void setnumdesktops(void);
#endif // PATCH_EWMH_TAGS
#if PATCH_CLIENT_OPACITY
static void setopacity(Client *c, double opacity);
#endif // PATCH_CLIENT_OPACITY
#if PATCH_FLAG_STICKY
static void setsticky(Client *c, int sticky);
#endif // PATCH_FLAG_STICKY
static int setup(void);
#if PATCH_IPC
static int setupepoll(void);
#endif // PATCH_IPC
#if PATCH_MOUSE_POINTER_HIDING
static void setup_sync_counters(void);
#endif // PATCH_MOUSE_POINTER_HIDING
static void seturgent(Client *c, int urg);
#if PATCH_EWMH_TAGS
static void setviewport(void);
#endif // PATCH_EWMH_TAGS
#if PATCH_MOUSE_POINTER_HIDING
static void showcursor(void);
#endif // PATCH_MOUSE_POINTER_HIDING
static void showhide(Client *c, int client_only);
static void showhidebar(Monitor *m);
#if PATCH_HANDLE_SIGNALS
static void sighup(int unused);
static void sigreload(int unused);
static void sigreloadrules(int unused);
static void sigterm(int unused);
#endif // PATCH_HANDLE_SIGNALS
#if PATCH_STATUSCMD
//static void sigchld(int unused);
static void sigstatusbar(const Arg *arg);
#endif // PATCH_STATUSCMD
static int skipnextkeyevent(int type, unsigned int keycode, unsigned int state, unsigned long serial);
static void snapchildclients(Client *p, int quiet);
//#if PATCH_FOCUS_FOLLOWS_MOUSE || PATCH_MOUSE_POINTER_HIDING
#if PATCH_MOUSE_POINTER_HIDING
static void snoop_root(void);
static int snoop_xinput(Window win);
#endif // PATCH_MOUSE_POINTER_HIDING
//#endif // PATCH_FOCUS_FOLLOWS_MOUSE || PATCH_MOUSE_POINTER_HIDING
static int solitary(Client *c);
static void spawn(const Arg *arg);
static pid_t spawnex(const void *v, int keyhelp);
static void spawnhelp(const Arg *arg);
#if PATCH_ALTTAB
static int strcmpbynum(const char *s1, const char *s2);
#endif // PATCH_ALTTAB
#if PATCH_IPC
static int subscribe(const char *event);
#endif // PATCH_IPC
#if PATCH_TERMINAL_SWALLOWING
static Client *swallowingclient(Window w);
#endif // PATCH_TERMINAL_SWALLOWING
static void swapmon(const Arg *arg); // switch view() on specified monitor;
#if PATCH_SYSTRAY
static Monitor *systraytomon(Monitor *m);
#endif // PATCH_SYSTRAY
static void tag(const Arg *arg);
static void tagmon(const Arg *arg);
#if PATCH_FLAG_FOLLOW_PARENT
static int tagsatellites(Client *p);
#endif // PATCH_FLAG_FOLLOW_PARENT
static int tagtoindex(unsigned int tag);
#if PATCH_TERMINAL_SWALLOWING
static Client *termforwin(const Client *w);
#endif // PATCH_TERMINAL_SWALLOWING
#if PATCH_SHOW_MASTER_CLIENT_ON_TAG && ((PATCH_WINDOW_ICONS && PATCH_WINDOW_ICONS_ON_TAGS) || PATCH_CUSTOM_TAG_ICONS)
static void textwithicon(char *text, Picture icon, unsigned int icw,
	unsigned int ich, char *placeholder_text, int x, int y, unsigned int w,
	unsigned int h, int offsetx,
	#if PATCH_CLIENT_INDICATORS
	int offsety,
	#endif // PATCH_CLIENT_INDICATORS
	int invert
);
#endif // PATCH_SHOW_MASTER_CLIENT_ON_TAG && ((PATCH_WINDOW_ICONS && PATCH_WINDOW_ICONS_ON_TAGS) || PATCH_CUSTOM_TAG_ICONS)
#if PATCH_FLAG_ALWAYSONTOP
static void togglealwaysontop(const Arg *arg);
#endif // PATCH_FLAG_ALWAYSONTOP
#if PATCH_ALT_TAGS
static void togglealttags(const Arg *arg);
#endif // PATCH_ALT_TAGS
static void togglebar(const Arg *arg);
static void togglebarex(Monitor *m);
#if PATCH_CONSTRAIN_MOUSE
static void toggleconstrain(const Arg *arg);
#endif // PATCH_CONSTRAIN_MOUSE
#if DEBUGGING
static void toggledebug(const Arg *arg);
#endif // DEBUGGING
#if PATCH_SHOW_DESKTOP
static void toggledesktop(const Arg *arg);
#endif // PATCH_SHOW_DESKTOP
#if PATCH_FLAG_FAKEFULLSCREEN
static void togglefakefullscreen(const Arg *arg);
#endif // PATCH_FLAG_FAKEFULLSCREEN
static void togglefloating(const Arg *arg);
static void togglefloatingex(Client *c);
static void togglefullscreen(const Arg *arg);
#if PATCH_FLAG_GAME
static void toggleisgame(const Arg *arg);
#endif // PATCH_FLAG_GAME
#if PATCH_MIRROR_LAYOUT
static void togglemirror(const Arg *arg);
#endif // PATCH_MIRROR_LAYOUT
#if PATCH_PAUSE_PROCESS
static void togglepause(const Arg *arg);
#endif // PATCH_PAUSE_PROCESS
#if DEBUGGING
static void toggleskiprules(const Arg *arg);
#endif // DEBUGGING
#if PATCH_VIRTUAL_MONITORS
static void togglesplit(const Arg *arg);
#endif // PATCH_VIRTUAL_MONITORS
#if PATCH_CLASS_STACKING
static void togglestacking(const Arg *arg);
#endif // PATCH_CLASS_STACKING
#if PATCH_FLAG_STICKY
static void togglesticky(const Arg *arg);
#endif // PATCH_FLAG_STICKY
static void toggletag(const Arg *arg);
static void toggletagex(Client *c, int tagmask);
#if PATCH_TORCH
static void toggletorch(const Arg *arg);
#endif // PATCH_TORCH
static void toggleview(const Arg *arg);
static void toggleviewex(Monitor *m, int tagmask);
#if PATCH_WINDOW_ICONS
#if PATCH_ALTTAB
static void freealticons(void);
#endif // PATCH_ALTTAB
static void freeicon(Client *c);
#endif // PATCH_WINDOW_ICONS
static void unfocus(Client *c, int setfocus);
#if PATCH_FLAG_HIDDEN
static void unhidewin(const Arg *arg);
#endif // PATCH_FLAG_HIDDEN
static void unmanage(Client *c, int destroyed, int cleanup);
static void unmapnotify(XEvent *e);
#if PATCH_FLAG_GAME || PATCH_FLAG_HIDDEN || PATCH_FLAG_PANEL
static void unminimize(Client *c);
#endif // PATCH_FLAG_GAME || PATCH_FLAG_HIDDEN || PATCH_FLAG_PANEL
static int updatebarpos(Monitor *m);
static void updatebars(void);
static void updateclientlist(void);
static void updateclientmonitors(void);
#if PATCH_EWMH_TAGS
static void updatecurrentdesktop(void);
#endif // PATCH_EWMH_TAGS
static int updategeom(void);
static void updatenumlockmask(void);
static void updatesizehints(Client *c);
static void updatestatus(void);
#if PATCH_SYSTRAY
static void updatesystray(int updatebar);
static void updatesystrayicongeom(Client *i, int w, int h);
static void updatesystrayiconstate(Client *i, XPropertyEvent *ev);
#endif // PATCH_SYSTRAY
static void updatetitle(Client *c, int fixempty);
#if PATCH_WINDOW_ICONS
static void updateicon(Client *c);
#endif // PATCH_WINDOW_ICONS
#if PATCH_VIRTUAL_MONITORS
static int updatevirtualmonitors(void);
#endif // PATCH_VIRTUAL_MONITORS
static int updatewindowstate(Client *c);
static void updatewindowtype(Client *c);
static void updatewmhints(Client *c);
static int usage(const char * err_text);
#if PATCH_TWO_TONE_TITLE
static int validate_colour(cJSON *string, char **colour);
#endif // PATCH_TWO_TONE_TITLE
static int validate_colours(cJSON *array, char *colours[3], char *defaults[3]);
static pid_t validate_pid(Client *c);
static int validclient(Client *c);
static void view(const Arg *arg);
#if PATCH_KEY_HOLD
static void viewkeyholdclient(const Arg *arg);
#endif // PATCH_KEY_HOLD
static void viewactive(const Arg *arg);	// argument is direction (on selected monitor);
static void viewactivenext(const Arg *arg);	// argument is monitor index;
static void viewactiveprev(const Arg *arg);	// argument is monitor index;
static void viewactiveex(Monitor *m, int direction);
static void viewmontag(Monitor *m, unsigned int tagmask, int switchmon);
static void waitforclearkeyboard(void);
#if PATCH_MOUSE_POINTER_WARPING
#if PATCH_MOUSE_POINTER_WARPING_SMOOTH
static void warptoclient(Client *c, int smoothly, int force);
#else // NO PATCH_MOUSE_POINTER_WARPING_SMOOTH
static void warptoclient(Client *c, int force);
#endif // PATCH_MOUSE_POINTER_WARPING_SMOOTH
#endif // PATCH_MOUSE_POINTER_WARPING
#if PATCH_EXTERNAL_WINDOW_ACTIVATION
static void window_switcher(const Arg *arg);
#endif // PATCH_EXTERNAL_WINDOW_ACTIVATION
static pid_t winpid(Window w);
static Client *wintoclient(Window w);
#if PATCH_SCAN_OVERRIDE_REDIRECTS
static Client *wintoorclient(Window w);
#endif // PATCH_SCAN_OVERRIDE_REDIRECTS
static Monitor *wintomon(Window w);
#if PATCH_SYSTRAY
static Client *wintosystrayicon(Window w);
#endif // PATCH_SYSTRAY
#if PATCH_IPC
static ssize_t write_socket(const void *buf, size_t count);
#endif // PATCH_IPC
static int xerror(Display *dpy, XErrorEvent *ee);
static int xerrordummy(Display *dpy, XErrorEvent *ee);
static int xerrorstart(Display *dpy, XErrorEvent *ee);
#if PATCH_ALPHA_CHANNEL
static void xinitvisual();
#endif // PATCH_ALPHA_CHANNEL
static void zoom(const Arg *arg);


/* variables */
#if PATCH_SYSTRAY
static Systray *systray = NULL;
#endif // PATCH_SYSTRAY
static const char broken[] = "broken";
static char stext[256];
#if PATCH_BIDIRECTIONAL_TEXT
static char fribidi_text[256];
#endif // PATCH_BIDIRECTIONAL_TEXT
#if PATCH_STATUSCMD
static int statussig;
static pid_t statuspid = -1;
#endif // PATCH_STATUSCMD
static int screen;
static int sw, sh;           /* X display screen geometry width, height */
static int bh;               /* bar height */
static int minbh;            /* minimum bar height */
static int lrpad;            /* sum of left and right padding for text */
static int nonstop = 1;		// scanning for windows or cleaning up before exit;
#if PATCH_CLIENT_INDICATORS
static unsigned int client_ind_offset = 0;
#endif // PATCH_CLIENT_INDICATORS

#if PATCH_STATUS_ALLOW_FIXED_MONITOR
static Monitor *status_always_on = NULL;
#endif // PATCH_STATUS_ALLOW_FIXED_MONITOR

#if PATCH_FLAG_GAME && PATCH_FLAG_GAME_STRICT
static Client *game = NULL;
#endif // PATCH_FLAG_GAME && PATCH_FLAG_GAME_STRICT

#if PATCH_FLAG_GAME || PATCH_CONSTRAIN_MOUSE
static int xfixes_support = 0;
static PointerBarrier barrierLeft = 0;
static PointerBarrier barrierRight = 0;
static PointerBarrier barrierTop = 0;
static PointerBarrier barrierBottom = 0;
#endif // PATCH_FLAG_GAME || PATCH_CONSTRAIN_MOUSE
#if PATCH_CONSTRAIN_MOUSE
static Monitor *constrained = NULL;
#endif // PATCH_CONSTRAIN_MOUSE

#if PATCH_CUSTOM_TAG_ICONS
static Client *dummyc = NULL;
#endif // PATCH_CUSTOM_TAG_ICONS

#if PATCH_KEY_HOLD
KeySym keyholdsym = 0;
unsigned int keyholdstate = 0;
Client *keyholdclient = NULL;
#endif // PATCH_KEY_HOLD

#if PATCH_MOUSE_POINTER_WARPING
static volatile sig_atomic_t warptoclient_stop_flag = 0;
#endif // PATCH_MOUSE_POINTER_WARPING

static int (*xerrorxlib)(Display *, XErrorEvent *);
static unsigned int numlockmask = 0;
static void (*handler[LASTEvent]) (XEvent *) = {
	[ButtonPress] = buttonpress,
	[ClientMessage] = clientmessage,
	[ConfigureRequest] = configurerequest,
	[ConfigureNotify] = configurenotify,
	[DestroyNotify] = destroynotify,
	[EnterNotify] = enternotify,
	[Expose] = expose,
	[FocusIn] = focusin,
	[KeyPress] = keypress,
	#if PATCH_ALT_TAGS || PATCH_KEY_HOLD
	[KeyRelease] = keyrelease,
	#endif // PATCH_ALT_TAGS || PATCH_KEY_HOLD
	[MappingNotify] = mappingnotify,
	[MapRequest] = maprequest,
	[MotionNotify] = motionnotify,
	[PropertyNotify] = propertynotify,
	#if PATCH_SYSTRAY
	[ResizeRequest] = resizerequest,
	#endif // PATCH_SYSTRAY
	[UnmapNotify] = unmapnotify
};
static Atom wmatom[WMLast], netatom[NetLast], xatom[XLast];
#if PATCH_HANDLE_SIGNALS
static volatile sig_atomic_t running = 1;
static volatile sig_atomic_t killable = 0;
#else
static int running = 1;
#endif // PATCH_HANDLE_SIGNALS
static Cur *cursor[CurLast];
static Clr **scheme;
static Display *dpy;
static Drw *drw;
#if PATCH_SCAN_OVERRIDE_REDIRECTS
static Client *orlist;
#endif // PATCH_SCAN_OVERRIDE_REDIRECTS
#if PATCH_VIRTUAL_MONITORS
static PMonitor *pmons = NULL;
#endif // PATCH_VIRTUAL_MONITORS
static Monitor *mons = NULL, *selmon = NULL;
static Window root, wmcheckwin;
#if PATCH_FOCUS_BORDER || PATCH_FOCUS_PIXEL
static Window focuswin = None;
#endif // PATCH_FOCUS_BORDER || PATCH_FOCUS_PIXEL
#if PATCH_FOCUS_PIXEL && !PATCH_FOCUS_BORDER
static unsigned int fpcurpos = 0;
#endif // PATCH_FOCUS_PIXEL && !PATCH_FOCUS_BORDER
#if PATCH_SHOW_DESKTOP
#if PATCH_SHOW_DESKTOP_UNMANAGED
static Window desktopwin = None;
static pid_t desktoppid = 0;
#endif // PATCH_SHOW_DESKTOP_UNMANAGED
#endif // PATCH_SHOW_DESKTOP
#if PATCH_TORCH
static Window torchwin;
#endif // PATCH_TORCH
#if PATCH_ALTTAB
static Monitor *altTabMon = NULL;
static int altTabActive = 0;	// 1 for selection is highlighted, 0 is not highlighted;
#endif // PATCH_ALTTAB

#if PATCH_EXTERNAL_WINDOW_ACTIVATION
static volatile sig_atomic_t enable_switching = 0;
#endif // PATCH_EXTERNAL_WINDOW_ACTIVATION
#if PATCH_HANDLE_SIGNALS
static volatile sig_atomic_t closing = 0;
#endif // PATCH_HANDLE_SIGNALS

#if PATCH_ALPHA_CHANNEL
static int useargb = 0;
static Visual *visual;
static int depth;
static Colormap cmap;
#endif // PATCH_ALPHA_CHANNEL

static xcb_connection_t *xcon;

#if PATCH_IPC
static char *ipcsockpath = NULL;
static int epoll_fd;
static int dpy_fd;
static unsigned int ipc_ignore_reply = 0;	// IPC client-side flag;
static Monitor *lastselmon;
#endif // PATCH_IPC

typedef struct charcodemap {
	wchar_t key;		// the letter for this key, like 'a';
	KeyCode code;		// the keycode that this key is on;
	KeySym symbol;		// the symbol representing this key;
	int group;			// the keyboard group that has this key in it;
	int modmask;		// the modifiers to apply when sending this key;
	int needs_binding;	// key needs to be bound at because it doesn't exist in the current keymap;
} charcodemap_t;
static charcodemap_t *charcodes;
int charcodes_len;

#define XF86AudioLowerVolume	0x1008ff11
#define XF86AudioMute			0x1008ff12
#define XF86AudioRaiseVolume	0x1008ff13
#define XF86AudioPlay			0x1008ff14
#define XF86AudioNext			0x1008ff15
#define XF86AudioPrev			0x1008ff16
#define XF86AudioStop			0x1008ff17

/* configuration, allows nested code to access above variables */
#include "config.h"

static unsigned int colourflags[LENGTH(colours)] = { 0 };

#define DWM_VERSION_STRING			"dwm-"VERSION"-"DWM_VERSION_SUFFIX" r"DWM_REVISION
#define DWM_VERSION_STRING_LONG		"dwm-"VERSION"-"DWM_VERSION_SUFFIX" revision "DWM_REVISION
#define DWM_VERSION_STRING_SHORT	"dwm-"VERSION"-"DWM_VERSION_SUFFIX

#if PATCH_IPC
#ifdef VERSION
#include "ipc-patch/IPCClient.c"
#include "ipc-patch/ipc.c"
#endif
#endif // PATCH_IPC


#if PATCH_PERTAG
struct Pertag {
	unsigned int curtag, prevtag; /* current and previous tag */
	#if PATCH_SWITCH_TAG_ON_EMPTY
	unsigned int switchonempty[LENGTH(tags) + 1];
	#endif // PATCH_SWITCH_TAG_ON_EMPTY
	int nmasters[LENGTH(tags) + 1]; /* number of windows in master area */
	float mfacts[LENGTH(tags) + 1]; /* mfacts per tag */
	float mfacts_def[LENGTH(tags) + 1]; /* default mfacts per tag */
	unsigned int sellts[LENGTH(tags) + 1]; /* selected layouts */
	const Layout *ltidxs[LENGTH(tags) + 1][2]; /* matrix of tags and layouts indexes  */
	int showbars[LENGTH(tags) + 1]; /* display bar for the current tag */
	int enablegaps[LENGTH(tags) + 1]; /* enable/disable vanity gaps */
	int alttagsquiet[LENGTH(tags) + 1];	// don't raise the bar or show over fullscreen clients;
	#if PATCH_CLASS_STACKING
	int class_stacking[LENGTH(tags) + 1];
	#endif // PATCH_CLASS_STACKING
	#if PATCH_MOUSE_POINTER_HIDING
	int cursorautohide[LENGTH(tags) + 1]; // autohide the cursor when stationary for the current tag;
	int cursorhideonkeys[LENGTH(tags) + 1]; // autohide the cursor on keys for the current tag;
	#endif // PATCH_MOUSE_POINTER_HIDING
};
#endif // PATCH_PERTAG

#include "vanitygaps.c"

/* compile-time check if all tags fit into an unsigned int bit array. */
struct NumTags { char limitexceeded[LENGTH(tags) > 31 ? -1 : 1]; };

/* function implementations */
void
activate(const Arg *arg)
{
	Client *c = getclientbyname(arg->v);
	if (c)
		activateclient(c, 1);
}

void
activateclient(Client *c, int setfocus)
{
	if (!c)
		return;
	Monitor *selm = selmon;

	if (selmon->sel)
		#if PATCH_FLAG_GAME && PATCH_FLAG_GAME_STRICT
		unfocus(selmon->sel, (c->mon != selmon ? (1 << 1) : 0));
		#else
		unfocus(selmon->sel, 0);
		#endif // PATCH_FLAG_GAME && PATCH_FLAG_GAME_STRICT

	#if PATCH_FLAG_HIDDEN
	if (c->ishidden) {
		c->ishidden = False;
		#if PATCH_PERSISTENT_METADATA
		setclienttagprop(c);
		#endif // PATCH_PERSISTENT_METADATA
		unminimize(c);
	}
	#endif // PATCH_FLAG_HIDDEN
	if (!ISVISIBLE(c))
		viewmontag(c->mon, c->tags, 0);
	if (!setfocus)
		return;
	selmon = c->mon;
	if (selmon != selm)
		drawbar(selm, 1);
	selmon->sel = c;
	focus(c, 1);
	#if PATCH_MOUSE_POINTER_WARPING
	if (selmon->sel)
		#if PATCH_MOUSE_POINTER_WARPING_SMOOTH
		warptoclient(selmon->sel, 1, 1);
		#else // NO PATCH_MOUSE_POINTER_WARPING_SMOOTH
		warptoclient(selmon->sel, 1);
		#endif // PATCH_MOUSE_POINTER_WARPING_SMOOTH
	#endif // PATCH_MOUSE_POINTER_WARPING
}

void
adjustfloatposition(Client *c)
{
	#if PATCH_FLAG_FLOAT_ALIGNMENT
	alignfloat(c, c->floatingx, c->floatingy);
	#if PATCH_FLAG_CENTRED
	int aligned =
	#endif // PATCH_FLAG_CENTRED
		alignfloat(c, c->floatalignx, c->floataligny);
	#endif // PATCH_FLAG_FLOAT_ALIGNMENT

	#if PATCH_FLAG_CENTRED
	if (c->isfloating && c->iscentred && (
		!c->isfullscreen
		#if PATCH_FLAG_FAKEFULLSCREEN
		|| c->fakefullscreen == 1
		#endif // PATCH_FLAG_FAKEFULLSCREEN
		)
	) {
		#if PATCH_FLAG_FLOAT_ALIGNMENT
		if (c->iscentred == 1
			|| (c->iscentred == 2
				&& (c->toplevel || c->fosterparent || !c->parent || c->parent->mon != c->mon)
			)
		) {
			if (!(aligned & FLOAT_ALIGNED_X))
				c->x = c->mon->wx + (c->mon->ww - WIDTH(c)) / 2;
			if (!(aligned & FLOAT_ALIGNED_Y))
				c->y = c->mon->wy + (c->mon->wh - HEIGHT(c)) / 2;
		}
		else if (c->iscentred == 2) {
			if (!(aligned & FLOAT_ALIGNED_X))
				c->x = MAX(c->parent->x + (WIDTH(c->parent) - WIDTH(c)) / 2, c->mon->wx);
			if (!(aligned & FLOAT_ALIGNED_Y))
				c->y = MAX(c->parent->y + (HEIGHT(c->parent) - HEIGHT(c)) / 2, c->mon->wy);
		}
		#else // NO PATCH_FLAG_FLOAT_ALIGNMENT
		if (c->iscentred == 1
			|| (c->iscentred == 2
				&& (c->toplevel || c->fosterparent || !c->parent || c->parent->mon != c->mon)
			)
		) {
			c->x = c->mon->wx + (c->mon->ww - WIDTH(c)) / 2;
			c->y = c->mon->wy + (c->mon->wh - HEIGHT(c)) / 2;
		}
		else if (c->iscentred == 2) {
			c->x = MAX(c->parent->x + (WIDTH(c->parent) - WIDTH(c)) / 2, c->mon->wx);
			c->y = MAX(c->parent->y + (HEIGHT(c->parent) - HEIGHT(c)) / 2, c->mon->wy);
		}
		#endif // PATCH_FLAG_FLOAT_ALIGNMENT
	}
	#endif // PATCH_FLAG_CENTRED

	if (c->x + WIDTH(c) > c->mon->wx + c->mon->ww)
		c->x = c->mon->wx + c->mon->ww - WIDTH(c);
	if (c->y + HEIGHT(c) > c->mon->wy + c->mon->wh)
		c->y = c->mon->wy + c->mon->wh - HEIGHT(c);
	c->x = MAX(c->x, c->mon->wx);
	c->y = MAX(c->y, c->mon->wy);
}

#if PATCH_FLAG_FLOAT_ALIGNMENT
int
alignfloat(Client *c, float relX, float relY)
{
	int x, y, w, h;
	if (c->isfloating) {
		w = c->w;
		h = c->h;
	}
	else {
		w = c->sfw;
		h = c->sfh;
	}
	if (!(c->isfullscreen
		#if PATCH_FLAG_FAKEFULLSCREEN
		&& c->fakefullscreen != 1
		#endif // PATCH_FLAG_FAKEFULLSCREEN
	)) {
		int alignx = 0, aligny = 0;
		if (relY >= 0) {
			aligny = FLOAT_ALIGNED_Y;
			if (relY <= 1) {
				#if PATCH_FLAG_PANEL
				if (c->ispanel)
					y = (relY * (c->mon->mh - h - c->bw*2) + c->mon->my);
				else
				#endif // PATCH_FLAG_PANEL
				#if PATCH_FLAG_CENTRED
				{
					if (c->iscentred == 2 && c->parent
						&& !c->toplevel && !c->fosterparent
						)
						y = (relY * (c->parent->h - h - c->bw*2) + c->parent->y);
					else
						y = (relY * (c->mon->wh - h - c->bw*2) + c->mon->wy);
				}
				#else // NO PATCH_FLAG_CENTRED
				y = (relY * (c->mon->wh - h - c->bw*2) + c->mon->wy);
				#endif // PATCH_FLAG_CENTRED
			}
			else {
				y = relY + (
					#if PATCH_FLAG_PANEL
					c->ispanel ? c->mon->my :
					#endif // PATCH_FLAG_PANEL
					c->mon->wy)
				;
			}
			if (c->isfloating)
				c->y = y;
			else
				c->sfy = y;
		}
		if (relX >= 0) {
			alignx = FLOAT_ALIGNED_X;
			if (relX <= 1) {
				#if PATCH_FLAG_PANEL
				if (c->ispanel)
					x = (relX * (c->mon->mw - w - c->bw*2) + c->mon->mx);
				else
				#endif // PATCH_FLAG_PANEL
				#if PATCH_FLAG_CENTRED
				{
					if (c->iscentred == 2 && c->parent
						&& !c->toplevel && !c->fosterparent
						)
						x = (relX * (c->parent->w - w - c->bw*2) + c->parent->x);
					else
						x = (relX * (c->mon->ww - w - c->bw*2) + c->mon->wx);
				}
				#else // NO PATCH_FLAG_CENTRED
				x = (relX * (c->mon->ww - w - c->bw*2) + c->mon->wx);
				#endif // PATCH_FLAG_CENTRED
			}
			else {
				x = relX + (
					#if PATCH_FLAG_PANEL
					c->ispanel ? c->mon->mx :
					#endif // PATCH_FLAG_PANEL
					c->mon->wx)
				;
			}
			if (c->isfloating)
				c->x = x;
			else
				c->sfx = x;
		}
		return (alignx | aligny);
	}
	return 0;
}
#endif // PATCH_FLAG_FLOAT_ALIGNMENT

#if PATCH_FONT_GROUPS
int
apply_barelement_fontgroup(Monitor *m, int BarElementType)
{
	int i, j, n = -1;
	cJSON *arr, *el, *nom;

	if (!drw || !drw->fonts || !fontgroups_json || !(barelement_fontgroups_json || m->barelement_fontgroups_json))
		return 0;

	arr = m->barelement_fontgroups_json ? m->barelement_fontgroups_json : barelement_fontgroups_json;

	if (cJSON_IsArray(arr))
		n = cJSON_GetArraySize(arr);
	else
		el = arr;

	for (i = 0; i < abs(n); i++) {
		if (n > 0)
			el = cJSON_GetArrayItem(arr, i);
		if (!(nom = cJSON_GetObjectItemCaseSensitive(el, "bar-element")) || !cJSON_IsString(nom))
			continue;

		// check if named bar-element is the one we want;
		for (j = LENGTH(BarElementTypes); j > 0; j--)
			if (BarElementTypes[j - 1].type == BarElementType &&
				strcmp(BarElementTypes[j - 1].name, nom->valuestring) == 0
				)
				break;
		if (!j)
			continue;

		if ((el = cJSON_GetObjectItemCaseSensitive(el, "font-group")) && cJSON_IsString(el) &&
			(drw_select_fontgroup(drw, el->valuestring))
		) {
			lrpad = drw->selfonts ? drw->selfonts->lrpad : drw->fonts->lrpad;
			return 1;
		}
	}
	drw->selfonts = NULL;
	lrpad = drw->fonts->lrpad;
	return 0;
}
#endif // PATCH_FONT_GROUPS

#if PATCH_BIDIRECTIONAL_TEXT
void
apply_fribidi(char *str)
{
	FriBidiStrIndex len = strlen(str);
	FriBidiChar logical[256];
	FriBidiChar visual[256];
	FriBidiParType base = FRIBIDI_PAR_ON;
	FriBidiCharSet charset;

	charset = fribidi_parse_charset("UTF-8");
	len = fribidi_charset_to_unicode(charset, str, len, logical);
	fribidi_log2vis(logical, len, &base, visual, NULL, NULL, NULL);
	fribidi_unicode_to_charset(charset, visual, len, fribidi_text);
}
#endif // PATCH_BIDIRECTIONAL_TEXT

void
applyrulesdeferred(Client *c, char *oldtitle)
{
	if (!c || c->ruledefer != 1) return;

	Monitor *m = c->mon;
	int sel = (m->sel == c);
	unsigned int tags = c->tags;
	unsigned int floating = c->isfloating_override;
	if (applyrules(c, 1, oldtitle)) {
		if (!c->tags && tags)
			c->tags = tags;
		Monitor *mm = c->mon;
		c->mon = m;
		if (floating != c->isfloating_override && c->isfloating) {
			if (c->isfloating) {
				// move client into appropriate position;
				if (selmon->sel == c) {
					adjustfloatposition(c);
					focus(c, 1);
					#if PATCH_MOUSE_POINTER_WARPING
					#if PATCH_MOUSE_POINTER_WARPING_SMOOTH
					warptoclient(c, 1, 1);
					#else // NO PATCH_MOUSE_POINTER_WARPING_SMOOTH
					warptoclient(c, 1);
					#endif // PATCH_MOUSE_POINTER_WARPING_SMOOTH
					#endif // PATCH_MOUSE_POINTER_WARPING
				}
				else
					adjustfloatposition(c);
			}
			arrange(c->mon);
			if (c->isurgent)
				focus(c, 0);
		}
		if (m == mm && (c->tags == tags || ISVISIBLE(c))) {
			if (c->monindex == -1)
				c->monindex = mm->num;
		}
		else {
			#if PATCH_FLAG_GAME && PATCH_FLAG_GAME_STRICT
			unfocus(c, 1 | (1 << 1));
			#else
			unfocus(c, 1);
			#endif // PATCH_FLAG_GAME && PATCH_FLAG_GAME_STRICT
			detach(c);
			detachstack(c);
			arrange(m);

			c->mon = mm;
			if (c->monindex == -1)
				c->monindex = mm->num;
			#if PATCH_CLASS_STACKING
			if(!attach_stackhead(c))
			{
			#endif // PATCH_CLASS_STACKING
				#if PATCH_ATTACH_BELOW_AND_NEWMASTER
				attachBelow(c);
				//	attachstack(c);
				attachstackBelow(c);
				#else // NO PATCH_ATTACH_BELOW_AND_NEWMASTER
				attach(c);
				attachstack(c);
				#endif // PATCH_ATTACH_BELOW_AND_NEWMASTER
			#if PATCH_CLASS_STACKING
			}
			#endif // PATCH_CLASS_STACKING

			if (sel && m == mm && ISVISIBLE(c)) {
				mm->sel = c;
				setfocus(c);
				sel = 0;
			}
			arrange(mm);
			if (sel)
				focus(NULL, 0);
		}
		#if PATCH_PERSISTENT_METADATA
		setclienttagprop(c);
		#endif // PATCH_PERSISTENT_METADATA
		c->ruledefer = -1;
	}

}

int
applyrules_stringtest(cJSON *rule_node, const char *string, int string_len, int match_type)
{
	int i, len;
	const char *empty_string = "";
	const char *test_string;
	cJSON *r_arrayitem;

	if(string == NULL)
	{
		string_len = 0;
		string = empty_string;
	}
	else if (string_len == -1)
		string_len = strlen(string);

	// check if the rule has an array of values or is just a plain string;
	if (cJSON_IsArray(rule_node)) {
		// this rule node holds an array of values;
		len = cJSON_GetArraySize(rule_node);
		// iterate through the array;
		for (i = 0; i < len; i++) {
			r_arrayitem = cJSON_GetArrayItem(rule_node, i);
			if (r_arrayitem) {
				// only check for matches on string entries;
				if (cJSON_IsString(r_arrayitem)) {
					test_string = r_arrayitem->valuestring;
					if (strlen(test_string) == 0) test_string = broken;
				}
				else test_string = broken;

				switch (match_type) {
					case APPLYRULES_STRING_EXACT:
						if ((strcmp(string, test_string)==0 && test_string != broken) || (string_len == 0 && test_string == broken)) return 1;
						break;
					case APPLYRULES_STRING_CONTAINS:
						if (strstr(string, test_string) != NULL) return 1;
						break;
					case APPLYRULES_STRING_BEGINS:
						if (strstr(string, test_string) == string) return 1;
						break;
					case APPLYRULES_STRING_ENDS:
						if (strstr(string, test_string) == string + string_len - strlen(test_string)) return 1;
				}
			}
		}
	}
	else {
		// just operate on the one string, and return match status;
		if (cJSON_IsString(rule_node)) {
			test_string = rule_node->valuestring;
			if (strlen(test_string) == 0) test_string = broken;
		}
		else test_string = broken;

		if (match_type == APPLYRULES_STRING_EXACT)
			return ((strcmp(string, test_string)==0 && test_string != broken) || (string_len == 0 && test_string == broken)) ? 1 : 0;
		else if (match_type == APPLYRULES_STRING_CONTAINS)
			return (strstr(string, test_string) != NULL) ? 1 : 0;
		else if (match_type == APPLYRULES_STRING_BEGINS)
			return (strstr(string, test_string) == string) ? 1 : 0;
		else if (match_type == APPLYRULES_STRING_ENDS)
			return (strstr(string, test_string) == string + string_len - strlen(test_string)) ? 1 : 0;
	}
	return 0;
}

int
napplyrules_stringtest(cJSON *rule_node, const char *string, int string_len, int match_type)
{
	int res = !applyrules_stringtest(rule_node, string, string_len, match_type);
	if(!res)
	{
		logdatetime(stderr);
		fprintf(stderr,"applyrules_stringtest(%s, %u): %i\n", string, match_type, res);
	}
	return res;
}

int
json_isboolean(cJSON *node)
{
	if (cJSON_IsBool(node))
		return 1;
	else if (cJSON_IsNumeric(node))
		return (ISBOOLEAN(node->valueint) ? 1 : 0);
	return 0;
}

#if PATCH_FLAG_HIDDEN && PATCH_ALTTAB
void
appendhidden(Monitor *m, const char *text, char *buffer, size_t len_buffer)
{
	size_t len_buffer_hidden = strlen(tabHidden);
	size_t len_name = strlen(text);
	if (m->title_align == 2) {
		int j;
		for (j = 0; j < len_buffer_hidden; j++) {
			if (j >= len_buffer)
				return;
			buffer[j] = tabHidden[j];
		}
		buffer[j++] = ' ';
		for (int i = 0; i < len_name; i++) {
			if (i + j > len_buffer - 1)
				return;
			buffer[j + i] = text[i];
		}
	}
	else {
		strncpy(buffer, text, len_buffer);
		if (len_name > (len_buffer - len_buffer_hidden))
			for (int j = 0; j < len_buffer_hidden; j++)
				buffer[len_buffer - len_buffer_hidden + j] = tabHidden[j];
		else {
			strncat(buffer, " ", len_buffer-1);
			strncat(buffer, tabHidden, len_buffer-1);
		}
	}
}
#endif // PATCH_FLAG_HIDDEN && PATCH_ALTTAB

int
applyrules(Client *c, int deferred, char *oldtitle)
{
	#if DEBUGGING
	if (skip_rules)
		return 0;
	#endif // DEBUGGING
	int matched = 0;
	int parsed = 0;
	Monitor *m;
	const char *class, *instance;
	#if PATCH_LOG_DIAGNOSTICS
	char *rule;
	#endif // PATCH_LOG_DIAGNOSTICS
	char role[64];
	cJSON *r_json;
	XClassHint ch = { NULL, NULL };

	#if PATCH_FLAG_PARENT
	Monitor *mm;
	XClassHint pch = { NULL, NULL };
	Client *p, *pp;
	#endif // PATCH_FLAG_PARENT

	#if PATCH_ACTIVE_CLIENT_CHECKS
	char active_role[64];
	active_role[0] = 0;
	XClassHint ach = { NULL, NULL };
	const char *active_title = NULL;
	size_t sz_active_title = 0;
	size_t sz_active_role = 0;
	if (selmon->sel) {
		active_title = selmon->sel->name;
		XGetClassHint(dpy, selmon->sel->win, &ach);
		sz_active_title = strlen(active_title);
		gettextprop(selmon->sel->win, wmatom[WMWindowRole], active_role, sizeof(active_role));
		sz_active_role = strlen(active_role);
	}
	const char *active_class = ach.res_class ? ach.res_class : broken;
	const char *active_instance = ach.res_name  ? ach.res_name  : broken;
	size_t sz_active_class = (active_class == broken ? 0 : strlen(active_class));
	size_t sz_active_instance = (active_instance == broken ? 0 : strlen(active_instance));
	#endif // PATCH_ACTIVE_CLIENT_CHECKS

	/* rule matching */
	XGetClassHint(dpy, c->win, &ch);
	class    = ch.res_class ? ch.res_class : broken;
	instance = ch.res_name  ? ch.res_name  : broken;

	gettextprop(c->win, wmatom[WMWindowRole], role, sizeof(role));
	size_t sz_class = (class == broken ? 0 : strlen(class));
	size_t sz_instance = (instance == broken ? 0 : strlen(instance));
	size_t sz_role = strlen(role);
	size_t sz_title = strlen(c->name);

	#if PATCH_FLAG_PARENT
	size_t sz_sp_title = 0;
	#endif // PATCH_FLAG_PARENT
	XClassHint p_ch = { NULL, NULL };
	const char *p_class = "";
	const char *p_instance = "";
	char p_role[64];
	const char *p_title = "";
	size_t sz_p_title = 0;
	size_t sz_p_class = 0;
	size_t sz_p_role = 0;
	size_t sz_p_instance = 0;

	if (c->parent) {
		p_title = c->parent->name;
		XGetClassHint(dpy, c->parent->win, &p_ch);
		p_class    = p_ch.res_class ? p_ch.res_class : broken;
		p_instance = p_ch.res_name  ? p_ch.res_name  : broken;
		gettextprop(c->parent->win, wmatom[WMWindowRole], p_role, sizeof(p_role));
		sz_p_class = (p_class == broken ? 0 : strlen(p_class));
		sz_p_instance = (p_instance == broken ? 0 : strlen(p_instance));
		sz_p_role = strlen(p_role);
		sz_p_title = strlen(p_title);
	}
	else {
		p_role[0] = '\0';
		c->toplevel = 1;
	}

	if (!deferred)
		setdefaultvalues(c);

	// some kind of broken window;
	if (class == broken && instance == broken && c->name[0] == '\0')
	{
		matched = -1;

		// make it float so it doesn't interfere with well mannered clients;
		c->isfloating = 1;
		c->autofocus = 0;
		logdatetime(stderr);
		fprintf(stderr, "note: window 0x%lx mapped (%ix%i+%ix%i) with no class, instance or title (pid:%i", c->win, c->w, c->h, c->x, c->y, c->pid);
		if (strlen(role))
			fprintf(stderr, ", role:\"%s\"", c->ultparent->name);
		else
			fprintf(stderr, ", role:<none>");
		if (c->ultparent)
			fprintf(stderr, ", parent:\"%s\")\n", c->ultparent->name);
		else
			fprintf(stderr, ", parent:<none>)\n");
	}
	else

	for (r_json = (rules_json ? rules_json->child : NULL); r_json; r_json = r_json->next) {

		cJSON *r_node;
		int match = 0;
		int exclusive = ((r_node = cJSON_GetObjectItemCaseSensitive(r_json, "exclusive")) && json_isboolean(r_node)) ? r_node->valueint : 0;

		int defer = ((r_node = cJSON_GetObjectItemCaseSensitive(r_json, "defer-rule")) && json_isboolean(r_node)) ? r_node->valueint : 0;
		if (deferred && (!defer || c->ruledefer != 1))
			continue;

		int has_parent = ((r_node = cJSON_GetObjectItemCaseSensitive(r_json, "if-has-parent")) && json_isboolean(r_node)) ? r_node->valueint : -1;
		int fixed_size = ((r_node = cJSON_GetObjectItemCaseSensitive(r_json, "if-fixed-size")) && json_isboolean(r_node)) ? r_node->valueint : -1;
		#if PATCH_SHOW_DESKTOP
		int is_desktop = ((r_node = cJSON_GetObjectItemCaseSensitive(r_json, "if-desktop")) && json_isboolean(r_node)) ? r_node->valueint : -1;
		#endif // PATCH_SHOW_DESKTOP

		// strings;
		// class;

		#if PATCH_FLAG_PARENT
		// check if the client title matches the set-parent rules;
		if (STRINGMATCHABLE(r_json, "set-parent")) {

			if (STRINGMATCH(r_json, c->name, sz_title, "set-parent")) {
				for (m = mons; m; m = m->next)
					for (p = m->clients; p; p = p->next) {
						if (p->parent_late == 1) {
							if (
								(p->parent_begins && applyrules_stringtest(p->parent_begins, c->name, sz_title, APPLYRULES_STRING_BEGINS))
							||	(p->parent_contains && applyrules_stringtest(p->parent_contains, c->name, sz_title, APPLYRULES_STRING_CONTAINS))
							||	(p->parent_ends && applyrules_stringtest(p->parent_ends, c->name, sz_title, APPLYRULES_STRING_ENDS))
							||	(p->parent_is && applyrules_stringtest(p->parent_is, c->name, sz_title, APPLYRULES_STRING_EXACT))
							) {
								XGetClassHint(dpy, p->win, &pch);
								p->parent_late = (strcmp(pch.res_class ? pch.res_class : broken, ch.res_class ? ch.res_class : broken) == 0) ? 0 : 1;
								if (pch.res_class)
									XFree(pch.res_class);
								if (pch.res_name)
									XFree(pch.res_name);
								if (!p->parent_late) {
									p->fosterparent = 0;
									p->toplevel = 0;
									if (p == c) {
										p->ultparent = c;
										p->parent = NULL;
										p->index = 0;
									}
									else {
										p->parent = c;
										if (p->index < c->index) {
											match = p->index;
											p->index = c->index;
											c->index = match;
										}
									}
									if (c->parent == p)
										c->parent = NULL;
									if (c->ultparent == p) {
										c->ultparent = c;
										c->toplevel = 1;
									}
									p->parent_begins = NULL;
									p->parent_contains = NULL;
									p->parent_ends = NULL;
									p->parent_is = NULL;

									if (p == p->ultparent) {
										// client was an ultimate parent, so adjust accordingly;
										for (mm = mons; mm; mm = mm->next)
											for (pp = mm->clients; pp; pp = pp->next)
												if (pp->ultparent == p)
													pp->ultparent = c->ultparent;
									}
								}
							}
						}

					}
			}
		}
		#endif // PATCH_FLAG_PARENT

		// only match when the rule has at least one string-matching entry;
		if (!(
				#if PATCH_ACTIVE_CLIENT_CHECKS
				STRINGMATCHABLE(r_json, "if-active-class") ||
				STRINGMATCHABLE(r_json, "if-active-instance") ||
				STRINGMATCHABLE(r_json, "if-active-role") ||
				STRINGMATCHABLE(r_json, "if-active-title") ||
				STRINGMATCHABLE(r_json, "if-not-active-class") ||
				STRINGMATCHABLE(r_json, "if-not-active-instance") ||
				STRINGMATCHABLE(r_json, "if-not-active-role") ||
				STRINGMATCHABLE(r_json, "if-not-active-title") ||
				#endif // PATCH_ACTIVE_CLIENT_CHECKS
				STRINGMATCHABLE(r_json, "if-class") ||
				STRINGMATCHABLE(r_json, "if-instance") ||
				STRINGMATCHABLE(r_json, "if-role") ||
				STRINGMATCHABLE(r_json, "if-title") ||
				STRINGMATCHABLE(r_json, "if-not-class") ||
				STRINGMATCHABLE(r_json, "if-not-instance") ||
				STRINGMATCHABLE(r_json, "if-not-role") ||
				STRINGMATCHABLE(r_json, "if-not-title") ||
				STRINGMATCHABLE(r_json, "if-parent-class") ||
				STRINGMATCHABLE(r_json, "if-parent-instance") ||
				STRINGMATCHABLE(r_json, "if-parent-title") ||
				STRINGMATCHABLE(r_json, "if-parent-role")
				#if PATCH_SHOW_DESKTOP
				|| is_desktop == 1
				#endif // PATCH_SHOW_DESKTOP
			) && (!deferred || !(cJSON_HasObjectItem(r_json, "if-title-was") || cJSON_HasObjectItem(r_json, "if-not-title-was")))
		) continue;

		if (
			NOT_STRINGMATCH(r_json, class, sz_class, "if-not-class") &&
			NOT_STRINGMATCH(r_json, instance, sz_instance, "if-not-instance") &&
			NOT_STRINGMATCH(r_json, role, sz_role, "if-not-role") &&
			NOT_STRINGMATCH(r_json, p_class, sz_p_class, "if-not-parent-class") &&
			NOT_STRINGMATCH(r_json, p_instance, sz_p_instance, "if-not-parent-instance") &&
			NOT_STRINGMATCH(r_json, p_role, sz_p_role, "if-not-parent-role") &&
			NOT_STRINGMATCH(r_json, p_title, sz_p_title, "if-not-parent-title") &&
			#if PATCH_ACTIVE_CLIENT_CHECKS
			NOT_STRINGMATCH(r_json, active_class, sz_active_class, "if-not-active-class") &&
			NOT_STRINGMATCH(r_json, active_instance, sz_active_instance, "if-not-active-instance") &&
			NOT_STRINGMATCH(r_json, active_role, sz_active_role, "if-not-active-role") &&
			NOT_STRINGMATCH(r_json, active_title, sz_active_title, "if-not-active-title") &&
			STRINGMATCH(r_json, active_class, sz_active_class, "if-active-class") &&
			STRINGMATCH(r_json, active_instance, sz_active_instance, "if-active-instance") &&
			STRINGMATCH(r_json, active_role, sz_active_role, "if-active-role") &&
			STRINGMATCH(r_json, active_title, sz_active_title, "if-active-title") &&
			#endif // PATCH_ACTIVE_CLIENT_CHECKS
			STRINGMATCH(r_json, class, sz_class, "if-class") &&
			STRINGMATCH(r_json, instance, sz_instance, "if-instance") &&
			STRINGMATCH(r_json, role, sz_role, "if-role") &&
			STRINGMATCH(r_json, p_class, sz_p_class, "if-parent-class") &&
			STRINGMATCH(r_json, p_instance, sz_p_instance, "if-parent-instance") &&
			STRINGMATCH(r_json, p_role, sz_p_role, "if-parent-role") &&
			STRINGMATCH(r_json, p_title, sz_p_title, "if-parent-title") &&
			(has_parent == -1 || (has_parent && c->parent) || (!has_parent && !c->parent)) &&
			(fixed_size == -1 || (c->isfixed == fixed_size || c->isfullscreen))
			#if PATCH_SHOW_DESKTOP
			&& (is_desktop == -1 || (is_desktop == c->wasdesktop))
			#endif // PATCH_SHOW_DESKTOP
		) {
			match = STRINGMATCH(r_json, c->name, sz_title, "if-title") && NOT_STRINGMATCH(r_json, c->name, sz_title, "if-not-title");

			if (defer) {
				if (!match && !deferred)
					c->ruledefer = 1;
				else if (!match && deferred) {
					if (cJSON_HasObjectItem(r_json, "if-title-was")) {
						if (oldtitle && applyrules_stringtest(cJSON_GetObjectItemCaseSensitive(r_json, "if-title-was"), oldtitle, strlen(oldtitle), APPLYRULES_STRING_EXACT))
							c->ruledefer = -1;
					}
					if (cJSON_HasObjectItem(r_json, "if-not-title-was")) {
						if (oldtitle && !applyrules_stringtest(cJSON_GetObjectItemCaseSensitive(r_json, "if-not-title-was"), oldtitle, strlen(oldtitle), APPLYRULES_STRING_EXACT))
							c->ruledefer = -1;
					}
				}
				else if (deferred && match) {
					if (cJSON_HasObjectItem(r_json, "if-title-was")) {
						match = (oldtitle && applyrules_stringtest(cJSON_GetObjectItemCaseSensitive(r_json, "if-title-was"), oldtitle, strlen(oldtitle), APPLYRULES_STRING_EXACT));
					}
					if (cJSON_HasObjectItem(r_json, "if-not-title-was")) {
						match = (oldtitle && !applyrules_stringtest(cJSON_GetObjectItemCaseSensitive(r_json, "if-not-title-was"), oldtitle, strlen(oldtitle), APPLYRULES_STRING_EXACT));
					}
				}
			}
			if (!match)
				continue;

			// use to prevent spamming the same warning messages when re-using a rule;
			parsed = ((r_node = cJSON_GetObjectItemCaseSensitive(r_json, "parsed")) && json_isboolean(r_node)) ? r_node->valueint : 0;
			if (!parsed) {
				if (r_node)
					cJSON_SetIntValue(r_node, 1);
				else
					cJSON_AddNumberToObject(r_json, "parsed", 1);
			}

			#if PATCH_LOG_DIAGNOSTICS
			int logrule = ((r_node = cJSON_GetObjectItemCaseSensitive(r_json, "log-rule")) && json_isboolean(r_node)) ? r_node->valueint : 0;
			m = c->mon ? c->mon : selmon;
			if (logrule || m->logallrules) {

				rule = cJSON_Print(r_json);
				fprintf(stderr,
					"\nnote: Rule matched - before%s mon:%u%s",
					(deferred ? " (deferred):" : ":"),
					m->num,
					(!c->mon ? "(default)" : "")
				);
				logdiagnostics_client_common(c, " ", "");
				fprintf(stderr, " (%ix%i+%ix%i:%ix%i) (pid:%i) ", c->w, c->h, c->x, c->y, (c->x - m->mx), (c->y - m->my), c->pid);
				if (strlen(role))
					fprintf(stderr, "role:\"%s\" ", role);
				else
					fprintf(stderr, "role:<none> ");

				fprintf(stderr,
					"(\"%s\", \"%s\")",
					instance,
					class
				);

				if (c->parent)
					fprintf(stderr, " %sparent:\"%s\"", c->fosterparent ? "[foster]" : "", c->parent->name);
				else
					fprintf(stderr, " parent:<none>");

				fprintf(stderr, " index:%i\n", c->index);

				if (rule) {
					fprintf(stderr, "%s\n", rule);
					cJSON_free(rule);
				}

			}
			#endif // PATCH_LOG_DIAGNOSTICS

			if (exclusive)
				setdefaultvalues(c);

			#if PATCH_FLAG_PARENT
			if (
				STRINGMATCHABLE(r_json, "set-parent")
				&& c->parent_late == -1
			) {
				// attempt to set the parent according to its title;
				c->parent_late = 1;
				if ((r_node = cJSON_GetObjectItemCaseSensitive(r_json, "set-parent-is"))) {
					if (cJSON_IsNull(r_node)) {
						c->parent = NULL;
						c->ultparent = c;
						c->toplevel = 1;
						c->fosterparent = 0;
						goto skip_parenting;
					}
					else if (cJSON_IsString(r_node))
						c->parent_is = r_node;
				}
				if ((r_node = cJSON_GetObjectItemCaseSensitive(r_json, "set-parent-begins")) && cJSON_IsString(r_node)) c->parent_begins = r_node;
				if ((r_node = cJSON_GetObjectItemCaseSensitive(r_json, "set-parent-contains")) && cJSON_IsString(r_node)) c->parent_contains = r_node;
				if ((r_node = cJSON_GetObjectItemCaseSensitive(r_json, "set-parent-ends")) && cJSON_IsString(r_node)) c->parent_ends = r_node;
				if (c->parent_is || c->parent_begins || c->parent_contains || c->parent_ends)
					c->parent_condition_node = r_json;
				for (m = mons; m; m = m->next) {
					for (p = m->clients; p; p = p->next) {
						sz_sp_title = strlen(p->name);
						if (
							(c->parent_begins && applyrules_stringtest(c->parent_begins, p->name, sz_sp_title, APPLYRULES_STRING_BEGINS))
						||	(c->parent_contains && applyrules_stringtest(c->parent_contains, p->name, sz_sp_title, APPLYRULES_STRING_CONTAINS))
						||	(c->parent_ends && applyrules_stringtest(c->parent_ends, p->name, sz_sp_title, APPLYRULES_STRING_ENDS))
						||	(c->parent_is && applyrules_stringtest(c->parent_is, p->name, sz_sp_title, APPLYRULES_STRING_EXACT))
						) {
							XGetClassHint(dpy, p->win, &pch);
							c->parent_late = (strcmp(pch.res_class ? pch.res_class : broken, ch.res_class ? ch.res_class : broken) == 0) ? 0 : 1;
							if (pch.res_class)
								XFree(pch.res_class);
							if (pch.res_name)
								XFree(pch.res_name);
							if (!c->parent_late) {
								c->fosterparent = 0;
								if (c == p) {
									c->ultparent = p;
									c->parent = NULL;
									c->toplevel = 1;
								}
								else {
									c->parent = p;
									c->ultparent = p->ultparent;
									c->tags = p->tags;
									c->mon = p->mon;
									c->monindex = p->monindex;
									c->toplevel = 0;
								}
								c->parent_begins = NULL;
								c->parent_contains = NULL;
								c->parent_ends = NULL;
								c->parent_is = NULL;
								break;
							}
						}
					}
					if (!c->parent_late)
						break;
				}
				if (c->parent_late) {
					for (m = mons; m; m = m->next) {
						for (p = m->clients; p; p = p->next) {
							sz_sp_title = strlen(p->name);
							if (
								(c->parent_begins && applyrules_stringtest(c->parent_begins, p->name, sz_sp_title, APPLYRULES_STRING_BEGINS))
							||	(c->parent_contains && applyrules_stringtest(c->parent_contains, p->name, sz_sp_title, APPLYRULES_STRING_CONTAINS))
							||	(c->parent_ends && applyrules_stringtest(c->parent_ends, p->name, sz_sp_title, APPLYRULES_STRING_ENDS))
							||	(c->parent_is && applyrules_stringtest(c->parent_is, p->name, sz_sp_title, APPLYRULES_STRING_EXACT))
							) {
								c->fosterparent = 0;
								if (c == p) {
									c->ultparent = p;
									c->parent = NULL;
									c->toplevel = 1;
								}
								else {
									c->parent = p;
									c->ultparent = p->ultparent;
									c->tags = p->tags;
									c->mon = p->mon;
									c->monindex = p->monindex;
									c->toplevel = 0;
								}
								c->parent_begins = NULL;
								c->parent_contains = NULL;
								c->parent_ends = NULL;
								c->parent_is = NULL;
								c->parent_late = 0;
								break;
							}
						}
						if (!c->parent_late)
							break;
					}
				}
				if (!c->parent_late) {
					for (m = mons; m; m = m->next)
						for (p = m->clients; p; p = p->next) {
							if (p->ultparent == c)
								p->ultparent = c->ultparent;
							if (p->parent == c)
								p->parent = c->parent;
						}
				}
				/*
				if (!c->parent_late && c == c->ultparent) {
					// client was an ultimate parent, so adjust accordingly;
					for (m = mons; m; m = m->next)
						for (p = m->clients; p; p = p->next)
							if (p->ultparent == c)
								p->ultparent = c->parent;
				}
				*/
			}
skip_parenting:
			if ((r_node = cJSON_GetObjectItemCaseSensitive(r_json, "set-parent-guess")) && json_isboolean(r_node) && r_node->valueint) {
				if (!(m = selmon))
					for (m = mons; m && !m->stack; m = m->next);
				if (m && (p = m->sel ? m->sel : m->stack)) {
					c->parent = p;
					c->ultparent = p->ultparent;
					c->index = p->index + 1;
					c->toplevel = 0;
					c->fosterparent = 1;
				}
			}
			if ((r_node = cJSON_GetObjectItemCaseSensitive(r_json, "set-never-parent")) && json_isboolean(r_node)) c->neverparent = r_node->valueint;
			#endif // PATCH_FLAG_PARENT

			#if PATCH_ALTTAB
			if ((r_node = cJSON_GetObjectItemCaseSensitive(r_json, "set-class-group")) && cJSON_IsString(r_node)) c->grpclass = r_node->valuestring;
			#endif // PATCH_ALTTAB
			#if PATCH_CLASS_STACKING
			if ((r_node = cJSON_GetObjectItemCaseSensitive(r_json, "set-class-stack")) && cJSON_IsString(r_node)) c->stackclass = r_node->valuestring;
			#endif // PATCH_CLASS_STACKING

			#if PATCH_SHOW_MASTER_CLIENT_ON_TAG
			if ((r_node = cJSON_GetObjectItemCaseSensitive(r_json, "set-class-display")) && cJSON_IsString(r_node)) c->dispclass = r_node->valuestring;
			#endif // PATCH_SHOW_MASTER_CLIENT_ON_TAG

			#if PATCH_FLAG_GAME || PATCH_FLAG_HIDDEN || PATCH_FLAG_PANEL
			if ((r_node = cJSON_GetObjectItemCaseSensitive(r_json, "set-autohide")) && json_isboolean(r_node)) c->autohide = r_node->valueint;
			#endif // PATCH_FLAG_GAME || PATCH_FLAG_HIDDEN || PATCH_FLAG_PANEL

			#if PATCH_CLIENT_OPACITY
			if ((r_node = cJSON_GetObjectItemCaseSensitive(r_json, "set-opacity-active")) && cJSON_IsNumeric(r_node)) {
				c->opacity = r_node->valuedouble;
				if (c->opacity <= 0 || c->opacity > 1.0f) {
					if (config_warnings && !parsed) {
						logdatetime(stderr);
						fprintf(stderr, "dwm: warning: set-opacity-active value must be greater than 0 and less than or equal to 1.\n");
					}
					c->opacity = -1;
				}
			}
			if ((r_node = cJSON_GetObjectItemCaseSensitive(r_json, "set-opacity-inactive")) && cJSON_IsNumeric(r_node)) {
				c->unfocusopacity = r_node->valuedouble;
				if (c->unfocusopacity <= 0 || c->unfocusopacity > 1.0f) {
					if (config_warnings && !parsed) {
						logdatetime(stderr);
						fprintf(stderr, "dwm: warning: set-opacity-inactive value must be greater than 0 and less than or equal to 1.\n");
					}
					c->unfocusopacity = -1;
				}
			}
			#endif // PATCH_CLIENT_OPACITY

			#if PATCH_MOUSE_POINTER_HIDING
			if ((r_node = cJSON_GetObjectItemCaseSensitive(r_json, "set-cursor-autohide")) && json_isboolean(r_node)) c->cursorautohide = r_node->valueint;
			if ((r_node = cJSON_GetObjectItemCaseSensitive(r_json, "set-cursor-hide-on-keys")) && json_isboolean(r_node)) c->cursorhideonkeys = r_node->valueint;
			#endif // PATCH_MOUSE_POINTER_HIDING

			#if PATCH_SHOW_DESKTOP
			if ((r_node = cJSON_GetObjectItemCaseSensitive(r_json, "set-desktop")) && json_isboolean(r_node)
				#if PATCH_SHOW_DESKTOP_UNMANAGED
				&& showdesktop && !showdesktop_unmanaged
				#endif // PATCH_SHOW_DESKTOP_UNMANAGED
				) c->isdesktop = r_node->valueint;
			#endif // PATCH_SHOW_DESKTOP

			if ((r_node = cJSON_GetObjectItemCaseSensitive(r_json, "set-autofocus")) && json_isboolean(r_node)) c->autofocus = r_node->valueint;
			#if PATCH_FLAG_NEVER_MOVE
			if ((r_node = cJSON_GetObjectItemCaseSensitive(r_json, "set-never-move")) && json_isboolean(r_node)) c->nevermove = r_node->valueint;
			#endif // PATCH_FLAG_NEVER_MOVE
			#if PATCH_FLAG_NEVER_RESIZE
			if ((r_node = cJSON_GetObjectItemCaseSensitive(r_json, "set-never-resize")) && json_isboolean(r_node)) c->neverresize = r_node->valueint;
			#endif // PATCH_FLAG_NEVER_RESIZE
			#if PATCH_FLAG_CAN_LOSE_FOCUS
			if ((r_node = cJSON_GetObjectItemCaseSensitive(r_json, "set-can-lose-focus")) && json_isboolean(r_node)) c->canlosefocus = r_node->valueint;
			#endif // PATCH_FLAG_CAN_LOSE_FOCUS
			#if PATCH_FLAG_CENTRED
			if ((r_node = cJSON_GetObjectItemCaseSensitive(r_json, "set-centred")) && cJSON_IsNumeric(r_node)) c->iscentred = c->iscentred_override = r_node->valueint;
			#endif // PATCH_FLAG_CENTRED
			#if PATCH_CFACTS
			if ((r_node = cJSON_GetObjectItemCaseSensitive(r_json, "set-cfact")) && cJSON_IsNumber(r_node)) c->cfact = r_node->valuedouble;
			#endif // PATCH_CFACTS
			#if PATCH_TERMINAL_SWALLOWING
			if ((r_node = cJSON_GetObjectItemCaseSensitive(r_json, "set-terminal")) && json_isboolean(r_node)) c->isterminal = r_node->valueint;
			#endif // PATCH_TERMINAL_SWALLOWING
			#if PATCH_MOUSE_POINTER_WARPING
			if ((r_node = cJSON_GetObjectItemCaseSensitive(r_json, "set-focus-origin-absolute")) && json_isboolean(r_node)) c->focusabs = r_node->valueint;
			if ((r_node = cJSON_GetObjectItemCaseSensitive(r_json, "set-focus-origin-dx")) && cJSON_IsNumeric(r_node)) {
				c->focusdx = r_node->valuedouble;
				if (
					!c->focusabs && (c->focusdx < -2 || c->focusdx > 2)
				) {
					if (config_warnings && !parsed) {
						logdatetime(stderr);
						fprintf(stderr, "dwm: warning: focus-origin-dx relative value must be between -2 and 2.\n");
					}
					c->focusdx = 1;
				}
				else if (c->focusabs & cJSON_IsNumber(r_node) && config_warnings && !parsed) {
					logdatetime(stderr);
					fprintf(stderr, "dwm: warning: focus-origin-dx absolute value should be an integer.\n");
				}
			}
			if ((r_node = cJSON_GetObjectItemCaseSensitive(r_json, "set-focus-origin-dy")) && cJSON_IsNumeric(r_node)) {
				c->focusdy = r_node->valuedouble;
				if (
					!c->focusabs && (c->focusdy < -2 || c->focusdy > 2)
				) {
					if (config_warnings && !parsed) {
						logdatetime(stderr);
						fprintf(stderr, "dwm: warning: focus-origin-dy relative value must be between -2 and 2.\n");
					}
					c->focusdy = 1;
				}
				else if (c->focusabs & cJSON_IsNumber(r_node) && config_warnings && !parsed) {
					logdatetime(stderr);
					fprintf(stderr, "dwm: warning: focus-origin-dy absolute value should be an integer.\n");
				}
			}
			#endif // PATCH_MOUSE_POINTER_WARPING
			#if PATCH_ATTACH_BELOW_AND_NEWMASTER
			if ((r_node = cJSON_GetObjectItemCaseSensitive(r_json, "set-newmaster")) && json_isboolean(r_node)) c->newmaster = r_node->valueint;
			#endif // PATCH_ATTACH_BELOW_AND_NEWMASTER
			#if PATCH_TERMINAL_SWALLOWING
			if ((r_node = cJSON_GetObjectItemCaseSensitive(r_json, "set-noswallow")) && json_isboolean(r_node)) c->noswallow = r_node->valueint;
			#endif // PATCH_TERMINAL_SWALLOWING
			#if PATCH_FLAG_HIDDEN
			if ((r_node = cJSON_GetObjectItemCaseSensitive(r_json, "set-hidden")) && json_isboolean(r_node)) c->ishidden = r_node->valueint;
			#endif // PATCH_FLAG_HIDDEN

			#if PATCH_WINDOW_ICONS
			#if PATCH_WINDOW_ICONS_CUSTOM_ICONS
			if ((r_node = cJSON_GetObjectItemCaseSensitive(r_json, "set-icon")) && cJSON_IsString(r_node)) {
				c->icon_file = r_node->valuestring;
				c->icon_replace = 1;
			}
			if ((r_node = cJSON_GetObjectItemCaseSensitive(r_json, "set-missing-icon")) && cJSON_IsString(r_node)) {
				c->icon_file = r_node->valuestring;
				c->icon_replace = 0;
			}
			#endif // PATCH_WINDOW_ICONS_CUSTOM_ICONS
			#endif // PATCH_WINDOW_ICONS

			#if PATCH_FLAG_IGNORED
			if ((r_node = cJSON_GetObjectItemCaseSensitive(r_json, "set-ignored")) && json_isboolean(r_node)) c->isignored = r_node->valueint;
			#endif // PATCH_FLAG_IGNORED
			#if PATCH_FLAG_PANEL
			if ((r_node = cJSON_GetObjectItemCaseSensitive(r_json, "set-panel")) && json_isboolean(r_node)) c->ispanel = r_node->valueint;
			#endif // PATCH_FLAG_PANEL
			if ((r_node = cJSON_GetObjectItemCaseSensitive(r_json, "set-floating")) && json_isboolean(r_node)) {
				c->isfloating_override = r_node->valueint;
				if (deferred) {
					if (c->isfloating != c->isfloating_override)
						togglefloatingex(c);
				}
				else
					c->isfloating = c->isfloating_override;
			}
			#if PATCH_FLAG_GAME
			if ((r_node = cJSON_GetObjectItemCaseSensitive(r_json, "set-game")) && json_isboolean(r_node)) c->isgame = r_node->valueint;
			#if PATCH_FLAG_GAME_STRICT
			if ((r_node = cJSON_GetObjectItemCaseSensitive(r_json, "set-game-strict")) && json_isboolean(r_node)) c->isgamestrict = r_node->valueint;
			#endif // PATCH_FLAG_GAME_STRICT
			#endif // PATCH_FLAG_GAME

			#if PATCH_FLAG_TITLE
			if ((r_node = cJSON_GetObjectItemCaseSensitive(r_json, "set-title")) && cJSON_IsString(r_node))
				c->displayname = r_node->valuestring;
			#endif // PATCH_FLAG_TITLE

			#if PATCH_FOCUS_FOLLOWS_MOUSE
			#if PATCH_FLAG_GREEDY_FOCUS
			if ((r_node = cJSON_GetObjectItemCaseSensitive(r_json, "set-greedy-focus")) && json_isboolean(r_node)) c->isgreedy = r_node->valueint;
			#endif // PATCH_FLAG_GREEDY_FOCUS
			#endif // PATCH_FOCUS_FOLLOWS_MOUSE
			#if PATCH_FLAG_FAKEFULLSCREEN
			if ((r_node = cJSON_GetObjectItemCaseSensitive(r_json, "set-fakefullscreen")) && json_isboolean(r_node)) c->fakefullscreen = r_node->valueint;
			#endif // PATCH_FLAG_FAKEFULLSCREEN
			#if PATCH_MODAL_SUPPORT
			if ((r_node = cJSON_GetObjectItemCaseSensitive(r_json, "set-modal")) && json_isboolean(r_node)) c->ismodal = c->ismodal_override = r_node->valueint;
			#endif // PATCH_MODAL_SUPPORT
			#if PATCH_FLAG_NEVER_FOCUS
			if ((r_node = cJSON_GetObjectItemCaseSensitive(r_json, "set-never-focus")) && json_isboolean(r_node)) c->neverfocus = c->neverfocus_override = r_node->valueint;
			#endif // PATCH_FLAG_NEVER_FOCUS
			#if PATCH_FLAG_NEVER_FULLSCREEN
			if ((r_node = cJSON_GetObjectItemCaseSensitive(r_json, "set-never-fullscreen")) && json_isboolean(r_node)) c->neverfullscreen = r_node->valueint;
			#endif // PATCH_FLAG_NEVER_FULLSCREEN
			#if PATCH_FLAG_ACTIVATION_CLICK
			if ((r_node = cJSON_GetObjectItemCaseSensitive(r_json, "set-activation-click")) && cJSON_IsInteger(r_node)) c->activationclick = r_node->valueint;
			#endif // PATCH_FLAG_ACTIVATION_CLICK
			#if PATCH_FLAG_ALWAYSONTOP
			if ((r_node = cJSON_GetObjectItemCaseSensitive(r_json, "set-alwaysontop")) && json_isboolean(r_node)) c->alwaysontop = r_node->valueint;
			#endif // PATCH_FLAG_ALWAYSONTOP

			if ((r_node = cJSON_GetObjectItemCaseSensitive(r_json, "set-floating-width")) && cJSON_IsNumeric(r_node)) {
				if (cJSON_IsInteger(r_node))
					c->sfw = r_node->valueint;
				else {
					c->sfw = c->w * r_node->valuedouble;
				}
				if (c->isfloating)
					c->w = c->sfw;
			}
			if ((r_node = cJSON_GetObjectItemCaseSensitive(r_json, "set-floating-height")) && cJSON_IsNumeric(r_node)) {
				if (cJSON_IsInteger(r_node))
					c->sfh = r_node->valueint;
				else
					c->sfh = c->h * r_node->valuedouble;
				if (c->isfloating)
					c->h = c->sfh;
			}

			if ((r_node = cJSON_GetObjectItemCaseSensitive(r_json, "set-tags-mask")) && cJSON_IsInteger(r_node)) {
				#if PATCH_FLAG_STICKY
				if (r_node->valueint == TAGMASK)
					c->issticky = 1;
				else
				#endif // PATCH_FLAG_STICKY
					c->tags = r_node->valueint;
					//c->tags |= r_node->valueint;
			}
			#if PATCH_FLAG_PAUSE_ON_INVISIBLE
			if ((r_node = cJSON_GetObjectItemCaseSensitive(r_json, "set-pause-on-invisible")) && json_isboolean(r_node)) c->pauseinvisible = r_node->valueint;
			#endif // PATCH_FLAG_PAUSE_ON_INVISIBLE
			#if PATCH_FLAG_STICKY
			if ((r_node = cJSON_GetObjectItemCaseSensitive(r_json, "set-sticky")) && json_isboolean(r_node)) c->issticky = r_node->valueint;
			#endif // PATCH_FLAG_STICKY

			if ((r_node = cJSON_GetObjectItemCaseSensitive(r_json, "set-top-level")) && json_isboolean(r_node)) c->toplevel = r_node->valueint ? 1 : 0;

			#if PATCH_FLAG_FOLLOW_PARENT
			if ((r_node = cJSON_GetObjectItemCaseSensitive(r_json, "set-follow-parent")) && json_isboolean(r_node)) c->followparent = r_node->valueint;
			#endif // PATCH_FLAG_FOLLOW_PARENT

			if ((r_node = cJSON_GetObjectItemCaseSensitive(r_json, "set-urgent")) && json_isboolean(r_node))
				seturgent(c, r_node->valueint);

			if ((r_node = cJSON_GetObjectItemCaseSensitive(r_json, "set-monitor")) && cJSON_IsInteger(r_node)) {
				c->monindex = r_node->valueint;
				for (m = mons; m && m->num != c->monindex; m = m->next);
				if (m)
					c->mon = m;
				#if PATCH_VIRTUAL_MONITORS
				else if (c->monindex >= 1000) {
					for (m = mons; m && m->num != c->monindex % 1000; m = m->next);
					if (m)
						c->mon = m;
				}
				#endif // PATCH_VIRTUAL_MONITORS
			}

			#if PATCH_FLAG_FLOAT_ALIGNMENT
			if ((r_node = cJSON_GetObjectItemCaseSensitive(r_json, "set-float-align-x")) && cJSON_IsNumeric(r_node)) c->floatalignx = (r_node->valuedouble > 1.0f ? 1.0f : r_node->valuedouble);
			if ((r_node = cJSON_GetObjectItemCaseSensitive(r_json, "set-float-align-y")) && cJSON_IsNumeric(r_node)) c->floataligny = (r_node->valuedouble > 1.0f ? 1.0f : r_node->valuedouble);
			if ((r_node = cJSON_GetObjectItemCaseSensitive(r_json, "set-floating-x")) && cJSON_IsNumeric(r_node)) c->floatingx = r_node->valuedouble;
			if ((r_node = cJSON_GetObjectItemCaseSensitive(r_json, "set-floating-y")) && cJSON_IsNumeric(r_node)) c->floatingy = r_node->valuedouble;
			#endif // PATCH_FLAG_FLOAT_ALIGNMENT

			matched = 1;

			#if PATCH_LOG_DIAGNOSTICS
			m = c->mon ? c->mon : selmon;
			if (logrule || m->logallrules) {

				//rule = cJSON_Print(r_json);
				fprintf(stderr,
					"note: Rule matched - after %s mon:%u%s",
					(deferred ? " (deferred):" : ":"),
					m->num,
					(!c->mon ? "(default)" : "")
				);
				logdiagnostics_client_common(c, " ", "");
				fprintf(stderr, " (%ix%i+%ix%i:%ix%i) (pid:%i) ", c->w, c->h, c->x, c->y, (c->x - m->mx), (c->y - m->my), c->pid);
				if (strlen(role))
					fprintf(stderr, "role:\"%s\" ", role);
				else
					fprintf(stderr, "role:<none> ");

				fprintf(stderr,
					"(\"%s\", \"%s\")",
					instance,
					class
				);

				if (c->parent)
					fprintf(stderr, " %sparent:\"%s\"", c->fosterparent ? "[foster]" : "", c->parent->name);
				else
					fprintf(stderr, " parent:<none>");

				fprintf(stderr, " index:%i", c->index);
				/*
				if (rule) {
					fprintf(stderr, "\n%s", rule);
					cJSON_free(rule);
				}
				*/
				fprintf(stderr, "\n");

			}
			#endif // PATCH_LOG_DIAGNOSTICS

			// only apply first matching exclusive rule;
			if (exclusive)
				break;

		}

	}

	// hack to mark broken clients;
	if (c->name[0] == '\0')
		strcpy(c->name, broken);

	#if PATCH_FLAG_NEVER_FOCUS
	if (c->neverfocus_override == 1)
		c->autofocus = 0;
	#endif // PATCH_FLAG_NEVER_FOCUS

	if (
		#if PATCH_FLAG_GAME
		c->isgame == 1 ||
		#endif // PATCH_FLAG_GAME
		#if PATCH_FLAG_IGNORED
		c->isignored == 1 ||
		#endif // PATCH_FLAG_IGNORED
		#if PATCH_FLAG_PANEL
		c->ispanel == 1 ||
		#endif // PATCH_FLAG_PANEL
		0
		) c->isfloating = 1;
	#if PATCH_FLAG_GAME
	#if PATCH_ATTACH_BELOW_AND_NEWMASTER
	if (c->isgame)
		c->newmaster = 1;
	#endif // PATCH_ATTACH_BELOW_AND_NEWMASTER
	#if PATCH_FLAG_FAKEFULLSCREEN
	if (c->isgame)
		c->fakefullscreen = 0;
	#endif // PATCH_FLAG_FAKEFULLSCREEN
	#endif // PATCH_FLAG_GAME
	#if PATCH_TERMINAL_SWALLOWING
	if (!c->noswallow)
		c->noswallow = (c->isfloating
			#if PATCH_FLAG_CENTRED
			|| c->iscentred
			#endif // PATCH_FLAG_CENTRED
		);
	#endif // PATCH_TERMINAL_SWALLOWING

	#if PATCH_FLAG_PANEL
	#if PATCH_FLAG_ALWAYSONTOP
	if (c->ispanel)
		c->alwaysontop = 1;
	#endif // PATCH_FLAG_ALWAYSONTOP
	#endif // PATCH_FLAG_PANEL

	#if PATCH_SHOW_DESKTOP
	if (showdesktop) {
		if (c->isdesktop == -1)
			c->isdesktop = c->wasdesktop;
		if (c->isdesktop) {
			c->toplevel = 1;
			c->fosterparent = 0;
			c->parent = NULL;
			c->ultparent = c;
			c->ondesktop = 0;
		}
	}
	else {
		c->isdesktop = 0;
		c->ondesktop = 0;
	}
	#endif // PATCH_SHOW_DESKTOP

	#if PATCH_TERMINAL_SWALLOWING
	if (!ch.res_class || !ch.res_name)
		c->noswallow = 1;
	#endif // PATCH_TERMINAL_SWALLOWING
	if (ch.res_class)
		XFree(ch.res_class);
	if (ch.res_name)
		XFree(ch.res_name);

	return matched;
}

int
applysizehints(Client *c, int *x, int *y, int *w, int *h, int interact)
{
	int baseismin;
	Monitor *m = c->mon;

	/* set minimum possible */
	*w = MAX(1, *w);
	*h = MAX(1, *h);
	if (interact) {
		if (*x > sw)
			*x = sw - WIDTH(c);
		if (*y > sh)
			*y = sh - HEIGHT(c);
		if (*x + *w + 2 * c->bw < 0)
			*x = 0;
		if (*y + *h + 2 * c->bw < 0)
			*y = 0;
	} else {
		int wx, wy, wh, ww;
		if (c->isfullscreen) {
			wx = m->mx;
			wy = m->my;
			wh = m->mh;
			ww = m->mw;
		}
		else {
			wx = m->wx;
			wy = m->wy;
			wh = m->wh;
			ww = m->ww;
		}
		if (*x >= wx + ww)
			*x = wx + ww - WIDTH(c);
		if (*y >= wy + wh)
			*y = wy + wh - HEIGHT(c);
		if (*x + *w + 2 * c->bw <= wx)
			*x = wx;
		if (*y + *h + 2 * c->bw <= wy)
			*y = wy;
	}
	if (*h < minbh)
		*h = minbh;
	if (*w < minbh)
		*w = minbh;
	if (resizehints || ((c->isfloating || !c->mon->lt[c->mon->sellt]->arrange) && (
			#if PATCH_FLAG_NEVER_RESIZE
			!c->neverresize ||
			#endif // PATCH_FLAG_NEVER_RESIZE
			interact
		)
	)) {
		if (!c->hintsvalid)
			updatesizehints(c);
		/* see last two sentences in ICCCM 4.1.2.3 */
		baseismin = c->basew == c->minw && c->baseh == c->minh;
		if (!baseismin) { /* temporarily remove base dimensions */
			*w -= c->basew;
			*h -= c->baseh;
		}
		/* adjust for aspect limits */
		if (c->mina > 0 && c->maxa > 0) {
			if (c->maxa < (float)*w / *h)
				*w = *h * c->maxa + 0.5;
			else if (c->mina < (float)*h / *w)
				*h = *w * c->mina + 0.5;
		}
		if (baseismin) { /* increment calculation requires this */
			*w -= c->basew;
			*h -= c->baseh;
		}
		/* adjust for increment value */
		if (c->incw)
			*w -= *w % c->incw;
		if (c->inch)
			*h -= *h % c->inch;
		/* restore base dimensions */
		*w = MAX(*w + c->basew, c->minw);
		*h = MAX(*h + c->baseh, c->minh);
		if (c->maxw)
			*w = MIN(*w, c->maxw);
		if (c->maxh)
			*h = MIN(*h, c->maxh);
	}
	return *x != c->x || *y != c->y || *w != c->w || *h != c->h;
}

void
arrange(Monitor *m)
{
	if (m) {
		arrangemon(m);
		restack(m);
		showhide(m->stack, 0);
	} else for (m = mons; m; m = m->next) {
		arrangemon(m);
		restack(m);
		showhide(m->stack, 0);
	}
}

void
arrangemon(Monitor *m)
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstringop-truncation"
	strncpy(m->ltsymbol, m->lt[m->sellt]->symbol, sizeof m->ltsymbol);
#pragma GCC diagnostic pop

	Client *c, *c2 = NULL;

	// attempt cleanup of BadWindow clients;
	// validate all client pids;
	for (c = m->clients; c; c = c->next)
		#if PATCH_FLAG_IGNORED
		if (!validate_pid(c))
			c->isignored = 1;
		#else // NO PATCH_FLAG_IGNORED
		validate_pid(c);
		#endif // PATCH_FLAG_IGNORED

	for (c = m->clients; c; ) {
		c2 = c->next;
		if (c->dormant == -1) {
			detach(c);
			detachstack(c);
			removelinks(c);
			#if PATCH_WINDOW_ICONS
			freeicon(c);
			#endif // PATCH_WINDOW_ICONS
			logdatetime(stderr);
			fprintf(stderr, "debug: freeing BadWindow client: \"%s\"\n", c->name);
			free(c);
		}
		c = c2;
	}

	#if PATCH_CLASS_STACKING
	if (m == selmon && !m->class_stacking && m->sel && (m->sel->isstackhead || m->sel->stackhead) && ISVISIBLE(m->sel)
		#if PATCH_FLAG_HIDDEN
		&& !m->sel->ishidden
		#endif // PATCH_FLAG_HIDDEN
		#if PATCH_FLAG_IGNORED
		&& !m->sel->isignored
		#endif // PATCH_FLAG_IGNORED
		#if PATCH_FLAG_PANEL
		&& !m->sel->ispanel
		#endif // PATCH_FLAG_PANEL
		#if PATCH_SHOW_DESKTOP
		&& !m->sel->isdesktop && !m->sel->ondesktop
		#endif // PATCH_SHOW_DESKTOP
		)
		XSetWindowBorder(dpy, m->sel->win, scheme[SchemeSel][ColBorder].pixel);

	for (c = m->clients; c; c = c->next) {
		c->stackhead = NULL;
		c->isstackhead = 0;
	}
	if (m->class_stacking)
	{
		#if PATCH_ALTTAB && PATCH_ALTTAB_HIGHLIGHT
		if (tabHighlight && altTabMon && altTabMon->isAlt && !(altTabMon->isAlt & ALTTAB_MOUSE) && altTabMon->highlight && altTabMon->highlight->mon == m)
			arrangemon_process_classstack(altTabMon->highlight, 1);
		#endif // PATCH_ALTTAB && PATCH_ALTTAB_HIGHLIGHT
		for (c = m->stack; c; c = c->snext)
		{
			if (!c->snext || c->isfloating
				#if PATCH_ALTTAB && PATCH_ALTTAB_HIGHLIGHT
				|| (tabHighlight && altTabMon && altTabMon->isAlt && !(altTabMon->isAlt & ALTTAB_MOUSE) && altTabMon->highlight == c)
				#endif // PATCH_ALTTAB && PATCH_ALTTAB_HIGHLIGHT
				)
				continue;
			arrangemon_process_classstack(c, 1);
		}

		groupallclassstacks(m);

		if (m->lt[m->sellt]->arrange)
			m->lt[m->sellt]->arrange(m);

		for (c = m->clients; c; c = c->next)
		{
			// force class-stacked clients to match the position/dimensions of the stack head client;
			if (c->stackhead)
				resizeclient(c, c->stackhead->x, c->stackhead->y, c->stackhead->w, c->stackhead->h, 0);
			else if (c->isstackhead && m->sel == c)
				XSetWindowBorder(dpy, c->win, scheme[SchemeUrg][ColBorder].pixel);
		}
	}
	else
	#endif // PATCH_CLASS_STACKING
	if (m->lt[m->sellt]->arrange)
		m->lt[m->sellt]->arrange(m);
	/*
	// compensate for possible earlier exclusion of window border;
	if (m == selmon && m->sel && !solitary(m->sel)
		#if PATCH_ALTTAB
		&& !altTabMon
		#endif // PATCH_ALTTAB
		)
		XSetWindowBorder(dpy, m->sel->win, scheme[SchemeSel][ColBorder].pixel);
	*/
}

#if PATCH_CLASS_STACKING
void
arrangemon_process_classstack(Client *c, int added_to_stack)
{
	XClassHint ch = { NULL, NULL };
	XClassHint ch2 = { NULL, NULL };

	if ((added_to_stack && (!c->snext || !ISVISIBLE(c))) || c->isfloating || c->stackhead
		#if PATCH_FLAG_HIDDEN
		|| c->ishidden
		#endif // PATCH_FLAG_HIDDEN
		#if PATCH_FLAG_IGNORED
		|| c->isignored
		#endif // PATCH_FLAG_IGNORED
		#if PATCH_FLAG_PANEL
		|| c->ispanel
		#endif // PATCH_FLAG_PANEL
		#if PATCH_SHOW_DESKTOP
		|| c->isdesktop || c->ondesktop
		#endif // PATCH_SHOW_DESKTOP
	) return;
	if (!c->stackclass) {
		XGetClassHint(dpy, c->win, &ch);
		if (ch.res_name)
			XFree(ch.res_name);
		if (!ch.res_class)
			return;
	}
	for (Client *c2 = added_to_stack ? c->snext : c->mon->stack; c2; c2 = c2->snext)
	{
		if (c2->isfloating || !ISVISIBLE(c2) || c2->stackhead
			#if PATCH_FLAG_HIDDEN
			|| c2->ishidden
			#endif // PATCH_FLAG_HIDDEN
			#if PATCH_FLAG_IGNORED
			|| c2->isignored
			#endif // PATCH_FLAG_IGNORED
		)
			continue;
		if (!c2->stackclass) {
			XGetClassHint(dpy, c2->win, &ch2);
			if (ch2.res_name)
				XFree(ch2.res_name);
			if (!ch2.res_class)
				continue;
			if (
				(c->stackclass && strcmp(ch2.res_class, c->stackclass) == 0) ||
				(!c->stackclass && strcmp(ch.res_class, ch2.res_class) == 0)
				)
				c2->stackhead = c;
		}
		else if (
			(c->stackclass && strcmp(c->stackclass, c2->stackclass) == 0) ||
			(!c->stackclass && strcmp(ch.res_class, c2->stackclass) == 0)
			)
			c2->stackhead = c;
		if (ch2.res_class) {
			XFree(ch2.res_class);
			ch2.res_class = NULL;
		}
		if (c2->stackhead)
			c->isstackhead = 1;
	}
	if (ch.res_class) {
		XFree(ch.res_class);
		ch.res_class = NULL;
	}
	if (added_to_stack && ISVISIBLE(c) &&
		(
			(c->mon == selmon && (c->isstackhead || c->stackhead) && c->mon->sel == c)
			#if PATCH_ALTTAB && PATCH_ALTTAB_HIGHLIGHT
			|| (tabHighlight && altTabMon && altTabMon->isAlt && !(altTabMon->isAlt & ALTTAB_MOUSE) && altTabMon->highlight == c)
			#endif // PATCH_ALTTAB && PATCH_ALTTAB_HIGHLIGHT
		)
		#if PATCH_FLAG_HIDDEN
		&& !c->ishidden
		#endif // PATCH_FLAG_HIDDEN
		#if PATCH_FLAG_IGNORED
		&& !c->isignored
		#endif // PATCH_FLAG_IGNORED
		#if PATCH_FLAG_PANEL
		&& !c->ispanel
		#endif // PATCH_FLAG_PANEL
		#if PATCH_SHOW_DESKTOP
		&& !c->isdesktop && !c->ondesktop
		#endif // PATCH_SHOW_DESKTOP
		)
		XSetWindowBorder(dpy, c->win, scheme[SchemeUrg][ColBorder].pixel);
	
}

int
attach_stackhead(Client *c)
{
	if (c->isfloating || !ISVISIBLE(c)
		#if PATCH_FLAG_HIDDEN
		|| c->ishidden
		#endif // PATCH_FLAG_HIDDEN
		#if PATCH_FLAG_IGNORED
		|| c->isignored
		#endif // PATCH_FLAG_IGNORED
		#if PATCH_FLAG_PANEL
		|| c->ispanel
		#endif // PATCH_FLAG_PANEL
		#if PATCH_SHOW_DESKTOP
		|| c->isdesktop || c->ondesktop
		#endif // PATCH_SHOW_DESKTOP
	) return 0;
	
	if(!c->stackhead)
	{
		arrangemon_process_classstack(c, 0);
		if(!(c->isstackhead || c->stackhead))
			return 0;
	}
	Client *h, *sh = c->stackhead;

	if(!sh)
	{
		for(sh = c->mon->stack; sh; sh = sh->snext)
		{
			if (sh->isfloating || !ISVISIBLE(sh)
				#if PATCH_FLAG_HIDDEN
				|| sh->ishidden
				#endif // PATCH_FLAG_HIDDEN
				#if PATCH_FLAG_IGNORED
				|| sh->isignored
				#endif // PATCH_FLAG_IGNORED
				#if PATCH_FLAG_PANEL
				|| sh->ispanel
				#endif // PATCH_FLAG_PANEL
				#if PATCH_SHOW_DESKTOP
				|| sh->isdesktop || sh->ondesktop
				#endif // PATCH_SHOW_DESKTOP
			) continue;
			break;
		}
		if(!sh)
			return 0;
	}

	for(h = c->mon->clients; h && h->next; h = h->next)
	{
		if(h == sh)
		{
			h->stackhead = c;
			h->isstackhead = 0;
			c->stackhead = NULL;
			c->isstackhead = 1;
			attach(c);
			attachstack(c);
			return 1;
		}
		if(h->next == sh)
		{
			h->next->stackhead = c;
			h->next->isstackhead = 0;
			c->next = h->next;
			h->next = c;
			c->stackhead = NULL;
			c->isstackhead = 1;
			break;
		}
	}
	if(!c->isstackhead)
		return 0;

	for(h = c->mon->stack; h && h->snext; h = h->snext)
	{
		if(c->next == h)
		{
			attachstack(c);
			return 1;
		}
		if(c->next == h->snext)
		{
			c->sprev = h;
			h->snext->sprev = c;
			c->snext = h->snext->snext;
			h->snext = c;
			return 1;
		}
	}
	return 0;
}

#endif // PATCH_CLASS_STACKING

void
attach(Client *c)
{
	c->next = c->mon->clients;
	c->mon->clients = c;
}

#if PATCH_ATTACH_BELOW_AND_NEWMASTER
void
attachBelow(Client *c)
{
	// if there is nothing on the monitor or the selected client is floating, attach as normal
	if(c->mon->sel == NULL || c->mon->sel->isfloating || !ISVISIBLE(c)) {
		int nmaster = 1;
		for (int i = 0; i < LENGTH(tags); i++) {
			if ((c->tags & (1 << i)) > 0) {
				#if PATCH_PERTAG
				nmaster = c->mon->pertag->nmasters[i + 1];
				#else // NO PATCH_PERTAG
				nmaster = c->mon->nmaster;
				#endif // PATCH_PERTAG
				break;
			}
		}
		Client *lastmatch = NULL;
		Client *at = c->mon->clients;
		for (; nmaster > 0; nmaster--) {
			if (!(at = nexttaggedafter(at, c->tags))) break;
			lastmatch = at;
			at = at->next;
		}
		if(!lastmatch) {
			attach(c);
			return;
		}
		c->next = lastmatch->next;
		lastmatch->next = c;
		return;
	}

	// set the new client's next property to the same as the currently selected clients next
	c->next = c->mon->sel->next;
	// set the currently selected clients next property to the new client
	c->mon->sel->next = c;

}
#endif // PATCH_ATTACH_BELOW_AND_NEWMASTER

void
attachstack(Client *c)
{
	if (!c)
		return;
	if (c == c->mon->stack) {
		logdatetime(stderr);
		fprintf(stderr, "debug: attachstack: ALREADY ATTACHED! c == c->mon->stack \"%s\"\n", c->name);
		return;
	}

	// attempt to keep floating clients above tiled in the stack;
	Client *f = c->mon->stack;
	if (!c->isfloating
		#if PATCH_FLAG_PANEL
		|| c->ispanel
		#endif // PATCH_FLAG_PANEL
	)
		for (;
			f && f->isfloating
			#if PATCH_SHOW_DESKTOP
			&& !f->isdesktop
			#endif // PATCH_SHOW_DESKTOP
			#if PATCH_FLAG_PANEL
			&& !f->ispanel
			#endif // PATCH_FLAG_PANEL
			; f = f->snext
		);

	if (!f || f == c->mon->stack || (c->isfloating
		#if PATCH_SHOW_DESKTOP
		&& !c->isdesktop
		#endif // PATCH_SHOW_DESKTOP
		#if PATCH_FLAG_PANEL
		&& !c->ispanel
		#endif // PATCH_FLAG_PANEL
	)) {
		// default stack behaviour - client to the top;
		c->sprev = NULL;
		if ((c->snext = c->mon->stack))
			c->snext->sprev = c;
		c->mon->stack = c;

		// floating client on top of stack, will probably want to be the new selection;
		if (c->mon != selmon && ISVISIBLE(c) && !c->neverfocus && c->isfloating
			#if PATCH_FLAG_HIDDEN
			&& !c->ishidden
			#endif // PATCH_FLAG_HIDDEN
			#if PATCH_FLAG_IGNORED
			&& !c->isignored
			#endif // PATCH_FLAG_IGNORED
			#if PATCH_FLAG_PANEL
			&& !c->ispanel
			#endif // PATCH_FLAG_PANEL
			#if PATCH_SHOW_DESKTOP
			&& !c->isdesktop
			#endif // PATCH_SHOW_DESKTOP
			)
			c->mon->sel = c;
	}
	else {
		if ((c->sprev = f->sprev))
			c->sprev->snext = c;
		if ((c->snext = f))
			c->snext->sprev = c;
	}
}

void
attachstackex(Client *c)
{
	if (!c || !c->mon)
		return;
	c->sprev = NULL;
	if ((c->snext = c->mon->stack))
		c->snext->sprev = c;
	c->mon->stack = c;
}

#if PATCH_ATTACH_BELOW_AND_NEWMASTER
void
attachstackBelow(Client *c)
{
	// if no client is selected on the monitor or the selected client is floating,
	// and the new client will be visible on the currently selected tag, then attach as normal;
	// otherwise:
	if((c->mon->sel && !c->mon->sel->isfloating) || !ISVISIBLE(c)) {

		// find nmaster for (first) client tag;
		int nmaster = 1;
		for (int i = 0; i < LENGTH(tags); i++) {
			if ((c->tags & (1 << i)) > 0) {
				#if PATCH_PERTAG
				nmaster = c->mon->pertag->nmasters[i + 1];
				#else // NO PATCH_PERTAG
				nmaster = c->mon->nmaster;
				#endif // PATCH_PERTAG
				break;
			}
		}

		// attempt to compensate for nmaster>1
		Client *lastmatch = NULL;
		for(Client *walked = c->mon->stack; walked && walked != walked->snext; walked = walked->snext) {
			if (ISVISIBLEONTAG(walked, c->tags) && !(
				walked->isfloating
				#if PATCH_FLAG_HIDDEN
				|| walked->ishidden
				#endif // PATCH_FLAG_HIDDEN
				#if PATCH_FLAG_PANEL
				|| walked->ispanel
				#endif // PATCH_FLAG_PANEL
				)) {
				lastmatch = walked;
				// if we've gone past all master clients on the tag,
				// or there aren't any more clients, then insert the new client;
				if (--nmaster == 0 || !walked->snext) {
					DEBUG("attaching \"%s\" after \"%s\"\n", c->name, walked->name);
					c->snext = walked->snext;
					if (c->snext)
						c->snext->sprev = c;
					c->sprev = walked;
					walked->snext = c;
					return;
				}
			}
		}

		// if there was at least 1 client on the tag,
		// but weren't more than nmaster clients on the tag;
		if (lastmatch) {
			//if (lastmatch->snext)
			//	lastmatch = lastmatch->snext;
			c->snext = lastmatch->snext;
			if (c->snext)
				c->snext->sprev = c;
			c->sprev = lastmatch;
			lastmatch->snext = c;
			return;
		}

		// if there are no clients on the tag, then we attach as normal;
	}
	attachstack(c);
}
#endif // PATCH_ATTACH_BELOW_AND_NEWMASTER

#if PATCH_TERMINAL_SWALLOWING
void
swallow(Client *p, Client *c)
{

	if (c->noswallow || c->isterminal)
		return;
	if (c->isfloating ||
		#if PATCH_FLAG_GAME
		c->isgame ||
		#endif // PATCH_FLAG_GAME
		c->isfixed)
		return;
	if (!(p->tags & c->tags && p->mon == c->mon))
		return;
	detach(c);
	detachstackex(c);
	if (c->mon->sel == c)
		c->mon->sel = p;

	setclientstate(c, WithdrawnState);
	XUnmapWindow(dpy, p->win);

	p->swallowing = c;
	c->mon = p->mon;
	c->monindex = p->monindex;

	Window w = p->win;
	p->win = c->win;
	c->win = w;

	updatetitle(p, 1);
	XMoveResizeWindow(dpy, p->win, p->x, p->y, p->w, p->h);
	#if PATCH_WINDOW_ICONS
	freeicon(p);
	p->icon = c->icon;
	#endif // PATCH_WINDOW_ICONS

	arrange(p->mon);
	configure(p);
	updateclientlist();
}

void
unswallow(Client *c)
{
	DEBUG("unswallow(c): %s\n", c->name);

	c->win = c->swallowing->win;
	#if PATCH_WINDOW_ICONS
	updateicon(c);
	#endif // PATCH_WINDOW_ICONS

	free(c->swallowing);
	c->swallowing = NULL;

	/* unfullscreen the client */
	setfullscreen(c, 0);
	updatetitle(c, 1);
	arrange(c->mon);
	XMapWindow(dpy, c->win);
	XMoveResizeWindow(dpy, c->win, c->x, c->y, c->w, c->h);
	setclientstate(c, NormalState);
	focus(NULL, 0);
	arrange(c->mon);
}
#endif // PATCH_TERMINAL_SWALLOWING

void
buttonpress(XEvent *e)
{
	unsigned int i, x = 0, click, occ = 0;
	int zone = -1;
	Arg arg = {0};
	Client *c;
	Monitor *m = selmon, *lastmon = selmon;
	XButtonPressedEvent *ev = &e->xbutton;
	#if PATCH_STATUSCMD
	unsigned int tw = 0;
	char *text, *s, ch;
	#if PATCH_STATUSCMD_COLOURS || PATCH_STATUSCMD_NONPRINTING
	char *s2;
	char buffer[256];
	size_t bufsize, bufptr;
	#endif // PATCH_STATUSCMD_COLOURS || PATCH_STATUSCMD_NONPRINTING
	#endif // PATCH_STATUSCMD
	click = ClkRootWin;
	/* focus monitor if necessary */
	if ((m = wintomon(ev->window)) && m != selmon) {
		#if PATCH_FLAG_GAME && PATCH_FLAG_GAME_STRICT
		unfocus(selmon->sel, 1 | (1 << 1));
		#else
		unfocus(selmon->sel, 1);
		#endif // PATCH_FLAG_GAME && PATCH_FLAG_GAME_STRICT
		selmon = m;
		drawbar(lastmon, 1);
		focus(NULL, 0);
	}
	#if PATCH_FOCUS_BORDER
	else if (ev->window == focuswin && ev->button == 1 && selmon->sel) {
		if (selmon->sel->isfloating || !selmon->lt[selmon->sellt]->arrange)
			movemouse(&(Arg){.i = 1});
		else
			placemouse(&(Arg){.i = 1});
		return;
	}
	#endif // PATCH_FOCUS_BORDER
	if (ev->window == m->barwin) {

		// determine zone of interaction;
		for (i = 0; i < LENGTH(m->bar); i++)
			if (m->bar[i].x != -1 && ev->x >= m->bar[i].x && ev->x <= (m->bar[i].x + m->bar[i].w)) {
				zone = i;
				break;
			}
		if (zone < 0)
			return;

		x = m->bar[zone].x;

		switch (m->bar[zone].type) {
			case TagBar:
				i = 0;
				for (c = m->clients; c; c = c->next)
					if (!c->dormant
						#if PATCH_FLAG_PANEL
						&& !c->ispanel
						#endif // PATCH_FLAG_PANEL
						#if PATCH_FLAG_IGNORED
						&& !c->isignored
						#endif // PATCH_FLAG_IGNORED
						)
						occ |= c->tags == 255 ? 0 : c->tags;
				do {
					#if PATCH_HIDE_VACANT_TAGS
					// do not reserve space for vacant tags;
					if (m->hidevacant && !m->alwaysvisible[i] &&
						!(occ & 1 << i || m->tagset[m->seltags] & 1 << i))
						continue;
					#endif // PATCH_HIDE_VACANT_TAGS
					x += selmon->tagw[i];
				} while (ev->x >= x && ++i < LENGTH(tags));
				if (i < LENGTH(tags)) {
					click = ClkTagBar;
					arg.ui = 1 << i | 1 << 31;	// use bit 31 to indicate ClkTagBar in view();
				}
				break;

			case LtSymbol:
				#if PATCH_SHOW_DESKTOP
				if (showdesktop && m->showdesktop) {
					if (ev->button == 1)
						toggledesktop(0);
				}
				else
				#endif // PATCH_SHOW_DESKTOP
				click = ClkLtSymbol;
				break;

			case WinTitle:
				click = ClkWinTitle;
				break;

			case StatusText:
				click = ClkStatusText;
				#if PATCH_STATUSCMD
				#if PATCH_SYSTRAY
				if (showsystray && systrayonleft)
					x += m->stw;
				#endif // PATCH_SYSTRAY
				statussig = 0;
				#if PATCH_FONT_GROUPS
				apply_barelement_fontgroup(m, StatusText);
				#endif // PATCH_FONT_GROUPS

				#if PATCH_STATUSCMD_COLOURS || PATCH_STATUSCMD_NONPRINTING
				strncpy(buffer, stext, sizeof buffer);
				bufsize = strlen(buffer);
				for (s = text = buffer; *s && --bufsize; s++)

					while (bufsize > 1 && (unsigned char)(*s) == '^') {
						s2 = s;
						while ((unsigned char)(*(++s2)) != '^' && --bufsize);
						if (!bufsize)
							break;
						s2++;
						text = s;
						#if PATCH_STATUSCMD_NONPRINTING
						if ((unsigned char)(*(s + 1)) == 'N') {
							for (s += 2, bufptr = bufsize; (unsigned char)(*s) != '^' && --bufsize; s++)
								*(text + (bufptr - bufsize - 1)) = (unsigned char)(*s);
							if (!bufsize)
								break;
							s = text + (bufptr - bufsize);
						}
						#endif // PATCH_STATUSCMD_NONPRINTING
						for (bufptr = bufsize; bufptr--; s2++)
							*(s + (bufsize - bufptr - 1)) = (unsigned char)(*s2);
						*(s + bufsize) = '\0';
					}
				#endif // PATCH_STATUSCMD_COLOURS || PATCH_STATUSCMD_NONPRINTING

				x += lrpad / 2;
				for (text = s =
					#if PATCH_STATUSCMD_COLOURS || PATCH_STATUSCMD_NONPRINTING
					buffer
					#else // NO PATCH_STATUSCMD_COLOURS || PATCH_STATUSCMD_NONPRINTING
					stext
					#endif // PATCH_STATUSCMD_COLOURS || PATCH_STATUSCMD_NONPRINTING
					; *s && x <= ev->x; s++) {
					if ((unsigned char)(*s) < ' ') {
						ch = *s;
						*s = '\0';
						tw = drw_fontset_getwidth(drw, text);
						if (tw)
							x += tw + lrpad / 2;
						*s = ch;
						text = s + 1;
						if (x >= ev->x)
							break;
						statussig = ch;
					}
				}
				#endif // PATCH_STATUSCMD
				break;

			#if PATCH_SHOW_DESKTOP
			#if PATCH_SHOW_DESKTOP_BUTTON
			case ShowDesktop:
				click = ClkShowDesktop;
				break;
			#endif // PATCH_SHOW_DESKTOP_BUTTON
			#endif // PATCH_SHOW_DESKTOP

			default:
				return;
		}

	} else if ((c = wintoclient(ev->window))) {
		if (selmon->sel != c)
			focus(c, 1);
		XAllowEvents(dpy, ReplayPointer, CurrentTime);
		click = ClkClientWin;
	}
	for (i = 0; i < LENGTH(buttons); i++)
		if (click == buttons[i].click && buttons[i].func && buttons[i].button == ev->button
		#if PATCH_STATUSCMD && PATCH_STATUSCMD_MODIFIERS
		&& (click == ClkStatusText || CLEANMASK(buttons[i].mask) == CLEANMASK(ev->state))) {
			if (click == ClkStatusText) {
				arg.i = buttons[i].arg.i;
				x = CLEANMASK(ev->state);
				if (x & ShiftMask)
					arg.i |= 0x100;
				if (x & ControlMask)
					arg.i |= 0x200;
				if (x & Mod1Mask)
					arg.i |= 0x400;
				if (x & Mod2Mask)
					arg.i |= 0x800;
				if (x & Mod3Mask)
					arg.i |= 0x1000;
				if (x & Mod4Mask)
					arg.i |= 0x2000;
				if (x & Mod5Mask)
					arg.i |= 0x4000;
				buttons[i].func(&arg);
			}
			else
		#else // NO PATCH_STATUSCMD && PATCH_STATUSCMD_MODIFIERS
		&& CLEANMASK(buttons[i].mask) == CLEANMASK(ev->state)) {
		#endif // PATCH_STATUSCMD && PATCH_STATUSCMD_MODIFIERS
				buttons[i].func(click == ClkTagBar && buttons[i].arg.i == 0 ? &arg : &buttons[i].arg);
		}
}

#if PATCH_CLIENT_OPACITY
void
changefocusopacity(const Arg *arg)
{
	#if PATCH_FOCUS_FOLLOWS_MOUSE
	checkmouseoverclient();
	#endif // PATCH_FOCUS_FOLLOWS_MOUSE
	if (!selmon->sel)
		return;
	if (selmon->sel->opacity < 0)
		selmon->sel->opacity = selmon->activeopacity;
	selmon->sel->opacity+=arg->f;
	if (selmon->sel->opacity > 1.0) selmon->sel->opacity = 1.0;
	if (selmon->sel->opacity < 0.1) selmon->sel->opacity = 0.1;
	opacity(selmon->sel, 1);
}

void
changeunfocusopacity(const Arg *arg)
{
	#if PATCH_FOCUS_FOLLOWS_MOUSE
	checkmouseoverclient();
	#endif // PATCH_FOCUS_FOLLOWS_MOUSE
	if (!selmon->sel)
		return;
	if (selmon->sel->unfocusopacity < 0)
		selmon->sel->unfocusopacity = selmon->inactiveopacity;
	selmon->sel->unfocusopacity+=arg->f;
	if (selmon->sel->unfocusopacity > 1.0) selmon->sel->unfocusopacity = 1.0;
	if (selmon->sel->unfocusopacity < 0.1) selmon->sel->unfocusopacity = 0.1;
	opacity(selmon->sel, 0);
}
#endif // PATCH_CLIENT_OPACITY

#if PATCH_FOCUS_FOLLOWS_MOUSE
void
checkmouseoverclient(void)
{
	int x, y;
	if (!getrootptr(&x, &y))
		return;

	Monitor *m = recttomon(x, y, 1, 1);
	if (m != selmon)
		focusmonex(m);

	Client *r = getclientatcoords(x, y, 0);
	if (r)
		focus(r, 1);
}

void
checkmouseovermonitor(Monitor *m)
{
	int x, y;
	if (!getrootptr(&x, &y))
		return;
	Monitor *mm = recttomon(x, y, 1, 1);
	if (m == mm)
		return;
	focusmonex(mm);
	focus(NULL, 0);
}
/*
void
checkmouseposition(Monitor *m, Client *c)
{
	if (!c)
		checkmouseovermonitor(m ? m : selmon);
	else
		checkmouseoverclient(c);
}
*/
#endif // PATCH_FOCUS_FOLLOWS_MOUSE

void
checkotherwm(void)
{
	xerrorxlib = XSetErrorHandler(xerrorstart);
	// this causes an error if some other window manager is running;
	XSelectInput(dpy, DefaultRootWindow(dpy), SubstructureRedirectMask);
	XSync(dpy, False);
	XSetErrorHandler(xerror);
	XSync(dpy, False);
}

#if DEBUGGING || PATCH_LOG_DIAGNOSTICS
int
checkstack(Monitor *mon)
{
	Client *c;
	int ok = 1;
	for (c = mon->stack; c && c->snext != c; c = c->snext);
	if (c) {
		logdatetime(stderr);
		fprintf(stderr, "debug: checkstack: FATAL: client == client->snext c:\"%s\"\n", c->name);
		sleep(2);
		ok = 0;
	}
	else {
		for (c = mon->stack; c && c->snext; c = c->snext);
		for (; c && c->sprev != c; c = c->sprev);
		if (c) {
			logdatetime(stderr);
			fprintf(stderr, "debug: checkstack: client == client->sprev c:\"%s\" - rebuilding stack\n", c->name);
			mon->stack->sprev = NULL;
			for (c = mon->stack; c && c->snext; c = c->snext)
				c->snext->sprev = c;
		}
	}
	for (c = mon->clients; c && c->next != c; c = c->next);
	if (c) {
		logdatetime(stderr);
		fprintf(stderr, "debug: checkstack: FATAL: client == client->next c:\"%s\"\n", c->name);
		sleep(2);
		ok = 0;
	}
	return ok;
}
#endif // DEBUGGING || PATCH_LOG_DIAGNOSTICS

void
cleanup(void)
{
	Layout foo = { "", NULL };
	Monitor *m;
	size_t i;

	nonstop = 1;

logdatetime(stderr);
fprintf(stderr, "dwm: starting cleanup...\n");

	free(charcodes);

	#if PATCH_ALTTAB
	altTabEnd();
	#endif // PATCH_ALTTAB

	#if PATCH_FOCUS_BORDER || PATCH_FOCUS_PIXEL
	if (focuswin) {
		XDestroyWindow(dpy, focuswin);
		focuswin = None;
	}
	#endif // PATCH_FOCUS_BORDER || PATCH_FOCUS_PIXEL

	for (Monitor *m = mons; m; m = m->next) {
		m->lt[m->sellt] = &foo;
		viewmontag(m, ~0, 0);
	}

	#if PATCH_PERSISTENT_METADATA
	int index = 0;
	for (Monitor *m = mons; m; m = m->next)
		for (Client *c = m->clients; c; c = c->next) {
			if (c->isfullscreen
				#if PATCH_FLAG_GAME
				&& !c->isgame
				#endif // PATCH_FLAG_GAME
				#if PATCH_FLAG_FAKEFULLSCREEN
				&& c->fakefullscreen != 1
				#endif // PATCH_FLAG_FAKEFULLSCREEN
			) {
				#if PATCH_FLAG_FAKEFULLSCREEN
				c->fakefullscreen = 2;
				#endif // PATCH_FLAG_FAKEFULLSCREEN
				setfullscreen(c, 0);
			}
			#if PATCH_SHOW_DESKTOP
			if (!showdesktop || !c->isdesktop)
			#endif // PATCH_SHOW_DESKTOP
			setclienttagpropex(c, ++index);
		}
	#if PATCH_SHOW_DESKTOP
	if (showdesktop)
		for (Monitor *m = mons; m; m = m->next)
			for (Client *c = m->clients; c; c = c->next)
				if (c->isdesktop)
					setclienttagpropex(c, ++index);
	#endif // PATCH_SHOW_DESKTOP
	#else // NO PATCH_PERSISTENT_METADATA
	for (Monitor *m = mons; m; m = m->next)
		for (Client *c = m->clients; c; c = c->next)
			if (c->isfullscreen
				#if PATCH_FLAG_GAME
				&& !c->isgame
				#endif // PATCH_FLAG_GAME
				) {
				setfullscreen(c, 0);
			}
	#endif // PATCH_PERSISTENT_METADATA

	#if PATCH_TORCH
	if (torchwin) {
		XUnmapWindow(dpy, torchwin);
		XDestroyWindow(dpy, torchwin);
	}
	#endif // PATCH_TORCH
	#if PATCH_CONSTRAIN_MOUSE
	constrained = NULL;
	#endif // PATCH_CONSTRAIN_MOUSE
	#if PATCH_FLAG_GAME || PATCH_CONSTRAIN_MOUSE
	destroybarrier();
	#endif // PATCH_FLAG_GAME || PATCH_CONSTRAIN_MOUSE

	#if PATCH_SCAN_OVERRIDE_REDIRECTS
	Client *nc;
	for (Client *c = orlist; c; ) {
		nc = c->next;
		free(c);
		c = nc;
	}
	#endif // PATCH_SCAN_OVERRIDE_REDIRECTS

	for (m = mons; m; m = m->next)
		while (m->stack)
			unmanage(m->stack, 0, 1);

logdatetime(stderr);
fprintf(stderr, "dwm: done unmanage().\n");

	XUngrabKey(dpy, AnyKey, AnyModifier, root);
	while (mons)
		cleanupmon(mons);

logdatetime(stderr);
fprintf(stderr, "dwm: done cleanupmon().\n");

	#if PATCH_CUSTOM_TAG_ICONS
	if (dummyc)
		free(dummyc);
	#endif // PATCH_CUSTOM_TAG_ICONS

	#if PATCH_SYSTRAY
	if (showsystray) {
		for (Client *ii = systray->icons; ii; ii = ii->next)
			XReparentWindow(dpy, ii->win, root, 0, 0);
		XUnmapWindow(dpy, systray->win);
		XDestroyWindow(dpy, systray->win);
		free(systray);
	}
	#endif // PATCH_SYSTRAY

	for (i = 0; i < CurLast; i++)
		drw_cur_free(drw, cursor[i]);
	for (i = 0; i < LENGTH(colours); i++)
		free(scheme[i]);
	free(scheme);

	XDestroyWindow(dpy, wmcheckwin);
	drw_free(drw);
	XSync(dpy, False);
	XSetInputFocus(dpy, PointerRoot, RevertToPointerRoot, CurrentTime);
	XDeleteProperty(dpy, root, netatom[NetActiveWindow]);

logdatetime(stderr);
fprintf(stderr, "dwm: done cleanup.\n");

	nonstop = 0;
}

void
cleanupmon(Monitor *mon)
{
	Monitor *m;

	if (mon == mons)
		mons = mons->next;
	else {
		for (m = mons; m && m->next != mon; m = m->next);
		m->next = mon->next;
	}
	if (mon->barwin != None) {
		XUnmapWindow(dpy, mon->barwin);
		XDestroyWindow(dpy, mon->barwin);
	}
	#if PATCH_CUSTOM_TAG_ICONS
	for (int i = 0; i < LENGTH(tags); i++)
		if (mon->tagicons[i])
			XRenderFreePicture(dpy, mon->tagicons[i]);
	#endif // PATCH_CUSTOM_TAG_ICONS
	#if PATCH_PERTAG
	free(mon->pertag);
	#endif // PATCH_PERTAG
	free(mon);
}

void
clearurgency(const Arg *arg)
{
	for (Monitor *m = mons; m; m = m->next)
		for (Client *c = m->clients; c; c = c->next)
			if (c->isurgent)
				seturgent(c, 0);
}

void
clientmessage(XEvent *e)
{
	XClientMessageEvent *cme = &e->xclient;
	Client *c = wintoclient(cme->window);

	#if PATCH_SYSTRAY
	XWindowAttributes wa;
	XSetWindowAttributes swa;
	if (showsystray && cme->window == systray->win && cme->message_type == netatom[NetSystemTrayOP]) {
		/* add systray icons */
		if (cme->data.l[1] == SYSTEM_TRAY_REQUEST_DOCK) {
			if (!(c = (Client *)calloc(1, sizeof(Client))))
				die("fatal: could not malloc() %u bytes\n", sizeof(Client));
			if (!(c->win = cme->data.l[2])) {
				free(c);
				return;
			}
			c->mon = selmon;
			c->monindex = -1;
			c->next = systray->icons;
			systray->icons = c;
			if (!XGetWindowAttributes(dpy, c->win, &wa)) {
				/* use sane defaults */
				wa.width = minbh;
				wa.height = wa.width; //minbh;
				wa.border_width = 0;
			}
			c->x = c->oldx = c->y = c->oldy = 0;
			c->w = c->oldw = wa.width;
			c->h = c->oldh = wa.height;
			c->oldbw = wa.border_width;
			c->bw = 0;
			c->isfloating = True;
			/* reuse tags field as mapped status */
			c->tags = 1;
			updatesizehints(c);
			updatesystrayicongeom(c, wa.width, wa.height);
			XAddToSaveSet(dpy, c->win);
			XSelectInput(dpy, c->win, StructureNotifyMask | PropertyChangeMask | ResizeRedirectMask);
			XReparentWindow(dpy, c->win, systray->win, 0, 0);
			// use parents background colour;
			swa.background_pixel = scheme[SchemeNorm][ColBg].pixel;
			XChangeWindowAttributes(dpy, c->win, CWBackPixel, &swa);
			sendevent(c->win, netatom[Xembed], StructureNotifyMask, CurrentTime, XEMBED_EMBEDDED_NOTIFY, 0 , systray->win, XEMBED_EMBEDDED_VERSION);
			// FIXME not sure if I have to send these events, too ;
			sendevent(c->win, netatom[Xembed], StructureNotifyMask, CurrentTime, XEMBED_FOCUS_IN, 0 , systray->win, XEMBED_EMBEDDED_VERSION);
			sendevent(c->win, netatom[Xembed], StructureNotifyMask, CurrentTime, XEMBED_WINDOW_ACTIVATE, 0 , systray->win, XEMBED_EMBEDDED_VERSION);
			sendevent(c->win, netatom[Xembed], StructureNotifyMask, CurrentTime, XEMBED_MODALITY_ON, 0 , systray->win, XEMBED_EMBEDDED_VERSION);
			XSync(dpy, False);
			updatesystray(1);
			resizebarwin(selmon);
			setclientstate(c, NormalState);
		}
		return;
	}
	else
	#endif // PATCH_SYSTRAY

	if (!c
		#if PATCH_CROP_WINDOWS
		&& !(c = cropwintoclient(cme->window))
		#endif // PATCH_CROP_WINDOWS
		)
		return;
	if (cme->message_type == netatom[NetWMState]) {
		if (cme->data.l[1] == netatom[NetWMAttention]
		|| cme->data.l[2] == netatom[NetWMAttention]) {
			if (!c->isurgent && urgency) {
				seturgent(c, 1);
				if (ISVISIBLE(c) && !MINIMIZED(c))
					drawbar(c->mon, 0);
			}
			return;
		}
		else if (cme->data.l[1] == netatom[NetWMFullscreen]
		#if PATCH_FLAG_FAKEFULLSCREEN
		|| cme->data.l[2] == netatom[NetWMFullscreen]) {
			if (c->fakefullscreen == 2 && c->isfullscreen)
				c->fakefullscreen = 3;
		#else // NO PATCH_FLAG_FAKEFULLSCREEN
		|| cme->data.l[2] == netatom[NetWMFullscreen]) {
		#endif // PATCH_FLAG_FAKEFULLSCREEN
			DEBUG("clientmessage(NetWMFullscreen) %s client:\"%s\"\n",
				(cme->data.l[0] == 1 ? "_NET_WM_STATE_ADD" : (cme->data.l[0] == 2 ? "_NET_WM_STATE_TOGGLE" : "OFF?")), c->name
			);
			setfullscreen(c, (cme->data.l[0] == 1 /* _NET_WM_STATE_ADD    */
				|| (cme->data.l[0] == 2 /* _NET_WM_STATE_TOGGLE */ && !c->isfullscreen)));
			return;
		}
		#if PATCH_FLAG_STICKY
		else if (cme->data.l[1] == netatom[NetWMSticky]
			|| cme->data.l[2] == netatom[NetWMSticky]) {
			DEBUG("clientmessage(NetWMSticky) %s client:\"%s\"\n",
				(cme->data.l[0] == 1 ? "_NET_WM_STATE_ADD" : (cme->data.l[0] == 2 ? "_NET_WM_STATE_TOGGLE" : "OFF?")), c->name
			);
			setsticky(c, (cme->data.l[0] == 1 || (cme->data.l[0] == 2 && !c->issticky)));
			return;
		}
		#endif // PATCH_FLAG_STICKY
		#if PATCH_FLAG_ALWAYSONTOP
		else if (cme->data.l[1] == netatom[NetWMStaysOnTop]
			|| cme->data.l[2] == netatom[NetWMStaysOnTop]) {
			DEBUG("clientmessage(NetWMStaysOnTop) %s client:\"%s\"\n",
				(cme->data.l[0] == 1 ? "_NET_WM_STATE_ADD" : (cme->data.l[0] == 2 ? "_NET_WM_STATE_TOGGLE" : "OFF?")), c->name
			);
			setalwaysontop(c, (cme->data.l[0] == 1 || (cme->data.l[0] == 2 && !c->alwaysontop)));
			return;
		}
		#endif // PATCH_FLAG_ALWAYSONTOP
		#if PATCH_FLAG_HIDDEN
		else if (cme->data.l[1] == netatom[NetWMHidden] ||
				cme->data.l[2] == netatom[NetWMHidden]) {
			DEBUG("clientmessage(NetWMHidden) %s client:\"%s\"\n",
				(cme->data.l[0] == 1 ? "_NET_WM_STATE_ADD" : (cme->data.l[0] == 2 ? "_NET_WM_STATE_TOGGLE" : "OFF?")), c->name
			);
			sethidden(c, (cme->data.l[0] == 1 || (cme->data.l[0] == 2 && !c->ishidden)), True);
			return;
		}
		#endif // PATCH_FLAG_HIDDEN
		#if PATCH_LOG_DIAGNOSTICS
		else if (
			cme->data.l[1] == netatom[NetWMAbove] ||
			cme->data.l[2] == netatom[NetWMAbove]
		) {
			DEBUG("clientmessage(NetWMAbove) %s client:\"%s\"\n",
				(cme->data.l[0] == 1 ? "_NET_WM_STATE_ADD" : (cme->data.l[0] == 2 ? "_NET_WM_STATE_TOGGLE" : "OFF?")), c->name
			);
		}
		else if (
			cme->data.l[1] == netatom[NetWMBelow] ||
			cme->data.l[2] == netatom[NetWMBelow]
		) {
			DEBUG("clientmessage(NetWMBelow) %s client:\"%s\"\n",
				(cme->data.l[0] == 1 ? "_NET_WM_STATE_ADD" : (cme->data.l[0] == 2 ? "_NET_WM_STATE_TOGGLE" : "OFF?")), c->name
			);
		}
		else if (
			cme->data.l[1] == netatom[NetWMMaximizedH] ||
			cme->data.l[2] == netatom[NetWMMaximizedH]
		) {
			DEBUG("clientmessage(NetWMMaximizedH) %s client:\"%s\"\n",
				(cme->data.l[0] == 1 ? "_NET_WM_STATE_ADD" : (cme->data.l[0] == 2 ? "_NET_WM_STATE_TOGGLE" : "OFF?")), c->name
			);
		}
		else if (
			cme->data.l[1] == netatom[NetWMMaximizedV] ||
			cme->data.l[2] == netatom[NetWMMaximizedV]
		) {
			DEBUG("clientmessage(NetWMMaximizedV) %s client:\"%s\"\n",
				(cme->data.l[0] == 1 ? "_NET_WM_STATE_ADD" : (cme->data.l[0] == 2 ? "_NET_WM_STATE_TOGGLE" : "OFF?")), c->name
			);
		}
		else if (
			cme->data.l[1] == netatom[NetWMShaded] ||
			cme->data.l[2] == netatom[NetWMShaded]
		) {
			DEBUG("clientmessage(NetWMShaded) %s client:\"%s\"\n",
				(cme->data.l[0] == 1 ? "_NET_WM_STATE_ADD" : (cme->data.l[0] == 2 ? "_NET_WM_STATE_TOGGLE" : "OFF?")), c->name
			);
		}
		else if (
			cme->data.l[1] == netatom[NetWMSkipPager] ||
			cme->data.l[2] == netatom[NetWMSkipPager]
		) {
			DEBUG("clientmessage(NetWMSkipPager) %s client:\"%s\"\n",
				(cme->data.l[0] == 1 ? "_NET_WM_STATE_ADD" : (cme->data.l[0] == 2 ? "_NET_WM_STATE_TOGGLE" : "OFF?")), c->name
			);
		}
		else if (
			cme->data.l[1] == netatom[NetWMSkipTaskbar] ||
			cme->data.l[2] == netatom[NetWMSkipTaskbar]
		) {
			DEBUG("clientmessage(NetWMSkipTaskbar) %s client:\"%s\"\n",
				(cme->data.l[0] == 1 ? "_NET_WM_STATE_ADD" : (cme->data.l[0] == 2 ? "_NET_WM_STATE_TOGGLE" : "OFF?")), c->name
			);
		}
		else {
			DEBUG(
				"clientmessage(1: 0x%lx, 2: 0x%lx) x:%i y:%i w:%i h:%i c:%s\n",
				cme->data.l[1], cme->data.l[2], c->x, c->y, c->w, c->h, c->name
			);
		}
		#endif // PATCH_LOG_DIAGNOSTICS
	} else if (cme->message_type == netatom[NetActiveWindow]) {

		DEBUG("clientmessage(NetActiveWindow) client:\"%s\"\n", c->name);

		if (c != selmon->sel) {
			#if PATCH_EXTERNAL_WINDOW_ACTIVATION || PATCH_FLAG_GAME || PATCH_SHOW_DESKTOP
			if (
				#if PATCH_HANDLE_SIGNALS
				closing == 1 ||
				#endif // PATCH_HANDLE_SIGNALS
				#if PATCH_EXTERNAL_WINDOW_ACTIVATION
				enable_switching ||
				#endif // PATCH_EXTERNAL_WINDOW_ACTIVATION
				#if PATCH_FLAG_GAME
				(c->isgame && selmon->sel && selmon->sel->isgame) ||
				#endif // PATCH_FLAG_GAME
				#if PATCH_SHOW_DESKTOP
				(showdesktop && c->mon->showdesktop) ||
				#endif // PATCH_SHOW_DESKTOP
				#if PATCH_FLAG_CAN_LOSE_FOCUS
				(selmon->sel && selmon->sel->canlosefocus) ||
				#endif // PATCH_FLAG_CAN_LOSE_FOCUS
				0
			) {
				if (!ISVISIBLE(c))
					viewmontag(c->mon, c->tags, 1);
				else
					selmon = c->mon;
				focus(c, 1);

				#if PATCH_MOUSE_POINTER_WARPING
				#if PATCH_MOUSE_POINTER_WARPING_SMOOTH
				warptoclient(c, 1, 0);
				#else // NO PATCH_MOUSE_POINTER_WARPING_SMOOTH
				warptoclient(c, 0);
				#endif // PATCH_MOUSE_POINTER_WARPING_SMOOTH
				#endif // PATCH_MOUSE_POINTER_WARPING
			}
			else
			#endif // PATCH_EXTERNAL_WINDOW_ACTIVATION || PATCH_FLAG_GAME || PATCH_SHOW_DESKTOP
			if (!c->isurgent && urgency
				&& (c->mon != selmon || !ISVISIBLE(c) || c->mon->lt[c->mon->sellt]->arrange == monocle)
				#if PATCH_SHOW_DESKTOP
				&& !c->isdesktop && !c->ondesktop
				#endif // PATCH_SHOW_DESKTOP
			) {
				seturgent(c, 1);
				if (!MINIMIZED(c))
					drawbar(c->mon, 0);
			}
		}

	}
}

void
configure(Client *c)
{
	XConfigureEvent ce;

	#if PATCH_SHOW_DESKTOP
	if (showdesktop && c->isdesktop && c->mon) {
		c->x = c->mon->wx;
		c->y = c->mon->wy;
		c->w = c->mon->ww;
		c->h = c->mon->wh;
		c->bw = 0;
		c->isfloating = 1;
		c->isfloating_override = 1;
	}
	#endif // PATCH_SHOW_DESKTOP

	ce.type = ConfigureNotify;
	ce.display = dpy;
	ce.event = c->win;
	ce.window = c->win;
	ce.x = c->x;
	ce.y = c->y;
	ce.width = c->w;
	ce.height = c->h;
	ce.border_width = c->bw;
	ce.above = None;
	ce.override_redirect = False;
	XSendEvent(dpy, c->win, False, StructureNotifyMask, (XEvent *)&ce);
}

void
configurenotify(XEvent *e)
{
	Monitor *m;
	Client *c;
	XConfigureEvent *ev = &e->xconfigure;
	int dirty;

	/* TODO: updategeom handling sucks, needs to be simplified */
	if (ev->window == root) {
		dirty = (sw != ev->width || sh != ev->height);
		sw = ev->width;
		sh = ev->height;
		if (updategeom() || dirty) {
			drw_resize(drw, sw, sh);
			updatebars();
			for (m = mons; m; m = m->next) {
				for (c = m->clients; c; c = c->next)
					if (c->isfullscreen
						#if PATCH_FLAG_FAKEFULLSCREEN
						&& c->fakefullscreen != 1
						#endif // PATCH_FLAG_FAKEFULLSCREEN
						)
						resizeclient(c, m->mx, m->my, m->mw, m->mh, 0);
				resizebarwin(m);
			}
			focus(NULL, 0);
			arrange(NULL);
		}
	}
}

void
configurerequest(XEvent *e)
{
	Client *c;
	#if PATCH_CROP_WINDOWS
	Client *cc = NULL;
	#endif // PATCH_CROP_WINDOWS
	Monitor *m;
	XConfigureRequestEvent *ev = &e->xconfigurerequest;
	XWindowChanges wc;

	if (((c = wintoclient(ev->window))
		#if PATCH_CROP_WINDOWS
		|| (c = cc = cropwintoclient(ev->window))
		#endif // PATCH_CROP_WINDOWS
		)
		&& !c->dormant
		#if PATCH_FLAG_IGNORED
		&& !c->isignored
		#endif // PATCH_FLAG_IGNORED
	) {
		if (ev->value_mask & CWBorderWidth
			#if PATCH_SHOW_DESKTOP
			&& !c->isdesktop
			#endif // PATCH_SHOW_DESKTOP
			)
			c->bw = ev->border_width;
		if (
			(c->isfloating || !selmon->lt[selmon->sellt]->arrange) && (
				!c->isfullscreen
				#if PATCH_FLAG_FAKEFULLSCREEN
				|| c->fakefullscreen == 1
				#endif // PATCH_FLAG_FAKEFULLSCREEN
			)
			#if PATCH_SHOW_DESKTOP
			&& !c->isdesktop
			#endif // PATCH_SHOW_DESKTOP
		) {

			m = c->mon;
			#if PATCH_CROP_WINDOWS
			if (c->crop)
				c = c->crop;
			#endif // PATCH_CROP_WINDOWS

			if (1
				#if PATCH_FLAG_NEVER_RESIZE
				&& !c->neverresize
				#endif // PATCH_FLAG_NEVER_RESIZE
			) {
				if (ev->value_mask & CWWidth) {
					c->oldw = c->w;
					c->w = ev->width;
				}
				if (ev->value_mask & CWHeight) {
					c->oldh = c->h;
					c->h = ev->height;
				}
			}
			#if PATCH_FLAG_FLOAT_ALIGNMENT
			alignfloat(c, c->floatalignx, c->floataligny);
			#endif // PATCH_FLAG_FLOAT_ALIGNMENT
			if (1
				#if PATCH_FLAG_PANEL
				&& !c->ispanel
				#endif // PATCH_FLAG_PANEL
				#if PATCH_FLAG_GAME
				&& !c->isgame
				#endif // PATCH_FLAG_GAME
				#if PATCH_SHOW_DESKTOP
				&& !c->isdesktop
				#endif // PATCH_SHOW_DESKTOP
			) {
				if (1
					#if PATCH_FLAG_NEVER_MOVE
					&& !c->nevermove
					#endif // PATCH_FLAG_NEVER_MOVE
				) {
					// why are the following 2 lines here?
					//XClassHint ch = { NULL, NULL };
					//XGetClassHint(dpy, c->win, &ch);

					if (ev->value_mask & CWX) {
						c->oldx = c->x;
						//c->x = m->mx + ev->x;
						c->x = ev->x;
					}
					if (ev->value_mask & CWY) {
						c->oldy = c->y;
						//c->y = m->my + ev->y;
						c->y = ev->y;
					}
				}
				#if PATCH_FLAG_PANEL
				if (!c->ultparent->ispanel)
				#endif // PATCH_FLAG_PANEL
				{
					if ((c->x + c->w) > m->mx + m->mw && c->isfloating)
						c->x = m->mx + (m->mw / 2 - WIDTH(c) / 2); /* centre in x direction */
					if ((c->y + c->h) > m->my + m->mh && c->isfloating)
						c->y = m->my + (m->mh / 2 - HEIGHT(c) / 2); /* centre in y direction */
				}
			}
			if (ISVISIBLE(c))
				resizeclient(c, c->x, c->y, c->w, c->h, 0);
			#if PATCH_CROP_WINDOWS
			if (cc)
				cropresize(cc);
			#endif // PATCH_CROP_WINDOWS
			#if PATCH_FLAG_PANEL
			if (c->ispanel)
				drawbar(m, 0);
			#endif // PATCH_FLAG_PANEL
		}
		if (
			(ev->value_mask & CWBorderWidth) ||
			((ev->value_mask & (CWX|CWY)) && !(ev->value_mask & (CWWidth|CWHeight)))
			)
			configure(c);
	} else {
		wc.x = ev->x;
		wc.y = ev->y;
		wc.width = ev->width;
		wc.height = ev->height;
		wc.border_width = ev->border_width;
		wc.sibling = ev->above;
		wc.stack_mode = ev->detail;
		XConfigureWindow(dpy, ev->window, ev->value_mask, &wc);
	}
	XSync(dpy, False);
}

#if PATCH_IPC
int
connect_to_socket()
{
	if (sock_fd != -1)
		return sock_fd;

	struct sockaddr_un addr;

	int sock = socket(AF_UNIX, SOCK_STREAM, 0);

	// Initialize struct to 0
	memset(&addr, 0, sizeof(struct sockaddr_un));

	addr.sun_family = AF_UNIX;
	if (ipcsockpath)
		free(ipcsockpath);
	ipcsockpath = expandenv(socketpath);
	if (!ipcsockpath || (strchr(socketpath, '$') && strlen(ipcsockpath) == strlen(socketpath))) {
		logdatetime(stderr);
		if (ipcsockpath) {
			fprintf(stderr, "dwm: Failed to expand socket path: \"%s\"; using fallback \"%s\".\n", socketpath, socketpath_fallback);
			free(ipcsockpath);
		}
		else
			fprintf(stderr, "dwm: Unable to evaluate socket path: \"%s\"; using fallback \"%s\".\n", socketpath, socketpath_fallback);
		ipcsockpath = strdup(socketpath_fallback);
	}
	strcpy(addr.sun_path, ipcsockpath);
	connect(sock, (const struct sockaddr *)&addr, sizeof(struct sockaddr_un));
	sock_fd = sock;
	return sock_fd;
}
#endif // PATCH_IPC

#if PATCH_FLAG_GAME
void
createbarrier(Client *c)
{
	unsigned int x, y, w, h;
	if (!c) return;
	#if PATCH_CONSTRAIN_MOUSE
	if (constrained)
		return;
	#endif // PATCH_CONSTRAIN_MOUSE
	if (xfixes_support) {
		destroybarrier();
		if (c->isfullscreen
			#if PATCH_FLAG_FAKEFULLSCREEN
			&& c->fakefullscreen != 1
			#endif // PATCH_FLAG_FAKEFULLSCREEN
		) {
			x = c->mon->mx;
			y = c->mon->my;
			w = c->mon->mw;
			h = c->mon->mh;
		}
		else {
			x = c->x + c->bw;
			y = c->y + c->bw;
			w = c->w - 2*c->bw;
			h = c->h - 2*c->bw;
		}
		barrierLeft = XFixesCreatePointerBarrier(dpy, root, x, y, x, (y + h), BarrierPositiveX, 0, NULL);
		barrierRight = XFixesCreatePointerBarrier(dpy, root, (x + w), y, (x + w), (y + h), BarrierNegativeX, 0, NULL);
		barrierTop = XFixesCreatePointerBarrier(dpy, root, x, y, (x + w), y, BarrierPositiveY, 0, NULL);
		barrierBottom = XFixesCreatePointerBarrier(dpy, root, x, (y + h), (x + w), (y + h), BarrierNegativeY, 0, NULL);
	}
}
#endif // PATCH_FLAG_GAME
#if PATCH_CONSTRAIN_MOUSE
void
createbarriermon(Monitor *m)
{
	if (constrained)
		return;
	Monitor *mm = (m ? m : selmon);
	if (!mm) return;
	if (xfixes_support) {
		destroybarrier();
		constrained = mm;
		barrierLeft = XFixesCreatePointerBarrier(dpy, root, mm->mx, mm->my, mm->mx, (mm->my + mm->mh), BarrierPositiveX, 0, NULL);
		barrierRight = XFixesCreatePointerBarrier(dpy, root, (mm->mx + mm->mw), mm->my, (mm->mx + mm->mw), (mm->my + mm->mh), BarrierNegativeX, 0, NULL);
		barrierTop = XFixesCreatePointerBarrier(dpy, root, mm->mx, mm->my, (mm->mx + mm->mw), mm->my, BarrierPositiveY, 0, NULL);
		barrierBottom = XFixesCreatePointerBarrier(dpy, root, mm->mx, (mm->my + mm->mh), (mm->mx + mm->mw), (mm->my + mm->mh), BarrierNegativeY, 0, NULL);
	}
}
#endif // PATCH_CONSTRAIN_MOUSE

Monitor *
createmon(
	#if PATCH_VIRTUAL_MONITORS
	int index
	#else // NO PATCH_VIRTUAL_MONITORS
	void
	#endif // PATCH_VIRTUAL_MONITORS
)
{
	#if PATCH_VIRTUAL_MONITORS
	Monitor *m;
	unsigned int i;
	#else // NO PATCH_VIRTUAL_MONITORS
	Monitor *m, *mon;
	unsigned int mi, i;
	#endif // PATCH_VIRTUAL_MONITORS

	m = ecalloc(1, sizeof(Monitor));
	#if PATCH_VIRTUAL_MONITORS
	m->num = index;
	m->enablesplit = 0;
	m->split = 0;
	#endif // PATCH_VIRTUAL_MONITORS
	m->tagset[0] = m->tagset[1] = 1;

	for (i = 0; i < LENGTH(tags); i++)
		m->focusontag[i] = NULL;

	// set defaults;

	#if PATCH_FONT_GROUPS
	m->bh = 0;
	m->minbh = 0;
	#endif // PATCH_FONT_GROUPS
	for (i = 0; i < LENGTH(m->bar); ++i) {
		m->bar[i].type = i;
		m->bar[i].x = -1;
		m->barlayout[i] = NoElement;
	}
	for (i = 0; i < LENGTH(barlayout); ++i)
		m->barlayout[i] = barlayout[i];

	#if PATCH_SHOW_DESKTOP
	m->showdesktop = 0;
	#endif // PATCH_SHOW_DESKTOP
	m->title_align = title_align;
	#if PATCH_CLIENT_OPACITY
	m->activeopacity = activeopacity;
	m->inactiveopacity = inactiveopacity;
	#endif // PATCH_CLIENT_OPACITY
	#if PATCH_CLASS_STACKING
	m->class_stacking = class_stacking;
	#endif // PATCH_CLASS_STACKING
	#if PATCH_MOUSE_POINTER_HIDING
	m->cursorautohide = cursorautohide;
	m->cursorhideonkeys = cursorhideonkeys;
	#endif // PATCH_MOUSE_POINTER_HIDING
	m->mfact_def = m->mfact = mfact;
	m->nmaster = nmaster;
	m->showbar = showbar;
	m->barvisible = showbar;
	m->topbar = topbar;
	#if PATCH_CLIENT_INDICATORS
	m->client_ind_top = client_ind_top;
	#endif // PATCH_CLIENT_INDICATORS
	#if PATCH_VANITY_GAPS
	m->gappih = gappih;
	m->gappiv = gappiv;
	m->gappoh = gappoh;
	m->gappov = gappov;
	#endif // PATCH_VANITY_GAPS
	#if PATCH_ALTTAB
	m->nTabs = 0;
	m->tabBW = tabBW;
	m->tabTextAlign = tabTextAlign;
	m->tabMaxW = tabMaxW;
	m->tabMaxH = tabMaxH;
	m->tabPosX = tabPosX;
	m->tabPosY = tabPosY;
	#endif // PATCH_ALTTAB
	m->defaulttag = 0;
	m->isdefault = 0;
	#if PATCH_HIDE_VACANT_TAGS
	m->hidevacant = hidevacant;
	#endif // PATCH_HIDE_VACANT_TAGS
	#if PATCH_MIRROR_LAYOUT
	m->mirror = mirror_layout;
	#endif // PATCH_MIRROR_LAYOUT
	m->showstatus = 1;
	#if PATCH_SHOW_MASTER_CLIENT_ON_TAG
	m->reversemaster = reverselbl;
	m->etagf = etagf;
	m->ptagf = ptagf;
	m->showmaster = showmaster;
	#endif // PATCH_SHOW_MASTER_CLIENT_ON_TAG
	#if PATCH_CUSTOM_TAG_ICONS
	m->showcustomtagicons = showcustomtagicons;
	for (i = 0; i < LENGTH(tags); i++) {
		m->tagiconpaths[i] = tagiconpaths[i];
		if (m->tagicons[i])
			XRenderFreePicture(dpy, m->tagicons[i]);
		m->tagicons[i] = None;
	}
	#endif // PATCH_CUSTOM_TAG_ICONS
	#if PATCH_WINDOW_ICONS
	#if PATCH_WINDOW_ICONS_ON_TAGS
	m->showiconsontags = showiconsontags;
	#endif // PATCH_WINDOW_ICONS_ON_TAGS
	#endif // PATCH_WINDOW_ICONS
	#if PATCH_LOG_DIAGNOSTICS
	m->logallrules = 0;
	#endif // PATCH_LOG_DIAGNOSTICS
	#if PATCH_VANITY_GAPS
	m->enablegaps = defgaps;
	#endif // PATCH_VANITY_GAPS
	#if PATCH_ALT_TAGS
	m->alttagsquiet = 0;
	#endif // PATCH_ALT_TAGS
	#if PATCH_SYSTRAY
	m->stw = 0;
	#endif // PATCH_SYSTRAY
	#if PATCH_ALT_TAGS
	for (i = 0; i < LENGTH(tags); i++)
		m->tags[i] = tags[i];
	#endif // PATCH_ALT_TAGS
	#if PATCH_SWITCH_TAG_ON_EMPTY
	m->switchonempty = 0;
	#endif // PATCH_SWITCH_TAG_ON_EMPTY

	m->lt[0] = &layouts[0];
	m->lt[1] = &layouts[1 % LENGTH(layouts)];

	#if PATCH_PERTAG
	m->pertag = ecalloc(1, sizeof(Pertag));
	m->pertag->curtag = m->pertag->prevtag = 1;
	for (i = 0; i <= LENGTH(tags); i++) {
		#if PATCH_SWITCH_TAG_ON_EMPTY
		m->pertag->switchonempty[i] = m->switchonempty;
		#endif // PATCH_SWITCH_TAG_ON_EMPTY
		#if PATCH_MOUSE_POINTER_HIDING
		m->pertag->cursorautohide[i] = m->cursorautohide;
		m->pertag->cursorhideonkeys[i] = m->cursorhideonkeys;
		#endif // PATCH_MOUSE_POINTER_HIDING
		m->pertag->nmasters[i] = m->nmaster;
		m->pertag->mfacts_def[i] = m->pertag->mfacts[i] = m->mfact;
		m->pertag->ltidxs[i][0] = m->lt[0];
		m->pertag->ltidxs[i][1] = m->lt[1];
		m->pertag->sellts[i] = m->sellt;
		m->pertag->showbars[i] = m->showbar;
		#if PATCH_VANITY_GAPS
		m->pertag->enablegaps[i] = m->enablegaps;
		#endif // PATCH_VANITY_GAPS
		#if PATCH_ALT_TAGS
		m->pertag->alttagsquiet[i] = m->alttagsquiet;
		#endif // PATCH_ALT_TAGS
		#if PATCH_CLASS_STACKING
		m->pertag->class_stacking[i] = m->class_stacking;
		#endif // PATCH_CLASS_STACKING
	}
	#endif // PATCH_PERTAG
	#if PATCH_VIRTUAL_MONITORS
	parsemon(m, index, 1);
	#else // NO PATCH_VIRTUAL_MONITORS
	// identify monitor index;
	for (mi = 0, mon = mons; mon; mon = mon->next, mi++);
	parsemon(m, mi, 1);
	#endif // PATCH_VIRTUAL_MONITORS

	return m;
}

#if PATCH_VIRTUAL_MONITORS
PMonitor *
createpmon(void)
{
	PMonitor *pm;
	pm = ecalloc(1, sizeof(PMonitor));
	pm->mon1 = NULL;
	pm->mon2 = NULL;
	pm->mx = 0;
	pm->my = 0;
	pm->mw = 0;
	pm->mh = 0;
	pm->disappeared = 0;
	return pm;
}
#endif // PATCH_VIRTUAL_MONITORS

#if PATCH_CROP_WINDOWS
Client *
cropwintoclient(Window w)
{
	Client *c;
	Monitor *m;

	for (m = mons; m; m = m->next)
		for (c = m->clients; c; c = c->next)
			if (c->crop && c->crop->win == w)
				return c;
	return NULL;
}
void
cropwindow(Client *c)
{
	XEvent ev;
	XSetWindowAttributes wa = { .event_mask = SubstructureRedirectMask };

	if (!c->crop) {
		c->crop = ecalloc(1, sizeof(Client));
		memcpy(c->crop, c, sizeof(Client));
		c->crop->crop = NULL;
		c->crop->x = c->crop->y = c->crop->bw = 0;
		c->basew = c->baseh = c->mina = 0;
		//c->maxa = c->maxw = c->maxh = c->incw = c->inch = 0;
		c->minw = c->minh = 1;
		if (!c->isfloating)
			togglefloatingex(c);
		c->win = XCreateWindow(dpy, root,
			c->x, c->y, c->w, c->h, c->bw, 0, 0, 0, CWEventMask, &wa
		);
		XReparentWindow(dpy, c->crop->win, c->win, 0, 0);
		XMapWindow(dpy, c->win);
		focus(c, 0);
		XCheckTypedWindowEvent(dpy, c->crop->win, UnmapNotify, &ev);
		if (XCheckTypedWindowEvent(dpy, root, UnmapNotify, &ev) &&
			ev.xunmap.window != c->crop->win
			)
			XPutBackEvent(dpy, &ev);
	}
}
void
cropdelete(Client *c)
{
	Client *crop;
	XEvent ev;

	c->crop->x += c->x;
	c->crop->y += c->y;
	c->crop->bw = c->bw;
	c->crop->next = c->next;
	c->crop->snext = c->snext;
	c->crop->tags = c->tags;
	c->crop->mon = c->mon;
	XReparentWindow(dpy, c->crop->win, root, c->crop->x, c->crop->y);
	XDestroyWindow(dpy, c->win);
	crop = c->crop;
	memcpy(c, c->crop, sizeof(Client));
	c->crop = NULL;
	free(crop);
	resize(c, c->x, c->y, c->w, c->h, 0);
	focus(c, 0);
	XCheckTypedWindowEvent(dpy, c->win, UnmapNotify, &ev);
}
void
cropmove(Client *c)
{
	if (c->crop->x > 0 || c->crop->w < c->w)
		c->crop->x = 0;
	if (c->crop->x + c->crop->w < c->w)
		c->crop->x = c->w - c->crop->w;
	if (c->crop->y > 0 || c->crop->h < c->h)
		c->crop->y = 0;
	if (c->crop->y + c->crop->h < c->h)
		c->crop->y = c->h - c->crop->h;
	resizeclient(c->crop,
		BETWEEN(c->crop->x, -(c->crop->w), 0) ? c->crop->x : 0,
		BETWEEN(c->crop->y, -(c->crop->h), 0) ? c->crop->y : 0,
		c->crop->w, c->crop->h, False
	);
}
void
cropresize(Client *c)
{
	cropmove(c);
	resize(c, c->x, c->y,
		MIN(c->w, c->crop->x + c->crop->w),
		MIN(c->h, c->crop->y + c->crop->h), 0
	);
}
#endif // PATCH_CROP_WINDOWS

void
cyclelayoutmouse(const Arg *arg)
{
	#if PATCH_FOCUS_FOLLOWS_MOUSE
	checkmouseovermonitor(selmon);
	#endif // PATCH_FOCUS_FOLLOWS_MOUSE
	Layout *l;
	for(l = (Layout *)layouts; l != selmon->lt[selmon->sellt]; l++);
	if(arg->i > 0) {
		if(l->symbol && (l + 1)->symbol)
			setlayoutreplace(&((Arg) { .v = (l + 1) }));
		else
			setlayoutreplace(&((Arg) { .v = layouts }));
	} else {
		if(l != layouts && (l - 1)->symbol)
			setlayoutreplace(&((Arg) { .v = (l - 1) }));
		else
			setlayoutreplace(&((Arg) { .v = &layouts[LENGTH(layouts) - 2] }));
	}
}

void
cyclelayout(const Arg *arg)
{
	cyclelayoutmouse(arg);
	#if PATCH_MOUSE_POINTER_WARPING
	if (selmon->sel)
		#if PATCH_MOUSE_POINTER_WARPING_SMOOTH
		warptoclient(selmon->sel, 1, 0);
		#else // NO PATCH_MOUSE_POINTER_WARPING_SMOOTH
		warptoclient(selmon->sel, 0);
		#endif // PATCH_MOUSE_POINTER_WARPING_SMOOTH
	#endif // PATCH_MOUSE_POINTER_WARPING
}

#if PATCH_SHOW_DESKTOP
int
desktopvalid(Client *c)
{
	return desktopvalidex(c, c->mon->tagset[c->mon->seltags], c->mon->showdesktop);
}

int
desktopvalidex(Client *c, unsigned int tagset, int show_desktop)
{
	// client visible normally;
	int ret = ISVISIBLEONTAG(c, tagset);

	// showdesktop disabled;
	if (!showdesktop)
		return (c->isdesktop ? 0 : ret);

	// showdesktop enabled, desktop not visible;
	if (!show_desktop)
		return (ret && !c->ondesktop && !c->isdesktop);
	// showdesktop enabled, desktop visible AND normal clients visible;
	else if (show_desktop == -1)
		return (ret && !c->ondesktop && !c->isdesktop) || c->ondesktop || c->isdesktop;

	#if PATCH_SHOW_DESKTOP_WITH_FLOATING
	// showdesktop enabled, desktop visible, floating normal clients visible;
	if (showdesktop_floating) {
		return
			(c->isdesktop || c->ondesktop || (ret && c->isfloating
				#if PATCH_MODAL_SUPPORT
				&& (!c->ismodal || c->toplevel || !c->parent || c->parent->isfloating)
				#endif // PATCH_MODAL_SUPPORT
			));
	}
	else
	#endif // PATCH_SHOW_DESKTOP_WITH_FLOATING

	// showdesktop enabled, desktop visible;
	return
		(
			c->ondesktop || c->isdesktop
			#if PATCH_FLAG_PANEL
			|| (ret && c->ispanel)
			#endif // PATCH_FLAG_PANEL
		);
}
#endif // PATCH_SHOW_DESKTOP

#if PATCH_FLAG_GAME || PATCH_CONSTRAIN_MOUSE
void
destroybarrier(void)
{
	#if PATCH_CONSTRAIN_MOUSE
	if (constrained)
		return;
	#endif // PATCH_CONSTRAIN_MOUSE
	if (xfixes_support) {
		if (barrierLeft) XFixesDestroyPointerBarrier(dpy, barrierLeft);
		if (barrierRight) XFixesDestroyPointerBarrier(dpy, barrierRight);
		if (barrierTop) XFixesDestroyPointerBarrier(dpy, barrierTop);
		if (barrierBottom) XFixesDestroyPointerBarrier(dpy, barrierBottom);
		barrierLeft = 0;
		barrierRight = 0;
		barrierTop = 0;
		barrierBottom = 0;
	}
}
#endif // PATCH_FLAG_GAME || PATCH_CONSTRAIN_MOUSE
#if PATCH_CONSTRAIN_MOUSE
void
destroybarriermon(void)
{
	if (!constrained)
		return;
	constrained = NULL;
	destroybarrier();
}
#endif // PATCH_CONSTRAIN_MOUSE

void
destroynotify(XEvent *e)
{
	Client *c;
	XDestroyWindowEvent *ev = &e->xdestroywindow;

	#if PATCH_SHOW_DESKTOP
	#if PATCH_SHOW_DESKTOP_UNMANAGED
	if (showdesktop && showdesktop_unmanaged && desktopwin == ev->window) {
		desktopwin = None;
		desktoppid = 0;
		return;
	}
	else
	#endif // PATCH_SHOW_DESKTOP_UNMANAGED
	#endif // PATCH_SHOW_DESKTOP
	if ((c = wintoclient(ev->window))
		#if PATCH_CROP_WINDOWS
		|| (c = cropwintoclient(ev->window))
		#endif // PATCH_CROP_WINDOWS
		)
		unmanage(c, 1, 0);
	#if PATCH_SYSTRAY
	else if ((c = wintosystrayicon(ev->window))) {
		removesystrayicon(c);
		updatesystray(1);
		resizebarwin(selmon);
	}
	#endif // PATCH_SYSTRAY
	#if PATCH_TERMINAL_SWALLOWING
	else if ((c = swallowingclient(ev->window)))
		unmanage(c->swallowing, 1, 0);
	#endif // PATCH_TERMINAL_SWALLOWING
	#if PATCH_SCAN_OVERRIDE_REDIRECTS
	else if ((c = wintoorclient(ev->window))) {
		DEBUG("destroying override_redirect: 0x%lx\n", ev->window);
		detachor(c);
		free(c);
	}
	#endif // PATCH_SCAN_OVERRIDE_REDIRECTS
}

void
detach(Client *c)
{
	Client **tc;

	for (tc = &c->mon->clients; *tc && *tc != c; tc = &(*tc)->next);
	*tc = c->next;
}

#if PATCH_SCAN_OVERRIDE_REDIRECTS
void
detachor(Client *c)
{
	Client **tc;

	for (tc = &orlist; *tc && *tc != c; tc = &(*tc)->next);
	*tc = c->next;
}
#endif // PATCH_SCAN_OVERRIDE_REDIRECTS

void
detachstackex(Client *c)
{
	Client **tc;

	for (tc = &c->mon->stack; *tc && *tc != c; tc = &(*tc)->snext);
	if (c->snext)
		c->snext->sprev = (*tc)->sprev;
	*tc = c->snext;
}

void
detachstack(Client *c)
{
	detachstackex(c);

	for (int i = 0; i < LENGTH(tags); i++)
		if (c->mon->focusontag[i] == c)
			c->mon->focusontag[i] = NULL;

	Client *t;
	if (c == c->mon->sel) {
		if (c->mon == selmon && ISVISIBLE(c)
			#if PATCH_FLAG_HIDDEN
			&& !c->ishidden
			#endif // PATCH_FLAG_HIDDEN
			#if PATCH_FLAG_IGNORED
			&& !c->isignored
			#endif // PATCH_FLAG_IGNORED
			#if PATCH_FLAG_PANEL
			&& !c->ispanel
			#endif // PATCH_FLAG_PANEL
			#if PATCH_SHOW_DESKTOP
			&& !c->isdesktop && !c->ondesktop
			#endif // PATCH_SHOW_DESKTOP
			)
			XSetWindowBorder(dpy, c->win, scheme[SchemeNorm][ColBorder].pixel);

		for (t = c->mon->stack; t && (!ISVISIBLE(t)
			#if PATCH_FLAG_HIDDEN
			|| t->ishidden
			#endif // PATCH_FLAG_HIDDEN
			#if PATCH_FLAG_PANEL
			|| t->ispanel
			#endif // PATCH_FLAG_PANEL
			#if PATCH_FLAG_IGNORED
			|| t->isignored
			#endif // PATCH_FLAG_IGNORED
		); t = t->snext);
		c->mon->sel = t;
	}
}

Monitor *
dirtomon(int dir)
{
	Monitor *m = NULL;

	#if PATCH_VIRTUAL_MONITORS
	PMonitor *pm = NULL;
	if (dir > 0) {
		if (selmon->pmon->mon1 == selmon && selmon->pmon->mon2)
			m = selmon->pmon->mon2;
		else if ((pm = selmon->pmon->next))
			m = pm->mon1;
		else
			m = pmons->mon1;
	}
	else {
		if (selmon->pmon->mon2 == selmon && selmon->pmon->mon1)
			m = selmon->pmon->mon1;
		else {
			for (pm = pmons; pm && pm->next && pm->next != selmon->pmon; pm = pm->next);
			if (pm->mon2)
				m = pm->mon2;
			else
				m = pm->mon1;
		}
	}
	#else // NO PATCH_VIRTUAL_MONITORS
	if (dir > 0) {
		if (!(m = selmon->next))
			m = mons;
	} else if (selmon == mons)
		for (m = mons; m->next; m = m->next);
	else
		for (m = mons; m->next != selmon; m = m->next);
	#endif // PATCH_VIRTUAL_MONITORS
	return m;
}

#if PATCH_DRAG_FACTS
void
dragfact(const Arg *arg)
{
	#if PATCH_FOCUS_FOLLOWS_MOUSE
	checkmouseovermonitor(selmon);
	#endif // PATCH_FOCUS_FOLLOWS_MOUSE

	unsigned int n;
	int px, py; // pointer coordinates
	int lock_x = 0, lock_y = 0;
	int dist_x, dist_y;
	#if PATCH_CFACTS
	int nocfact = 0;
	int nomfact = 0;
	#endif // PATCH_CFACTS
	int horizontal = 0; // layout configuration
	int reverse_h = 1 ; // reverse horizontal handling;
	int reverse_v = 1 ; // reverse vertical handling;
	int rotate = 0;		//
	float mfact, mw, mh;
	#if PATCH_CFACTS
	float cfact, cf, ch, cw;
	#endif // PATCH_CFACTS
	Client *c;
	Monitor *m = selmon;
	XEvent ev;
	Time lasttime = 0;

	for (n = 0, c = nexttiled(m->clients); c; c = nexttiled(c->next), n++);
	if (!(c = m->sel) || !n || !m->lt[m->sellt]->arrange)
		return;

	// Layouts that don't observe factors;
	if (m->lt[m->sellt]->arrange == monocle
		#if PATCH_LAYOUT_GRID
		|| m->lt[m->sellt]->arrange == grid
		#endif // PATCH_LAYOUT_GRID
		#if PATCH_LAYOUT_NROWGRID
		|| m->lt[m->sellt]->arrange == nrowgrid
		#endif // PATCH_LAYOUT_NROWGRID
		#if PATCH_LAYOUT_GAPLESSGRID
		|| m->lt[m->sellt]->arrange == gaplessgrid
		#endif // PATCH_LAYOUT_GAPLESSGRID
		)
		return;
	else
	// Add custom handling for horizontal layouts here;
	if (
		#if PATCH_LAYOUT_BSTACK
		m->lt[m->sellt]->arrange == bstack ||
		#endif // PATCH_LAYOUT_BSTACK
		#if PATCH_LAYOUT_BSTACKHORIZ
		m->lt[m->sellt]->arrange == bstackhoriz ||
		#endif // PATCH_LAYOUT_BSTACKHORIZ
		#if PATCH_LAYOUT_HORIZGRID
		m->lt[m->sellt]->arrange == horizgrid ||
		#endif // PATCH_LAYOUT_HORIZGRID
		0)
		horizontal = 1;

	#if PATCH_LAYOUT_BSTACKHORIZ
	if (
		m->lt[m->sellt]->arrange == bstackhoriz && !m->nmaster
		)
		rotate = 1;
	#endif // PATCH_LAYOUT_BSTACKHORIZ

	// no mfact handling in the following layouts;
	if (
		#if PATCH_LAYOUT_HORIZGRID
		m->lt[m->sellt]->arrange == horizgrid ||
		#endif // PATCH_LAYOUT_HORIZGRID
		0)
		#if PATCH_CFACTS
		nomfact = 1;
		#else // NO PATCH_CFACTS
		return;
		#endif // PATCH_CFACTS

	#if !PATCH_CFACTS
	if (!m->nmaster)
		return;
	#endif // !PATCH_CFACTS

	#if PATCH_CFACTS
	// no cfact handling in the following layouts;
	if (
		#if PATCH_LAYOUT_DECK
		m->lt[m->sellt]->arrange == deck ||
		#endif // PATCH_LAYOUT_DECK
		#if PATCH_LAYOUT_DWINDLE
		m->lt[m->sellt]->arrange == dwindle ||
		#endif // PATCH_LAYOUT_DWINDLE
		#if PATCH_LAYOUT_SPIRAL
		m->lt[m->sellt]->arrange == spiral ||
		#endif // PATCH_LAYOUT_SPIRAL
		0)
		nocfact = 1;
	#endif // PATCH_CFACTS

	if (XGrabPointer(dpy, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync,
		None, cursor[CurResize]->cursor, CurrentTime) != GrabSuccess)
		return;

	if (!getrootptr(&px, &py))
		return;

	reverse_h = (px < (c->x + c->w / 2)) ? -1 : 1;
	reverse_v = (py < (c->y + c->h / 2)) ? -1 : 1;

	if (!ismaster(c)) {
		if (horizontal)
			reverse_v *= -1;
		else
			reverse_h *= -1;
	}

	if (
		m->lt[m->sellt]->arrange == tile
		#if PATCH_LAYOUT_CENTREDMASTER
		|| m->lt[m->sellt]->arrange == centredmaster
		#endif // PATCH_LAYOUT_CENTREDMASTER
		#if PATCH_LAYOUT_CENTREDFLOATINGMASTER
		|| m->lt[m->sellt]->arrange == centredfloatingmaster
		#endif // PATCH_LAYOUT_CENTREDFLOATINGMASTER
		)
		reverse_v *= -1;

	#if PATCH_CFACTS
	cf = c->cfact;
	ch = c->h;
	cw = c->w;
	#endif // PATCH_CFACTS
	mw = m->ww * m->mfact;
	mh = m->wh * m->mfact;

	#if PATCH_FOCUS_BORDER || PATCH_FOCUS_PIXEL
	if (focuswin)
		XUnmapWindow(dpy, focuswin);
	#endif // PATCH_FOCUS_BORDER || PATCH_FOCUS_PIXEL

	do {
		XMaskEvent(dpy, MOUSEMASK|ExposureMask|SubstructureRedirectMask, &ev);
		switch (ev.type) {
		case ConfigureRequest:
		case Expose:
		case MapRequest:
			handler[ev.type](&ev);
			break;
		case MotionNotify:
			if ((ev.xmotion.time - lasttime) <= (1000 / 40))
				continue;
			lasttime = ev.xmotion.time;

			dist_x = (lock_x ? 0 : ev.xmotion.x - px);
			dist_y = (lock_y ? 0 : ev.xmotion.y - py);

			if (!lock_x && !lock_y) {
				#if PATCH_CFACTS
				if (
					(nocfact && !horizontal) ||
					(nomfact && horizontal) ||
					(!m->nmaster && horizontal && !rotate)
				) lock_y = 1;
				if (
					(nocfact && horizontal) ||
					(nomfact && !horizontal) ||
					(!m->nmaster && (!horizontal || rotate))
				) lock_x = 1;
				#endif // PATCH_CFACTS

				if (!lock_x && !lock_y) {
					if (abs(dist_x) > abs(dist_y))
						lock_y = 1;
					if (abs(dist_y) > abs(dist_x))
						lock_x = 1;
				}
				if (lock_y) {
					dist_y = 0;
					if (XGrabPointer(dpy, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync,
						None, cursor[CurResizeH]->cursor, CurrentTime) != GrabSuccess)
						return;
				}
				else if (lock_x) {
					dist_x = 0;
					if (XGrabPointer(dpy, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync,
						None, cursor[CurResizeV]->cursor, CurrentTime) != GrabSuccess)
						return;
				}
				else
					continue;
			}

			if ((horizontal && !rotate) || (!horizontal && rotate)) {
				#if PATCH_CFACTS
				cfact = (float) cf * (cw + reverse_h * dist_x) / cw;
				#endif // PATCH_CFACTS
				mfact = (float) (mh + (reverse_v * dist_y)) / m->wh;
			} else {
				#if PATCH_CFACTS
				cfact = (float) cf * (ch - (reverse_v * dist_y)) / ch;
				#endif // PATCH_CFACTS
				mfact = (float) (mw + reverse_h * dist_x) / m->ww;
			}

			#if PATCH_CFACTS
			c->cfact = MAX(0.25, MIN(4.0, cfact));
			#endif // PATCH_CFACTS
			if (m->nmaster)
				m->mfact = MAX(0.05, MIN(0.95, mfact));
			arrangemon(m);
			break;
		}
	} while (ev.type != ButtonRelease);

	XUngrabPointer(dpy, CurrentTime);
	while (XCheckMaskEvent(dpy, EnterWindowMask, &ev));

	#if PATCH_FOCUS_BORDER || PATCH_FOCUS_PIXEL
	if (focuswin) {
		drawfocusborder(0);
		XMapWindow(dpy, focuswin);
	}
	#endif // PATCH_FOCUS_BORDER || PATCH_FOCUS_PIXEL
}
#endif // PATCH_DRAG_FACTS

int
elementafter(Monitor *m, unsigned int el1, unsigned int el2)
{
	int p1 = -1, p2 = -1;
	for (unsigned int i = 0; i < LENGTH(m->barlayout); i++) {
		if (m->barlayout[i] == el1)
			p1 = i;
		else if (m->barlayout[i] == el2)
			p2 = i;
		if (p1 > -1 && p2 > -1)
			break;
	}
	return (p1 > p2);
}

void
drawbar(Monitor *m, int skiptags)
{
	if (running != 1 || (nonstop & 1)
		#if PATCH_TORCH
		|| torchwin
		#endif // PATCH_TORCH
		)
		return;

	#if PATCH_CLIENT_INDICATORS
	int offsety = 0;
	int total[LENGTH(tags)];
	#if PATCH_FLAG_STICKY
	int sticky[LENGTH(tags)];
	#endif // PATCH_FLAG_STICKY
	if (client_ind && client_ind_offset)
		offsety = client_ind_offset * (m->client_ind_top ? 1 : -1);
	#endif // PATCH_CLIENT_INDICATORS
	int x = 0, w;
	unsigned int customwidth = 0;	// extra bar modules (drawn in front of status) total width;
	#if PATCH_SYSTRAY
	m->stw = 0;
	#endif // PATCH_SYSTRAY
	#if PATCH_ALT_TAGS
	int tagw = 0, alttagw = 0;
	#endif // PATCH_ALT_TAGS
	#if PATCH_FONT_GROUPS
	int boxs = 0;
	int boxw = 0;
	#else // NO PATCH_FONT_GROUPS
	int boxs = drw->fonts->h / 9;
	int boxw = drw->fonts->h / 6 + 2;
	#endif // PATCH_FONT_GROUPS
	unsigned int i, occ = 0, urg = 0;
	unsigned int a = 0, s = 0;
	#if PATCH_FLAG_PANEL
	unsigned int px = 0, pw = 0;
	#endif // PATCH_FLAG_PANEL
	Client *c;
	char tagdisp[64];
	#if PATCH_FLAG_HIDDEN
	int hidden[LENGTH(tags)], visible[LENGTH(tags)];
	#endif // PATCH_FLAG_HIDDEN
	#if PATCH_SHOW_MASTER_CLIENT_ON_TAG
	char masterclientbuff[64];
	char *masterclientontag[LENGTH(tags)];
	int masterclientontagmem[LENGTH(tags)];
	#endif // PATCH_SHOW_MASTER_CLIENT_ON_TAG
	#if (PATCH_WINDOW_ICONS && PATCH_WINDOW_ICONS_ON_TAGS) || PATCH_CUSTOM_TAG_ICONS
	Client *mc[LENGTH(tags)];		// (primary) master client per tag;
	#endif // (PATCH_WINDOW_ICONS && PATCH_WINDOW_ICONS_ON_TAGS) || PATCH_CUSTOM_TAG_ICONS

	if ((m->showbar && (
		!m->sel || !m->sel->isfullscreen
		#if PATCH_FLAG_FAKEFULLSCREEN
		|| m->sel->fakefullscreen == 1
		#endif // PATCH_FLAG_FAKEFULLSCREEN
		))
		#if PATCH_ALT_TAGS
	 	|| (m->alttags && !m->alttagsquiet)
		#endif // PATCH_ALT_TAGS
	) {
		m->barvisible = 1;
		m->by = m->topbar ? m->my : m->my + m->wh;
	}
	else {
		m->barvisible = 0;
		#if PATCH_FONT_GROUPS
		m->by = -m->bh;
		#else // NO PATCH_FONT_GROUPS
		m->by = -bh;
		#endif // PATCH_FONT_GROUPS
	}

	#if PATCH_SYSTRAY
	if(showsystray && m == systraytomon(m)) {
		updatesystray(0);
		m->stw = getsystraywidth();
	}
	else m->stw = 0;
	#endif // PATCH_SYSTRAY

	#if PATCH_FLAG_PANEL
	#if PATCH_FLAG_FLOAT_ALIGNMENT
	getpanelpadding(m, &px, &pw);
	if (px) {
		drw_setscheme(drw, scheme[SchemeNorm]);
		#if PATCH_FONT_GROUPS
		drw_rect(drw, 0, 0, px, m->bh, 1, 1);
		#else // NO PATCH_FONT_GROUPS
		drw_rect(drw, 0, 0, px, bh, 1, 1);
		#endif // PATCH_FONT_GROUPS
	}
	#if PATCH_LOG_DIAGNOSTICS
	m->offsetx = px;
	m->panelw = pw;
	#endif // PATCH_LOG_DIAGNOSTICS
	#endif // PATCH_FLAG_FLOAT_ALIGNMENT
	#endif // PATCH_FLAG_PANEL

	/* draw status first so it can be overdrawn by tags later */
	if (m->showstatus &&
			(m == selmon
			#if PATCH_STATUS_ALLOW_FIXED_MONITOR
			|| m == status_always_on
			#endif // PATCH_STATUS_ALLOW_FIXED_MONITOR
			)
		) {
		#if PATCH_COLOUR_BAR
		drw_setscheme(drw, scheme[SchemeStatus]);
		#else // NO PATCH_COLOUR_BAR
		drw_setscheme(drw, scheme[SchemeNorm]);
		#endif // PATCH_COLOUR_BAR

		#if PATCH_FONT_GROUPS
		apply_barelement_fontgroup(m, StatusText);
		#endif // PATCH_FONT_GROUPS

		#if PATCH_STATUSCMD
		if (m->showstatus == -1) {
			m->bar[StatusText].w = TEXTW(DWM_VERSION_STRING_SHORT) - lrpad / 2 + 2 /* 2px extra right padding */
				#if PATCH_SYSTRAY
				+ m->stw
				#endif // PATCH_SYSTRAY
			;
			m->bar[StatusText].x = (
				m->mw - m->bar[StatusText].w
				#if PATCH_FLAG_PANEL
				- pw
				#endif // PATCH_FLAG_PANEL
			);
			#if PATCH_BIDIRECTIONAL_TEXT
			apply_fribidi(DWM_VERSION_STRING_SHORT);
			#endif // PATCH_BIDIRECTIONAL_TEXT
			x = drw_text(drw,
				#if PATCH_FONT_GROUPS
				m->bar[StatusText].x, 0, m->bar[StatusText].w, m->bh, lrpad / 2 - 2
				#else // NO PATCH_FONT_GROUPS
				m->bar[StatusText].x, 0, m->bar[StatusText].w, bh, lrpad / 2 - 2
				#endif // PATCH_FONT_GROUPS
				#if PATCH_SYSTRAY
				+ (showsystray && systrayonleft ? m->stw : 0)
				#endif // PATCH_SYSTRAY
				,
				0,
				#if PATCH_CLIENT_INDICATORS
				0,
				#endif // PATCH_CLIENT_INDICATORS
				1,
				#if PATCH_BIDIRECTIONAL_TEXT
				fribidi_text,
				#else // NO PATCH_BIDIRECTIONAL_TEXT
				DWM_VERSION_STRING_SHORT,
				#endif // PATCH_BIDIRECTIONAL_TEXT
				0
			);
		}
		else {
			char *text, *s;
			#if PATCH_STATUSCMD_COLOURS || PATCH_STATUSCMD_NONPRINTING
			char *s2;
			size_t bufptr;
			unsigned int isCode = 0;
			#endif // PATCH_STATUSCMD_COLOURS || PATCH_STATUSCMD_NONPRINTING
			char buffer[256];
			strncpy(buffer, stext, sizeof buffer);
			size_t bufsize = strlen(buffer);
			unsigned int padw = lrpad / 2;
			unsigned int tw = padw;
			for (text = s = buffer; *s; s++) {
				if ((unsigned char)(*s) < ' ') {
					*s = '\0';

					w = drw_fontset_getwidth(drw, text);
					if (w)
						tw += w + padw;
					text = s + 1;
				#if PATCH_STATUSCMD_COLOURS || PATCH_STATUSCMD_NONPRINTING
				} else if ((unsigned char)(*s) == '^') {
					if (!isCode) {
						if (text < s) {
							*s = '\0';
							tw += drw_fontset_getwidth(drw, text);
							*s = '^';
						}
						#if PATCH_STATUSCMD_COLOURS
						if (*(s + 1) == 'C')
							isCode = 2;
						#endif // PATCH_STATUSCMD_COLOURS
						#if PATCH_STATUSCMD_NONPRINTING
						if (*(s + 1) == 'N') {
							isCode = 3;
							text = s + 2;
						}
						#endif // PATCH_STATUSCMD_NONPRINTING
						if (!isCode)
							isCode = 1;
					} else {
						#if PATCH_STATUSCMD_NONPRINTING
						if (isCode == 3) {
							if (text < s) {
								*s = '\0';
								tw += drw_fontset_getwidth(drw, text);
								*s = '^';
							}
						}
						#endif // PATCH_STATUSCMD_NONPRINTING
						isCode = 0;
						text = s + 1;
					}
				#endif // PATCH_STATUSCMD_COLOURS || PATCH_STATUSCMD_NONPRINTING
				}
			}
			m->bar[StatusText].w = tw + drw_fontset_getwidth(drw, text) + lrpad / 2 + 2
				#if PATCH_SYSTRAY
				+ m->stw
				#endif // PATCH_SYSTRAY
			;
			m->bar[StatusText].x = (
				m->mw - m->bar[StatusText].w
				#if PATCH_FLAG_PANEL
				- pw
				#endif // PATCH_FLAG_PANEL
			);

			#if PATCH_STATUSCMD_COLOURS
			drw_setscheme(drw, scheme[SchemeStatusCmd]);
			#if PATCH_COLOUR_BAR
			drw->scheme[ColFg] = scheme[SchemeStatus][ColFg];
			drw->scheme[ColBg] = scheme[SchemeStatus][ColBg];
			#else // NO PATCH_COLOUR_BAR
			drw->scheme[ColFg] = scheme[SchemeNorm][ColFg];
			#endif // PATCH_COLOUR_BAR
			#endif // PATCH_STATUSCMD_COLOURS
			#if PATCH_STATUSCMD_NONPRINTING
			Clr* oldscheme;
			#endif // PATCH_STATUSCMD_NONPRINTING
			padw = lrpad / 2;
			#if PATCH_SYSTRAY
			if (showsystray && systrayonleft)
				x += m->stw;
			#endif // PATCH_SYSTRAY

			for (text = s = buffer; --bufsize > 0; s++) {
				#if PATCH_STATUSCMD_COLOURS || PATCH_STATUSCMD_NONPRINTING
				while (bufsize > 1 && (unsigned char)(*s) == '^') {
					s2 = s;
					while ((unsigned char)(*(++s2)) != '^' && --bufsize);
					if (!bufsize)
						break;
					*s = *s2 = '\0';
					s2++;

					if (text < s) {
						w = drw_fontset_getwidth(drw, text);
						if (w) {
							tw = w + padw;
							#if PATCH_BIDIRECTIONAL_TEXT
							apply_fribidi(text);
							#endif // PATCH_BIDIRECTIONAL_TEXT
							drw_text(drw,
								#if PATCH_FONT_GROUPS
								m->bar[StatusText].x + x, 0, tw, m->bh, padw, 0,
								#else // NO PATCH_FONT_GROUPS
								m->bar[StatusText].x + x, 0, tw, bh, padw, 0,
								#endif // PATCH_FONT_GROUPS
								#if PATCH_CLIENT_INDICATORS
								0,
								#endif // PATCH_CLIENT_INDICATORS
								1,
								#if PATCH_BIDIRECTIONAL_TEXT
								fribidi_text,
								#else // NO PATCH_BIDIRECTIONAL_TEXT
								text,
								#endif // PATCH_BIDIRECTIONAL_TEXT
								0
							);
							x += tw;
							padw = 0;
						}
					}
					text = s;

					#if PATCH_STATUSCMD_COLOURS
					if ((unsigned char)(*(s + 1)) == 'C'
						#if PATCH_STATUSCMD_COLOURS_DECOLOURIZE
						&& (!status_decolourize_inactive || m == selmon)
						#endif // PATCH_STATUSCMD_COLOURS_DECOLOURIZE
					) {

						isCode = atoi(s + 2);
						if (isCode && isCode <= (SchemeStatusCmd - SchemeStatC1 + 1))
							drw_setscheme(drw, scheme[isCode + SchemeStatC1 - 1]);
						else {
							drw_setscheme(drw, scheme[SchemeStatusCmd]);
							drw_clr_create(drw, &drw->scheme[ColFg], s + 2);
						}
						#if PATCH_COLOUR_BAR
						drw->scheme[ColBg] = scheme[SchemeStatus][ColBg];
						#endif // PATCH_COLOUR_BAR
					}
					#endif // PATCH_STATUSCMD_COLOURS
					#if PATCH_STATUSCMD_NONPRINTING
					if ((unsigned char)(*(s + 1)) == 'N') {
						w = drw_fontset_getwidth(drw, s + 2);
						if (w) {
							tw = w + padw;
							oldscheme = drw->scheme;
							drw_setscheme(drw, scheme[SchemeStatCNP]);
							drw->scheme[ColBg] = drw->scheme[ColFg] = scheme[
								#if PATCH_COLOUR_BAR
								SchemeStatus
								#else // NO PATCH_COLOUR_BAR
								SchemeNorm
								#endif // PATCH_COLOUR_BAR
							][ColBg];
							drw_text(drw,
								#if PATCH_FONT_GROUPS
								m->bar[StatusText].x + x, 0, tw, m->bh, padw, 0,
								#else // NO PATCH_FONT_GROUPS
								m->bar[StatusText].x + x, 0, tw, bh, padw, 0,
								#endif // PATCH_FONT_GROUPS
								#if PATCH_CLIENT_INDICATORS
								0,
								#endif // PATCH_CLIENT_INDICATORS
								1, "", 0
							);
							drw->scheme = oldscheme;
							x += tw;
							padw = 0;
						}
					}
					#endif // PATCH_STATUSCMD_NONPRINTING

					for (bufptr = bufsize; bufptr--; s2++)
						*(s + (bufsize - bufptr - 1)) = (unsigned char)(*s2);
					*(s + bufsize) = '\0';
				}
				#endif // PATCH_STATUSCMD_COLOURS || PATCH_STATUSCMD_NONPRINTING
				if ((unsigned char)(*s) < ' ') {
					if (text < s) {
						w = drw_fontset_getwidth(drw, text);
						if (w) {
							tw = w + padw;
							#if PATCH_BIDIRECTIONAL_TEXT
							apply_fribidi(text);
							#endif // PATCH_BIDIRECTIONAL_TEXT
							drw_text(drw,
								#if PATCH_FONT_GROUPS
								m->bar[StatusText].x + x, 0, m->mw - m->bar[StatusText].x + x, m->bh, padw, 0,
								#else // NO PATCH_FONT_GROUPS
								m->bar[StatusText].x + x, 0, m->mw - m->bar[StatusText].x + x, bh, padw, 0,
								#endif // PATCH_FONT_GROUPS
								#if PATCH_CLIENT_INDICATORS
								0,
								#endif // PATCH_CLIENT_INDICATORS
								1,
								#if PATCH_BIDIRECTIONAL_TEXT
								fribidi_text,
								#else // NO PATCH_BIDIRECTIONAL_TEXT
								text,
								#endif // PATCH_BIDIRECTIONAL_TEXT
								0
							);
							x += tw;
						}
					}
					text = s + 1;
					padw = lrpad / 2;
					if (!*text)
						break;

					#if PATCH_STATUSCMD_COLOURS
					drw_setscheme(drw, scheme[SchemeStatusCmd]);
					#if PATCH_COLOUR_BAR
					drw->scheme[ColFg] = scheme[SchemeStatus][ColFg];
					drw->scheme[ColBg] = scheme[SchemeStatus][ColBg];
					#else // NO PATCH_COLOUR_BAR
					drw->scheme[ColFg] = scheme[SchemeNorm][ColFg];
					#endif // PATCH_COLOUR_BAR
					#endif // PATCH_STATUSCMD_COLOURS
				}
			}
			if (*text) {
				#if PATCH_BIDIRECTIONAL_TEXT
				apply_fribidi(text);
				#endif // PATCH_BIDIRECTIONAL_TEXT
				x = drw_text(drw,
					#if PATCH_FONT_GROUPS
					m->bar[StatusText].x + x, 0, m->mw - (m->bar[StatusText].x + x), m->bh, padw, 0,
					#else // NO PATCH_FONT_GROUPS
					m->bar[StatusText].x + x, 0, m->mw - (m->bar[StatusText].x + x), bh, padw, 0,
					#endif // PATCH_FONT_GROUPS
					#if PATCH_CLIENT_INDICATORS
					0,
					#endif // PATCH_CLIENT_INDICATORS
					1,
					#if PATCH_BIDIRECTIONAL_TEXT
					fribidi_text,
					#else // NO PATCH_BIDIRECTIONAL_TEXT
					text,
					#endif // PATCH_BIDIRECTIONAL_TEXT
					0
				);
			}
		}
		#else // NO PATCH_STATUSCMD
		m->bar[StatusText].w = drw_fontset_getwidth(drw, (m->showstatus == -1 ? DWM_VERSION_STRING_SHORT : stext)) + lrpad / 2 + 2 /* 2px extra right padding */
			#if PATCH_SYSTRAY
			+ m->stw
			#endif // PATCH_SYSTRAY
		;
		m->bar[StatusText].x = (
			m->mw - m->bar[StatusText].w
			#if PATCH_FLAG_PANEL
			- pw
			#endif // PATCH_FLAG_PANEL
		);
		#if PATCH_BIDIRECTIONAL_TEXT
		apply_fribidi(m->showstatus == -1 ? DWM_VERSION_STRING_SHORT : stext);
		#endif // PATCH_BIDIRECTIONAL_TEXT
		x = drw_text(drw,
			#if PATCH_FONT_GROUPS
			m->bar[StatusText].x, 0, m->bar[StatusText].w, m->bh,
			#else // NO PATCH_FONT_GROUPS
			m->bar[StatusText].x, 0, m->bar[StatusText].w, bh,
			#endif // PATCH_FONT_GROUPS
			lrpad / 2 - 2
			#if PATCH_SYSTRAY
			+ (showsystray && systrayonleft ? m->stw : 0)
			#endif // PATCH_SYSTRAY
			,
			0,
			#if PATCH_CLIENT_INDICATORS
			0,
			#endif // PATCH_CLIENT_INDICATORS
			1,
			#if PATCH_BIDIRECTIONAL_TEXT
			fribidi_text,
			#else // NO PATCH_BIDIRECTIONAL_TEXT
			m->showstatus == -1 ? DWM_VERSION_STRING_SHORT : stext,
			#endif // PATCH_BIDIRECTIONAL_TEXT
			0
		);
		#endif // PATCH_STATUSCMD
	}
	else {
		#if PATCH_SYSTRAY
		m->bar[StatusText].w = m->stw;
		#else // NO PATCH_SYSTRAY
		m->bar[StatusText].w = 0;
		#endif // PATCH_SYSTRAY
		m->bar[StatusText].x = (
			m->mw - m->bar[StatusText].w
			#if PATCH_FLAG_PANEL
			- pw
			#endif // PATCH_FLAG_PANEL
		);
		#if PATCH_FLAG_PANEL
		#if PATCH_FONT_GROUPS
		drw_rect(drw, m->bar[StatusText].x, 0, m->mw - m->bar[StatusText].x, m->bh, 1, 1);
		#else // NO PATCH_FONT_GROUPS
		drw_rect(drw, m->bar[StatusText].x, 0, m->mw - m->bar[StatusText].x, bh, 1, 1);
		#endif // PATCH_FONT_GROUPS
		#endif // PATCH_FLAG_PANEL
	}

	// draw custom bar modules here;
	// calculate module width
	//m->bar[CustomModule].w = ...
	//m->bar[CustomModule].x = m->bar[StatusText].x - customwidth - m->bar[CustomModule].w;
	//customwidth += m->bar[CustomModule].w;
	#if PATCH_SHOW_DESKTOP
	#if PATCH_SHOW_DESKTOP_BUTTON
	m->bar[ShowDesktop].w = 0;
	if (drawbar_elementvisible(m, ShowDesktop) && showdesktop) {
		#if PATCH_FONT_GROUPS
		apply_barelement_fontgroup(m, ShowDesktop);
		#endif // PATCH_FONT_GROUPS

		#if PATCH_SHOW_DESKTOP_ONLY_WHEN_ACTIVE || PATCH_SHOW_DESKTOP_UNMANAGED
		c = getdesktopclient(m, &x);
		if (
			#if PATCH_SHOW_DESKTOP_ONLY_WHEN_ACTIVE
			(!showdesktop_when_active || c || m->showdesktop) ||
			#endif // PATCH_SHOW_DESKTOP_ONLY_WHEN_ACTIVE
			#if PATCH_SHOW_DESKTOP_UNMANAGED
			(showdesktop_unmanaged && desktopwin) ||
			#endif // PATCH_SHOW_DESKTOP_UNMANAGED
			0
		)
		#endif // PATCH_SHOW_DESKTOP_ONLY_WHEN_ACTIVE || PATCH_SHOW_DESKTOP_UNMANAGED
		{
			m->bar[ShowDesktop].w = TEXTW(showdesktop_button);
			m->bar[ShowDesktop].x = m->bar[StatusText].x - customwidth - m->bar[ShowDesktop].w;
			customwidth += m->bar[ShowDesktop].w;
			drw_setscheme(drw, scheme[m->showdesktop ?
				#if PATCH_SHOW_DESKTOP_UNMANAGED
				showdesktop_unmanaged && (!desktopwin || !x) ? SchemeHide :
				#endif // PATCH_SHOW_DESKTOP_UNMANAGED
				#if PATCH_SHOW_DESKTOP_ONLY_WHEN_ACTIVE
				showdesktop_when_active && !x ? SchemeHide :
				#endif // PATCH_SHOW_DESKTOP_ONLY_WHEN_ACTIVE
				SchemeSel : SchemeNorm
			]);
			#if PATCH_BIDIRECTIONAL_TEXT
			apply_fribidi(showdesktop_button);
			#endif // PATCH_BIDIRECTIONAL_TEXT
			drw_text(drw,
				#if PATCH_FONT_GROUPS
				m->bar[ShowDesktop].x, 0, m->bar[ShowDesktop].w, m->bh, lrpad / 2, 0,
				#else // NO PATCH_FONT_GROUPS
				m->bar[ShowDesktop].x, 0, m->bar[ShowDesktop].w, bh, lrpad / 2, 0,
				#endif // PATCH_FONT_GROUPS
				#if PATCH_CLIENT_INDICATORS
				0,
				#endif // PATCH_CLIENT_INDICATORS
				1,
				#if PATCH_BIDIRECTIONAL_TEXT
				fribidi_text,
				#else // NO PATCH_BIDIRECTIONAL_TEXT
				showdesktop_button,
				#endif // PATCH_BIDIRECTIONAL_TEXT
				0
			);
			//drw_rect(drw, m->bar[ShowDesktop].x, -1, m->bar[ShowDesktop].w, bh +2, 0, 0);
		}
	}
	#endif // PATCH_SHOW_DESKTOP_BUTTON
	#endif // PATCH_SHOW_DESKTOP

	if (!skiptags || !m->bar[TagBar].w) {

		if (drawbar_elementvisible(m, TagBar)) {
			skiptags = 0;

			#if PATCH_FONT_GROUPS
			apply_barelement_fontgroup(m, TagBar);
			#endif // PATCH_FONT_GROUPS

			for (i = 0; i < LENGTH(tags); i++) {
				#if PATCH_SHOW_MASTER_CLIENT_ON_TAG
				masterclientontag[i] = NULL;
				masterclientontagmem[i] = 0;
				#endif // PATCH_SHOW_MASTER_CLIENT_ON_TAG
				#if (PATCH_WINDOW_ICONS && PATCH_WINDOW_ICONS_ON_TAGS) || PATCH_CUSTOM_TAG_ICONS
				mc[i] = NULL;
				#endif // (PATCH_WINDOW_ICONS && PATCH_WINDOW_ICONS_ON_TAGS) || PATCH_CUSTOM_TAG_ICONS
				#if PATCH_FLAG_HIDDEN
				visible[i] = hidden[i] = 0;
				#endif // PATCH_FLAG_HIDDEN
				#if PATCH_CLIENT_INDICATORS
				total[i] = 0;
				#if PATCH_FLAG_STICKY
				sticky[i] = 0;
				#endif // PATCH_FLAG_STICKY
				#endif // PATCH_CLIENT_INDICATORS
				m->tagw[i] = 0;
			}

			for (c = m->clients; c; c = c->next) {
				if (c->dormant
					#if PATCH_FLAG_IGNORED
					|| c->isignored
					#endif // PATCH_FLAG_IGNORED
					#if PATCH_FLAG_PANEL
					|| c->ispanel
					|| c->ultparent->ispanel
					#endif // PATCH_FLAG_PANEL
					) continue;
				//occ |= c->tags == TAGMASK ? 0 : c->tags;
				if (c->tags != TAGMASK)
					occ |= c->tags;
				if (c->isurgent && urgency
					#if PATCH_FLAG_HIDDEN
					&& !c->ishidden
					#endif // PATCH_FLAG_HIDDEN
					)
					urg |= c->tags;

				#if PATCH_SHOW_MASTER_CLIENT_ON_TAG
				if (m->showmaster) {
					for (i = 0; i < LENGTH(tags); i++)
						if (!masterclientontag[i] && c->tags & (1<<i)
							#if PATCH_SHOW_DESKTOP
							&& !(c->isdesktop || c->ondesktop)
							#endif // PATCH_SHOW_DESKTOP
							#if PATCH_FLAG_HIDDEN
							&& !c->ishidden
							#endif // PATCH_FLAG_HIDDEN
						) {
							if (c->dispclass)
								masterclientontag[i] = c->dispclass;
							else {
								XClassHint ch = { NULL, NULL };
								XGetClassHint(dpy, c->win, &ch);
								if (ch.res_class) {
									masterclientontag[i] = ch.res_class;
									masterclientontagmem[i] = 1;
								}
								else
									masterclientontag[i] = (char *)broken;
								if (ch.res_name)
									XFree(ch.res_name);
							}
							#if PATCH_WINDOW_ICONS && PATCH_WINDOW_ICONS_ON_TAGS
							if (m->showiconsontags)
								mc[i] = c;
							#endif // PATCH_WINDOW_ICONS && PATCH_WINDOW_ICONS_ON_TAGS
						}
				}
				#if PATCH_WINDOW_ICONS && PATCH_WINDOW_ICONS_ON_TAGS
				else if (m->showiconsontags)
				#endif // PATCH_WINDOW_ICONS && PATCH_WINDOW_ICONS_ON_TAGS
				#endif // PATCH_SHOW_MASTER_CLIENT_ON_TAG
				#if PATCH_WINDOW_ICONS && PATCH_WINDOW_ICONS_ON_TAGS
				{
					for (i = 0; i < LENGTH(tags); i++)
						if (!mc[i] && c->tags & (1<<i)
							#if PATCH_SHOW_DESKTOP
							&& !(c->isdesktop || c->ondesktop)
							#endif // PATCH_SHOW_DESKTOP
							)
							mc[i] = c;
				}
				#endif // PATCH_WINDOW_ICONS && PATCH_WINDOW_ICONS_ON_TAGS
				#if PATCH_FLAG_HIDDEN || PATCH_CLIENT_INDICATORS
				for (i = 0; i < LENGTH(tags); i++) {
					if (
						#if 0// PATCH_SHOW_DESKTOP
						showdesktop ? desktopvalidex(c, 1<<i, 0) :
						#endif // PATCH_SHOW_DESKTOP
						c->tags & (1<<i)
					) {
						#if PATCH_CLIENT_INDICATORS
						++total[i];
						#endif // PATCH_CLIENT_INDICATORS
						#if PATCH_FLAG_HIDDEN
						if (c->ishidden)
							++hidden[i];
						else
							++visible[i];
						#endif // PATCH_FLAG_HIDDEN
					}
					#if PATCH_FLAG_STICKY && PATCH_CLIENT_INDICATORS
					else if (c->issticky)
						++sticky[i];
					#endif // PATCH_FLAG_STICKY && PATCH_CLIENT_INDICATORS
				}
				#endif // PATCH_FLAG_HIDDEN || PATCH_CLIENT_INDICATORS
			}

			#if PATCH_FLAG_PANEL
			x = px;
			#else // NO PATCH_FLAG_PANEL
			x = 0;
			#endif // PATCH_FLAG_PANEL
			m->bar[TagBar].x = x;
			for (i = 0; i < LENGTH(tags); i++) {
				#if PATCH_HIDE_VACANT_TAGS
				/* do not draw vacant tags */
				if (m->hidevacant && !m->alwaysvisible[i] &&
					!(occ & 1 << i || m->tagset[m->seltags] & 1 << i))
					continue;
				#endif // PATCH_HIDE_VACANT_TAGS

				if (x >= m->bar[StatusText].x - customwidth) {
					m->tagw[i] = 0;
					continue;
				}

				#if PATCH_WINDOW_ICONS && PATCH_WINDOW_ICONS_ON_TAGS
				if (m->showiconsontags && mc[i] && !mc[i]->tagicon)
						mc[i]->tagicon = geticonprop(
							#if PATCH_WINDOW_ICONS_DEFAULT_ICON || PATCH_WINDOW_ICONS_CUSTOM_ICONS
							mc[i],
							#endif // PATCH_WINDOW_ICONS_DEFAULT_ICON || PATCH_WINDOW_ICONS_CUSTOM_ICONS
							mc[i]->win, &mc[i]->tagicw, &mc[i]->tagich,
							#if 0 // icon size relative to total bar height;
							#if PATCH_CLIENT_INDICATORS
							client_ind ? (MIN((bh - (client_ind_size / 2) - 7), (minbh - 2))) :
							#endif // PATCH_CLIENT_INDICATORS
							(MIN((bh - 8), (minbh - 2)))
							#endif // icon size relative to total bar height;
							(
								#if PATCH_FONT_GROUPS
								drw->selfonts ? drw->selfonts->h :
								#endif // PATCH_FONT_GROUPS
								drw->fonts->h
							)
						);
				#endif // PATCH_WINDOW_ICONS && PATCH_WINDOW_ICONS_ON_TAGS
				#if PATCH_CUSTOM_TAG_ICONS
				if (m->showcustomtagicons) {
					if (!mc[i]) {
						mc[i] = dummyc;
						mc[i]->tagicon = None;
					}
					if (mc[i] && !mc[i]->tagicon) {
						if (m->tagiconpaths[i] && !m->tagicons[i]) {
							m->tagicons[i] = drw_picture_create_resized_from_file(drw,
								m->tagiconpaths[i], &m->tagicw[i], &m->tagich[i],
								#if 0 // icon size relative to total bar height;
								#if PATCH_CLIENT_INDICATORS
								client_ind ? (MIN((bh - (client_ind_size / 2) - 7), (minbh - 2))) :
								#endif // PATCH_CLIENT_INDICATORS
								(MIN((bh - 8), (minbh - 2)))
								#endif // icon size relative to total bar height;
								(
									#if PATCH_FONT_GROUPS
									drw->selfonts ? drw->selfonts->h :
									#endif // PATCH_FONT_GROUPS
									drw->fonts->h
								)
							);
						}
						if (m->tagicons[i]) {
							mc[i]->tagicon = m->tagicons[i];
							mc[i]->tagicw = m->tagicw[i];
							mc[i]->tagich = m->tagich[i];
						}
					}
				}
				#endif // PATCH_CUSTOM_TAG_ICONS

				#if PATCH_ALT_TAGS
				#if (PATCH_WINDOW_ICONS && PATCH_WINDOW_ICONS_ON_TAGS) || PATCH_CUSTOM_TAG_ICONS
				if (
					(
						#if PATCH_WINDOW_ICONS && PATCH_WINDOW_ICONS_ON_TAGS
						m->showiconsontags
						#if PATCH_CUSTOM_TAG_ICONS
						||
						#endif // PATCH_CUSTOM_TAG_ICONS
						#endif // PATCH_WINDOW_ICONS && PATCH_WINDOW_ICONS_ON_TAGS
						#if PATCH_CUSTOM_TAG_ICONS
						m->showcustomtagicons
						#endif // PATCH_CUSTOM_TAG_ICONS
					) && mc[i] && mc[i]->tagicon && mc[i]->tagicw
				) {
					if ((tagw = mc[i]->tagicw))
						tagw += lrpad;
					else {
						snprintf(tagdisp, 64, "%s", m->tags[i]);
						tagw = TEXTW(tagdisp);
					}
				}
				else
				#endif // (PATCH_WINDOW_ICONS && PATCH_WINDOW_ICONS_ON_TAGS) || PATCH_CUSTOM_TAG_ICONS
				{
					snprintf(tagdisp, 64, "%s", m->tags[i]);
					tagw = TEXTW(tagdisp);
				}
				snprintf(tagdisp, 64, "%s", tags[i]);
				alttagw = TEXTW(tagdisp);
				if (tagw > alttagw)
					tagw = (m->alttags ? (tagw - alttagw) : 0);
				else
					tagw = (!m->alttags ? (alttagw - tagw) : 0);
				#else // NO PATCH_ALT_TAGS
				snprintf(tagdisp, 64, "%s", tags[i]);
				#endif // PATCH_ALT_TAGS

				//
				#if PATCH_SHOW_MASTER_CLIENT_ON_TAG
				if (masterclientontag[i]) {
					size_t k = strlen(masterclientontag[i]);
					if (k > sizeof masterclientbuff)
						k = sizeof masterclientbuff;
					if (lcaselbl) {
						for (size_t j = k; j; --j)
							masterclientbuff[j - 1] = tolower(masterclientontag[i][j - 1]);
					}
					else
						strncpy(masterclientbuff, masterclientontag[i], k);
					masterclientbuff[k] = '\0';
					if (masterclientontagmem[i])
						XFree(masterclientontag[i]);
				#if PATCH_ALT_TAGS
					#if (PATCH_WINDOW_ICONS && PATCH_WINDOW_ICONS_ON_TAGS) || PATCH_CUSTOM_TAG_ICONS
					if (!m->alttags && mc[i] && mc[i]->tagicon && mc[i]->tagicw) {
						if (m->reversemaster)
							snprintf(tagdisp, 64, m->ptagf, masterclientbuff, "%s");
						else
							snprintf(tagdisp, 64, m->ptagf, "%s", masterclientbuff);
					}
					else
					#endif // (PATCH_WINDOW_ICONS && PATCH_WINDOW_ICONS_ON_TAGS) || PATCH_CUSTOM_TAG_ICONS
					{
						if (m->reversemaster)
							snprintf(tagdisp, 64, m->ptagf, masterclientbuff, m->alttags ? tags[i] : m->tags[i]);
						else
							snprintf(tagdisp, 64, m->ptagf, m->alttags ? tags[i] : m->tags[i], masterclientbuff);
					}
				}
				else
				#if (PATCH_WINDOW_ICONS && PATCH_WINDOW_ICONS_ON_TAGS) || PATCH_CUSTOM_TAG_ICONS
				if (!m->alttags && mc[i] && mc[i]->tagicon && mc[i]->tagicw)
					snprintf(tagdisp, 64, (m->showmaster ? m->etagf : "%s"), "%s");
				else
				#endif // (PATCH_WINDOW_ICONS && PATCH_WINDOW_ICONS_ON_TAGS) || PATCH_CUSTOM_TAG_ICONS
					snprintf(tagdisp, 64, (m->showmaster ? m->etagf : "%s"), m->alttags ? tags[i] : m->tags[i]);

				#if (PATCH_WINDOW_ICONS && PATCH_WINDOW_ICONS_ON_TAGS) || PATCH_CUSTOM_TAG_ICONS
				if (!m->alttags && mc[i] && mc[i]->tagicon && mc[i]->tagicw) {
					w = mc[i]->tagicw + tagw + lrpad;
					if (m->showmaster)
						w += (drw_fontset_getwidth(drw, tagdisp) - drw_fontset_getwidth(drw, "%s"));
				}
				else
				#endif // (PATCH_WINDOW_ICONS && PATCH_WINDOW_ICONS_ON_TAGS) || PATCH_CUSTOM_TAG_ICONS
				w = (TEXTW(tagdisp) + tagw);
				if (x + w >= m->bar[StatusText].x - customwidth)
					w = ((m->bar[StatusText].x - customwidth) - x);
				m->tagw[i] = w;
				if (w && tagw > 0) {
					#if PATCH_COLOUR_BAR
					drw_setscheme(drw, scheme[SchemeTagBar]);
					#else // NO PATCH_COLOUR_BAR
					drw_setscheme(drw, scheme[SchemeNorm]);
					#endif // PATCH_COLOUR_BAR
					#if PATCH_FONT_GROUPS
					drw_rect(drw, x, 0, w, m->bh, 1, 1);
					#else // NO PATCH_FONT_GROUPS
					drw_rect(drw, x, 0, w, bh, 1, 1);
					#endif // PATCH_FONT_GROUPS
				}
				#else // NO PATCH_ALT_TAGS

					#if (PATCH_WINDOW_ICONS && PATCH_WINDOW_ICONS_ON_TAGS) || PATCH_CUSTOM_TAG_ICONS
					if (
						(
							#if PATCH_WINDOW_ICONS && PATCH_WINDOW_ICONS_ON_TAGS
							m->showiconsontags
							#if PATCH_CUSTOM_TAG_ICONS
							||
							#endif // PATCH_CUSTOM_TAG_ICONS
							#endif // PATCH_WINDOW_ICONS && PATCH_WINDOW_ICONS_ON_TAGS
							#if PATCH_CUSTOM_TAG_ICONS
							m->showcustomtagicons
							#endif // PATCH_CUSTOM_TAG_ICONS
						) && mc[i] && mc[i]->tagicon && mc[i]->tagicw) {
						if (m->reversemaster)
							snprintf(tagdisp, 64, m->ptagf, masterclientbuff, "%s");
						else
							snprintf(tagdisp, 64, m->ptagf, "%s", masterclientbuff);
					}
					else
					#endif // (PATCH_WINDOW_ICONS && PATCH_WINDOW_ICONS_ON_TAGS) || PATCH_CUSTOM_TAG_ICONS
					{
						if (m->reversemaster)
							snprintf(tagdisp, 64, m->ptagf, masterclientbuff, tags[i]);
						else
							snprintf(tagdisp, 64, m->ptagf, tags[i], masterclientbuff);
					}
				}
				else
					#if (PATCH_WINDOW_ICONS && PATCH_WINDOW_ICONS_ON_TAGS) || PATCH_CUSTOM_TAG_ICONS
					if (mc[i] && mc[i]->tagicon && mc[i]->tagicw)
						snprintf(tagdisp, 64, (m->showmaster ? m->etagf : "%s"), "%s");
					else
					#endif // (PATCH_WINDOW_ICONS && PATCH_WINDOW_ICONS_ON_TAGS) || PATCH_CUSTOM_TAG_ICONS
					snprintf(tagdisp, 64, (m->showmaster ? m->etagf : "%s"), tags[i]);

				#if (PATCH_WINDOW_ICONS && PATCH_WINDOW_ICONS_ON_TAGS) || PATCH_CUSTOM_TAG_ICONS
				if (mc[i] && mc[i]->tagicon && mc[i]->tagicw) {
					w = mc[i]->tagicw + lrpad;
					if (m->showmaster)
						w += (drw_fontset_getwidth(drw, tagdisp) - drw_fontset_getwidth(drw, "%s"));
				}
				else
				#endif // (PATCH_WINDOW_ICONS && PATCH_WINDOW_ICONS_ON_TAGS) || PATCH_CUSTOM_TAG_ICONS
				w = TEXTW(tagdisp);
				if (x + w >= m->bar[StatusText].x - customwidth)
					w = ((m->bar[StatusText].x - customwidth) - x);
				m->tagw[i] = w;

				#endif // PATCH_ALT_TAGS
				#else // NO PATCH_SHOW_MASTER_CLIENT_ON_TAG
				#if (PATCH_WINDOW_ICONS && PATCH_WINDOW_ICONS_ON_TAGS) || PATCH_CUSTOM_TAG_ICONS
				if (
					#if PATCH_ALT_TAGS
					!m->alttags &&
					#endif // PATCH_ALT_TAGS
					mc[i] && mc[i]->tagicon && mc[i]->tagicw
					)
					w = mc[i]->tagicw + lrpad;
				else
				#endif // (PATCH_WINDOW_ICONS && PATCH_WINDOW_ICONS_ON_TAGS) || PATCH_CUSTOM_TAG_ICONS
				{
					#if PATCH_ALT_TAGS
					snprintf(tagdisp, 64, "%s", m->alttags ? tags[i] : m->tags[i]);
					w = (TEXTW(tagdisp) + tagw);
					#else // NO PATCH_ALT_TAGS
					w = TEXTW(tagdisp);
					#endif // PATCH_ALT_TAGS
				}
				if (x + w >= m->bar[StatusText].x - customwidth)
					w = ((m->bar[StatusText].x - customwidth) - x);
				m->tagw[i] = w;
				#endif // PATCH_SHOW_MASTER_CLIENT_ON_TAG

				drw_setscheme(drw, scheme[
					(urg & 1 << i) ? SchemeUrg :
						m->tagset[m->seltags] & 1 << i
						#if PATCH_SHOW_DESKTOP
						&& !m->showdesktop
						#endif // PATCH_SHOW_DESKTOP
						?
							#if PATCH_RAINBOW_TAGS
							SchemeTag1 + i
							#else // NO PATCH_RAINBOW_TAGS
							#if PATCH_COLOUR_BAR
							SchemeTagBarSel
							#else // PATCH_COLOUR_BAR
							SchemeSel
							#endif // PATCH_COLOUR_BAR
							#endif // PATCH_RAINBOW_TAGS
						:
						#if PATCH_FLAG_HIDDEN
						(!visible[i] && hidden[i] > 0)
						#if PATCH_SHOW_DESKTOP
						|| (m->showdesktop && m->tagset[m->seltags] & 1 << i)
						#endif // PATCH_SHOW_DESKTOP
						?
							#if PATCH_COLOUR_BAR
							SchemeTagBarHide
							#else // NO PATCH_COLOUR_BAR
							SchemeHide
							#endif // PATCH_COLOUR_BAR
						:
						#endif // PATCH_FLAG_HIDDEN
						#if PATCH_COLOUR_BAR
						SchemeTagBar
						#else // NO PATCH_COLOUR_BAR
						SchemeNorm
						#endif // PATCH_COLOUR_BAR
				]);

				#if (PATCH_WINDOW_ICONS && PATCH_WINDOW_ICONS_ON_TAGS) || PATCH_CUSTOM_TAG_ICONS
				if (
					(
						(
							#if PATCH_WINDOW_ICONS && PATCH_WINDOW_ICONS_ON_TAGS
							!m->showiconsontags
							#if PATCH_CUSTOM_TAG_ICONS
							&&
							#endif // PATCH_CUSTOM_TAG_ICONS
							#endif // PATCH_WINDOW_ICONS && PATCH_WINDOW_ICONS_ON_TAGS
							#if PATCH_CUSTOM_TAG_ICONS
							!m->showcustomtagicons
							#endif // PATCH_CUSTOM_TAG_ICONS
						)
						|| !mc[i] || !mc[i]->tagicon || !mc[i]->tagicw
					)
					#if PATCH_ALT_TAGS
					|| m->alttags
					#endif // PATCH_ALT_TAGS
					#if PATCH_SHOW_MASTER_CLIENT_ON_TAG
					|| m->showmaster
					#endif // PATCH_SHOW_MASTER_CLIENT_ON_TAG
				)
				#endif // (PATCH_WINDOW_ICONS && PATCH_WINDOW_ICONS_ON_TAGS) || PATCH_CUSTOM_TAG_ICONS
				{
					#if PATCH_BIDIRECTIONAL_TEXT
					apply_fribidi(tagdisp);
					#endif // PATCH_BIDIRECTIONAL_TEXT
					#if (PATCH_SHOW_MASTER_CLIENT_ON_TAG && ((PATCH_WINDOW_ICONS && PATCH_WINDOW_ICONS_ON_TAGS) || PATCH_CUSTOM_TAG_ICONS))
					if (
						#if PATCH_ALT_TAGS
						!m->alttags &&
						#endif // PATCH_ALT_TAGS
						(
							#if PATCH_WINDOW_ICONS && PATCH_WINDOW_ICONS_ON_TAGS
							m->showiconsontags
							#if PATCH_CUSTOM_TAG_ICONS
							||
							#endif // PATCH_CUSTOM_TAG_ICONS
							#endif // NO PATCH_WINDOW_ICONS && PATCH_WINDOW_ICONS_ON_TAGS
							#if PATCH_CUSTOM_TAG_ICONS
							m->showcustomtagicons
							#endif // PATCH_CUSTOM_TAG_ICONS
						) && mc[i] && mc[i]->tagicon && mc[i]->tagicw
						)
						textwithicon(
							#if PATCH_BIDIRECTIONAL_TEXT
							fribidi_text,
							#else // NO PATCH_BIDIRECTIONAL_TEXT
							tagdisp,
							#endif // PATCH_BIDIRECTIONAL_TEXT
							mc[i]->tagicon,
							mc[i]->tagicw,
							mc[i]->tagich,
							#if PATCH_ALT_TAGS
							m->tags[i],
							#else // NO PATCH_ALT_TAGS
							tags[i],
							#endif // PATCH_ALT_TAGS
							x, 0, w,
							#if PATCH_FONT_GROUPS
							m->bh,
							#else // NO PATCH_FONT_GROUPS
							bh,
							#endif // PATCH_FONT_GROUPS
							(lrpad / 2),
							#if PATCH_CLIENT_INDICATORS
							client_ind
							#if 0 // only offset when there are indicators to be drawn;
							&& (total[i]
								#if PATCH_FLAG_STICKY
								+ sticky[i]
								#endif // PATCH_FLAG_STICKY
							)
							#endif // only offset when there are indicators to be drawn;
							? offsety : 0,
							#endif // PATCH_CLIENT_INDICATORS
							0
						);
					else
					#endif // (PATCH_SHOW_MASTER_CLIENT_ON_TAG && ((PATCH_WINDOW_ICONS && PATCH_WINDOW_ICONS_ON_TAGS) || PATCH_CUSTOM_TAG_ICONS))
					drw_text(
						drw, x, 0, w,
						#if PATCH_FONT_GROUPS
						m->bh,
						#else // NO PATCH_FONT_GROUPS
						bh,
						#endif // PATCH_FONT_GROUPS
						(lrpad / 2)
						#if PATCH_ALT_TAGS
						+ (
							#if PATCH_SHOW_MASTER_CLIENT_ON_TAG
							(m->showmaster && masterclientontag[i] && m->reversemaster) ? 0 :
							(m->showmaster && masterclientontag[i]) ? tagw :
							#endif // PATCH_SHOW_MASTER_CLIENT_ON_TAG
							(tagw / 2)
						)
						#endif // PATCH_ALT_TAGS
						,
						0,
						#if PATCH_CLIENT_INDICATORS
						client_ind
						#if 0 // only offset when there are indicators to be drawn;
						&& (total[i]
							#if PATCH_FLAG_STICKY
							+ sticky[i]
							#endif // PATCH_FLAG_STICKY
						)
						#endif // only offset when there are indicators to be drawn;
						? offsety : 0,
						#endif // PATCH_CLIENT_INDICATORS
						1,
						#if PATCH_BIDIRECTIONAL_TEXT
						fribidi_text,
						#else // NO PATCH_BIDIRECTIONAL_TEXT
						tagdisp,
						#endif // PATCH_BIDIRECTIONAL_TEXT
						0
					);
				}
				#if (PATCH_WINDOW_ICONS && PATCH_WINDOW_ICONS_ON_TAGS) || PATCH_CUSTOM_TAG_ICONS
				else if (
					(
						#if PATCH_WINDOW_ICONS && PATCH_WINDOW_ICONS_ON_TAGS
						m->showiconsontags
						#if PATCH_CUSTOM_TAG_ICONS
						||
						#endif // PATCH_CUSTOM_TAG_ICONS
						#endif // PATCH_WINDOW_ICONS && PATCH_WINDOW_ICONS_ON_TAGS
						#if PATCH_CUSTOM_TAG_ICONS
						m->showcustomtagicons
						#endif // PATCH_CUSTOM_TAG_ICONS
					) && mc[i] && mc[i]->tagicon && mc[i]->tagicw
					#if PATCH_ALT_TAGS
					&& !m->alttags
					#endif // PATCH_ALT_TAGS
				) {
					#if PATCH_FONT_GROUPS
					drw_rect(drw, x, 0, w, m->bh, 1, 1);
					drw_pic(
						drw, x + (
						#if PATCH_SHOW_MASTER_CLIENT_ON_TAG
						m->showmaster ? lrpad / 2 :
						#endif // PATCH_SHOW_MASTER_CLIENT_ON_TAG
						(w - mc[i]->tagicw) / 2),
						(m->bh - mc[i]->tagich) / 2 + offsety,
						mc[i]->tagicw, mc[i]->tagich, mc[i]->tagicon
					);
					#else // NO PATCH_FONT_GROUPS
					drw_rect(drw, x, 0, w, bh, 1, 1);
					drw_pic(
						drw, x + (
						#if PATCH_SHOW_MASTER_CLIENT_ON_TAG
						m->showmaster ? lrpad / 2 :
						#endif // PATCH_SHOW_MASTER_CLIENT_ON_TAG
						(w - mc[i]->tagicw) / 2),
						(bh - mc[i]->tagich) / 2 + offsety,
						mc[i]->tagicw, mc[i]->tagich, mc[i]->tagicon
					);
					#endif // PATCH_FONT_GROUPS
				}
				#endif // (PATCH_WINDOW_ICONS && PATCH_WINDOW_ICONS_ON_TAGS) || PATCH_CUSTOM_TAG_ICONS
				#if PATCH_CUSTOM_TAG_ICONS
				if (mc[i] == dummyc)
					dummyc->tagicon = None;
				#endif // PATCH_CUSTOM_TAG_ICONS

				#if PATCH_CLIENT_INDICATORS
				if (client_ind && (
						total[i]
						#if PATCH_FLAG_STICKY
						+ sticky[i]
						#endif // PATCH_FLAG_STICKY
					)
					#if 0// PATCH_SHOW_DESKTOP
					&& (!showdesktop || !m->showdesktop)
					#endif // PATCH_SHOW_DESKTOP
				) {
					int indn_gapi = (client_ind_size > 4 ? 4 : client_ind_size);	// inner gap between indicators;
					int indn_gapo = 2;												// outer gap around indicators;
					int indn_width = client_ind_size;
					if (client_ind_size > 28)
						indn_width = client_ind_size / 2;							// scale horizontal size by 1/2 if greater than 28;
					else if (client_ind_size >= 16)
						indn_width = (client_ind_size * 2 / 3);						// scale horizontal size by 2/3 if between 16 and 28;
					int indn_max = (w / (indn_width + indn_gapi));
					if (
						#if PATCH_FLAG_STICKY
						sticky[i] +
						#endif // PATCH_FLAG_STICKY
						total[i] < indn_max)
						indn_max = total[i]
							#if PATCH_FLAG_STICKY
							+ sticky[i]
							#endif // PATCH_FLAG_STICKY
						;

					drw_rect(drw,
						x - indn_gapo + (w - (indn_max * (indn_width + indn_gapi))) / 2,
						#if PATCH_FONT_GROUPS
						m->client_ind_top ? 0 : (m->bh - client_ind_size - 2),
						#else // NO PATCH_FONT_GROUPS
						m->client_ind_top ? 0 : (bh - client_ind_size - 2),
						#endif // PATCH_FONT_GROUPS
						(indn_max * (indn_width + indn_gapi)) + (2 * indn_gapo),
						client_ind_size + indn_gapo, 1, 1
					);

					for (int j = 0; j < indn_max; j++) {
						#if PATCH_FLAG_STICKY
						int iw = indn_width;
						int ih = client_ind_size;
						if (j >= total[i]) {
							if (indn_width >= 7) {
								iw = iw * 2 / 3;
								ih = ih * 2 / 3;
							}
							else if (indn_width >= 5) {
								iw -= 2;
								ih -= 2;
							}
							else
								ih /= 2;
							if (!iw) iw = 1;
							if (!ih) ih = 1;
						}
						#endif // PATCH_FLAG_STICKY
						if (indn_width >= 7)
							drw_ellipse(drw,
								#if PATCH_FLAG_STICKY
								(indn_width - iw) / 2 +
								#endif // PATCH_FLAG_STICKY
								x + (j * (indn_width + indn_gapi)) + (w - (indn_max * (indn_width + indn_gapi) - indn_gapi)) / 2,
								#if PATCH_FONT_GROUPS
								(m->client_ind_top ? 1 : (m->bh - client_ind_size - 1))
								#else // NO PATCH_FONT_GROUPS
								(m->client_ind_top ? 1 : (bh - client_ind_size - 1))
								#endif // PATCH_FONT_GROUPS
								#if PATCH_FLAG_STICKY
								+ (client_ind_size - ih) / 2, iw, ih, j < total[i] ? 1 : 0,
								#else // NO PATCH_FLAG_STICKY
								, indn_width, client_ind_size, 1,
								#endif // PATCH_FLAG_STICKY
								0
							);
						else
							drw_rect(drw,
								#if PATCH_FLAG_STICKY
								(indn_width - iw) / 2 +
								#endif // PATCH_FLAG_STICKY
								x + (j * (indn_width + indn_gapi)) + (w - (indn_max * (indn_width + indn_gapi) - indn_gapi)) / 2,
								#if PATCH_FONT_GROUPS
								(m->client_ind_top ? 1 : (m->bh - client_ind_size - 1))
								#else // NO PATCH_FONT_GROUPS
								(m->client_ind_top ? 1 : (bh - client_ind_size - 1))
								#endif // PATCH_FONT_GROUPS
								#if PATCH_FLAG_STICKY
								+ (client_ind_size - ih) / 2, iw, ih,
								#else // NO PATCH_FLAG_STICKY
								, indn_width, client_ind_size,
								#endif // PATCH_FLAG_STICKY
								1, 0
							);
					}
				}
				#endif // PATCH_CLIENT_INDICATORS
				x += w;
			}
			m->bar[TagBar].w = (x - m->bar[TagBar].x);
		}
		else
			m->bar[TagBar].x = -1;

		if (drawbar_elementvisible(m, LtSymbol)) {
			#if PATCH_FONT_GROUPS
			apply_barelement_fontgroup(m, LtSymbol);
			#endif // PATCH_FONT_GROUPS

			if (m->lt[m->sellt]->arrange == monocle)
			{
				#if PATCH_CLASS_STACKING
				for (c = nexttiledall(m->clients), a = 0, s = 0; c; c = nexttiledall(c->next), a++)
				#else // NO PATCH_CLASS_STACKING
				for (c = nexttiled(m->clients), a = 0, s = 0; c; c = nexttiled(c->next), a++)
				#endif // PATCH_CLASS_STACKING
					if (c == m->stack)
						s = a + 1;
				if (!s && a)
					s = 1;
				snprintf(m->ltsymbol, sizeof m->ltsymbol, "[M] [%d/%d]", s, a);
			}
			w =
				#if PATCH_SHOW_DESKTOP
				showdesktop && m->showdesktop ? TEXTW(desktopsymbol) :
				#endif // PATCH_SHOW_DESKTOP
				TEXTW(m->ltsymbol)
			;
			if (x > m->bar[StatusText].x - customwidth) {
				m->bar[LtSymbol].x = -1;
				m->bar[LtSymbol].w = 0;
			}
			else {
				if (x + w > m->bar[StatusText].x - customwidth)
					w = m->bar[StatusText].x - customwidth - x;
				m->bar[LtSymbol].x = x;
				m->bar[LtSymbol].w = w;
				drw_setscheme(drw, scheme[
					#if PATCH_COLOUR_BAR
					SchemeLayout
					#else // NO PATCH_COLOUR_BAR
					SchemeNorm
					#endif // PATCH_COLOUR_BAR
				]);
				x = drw_text(
						#if PATCH_FONT_GROUPS
						drw, x, 0, w, m->bh, lrpad / 2, 0,
						#else // NO PATCH_FONT_GROUPS
						drw, x, 0, w, bh, lrpad / 2, 0,
						#endif // PATCH_FONT_GROUPS
						#if PATCH_CLIENT_INDICATORS
						0,
						#endif // PATCH_CLIENT_INDICATORS
						1,
						#if PATCH_SHOW_DESKTOP
						showdesktop && m->showdesktop ? desktopsymbol :
						#endif // PATCH_SHOW_DESKTOP
						m->ltsymbol, 0
					);
			}
		}
	}
	m->bar[WinTitle].x = x =
		(m->bar[TagBar].x != -1 ? m->bar[TagBar].w : 0) +
		(m->bar[LtSymbol].x != -1 ? m->bar[LtSymbol].w : 0)
	;

	if (drawbar_elementvisible(m, WinTitle) && (w = m->bar[StatusText].x - x - customwidth) > 0) {

		#if PATCH_ALTTAB
		#define alttab_override (altTabMon && altTabMon->isAlt && !(altTabMon->isAlt & ALTTAB_SYSTEM_RESERVED))
		#endif // PATCH_ALTTAB
		Client *active = NULL;
		if ((m->sel && !m->sel->dormant
			#if PATCH_FLAG_PANEL
			&& !m->sel->ispanel
			#endif // PATCH_FLAG_PANEL
			#if PATCH_FLAG_IGNORED
			&& !m->sel->isignored
			#endif // PATCH_FLAG_IGNORED
		)
		#if PATCH_ALTTAB
		|| alttab_override
		#endif // PATCH_ALTTAB
		) {
			active =
				#if PATCH_ALTTAB
				alttab_override ? (altTabMon->highlight && altTabMon->highlight->mon == m ? altTabMon->highlight : NULL) :
				#endif // PATCH_ALTTAB
				m->sel
			;
		}
		#if PATCH_TWO_TONE_TITLE
		if ((
			#if PATCH_ALTTAB
			alttab_override && altTabMon->highlight && altTabMon->highlight->mon == m) || (!alttab_override &&
			#endif // PATCH_ALTTAB
			m == selmon
		)) {
			drw_setscheme(drw, scheme[
				#if PATCH_COLOUR_BAR
				SchemeTitleSel
				#else // NO PATCH_COLOUR_BAR
				SchemeSel
				#endif // PATCH_COLOUR_BAR
			]);
			drw->bg2 = 1;
			#if PATCH_FONT_GROUPS
			drw_gradient(drw, x, 0, w, m->bh, drw->scheme[ColBg].pixel, scheme[SchemeSel2][ColBg].pixel, !elementafter(m, WinTitle, TagBar));
			#else // NO PATCH_FONT_GROUPS
			drw_gradient(drw, x, 0, w, bh, drw->scheme[ColBg].pixel, scheme[SchemeSel2][ColBg].pixel, !elementafter(m, WinTitle, TagBar));
			#endif // PATCH_FONT_GROUPS
		}
		else {
			drw_setscheme(drw, scheme[
				#if PATCH_COLOUR_BAR
				SchemeTitle
				#else // NO PATCH_COLOUR_BAR
				SchemeNorm
				#endif // PATCH_COLOUR_BAR
			]);
			drw->bg2 = 0;
		}
		#else // NO PATCH_TWO_TONE_TITLE
		drw_setscheme(drw, scheme[(
			#if PATCH_ALTTAB
			alttab_override && altTabMon->highlight && altTabMon->highlight->mon == m) || (!alttab_override &&
			#endif // PATCH_ALTTAB
			m == selmon) ?
				#if PATCH_COLOUR_BAR
				SchemeTitleSel
				#else // NO PATCH_COLOUR_BAR
				SchemeSel
				#endif // PATCH_COLOUR_BAR
			:
			#if PATCH_COLOUR_BAR
			SchemeTitle
			#else // NO PATCH_COLOUR_BAR
			SchemeNorm
			#endif // PATCH_COLOUR_BAR
		]);
		if (!active)
			#if PATCH_FONT_GROUPS
			drw_rect(drw, x, 0, w, m->bh, 1, 1);
			#else // NO PATCH_FONT_GROUPS
			drw_rect(drw, x, 0, w, bh, 1, 1);
			#endif // PATCH_FONT_GROUPS
		#endif // PATCH_TWO_TONE_TITLE

		#if PATCH_FONT_GROUPS
		if (w > m->bh) {
		#else // NO PATCH_FONT_GROUPS
		if (w > bh) {
		#endif // PATCH_FONT_GROUPS

			#if PATCH_FONT_GROUPS
			apply_barelement_fontgroup(m, WinTitle);
			boxs = (drw->selfonts ? drw->selfonts : drw->fonts)->h;
			boxw = boxs / 6 + 2;
			boxs /= 9;
			#endif // PATCH_FONT_GROUPS

			m->bar[WinTitle].w = w;

			if (active) {
				int lpad = (2 * boxs + boxw);
				if (lpad < (lrpad / 2))
					lpad = (lrpad / 2);
				int pad = (
					lpad
					#if PATCH_WINDOW_ICONS
					+ (active->icon ? active->icw + iconspacing : 0)
					#endif // PATCH_WINDOW_ICONS
				);
				int rpad = lrpad / 2;
				unsigned int tw = 0;
				if (m->title_align) {
					tw =
						#if PATCH_FLAG_HIDDEN
						drw_fontset_getwidth(drw, (active->ishidden ? "window hidden" : active->name))
						#else // NO PATCH_FLAG_HIDDEN
						drw_fontset_getwidth(drw, active->name)
						#endif // PATCH_FLAG_HIDDEN
						+ lrpad / 2
						#if PATCH_WINDOW_ICONS
						+ (active->icon ? active->icw + iconspacing : 0)
						#endif // PATCH_WINDOW_ICONS
					;
					if ((tw + lpad) < w && m->title_align) {
						if (m->title_align == 1)
							pad = ((w - lpad - tw) / 2) + lpad
								#if PATCH_WINDOW_ICONS
								+ (active->icon ? active->icw + iconspacing : 0)
								#endif // PATCH_WINDOW_ICONS
							;
						else if (m->title_align == 2)
							pad = (w - tw);
					}
					else if (m->title_align == 2) {
						pad = lrpad / 2;
						rpad = lpad
							#if PATCH_WINDOW_ICONS
							+ (active->icon ? active->icw + iconspacing : 0)
							#endif // PATCH_WINDOW_ICONS
						;
					}
				}

				#if PATCH_BIDIRECTIONAL_TEXT
				apply_fribidi(
					#if PATCH_FLAG_HIDDEN
					active->ishidden ? "window hidden" :
					#endif // PATCH_FLAG_HIDDEN
					active->name
				);
				#endif // PATCH_BIDIRECTIONAL_TEXT
				#if PATCH_FONT_GROUPS
				drw_text(drw, x, 0, w, m->bh, pad, rpad,
				#else // NO PATCH_FONT_GROUPS
				drw_text(drw, x, 0, w, bh, pad, rpad,
				#endif // PATCH_FONT_GROUPS
					#if PATCH_CLIENT_INDICATORS
					0,
					#endif // PATCH_CLIENT_INDICATORS
					m->title_align,
					#if PATCH_BIDIRECTIONAL_TEXT
					fribidi_text,
					#else // NO PATCH_BIDIRECTIONAL_TEXT
					#if PATCH_FLAG_HIDDEN
					active->ishidden ? "window hidden" :
					#endif // PATCH_FLAG_HIDDEN
					active->name,
					#endif // PATCH_BIDIRECTIONAL_TEXT
					0
				);
				#if PATCH_TWO_TONE_TITLE
				drw->bg2 = 0;
				#endif // PATCH_TWO_TONE_TITLE
				#if PATCH_WINDOW_ICONS
				if (active->icon) {
					switch (m->title_align) {
						case 1:	// centre;
							#if PATCH_FONT_GROUPS
							drw_pic(drw, x + pad - active->icw - iconspacing, (m->bh - active->ich) / 2, active->icw, active->ich, active->icon);
							#else // NO PATCH_FONT_GROUPS
							drw_pic(drw, x + pad - active->icw - iconspacing, (bh - active->ich) / 2, active->icw, active->ich, active->icon);
							#endif // PATCH_FONT_GROUPS
							break;
						case 2:	// right;
							#if PATCH_FONT_GROUPS
							drw_pic(drw, x + w - active->icw - lpad, (m->bh - active->ich) / 2, active->icw, active->ich, active->icon);
							#else // NO PATCH_FONT_GROUPS
							drw_pic(drw, x + w - active->icw - lpad, (bh - active->ich) / 2, active->icw, active->ich, active->icon);
							#endif // PATCH_FONT_GROUPS
							break;
						default:
						case 0:	// left;
							#if PATCH_FONT_GROUPS
							drw_pic(drw, x + pad - active->icw - iconspacing, (m->bh - active->ich) / 2, active->icw, active->ich, active->icon);
							#else // NO PATCH_FONT_GROUPS
							drw_pic(drw, x + pad - active->icw - iconspacing, (bh - active->ich) / 2, active->icw, active->ich, active->icon);
							#endif // PATCH_FONT_GROUPS
					}
				}
				#endif // PATCH_WINDOW_ICONS
				if (active->isfloating) {
					drw_rect(drw, x + (m->title_align == 2 ? w - boxw - boxs : boxs), boxs, boxw, boxw, active->isfixed, 0);
					#if PATCH_MODAL_SUPPORT
					if (active->ismodal) {
						drw_setscheme(drw, scheme[SchemeUrg]);
						#if PATCH_FONT_GROUPS
						drw_rect(drw, x + (m->title_align == 2 ? w - boxw - boxs : boxs), (m->bh - boxw - boxs), boxw, boxw, 1,
						#else // NO PATCH_FONT_GROUPS
						drw_rect(drw, x + (m->title_align == 2 ? w - boxw - boxs : boxs), (bh - boxw - boxs), boxw, boxw, 1,
						#endif // PATCH_FONT_GROUPS
							#if PATCH_FLAG_ALWAYSONTOP
							!active->alwaysontop
							#else // NO PATCH_FLAG_ALWAYSONTOP
							0
							#endif // PATCH_FLAG_ALWAYSONTOP
						);
					}
					#endif // PATCH_MODAL_SUPPORT
				#if PATCH_FLAG_ALWAYSONTOP
					#if PATCH_MODAL_SUPPORT
					else
					#endif // PATCH_MODAL_SUPPORT
					if (active->alwaysontop)
						#if PATCH_FONT_GROUPS
						drw_rect(drw, x + (m->title_align == 2 ? w - boxw - boxs : boxs), (m->bh - boxw - boxs), boxw, boxw, 0, 0);
						#else // NO PATCH_FONT_GROUPS
						drw_rect(drw, x + (m->title_align == 2 ? w - boxw - boxs : boxs), (bh - boxw - boxs), boxw, boxw, 0, 0);
						#endif // PATCH_FONT_GROUPS
				}
				else if (active->alwaysontop) {
					#if PATCH_FONT_GROUPS
					drw_rect(drw, x + (m->title_align == 2 ? w - boxw - boxs : boxs), (m->bh - boxw - boxs), boxw, boxw, 0, 0);
					#else // NO PATCH_FONT_GROUPS
					drw_rect(drw, x + (m->title_align == 2 ? w - boxw - boxs : boxs), (bh - boxw - boxs), boxw, boxw, 0, 0);
					#endif // PATCH_FONT_GROUPS
				#endif // PATCH_FLAG_ALWAYSONTOP
				}
			} else {
				#if PATCH_TWO_TONE_TITLE
				drw->bg2 = 0;
				#endif // PATCH_TWO_TONE_TITLE
				drw_setscheme(drw, scheme[
					#if PATCH_COLOUR_BAR
					SchemeTitle
					#else // NO PATCH_COLOUR_BAR
					SchemeNorm
					#endif // PATCH_COLOUR_BAR
				]);
				if (m == selmon
					#if PATCH_ALTTAB
					&& !alttab_override
					#endif // PATCH_ALTTAB
				) {
					#if PATCH_FONT_GROUPS
					if (titleborderpx < m->bh)
						drw_rect(drw, x, (m->topbar ? 0 : titleborderpx), w, m->bh - titleborderpx, 1, 1);
						//drw_rect(drw, x+titleborderpx, titleborderpx, w-2*titleborderpx, m->bh-2*titleborderpx, 1, 1);
					#else // NO PATCH_FONT_GROUPS
					if (titleborderpx < bh)
						drw_rect(drw, x, (m->topbar ? 0 : titleborderpx), w, bh - titleborderpx, 1, 1);
						//drw_rect(drw, x+titleborderpx, titleborderpx, w-2*titleborderpx, bh-2*titleborderpx, 1, 1);
					#endif // PATCH_FONT_GROUPS
				}
				else
					#if PATCH_FONT_GROUPS
					drw_rect(drw, x, 0, w, m->bh, 1, 1);
					#else // NO PATCH_FONT_GROUPS
					drw_rect(drw, x, 0, w, bh, 1, 1);
					#endif // PATCH_FONT_GROUPS
			}
		}
		else {
			drw_text(
				#if PATCH_FONT_GROUPS
				drw, x, 0, w, m->bh, 0, 0,
				#else // NO PATCH_FONT_GROUPS
				drw, x, 0, w, bh, 0, 0,
				#endif // PATCH_FONT_GROUPS
				#if PATCH_CLIENT_INDICATORS
				0,
				#endif // PATCH_CLIENT_INDICATORS
				1, "", 0
			);
			#if PATCH_SYSTRAY
			w -= m->stw;
			#endif // PATCH_SYSTRAY
			m->bar[WinTitle].w = w;
			#if PATCH_TWO_TONE_TITLE
			drw->bg2 = 0;
			#endif // PATCH_TWO_TONE_TITLE
		}
	}
	else {
		m->bar[WinTitle].x = -1;
		m->bar[WinTitle].w = 0;
	}
	#if PATCH_SYSTRAY
	if (showsystray && m->stw && systrayonleft) {
		drw_setscheme(drw, scheme[SchemeNorm]);
		#if PATCH_FONT_GROUPS
		drw_rect(drw, m->bar[StatusText].x, 0, m->stw, m->bh, 1, 1);
		#else // NO PATCH_FONT_GROUPS
		drw_rect(drw, m->bar[StatusText].x, 0, m->stw, bh, 1, 1);
		#endif // PATCH_FONT_GROUPS
	}
	#endif // PATCH_SYSTRAY
	#if PATCH_FLAG_PANEL
	if (pw) {
		drw_setscheme(drw, scheme[SchemeNorm]);
		#if PATCH_FONT_GROUPS
		drw_rect(drw, m->mw - pw, 0, pw, m->bh, 1, 1);
		#else // NO PATCH_FONT_GROUPS
		drw_rect(drw, m->mw - pw, 0, pw, bh, 1, 1);
		#endif // PATCH_FONT_GROUPS
	}
	#endif // PATCH_FLAG_PANEL

	resizebarwin(m);
	showhidebar(m);

	unsigned int j = 0;
	#if PATCH_FLAG_PANEL
	x = px;
	if (x) {
		j = m->barlayout[0];
		#if PATCH_FONT_GROUPS
		drw_map(drw, m->barwin, 0, 0, m->bar[j].x, m->bh);
		#else // NO PATCH_FONT_GROUPS
		drw_map(drw, m->barwin, 0, 0, m->bar[j].x, bh);
		#endif // PATCH_FONT_GROUPS
	}
	#else // NO PATCH_FLAG_PANEL
	x = 0;
	#endif // PATCH_FLAG_PANEL

	for (i = 0; i < LENGTH(m->barlayout); i++) {
		if (m->barlayout[i] == NoElement)
			continue;
		j = m->barlayout[i];
		if (skiptags && (j == TagBar || j == LtSymbol)) {
			m->bar[j].x = x;
			x += m->bar[j].w;
			continue;
		}
		if (x >= m->mw)
			m->bar[j].w = 0;
		else if (m->bar[j].x != -1 && m->bar[j].w) {
			drw_maptrans(
				drw, m->barwin, m->bar[j].x, 0,
				(x + m->bar[j].w > m->mw) ? m->mw - x : m->bar[j].w,
				#if PATCH_FONT_GROUPS
				m->bh, x, 0
				#else // NO PATCH_FONT_GROUPS
				bh, x, 0
				#endif // PATCH_FONT_GROUPS
			);
			m->bar[j].x = x;
			x += m->bar[j].w;
		}
	}
	if (x < m->mw) {
		#if PATCH_FONT_GROUPS
		drw_maptrans(drw, m->barwin, m->bar[j].x + m->bar[j].w, 0, m->mw - (m->bar[j].x + m->bar[j].w), m->bh, x, 0);
		#else // NO PATCH_FONT_GROUPS
		drw_maptrans(drw, m->barwin, m->bar[j].x + m->bar[j].w, 0, m->mw - (m->bar[j].x + m->bar[j].w), bh, x, 0);
		#endif // PATCH_FONT_GROUPS
	}

	//drw_maptrans(drw, m->barwin, 0, 0, m->mw, bh, 0, bh);	// for debugging;
	#if PATCH_FONT_GROUPS
	drw->selfonts = NULL;
	#endif // PATCH_FONT_GROUPS

	#if PATCH_TORCH
	if (torchwin) XRaiseWindow(dpy, torchwin);
	#endif // PATCH_TORCH
}

int
drawbar_elementvisible(Monitor *m, unsigned int element_type)
{
	for (int i = 0; i < LENGTH(m->barlayout); i++)
		if (m->barlayout[i] == element_type)
			return 1;
	return 0;
}

void
drawbars(void)
{
	if (running != 1
		#if PATCH_TORCH
		|| torchwin
		#endif // PATCH_TORCH
	)
		return;

	Monitor *m;

	for (m = mons; m; m = m->next)
		drawbar(m, 0);

	#if 0 //PATCH_SYSTRAY
	if (showsystray && systraypinning == -1)
		updatesystray(1);
	#endif // PATCH_SYSTRAY
}

#if PATCH_FOCUS_BORDER || PATCH_FOCUS_PIXEL
void drawfocusborder(int remove)
{
	XWindowChanges wc;
	Client *c = NULL;
	if (!focuswin)
		return;
	if (!selmon || !(c = selmon->sel)
		#if PATCH_FOCUS_PIXEL || PATCH_FOCUS_BORDER
		|| remove
		#endif // PATCH_FOCUS_PIXEL || PATCH_FOCUS_BORDER
		#if PATCH_FLAG_PANEL
		|| c->ispanel
		#endif // PATCH_FLAG_PANEL
		#if PATCH_SHOW_DESKTOP
		|| c->isdesktop
		#endif // PATCH_SHOW_DESKTOP
		|| (c->isfullscreen
			#if PATCH_FLAG_FAKEFULLSCREEN
			&& c->fakefullscreen != 1
			#endif // PATCH_FLAG_FAKEFULLSCREEN
		)
	) {
		#if PATCH_FOCUS_BORDER
		if (c) {
			#if PATCH_SHOW_DESKTOP
			if (desktopvalid(c))
			#endif // PATCH_SHOW_DESKTOP
			XMoveResizeWindow(dpy, c->win, c->x, c->y, c->w, c->h);
		}
		#elif PATCH_FOCUS_PIXEL
		fpcurpos = 0;
		#endif // PATCH_FOCUS_BORDER || PATCH_FOCUS_PIXEL
		XMoveResizeWindow(dpy, focuswin, 0, -fh - 1, fh, fh);
		return;
	}
	XWindowAttributes wa;
	if (!XGetWindowAttributes(dpy, c->win, &wa))
		return;
	#if PATCH_FOCUS_BORDER
	if (wa.border_width) {
		unsigned int size = 0;
		switch (fbpos) {
			case FOCUS_BORDER_E:
				if (!c->isfloating || c->x + WIDTH(c) < c->mon->wx + c->mon->ww) {
					size = c->isfloating ? MIN(fh, MAX(c->mon->wx + c->mon->ww - WIDTH(c) - c->x, 1)) : fh;
					XMoveResizeWindow(dpy, focuswin, c->x + WIDTH(c) - (!c->isfloating ? size : 0), c->y, size, HEIGHT(c));
					if (!c->isfloating)
						XMoveResizeWindow(dpy, c->win, c->x, c->y, c->w - fh, c->h);
				}
				break;
			case FOCUS_BORDER_S:
				if (!c->isfloating || c->y + HEIGHT(c) < c->mon->wy + c->mon->wh) {
					size = c->isfloating ? MIN(fh, MAX(c->mon->wy + c->mon->wh - HEIGHT(c) - c->y, 1)) : fh;
					XMoveResizeWindow(dpy, focuswin, c->x, c->y + HEIGHT(c) - (!c->isfloating ? size : 0), WIDTH(c), size);
					if (!c->isfloating)
						XMoveResizeWindow(dpy, c->win, c->x, c->y, c->w, c->h - fh);
				}
				break;
			case FOCUS_BORDER_W:
				if (!c->isfloating || c->x > c->mon->wx) {
					size = c->isfloating ? MIN(fh, MAX(c->mon->wx + c->mon->ww - WIDTH(c) - c->x, 1)) : fh;
					XMoveResizeWindow(dpy, focuswin, c->x - (c->isfloating ? size : 0), c->y, size, HEIGHT(c));
					if (!c->isfloating)
						XMoveResizeWindow(dpy, c->win, c->x + fh, c->y, c->w - fh, c->h);
				}
				break;
			default:
			case FOCUS_BORDER_N:
				if (!c->isfloating || c->y > c->mon->wy) {
					size = c->isfloating ? MIN(fh, MAX(c->mon->wy + c->mon->wh - HEIGHT(c) - c->y, 1)) : fh;
					XMoveResizeWindow(dpy, focuswin, c->x, c->y - (c->isfloating ? size : 0), WIDTH(c), size);
					if (!c->isfloating)
						XMoveResizeWindow(dpy, c->win, c->x, c->y + fh, c->w, c->h - fh);
				}
		}
		if (size) {
			wc.stack_mode = Above;
			wc.sibling = c->win;
			XConfigureWindow(dpy, focuswin, CWSibling|CWStackMode, &wc);
		}
		else
			XMoveResizeWindow(dpy, focuswin, 0, -fh - 1, fh, fh);
	}
	else
		XMoveResizeWindow(dpy, focuswin, 0, -fh - 1, fh, fh);
	#elif PATCH_FOCUS_PIXEL
	int fhadj = fh;
	if (c->w >= c->h && fhadj >= (c->h / 2))
		fhadj = (c->h / 2) - 1;
	else if (c->h > c->w && fhadj >= (c->w / 2))
		fhadj = (c->w / 2) - 1;
	if (fhadj < 1)
		fhadj = 1;
	if (!fpcurpos)
		fpcurpos = fppos;
	switch (fpcurpos) {
		case FOCUS_PIXEL_SW:
			XMoveResizeWindow(dpy, focuswin,
				c->x + wa.border_width,
				c->y + c->h + wa.border_width - fhadj - 2,
				fhadj, fhadj
			);
			break;
		case FOCUS_PIXEL_NW:
			XMoveResizeWindow(dpy, focuswin,
				c->x + wa.border_width,
				c->y + wa.border_width,
				fhadj, fhadj
			);
			break;
		case FOCUS_PIXEL_NE:
			XMoveResizeWindow(dpy, focuswin,
				c->x + c->w + wa.border_width - fhadj - 2,
				c->y + wa.border_width,
				fhadj, fhadj
			);
			break;
		default:
		case FOCUS_PIXEL_SE:
			XMoveResizeWindow(dpy, focuswin,
				c->x + c->w + wa.border_width - fhadj - 2,
				c->y + c->h + wa.border_width - fhadj - 2,
				fhadj, fhadj
			);
	}
	wc.stack_mode = Above;
	wc.sibling = c->win;
	XConfigureWindow(dpy, focuswin, CWSibling|CWStackMode, &wc);
	#endif // PATCH_FOCUS_BORDER || PATCH_FOCUS_PIXEL
}
#endif // PATCH_FOCUS_BORDER || PATCH_FOCUS_PIXEL

#if PATCH_IPC
#if PATCH_MOUSE_POINTER_WARPING
void
enablemousewarp(const Arg *arg)
{
	mousewarp_disable = (arg->ui == 0 ? True : False);
}
#endif // PATCH_MOUSE_POINTER_WARPING
#if PATCH_TERMINAL_SWALLOWING
void
enabletermswallow(const Arg *arg)
{
	terminal_swallowing = (arg->ui == 1 ? True : False);
}
#endif // PATCH_TERMINAL_SWALLOWING

void
enableurgency(const Arg *arg)
{
	if (!(urgency = (arg->ui == 1 ? True : False)))
		clearurgency(0);
}
#endif // PATCH_IPC

void
enternotify(XEvent *e)
{
	#if PATCH_ALTTAB
	if (altTabMon && altTabMon->isAlt)
		return;
	#endif // PATCH_ALTTAB

	XEvent xev;
	XCrossingEvent *ev = &e->xcrossing;

	if ((ev->mode != NotifyNormal || ev->detail == NotifyInferior) && ev->window != root)
		return;
	if (ev->window == root)
		return;

	#if PATCH_FOCUS_FOLLOWS_MOUSE
	Monitor *selm = selmon;
	Client *sel = selmon->sel;
	Client *c = wintoclient(ev->window);
	#if PATCH_CROP_WINDOWS
	if (!c)
		c = cropwintoclient(ev->window);
	#endif // PATCH_CROP_WINDOWS

	if (nonstop || (c && c == sel)) {
		while (XCheckMaskEvent(dpy, EnterWindowMask, &xev));
		return;
	}
	else if ((c && c->neverfocus)
		#if PATCH_FLAG_GREEDY_FOCUS
		|| (sel && sel->isgreedy)
		#endif // PATCH_FLAG_GREEDY_FOCUS
	) {
		if (ev->window != root)
			while (XCheckMaskEvent(dpy, EnterWindowMask, &xev));
		#if 0 //PATCH_FLAG_GAME && PATCH_FLAG_GAME_STRICT
		if (sel->isgame && sel != game)
		#endif // PATCH_FLAG_GAME && PATCH_FLAG_GAME_STRICT
		focus(sel, 1);
		return;
	}

	Monitor *m = c ? c->mon : wintomon(ev->window);
	if (m && m != selmon) {
		focusmonex(m);
		if (!c)
			focus(NULL, 0);
	}

	if (c && !c->dormant
		&& (!c->lostfullscreen || solitary(c))
		#if PATCH_FLAG_PANEL
		&& !c->ispanel
		#endif // PATCH_FLAG_PANEL
		#if PATCH_FLAG_IGNORED
		&& !c->isignored
		#endif // PATCH_FLAG_IGNORED
		) {
		if (c != sel)
			focus(c, 0);
		else if (selm != m)
			drawbar(m, 1);
	}
	else if (c && !c->dormant
		&& (!c->lostfullscreen || solitary(c) || c->mon != selm)
		#if PATCH_FLAG_IGNORED
		&& !c->isignored
		#endif // PATCH_FLAG_IGNORED
		)
		focus(NULL, 0);
	else if (c && selm != m)
		drawbar(m, 1);
	/*
	#else // NO PATCH_FOCUS_FOLLOWS_MOUSE
	#if PATCH_FLAG_GAME
	if (!wintoclient(ev->window)) {
		Monitor *m = wintomon(ev->window);
		if (m && m->sel && m->sel->isgame && m->sel->isfullscreen && MINIMIZED(m->sel)) {
			if (m != selmon)
				focusmonex(m);
			focus(m->sel, 0);
		}
	}
	#endif // PATCH_FLAG_GAME
	*/
	#endif // PATCH_FOCUS_FOLLOWS_MOUSE
	while (XCheckMaskEvent(dpy, EnterWindowMask, &xev));
}

void
expose(XEvent *e)
{
	#if PATCH_TORCH
	if (torchwin)
		return;
	#endif // PATCH_TORCH

	Monitor *m;
	XExposeEvent *ev = &e->xexpose;
	#if PATCH_FLAG_PANEL
	int a, x, y, w, h;
	Client *c;
	#endif // PATCH_FLAG_PANEL

	if (ev->count == 0 && (m = wintomon(ev->window))) {

		// check if the bar window is exposed, and if the area is covered by
		#if PATCH_FLAG_PANEL
		// a panel window or
		#endif // PATCH_FLAG_PANEL
		// a tiled window (which should not happen);
		// if so, ignore the event, no need to redraw
		#if PATCH_FLAG_PANEL
		// as the drawbar() function will reraise
		// the panel window causing a feedback loop and chewing up cpu unnecessarily.
		#endif // PATCH_FLAG_PANEL
		if (ev->window == m->barwin) {

			// ignore if there is a fullscreen window covering the bar;
			for (c = m->clients; c; c = c->next)
				if (ISVISIBLE(c) && c->isfullscreen
					#if PATCH_FLAG_FAKEFULLSCREEN
					&& c->fakefullscreen != 1
					#endif // PATCH_FLAG_FAKEFULLSCREEN
					)
					return;

			x = ev->x;
			y = ev->y;
			w = ev->width;
			h = ev->height;
			for (c = m->clients; c; c = c->next) {
				if (c
				&& (!c->isfloating
					#if PATCH_FLAG_PANEL
					|| c->ispanel
					#endif // PATCH_FLAG_PANEL
				) && !MINIMIZED(c))
					if ((a = INTERSECTC(x, y, w, h, c))) {
						//logdatetime(stderr);
						//fprintf(stderr, "debug: expose x:%i y:%i w:%i h:%i intersects:\"%s\"\n", x,y,w,h, c->name);
						if (c->ispanel)
							raisewin(m, c->win, 1);
						return;
					}
			}
			// if not caused by the panel window, redraw the bar as normal
			// (which will cause a panel-related expose and drop out above)
			drawbar(m, 0);
			#if PATCH_SYSTRAY
			if (showsystray && m == systraytomon(m))
				updatesystray(0);
			#endif // PATCH_SYSTRAY
		}
	}
}

#if PATCH_IPC
void
flush_socket_reply(void)
{
	IPCMessageType reply_type;
	uint32_t reply_size;
	char *reply;

	read_socket(&reply_type, &reply_size, &reply);

	if (reply)
		free(reply);
}
#endif // PATCH_IPC

void
focus(Client *c, int force)
{
	#if PATCH_ALTTAB
	if (altTabMon)
		return;
	#endif // PATCH_ALTTAB

	Client *sel = selmon->sel;

	#if PATCH_MOUSE_POINTER_HIDING
	showcursor();
	#endif // PATCH_MOUSE_POINTER_HIDING

	c = getfocusable(c ? c->mon : selmon, c, force);

	#if PATCH_MODAL_SUPPORT
	// If this client has a modal child higher up in the stack, then focus it instead
	if (c
		#if PATCH_ALTTAB
		&& !altTabMon
		#endif // PATCH_ALTTAB
	) {
		for (Client *s = c->mon->stack; s; s = s->snext)
//			if (s->ultparent == c->ultparent && s->ismodal && s->index > c->index) {
			if (s->ismodal &&
				((s->ultparent == c->ultparent && s->index > c->index)
				|| (s->ultparent == s && s->parent == c))
			) {
				if (!ISVISIBLE(s)
					#if PATCH_FLAG_HIDDEN
					|| s->ishidden
					#endif // PATCH_FLAG_HIDDEN
					)
					break;
				DEBUG("focus(c) modal: %s\n", s->name);
				focus(s, 0);
				return;
			}

		// if there are gaps in the stack between modal client and its parent(s),
		// then group the clients together;
		if (c->ismodal)
			modalgroupclients(c);
	}
	#endif // PATCH_MODAL_SUPPORT

	if (sel && sel != c
		#if PATCH_ALTTAB
		&& !altTabMon
		#endif // PATCH_ALTTAB
	) {
		if (c && sel && ISVISIBLE(sel) && selmon == c->mon)
			losefullscreen(sel, NULL);
		#if PATCH_FLAG_GAME && PATCH_FLAG_GAME_STRICT
		unfocus(sel, 0 | (c && selmon != c->mon ? (1 << 1) : 0));
		#else
		unfocus(sel, 0);
		#endif // PATCH_FLAG_GAME && PATCH_FLAG_GAME_STRICT
		if (c && selmon != c->mon)
			drawbar(selmon, 0);
	}

	if (c) {

		#if PATCH_CLASS_STACKING
		if (c->stackhead)
		{
			for (Client *cc = c->mon->clients; cc; cc = cc->next)
				if (cc->stackhead == c->stackhead && cc != c) {
					cc->stackhead = c;
					cc->isstackhead = 0;
				}
			c->stackhead->stackhead = c;
			c->stackhead = NULL;
			c->isstackhead = 1;
		}
		#endif // PATCH_CLASS_STACKING

		if (c->mon != selmon) {
			selmon = c->mon;
			c->prevsel = NULL;
		}
		else if (c->prevsel != sel && c != sel)
			c->prevsel = sel;

		if (c->isurgent
			#if PATCH_ALTTAB
			&& !altTabMon
			#endif // PATCH_ALTTAB
			)
			seturgent(c, 0);

		#if PATCH_SHOW_DESKTOP
		if (showdesktop && c->mon->showdesktop != (c->isdesktop || c->ondesktop)
			#if PATCH_SHOW_DESKTOP_WITH_FLOATING
			&& (!showdesktop_floating || !c->isfloating || c->isdesktop || c->ondesktop)
			#endif // PATCH_SHOW_DESKTOP_WITH_FLOATING
		) {
			c->mon->showdesktop = (c->isdesktop || c->ondesktop);
			arrange(c->mon);
		}
		#endif // PATCH_SHOW_DESKTOP

		#if PATCH_ALTTAB
		if (!altTabMon)
		#endif // PATCH_ALTTAB
		{
			detachstackex(c);
			attachstackex(c);
			grabbuttons(c, 1);
		}
		/* Avoid flickering when another client appears and the border
		* is restored */
		//if (!solitary(c))
		#if PATCH_CLASS_STACKING
		if (c->isstackhead)
			XSetWindowBorder(dpy, c->win, scheme[SchemeUrg][ColBorder].pixel);
		else
		#endif // PATCH_CLASS_STACKING
		XSetWindowBorder(dpy, c->win, scheme[SchemeSel][ColBorder].pixel);

		#if PATCH_ALTTAB
		if (altTabMon)
			return;
		#endif // PATCH_ALTTAB
		if(c->lostfullscreen)
		{
			c->lostfullscreen = 0;
			#if PATCH_FLAG_FAKEFULLSCREEN
			if(c->fakefullscreen != 1)
			#endif // PATCH_FLAG_FAKEFULLSCREEN
			{
				setfullscreen(c, 1);
			}
		}
		setfocus(c);
		#if PATCH_CLIENT_OPACITY
		opacity(c, 1);
		#endif // PATCH_CLIENT_OPACITY
	} else {
		#if PATCH_SHOW_DESKTOP
		#if PATCH_SHOW_DESKTOP_ONLY_WHEN_ACTIVE
		if (showdesktop && showdesktop_when_active && !selmon->showdesktop) {
			int nondesktop = 0;
			if (getdesktopclient(selmon, &nondesktop)) {
				toggledesktop(0);
				return;
			}
		}
		#endif // PATCH_SHOW_DESKTOP_ONLY_WHEN_ACTIVE
		#endif // PATCH_SHOW_DESKTOP
		XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
		XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
	}
	#if PATCH_ALTTAB
	if (!altTabMon)
	#endif // PATCH_ALTTAB
	{
		/*
		#if PATCH_SHOW_DESKTOP
		if (!showdesktop || !selmon->showdesktop)
		#endif // PATCH_SHOW_DESKTOP
		{
			int i = tagtoindex(selmon->tagset[selmon->seltags]);
			if (i)
				selmon->focusontag[i - 1] = c;
		}
		*/
		selmon->sel = c;
		#if PATCH_FOCUS_BORDER || PATCH_FOCUS_PIXEL
		if (focuswin)
			drawfocusborder(0);
		#endif // PATCH_FOCUS_BORDER || PATCH_FOCUS_PIXEL
	}

	restack(selmon);
}

/* there are some broken focus acquiring clients needing extra handling */
void
focusin(XEvent *e)
{
	XFocusChangeEvent *ev = &e->xfocus;

	#if PATCH_ALTTAB
	if (altTabMon && altTabMon->isAlt)
		return;
	#endif // PATCH_ALTTAB

	#if PATCH_FLAG_GAME
	#if 0 //PATCH_FLAG_GAME_STRICT
	Client *c = wintoclient(ev->window);
	if (c->isgame && !c->isgamestrict)
	if (selmon->sel && selmon->sel->isgame && !selmon->sel->isgamestrict)
		return;
	#endif // PATCH_FLAG_GAME_STRICT
	#endif // PATCH_FLAG_GAME

	#if PATCH_FOCUS_FOLLOWS_MOUSE
	#if PATCH_SHOW_DESKTOP
	#if PATCH_SHOW_DESKTOP_UNMANAGED
	if (showdesktop && showdesktop_unmanaged && desktopwin == ev->window) {
		Monitor *m;
		int x, y;
		if (!getrootptr(&x, &y))
			return;
		if ((m = recttomon(x, y, 1, 1)) != selmon && selmon) {
			#if PATCH_FLAG_GAME && PATCH_FLAG_GAME_STRICT
			unfocus(selmon->sel, 1 | (1 << 1));
			#else
			unfocus(selmon->sel, 1);
			#endif // PATCH_FLAG_GAME && PATCH_FLAG_GAME_STRICT
			selmon = m;
			focus(NULL, 0);
		}
	}
	#endif // PATCH_SHOW_DESKTOP_UNMANAGED
	#endif // PATCH_SHOW_DESKTOP
	#endif // PATCH_FOCUS_FOLLOWS_MOUSE

	if (selmon->sel && ev->window != selmon->sel->win)
		setfocus(selmon->sel);
}

void
focusmonex(Monitor *m)
{
	Monitor *s = selmon;
	Client *c;

	#if PATCH_CONSTRAIN_MOUSE && PATCH_FOCUS_FOLLOWS_MOUSE
	if (constrained)
		return;
	#endif // PATCH_CONSTRAIN_MOUSE && PATCH_FOCUS_FOLLOWS_MOUSE

	#if PATCH_ALT_TAGS
	if (s->alttags) {
		s->alttags = 0;
		m->alttags = 1;
	}
	#endif // PATCH_ALT_TAGS
	if ((c = s->sel)) {
		#if PATCH_FLAG_GAME && PATCH_FLAG_GAME_STRICT
		unfocus(c, (s != m ? (1 << 1) : 0));
		#else
		unfocus(c, 0);
		#endif // PATCH_FLAG_GAME && PATCH_FLAG_GAME_STRICT
	}
	selmon = m;
	#if PATCH_CLIENT_OPACITY
	if (c)
		opacity(c, 1);
	#endif // PATCH_CLIENT_OPACITY
	#if PATCH_FOCUS_BORDER || PATCH_FOCUS_PIXEL
	drawfocusborder(m->sel && ISVISIBLE(m->sel) ? 0 : 1);
	#endif // PATCH_FOCUS_BORDER || PATCH_FOCUS_PIXEL
	restack(s);
//	drawbar(m, 0);
}

void
focusmon(const Arg *arg)
{
	Monitor *m;

	if (!mons->next)
		return;
	if ((m = dirtomon(arg->i)) == selmon)
		return;

	#if PATCH_CONSTRAIN_MOUSE && PATCH_FOCUS_FOLLOWS_MOUSE
	if (constrained == selmon)
		return;
	#endif // PATCH_CONSTRAIN_MOUSE && PATCH_FOCUS_FOLLOWS_MOUSE

	#if PATCH_MOUSE_POINTER_WARPING
	#if PATCH_MOUSE_POINTER_WARPING_RECALL
	if (selmon->sel)
		lastcoordsstore(selmon->sel);
	#endif // PATCH_MOUSE_POINTER_WARPING_RECALL
	#endif // PATCH_MOUSE_POINTER_WARPING

	focusmonex(m);

	focus(m->sel, 0);

	#if PATCH_FOCUS_FOLLOWS_MOUSE
	if (!m->sel)
		XWarpPointer(dpy, None, root, 0, 0, 0, 0, m->wx + (m->ww / 2), m->wy + (m->wh / 2));
	#if !PATCH_MOUSE_POINTER_WARPING
	else if (
		m->sel->isfullscreen
		#if PATCH_FLAG_FAKEFULLSCREEN
		&& m->sel->fakefullscreen != 1
		#endif // PATCH_FLAG_FAKEFULLSCREEN
		)
		XWarpPointer(dpy, None, root, 0, 0, 0, 0, m->mx + (m->mw / 2), m->my + (m->mh / 2));
	else
		XWarpPointer(dpy, None, root, 0, 0, 0, 0, m->sel->x + (m->sel->w / 2), m->sel->y + (m->sel->h / 2));
	#endif // !PATCH_MOUSE_POINTER_WARPING
	#endif // PATCH_FOCUS_FOLLOWS_MOUSE

	#if PATCH_MOUSE_POINTER_WARPING
	#if PATCH_MOUSE_POINTER_WARPING_SMOOTH
	warptoclient(selmon->sel, 0, 1);
	#else // NO PATCH_MOUSE_POINTER_WARPING_SMOOTH
	warptoclient(selmon->sel, 1);
	#endif // PATCH_MOUSE_POINTER_WARPING_SMOOTH
	#endif // PATCH_MOUSE_POINTER_WARPING
}

void
focusstack(const Arg *arg)
{
	#if PATCH_FOCUS_FOLLOWS_MOUSE
	checkmouseovermonitor(selmon);
	#endif // PATCH_FOCUS_FOLLOWS_MOUSE

	Client *c = NULL, *i, *s;
	Monitor *m = selmon;
	XEvent xev;
	#if PATCH_MOUSE_POINTER_WARPING || PATCH_FOCUS_FOLLOWS_MOUSE
	int warp = 1;
	#endif // PATCH_MOUSE_POINTER_WARPING || PATCH_FOCUS_FOLLOWS_MOUSE

	if (!(s = m->sel) && !(s = m->stack))
		return;

	if (arg->i > 0) {
		for (c = s->next; c && (
			!ISVISIBLE(c)
			#if PATCH_FLAG_HIDDEN
			|| c->ishidden
			#endif // PATCH_FLAG_HIDDEN
			#if PATCH_FLAG_PANEL
			|| c->ispanel
			#endif // PATCH_FLAG_PANEL
			#if PATCH_FLAG_IGNORED
			|| c->isignored
			#endif // PATCH_FLAG_IGNORED
			|| c->neverfocus
			#if PATCH_MODAL_SUPPORT
			|| ismodalparent(c)
			#endif // PATCH_MODAL_SUPPORT
			); c = c->next);
		if (!c)
			for (c = m->clients; c && (
				!ISVISIBLE(c)
				#if PATCH_FLAG_HIDDEN
				|| c->ishidden
				#endif // PATCH_FLAG_HIDDEN
				#if PATCH_FLAG_PANEL
				|| c->ispanel
				#endif // PATCH_FLAG_PANEL
				#if PATCH_FLAG_IGNORED
				|| c->isignored
				#endif // PATCH_FLAG_IGNORED
				|| c->neverfocus
				#if PATCH_MODAL_SUPPORT
				|| ismodalparent(c)
				#endif // PATCH_MODAL_SUPPORT
				); c = c->next);
		#if PATCH_MOUSE_POINTER_WARPING || PATCH_FOCUS_FOLLOWS_MOUSE
		if (arg->i > 1)
			warp = 0;
		#endif // PATCH_MOUSE_POINTER_WARPING || PATCH_FOCUS_FOLLOWS_MOUSE
	} else {
		for (i = m->clients; i != s; i = i->next)
			if (
				ISVISIBLE(i)
				#if PATCH_FLAG_HIDDEN
				&& !i->ishidden
				#endif // PATCH_FLAG_HIDDEN
				#if PATCH_FLAG_PANEL
				&& !i->ispanel
				#endif // PATCH_FLAG_PANEL
				#if PATCH_FLAG_IGNORED
				&& !i->isignored
				#endif // PATCH_FLAG_IGNORED
				&& !i->neverfocus
				#if PATCH_MODAL_SUPPORT
				&& !ismodalparent(i)
				#endif // PATCH_MODAL_SUPPORT
				)
				c = i;
		if (!c)
			for (; i; i = i->next)
				if (
					ISVISIBLE(i)
					#if PATCH_FLAG_HIDDEN
					&& !i->ishidden
					#endif // PATCH_FLAG_HIDDEN
					#if PATCH_FLAG_PANEL
					&& !i->ispanel
					#endif // PATCH_FLAG_PANEL
					#if PATCH_FLAG_IGNORED
					&& !i->isignored
					#endif // PATCH_FLAG_IGNORED
					&& !i->neverfocus
					#if PATCH_MODAL_SUPPORT
					&& !ismodalparent(i)
					#endif // PATCH_MODAL_SUPPORT
					)
					c = i;
		#if PATCH_MOUSE_POINTER_WARPING || PATCH_FOCUS_FOLLOWS_MOUSE
		if (arg->i < -1)
			warp = 0;
		#endif // PATCH_MOUSE_POINTER_WARPING || PATCH_FOCUS_FOLLOWS_MOUSE
	}

	if (c) {
		if (c->dormant)
			restack(m);
		else {

			if (s->isfullscreen
				#if PATCH_FLAG_FAKEFULLSCREEN
				&& s->fakefullscreen != 1
				#endif // PATCH_FLAG_FAKEFULLSCREEN
			) {
				#if PATCH_FLAG_GAME
				if (s->isgame) {
					unfocus(s, 1);
				}
				else
				#endif // PATCH_FLAG_GAME
				if (c == s
					#if PATCH_FLAG_ALWAYSONTOP
					|| (s->alwaysontop && !c->alwaysontop)
					#endif // PATCH_FLAG_ALWAYSONTOP
					)
					return;
				else {
					s->lostfullscreen = 1;
					setfullscreen(s, 0);
				}
			}

			#if PATCH_FLAG_PAUSE_ON_INVISIBLE
			if (s->pauseinvisible && s->pid) {
				if (!ISVISIBLE(s) || s->mon->lt[s->mon->sellt]->arrange == monocle ||
					#if PATCH_FLAG_HIDDEN
					s->ishidden ||
					#endif // PATCH_FLAG_HIDDEN
					(c->isfullscreen
						#if PATCH_FLAG_FAKEFULLSCREEN
						&& c->fakefullscreen != 1
						#endif // PATCH_FLAG_FAKEFULLSCREEN
					)
				) {
					if (s->pauseinvisible == 1
						#if PATCH_HANDLE_SIGNALS
						&& !closing
						#endif // PATCH_HANDLE_SIGNALS
					) {
						kill (s->pid, SIGSTOP);
						s->pauseinvisible = -1;
						#if PATCH_PAUSE_PROCESS
						s->paused = 1;
						#endif // PATCH_PAUSE_PROCESS
						DEBUG("client stopped: \"%s\".\n", s->name);
					}
				}
				else if (s->pauseinvisible == -1) {
					kill (s->pid, SIGCONT);
					s->pauseinvisible = 1;
					#if PATCH_PAUSE_PROCESS
					s->paused = 0;
					#endif // PATCH_PAUSE_PROCESS
					DEBUG("client continued: \"%s\".\n", s->name);
				}
			}
			#endif // PATCH_FLAG_PAUSE_ON_INVISIBLE

			#if PATCH_MOUSE_POINTER_WARPING
			#if PATCH_MOUSE_POINTER_WARPING_RECALL
			if (warp && c != s)
				lastcoordsstore(s);
			#endif // PATCH_MOUSE_POINTER_WARPING_RECALL
			#endif // PATCH_MOUSE_POINTER_WARPING

			if (c != s
				#if PATCH_FLAG_GAME
				|| c->isgame
				#endif // PATCH_FLAG_GAME
			)
			focus(c, 0);

			#if PATCH_MOUSE_POINTER_WARPING
			if (warp)
				#if PATCH_MOUSE_POINTER_WARPING_SMOOTH
				warptoclient(c, 0, 0);
				#else // NO PATCH_MOUSE_POINTER_WARPING_SMOOTH
				warptoclient(c, 0);
				#endif // PATCH_MOUSE_POINTER_WARPING_SMOOTH
			#elif PATCH_FOCUS_FOLLOWS_MOUSE
			if (warp && !ismouseoverclient(c)) {
				if (
					c->isfullscreen
					#if PATCH_FLAG_FAKEFULLSCREEN
					&& c->fakefullscreen != 1
					#endif // PATCH_FLAG_FAKEFULLSCREEN
					)
					XWarpPointer(dpy, None, root, 0, 0, 0, 0, selmon->mx + (selmon->mw / 2), selmon->my + (selmon->mh / 2));
				else
					XWarpPointer(dpy, None, root, 0, 0, 0, 0, c->x + (c->w / 2), c->y + (c->h / 2));
			}
			#endif // PATCH_MOUSE_POINTER_WARPING || PATCH_FOCUS_FOLLOWS_MOUSE
		}
	}
	while (XCheckMaskEvent(dpy, EnterWindowMask, &xev));
}

#if 0 // original fullscreen (monocle + hide bar);
Layout *last_layout;
void
fullscreen(const Arg *arg)
{
	if (selmon->showbar) {
		for(last_layout = (Layout *)layouts; last_layout != selmon->lt[selmon->sellt]; last_layout++);
		setlayout(&((Arg) { .v = &layouts[1] }));
	} else {
		setlayout(&((Arg) { .v = last_layout }));
	}
	togglebar(arg);
}
#endif

#if PATCH_IPC
int
find_dwm_client(const char *name)
{
	if (connect_to_socket() == -1) {
		logdatetime(stderr);
		fprintf(stderr, "dwm: Failed to connect to socket \"%s\".\n", ipcsockpath);
		return 0;
	}

	char *msg;
	size_t msg_size;

	cJSON *gen = cJSON_CreateObject();
	cJSON_AddStringToObject(gen, "client_name", name);

	// Message format:
	// {
	//   "client_name": "<win>"
	// }

	msg = cJSON_PrintUnformatted(gen);
	msg_size = strlen(msg) + 1;

	send_message(IPC_TYPE_FIND_DWM_CLIENT, msg_size, (uint8_t *)msg);

	print_socket_reply();

	cJSON_free(msg);
	cJSON_Delete(gen);

	return 0;
}
int
get_dwm_client(Window win)
{
	if (connect_to_socket() == -1) {
		logdatetime(stderr);
		fprintf(stderr, "dwm: Failed to connect to socket \"%s\".\n", ipcsockpath);
		return 0;
	}

	char *msg;
	size_t msg_size;

	cJSON *gen = cJSON_CreateObject();
	cJSON_AddIntegerToObject(gen, "client_window_id", win);

	// Message format:
	// {
	//   "client_window_id": "<win>"
	// }

	msg = cJSON_PrintUnformatted(gen);
	msg_size = strlen(msg) + 1;

	send_message(IPC_TYPE_GET_DWM_CLIENT, msg_size, (uint8_t *)msg);

	print_socket_reply();

	cJSON_free(msg);
	cJSON_Delete(gen);

	return 0;
}

int
get_layouts()
{
	if (connect_to_socket() == -1) {
		logdatetime(stderr);
		fprintf(stderr, "dwm: Failed to connect to socket \"%s\".\n", ipcsockpath);
		return 0;
	}
	send_message(IPC_TYPE_GET_LAYOUTS, 1, (uint8_t *)"");
	print_socket_reply();

	return 0;
}

int
get_monitors()
{
	if (connect_to_socket() == -1) {
		logdatetime(stderr);
		fprintf(stderr, "dwm: Failed to connect to socket \"%s\".\n", ipcsockpath);
		return 0;
	}
	send_message(IPC_TYPE_GET_MONITORS, 1, (uint8_t *)"");
	print_socket_reply();
	return 0;
}

int
get_tags()
{
	if (connect_to_socket() == -1) {
		logdatetime(stderr);
		fprintf(stderr, "dwm: Failed to connect to socket \"%s\".\n", ipcsockpath);
		return 0;
	}
	send_message(IPC_TYPE_GET_TAGS, 1, (uint8_t *)"");
	print_socket_reply();

	return 0;
}
#endif // PATCH_IPC

#if PATCH_FLAG_GAME
Client *
getactivegameclient(Monitor *m)
{
	Client *c;
	for (c = m->stack; c && (
		!ISVISIBLE(c) || MINIMIZED(c) || !c->isgame || !c->isfullscreen
		#if PATCH_FLAG_FAKEFULLSCREEN
		|| c->fakefullscreen == 1
		#endif // PATCH_FLAG_FAKEFULLSCREEN
	); c = c->snext);

	return c;
}
#endif // PATCH_FLAG_GAME

Atom
getatomprop(Client *c, Atom prop)
{
	if (!c->win)
		return None;
	return (getatompropex(c->win, prop));
}
Atom
getatompropex(Window w, Atom prop)
{
	int di;
	unsigned long dl;
	unsigned char *p = NULL;
	Atom da, atom = None;

	/* FIXME getatomprop should return the number of items and a pointer to
	 * the stored data instead of this workaround */
	Atom req = XA_ATOM;
	if (prop == xatom[XembedInfo])
		req = xatom[XembedInfo];

	if (XGetWindowProperty(
			dpy, w, prop, 0L, sizeof atom, False, req, &da, &di, &dl, &dl, &p
		) == Success && p) {
		atom = *(Atom *)p;
		if (da == xatom[XembedInfo] && dl == 2)
			atom = ((Atom *)p)[1];
		XFree(p);
	}
	return atom;
}

Client *
getclientatcoords(int x, int y, int focusable)
{
	Client *sel = NULL;
	Monitor *m;

	// determine precise stacking order;
	int i;
	unsigned int num;
	Window d1, d2, *wins = NULL;
	Client *c;
	long order = 0;

	// clear existing;
	for (m = mons; m; m = m->next)
		for (c = m->clients; c; c = c->next)
			c->stackorder = -1;

	// XQueryTree returns windows in stacking order top to bottom;
	if (XQueryTree(dpy, root, &d1, &d2, &wins, &num)) {
		if (num > 0) {
			for (i = 0; i < num; i++)
				if ((c = wintoclient(wins[i])))
					c->stackorder = order++;
		}
		if (wins)
			XFree(wins);
	}

	if (x == -1 && y == -1)
		return NULL;

	m = recttomon(x, y, 1, 1);
	order = -1;

	int a, w = 1, h = 1;
	// check if coords are over a client;
	for(c = m->stack; c; c = c->snext)
		if (ISVISIBLE(c) && !MINIMIZED(c)
			#if PATCH_FLAG_IGNORED
			&& !c->isignored
			#endif // PATCH_FLAG_IGNORED
			#if PATCH_FLAG_HIDDEN
			&& !c->ishidden
			#endif // PATCH_FLAG_HIDDEN
			&& (!focusable || (!c->neverfocus
				#if PATCH_FLAG_PANEL
				&& !c->ispanel
				#endif // PATCH_FLAG_PANEL
			))
		) {
			if ((a = INTERSECTC(x, y, w, h, c)) && c->stackorder > order) {
				order = c->stackorder;
				sel = c;
			}
		}

	return sel;
}

Client *
getclientbyname(const char *name)
{
	Client *c, *sel = NULL;
	XClassHint ch = { NULL, NULL };

	for (Monitor *m = mons; m; m = m->next) {
		for (c = m->clients; c; c = c->next)
			if (
				#if PATCH_FLAG_IGNORED
				!c->isignored &&
				#endif // PATCH_FLAG_IGNORED
				#if PATCH_FLAG_PANEL
				!c->ispanel &&
				#endif // PATCH_FLAG_PANEL
				!c->dormant
			) {
				if (strstr(c->name, name)) {
					sel = c;
					break;
				}
				#if PATCH_SHOW_MASTER_CLIENT_ON_TAG
				else if (c->dispclass && strcmp(c->dispclass, name) == 0) {
					sel = c;
					break;
				}
				#endif // PATCH_SHOW_MASTER_CLIENT_ON_TAG
				else {
					XGetClassHint(dpy, c->win, &ch);
					if (ch.res_name) {
						if (strstr(ch.res_name, name))
							sel = c;
						XFree(ch.res_name);
					}
					if (ch.res_class) {
						if (!sel && strstr(ch.res_class, name))
							sel = c;
						XFree(ch.res_class);
					}
					if (sel)
						break;
				}
			}
		if (sel)
			break;
	}
	return sel;
}

#if PATCH_SHOW_DESKTOP
#if PATCH_SHOW_DESKTOP_ONLY_WHEN_ACTIVE
Client *
getdesktopclient(Monitor *m, int *nondesktop_exists)
{
	int c = 0;
	Client *d = NULL;
	// get first desktop client, and first non-desktop client;
	for (Client *cc = m->clients; cc; cc = cc->next) {
		#if PATCH_SHOW_DESKTOP_ONLY_WHEN_ACTIVE
		if (!d && cc->isdesktop)
			d = cc;
		else if (showdesktop_when_active && !c && ISVISIBLEONTAG(cc, m->tagset[m->seltags]) && !cc->isdesktop && !cc->ondesktop
			#if PATCH_FLAG_PANEL
			&& !cc->ispanel
			#endif // PATCH_FLAG_PANEL
			#if PATCH_FLAG_HIDDEN
			&& !cc->ishidden
			#endif // PATCH_FLAG_HIDDEN
		)
			c = 1;
		if ((c || !showdesktop_when_active) && (d
			#if PATCH_SHOW_DESKTOP_UNMANAGED
			|| showdesktop_unmanaged
			#endif // PATCH_SHOW_DESKTOP_UNMANAGED
		))
			break;
		#else // NO PATCH_SHOW_DESKTOP_ONLY_WHEN_ACTIVE
		if (cc->isdesktop) {
			d = cc;
			break;
		}
		#endif // PATCH_SHOW_DESKTOP_ONLY_WHEN_ACTIVE
	}
	*nondesktop_exists = c;
	return d;
}
#endif // PATCH_SHOW_DESKTOP_ONLY_WHEN_ACTIVE
#endif // PATCH_SHOW_DESKTOP

Client *
getfocusable(Monitor *m, Client *c, int force)
{
	Client *cc = c;

	if (!c || (
		#if PATCH_SHOW_DESKTOP
		showdesktop && !nonstop ? (
			m->showdesktop ?
				(!ISVISIBLEONTAG(c, c->mon->tagset[c->mon->seltags]) && !ISVISIBLE(c)) :
				!ISVISIBLE(c) && !(c->isdesktop || c->ondesktop)
		) :
		#endif // PATCH_SHOW_DESKTOP
		!ISVISIBLE(c)
		) || !validate_pid(c)
		#if PATCH_FLAG_HIDDEN
		|| c->ishidden
		#endif // PATCH_FLAG_HIDDEN
		|| (c->neverfocus && !force)
		#if PATCH_FLAG_IGNORED
		|| (c->isignored && !force)
		#endif // PATCH_FLAG_IGNORED
	) {
		if (!(c = m->sel) || !ISVISIBLE(c)
			#if PATCH_FLAG_HIDDEN
			|| c->ishidden
			#endif // PATCH_FLAG_HIDDEN
			#if PATCH_FLAG_IGNORED
			|| c->isignored
			#endif // PATCH_FLAG_IGNORED
			|| c->neverfocus
			)
			for (c = m->stack; c && (c == cc || !ISVISIBLE(c) || c->neverfocus
				#if PATCH_FLAG_PANEL
				|| c->ispanel
				#endif // PATCH_FLAG_PANEL
				#if PATCH_FLAG_IGNORED
				|| c->isignored
				#endif // PATCH_FLAG_IGNORED
				#if PATCH_FLAG_HIDDEN
				|| c->ishidden
				#endif // PATCH_FLAG_HIDDEN
			); c = c->snext);
	}
	if (!cc) {
		// check for fullscreen client
		cc = c;
		for (c = m->stack; c && (!ISVISIBLE(c) || c->neverfocus
			#if PATCH_FLAG_PANEL
			|| c->ispanel
			#endif // PATCH_FLAG_PANEL
			#if PATCH_FLAG_IGNORED
			|| c->isignored
			#endif // PATCH_FLAG_IGNORED
			#if PATCH_FLAG_HIDDEN
			|| c->ishidden
			#endif // PATCH_FLAG_HIDDEN
			|| !c->isfullscreen
			#if PATCH_FLAG_FAKEFULLSCREEN
			|| c->fakefullscreen == 1
			#endif // PATCH_FLAG_FAKEFULLSCREEN
		); c = c->snext);
		if (!c)
			c = cc;
	}
	return c;
}

#if PATCH_FLAG_PANEL
#if PATCH_FLAG_FLOAT_ALIGNMENT
int
getpanelpadding(Monitor *m, unsigned int *px, unsigned int *pw)
{
	Client *c;
	int haspanel = 0;
	*px = 0;
	*pw = 0;
	// find panel widths if any exist;
	for (c = m->clients; c; c = c->next)
		if (c->ispanel && ISVISIBLE(c)) {
			if ((m->topbar && c->floataligny == 0) || (!m->topbar && c->floataligny == 1)) {
				if (c->floatalignx == 0 && c->w > *px)
					*px = c->w;
				else if (c->floatalignx == 1 && c->w > *pw)
					*pw = c->w;
				haspanel = 1;
			}
		}
	return haspanel;
}
#endif // PATCH_FLAG_FLOAT_ALIGNMENT
#endif // PATCH_FLAG_PANEL

Client *
getparentclient(Client *c)
{
	Client *i = NULL, *p = NULL;
	Monitor *m, *s;
	Window r, parent = 0, *children = NULL;
	unsigned int num_children;

	#if PATCH_SHOW_DESKTOP
	if (showdesktop && c->wasdesktop)
		return NULL;
	#endif // PATCH_SHOW_DESKTOP

	if (XQueryTree(dpy, c->win, &r, &parent, &children, &num_children)) {
		if (children)
			XFree((char *)children);
		#if PATCH_SHOW_DESKTOP
		#if PATCH_SHOW_DESKTOP_UNMANAGED
		if (showdesktop && showdesktop_unmanaged && desktopwin == parent) {
			c->ondesktop = 1;
			return NULL;
		}
		#endif // PATCH_SHOW_DESKTOP_UNMANAGED
		#endif // PATCH_SHOW_DESKTOP
		if (!parent || parent == root || !(i = wintoclient(parent))
			#if PATCH_FLAG_PARENT
			|| i->neverparent
			#endif // PATCH_FLAG_PARENT
			#if PATCH_FLAG_HIDDEN
			|| i->ishidden
			#endif // PATCH_FLAG_HIDDEN
			)
			i = NULL;
		else if (!i && parent && parent != root) {
			logdatetime(stderr);
			fprintf(stderr, "note: parent 0x%lx is unmanaged for client: \"%s\" (pid:%u)\n", parent, c->name, c->pid);
		}
	}

	if (!i) {

		if (!c->pid)
			return NULL;

		#if PATCH_SHOW_DESKTOP
		#if PATCH_SHOW_DESKTOP_UNMANAGED
		if (showdesktop && showdesktop_unmanaged && desktoppid && isdescprocess(desktoppid, c->pid)) {
			c->ondesktop = 1;
			return NULL;
		}
		#endif // PATCH_SHOW_DESKTOP_UNMANAGED
		#endif // PATCH_SHOW_DESKTOP

		s = (c->mon ? c->mon : selmon);
		for (m = s; m; ) {
			for (i = (m->sel ? m->sel : m->stack); i; i = i->snext)
				if (
					#if PATCH_FLAG_PARENT
					!i->neverparent &&
					#endif // PATCH_FLAG_PARENT
					i->pid && isdescprocess(i->pid, c->pid)
				) {
					p = i;
					break;
				}
			if (p)
				break;
			if (m->sel && m->stack != m->sel) {
				for (i = m->stack; i && i != m->sel; i = i->snext)
					if (
						#if PATCH_FLAG_PARENT
						!i->neverparent &&
						#endif // PATCH_FLAG_PARENT
						i->pid && isdescprocess(i->pid, c->pid)
						)
					{
						p = i;
						break;
					}
				if (p)
					break;
			}
			if (m->next == s)
				break;
			else if (!m->next && s != mons)
				m = mons;
			else
				m = m->next;
		}

		if (p && parent && parent != root) {
			logdatetime(stderr);
			fprintf(stderr, "note: used client pid %u to assign parent client: \"%s\"\n", c->pid, p->name);
		}

		return p;
	}
	return i;
}


#if PATCH_WINDOW_ICONS
static uint32_t prealpha(uint32_t p) {
	uint8_t a = p >> 24u;
	uint32_t rb = (a * (p & 0xFF00FFu)) >> 8u;
	uint32_t g = (a * (p & 0x00FF00u)) >> 8u;
	return (rb & 0xFF00FFu) | (g & 0x00FF00u) | (a << 24u);
}

Picture
geticonprop(
	#if PATCH_WINDOW_ICONS_DEFAULT_ICON || PATCH_WINDOW_ICONS_CUSTOM_ICONS
	Client *c,
	#endif // PATCH_WINDOW_ICONS_DEFAULT_ICON || PATCH_WINDOW_ICONS_CUSTOM_ICONS
	Window win, unsigned int *picw, unsigned int *pich, unsigned int iconsize
)
{
	int format;
	unsigned long n, extra, *p = NULL;
	Atom real;
	uint32_t icw, ich;
	Picture ret;

	#if PATCH_WINDOW_ICONS_CUSTOM_ICONS
	if (c->icon_replace && c->icon_file) {

		ret = drw_picture_create_resized_from_file(drw, c->icon_file, picw, pich, iconsize);
		if (ret)
			return ret;

	}
	#endif // PATCH_WINDOW_ICONS_CUSTOM_ICONS

	Status status = XGetWindowProperty(dpy, win, netatom[NetWMIcon], 0L, LONG_MAX, False, AnyPropertyType, 
						   &real, &format, &n, &extra, (unsigned char **)&p);
	if (status == Success && (n == 0 || format != 32)) {
		XFree(p);
		status ^= 1;
	}

	if (status == Success) {

		unsigned long *bstp = NULL;
		uint32_t w, h, sz;
		{
			unsigned long *i; const unsigned long *end = p + n;
			uint32_t bstd = UINT32_MAX, d, m;
			for (i = p; i < end - 1; i += sz) {
				if ((w = *i++) >= 16384 || (h = *i++) >= 16384) { XFree(p); return None; }
				if ((sz = w * h) > end - i) break;
				if ((m = w > h ? w : h) >= iconsize && (d = m - iconsize) < bstd) { bstd = d; bstp = i; }
			}
			if (!bstp) {
				for (i = p; i < end - 1; i += sz) {
					if ((w = *i++) >= 16384 || (h = *i++) >= 16384) { XFree(p); return None; }
					if ((sz = w * h) > end - i) break;
					if ((d = iconsize - (w > h ? w : h)) < bstd) { bstd = d; bstp = i; }
				}
			}
			if (!bstp) { XFree(p); return None; }
		}

		if ((w = *(bstp - 2)) == 0 || (h = *(bstp - 1)) == 0) { XFree(p); return None; }

		if (w <= h) {
			ich = iconsize; icw = w * iconsize / h;
			if (icw == 0) icw = 1;
		}
		else {
			icw = iconsize; ich = h * iconsize / w;
			if (ich == 0) ich = 1;
		}
		*picw = icw; *pich = ich;

		uint32_t i, *bstp32 = (uint32_t *)bstp;
		for (sz = w * h, i = 0; i < sz; ++i) bstp32[i] = prealpha(bstp[i]);

		ret = drw_picture_create_resized(drw, (char *)bstp, w, h, icw, ich);
		XFree(p);

		return ret;

	}
	#if PATCH_WINDOW_ICONS_LEGACY_ICCCM
	else {

		XWMHints *wmh;
		Pixmap icon = 0;
		Pixmap mask = 0;
		Window root_return;
		int x, y;
		unsigned int w = 0, h = 0;
		unsigned int bw, depth = 0;

		if ((wmh = XGetWMHints(dpy, win))) {
			if (wmh->flags & IconPixmapHint) {
				icon = wmh->icon_pixmap;
				if (wmh->flags & IconMaskHint)
					mask = wmh->icon_mask;
			}
			XFree(wmh);
			if (icon) {

				XGetGeometry(dpy, icon, &root_return, &x, &y, &w, &h, &bw, &depth);

				if (w <= h) {
					ich = iconsize; icw = w * iconsize / h;
					if (icw == 0) icw = 1;
				}
				else {
					icw = iconsize; ich = h * iconsize / w;
					if (ich == 0) ich = 1;
				}
				*picw = icw; *pich = ich;

				XImage *img = XGetImage(dpy, icon, 0, 0, w, h, AllPlanes, ZPixmap);
				XImage *m = img;
				if (mask) {
					XGetGeometry(dpy, mask, &root_return, &x, &y, &w, &h, &bw, &depth);
					m = XGetImage(dpy, mask, 0, 0, w, h, AllPlanes, ZPixmap);
				}
				for (y = 0; y < h; ++y)
					for (x = 0; x < w; ++x) {
						unsigned long pixel = XGetPixel(img, x, y);
						unsigned long mpixel = XGetPixel(m, x, y);
						pixel = (pixel & 0xFFFFFFL) | (mpixel ? 0xFF000000L : 0);
						XPutPixel(img, x, y, pixel);
					}

				ret = drw_picture_create_resized(drw, (char *)img->data, w, h, icw, ich);
				return ret;
			}
		}
	}
	#endif // PATCH_WINDOW_ICONS_LEGACY_ICCCM

	#if PATCH_WINDOW_ICONS_CUSTOM_ICONS
	if (!c->icon_replace && c->icon_file) {

		ret = drw_picture_create_resized_from_file(drw, c->icon_file, picw, pich, iconsize);
		if (ret)
			return ret;

	}
	#endif // PATCH_WINDOW_ICONS_CUSTOM_ICONS

	#if PATCH_WINDOW_ICONS_DEFAULT_ICON
	#if PATCH_SHOW_DESKTOP
	if (showdesktop && c->isdesktop)
		ret = drw_picture_create_resized_from_file(drw, desktop_icon, picw, pich, iconsize);
	else
	#endif // PATCH_SHOW_DESKTOP
	ret = drw_picture_create_resized_from_file(drw, default_icon, picw, pich, iconsize);
	if (ret)
		return ret;
	#endif // PATCH_WINDOW_ICONS_DEFAULT_ICON

	return None;
}
#endif // PATCH_WINDOW_ICONS

pid_t
getparentprocess(pid_t p)
{
	unsigned int v = 0;

#ifdef __linux__
	FILE *f;
	char buf[256];
	snprintf(buf, sizeof(buf) - 1, "/proc/%u/stat", (unsigned)p);

	if (!(f = fopen(buf, "r")))
		return 0;

	fscanf(f, "%*u %*s %*c %u", &v);
	fclose(f);
#endif /* __linux__*/

#ifdef __OpenBSD__
	int n;
	kvm_t *kd;
	struct kinfo_proc *kp;

	kd = kvm_openfiles(NULL, NULL, NULL, KVM_NO_FILES, NULL);
	if (!kd)
		return 0;

	kp = kvm_getprocs(kd, KERN_PROC_PID, p, sizeof(*kp), &n);
	v = kp->p_ppid;
#endif /* __OpenBSD__ */

	return (pid_t)v;
}

pid_t
getprocessid(const char *procname)
{
	unsigned int v = 0;
#ifdef __linux__
	FILE *fp;
	char buf[256];
	snprintf(buf, sizeof(buf) - 1, "pidof -s %s", procname);
	if (!(fp = popen(buf, "r")))
		return 0;
	fgets(buf, sizeof(buf), fp);
	pclose(fp);
	v = strtol(buf, NULL, 10);
#endif /* __linux__*/
	return (pid_t)v;
}

int
getprocname(pid_t pid, char *buffer, size_t buffer_size, char **procname, char **parameters)
{
	int ret = 0;
	if (!pid)
		return ret;
#ifdef __linux__
	FILE *fp;
	char *substr;
	snprintf(buffer, buffer_size, "/proc/%u/cmdline", (unsigned)pid);
	if (!(fp = fopen(buffer, "r"))) {
		logdatetime(stderr);
		fprintf(stderr, "dwm: unable to open \"%s\"\n", buffer);
		return 0;
	}
	memset(buffer,0,buffer_size);
	fgets(buffer, buffer_size - 2, fp);
	fclose(fp);

	// cope with replaced command lines with spaces instead of nul chars;
	substr = strchr(buffer, ' ');
	if (substr) {
		if (substr < buffer + buffer_size - 2) {
			substr[0] = '\0';
			*parameters = substr + 1;
		}
		else
			*parameters = NULL;
	}
	else {
		size_t flen = strlen(buffer);
		if (flen < buffer_size - 1)
			*parameters = buffer + flen + 1;
		else
			*parameters = NULL;
	}
	if (*parameters) {
		for (char *pbuff = *parameters; pbuff < buffer + buffer_size - 1; ++pbuff)
			if (pbuff[0] == '\0' && pbuff[1] != '\0')
				pbuff[0] = ' ';

	}
	substr = strrchr(buffer, '/');
	if (substr)
		*procname = substr + 1;
	else
		*procname = buffer;
	ret = 1;
#endif /* __linux__*/
	return ret;
}

#if PATCH_MOUSE_POINTER_WARPING || PATCH_FOCUS_FOLLOWS_MOUSE
int
getrelativeptr(Client *c, int *x, int *y)
{
	*x = 0;
	*y = 0;

	if (!c)
		return 0;

	int di;
	unsigned int dui;
	Window dummy;
	int ok = 1;

	if (!XQueryPointer(dpy, c->win, &dummy, &dummy, &di, &di, x, y, &dui))
		ok = 0;
	else {
		if (*x + c->bw < 0 || *y + c->bw < 0 || *x > (c->w + c->bw) || *y > (c->h + c->bw))
			ok = 0;
	}
	if (!ok) {
		*x = c->w / 2;
		*y = c->h / 2;
		return 0;
	}
	return 1;
}

int
getrelativeptrex(Client *c, int *x, int *y)
{
	if (!c)
		return 0;

	int di;
	unsigned int dui;
	Window dummy;

	if (!XQueryPointer(dpy, c->win, &dummy, &dummy, &di, &di, x, y, &dui))
		return 0;

	return 1;
}
#endif // PATCH_MOUSE_POINTER_WARPING || PATCH_FOCUS_FOLLOWS_MOUSE

int
getrootptr(int *x, int *y)
{
	int di;
	unsigned int dui;
	Window dummy;

	return XQueryPointer(dpy, root, &dummy, &dummy, x, y, &di, &di, &dui);
}

long
getstate(Window w)
{
	int format;
	long result = -1;
	unsigned char *p = NULL;
	unsigned long n, extra;
	Atom real;

	if (XGetWindowProperty(dpy, w, wmatom[WMState], 0L, 2L, False, wmatom[WMState],
		&real, &format, &n, &extra, (unsigned char **)&p) != Success)
	{
		// attempt to catch cases where WM_STATE is not picked up,
		// but _NET_WM_WINDOW_TYPE is populated;
		if (getatompropex(w, netatom[NetWMWindowType]) != None)
			return IconicState;
		return -1;
	}
	if (n != 0)
		result = *p;
	XFree(p);

	return result;
}

#if PATCH_STATUSCMD
pid_t
getstatusbarpid()
{
	char buf[32], *str = buf, *c;
	FILE *fp;

	if (statuspid > 0) {
		snprintf(buf, sizeof(buf), "/proc/%u/cmdline", statuspid);
		if ((fp = fopen(buf, "r"))) {
			fgets(buf, sizeof(buf), fp);
			while ((c = strchr(str, '/')))
				str = c + 1;
			fclose(fp);
			if (!strcmp(str, STATUSBAR))
				return statuspid;
		}
	}
	if (!(fp = popen("pidof -s "STATUSBAR, "r")))
		return -1;
	fgets(buf, sizeof(buf), fp);
	pclose(fp);
	return strtol(buf, NULL, 10);
}
#endif // PATCH_STATUSCMD

#if PATCH_SYSTRAY
unsigned int
getsystraywidth()
{
	unsigned int w = 0;
	Client *i;
	if(showsystray)
		for(i = systray->icons; i; w += i->w + systrayspacing, i = i->next) ;
	return w ? w + systrayspacing : 1;
}
#endif // PATCH_SYSTRAY

int
gettextprop(Window w, Atom atom, char *text, unsigned int size)
{
	char **list = NULL;
	int n;
	XTextProperty name;

	if (!text || size == 0)
		return 0;
	text[0] = '\0';
	if (!XGetTextProperty(dpy, w, &name, atom) || !name.nitems)
		return 0;
	if (name.encoding == XA_STRING) {
		strncpy(text, (char *)name.value, size - 1);
	} else if (XmbTextPropertyToTextList(dpy, &name, &list, &n) >= Success && n > 0 && *list) {
		strncpy(text, *list, size - 1);
		XFreeStringList(list);
	}
	text[size - 1] = '\0';
	XFree(name.value);
	return 1;
}

Client *
getmontopclient(Monitor *m)
{
	int i;
	unsigned int num;
	Window d1, d2, *wins = NULL;
	Client *c, *top = NULL;

	if (XQueryTree(dpy, root, &d1, &d2, &wins, &num)) {
		if (num > 0) {
			for (i = 0; i < num; i++)
				if ((c = wintoclient(wins[i])) && c->mon == m) {
					top = c;
					break;
				}
		}
		if (wins)
			XFree(wins);
	}
	return top;
}

Client *
getultimateparentclient(Client *c)
{
	Client *i = NULL;
	Monitor *m, *s;
	Window r, parent, *children = NULL;
	unsigned int num_children;

	#if PATCH_SHOW_DESKTOP
	if (showdesktop && c->wasdesktop)
		return NULL;
	#endif // PATCH_SHOW_DESKTOP

	if (XQueryTree(dpy, c->win, &r, &parent, &children, &num_children)) {
		if (children)
			XFree((char *)children);
		#if PATCH_SHOW_DESKTOP
		#if PATCH_SHOW_DESKTOP_UNMANAGED
		if (showdesktop && showdesktop_unmanaged && desktopwin == parent) {
			c->ondesktop = 1;
			return NULL;
		}
		#endif // PATCH_SHOW_DESKTOP_UNMANAGED
		#endif // PATCH_SHOW_DESKTOP
		i = wintoclient(parent);

		if (i) {
			if (c->pid && i->pid == c->pid)
				return i->ultparent;
			i = NULL;
		}
	}

	if (!i) {

		if (!c->pid)
			return NULL;

		s = (c->mon ? c->mon : selmon);
		for (m = s; m; ) {
			for (i = (m->sel ? m->sel : m->stack); i; i = i->snext)
				if (
					#if PATCH_FLAG_PARENT
					!i->neverparent &&
					#endif // PATCH_FLAG_PARENT
					i->pid == c->pid && i->ultparent == i
					)
					return i;

			if (m->sel && m->stack != m->sel)
				for (i = m->stack; i && i != m->sel; i = i->snext)
					if (
						#if PATCH_FLAG_PARENT
						!i->neverparent &&
						#endif // PATCH_FLAG_PARENT
						i->pid == c->pid && i->ultparent == i
						)
						return i;

			if (m->next == s)
				break;
			else if (!m->next && s != mons)
				m = mons;
			else
				m = m->next;
		}

	}
	return i;
}

void
grabbuttons(Client *c, int focused)
{
	updatenumlockmask();
	{
		unsigned int i, j;
		unsigned int modifiers[] = { 0, LockMask, numlockmask, numlockmask|LockMask };
		XUngrabButton(dpy, AnyButton, AnyModifier, c->win);
		if (!focused
			#if PATCH_FLAG_GAME
			|| (c->isgame && c->isfullscreen)
			#endif // PATCH_FLAG_GAME
			)
			XGrabButton(dpy, AnyButton, AnyModifier, c->win, False,
				BUTTONMASK, GrabModeSync, GrabModeSync, None, None);
		for (i = 0; i < LENGTH(buttons); i++) {
			if (buttons[i].click == ClkClientWin) {
				for (j = 0; j < LENGTH(modifiers); j++)
					if (buttons[i].mask) {
						XGrabButton(dpy, buttons[i].button,
							buttons[i].mask | modifiers[j],
							c->win, False, BUTTONMASK,
							GrabModeAsync, GrabModeSync, None, None);
					}
					else {
						//fprintf(stderr, "can't do selectinput for buttonpressmask\n");
						//XSelectInput(dpy, c->win, ButtonPressMask);
					}
			}
		}
	}
}

#if PATCH_ALTTAB || PATCH_TORCH
int
grabinputs(int keyboard, int mouse, Cursor cursor)
{
	struct timespec ts = { .tv_sec = 0, .tv_nsec = 1000000 };

	int grabbed = 1, grabkey = !keyboard, grabmouse = !mouse;
	for (int i = 1000; i > 0; i--) {
		if (!grabkey && XGrabKeyboard(dpy, root, True, GrabModeAsync, GrabModeAsync, CurrentTime) == GrabSuccess)
			grabkey = 1;
		if (!grabmouse && XGrabPointer(dpy, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync, None, cursor, CurrentTime) == GrabSuccess)
			grabmouse = 1;
		if (grabkey && grabmouse) break;

		nanosleep(&ts, NULL);
		if (i <= 0)
			grabbed = 0;
	}
	return grabbed;
}
#endif // PATCH_ALTTAB || PATCH_TORCH

void
grabkeys(void)
{
	updatenumlockmask();
	{
		unsigned int i, j, k;
		unsigned int modifiers[] = { 0, LockMask, numlockmask, numlockmask|LockMask };
		int start, end, skip;
		KeySym *syms;

		XUngrabKey(dpy, AnyKey, AnyModifier, root);
		XDisplayKeycodes(dpy, &start, &end);
		syms = XGetKeyboardMapping(dpy, start, end - start + 1, &skip);
		if (!syms)
			return;
		for (k = start; k <= end; k++)
			for (i = 0; i < LENGTH(keys); i++)
				/* skip modifier codes, we do that ourselves */
				if (keys[i].keysym == syms[(k - start) * skip]
					#if PATCH_KEY_HOLD
					&& !(keys[i].mod & ModKeyHoldMask)
					#endif // PATCH_KEY_HOLD
					)
					for (j = 0; j < LENGTH(modifiers); j++)
						if (!(XGrabKey(
								dpy, k,
								(keys[i].mod & ~ModKeyNoRepeatMask) | modifiers[j],
								root, True,
								GrabModeAsync, GrabModeAsync
							)))
							fprintf(stderr, "Unable to grab keycode: %u + %lu\n", keys[i].mod | modifiers[j], keys[i].keysym);
		XFree(syms);
	}
}

#if PATCH_CLASS_STACKING
void
groupallclassstacks(Monitor *m)
{
	Client *c, *head;
	unsigned int count = 0, i = 0;
	for (c = m->stack; c; c = c->snext)
		if (c->stackhead)
			++count;
	if (!count)
		return;

	Client **stack = (Client **) malloc(count * sizeof(Client *));

	for (c = m->stack; c; c = c->snext)
		if (c->stackhead && !c->isstackhead)
			stack[i++] = c;

	for (i = 0; i < count; ++i) {
		detachstack(stack[i]);
		detach(stack[i]);
	}

	for (i = 0; i < count; ++i) {
		c = stack[count - 1 - i];
		head = c->stackhead;
		c->next = head->next;
		if ((c->snext = head->snext))
			c->snext->sprev = c;
		head->next = c;
		head->snext = c;
		c->sprev = head;
		if (m->sel == c)
			m->sel = head;
	}
	free(stack);

}
#endif // PATCH_CLASS_STACKING

Client *
guessnextfocus(Client *c, Monitor *m)
{
	Client *sel = NULL;
	#if PATCH_FOCUS_FOLLOWS_MOUSE
	int a, w = 1, h = 1;
	#endif // PATCH_FOCUS_FOLLOWS_MOUSE
	int x, y;
	if (!c) {
		if (!m)
			m = selmon;
	}
	else
		if (!m)
			m = c->mon;

	#if PATCH_FOCUS_FOLLOWS_MOUSE
	getrootptr(&x, &y);
	#endif // PATCH_FOCUS_FOLLOWS_MOUSE

	// use prevsel if it's visible and on the same monitor;
	if (!sel && c && c->prevsel && validclient(c->prevsel)
		&& ISVISIBLE(c->prevsel) && !MINIMIZED(c->prevsel)
		&& c->prevsel->mon == c->mon
		#if PATCH_FLAG_HIDDEN
		&& !c->prevsel->ishidden
		#endif // PATCH_FLAG_HIDDEN
		#if PATCH_FOCUS_FOLLOWS_MOUSE
		&& (a = INTERSECTC(x, y, w, h, c->prevsel))
		#endif // PATCH_FOCUS_FOLLOWS_MOUSE
		)
		sel = c->prevsel;

	// prefer the client's parent for the next focus;
	if (!sel && c && c->parent && !c->toplevel && !c->fosterparent
		&& ISVISIBLE(c->parent) && !MINIMIZED(c->parent)
		&& c->parent->mon == c->mon
		#if PATCH_FLAG_HIDDEN
		&& !c->parent->ishidden
		#endif // PATCH_FLAG_HIDDEN
		#if PATCH_FOCUS_FOLLOWS_MOUSE
		&& (a = INTERSECTC(x, y, w, h, c->parent))
		#endif // PATCH_FOCUS_FOLLOWS_MOUSE
		) {
		sel = c->parent;
		// if floating client has autofocus=0 then look at its parent instead;
		while (sel && sel->isfloating && !sel->autofocus) {
			if (sel->parent && !sel->toplevel && !sel->fosterparent
				&& ISVISIBLE(sel->parent) && !MINIMIZED(sel->parent)
				&& sel->parent->mon == c->mon)
				sel = sel->parent;
			else
				sel = NULL;
		}
	}

	#if PATCH_FLAG_GAME
	// try to avoid disrupting an already focused fullscreen game client;
	if (!sel && (!c || c->isfloating))
		sel = getactivegameclient(m);
	#endif // PATCH_FLAG_GAME

	if (!sel
		#if !PATCH_FOCUS_FOLLOWS_MOUSE
		&& getrootptr(&x, &y)
		#endif // !PATCH_FOCUS_FOLLOWS_MOUSE
		) {
 		if ((sel = getclientatcoords(x, y, True)) && sel == c)
			sel = NULL;
	}

	// last resort is next valid focusable client from the stack;
	if (!sel)
		sel = getfocusable(m, NULL, 0);

	return sel;
}

#if PATCH_IPC
int
handlexevent(struct epoll_event *ev)
{
	if (ev->events & EPOLLIN) {
		XEvent ev;
		#if PATCH_MOUSE_POINTER_HIDING
		XGenericEventCookie *cookie;
		#endif // PATCH_MOUSE_POINTER_HIDING
		while (running == 1 && XPending(dpy)) {
			#if PATCH_MOUSE_POINTER_HIDING
			cookie = &ev.xcookie;
			#endif // PATCH_MOUSE_POINTER_HIDING
			XNextEvent(dpy, &ev);

			#if PATCH_MOUSE_POINTER_HIDING
			if (ev.type == motion_type) {
				if (!cursor_always_hide)
					showcursor();
				#if 0 //PATCH_FOCUS_FOLLOWS_MOUSE
				if (selmon->sel)
					checkmouseoverclient();
				#endif // PATCH_FOCUS_FOLLOWS_MOUSE
				break;
			}
			else if (ev.type == key_release_type) {

				if ((!selmon && cursorhideonkeys)
				|| (selmon && !selmon->sel && selmon->cursorhideonkeys)
				|| (selmon && selmon->sel && (selmon->sel->cursorhideonkeys == 1 || (selmon->sel->cursorhideonkeys == -1 && selmon->cursorhideonkeys)))) {
					unsigned int state = 0;
					if (cursor_ignore_mods) {
						// extract modifier state;
						// xinput device event;
						XDeviceKeyEvent *key = (XDeviceKeyEvent *) &ev;
						if ((state = (key->keycode == 9)))
							showcursor();
						else
							state = (key->state & cursor_ignore_mods);
					}
					if (!state)
						hidecursor();
				}
			}
			else if (ev.type == button_press_type || ev.type == button_release_type) {
				if (!cursor_always_hide)
					showcursor();
			}
			else if (ev.type == device_change_type) {
				XDevicePresenceNotifyEvent *xdpe = (XDevicePresenceNotifyEvent *)&ev;
				if (last_device_change != xdpe->serial) {
					snoop_root();
					last_device_change = xdpe->serial;
				}
			}
			else if (ev.type == GenericEvent) {
				/* xi2 raw event */
				XGetEventData(dpy, cookie);
				XIDeviceEvent *xie = (XIDeviceEvent *)cookie->data;

				switch (xie->evtype) {
					case XI_RawMotion:
					#if 0
						#if PATCH_FOCUS_FOLLOWS_MOUSE
						if (selmon->sel)
							checkmouseoverclient();
						#endif // PATCH_FOCUS_FOLLOWS_MOUSE
					#endif

					case XI_RawButtonPress:
						if (ignore_scroll && ((xie->detail >= 4 && xie->detail <= 7) || xie->event_x == xie->event_y))
							break;
						if (!cursor_always_hide)
							showcursor();
						break;

					case XI_RawButtonRelease:
						break;

					default:
						DEBUG("unknown XI event type %d\n", xie->evtype);
				}

				XFreeEventData(dpy, cookie);
			}
			else if (ev.type == (timer_sync_event + XSyncAlarmNotify)) {
				XSyncAlarmNotifyEvent *alarm_e = (XSyncAlarmNotifyEvent *)&ev;
				if (alarm_e->alarm == cursor_idle_alarm) {
					if (cursortimeout) {
						int hide = 0;

						if ((!selmon && cursorautohide)
						|| (selmon && !selmon->sel && selmon->cursorautohide))
							hide = 1;
						else {
							int x, y;
							Client *sel = NULL;
							if (getrootptr(&x, &y))
								sel = getclientatcoords(x, y, False);
							if (!sel)
								sel = selmon->sel;
							if (sel && sel == selmon->sel && (sel->cursorautohide == 1 || (sel->cursorautohide == -1 && sel->mon->cursorautohide)))
								hide = 1;
						}
						if (hide) {
							//DEBUG("idle counter reached %dms, hiding cursor\n", XSyncValueLow32(alarm_e->counter_value));
							hidecursor();
						}
						#if DEBUGGING
						else {
							//DEBUG("idle counter reached %dms\n", XSyncValueLow32(alarm_e->counter_value));
						}
						#endif // DEBUGGING
					}
				}
			}
			else
			#endif // PATCH_MOUSE_POINTER_HIDING
			if (handler[ev.type]) {
				#if PATCH_LOG_DIAGNOSTICS
				if (logdiagnostics_event(ev))
				#endif // PATCH_LOG_DIAGNOSTICS
				{
					handler[ev.type](&ev); /* call handler */
					ipc_send_events(mons, &lastselmon, selmon);
				}
			}
		}
	} else if (ev->events & EPOLLHUP) {
		return -1;
	}

	return 0;
}
#endif // PATCH_IPC

#if PATCH_MOUSE_POINTER_HIDING
void
hidecursor(void)
{
#if 0
	Window win;
	XWindowAttributes attrs;
	int x, y, h, w, junk;
	unsigned int ujunk;
#endif
	if (cursorhiding)
		return;

	int x = 0, y = 0;
	Client *c;
	if ((c = selmon->sel)) {

		if (getrootptr(&cursormove_x, &cursormove_y)) {

			#if PATCH_MOUSE_POINTER_WARPING
			#if PATCH_MOUSE_POINTER_WARPING_RECALL
			lastcoordsrecall(c, 1, 1, &x, &y);
			#else // NO PATCH_MOUSE_POINTER_WARPING_RECALL
			if (c->focusabs) {
				x = c->focusdx + (c->focusdx < 0 ? c->w : 0);
				y = c->focusdy + (c->focusdy < 0 ? c->h : 0);
			}
			else {
				if (c->focusdx != 1.0f)
					x = c->focusdx * c->w/2 + (c->focusdx < 0 ? c->w : 0);
				else
					x = c->w;
				if (c->focusdy != 1.0f)
					y = c->focusdy * c->h/2 + (c->focusdy < 0 ? c->h : 0);
				else
					y = c->h;
			}
			#endif // !PATCH_MOUSE_POINTER_WARPING_RECALL
			XWarpPointer(dpy, None, c->win, 0, 0, 0, 0, x, y);
			XSync(dpy, False);
			#endif // PATCH_MOUSE_POINTER_WARPING
		}
		else {
			cursormove_x = -1;
			cursormove_y = -1;
		}
	}
	#if 0
	if (cursormove) {
		if (XQueryPointer(dpy, root,
		    &win, &win, &x, &y, &junk, &junk, &ujunk)) {
			cursormove_x = x;
			cursormove_y = y;

			XGetWindowAttributes(dpy, win, &attrs);

			h = XHeightOfScreen(DefaultScreenOfDisplay(dpy));
			w = XWidthOfScreen(DefaultScreenOfDisplay(dpy));

			switch (cursormove) {
			case MOVE_NW:
				x = 0;
				y = 0;
				break;
			case MOVE_NE:
				x = w;
				y = 0;
				break;
			case MOVE_SW:
				x = 0;
				y = h;
				break;
			case MOVE_SE:
				x = w;
				y = h;
				break;
			case MOVE_WIN_NW:
				x = attrs.x;
				y = attrs.y;
				break;
			case MOVE_WIN_NE:
				x = attrs.x + attrs.width;
				y = attrs.y;
				break;
			case MOVE_WIN_SW:
				x = attrs.x;
				y = attrs.x + attrs.height;
				break;
			case MOVE_WIN_SE:
				x = attrs.x + attrs.width;
				y = attrs.x + attrs.height;
				break;
			case MOVE_CUSTOM:
				x = (cursormove_custom_mask & XNegative ? w : 0) + cursormove_custom_x;
				y = (cursormove_custom_mask & YNegative ? h : 0) + cursormove_custom_y;
				break;
			}

			XWarpPointer(dpy, None, root, 0, 0, 0, 0, x, y);
		} else {
			cursormove_x = -1;
			cursormove_y = -1;
			logdatetime(stderr);
			fprintf(stderr, "dwm: failed finding cursor coordinates.\n");
		}
	}
	#endif
	XFixesHideCursor(dpy, root);
	cursorhiding = 1;
}
#endif // PATCH_MOUSE_POINTER_HIDING

#if PATCH_FLAG_HIDDEN
void
hidewin(const Arg *arg) {
	#if PATCH_FOCUS_FOLLOWS_MOUSE
	checkmouseoverclient();
	#endif // PATCH_FOCUS_FOLLOWS_MOUSE

	Client *c = selmon->sel;
	Client *h = NULL;
	if (arg->ui) {
		// hide all other windows in this tag
		for (h = c->mon->clients; h; h = h->next)
			if (h != c && !h->ishidden && ISVISIBLE(h)
				#if PATCH_FLAG_GAME
				&& !(h->isgame && h->isfullscreen)
				#endif // PATCH_FLAG_GAME
				#if PATCH_SHOW_DESKTOP
				&& !(h->isdesktop || h->ondesktop)
				#endif // PATCH_SHOW_DESKTOP
			) {
				sethidden(h, True, False);
				#if PATCH_PERSISTENT_METADATA
				setclienttagprop(h);
				#endif // PATCH_PERSISTENT_METADATA
			}
		#if PATCH_CLASS_STACKING
		c->stackhead = NULL;
		#endif // PATCH_CLASS_STACKING
		focus(NULL, 0);
		h = NULL;
	} else {
		// hide just the active window
		if (!c)
			return;
		if (c->ishidden
			#if PATCH_FLAG_GAME
			|| (c->isgame && c->isfullscreen)
			#endif // PATCH_FLAG_GAME
			#if PATCH_SHOW_DESKTOP
			|| (c->isdesktop || c->ondesktop)
			#endif // PATCH_SHOW_DESKTOP
			) return;
		sethidden(c, True, False);
		#if PATCH_PERSISTENT_METADATA
		setclienttagprop(c);
		#endif // PATCH_PERSISTENT_METADATA
		if (!c->isfloating) {
			#if PATCH_CLASS_STACKING
			arrangemon(c->mon);
			#endif // PATCH_CLASS_STACKING
			if (!(h = nexttiled(c)))
				h = prevtiled(c);
		}
		focus(h, 0);
		h = selmon->sel;
	}
	arrange(selmon);
	drawbar(selmon, 0);

	#if PATCH_MOUSE_POINTER_WARPING
	if (h)
		#if PATCH_MOUSE_POINTER_WARPING_SMOOTH
		warptoclient(h, 1, 0);
		#else // NO PATCH_MOUSE_POINTER_WARPING_SMOOTH
		warptoclient(h, 0);
		#endif // PATCH_MOUSE_POINTER_WARPING_SMOOTH
	#endif // PATCH_MOUSE_POINTER_WARPING
}
#endif // PATCH_FLAG_HIDDEN

#if PATCH_ALTTAB
void
highlight(Client *c)
{
	#if PATCH_ALTTAB_HIGHLIGHT
	if (altTabMon->isAlt & ALTTAB_MOUSE || !tabHighlight)
	#endif // PATCH_ALTTAB_HIGHLIGHT
	{
		#if PATCH_SHOW_DESKTOP
		if (altTabMon->highlight && showdesktop) {
			Monitor *m = altTabMon->highlight->mon;
			if (m->showdesktop != m->altTabDesktop) {
				m->showdesktop = m->altTabDesktop;
				arrange(m);
			}
		}
		#endif // PATCH_SHOW_DESKTOP
		altTabMon->highlight = c;
		return;
	}

	#if PATCH_ALTTAB_HIGHLIGHT
	#if PATCH_MOUSE_POINTER_WARPING
	warptoclient_stop_flag = 1;
	#endif // PATCH_MOUSE_POINTER_WARPING
	
	XWindowChanges wc;
	Client *h = altTabMon->highlight;
	// unhighlight previous;
	if (h && h != c) {
		altTabMon->highlight = NULL;
		XSetWindowBorder(dpy, h->win, scheme[SchemeNorm][ColBorder].pixel);
		if (h->isfullscreen
			#if PATCH_FLAG_FAKEFULLSCREEN
			&& h->fakefullscreen != 1
			#endif // PATCH_FLAG_FAKEFULLSCREEN
		) {
			#if PATCH_FLAG_GAME
			if (h->isgame && h->isfullscreen) {
				destroybarrier();
				#if PATCH_FLAG_GAME_STRICT
				//if (h->isgamestrict || !c || c->mon != h->mon)
				if (h->isgamestrict || (h != game) || (c && c->mon == h->mon))
				#endif // PATCH_FLAG_GAME_STRICT
				minimize(h);
			}
			#endif // PATCH_FLAG_GAME
		}

		#if PATCH_CLIENT_OPACITY
		opacity(h, 0);
		#endif // PATCH_CLIENT_OPACITY


		if ((!c || c->mon != h->mon) && ISVISIBLE(h)) {
			Monitor *m = h->mon;
			#if PATCH_SHOW_DESKTOP
			if (showdesktop && m->showdesktop != m->altTabDesktop) {
				m->showdesktop = m->altTabDesktop;
				arrange(m);
			}
			else
			#endif // PATCH_SHOW_DESKTOP
			drawbar(m, 0);
		}
	}

	// highlight c;
	if (c
		#if PATCH_FLAG_HIDDEN
		&& !c->ishidden
		#endif // PATCH_FLAG_HIDDEN
		)
	{
		if (!ISVISIBLE(c))
			viewmontag(c->mon, c->tags, 1);
		#if PATCH_CLASS_STACKING
		else if (c->mon != selmon)
			arrangemon(c->mon);
		if (c->mon->class_stacking && (c->isstackhead || c->stackhead))
			XSetWindowBorder(dpy, c->win, scheme[SchemeUrg][ColBorder].pixel);
		else
		#endif // PATCH_CLASS_STACKING
		XSetWindowBorder(dpy, c->win, scheme[SchemeSel][ColBorder].pixel);

		#if PATCH_SHOW_DESKTOP
		if (showdesktop) {
			int isdesktop = (c->isdesktop || c->ondesktop);
			if (c->mon->showdesktop != isdesktop) {
				c->mon->showdesktop = isdesktop;
				arrange(c->mon);
			}
		}
		#endif // PATCH_SHOW_DESKTOP

		#if PATCH_CLIENT_OPACITY
		setopacity(c, 0);
		#endif // PATCH_CLIENT_OPACITY

		raisewin(altTabMon, altTabMon->tabwin, True);

		if (c->isfullscreen) {
			#if PATCH_FLAG_GAME
			if (c->isgame) {
				unminimize(c);
			}
			#endif // PATCH_FLAG_GAME
			#if PATCH_FLAG_FAKEFULLSCREEN
			if (c->fakefullscreen != 1)
			#endif // PATCH_FLAG_FAKEFULLSCREEN
			resizeclient(c, c->mon->mx, c->mon->my, c->mon->mw, c->mon->mh, 0);
		}
		else {
			#if PATCH_FLAG_GAME
			destroybarrier();
			#endif // PATCH_FLAG_GAME
		}

		altTabMon->highlight = c;
		drawbar(c->mon, 0);

		#if PATCH_SHOW_DESKTOP
		if (!showdesktop || !c->isdesktop)
		#endif // PATCH_SHOW_DESKTOP
		{
			wc.stack_mode = Below;
			if (altTabMon == c->mon) {
				// if the client is on the alt-tab monitor,
				// place the bar window just below the alt-tab switcher;
				wc.sibling = altTabMon->tabwin;
				XConfigureWindow(dpy, c->win, CWSibling|CWStackMode, &wc);
				XConfigureWindow(dpy, altTabMon->barwin, CWSibling|CWStackMode, &wc);
			}
			else {
				// otherwise, raise the bar window, and place the client just below it;
				raisewin(c->mon, c->mon->barwin, True);
				wc.sibling = c->mon->barwin;
				XConfigureWindow(dpy, c->win, CWSibling|CWStackMode, &wc);

				#if PATCH_CLASS_STACKING
				// without the restack() class-stacked clients' z-order doesn't get readjusted properly;
				if (altTabMon && altTabMon->highlight && (altTabMon->highlight->stackhead || altTabMon->highlight->isstackhead)) {
					restack(c->mon);
				}
				#endif // PATCH_CLASS_STACKING
			}
		}
	}
	else
		altTabMon->highlight = NULL;
	#endif // PATCH_ALTTAB_HIGHLIGHT
}
#endif // PATCH_ALTTAB

void
incnmaster(const Arg *arg)
{
	#if PATCH_FOCUS_FOLLOWS_MOUSE
	checkmouseovermonitor(selmon);
	#endif // PATCH_FOCUS_FOLLOWS_MOUSE
	#if PATCH_SHOW_DESKTOP
	if (showdesktop && selmon->showdesktop)
		return;
	#endif // PATCH_SHOW_DESKTOP
	#if PATCH_PERTAG
	selmon->nmaster = selmon->pertag->nmasters[selmon->pertag->curtag] = MAX(selmon->nmaster + arg->i, 0);
	#else // NO PATCH_PERTAG
	selmon->nmaster = MAX(selmon->nmaster + arg->i, 0);
	#endif // PATCH_PERTAG
	arrange(selmon);
}

#if PATCH_DRAG_FACTS
int
ismaster(Client *c)
{
	if (!c || !c->mon || !c->mon->nmaster || !ISVISIBLE(c))
		return 0;

	int nmaster = 0;
	Client *cc = c->mon->clients;
	for (; cc; cc = cc->next) {
		if (ISVISIBLE(cc)
			#if PATCH_FLAG_HIDDEN
			&& !cc->ishidden
			#endif // PATCH_FLAG_HIDDEN
			#if PATCH_FLAG_IGNORED
			&& !cc->isignored
			#endif // PATCH_FLAG_IGNORED
			#if PATCH_FLAG_PANEL
			&& !cc->ispanel
			#endif // PATCH_FLAG_PANEL
		) {
			nmaster++;
			if (c == cc)
				break;
		}
	}
	return (cc && nmaster <= c->mon->nmaster) ? 1 : 0;
}
#endif // PATCH_DRAG_FACTS

#if PATCH_MODAL_SUPPORT
int
ismodalparent(Client *c)
{
	Client *s;

	if (c) {
		for (s = c->mon->stack; s; s = s->snext)
			if (s->ultparent == c->ultparent && s->ismodal && s->index > c->index) {
				if (!ISVISIBLE(s)
					#if PATCH_FLAG_HIDDEN
					|| s->ishidden
					#endif // PATCH_FLAG_HIDDEN
				)
					continue;
				return 1;
			}
	}
	return 0;
}
#endif // PATCH_MODAL_SUPPORT

#if PATCH_FOCUS_FOLLOWS_MOUSE
int
ismouseoverclient(Client *c)
{
	int x, y;
	if (!getrootptr(&x, &y))
		return 0;

	Client *r = getclientatcoords(x, y, 0);
	return (r == c ? 1 : 0);
}
#endif // PATCH_FOCUS_FOLLOWS_MOUSE

#ifdef XINERAMA
static int
isuniquegeom(XineramaScreenInfo *unique, size_t n, XineramaScreenInfo *info)
{
	while (n--)
		if (unique[n].x_org == info->x_org && unique[n].y_org == info->y_org
		&& unique[n].width == info->width && unique[n].height == info->height)
			return 0;
	return 1;
}
#endif /* XINERAMA */

int
keycode_to_modifier(XModifierKeymap *modmap, KeyCode keycode)
{
	int i = 0, j = 0, max = modmap->max_keypermod;

	for (i = 0; i < 8; i++) // 8 modifier types, per XGetModifierMapping(3X);
		for (j = 0; j < max && modmap->modifiermap[(i * max) + j]; j++)
			if (keycode == modmap->modifiermap[(i * max) + j])
				switch (i) {
					case ShiftMapIndex: return ShiftMask; break;
					case LockMapIndex: return LockMask; break;
					case ControlMapIndex: return ControlMask; break;
					case Mod1MapIndex: return Mod1Mask; break;
					case Mod2MapIndex: return Mod2Mask; break;
					case Mod3MapIndex: return Mod3Mask; break;
					case Mod4MapIndex: return Mod4Mask; break;
					case Mod5MapIndex: return Mod5Mask; break;
				}

	// No modifier found for this keycode, return no mask;
	return 0;
}

void
keypress(XEvent *e)
{
	unsigned int i;
	KeySym keysym;
	XKeyEvent *ev;

	ev = &e->xkey;
	keysym = XKeycodeToKeysym(dpy, (KeyCode)ev->keycode, 0);
	#if PATCH_KEY_HOLD
	for (i = 0; i < LENGTH(keys); i++)
		if (keysym == keys[i].keysym
		&& CLEANMASK(keys[i].mod) == CLEANMASK(ev->state)
		&& keys[i].func) {
			#if PATCH_TORCH
			if (torchwin && keys[i].func && keys[i].func != toggletorch)
				break;
			#endif // PATCH_TORCH
			if (keys[i].mod & ModKeyHoldMask) {
				keyholdstate = CLEANMASK(keys[i].mod);
				keyholdsym = keysym;
				keyholdclient = selmon->sel;
			}
		}
	#endif // PATCH_KEY_HOLD
	for (i = 0; i < LENGTH(keys); i++)
		if (keysym == keys[i].keysym
		&& CLEANMASK(keys[i].mod) == CLEANMASK(ev->state)
		&& keys[i].func) {
			#if PATCH_TORCH
			if (torchwin && keys[i].func && keys[i].func != toggletorch)
				break;
			#endif // PATCH_TORCH
			#if PATCH_KEY_HOLD
			if (!(keys[i].mod & ModKeyHoldMask))
			#endif // PATCH_KEY_HOLD
			keys[i].func(&(keys[i].arg));
		}
}

#if PATCH_ALT_TAGS || PATCH_KEY_HOLD
void
keyrelease(XEvent *e)
{
	unsigned int i;
	int skipevent = 0;
	KeySym keysym;
	XKeyEvent *ev;

	ev = &e->xkey;
	keysym = XKeycodeToKeysym(dpy, (KeyCode)ev->keycode, 0);

    for (i = 0; i < LENGTH(keys); i++) {
		if (
			#if PATCH_ALT_TAGS
			!(
				keys[i].func && keys[i].func == togglealttags &&
				(
					keysym == keys[i].keysym ||
					CLEANMASK(keys[i].mod) == CLEANMASK(ev->state)
				)
			) &&
			#endif // PATCH_ALT_TAGS
			(
				keysym != keys[i].keysym ||
				CLEANMASK(keys[i].mod) != CLEANMASK(ev->state)
			))
			continue;
		if ((keys[i].mod & ModKeyNoRepeatMask))
			skipevent = skipnextkeyevent(KeyPress, ev->keycode, CLEANMASK(ev->state), ev->serial);
		#if PATCH_ALT_TAGS
        if (!skipevent && keys[i].func && keys[i].func == togglealttags) {
			if (selmon->alttags)
            	keys[i].func(&(keys[i].arg));
			else
				for (Monitor *m = mons; m; m = m->next) {
					m->alttags = 0;
					drawbar(m, 0);
				}
		}
		#endif // PATCH_ALT_TAGS
		#if PATCH_KEY_HOLD
		if (skipevent && (keys[i].mod & ModKeyHoldMask)
			&& keysym == keyholdsym
			&& CLEANMASK(keys[i].mod) == keyholdstate
			) {
			keyholdsym = 0;
			keyholdstate = 0;
			if (keyholdclient && keys[i].func)
				keys[i].func(&(keys[i].arg));
			keyholdclient = NULL;
		}
		#endif // PATCH_KEY_HOLD
	}
	#if PATCH_KEY_HOLD
	if (!skipevent)
		keyholdclient = NULL;
	#endif // PATCH_KEY_HOLD
}
#endif // PATCH_ALT_TAGS || PATCH_KEY_HOLD

void
killclient(const Arg *arg)
{
	#if PATCH_FOCUS_FOLLOWS_MOUSE
	checkmouseoverclient();
	#endif // PATCH_FOCUS_FOLLOWS_MOUSE
	Client *c;
	if (!selmon || !(c = selmon->sel))
		return;
	if (arg->ui > 1) {
		if (!(c = wintoclient(arg->ui)))
			return;
	}
	#if PATCH_HANDLE_SIGNALS
	if (arg->ui == 0) {
		c->sigtermcount = 0;
		if (procparents) {
			int cnt = cJSON_GetArraySize(procparents);
			for (int i = 0; i < cnt; ++i) {
				cJSON *pp = cJSON_GetArrayItem(procparents, i);
				if (!pp)
					continue;
				cJSON *pp_count = cJSON_GetObjectItemCaseSensitive(pp, "sigtermcount");
				if (pp_count)
					cJSON_SetIntValue(pp_count, 0L);
			}
		}
	}
	#endif // PATCH_HANDLE_SIGNALS
	killclientex(c, (arg->ui == 0 ? 1 : 0));
}
void
killclientex(Client *c, int sigterm)
{
	if (!c)
		return;
	char buffer[256];
	char *params = NULL, *procname = NULL;
	pid_t pid = c->pid;
	int gotname = 0;
	int do_sigterm = sigterm;
	if (do_sigterm) {
		if ((gotname = getprocname(c->pid, buffer, sizeof(buffer), &procname, &params))) {
			int cnt = 0;
			if (badprocs && (cnt = cJSON_GetArraySize(badprocs))) {
				for (int i = 0; i < cnt; ++i) {
					cJSON *badproc = cJSON_GetArrayItem(badprocs, i);
					char *string = cJSON_GetStringValue(badproc);
					if (!string)
						continue;
					if (strcmp(string, buffer) == 0 || strcmp(string, procname) == 0 ||
						(strchr(string, ' ') && params && strstr(string, buffer) == string && strstr(params, string + strlen(buffer) + 1))
					) {
						logdatetime(stderr);
						fprintf(stderr, "dwm: found procname listed in process-no-sigterm: \"%s\"; will send WM_DELETE, not SIGTERM\n", buffer);
						do_sigterm = 0;
						break;
					}
				}
			}
			if (do_sigterm && procparents && (cnt = cJSON_GetArraySize(procparents))) {
				for (int i = 0; i < cnt; ++i) {
					cJSON *pp = cJSON_GetArrayItem(procparents, i);
					if (!pp)
						continue;
					cJSON *pp_name = cJSON_GetObjectItemCaseSensitive(pp, "procname");
					cJSON *pp_parent = cJSON_GetObjectItemCaseSensitive(pp, "parent");
					#if PATCH_HANDLE_SIGNALS
					cJSON *pp_count = cJSON_GetObjectItemCaseSensitive(pp, "sigtermcount");
					if (!pp_count)
						pp_count = cJSON_AddIntegerToObject(pp, "sigtermcount", 0L);
					#endif // PATCH_HANDLE_SIGNALS
					if (pp_name && cJSON_IsString(pp_name) && pp_parent && cJSON_IsString(pp_parent)) {
						char *string = cJSON_GetStringValue(pp_name);
						if (strcmp(string, buffer) == 0 || strcmp(string, procname) == 0 ||
							(strchr(string, ' ') && params && strstr(string, buffer) == string && strstr(params, string + strlen(buffer) + 1))
						) {
							logdatetime(stderr);
							fprintf(stderr, "dwm: found procname listed in process-parents: \"%s\"; ", buffer);
							string = cJSON_GetStringValue(pp_parent);
							pid = getprocessid(string);
							if (pid) {
								#if PATCH_HANDLE_SIGNALS
								if (pp_count->valueint) {
									fprintf(stderr, "skipping, already sent SIGTERM to replacement process \"%s\" (pid:%u)\n", string, pid);
									return;
								}
								else
									cJSON_SetIntValue(pp_count, 1);
								#endif // PATCH_HANDLE_SIGNALS
								fprintf(stderr, "will send SIGTERM to replacement process \"%s\" (pid:%u)\n", string, pid);
								break;
							}
							else
								fprintf(stderr, "unable to find replacement pid for process \"%s\"\n", string);
						}
					}
				}
			}
		}
	}
	#if PATCH_HANDLE_SIGNALS
	if (sigterm) {
		if (do_sigterm && c->ultparent && c != c->ultparent) {
			if (c->ultparent->sigtermcount) {
				logdatetime(stderr);
				fprintf(stderr, "dwm: skipping, ultimate parent \"%s\" already flagged in this pass\n", c->ultparent->name);
				return;
			}
			else
				c->ultparent->sigtermcount++;
		}
		else {
			if (c->sigtermcount) {
				logdatetime(stderr);
				fprintf(stderr, "dwm: skipping, client \"%s\" already flagged in this pass\n", c->name);
				return;
			}
			else
				c->sigtermcount++;
		}
	}
	#endif // PATCH_HANDLE_SIGNALS
	#if PATCH_FLAG_PAUSE_ON_INVISIBLE
	if (c->pauseinvisible == -1 && c->pid) {
		if (do_sigterm && pid) {
			// just kill a paused process
			logdatetime(stderr);
			if (gotname)
				fprintf(stderr, "dwm: sending SIGKILL to paused process %u (procname: %s) for client \"%s\"\n", pid, procname, c->name);
			else
				fprintf(stderr, "dwm: sending SIGKILL to paused process %u for client \"%s\"\n", pid, c->name);
			kill (pid, SIGKILL);
			return;
		}
		kill (c->pid, SIGCONT);
		c->pauseinvisible = 1;
		#if PATCH_PAUSE_PROCESS
		c->paused = 0;
		#endif // PATCH_PAUSE_PROCESS
		DEBUG("client continued: \"%s\".\n", c->name);
	}
	#if PATCH_PAUSE_PROCESS
	else
	#endif // PATCH_PAUSE_PROCESS
	#endif // PATCH_FLAG_PAUSE_ON_INVISIBLE
	#if PATCH_PAUSE_PROCESS
	if (c->paused) {
		if (do_sigterm && pid) {
			// just kill a paused process
			logdatetime(stderr);
			if (gotname)
				fprintf(stderr, "dwm: sending SIGKILL to paused process %u (procname: %s) for client \"%s\"\n", pid, procname, c->name);
			else
				fprintf(stderr, "dwm: sending SIGKILL to paused process %u for client \"%s\"\n", pid, c->name);
			kill (pid, SIGKILL);
			return;
		}
		kill (c->pid, SIGCONT);
		c->paused = 0;
	}
	#endif // PATCH_PAUSE_PROCESS
	if (do_sigterm) {
		if (pid) {
			logdatetime(stderr);
			if (gotname)
				fprintf(stderr, "dwm: sending SIGTERM to process %d (procname: %s) for client \"%s\"\n", pid, procname, c->name);
			else
				fprintf(stderr, "dwm: sending SIGTERM to process %d for client \"%s\"\n", pid, c->name);
			kill (pid, SIGTERM);
		}
		return;
	}
	#if PATCH_CROP_WINDOWS
	if (c->crop)
		cropdelete(c);
	#endif // PATCH_CROP_WINDOWS
	#if PATCH_FLAG_GAME
	#if PATCH_FLAG_GAME_STRICT
	if (c == game)
		game = NULL;
	#endif // PATCH_FLAG_GAME_STRICT
	#endif // PATCH_FLAG_GAME
	killwin(c->win);
}
void
killgroup(const Arg *arg)
{
	if (!arg || !(arg->ui & (KILLGROUP_BY_NAME | KILLGROUP_BY_CLASS | KILLGROUP_BY_INSTANCE)))
		return;
	#if PATCH_FOCUS_FOLLOWS_MOUSE
	checkmouseoverclient();
	#endif // PATCH_FOCUS_FOLLOWS_MOUSE
	if (!selmon->sel)
		return;

	Monitor *m;
	Client **list;
	Client *c = selmon->sel;
	XClassHint sel_ch = { NULL, NULL };
	XClassHint ch = { NULL, NULL };
	const char *sel_class = broken, *sel_instance = broken;
	const char *class, *instance;
	const char *sel_name = broken;
	// if the selected client has a class-group rule, then ignore instance;
	// and match other clients with the matching class-group rule;
	#if PATCH_ALTTAB
	int usegrp = c->grpclass ? 1 : 0;
	#endif // PATCH_ALTTAB

	if (arg->ui & KILLGROUP_BY_NAME)
		sel_name = c->name;
	if (arg->ui & (KILLGROUP_BY_CLASS | KILLGROUP_BY_INSTANCE)) {
		#if PATCH_ALTTAB
		if (usegrp) {
			sel_class = c->grpclass;
			sel_instance = broken;
		}
		else
		#endif // PATCH_ALTTAB
		{
			XGetClassHint(dpy, c->win, &sel_ch);
			sel_class    = sel_ch.res_class ? sel_ch.res_class : broken;
			sel_instance = sel_ch.res_name  ? sel_ch.res_name  : broken;
		}
	}
	int n = 0;
	for (m = mons; m; m = m->next)
		for (c = m->clients; c; c = c->next)
			#if PATCH_ALTTAB
			if (!usegrp || c->grpclass) n++;
			#else // NO PATCH_ALTTAB
			n++;
			#endif // PATCH_ALTTAB

	list = (Client **) malloc(n * sizeof(Client *));
	int i = 0;
	for (m = mons; m; m = m->next)
		for (c = m->clients; c; c = c->next) {
			#if PATCH_ALTTAB
			if (usegrp && !c->grpclass) continue;
			if (usegrp) {
				class = c->grpclass ? c->grpclass : broken;
				instance = broken;
			}
			else {
			#else // NO PATCH_ALTTAB
			{
			#endif // PATCH_ALTTAB
				XGetClassHint(dpy, c->win, &ch);
				class    = ch.res_class ? ch.res_class : broken;
				instance = ch.res_name  ? ch.res_name  : broken;
			}
			if ((!(arg->ui & KILLGROUP_BY_NAME) || strcmp(sel_name, c->name)==0) &&
				(!(arg->ui & KILLGROUP_BY_CLASS) || strcmp(sel_class, class)==0) &&
				(!(arg->ui & KILLGROUP_BY_INSTANCE) || strcmp(sel_instance, instance)==0))
				list[i++] = c;
			else
				n--;

			if (ch.res_class)
				XFree(ch.res_class);
			if (ch.res_name)
				XFree(ch.res_name);
		}

	if (sel_ch.res_class)
		XFree(sel_ch.res_class);
	if (sel_ch.res_name)
		XFree(sel_ch.res_name);

	// now doing the killings;
	for (i = 0; i < n; i++)
		if ((c = list[i])) {
			#if PATCH_FLAG_PAUSE_ON_INVISIBLE
			if (c->pauseinvisible == -1 && c->pid) {
				kill (c->pid, SIGCONT);
				c->pauseinvisible = 1;
				#if PATCH_PAUSE_PROCESS
				c->paused = 0;
				#endif // PATCH_PAUSE_PROCESS
				DEBUG("client continued: \"%s\".\n", c->name);
			}
			#if PATCH_PAUSE_PROCESS
			else
			#endif // PATCH_PAUSE_PROCESS
			#endif // PATCH_FLAG_PAUSE_ON_INVISIBLE
			#if PATCH_PAUSE_PROCESS
			if (c->paused) {
				kill (c->pid, SIGCONT);
				c->paused = 0;
			}
			#endif // PATCH_PAUSE_PROCESS
			killwin(c->win);
		}

	free(list);
}
void
killwin(Window w)
{
	if (!w)
		return;
	if (!sendevent(w, wmatom[WMDelete], NoEventMask, wmatom[WMDelete], CurrentTime, 0 , 0, 0)) {
		XGrabServer(dpy);
		XSetErrorHandler(xerrordummy);
		XSetCloseDownMode(dpy, DestroyAll);
		XKillClient(dpy, w);
		XSync(dpy, False);
		XSetErrorHandler(xerror);
		XUngrabServer(dpy);
	}
}

#if PATCH_MOUSE_POINTER_WARPING
#if PATCH_MOUSE_POINTER_WARPING_RECALL
void
lastcoordsrecall(Client *c, int reset, int relative, int *px, int *py)
{
	int x = c->w/2;
	int y = c->h/2;

	if (c->nolastcoords || reset) {
		if (c->focusabs) {
			x = c->focusdx + (c->focusdx < 0 ? c->w : 0);
			y = c->focusdy + (c->focusdy < 0 ? c->h : 0);
		} else {
			if (c->focusdx != 1.0f)
				x = (int) ((float) c->focusdx * (int) c->w/2 + (c->focusdx < 0 ? c->w : 0));
			if (c->focusdy != 1.0f)
				y = (int) ((float) c->focusdy * (int) c->h/2 + (c->focusdy < 0 ? c->w : 0));
		}
	}
	else {
		x = c->lastdx + (c->lastdx < 0 ? c->w : 0);
		y = c->lastdy + (c->lastdy < 0 ? c->h : 0);
	}

	if (x < 0 || x > c->w)
		x = c->w/2;
	if (y < 0 || y > c->h)
		y = c->h/2;

	c->lastdx = x;
	c->lastdy = y;
	c->nolastcoords = 0;

	if (relative) {
		*px = x;
		*px = y;
	}
	else {
		*px = x + c->x + c->bw;
		*py = y + c->y + c->bw;
	}
}

void
lastcoordsstore(Client *c)
{
	int px, py;
	if (!getrelativeptrex(c, &px, &py))
		return;

	// store existing coordinates;
	if (px < 0 || px > c->w || py < 0 || py > c->h)
		return;

	c->lastdx = px;
	c->lastdy = py;
	c->nolastcoords = 0;
}
#endif // PATCH_MOUSE_POINTER_WARPING_RECALL
#endif // PATCH_MOUSE_POINTER_WARPING

#if PATCH_LOG_DIAGNOSTICS
void
logdiagnostics_stacktiled(Monitor *m, const char *title, const char *indent)
{
	Client *c;
	unsigned int count = 0;

	fprintf(stderr, "\n%s%s\n", indent, title);
	for (c = m->stack; c; c = c->snext) {

		if (c == c->sprev) {
			fprintf(stderr, "%sCRITICAL: Stack is corrupted at previous client!\n", indent);
			break;
		}

		if  (c->isfloating)
			continue;

		count++;

		logdiagnostics_client_common(c, indent, "    ");
		fprintf(stderr, " (index:%i)", c->index);

		#if PATCH_CFACTS
		if (c->cfact > 0 && c->cfact != 1.0f)
			fprintf(stderr, " cfact:%f", c->cfact);
		#endif // PATCH_CFACTS

		fprintf(stderr, " (XQuery order: %li)", c->stackorder);

		fprintf(stderr, "\n");

	}
	if (!count)
		fprintf(stderr, "%s    <none>\n", indent);
}

void
logdiagnostics_stackfloating(Monitor *m, const char *title, const char *indent)
{
	Client *c;
	unsigned int count = 0;

	fprintf(stderr, "\n%s%s\n", indent, title);
	for (c = m->clients; (c && c->snext); c = c->next);

	for (; c; c = c->sprev) {

		if (c == c->snext) {
			fprintf(stderr, "%sCRITICAL: Stack is corrupted at previous client!\n", indent);
			break;
		}

		if (!c->isfloating)
			continue;

		count++;

		logdiagnostics_client_common(c, indent, "    ");
		fprintf(stderr, " (index:%i)", c->index);

		fprintf(stderr, " (XQuery order: %li)", c->stackorder);

		fprintf(stderr, "\n");

	}
	if (!count)
		fprintf(stderr, "%s    <none>\n", indent);
}

int
logdiagnostics_event(XEvent ev)
{
	int retval = 1;
	DEBUGIF

	Client *c, *c2;

	switch (ev.type) {
		case ButtonPress:
		case ButtonRelease:
		break;
			logdatetime(stderr);
			fprintf(stderr, "debug: ButtonPress\n");
			break;
		case ClientMessage:
		break;
			if (log_ev_no_root && ev.xclient.window == root)
				return retval;
			logdatetime(stderr);
			fprintf(stderr, "debug: ClientMessage");
			/*
			if (ev.xclient.message_type == netatom[NetWMState])
				fputs(" NetWMState", stderr);
			else if (ev.xclient.message_type == netatom[NetActiveWindow])
			*/
			if ((c = wintoclient(ev.xclient.window)))
				fprintf(stderr, " Client:%s\n", c->name);
			else fprintf(stderr, " Win:%#10lx\n", ev.xclient.window);
			break;
		case ConfigureRequest:
			if (log_ev_no_root && ev.xconfigurerequest.window == root)
				return retval;
			logdatetime(stderr);
			fprintf(stderr, "debug: ConfigureRequest(");
			int b = 0;
			if (ev.xconfigurerequest.value_mask & CWBorderWidth) {
				fprintf(stderr, "CWBorderWidth:%i", ev.xconfigurerequest.border_width);
				b = 1;
			}
			if (ev.xconfigurerequest.value_mask & CWWidth) {
				fprintf(stderr, "%sCWWidth:%i", b ? " | " : "", ev.xconfigurerequest.width);
				b = 1;
			}
			if (ev.xconfigurerequest.value_mask & CWHeight) {
				fprintf(stderr, "%sCWHeight:%i", b ? " | " : "", ev.xconfigurerequest.height);
				b = 1;
			}
			if (ev.xconfigurerequest.value_mask & CWX) {
				fprintf(stderr, "%sCWX:%i", b ? " | " : "", ev.xconfigurerequest.x);
				b = 1;
			}
			if (ev.xconfigurerequest.value_mask & CWY) {
				fprintf(stderr, "%sCWY:%i", b ? " | " : "", ev.xconfigurerequest.y);
				b = 1;
			}
			if (ev.xconfigurerequest.value_mask & CWSibling) {
				if (!ev.xconfigurerequest.above)
					fprintf(stderr, "%sCWSibling:None", b ? " | " : "");
				else {
					c2 = wintoclient(ev.xconfigurerequest.above);
					if (c2 && c2->name != broken)
						fprintf(stderr, "%sCWSibling:\"%s\"", b ? " | " : "", c2->name);
					else if (c2)
						fprintf(stderr, "%sCWSibling:0x%lx(\"%s\")", b ? " | " : "", c2->win, c2->name);
					else
						fprintf(stderr, "%sCWSibling:0x%lx", b ? " | " : "", c2->win);
				}
				b = 1;
			}
			if (ev.xconfigurerequest.value_mask & CWStackMode) {
				switch (ev.xconfigurerequest.detail) {
					case Above:
						fputs(":Above", stderr);
						break;
					case Below:
						fputs(":Below", stderr);
						break;
					case TopIf:
						fputs(":TopIf", stderr);
						break;
					case BottomIf:
						fputs(":BottomIf", stderr);
						break;
					case Opposite:
						fputs(":Opposite", stderr);
						break;
					default:
						fputs(":unknown", stderr);
				}
				b = 1;
			}
			if (!b)
				fprintf(stderr, "0x%lx", ev.xconfigurerequest.value_mask);
			fputs(")", stderr);
			if ((c = wintoclient(ev.xconfigurerequest.window)))
				fprintf(stderr, " Client:%s\n", c->name);
			else fprintf(stderr, " Win:%#10lx\n", ev.xconfigurerequest.window);
			break;
		case ConfigureNotify:
			break;
			if (log_ev_no_root && ev.xconfigure.window == root)
				return retval;
			logdatetime(stderr);
			fprintf(stderr, "debug: ConfigureNotify");
			if ((c = wintoclient(ev.xconfigure.window)))
				fprintf(stderr, " Client:%s\n", c->name);
			else fprintf(stderr, " Win:%#10lx\n", ev.xconfigure.window);
			break;
		case DestroyNotify:
			if (log_ev_no_root && ev.xdestroywindow.window == root)
				return retval;
			logdatetime(stderr);
			fprintf(stderr, "debug: DestroyNotify");
			if ((c = wintoclient(ev.xdestroywindow.window)))
				fprintf(stderr, " Client:%s\n", c->name);
			else fprintf(stderr, " Win:%#10lx\n", ev.xdestroywindow.window);
			break;
		case EnterNotify:
			if (log_ev_no_root && ev.xcrossing.window == root)
				return retval;
			logdatetime(stderr);
			fprintf(stderr, "debug: EnterNotify");
			if ((c = wintoclient(ev.xcrossing.window)))
				fprintf(stderr, " Client:%s\n", c->name);
			else fprintf(stderr, " Win:%#10lx\n", ev.xcrossing.window);
			break;
		case Expose:
			if (log_ev_no_root && ev.xexpose.window == root)
				return retval;
			logdatetime(stderr);
			fprintf(stderr, "debug: Expose");
			if ((c = wintoclient(ev.xexpose.window)))
				fprintf(stderr, " Client:%s\n", c->name);
			else fprintf(stderr, " Win:%#10lx\n", ev.xexpose.window);
			break;
		case FocusIn:
		break;
			logdatetime(stderr);
			fprintf(stderr, "debug: FocusIn\n");
			break;
		case KeyPress:
		break;
			logdatetime(stderr);
			fprintf(stderr, "debug: KeyPress\n");
			break;
		case KeyRelease:
		break;
			logdatetime(stderr);
			fprintf(stderr, "debug: KeyRelease\n");
			break;
		case MappingNotify:
			logdatetime(stderr);
			fprintf(stderr, "debug: MappingNotify Win:%#10lx\n", ev.xmapping.window);
			break;
		case MapRequest:
			logdatetime(stderr);
			fprintf(stderr, "debug: MapRequest Win:%#10lx\n", ev.xmaprequest.window);
			break;
		case MotionNotify:
		break;
			logdatetime(stderr);
			fprintf(stderr, "debug: MotionNotify\n");
			break;
		case PropertyNotify:
			if (log_ev_no_root && ev.xproperty.window == root)
				return retval;
			if (!(ev.xproperty.window == root) && (ev.xproperty.atom == XA_WM_NAME)) {
				logdatetime(stderr);
				fprintf(stderr, "debug: PropertyNotify");
				if ((c = wintoclient(ev.xproperty.window)))
					fprintf(stderr, " Client:%s\n", c->name);
				else fprintf(stderr, " Win:%#10lx\n", ev.xproperty.window);
			}
			break;
		case ResizeRequest:
			if (log_ev_no_root && ev.xresizerequest.window == root)
				return retval;
			logdatetime(stderr);
			fprintf(stderr, "debug: ResizeRequest");
			if ((c = wintoclient(ev.xresizerequest.window)))
				fprintf(stderr, " Client:%s\n", c->name);
			else fprintf(stderr, " Win:%#10lx\n", ev.xresizerequest.window);
			break;
		case UnmapNotify:
			if (log_ev_no_root && ev.xunmap.window == root)
				return retval;
			logdatetime(stderr);
			fprintf(stderr, "debug: UnmapNotify");
			if ((c = wintoclient(ev.xunmap.window)))
				fprintf(stderr, " Client:%s\n", c->name);
			else fprintf(stderr, " Win:%#10lx\n", ev.xunmap.window);
	}

	DEBUGENDIF
	return retval;
}

void
logdiagnostics_stack(Monitor *m, const char *title, const char *indent)
{
	Client *c;
	unsigned int count = 0;

	fprintf(stderr, "\n%s%s\n", indent, title);

	for (c = m->stack; c; c = c->snext) {

		if (c == c->sprev) {
			fprintf(stderr, "%sCRITICAL: Stack is corrupted at previous client!\n", indent);
			break;
		}

		count++;

		logdiagnostics_client_common(c, indent, "    ");
		fprintf(stderr, " (index:%i)\n", c->index);
	}

	if (!count)
		fprintf(stderr, "%s    <none>\n", indent);
}

void
logdiagnostics_client(Client *c, const char *indent)
{
	Monitor *m = c->mon;
	XClassHint ch = { NULL, NULL };
	const char *class, *instance;
	char role[64];

	XGetClassHint(dpy, c->win, &ch);
	class    = ch.res_class ? ch.res_class : broken;
	instance = ch.res_name  ? ch.res_name  : broken;
	gettextprop(c->win, wmatom[WMWindowRole], role, sizeof(role));

	logdiagnostics_client_common(c, indent, "    ");

	if (!c->w && !c->h && !c->x && !c->y)
		fprintf(stderr, " (no geometry yet) (pid:%u) ", c->pid);
	else
		fprintf(stderr, " (%ix%i+%ix%i:%ix%i) (pid:%u) ", c->w, c->h, c->x, c->y, (c->x - m->mx), (c->y - m->my), c->pid);
	if (strlen(role))
		fprintf(stderr, "role:\"%s\" ", role);
	else
		fputs("role:<none> ", stderr);

	fprintf(stderr,
		"(\"%s\", \"%s\"",
		instance,
		class
	);
	#if PATCH_SHOW_MASTER_CLIENT_ON_TAG
	if (c->dispclass)
		fprintf(stderr, " displays:\"%s\"", c->dispclass);
	#endif // PATCH_SHOW_MASTER_CLIENT_ON_TAG
	fputs(")", stderr);

	if (c->parent)
		fprintf(stderr, " %sparent:\"%s\"", c->fosterparent ? "[foster]" : "", c->parent->name);
	else
		fputs(" parent:<none>", stderr);

	if (c->ultparent != c && c->ultparent != c->parent)
		fprintf(stderr, " ult-parent:\"%s\"", c->ultparent->name);

	if (c->monindex != -1 && c->mon->num != c->monindex)
		fprintf(stderr, " wants-monitor:%i", c->monindex);

	fputs("\n", stderr);

	if (ch.res_class)
		XFree(ch.res_class);
	if (ch.res_name)
		XFree(ch.res_name);

}

int
layoutstringtoindex(const char *layout)
{
	int i;
	for (i = 0; i < LENGTH(layouts); i++)
		if (strcmp(layout, layouts[i].symbol)==0)
			return i;
	return 0;
}

int
line_to_buffer(const char *text, char *buffer, size_t buffer_size, size_t line_length, size_t *index)
{
	size_t w;
	size_t pindex = *index;
	strncpy(buffer, text + *index,  buffer_size);

	while (text[*index] != '\0') {
		if (text[*index] == ' ') {
			w = 0;
			while (
				text[*index + w + 1] != ' ' &&
				text[*index + w + 1] != '\0' &&
				text[*index + w + 1] != '\n'
				)
				++w;
			if (*index - pindex + w >= line_length)
				buffer[*index - pindex] = '\n';
		}
		(*index)++;
		if (buffer[*index - pindex - 1] == '\n' || text[*index - 1] == '\n') {
			buffer[*index - pindex - 1] = '\0';
			return 0;
		}
	}
	return 1;
}

void
logdiagnostics(const Arg *arg)
{
	Client *c;
	char buffer[33];
	const char *flags[] = {
		#if PATCH_ATTACH_BELOW_AND_NEWMASTER
		"N: newmaster",
		#endif // PATCH_ATTACH_BELOW_AND_NEWMASTER
		"x: isfixed",
		"f: isfloating",
		#if PATCH_SHOW_DESKTOP
		"D: isdesktop",
		"d: ondesktop",
		#endif // PATCH_SHOW_DESKTOP
		#if PATCH_FLAG_GAME
		"g: isgame",
		#if PATCH_FLAG_GAME_STRICT
		"G: isgame with strict focus rules",
		#endif // PATCH_FLAG_GAME_STRICT
		#endif // PATCH_FLAG_GAME
		#if PATCH_TERMINAL_SWALLOWING
		"t: isterminal",
		#endif // PATCH_TERMINAL_SWALLOWING
		#if PATCH_FLAG_ALWAYSONTOP
		"a: alwaysontop",
		#endif // PATCH_FLAG_ALWAYSONTOP
		#if PATCH_MODAL_SUPPORT
		"m: ismodal",
		#endif // PATCH_MODAL_SUPPORT
		#if PATCH_FLAG_STICKY
		"s: issticky",
		#endif // PATCH_FLAG_STICKY
		#if PATCH_FLAG_HIDDEN
		"h: ishidden",
		#endif // PATCH_FLAG_HIDDEN
		#if PATCH_FLAG_IGNORED
		"i: isignored",
		#endif // PATCH_FLAG_IGNORED
		"n: neverfocus",
		#if PATCH_FLAG_PANEL
		"p: ispanel",
		#endif // PATCH_FLAG_PANEL
		#if PATCH_CLASS_STACKING
		"=: class stacked",
		#endif // PATCH_CLASS_STACKING
		"P: client has a parent",
		"u: client is an ultimate parent",
		"U: client is urgent",
		#if PATCH_FLAG_FOLLOW_PARENT
		"+: client follows parent",
		#endif // PATCH_FLAG_FOLLOW_PARENT
		#if PATCH_FLAG_CENTRED
		"c: iscentred (0, 1 monitor, 2 parent client)",
		#endif // PATCH_FLAG_CENTRED
		#if PATCH_FLAG_FAKEFULLSCREEN
		"f: fakefullscreen",
		#endif // PATCH_FLAG_FAKEFULLSCREEN
		"F: isfullscreen (0, 1)",
		#if PATCH_FLAG_GAME && PATCH_FLAG_GAME_STRICT
		"@: is a fullscreen game client",
		#endif // PATCH_FLAG_GAME && PATCH_FLAG_GAME_STRICT
		"*: is visible under the current tag selection"
	};
	fprintf(stderr, "--\n");
	logdatetime(stderr);
	fprintf(stderr, "dwm: Diagnostics:\n\nversion: "DWM_VERSION_STRING_LONG"\nbar height: %i\n", bh);
	#if PATCH_ALPHA_CHANNEL
	fprintf(stderr, "useargb: %i\n", useargb);
	#endif // PATCH_ALPHA_CHANNEL
	#if PATCH_WINDOW_ICONS
	#if PATCH_WINDOW_ICONS_ON_TAGS
	fprintf(stderr, "showiconsontags: %i\n", showiconsontags);
	#endif // PATCH_WINDOW_ICONS_ON_TAGS
	#endif // PATCH_WINDOW_ICONS

	#if PATCH_VIRTUAL_MONITORS
	fputs("\nPhysical monitors:\n", stderr);
	unsigned int pi = 0;
	for (PMonitor *pm = pmons; pm; pm = pm->next, pi++) {
		fprintf(stderr, "    %i: %i x %i + %i x %i: ", pi, pm->mw, pm->mh, pm->mx, pm->my);
		if (pm->mon2)
			fprintf(stderr, "contains virtual monitors %i: %i x %i + %i x %i and %i: %i x %i + %i x %i\n",
				pm->mon1->num, pm->mon1->mw, pm->mon1->mh, pm->mon1->mx, pm->mon1->my,
				pm->mon2->num, pm->mon2->mw, pm->mon2->mh, pm->mon2->mx, pm->mon2->my
			);
		else if (pm->mon1)
			fprintf(stderr, "contains virtual monitor %i: %i x %i + %i x %i\n",
				pm->mon1->num, pm->mon1->mw, pm->mon1->mh, pm->mon1->mx, pm->mon1->my);
		else
			fputs("<NO VIRTUAL MONITORS>\n", stderr);
	}
	fputs("\n", stderr);
	#endif // PATCH_VIRTUAL_MONITORS

	fprintf(stderr, "Client flags:\n");
	for (int i = 0; i < LENGTH(flags); i++)
		fprintf(stderr, "    %s\n", flags[i]);

	#if PATCH_FLAG_GAME && PATCH_FLAG_GAME_STRICT
	if (game) {
		fprintf(stderr, "\nVisible fullscreen game client on monitor %i:\n", game->mon->num);
		logdiagnostics_client(game, "    ");
	}
	else
		fprintf(stderr, "\nNo visible fullscreen game client\n");
	#endif // PATCH_FLAG_GAME && PATCH_FLAG_GAME_STRICT

	// update the XQuery order;
	getclientatcoords(-1, -1, 0);

	for (Monitor *m = mons; m; m = m->next) {

		if ((arg && !arg->ui) && m != selmon)
			continue;

		fprintf(stderr,
			"\nMonitor %u (%u x %u + %u x %u):\n    bar-elements:\n",
			m->num, m->mw, m->mh, m->mx, m->my
		);

		unsigned int len = 8;
		for (int i = 0, j; i < LENGTH(BarElementTypes); i++) {
			if (!BarElementTypes[i].name)
				continue;
			j = strlen(BarElementTypes[i].name);
			if (j > len)
				len = j;
		}
		len++;
		for (int i = 0, j; i < LENGTH(m->barlayout); i++) {
			BarElement e = m->bar[m->barlayout[i]];
			strncpy(buffer, "Unknown!", sizeof buffer);
			for (j = 0; j < LENGTH(BarElementTypes); j++)
				if (BarElementTypes[j].type == e.type) {
					strncpy(buffer, (BarElementTypes[j].name ? BarElementTypes[j].name : "NoElement"), sizeof buffer - 1);
					break;
				}
			for (j = 0; j < len-1; j++)
				if (buffer[j] == '\0')
					buffer[j] = ' ';
			buffer[j] = '\0';
			fprintf(stderr, "        type:%s    x:%-5i    width:%u\n", buffer, e.x, e.w);
		}

		#if PATCH_CLASS_STACKING
		fprintf(stderr, "    class-stacking: %i\n", m->class_stacking);
		#endif // PATCH_CLASS_STACKING
		#if PATCH_WINDOW_ICONS
		#if PATCH_WINDOW_ICONS_ON_TAGS
		fprintf(stderr, "    iconsontags   : %i\n", m->showiconsontags);
		#endif // PATCH_WINDOW_ICONS_ON_TAGS
		#endif // PATCH_WINDOW_ICONS

		fprintf(stderr,
			"    layout        : %s\n    mfact         : %f\n    nmaster       : %u\n",
			m->ltsymbol, m->mfact, m->nmaster
		);

		if (m->sel) {
			fprintf(stderr, "    selected      : ");
			logdiagnostics_client_common(m->sel, "", "");
			fprintf(stderr, "\n");
		}
		else
			fprintf(stderr, "    selected      : <none>\n");

		#if PATCH_SHOW_DESKTOP
			if (showdesktop) {
				fprintf(stderr, "    showdesktop   : %i", m->showdesktop);
				for (c = m->clients; c; c = c->next)
					if (c->isdesktop) {
						fprintf(stderr, ": ");
						logdiagnostics_client_common(c, "", "");
						break;
					}
				fprintf(stderr, "\n");
			}
		#endif // PATCH_SHOW_DESKTOP

		#if PATCH_SWITCH_TAG_ON_EMPTY
		fprintf(stderr, "    switch-to     : %u\n", m->switchonempty);
		#endif // PATCH_SWITCH_TAG_ON_EMPTY

		unsigned int seltags = m->tagset[m->seltags];
		unsigned int newtags;
		for (int i = LENGTH(tags); i >= 0; i--) {
			newtags = (seltags >> 1);
			buffer[i] = (((newtags << 1) == seltags) ? '0' : '1');
			seltags = newtags;
		}
		buffer[LENGTH(tags)+1] = '\0';

		fprintf(stderr,"    tags-mask     : %s [ ", buffer);
		seltags = m->tagset[m->seltags];
		for (int i = 0; i < LENGTH(tags); i++)
			if (seltags & (1 << i))
				fprintf(stderr, "%u ", (i + 1));

		fprintf(stderr, "]\n    tag-widths:\n");
		for (int i = 0; i < LENGTH(tags); i++)
			#if PATCH_HIDE_VACANT_TAGS
			if (m->alwaysvisible[i])
				fprintf(stderr, "        [%i] tagw[%i]: %u (always-visible)\n", (i + 1), i, m->tagw[i]);
			else
			#endif // PATCH_HIDE_VACANT_TAGS
			fprintf(stderr, "        [%i] tagw[%i]: %u\n", (i + 1), i, m->tagw[i]);

		#if PATCH_FLAG_PANEL
		#if PATCH_FLAG_FLOAT_ALIGNMENT
		fprintf(stderr,
			"    offsetx: %u\n"
			"    panelw : %u\n"
			, m->offsetx
			, m->panelw
		);
		#endif // PATCH_FLAG_FLOAT_ALIGNMENT
		#endif // PATCH_FLAG_PANEL

		#if PATCH_SYSTRAY
		fprintf(stderr,
			"    stw    : %u\n    statusw: %u\n",
			m->stw, m->bar[StatusText].w
		);
		#endif // PATCH_SYSTRAY

		fprintf(stderr, "    Focused on tag:");
		for (int i = 0; i < LENGTH(tags); i++) {
			fprintf(stderr, "\n        [%i] ", i + 1);
			if ((c = m->focusontag[i])) {
				if (validclient(c)) {
					logdiagnostics_client_common(c, "", "");
					continue;
				}
				m->focusontag[i] = NULL;
			}
			fputs("<none>", stderr);
		}

		fprintf(stderr, "\n    Clients:\n");

		checkstack(m);

		for (c = m->clients; c; c = c->next)
			logdiagnostics_client(c, "        ");

		logdiagnostics_stack(m, "Stack Order:", "    ");

		logdiagnostics_stacktiled(m, "Stack Order (Tiled):", "    ");

		logdiagnostics_stackfloating(m, "Reversed Stack Order (Floating):", "    ");

		#if PATCH_MOUSE_POINTER_WARPING || PATCH_FOCUS_FOLLOWS_MOUSE
		int x, y;
		if (m == selmon && m->sel && getrelativeptrex(m->sel, &x, &y)) {
			fprintf(stderr,
				"\n    Pointer relative position: %f, %f",
				(float) x / ((m->sel->w + m->sel->bw) / 2), (float) y / ((m->sel->h + m->sel->bw) / 2)
			);
			fprintf(stderr,
				" and %f, %f (\"%s\")\n",
				(float) -(m->sel->w + m->sel->bw - x) / ((m->sel->w + m->sel->bw) / 2), (float) -(m->sel->h + m->sel->bw - y) / ((m->sel->h + m->sel->bw) / 2),
				m->sel->name
			);
		}
		#endif // PATCH_MOUSE_POINTER_WARPING || PATCH_FOCUS_FOLLOWS_MOUSE

	}

	#if PATCH_SCAN_OVERRIDE_REDIRECTS
	fprintf(stderr, "\nOverride_redirect Clients:\n");
	if (orlist) {
		XClassHint ch = { NULL, NULL };
		const char *class, *instance;
		char role[64];
		for (c = orlist; c; c = c->next) {

			XGetClassHint(dpy, c->win, &ch);
			class    = ch.res_class ? ch.res_class : broken;
			instance = ch.res_name  ? ch.res_name  : broken;
			gettextprop(c->win, wmatom[WMWindowRole], role, sizeof(role));

			fprintf(stderr, "    %#10lx \"%s\" (pid:%u) ", c->win, c->name, c->pid);
			if (strlen(role))
				fprintf(stderr, "role:\"%s\" ", role);
			else
				fprintf(stderr, "role:<none> ");

			fprintf(stderr,
				"(\"%s\", \"%s\"",
				instance,
				class
			);
			fprintf(stderr, ")");

			if (c->parent)
				fprintf(stderr, " parent:\"%s\"", c->parent->name);
			else
				fprintf(stderr, " parent:<none>");

			if (c->ultparent != c && c->ultparent != c->parent)
				fprintf(stderr, " ult-parent:\"%s\"", c->ultparent->name);

			fprintf(stderr, "\n");

			if (ch.res_class)
				XFree(ch.res_class);
			if (ch.res_name)
				XFree(ch.res_name);

		}
	}
	else
		fprintf(stderr, "    <none>\n");
	#endif // PATCH_SCAN_OVERRIDE_REDIRECTS

	fprintf(stderr, "--\n");
}

void
logdiagnostics_client_common(Client *c, const char *indent1, const char *indent2)
{
	fprintf(stderr,
		"%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s "
		#if PATCH_FLAG_CENTRED
		"c%u "
		#endif // PATCH_FLAG_CENTRED
		#if PATCH_FLAG_FAKEFULLSCREEN
		"f%u "
		#endif // PATCH_FLAG_FAKEFULLSCREEN
		"F%s %#10lx %s [ ",
		indent1,
		#if PATCH_ATTACH_BELOW_AND_NEWMASTER
		c->newmaster ? "N" : "_",
		#else // NO PATCH_ATTACH_BELOW_AND_NEWMASTER
		"",
		#endif // PATCH_ATTACH_BELOW_AND_NEWMASTER
		(c->isfixed ? "x" : "_"),
		(
			(c->isfullscreen
			#if PATCH_FLAG_FAKEFULLSCREEN
			&& c->fakefullscreen != 1
			#endif // PATCH_FLAG_FAKEFULLSCREEN
			? (c->oldstate & ~(1 << 1)) : c->isfloating)
			? "f" : "_"
		),
		#if PATCH_FLAG_GAME
			#if PATCH_TERMINAL_SWALLOWING
				#if PATCH_SHOW_DESKTOP
				(
					c->isdesktop ? "D" :
					c->ondesktop ? "d" :
					c->isgame ?
					#if PATCH_FLAG_GAME_STRICT
					(c->isgamestrict ? "G" : "g")
					#else // NO PATCH_FLAG_GAME_STRICT
					"g"
					#endif // PATCH_FLAG_GAME_STRICT
					: (c->isterminal ? "t" : "_")
				),
				#else // NO PATCH_SHOW_DESKTOP
				(
					c->isgame ?
					#if PATCH_FLAG_GAME_STRICT
					(c->isgamestrict ? "G" : "g")
					#else // NO PATCH_FLAG_GAME_STRICT
					"g"
					#endif // PATCH_FLAG_GAME_STRICT
					:(c->isterminal ? "t" : "_")
				),
				#endif // PATCH_SHOW_DESKTOP
			#else // NO PATCH_TERMINAL_SWALLOWING
			(c->isgame ? "g" : "_"),
			#endif // PATCH_TERMINAL_SWALLOWING
		#else // NO PATCH_FLAG_GAME
			#if PATCH_SHOW_DESKTOP
				c->isdesktop ? "D" :
				c->ondesktop ? "d" :
				#if PATCH_TERMINAL_SWALLOWING
				c->isterminal ? "t" : "_"
				#else // NO PATCH_TERMINAL_SWALLOWING
				"_"
				#endif // PATCH_TERMINAL_SWALLOWING
			,
			#else // NO PATCH_SHOW_DESKTOP
				#if PATCH_TERMINAL_SWALLOWING
				(c->isterminal ? "t" : "_"),
				#else // NO PATCH_TERMINAL_SWALLOWING
				"",
				#endif // PATCH_TERMINAL_SWALLOWING
			#endif // PATCH_SHOW_DESKTOP
		#endif // PATCH_FLAG_GAME
		#if PATCH_FLAG_ALWAYSONTOP
		(c->alwaysontop ? "a" : "_"),
		#else // NO PATCH_FLAG_ALWAYSONTOP
		"",
		#endif // PATCH_FLAG_ALWAYSONTOP
		#if PATCH_MODAL_SUPPORT
		(c->ismodal ? "m" : "_"),
		#else // NO PATCH_MODAL_SUPPORT
		"",
		#endif // PATCH_MODAL_SUPPORT
		#if PATCH_FLAG_STICKY
		(c->issticky ? "s" : "_"),
		#else // NO PATCH_FLAG_STICKY
		"",
		#endif // PATCH_FLAG_STICKY
		#if PATCH_FLAG_HIDDEN
		(c->ishidden ? "h" : "_"),
		#else // NO PATCH_FLAG_HIDDEN
		"",
		#endif // PATCH_FLAG_HIDDEN
		#if PATCH_FLAG_IGNORED
		(c->isignored ? "i" : (c->neverfocus ? "n" : "_")),
		#else // NO PATCH_FLAG_IGNORED
		(c->neverfocus ? "n" : "_"),
		#endif // PATCH_FLAG_IGNORED
		#if PATCH_FLAG_PANEL
		(c->ispanel ? "p" : "_"),
		#else // NO PATCH_FLAG_PANEL
		"",
		#endif // PATCH_FLAG_PANEL
		#if PATCH_CLASS_STACKING
		(c->stackhead ? "=" : "_"),
		#else // NO PATCH_CLASS_STACKING
		"",
		#endif // PATCH_CLASS_STACKING
		(c->ultparent == c ? "u" : "_"),
		(c->parent ? "P" : "_"),
		(c->toplevel ? "^" : "_"),
		(c->isurgent ? "U" : "_"),
		#if PATCH_FLAG_FOLLOW_PARENT
		(c->followparent ? "+" : " "),
		#else // NO PATCH_FLAG_FOLLOW_PARENT
		"",
		#endif // PATCH_FLAG_FOLLOW_PARENT
		#if PATCH_FLAG_CENTRED
		c->iscentred,
		#endif // PATCH_FLAG_CENTRED
		#if PATCH_FLAG_FAKEFULLSCREEN
		c->fakefullscreen,
		#endif // PATCH_FLAG_FAKEFULLSCREEN
		(c->isfullscreen ? "1" : "0"),
		c->win,
		#if PATCH_FLAG_GAME && PATCH_FLAG_GAME_STRICT
		game == c ? "@" :
		#endif // PATCH_FLAG_GAME && PATCH_FLAG_GAME_STRICT
		c->mon ? (ISVISIBLE(c) ? "*" : " ") : "?"
	);

	if (!c->tags)
		fprintf(stderr, "current ");
	else
		for (int i = 0; i < LENGTH(tags); i++)
			if (c->tags & (1 << i))
				fprintf(stderr, "%u ", (i + 1));

	fprintf(stderr, "] \"%s\"", (c->name[0] == '\0' ? "broken" : c->name));
}
#endif // PATCH_LOG_DIAGNOSTICS

void
logrules(const Arg *arg)
{
	char *json_buffer;

	for (cJSON *r_json = (rules_json ? rules_json->child : NULL); r_json; r_json = r_json->next)
		if (cJSON_HasObjectItem(r_json, "parsed"))
			cJSON_DeleteItemFromObject(r_json, "parsed");

	if (arg->ui)
		json_buffer = cJSON_Print(rules_json);
	else {
		json_buffer = cJSON_PrintUnformatted(rules_json);
		for (int i = 1; i < strlen(json_buffer)-1; i++)
			if (json_buffer[i-1] == '}' && json_buffer[i] == ',' && json_buffer[i+1] == '{')
				json_buffer[i] = '\n';
	}
	fprintf(stderr, "--\nRules after pre-processing\n==========================\nNumber of rules: %i\n\n%s", cJSON_GetArraySize(rules_json), json_buffer);
	fprintf(stderr, "\n--\n");
	cJSON_free(json_buffer);
}

void
losefullscreen(Client *active, Client *next)
{
	Client *sel = active ? active : selmon->sel;
	if (!sel || sel == next)
		return;
	if (sel->isfullscreen
		#if PATCH_FLAG_FAKEFULLSCREEN
		&& sel->fakefullscreen != 1
		#endif // PATCH_FLAG_FAKEFULLSCREEN
		&& (!next || (ISVISIBLE(sel) && sel->mon == next->mon && !next->isfloating))) {
		#if PATCH_FLAG_GAME
		if (sel->isgame
			#if PATCH_FLAG_GAME_STRICT
			&& (sel->isgamestrict || !next || sel->mon != next->mon)
			#endif // PATCH_FLAG_GAME_STRICT
		)
			minimize(sel);
		else
		#endif // PATCH_FLAG_GAME
		{
			sel->lostfullscreen = 1;
			setfullscreen(sel, 0);
		}
	}
}

void
manage(Window w, XWindowAttributes *wa)
{
	Client *c = wintoclient(w);
	if (c)
		return;

	int takefocus = !(nonstop & 1);

	Client *t = NULL;
	#if PATCH_TERMINAL_SWALLOWING
	Client *term = NULL;
	#endif // PATCH_TERMINAL_SWALLOWING
	Monitor *m;
	Window trans = None;
	XWindowChanges wc;
	XEvent xev;
	#if PATCH_SHOW_DESKTOP
	#if PATCH_SHOW_DESKTOP_ONLY_WHEN_ACTIVE
	int nondesktop = 0;
	#endif // PATCH_SHOW_DESKTOP_ONLY_WHEN_ACTIVE
	#endif // PATCH_SHOW_DESKTOP

	c = ecalloc(1, sizeof(Client));
	c->win = w;
	c->pid = winpid(w);
	// set index accordingly
	c->index = 0;

	#if PATCH_HANDLE_SIGNALS
	c->sigtermcount = 0;
	#endif // PATCH_HANDLE_SIGNALS

	#if PATCH_SHOW_DESKTOP
	if (getatompropex(w, netatom[NetWMWindowType]) == netatom[NetWMWindowTypeDesktop])
		#if PATCH_SHOW_DESKTOP_UNMANAGED
		if (showdesktop && showdesktop_unmanaged) {
			logdatetime(stderr);
			fprintf(stderr, "dwm: desktop window has been recorded as 0x%lx.\n", w);
			if (desktopwin) {
				logdatetime(stderr);
				fputs("dwm: warning: more than one desktop window exists - there may be trouble ahead.\n", stderr);
			}
			desktopwin = w;
			desktoppid = c->pid;
			XSelectInput(dpy, desktopwin, EnterWindowMask|PointerMotionMask|FocusChangeMask);
			XMapWindow(dpy, desktopwin);
			XLowerWindow(dpy, desktopwin);
			free(c);
			return;
		}
		else
		#endif // PATCH_SHOW_DESKTOP_UNMANAGED
		c->wasdesktop = 1;
	else
		c->wasdesktop = 0;
	#endif // PATCH_SHOW_DESKTOP
	c->ultparent = getultimateparentclient(c);

	updatetitle(c, 0);

	#if PATCH_SCAN_OVERRIDE_REDIRECTS
	if (wa->override_redirect) {
		c->next = orlist;
		orlist = c;
		logdatetime(stderr);
		fprintf(stderr, "debug: new override_redirect: 0x%lx\n", c->win);
		return;
	}
	#endif // PATCH_SCAN_OVERRIDE_REDIRECTS

	if (c->ultparent) {
		for (m = mons; m; m = m->next) {
			for (t = m->clients; t && t->snext; t = t->snext);
			for (; t; t = t->sprev)
				if (t->ultparent == c->ultparent && t->index >= c->index)
					c->index = (t->index + 1);
		}
	}
	else c->ultparent = c;

	/* geometry */
	c->x = c->oldx = wa->x;
	c->y = c->oldy = wa->y;
	c->w = c->oldw = wa->width;
	c->h = c->oldh = wa->height;
	c->oldbw = wa->border_width;
	#if PATCH_CFACTS
	c->cfact = 1.0;
	#endif // PATCH_CFACTS

	c->dormant = 1;	// keep things on the down low until initial setup is complete;

	updatesizehints(c);

	if (XGetTransientForHint(dpy, w, &trans) && (t = wintoclient(trans))) {
		c->isfloating = c->oldstate = 1;
		c->mon = t->mon;
		c->monindex = t->monindex;
		c->parent = t;
		#if PATCH_FLAG_FOLLOW_PARENT
		if (c->parent)
			c->followparent = c->parent->followparent;
		#endif // PATCH_FLAG_FOLLOW_PARENT
		if (applyrules(c, 0, NULL) == -1) {
			free(c);
			return;
		}
		if (!c->tags && !c->toplevel && c->parent == t && t->mon == c->mon) {
			#if PATCH_FLAG_STICKY
			if (t->issticky)
				c->issticky = 1;
			#endif // PATCH_FLAG_STICKY
			c->tags = t->tags;
		}
		c->tags = c->tags & TAGMASK ? c->tags & TAGMASK : c->mon->tagset[c->mon->seltags];
		c->isfloating_override = -1;
	} else {
		trans = None;
		c->isfloating = c->oldstate = c->isfixed;
		c->mon = NULL;
		c->monindex = -1;
		c->parent = t = getparentclient(c);
		if (t && t->pid != c->pid)
			c->fosterparent = 1;
		#if PATCH_FLAG_FOLLOW_PARENT
		if (c->parent && !c->fosterparent)
			c->followparent = c->parent->followparent;
		#endif // PATCH_FLAG_FOLLOW_PARENT
		if (applyrules(c, 0, NULL) == -1) {
			free(c);
			return;
		}
		if (!c->fosterparent && !c->toplevel && c->parent && c->parent == t && !c->tags && !c->mon) {
			#if PATCH_FLAG_STICKY
			if (t->issticky
				#if PATCH_MODAL_SUPPORT
				&& !c->ismodal
				#endif // PATCH_MODAL_SUPPORT
				)
				c->tags = t->mon->tagset[t->mon->seltags];
			else
			#endif // PATCH_FLAG_STICKY
			c->tags = t->tags;
			c->mon = t->mon;
			c->monindex = t->monindex;
			#if PATCH_FLAG_STICKY
			#if PATCH_MODAL_SUPPORT
			if (t->issticky && c->ismodal)
				c->issticky = 1;
			#endif // PATCH_MODAL_SUPPORT
			#endif // PATCH_FLAG_STICKY
		}
		if (c->parent && !c->toplevel) {
			if (!c->mon) {
				if (c->isfloating
					#if PATCH_FLAG_IGNORED
					&& !c->isignored
					#endif // PATCH_FLAG_IGNORED
					#if PATCH_FLAG_PANEL
					&& !c->ispanel
					#endif // PATCH_FLAG_PANEL
					)
					c->mon = c->parent->mon;
				else
					c->mon = selmon;
			}
			#if PATCH_MODAL_SUPPORT
			if (c->ismodal_override == -1 && !c->fosterparent)
				c->ismodal = c->parent->ismodal;
			#endif // PATCH_MODAL_SUPPORT
			#if PATCH_FLAG_STICKY
			#if PATCH_FLAG_PANEL
			// if parent is born of a panel, then it must be as sticky as the panel;
			if (c->ultparent->ispanel)
				c->issticky = c->ultparent->issticky;
			#endif // PATCH_FLAG_PANEL
			#endif // PATCH_FLAG_STICKY
		}
		else if (!c->mon) {
			#if PATCH_SHOW_DESKTOP
			if (c->isdesktop) {
				if ((m = recttomon(c->x, c->y, c->w, c->h)))
					c->mon = m;
				else
					c->mon = selmon;
			}
			else
			#endif // PATCH_SHOW_DESKTOP
			c->mon = selmon;
		}
		c->monindex = c->mon->num;
		c->tags = c->tags & TAGMASK ? c->tags & TAGMASK : c->mon->tagset[c->mon->seltags];

		#if PATCH_TERMINAL_SWALLOWING
		if (terminal_swallowing
			#if PATCH_FLAG_IGNORED
			&& !c->isignored
			#endif // PATCH_FLAG_IGNORED
			#if PATCH_FLAG_PANEL
			&& !c->ispanel
			#endif // PATCH_FLAG_PANEL
			#if PATCH_SHOW_DESKTOP
			&& !c->isdesktop && !c->ultparent->isdesktop
			#endif // PATCH_SHOW_DESKTOP
			)
			term = termforwin(c);
		#endif // PATCH_TERMINAL_SWALLOWING
	}

	#if PATCH_FLAG_TITLE
	if (c->displayname)
		updatetitle(c, 0);
	#endif // PATCH_FLAG_TITLE

	#if PATCH_WINDOW_ICONS
	updateicon(c);
	#endif // PATCH_WINDOW_ICONS

	c->bw = borderpx;
	if (
		#if PATCH_FLAG_PANEL
		c->ispanel ||
		#endif // PATCH_FLAG_PANEL
		#if PATCH_SHOW_DESKTOP
		c->isdesktop ||
		#endif // PATCH_SHOW_DESKTOP
		#if PATCH_FLAG_IGNORED
		c->isignored ||
		#endif // PATCH_FLAG_IGNORED
		0
	) {
		#if PATCH_SHOW_DESKTOP
		#if PATCH_SHOW_DESKTOP_ONLY_WHEN_ACTIVE
		if (showdesktop && showdesktop_when_active && c->isdesktop
			&& (getdesktopclient(c->mon, &nondesktop) || nondesktop))
		#endif // PATCH_SHOW_DESKTOP_ONLY_WHEN_ACTIVE
		#endif // PATCH_SHOW_DESKTOP
		c->autofocus = 0;
		c->bw = 0;
	}

	updatewindowtype(c);
	updatewindowstate(c);

	wc.border_width = c->bw;
	XConfigureWindow(dpy, w, CWBorderWidth, &wc);
	XSetWindowBorder(dpy, w, scheme[SchemeNorm][ColBorder].pixel);
	configure(c); /* propagates border_width, if size doesn't change */

	c->dormant = 0;

	#if PATCH_FLAG_IGNORED
	if (c->ultparent->isignored && !c->isignored)
		c->isignored = 1;
	#endif // PATCH_FLAG_IGNORED

	#if PATCH_FLAG_GAME
	#if PATCH_FLAG_CENTRED
	if (c->isgame && c->isfloating && !c->isfullscreen && c->iscentred_override == -1)
		c->iscentred = 1;
	#endif // PATCH_FLAG_CENTRED
	#endif // PATCH_FLAG_GAME

	if (c->isfloating)
		adjustfloatposition(c);

	updatesizehints(c);
	updatewmhints(c);

	#if PATCH_SHOW_DESKTOP
	if (!c->ondesktop)
		c->ondesktop = !c->isdesktop && (
			c->isfloating && ((c->parent && (c->parent->isdesktop || c->parent->ondesktop)) || c->ultparent->isdesktop)
		) ? 1 : 0;
	if (c->isdesktop || c->ondesktop) {
		c->tags = 0;
		#if PATCH_FLAG_STICKY
		c->issticky = 0;
		#endif // PATCH_FLAG_STICKY
		#if PATCH_FLAG_HIDDEN
		c->ishidden = 0;
		#endif // PATCH_FLAG_HIDDEN
	}
	#endif // PATCH_SHOW_DESKTOP

	#if PATCH_PERSISTENT_METADATA
	if (1
		#if PATCH_FLAG_IGNORED
		&& !c->isignored
		#endif // PATCH_FLAG_IGNORED
		#if PATCH_FLAG_PANEL
		&& !c->ispanel
		#endif // PATCH_FLAG_PANEL
	) {
		int format;
		unsigned long *data, n, extra, x = -1, y = -1, w = -1, h = -1;
		unsigned long longdata;
		Atom atom;
		if (XGetWindowProperty(dpy, c->win, netatom[NetClientInfo], 0L, 16L, False, XA_CARDINAL,
				&atom, &format, &n, &extra, (unsigned char **)&data) == Success && n > 0) {

			switch (n) {
				case 16:
					#if PATCH_SHOW_DESKTOP
					if (ISBOOLEAN(*(data+15)))
						c->ondesktop = *(data+15);
					#endif // PATCH_SHOW_DESKTOP
				case 15:
					#if PATCH_SHOW_DESKTOP
					if (ISBOOLEAN(*(data+14)))
						c->isdesktop = *(data+14);
					#endif // PATCH_SHOW_DESKTOP
				case 14:
					#if PATCH_FLAG_HIDDEN
					if (ISBOOLEAN(*(data+13))
						#if PATCH_SHOW_DESKTOP
						&& !c->isdesktop
						#endif // PATCH_SHOW_DESKTOP
					)
						c->ishidden = *(data+13);
					#endif // PATCH_FLAG_HIDDEN
				case 13:
					#if PATCH_FLAG_FAKEFULLSCREEN
					if (ISBOOLEAN(*(data+12)))
						c->fakefullscreen = *(data+12);
					#endif // PATCH_FLAG_FAKEFULLSCREEN
				case 12:
					#if PATCH_CFACTS
					longdata = *(data+11);
					if (longdata) {
						c->cfact = (float) longdata / 100;
						if (c->cfact > 4.0f || c->cfact < 0.25f)
							c->cfact = 1.0f;
					}
					else
						c->cfact = 1.0f;
					#endif // PATCH_CFACTS
				case 11:
					longdata = *(data+10);
					if (longdata) {
						--longdata;
						c->oldbw = c->bw = longdata;
					}
					else
						c->oldbw = c->bw = borderpx;
				case 10:
					longdata = *(data+9);
					if (longdata) {
						c->sfyo = (float) longdata / 1000;
						if (c->sfyo > 2.0f || c->sfyo < -2.0f)
							c->sfyo = 0;
					}
				case 9:
					longdata = *(data+8);
					if (longdata) {
						c->sfxo = (float) longdata / 1000;
						if (c->sfxo > 2.0f || c->sfxo < -2.0f)
							c->sfxo = 0;
					}
				case 8:
					h = *(data+7);
				case 7:
					w = *(data+6);
				case 6:
					y = *(data+5);
				case 5:
					x = *(data+4);
				case 4:
					if (ISBOOLEAN(*(data+3))) {
						format = c->isfloating & (1 << 1);
						c->oldstate = *(data+3);
						c->isfloating = c->oldstate | format;
					}
				case 3:
					c->monindex = *(data+2);
					for (m = mons; m; m = m->next) {
						if (m->num == *(data+2)) {
							c->mon = m;
							break;
						}
					}
					#if PATCH_VIRTUAL_MONITORS
					if (c->monindex != c->mon->num) {
						for (m = mons; m; m = m->next) {
							if (m->num == *(data+2) % 1000) {
								c->mon = m;
								break;
							}
						}
					}
					#endif // PATCH_VIRTUAL_MONITORS
					if (w > c->mon->mw)
						w = c->mon->mw;
					if (h > c->mon->mh)
						h = c->mon->mh;
					c->sfw = w;
					c->sfh = h;
					x += c->mon->mx;
					y += c->mon->my;
					if (x >= c->mon->mx && x <= (c->mon->mx + c->mon->mw))
						c->sfx = x;
					if (y >= c->mon->my && y <= (c->mon->my + c->mon->mh))
						c->sfy = y;
					if (c->isfloating) {
						c->w = w;
						c->h = h;
						c->x = c->sfx;
						c->y = c->sfy;
					}
				case 2:
					if (*(data + 1) == (*(data + 1) & TAGMASK))
						c->tags = *(data + 1);
			}

		}
		if (n > 0)
			XFree(data);
	}

	setclienttagprop(c);
	#endif // PATCH_PERSISTENT_METADATA
	#if PATCH_FLAG_HIDDEN
	sethidden(c, c->ishidden, False);
	#endif // PATCH_FLAG_HIDDEN
	publishwindowstate(c);

	#if PATCH_SHOW_DESKTOP
	if (c->isdesktop)
		c->ultparent = c;
	#endif // PATCH_SHOW_DESKTOP

	#if PATCH_FLAG_GAME
	if (c->isgame && !c->isfullscreen)
		XSelectInput(dpy, w, EnterWindowMask|FocusChangeMask|PropertyChangeMask|StructureNotifyMask|ResizeRedirectMask);
	else
	#endif // PATCH_FLAG_GAME
	XSelectInput(dpy, w, EnterWindowMask|FocusChangeMask|LeaveWindowMask|PropertyChangeMask|StructureNotifyMask|ResizeRedirectMask);
	grabbuttons(c, 0);

	if (takefocus && (
		(c->isfloating && !c->autofocus
			#if PATCH_SHOW_DESKTOP
			&& !(showdesktop && c->mon->showdesktop
					#if PATCH_SHOW_DESKTOP_WITH_FLOATING
					&& showdesktop_floating
					#endif // PATCH_SHOW_DESKTOP_WITH_FLOATING
				)
			#endif // PATCH_SHOW_DESKTOP
		)
		#if PATCH_ATTACH_BELOW_AND_NEWMASTER
		|| (!c->isfloating && !c->newmaster && c->mon->lt[c->mon->sellt]->arrange != monocle
			#if PATCH_SHOW_DESKTOP
			&& !c->mon->showdesktop
			#endif // PATCH_SHOW_DESKTOP
		)
		#endif // PATCH_ATTACH_BELOW_AND_NEWMASTER
		|| (c->mon != selmon && !c->isurgent)
		#if PATCH_FLAG_HIDDEN
		|| c->ishidden
		#endif // PATCH_FLAG_HIDDEN
		#if PATCH_FLAG_IGNORED
		|| c->isignored
		#endif // PATCH_FLAG_IGNORED
		#if PATCH_FLAG_PANEL
		|| c->ispanel
		#endif // PATCH_FLAG_PANEL
		#if PATCH_FLAG_GAME
		// prevent new clients from taking focus from an active fullscreen game client;
		|| ((t = getactivegameclient(c->mon)) && t != c)
		#endif // PATCH_FLAG_GAME
		))
		takefocus = 0;

	if (takefocus && (
		(c->isfloating && c->autofocus) ||
		(!c->isfloating && (
			#if PATCH_ATTACH_BELOW_AND_NEWMASTER
			c->newmaster ||
			#endif // PATCH_ATTACH_BELOW_AND_NEWMASTER
			c->mon->lt[c->mon->sellt]->arrange == monocle
		)) ||
		(c->isfullscreen
			#if PATCH_FLAG_FAKEFULLSCREEN
			&& !c->fakefullscreen
			#endif // PATCH_FLAG_FAKEFULLSCREEN
		)
		#if PATCH_FLAG_GAME
		|| (c->isgame && c->isfullscreen)
		#endif // PATCH_FLAG_GAME
	)) {

		if (!((ISVISIBLE(c)
			#if PATCH_SHOW_DESKTOP
			|| ISVISIBLEONTAG(c, c->mon->tagset[c->mon->seltags])
			#endif // PATCH_SHOW_DESKTOP
			) && c->mon == selmon) && !c->isurgent)
			takefocus = 0;
		else if (c->mon->sel) {
			if (((
					!c->isfloating
					#if PATCH_FLAG_GAME
					|| c->isgame
					#endif // PATCH_FLAG_GAME
				)
				&& c->mon->sel->isfullscreen
				#if PATCH_FLAG_FAKEFULLSCREEN
				&& c->mon->sel->fakefullscreen != 1
				#endif // PATCH_FLAG_FAKEFULLSCREEN
				)
				#if PATCH_FLAG_GAME
				|| (c->mon->sel->isgame && !c->isgame)
				#endif // PATCH_FLAG_GAME
				)
				takefocus = 0;
		}

	}

	#if PATCH_SHOW_DESKTOP
	if (c->ondesktop && !c->ultparent->isdesktop) {
		// not connected to a desktop client - find one;
		Client *d = NULL;
		for (Monitor *mm = mons; mm; mm = mm->next)
			for (Client *cc = mm->clients; cc; cc = cc->next)
				if (cc->isdesktop) {
					d = cc;
					if (d->mon == c->mon)
						c->ultparent = d;
					break;
				}
		if (!c->ultparent->isdesktop) {
			logdatetime(stderr);
			if (!d)
				fprintf(stderr, "note: client \"%s\" (0x%lx) belongs on a desktop, but no desktop found.\n", c->name, c->win);
			else {
				fprintf(stderr,
					"note: client \"%s\" (0x%lx) belongs on a desktop, but no desktop found on monitor %i; moving to desktop on monitor %i.\n",
					c->name, c->win, c->mon->num, d->mon->num
				);
				c->ultparent = d;
				c->mon = d->mon;
			}
		}
	}
	#endif // PATCH_SHOW_DESKTOP


	#if PATCH_CLASS_STACKING
	if(attach_stackhead(c)) {
		if (c->mon == selmon)
			takefocus = 1;
	}
	else
	{
	#endif // PATCH_CLASS_STACKING
		#if PATCH_ATTACH_BELOW_AND_NEWMASTER
		if (
			((nonstop & 1) || c->newmaster || (
				((ISVISIBLE(c) && c->mon == selmon) || (c->tags & c->mon->tagset[c->mon->seltags]))
				&& c->mon->lt[c->mon->sellt]->arrange == monocle
			)) &&
			((!c->mon->sel || !c->mon->sel->isfullscreen
				#if PATCH_FLAG_FAKEFULLSCREEN
				|| c->mon->sel->fakefullscreen == 1
				#endif // PATCH_FLAG_FAKEFULLSCREEN
				#if PATCH_FLAG_GAME
				|| (c->mon->sel->isgame && c->isgame)
				|| (c->isgame && c->isfullscreen
					#if PATCH_FLAG_FAKEFULLSCREEN
					&& c->fakefullscreen != 1
					#endif // PATCH_FLAG_FAKEFULLSCREEN
				)
				#endif // PATCH_FLAG_GAME
			) || !ISVISIBLE(c)) &&
			!(0
			#if PATCH_FLAG_IGNORED
			|| c->isignored
			#endif // PATCH_FLAG_IGNORED
			#if PATCH_FLAG_PANEL
			|| c->ispanel
			#endif // PATCH_FLAG_PANEL
			)
		){
		#endif // PATCH_ATTACH_BELOW_AND_NEWMASTER
			attach(c);
			attachstack(c);
		#if PATCH_ATTACH_BELOW_AND_NEWMASTER
		} else {
			attachBelow(c);
			attachstackBelow(c);
		}
		#endif // PATCH_ATTACH_BELOW_AND_NEWMASTER
	#if PATCH_CLASS_STACKING
	}
	#endif // PATCH_CLASS_STACKING
	XChangeProperty(dpy, root, netatom[NetClientList], XA_WINDOW, 32, PropModeAppend,
		(unsigned char *) &(c->win), 1);

	#if PATCH_FLAG_GAME || PATCH_FLAG_HIDDEN
	if (strcmp(c->name,"Wine System Tray")==0) {
		minimize(c);
		#if PATCH_FLAG_IGNORED
		c->isignored = 1;
		#endif // PATCH_FLAG_IGNORED
		takefocus = 0;
	}
	#endif // PATCH_FLAG_GAME || PATCH_FLAG_HIDDEN

	if (!nonstop && !c->isfixed && !c->isfloating
	) {
		logdatetime(stderr);
		fprintf(stderr, "c:\"%s\" x:%i y:%i old w:%i h:%i ", c->name, c->x, c->y, c->w, c->h);
		if (c->x < c->mon->wx)
			c->x = c->mon->wx;
		if (c->y < c->mon->wy)
			c->y = c->mon->wy;
		fprintf(stderr, "new x:%i y:%i\n", c->x, c->y);
	}

	if (!nonstop && !ISVISIBLE(c) && !c->isfixed && !c->isfloating
		#if PATCH_FLAG_IGNORED
		&& !c->isignored
		#endif // PATCH_FLAG_IGNORED
		#if PATCH_SHOW_DESKTOP
		&& !c->isdesktop && !c->ondesktop
		#endif // PATCH_SHOW_DESKTOP
	) {
		logdatetime(stderr);
		fprintf(stderr, "c:\"%s\" x:%i y:%i old w:%i h:%i ", c->name, c->x, c->y, c->w, c->h);
		c->w = c->mon->ww;
		c->h = c->mon->wh;
		fprintf(stderr, "new w:%i h:%i\n", c->w, c->h);
	}

	#if PATCH_FLAG_IGNORED
	if (c->isignored
		#if PATCH_FLAG_HIDDEN
		&& !c->ishidden
		#endif // PATCH_FLAG_HIDDEN
		)
		XMoveResizeWindow(dpy, c->win, c->x, c->y, c->w, c->h);
	else
	#endif // PATCH_FLAG_IGNORED
	XMoveResizeWindow(dpy, c->win, c->x + 2 * sw, c->y, c->w, c->h); /* some windows require this */

	#if PATCH_HANDLE_SIGNALS
	if (closing) {
		if ((!c->ultparent || c->ultparent == c)
			#if PATCH_MODAL_SUPPORT
			&& !(c->ismodal && c->parent)
			#endif // PATCH_MODAL_SUPPORT
		) {
			int related = 0;
			for (Monitor *mm = c->mon; mm;) {
				for (Client *cc = mm->stack; cc; cc = cc->snext) {
					if (cc != c && cc->pid == c->pid) {
						related = 1;
						break;
					}
				}
				if (related)
					break;
				if ((mm = mm->next))
					continue;
				if (c->mon != mons)
					mm = mons;
				else
					break;
			}
			if (!related) {
				logdatetime(stderr);
				fprintf(stderr, "dwm: closing all clients, orphan client will be auto-closed: \"%s\"\n", c->name);
				killclientex(c, 1);
				return;
			}
		}
	}
	#endif // PATCH_HANDLE_SIGNALS

	setclientstate(c, NormalState);
	if (!nonstop) arrange(c->mon);
	XMapWindow(dpy, c->win);

	#if PATCH_FLAG_HIDDEN
	if (c->ishidden
		#if PATCH_FLAG_IGNORED
		&& !c->isignored
		#endif // PATCH_FLAG_IGNORED
		)
		minimize(c);
	#endif // PATCH_FLAG_HIDDEN

	if (c->isurgent && !nonstop && takefocus)
	{
		XWindowAttributes wa;
		XGetWindowAttributes(dpy, c->win, &wa);
		if (wa.map_state == IsViewable) {
			#if PATCH_FLAG_GAME && PATCH_FLAG_GAME_STRICT
			unfocus(selmon->sel, (c->mon != selmon ? (1 << 1) : 0));
			#else
			unfocus(selmon->sel, 0);
			#endif // PATCH_FLAG_GAME && PATCH_FLAG_GAME_STRICT
			if (selmon != c->mon)
				drawbar(selmon, 0);
			if (!ISVISIBLE(c))
				viewmontag(c->mon, c->tags, 0);
			selmon = c->mon;
			if (getstate(c->win) == IconicState) {
				unminimize(c);
			}
			#if PATCH_MOUSE_POINTER_WARPING
			warptoclient_stop_flag = 1;
			#endif // PATCH_MOUSE_POINTER_WARPING
		}
	}
	#if PATCH_SHOW_DESKTOP
	else if (c->autofocus == -1) {
		takefocus = 0;
		c->autofocus = 0;
		if (showdesktop && c->mon->showdesktop != (c->isdesktop || c->ondesktop)
			#if PATCH_SHOW_DESKTOP_WITH_FLOATING
			&& (!showdesktop_floating || !c->isfloating || c->isdesktop || c->ondesktop)
			#endif // PATCH_SHOW_DESKTOP_WITH_FLOATING
		) {
			c->mon->showdesktop = (c->isdesktop || c->ondesktop);
			t = c->mon->sel;
			c->mon->sel = NULL;
			arrange(c->mon);
			losefullscreen(t, c);
			unfocus(t, 1);
			t = NULL;
		}
	}
	#endif // PATCH_SHOW_DESKTOP
/*
DEBUGIF
DEBUG(
	"selmon:%i take:%i newm:%i p:%i f:%i af:%i c:\"%s\" cm:%i cmsel:\"%s\"\n",
	selmon->num,takefocus,c->newmaster,c->ispanel,c->isfloating,c->autofocus,c->name,c->mon->num,
	c->mon->sel ? c->mon->sel->name : ""
);
DEBUGENDIF
*/
	#if PATCH_TERMINAL_SWALLOWING
	if (terminal_swallowing
		&& term && term->mon == c->mon && !c->noswallow && !c->isterminal && !c->isfixed
		#if PATCH_FLAG_GAME
		&& !c->isgame
		#endif // PATCH_FLAG_GAME
		#if PATCH_SHOW_DESKTOP
		&& !c->isdesktop
		#endif // PATCH_SHOW_DESKTOP
		&& (c->tags & term->tags) > 0)
	{
		swallow(term, c);
		if (term == term->mon->sel) {
			c = term;
			if (term->mon == selmon)
				takefocus = 1;
		}
	}
	#endif // PATCH_TERMINAL_SWALLOWING

	if (takefocus) {
		if (!c->neverfocus) {

			// try to prevent focus-stealing from floating clients to unrelated floating clients;
			if (c->isfloating && c->autofocus
				#if PATCH_FLAG_GAME
				&& !(c->isgame && c->isfullscreen)
				#endif // PATCH_FLAG_GAME
				&& selmon->sel && selmon->sel->isfloating && selmon->sel->ultparent != c->ultparent
				#if PATCH_FLAG_PANEL
				&& !selmon->sel->ispanel
				#endif // PATCH_FLAG_PANEL
				#if PATCH_SHOW_DESKTOP
				&& !selmon->sel->isdesktop
				#endif // PATCH_SHOW_DESKTOP
			) {
				logdatetime(stderr);
				fprintf(stderr, "debug: prevent steal from sel:\"%s\" to c:\"%s\"\n", selmon->sel->name, c->name);
				unfocus(c->mon->sel, 0);
				if (c->mon != selmon) {
					c->mon->sel = c;
					drawbar(c->mon, False);
				}
				focus(c, 0);
			}
			else {
				#if PATCH_MOUSE_POINTER_WARPING
				warptoclient_stop_flag = 1;
				#endif // PATCH_MOUSE_POINTER_WARPING

				losefullscreen(c->mon->sel, c);
				unfocus(c->mon->sel,
					#if PATCH_FLAG_PANEL
					c->ispanel ? 1 :
					#endif // PATCH_FLAG_PANEL
				0);

				#if PATCH_SHOW_DESKTOP || PATCH_MOUSE_POINTER_WARPING
				//focus(c, 1);
				#endif // PATCH_SHOW_DESKTOP || PATCH_MOUSE_POINTER_WARPING
				#if PATCH_FLAG_PANEL
				if (!c->ispanel)
				#endif // PATCH_FLAG_PANEL
				focus(c, 1);

				#if PATCH_MOUSE_POINTER_WARPING
				#if PATCH_FLAG_PANEL
				// avoid warping when the new client is 'attached' to its parent panel;
				if (!c->ispanel && (!c->isfloating || !c->parent || !c->parent->ispanel ||
					c->toplevel || c->fosterparent ||
					(c->y + HEIGHT(c)) < c->parent->y || c->y > (c->parent->y + c->parent->h) ||
					(c->x + WIDTH(c)) < c->parent->x || c->x > (c->parent->x + c->parent->w)
				))
				#endif // PATCH_FLAG_PANEL
				{
					#if PATCH_MOUSE_POINTER_WARPING_SMOOTH
					#if PATCH_FOCUS_FOLLOWS_MOUSE
					#if PATCH_FLAG_GREEDY_FOCUS
					if (c->isgreedy && (
							(c->isfloating && c->autofocus)
							#if PATCH_ATTACH_BELOW_AND_NEWMASTER
							|| (!c->isfloating && c->newmaster)
							#endif // PATCH_ATTACH_BELOW_AND_NEWMASTER
						))
						warptoclient(c, 0, 1);
					else
					#endif // PATCH_FLAG_GREEDY_FOCUS
					#endif // PATCH_FOCUS_FOLLOWS_MOUSE
					warptoclient(c, 1, 0);
					#else // NO PATCH_MOUSE_POINTER_WARPING_SMOOTH
					warptoclient(c, 0);
					#endif // PATCH_MOUSE_POINTER_WARPING_SMOOTH
				}
				#endif // PATCH_MOUSE_POINTER_WARPING
			}
		}
		else focus(NULL, 0);
	}
	#if !PATCH_MOUSE_POINTER_WARPING
	else if (c->mon->sel && selmon == c->mon)
		focus(c->mon->sel, 0);
	#endif // !PATCH_MOUSE_POINTER_WARPING

	#if PATCH_CLIENT_OPACITY
	if (c->mon->sel != c)
		opacity(c, 0);
	#endif // PATCH_CLIENT_OPACITY

	#if PATCH_FLAG_PANEL
	if (c->ispanel)
		raisewin(c->mon, c->win, 1);
	#endif // PATCH_FLAG_PANEL

	if (c->isfloating && c->parent && c->parent->mon == c->mon
		&& !c->fosterparent && !c->toplevel
		&& ISVISIBLEONTAG(c, c->parent->tags) && (
			!c->isfullscreen
			#if PATCH_FLAG_FAKEFULLSCREEN
			|| c->fakefullscreen == 1
			#endif // PATCH_FLAG_FAKEFULLSCREEN
		)
		#if PATCH_FLAG_IGNORED
		&& !c->isignored
		#endif // PATCH_FLAG_IGNORED
		#if PATCH_FLAG_PANEL
		&& !c->ispanel
		#endif // PATCH_FLAG_PANEL
		#if PATCH_SHOW_DESKTOP
		&& !c->isdesktop
		#endif // PATCH_SHOW_DESKTOP
	) {
		if (!c->sfxo
			#if PATCH_FLAG_FLOAT_ALIGNMENT
			|| c->floatalignx != -1
			#endif // PATCH_FLAG_FLOAT_ALIGNMENT
			)
			c->sfxo = (float)(c->x - c->parent->x + c->w / 2) / (c->parent->w / 2);
		else
			c->x = MAX(
					MIN(
						(((c->sfxo == 0 ? 1 : c->sfxo) * c->parent->w / 2) + c->parent->x - c->w / 2),
						(c->mon->wx + c->mon->ww - c->w)
					),
					c->mon->wx
				);

		if (!c->sfyo
			#if PATCH_FLAG_FLOAT_ALIGNMENT
			|| c->floataligny != -1
			#endif // PATCH_FLAG_FLOAT_ALIGNMENT
			)
			c->sfyo = (float)(c->y - c->parent->y + c->h / 2) / (c->parent->h / 2);
		else
			c->y = MAX(
					MIN(
						(((c->sfyo == 0 ? 1 : c->sfyo) * c->parent->h / 2) + c->parent->y - c->h / 2),
						(c->mon->wy + c->mon->wh - c->h)
					),
					c->mon->wy
				);
		if (ISVISIBLE(c)
			#if PATCH_FLAG_FLOAT_ALIGNMENT
			&& (c->floatalignx != -1 || c->floataligny != -1)
			#endif // PATCH_FLAG_FLOAT_ALIGNMENT
			)
			XMoveWindow(dpy, c->win, c->x, c->y);
	}

//	#if PATCH_MOUSE_POINTER_WARPING
//	if (!nonstop)
//	#endif // PATCH_MOUSE_POINTER_WARPING
	//restack(c->mon);

	while (XCheckMaskEvent(dpy, EnterWindowMask, &xev));

	if (ISVISIBLE(c) && !MINIMIZED(c)
		#if PATCH_FLAG_HIDDEN
		&& !c->ishidden
		#endif // PATCH_FLAG_HIDDEN
		#if PATCH_TERMINAL_SWALLOWING
		&& !c->swallowing
		#endif // PATCH_TERMINAL_SWALLOWING
		#if PATCH_SHOW_DESKTOP
		&& !c->isdesktop
		#endif // PATCH_SHOW_DESKTOP
		)
		setclientstate(c, NormalState);

	#if PATCH_SHOW_DESKTOP
	if (c->isdesktop)
		showhide(c, 1);
	#endif // PATCH_SHOW_DESKTOP

	#if PATCH_TORCH
	if (torchwin) XRaiseWindow(dpy, torchwin);
	#endif // PATCH_TORCH
}

void
mappingnotify(XEvent *e)
{
	XMappingEvent *ev = &e->xmapping;

	XRefreshKeyboardMapping(ev);
	if (ev->request == MappingKeyboard)
		grabkeys();
}

void
maprequest(XEvent *e)
{
	static XWindowAttributes wa;
	XMapRequestEvent *ev = &e->xmaprequest;

	#if PATCH_SYSTRAY
	Client *i;
	if ((i = wintosystrayicon(ev->window))) {
		sendevent(i->win, netatom[Xembed], StructureNotifyMask, CurrentTime, XEMBED_WINDOW_ACTIVATE, 0, systray->win, XEMBED_EMBEDDED_VERSION);
		updatesystray(1);
		resizebarwin(selmon);
	}
	#endif // PATCH_SYSTRAY
	if (!XGetWindowAttributes(dpy, ev->window, &wa) || !wa.depth
		#if !PATCH_SCAN_OVERRIDE_REDIRECTS
		|| wa.override_redirect
		#endif // !PATCH_SCAN_OVERRIDE_REDIRECTS
		)
		return;

	if (!wintoclient(ev->window))
		manage(ev->window, &wa);
}

#if PATCH_FLAG_GAME || PATCH_FLAG_HIDDEN || PATCH_FLAG_PANEL
void
minimize(Client *c)
{
	if (!c || MINIMIZED(c))
		return;

	for (int i = 0; i < LENGTH(tags); i++)
		if (c->mon->focusontag[i] == c)
			c->mon->focusontag[i] = NULL;

	Window w = c->win;
	static XWindowAttributes ra, ca;

	// more or less taken directly from blackbox's hide() function
	XGrabServer(dpy);
	XGetWindowAttributes(dpy, root, &ra);
	XGetWindowAttributes(dpy, w, &ca);
	// prevent UnmapNotify events
	XSelectInput(dpy, root, ra.your_event_mask & ~SubstructureNotifyMask);
	XSelectInput(dpy, w, ca.your_event_mask & ~StructureNotifyMask);
	XUnmapWindow(dpy, w);
	setclientstate(c, IconicState);
	XSelectInput(dpy, root, ra.your_event_mask);
	XSelectInput(dpy, w, ca.your_event_mask);
	XUngrabServer(dpy);
}
#endif // PATCH_FLAG_GAME || PATCH_FLAG_HIDDEN || PATCH_FLAG_PANEL

#if PATCH_MODAL_SUPPORT
int
modalgroupclients(Client *c)
{
	if (c && c->ismodal && c->mon
		#if PATCH_SHOW_DESKTOP
		&& (!c->ondesktop && !c->isdesktop)
		#endif // PATCH_SHOW_DESKTOP
	) {

		// take precautions with modal client in monocle mode;
		// so that the modal client's ancestors are effectively brought forward;
		// to be visible directly below the modal client;

		Client *s;
		if ((s = c->mon->stack)) {
			// dirty flag used to indicate that our modal client's ancestors need to be pulled together;
			int dirty = 1;
			Client *ns, *snew = NULL;
			if (s == c) {
				// top of the stack is our modal client, so iterate through the clients;
				// setting dirty flag if ancestors are found in more than one contiguous group;
				dirty = 0;
				for (s = c->mon->stack; s && s->snext; s = s->snext)
					if (s->ultparent == c->ultparent) {
						if (dirty) {
							dirty = 1;
							break;
						}
					}
					else dirty = -1;
				if (dirty < 0)
					dirty = 0;
			}

			if (dirty) {
				// start at the bottom of the stack;
				for (s = c->mon->stack; s && s->snext; s = s->snext);

				// detach each ancestor from the stack onto a temporary stack (snew);
				ns = s;
				while (ns) {
					s = ns;
					ns = s->sprev;
					if ((s->ultparent == c->ultparent
						|| (s->parent == c->parent && !s->toplevel && !c->toplevel)
						|| s == c)
						&& ISVISIBLE(s)
						#if PATCH_FLAG_PANEL
						&& !s->ispanel
						#endif // PATCH_FLAG_PANEL
						#if PATCH_FLAG_IGNORED
						&& !s->isignored
						#endif // PATCH_FLAG_IGNORED
						#if PATCH_SHOW_DESKTOP
						&& !s->isdesktop
						#endif // PATCH_SHOW_DESKTOP
					) {
						detachstackex(s);

						s->sprev = NULL;
						if ((s->snext = snew))
							s->snext->sprev = s;
						snew = s;
					}
				}
				// join the current stack onto our temporary one;
				for (s = snew; s && s->snext; s = s->snext);
				s->snext = c->mon->stack;
				s->snext->sprev = s;
				// replacing the current stack;
				c->mon->stack = snew;

				return 1;
			}
		}
	}
	return 0;
}
#endif // PATCH_MODAL_SUPPORT

void
monocle(Monitor *m)
{
	for (Client *c = m->stack; c; c = c->snext)
		if (!c->isfloating && ISVISIBLE(c)
			#if PATCH_FLAG_HIDDEN
			&& !c->ishidden
			#endif // PATCH_FLAG_HIDDEN
			#if PATCH_FLAG_PANEL
			&& !c->ispanel
			#endif // PATCH_FLAG_PANEL
			#if PATCH_SHOW_DESKTOP
			&& !c->isdesktop
			#endif // PATCH_SHOW_DESKTOP
			)
			resize(c, m->wx, m->wy, m->ww - 2 * c->bw, m->wh - 2 * c->bw, 0);
}

#if PATCH_FLAG_FOLLOW_PARENT || PATCH_MODAL_SUPPORT
void
monsatellites(Client *pp, Monitor *mon) {

	/* child windows to match parent monitor */
	for (Monitor *m = mons; m; m = m->next) {
		Client *c, *p;
		for (c = m->clients; c && c->snext; c = c->snext);
		for (; c; c = p) {
			p = c->sprev;
			if (c != pp
				&& !c->toplevel && !c->fosterparent && c->parent == pp
				&& ISVISIBLE(c) && (0
				#if PATCH_FLAG_FOLLOW_PARENT
				|| c->followparent
				#endif // PATCH_FLAG_FOLLOW_PARENT
				#if PATCH_MODAL_SUPPORT
				|| c->ismodal
				#endif // PATCH_MODAL_SUPPORT
			)) {
				if (c->mon != mon) {
					detach(c);
					detachstack(c);
					c->mon = mon;
					c->monindex = pp->monindex;
					c->tags = c->mon->tagset[c->mon->seltags]; 	// assign tags of target monitor
					#if PATCH_ATTACH_BELOW_AND_NEWMASTER
					attachBelow(c);
					attachstackBelow(c);
					#else // NO PATCH_ATTACH_BELOW_AND_NEWMASTER
					attach(c);
					attachstack(c);
					#endif // PATCH_ATTACH_BELOW_AND_NEWMASTER
					#if PATCH_PERSISTENT_METADATA
					setclienttagprop(c);
					#endif // PATCH_PERSISTENT_METADATA
				}
				monsatellites(c, c->mon);
			}
		}
	}

}
#endif // PATCH_FLAG_FOLLOW_PARENT || PATCH_MODAL_SUPPORT

void
motionnotify(XEvent *e)
{
	#if PATCH_FOCUS_FOLLOWS_MOUSE
	Monitor *m;
	#endif // PATCH_FOCUS_FOLLOWS_MOUSE
	#if PATCH_FOCUS_PIXEL && !PATCH_FOCUS_BORDER
	XEvent xev;
	#endif // PATCH_FOCUS_PIXEL && !PATCH_FOCUS_BORDER
	XMotionEvent *ev = &e->xmotion;

	#if PATCH_ALTTAB
	if (altTabMon && altTabMon->isAlt)
		return;
	#endif // PATCH_ALTTAB

	#if PATCH_FOCUS_PIXEL && !PATCH_FOCUS_BORDER
	if (focuswin && ev->window == focuswin)
	{
		while (XCheckMaskEvent(dpy, PointerMotionMask, &xev));
		repelfocusborder();
		return;
	}
	#endif // PATCH_FOCUS_PIXEL && !PATCH_FOCUS_BORDER

	if (ev->window != root
		#if PATCH_SHOW_DESKTOP
		#if PATCH_SHOW_DESKTOP_UNMANAGED
		&& (showdesktop && showdesktop_unmanaged && desktopwin != ev->window)
		#endif // PATCH_SHOW_DESKTOP_UNMANAGED
		#endif // PATCH_SHOW_DESKTOP
		)
		return;
	#if PATCH_FOCUS_FOLLOWS_MOUSE
	if ((m = recttomon(ev->x_root, ev->y_root, 1, 1)) != selmon && selmon) {
		focusmonex(m);
		focus(NULL, 0);
	}
	#endif // PATCH_FOCUS_FOLLOWS_MOUSE
}

void
mouseview(const Arg *arg)
{
	signed int active = 0;
	signed int direction = 0;
	static signed int taglength = LENGTH(tags);

	#if PATCH_FOCUS_FOLLOWS_MOUSE
	checkmouseovermonitor(selmon);
	#endif // PATCH_FOCUS_FOLLOWS_MOUSE

	// ascertain first active tag
	for (int i = 0; i < taglength; i++)
		if (selmon->tagset[selmon->seltags] & (1 << i)) {
			active = i;
			break;
		}

	// new active starting tag will be current active ± 1

	direction = (arg->i > 0 ? 1 : -1);
	active += direction;

	// if new active is out of tag range, loop round
	if (active >= taglength)
		active = 0;
	if (active < 0)
		active = taglength-1;

	#if PATCH_MOUSE_POINTER_WARPING
	viewmontag(selmon, (1 << active) | (abs(arg->i) > 1 ? (1 << 31) : 0), 1);
	#else // NO PATCH_MOUSE_POINTER_WARPING
	viewmontag(selmon, 1 << active, 1);
	#endif // PATCH_MOUSE_POINTER_WARPING

}

#if PATCH_MOVE_FLOATING_WINDOWS
void
movefloat(const Arg *arg)
{
	if (!selmon)
		return;

	#if PATCH_FOCUS_FOLLOWS_MOUSE
	checkmouseoverclient();
	#endif // PATCH_FOCUS_FOLLOWS_MOUSE

	Client *c;
	if (!(c = selmon->sel) || !c->isfloating || (c->isfullscreen
		#if PATCH_FLAG_FAKEFULLSCREEN
		&& c->fakefullscreen != 1
		#endif // PATCH_FLAG_FAKEFULLSCREEN
		)
		#if PATCH_SHOW_DESKTOP
		|| c->isdesktop
		#endif // PATCH_SHOW_DESKTOP
	) return;

	int x = 0, y = 0, step = (arg->ui & MOVE_FLOATING_BIGGER) ? MOVE_FLOATING_STEP_BIG : MOVE_FLOATING_STEP;

	if (arg->ui & MOVE_FLOATING_DOWN)
		y += step;
	else if (arg->ui & MOVE_FLOATING_UP)
		y -= step;

	if (arg->ui & MOVE_FLOATING_LEFT)
		x -= step;
	else if (arg->ui & MOVE_FLOATING_RIGHT)
		x += step;

	if (c->x + x + c->w + c->bw*2 > (c->mon->wx + c->mon->ww))
		c->x = (c->mon->wx + c->mon->ww) - (c->w + c->bw*2);
	else if (c->x + x < c->mon->wx)
		c->x = c->mon->wx;
	else
		c->x += x;

	if (c->y + y + c->h + c->bw*2 > (c->mon->wy + c->mon->wh))
		c->y = (c->mon->wy + c->mon->wh) - (c->h + c->bw*2);
	else if (c->y + y < c->mon->wy)
		c->y = c->mon->wy;
	else
		c->y += y;

	#if PATCH_MOUSE_POINTER_WARPING || PATCH_FOCUS_FOLLOWS_MOUSE
	int px, py;
	getrelativeptr(c, &px, &py);
	XMoveWindow(dpy, c->win, c->x, c->y);
	XWarpPointer(dpy, None, c->win, 0, 0, 0, 0, px, py);
	focus(c, 1);
	#endif // PATCH_MOUSE_POINTER_WARPING || PATCH_FOCUS_FOLLOWS_MOUSE
	XMoveWindow(dpy, c->win, c->x, c->y);

	#if PATCH_FOCUS_BORDER || PATCH_FOCUS_PIXEL
	if (focuswin)
		drawfocusborder(0);
	#endif // PATCH_FOCUS_BORDER || PATCH_FOCUS_PIXEL

	if (c->isfloating
		&& !c->toplevel && !c->fosterparent
		&& c->parent && c->parent->mon == c->mon
		&& (!c->isfullscreen
		#if PATCH_FLAG_FAKEFULLSCREEN
		|| c->fakefullscreen == 1
		#endif // PATCH_FLAG_FAKEFULLSCREEN
	)) {
		c->sfxo = (float)(c->x - c->parent->x + c->w / 2) / (c->parent->w / 2);
		c->sfyo = (float)(c->y - c->parent->y + c->h / 2) / (c->parent->h / 2);
	}
	snapchildclients(c, 0);

}
#endif // PATCH_MOVE_FLOATING_WINDOWS


void
movemouse(const Arg *arg)
{
	#if PATCH_FOCUS_FOLLOWS_MOUSE
	checkmouseoverclient();
	#endif // PATCH_FOCUS_FOLLOWS_MOUSE

	int x, y, ocx, ocy, nx, ny;
	Client *c;
	#if PATCH_MODAL_SUPPORT
	Client *mc = NULL;
	#endif // PATCH_MODAL_SUPPORT
	Monitor *m;
	XEvent ev;
	Time lasttime = 0;

	if (!(c = selmon->sel) || c->dormant
		#if PATCH_FLAG_IGNORED
		|| c->isignored
		#endif // PATCH_FLAG_IGNORED
		#if PATCH_SHOW_DESKTOP
		|| c->isdesktop
		#endif // PATCH_SHOW_DESKTOP
		)
		return;
	if (c->isfullscreen
		#if PATCH_FLAG_FAKEFULLSCREEN
		&& c->fakefullscreen != 1
		#endif // PATCH_FLAG_FAKEFULLSCREEN
		) /* no support moving fullscreen windows by mouse */
		return;
	#if PATCH_CROP_WINDOWS
	if (arg->i == 1 && !c->crop)
		return;
	#endif // PATCH_CROP_WINDOWS

	restack(selmon);

	nx = ocx = c->x;
	ny = ocy = c->y;
	if (XGrabPointer(dpy, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync,
		None, cursor[CurMove]->cursor, CurrentTime) != GrabSuccess)
		return;
	if (!getrootptr(&x, &y))
		return;

	#if PATCH_CROP_WINDOWS
	if (arg->i == 1) {
		nx = ocx = c->crop->x;
		ny = ocy = c->crop->y;
	}
	else
	#endif // PATCH_CROP_WINDOWS
	#if PATCH_MODAL_SUPPORT
	if (c->ismodal
		&& !c->toplevel && !c->fosterparent
		&& c->parent && c->parent->isfloating
		#if PATCH_SHOW_DESKTOP
		&& !c->ondesktop
		#endif // PATCH_SHOW_DESKTOP
		&& (x < c->x || x > (c->x + WIDTH(c)) || y < c->y || y > (c->y + HEIGHT(c)))
		&& x > c->parent->x && x < (c->parent->x + WIDTH(c->parent))
		&& y > c->parent->y && y < (c->parent->y + HEIGHT(c->parent))
		#if PATCH_FOCUS_BORDER
		&& (!focuswin ||
			fbpos == FOCUS_BORDER_W ? (x < c->x - fh || x > (c->x + WIDTH(c)) || y < c->y || y > c->y + HEIGHT(c)) :
				fbpos == FOCUS_BORDER_E ? (x > c->x + WIDTH(c) + fh || x < c->x || y < c->y || y > c->y + HEIGHT(c)) :
					fbpos == FOCUS_BORDER_N ? (y < c->y - fh || y > c->y + HEIGHT(c) || x < c->x || x > c->x + WIDTH(c)) :
						y > c->y + HEIGHT(c) + fh || y < c->y || x < c->x || x > c->x + WIDTH(c)
		)
		#endif // PATCH_FOCUS_BORDER
	) {
		mc = c;
		c = mc->parent;
		nx = ocx = c->x;
		ny = ocy = c->y;
	}
	#endif // PATCH_MODAL_SUPPORT

	XRaiseWindow(dpy, c->win);
	#if PATCH_MODAL_SUPPORT
	if (mc)
		XRaiseWindow(dpy, mc->win);
	#endif // PATCH_MODAL_SUPPORT
	#if PATCH_FOCUS_BORDER || PATCH_FOCUS_PIXEL
	if (focuswin) {
		drawfocusborder(1);
		XUnmapWindow(dpy, focuswin);
	}
	#endif // PATCH_FOCUS_BORDER || PATCH_FOCUS_PIXEL
	// prevent client moves via resize() triggering updates of
	// the client's parent offset coordinates, sfxo & sfyo
	nonstop = 1;
	do {
		XMaskEvent(dpy, MOUSEMASK|ExposureMask|SubstructureRedirectMask, &ev);
		switch(ev.type) {
		case ConfigureRequest:
		case Expose:
		case MapRequest:
			handler[ev.type](&ev);
			break;
		case MotionNotify:
			if ((ev.xmotion.time - lasttime) <= (1000 / 60))
				continue;
			lasttime = ev.xmotion.time;

			nx = ocx + (ev.xmotion.x - x);
			ny = ocy + (ev.xmotion.y - y);

			#if PATCH_CROP_WINDOWS
			if (arg->i == 1) {
				c->crop->x = nx;
				c->crop->y = ny;
				cropmove(c);
				continue;
			}
			#endif // PATCH_CROP_WINDOWS

			if (abs(selmon->wx - nx) < snap)
				nx = selmon->wx;
			else if (abs((selmon->wx + selmon->ww) - (nx + WIDTH(c))) < snap)
				nx = selmon->wx + selmon->ww - WIDTH(c);
			if (abs(selmon->wy - ny) < snap)
				ny = selmon->wy;
			else if (abs((selmon->wy + selmon->wh) - (ny + HEIGHT(c))) < snap)
				ny = selmon->wy + selmon->wh - HEIGHT(c);
			if (!c->isfloating && selmon->lt[selmon->sellt]->arrange
			&& (abs(nx - c->x) > snap || abs(ny - c->y) > snap))
				togglefloatingex(c);
			if (!selmon->lt[selmon->sellt]->arrange || c->isfloating)
				resize(c, nx, ny, c->w, c->h, 1);
			#if PATCH_FOCUS_BORDER || PATCH_FOCUS_PIXEL
			if (focuswin)
				drawfocusborder(1);
			#endif // PATCH_FOCUS_BORDER || PATCH_FOCUS_PIXEL
			break;
		}
	} while (ev.type != ButtonRelease);
	XUngrabPointer(dpy, CurrentTime);
	// normal coord tracking behaviour continues;
	nonstop = 0;
	#if PATCH_FOCUS_BORDER || PATCH_FOCUS_PIXEL
	if (focuswin)
		XMapWindow(dpy, focuswin);
	#endif // PATCH_FOCUS_BORDER || PATCH_FOCUS_PIXEL


	#if PATCH_CROP_WINDOWS
	if (arg && arg->i == 1 && c->crop);
	else
	#endif // PATCH_CROP_WINDOWS
	if ((m = recttomon(c->x, c->y, c->w, c->h)) != selmon) {
		if (
			#if PATCH_FLAG_FOLLOW_PARENT
			(
				c->followparent && !c->toplevel && !c->fosterparent
				&& c->parent && c->parent->mon == c->mon
			) ||
			#endif // PATCH_FLAG_FOLLOW_PARENT
			#if PATCH_MODAL_SUPPORT
			mc ||
			#endif // PATCH_MODAL_SUPPORT
			#if PATCH_SHOW_DESKTOP
			c->ondesktop ||
			#endif // PATCH_SHOW_DESKTOP
			0
		) {
			m = c->mon;
			c->x = ocx;
			c->y = ocy;
			resize(c, c->x, c->y, c->w, c->h, 1);
		}

		selmon = m;
		sendmon(c, m, c, 1);
		raisewin(c->mon, c->win, True);
		c->monindex = c->mon->num;
		arrange(NULL);
		focus(c, 1);
	#if PATCH_FOCUS_BORDER || PATCH_FOCUS_PIXEL
	} else if (c == selmon->sel) {
		#if PATCH_FOCUS_BORDER
		raisewin(c->mon, c->win, True);
		#if PATCH_MODAL_SUPPORT
		if (mc)
			raisewin(c->mon, mc->win, True);
		#endif // PATCH_MODAL_SUPPORT
		focus(c, 1);
		#elif PATCH_FOCUS_PIXEL
		drawfocusborder(0);
		#endif // PATCH_FOCUS_BORDER || PATCH_FOCUS_PIXEL
	#endif // PATCH_FOCUS_BORDER
	}

	if (c->isfloating && c->parent && c->parent->mon == c->mon && (!c->isfullscreen
		#if PATCH_FLAG_FAKEFULLSCREEN
		|| c->fakefullscreen == 1
		#endif // PATCH_FLAG_FAKEFULLSCREEN
	)) {
		c->sfxo = (float)(c->x - c->parent->x + c->w / 2) / (c->parent->w / 2);
		c->sfyo = (float)(c->y - c->parent->y + c->h / 2) / (c->parent->h / 2);
	}
	snapchildclients(c, 0);

	#if PATCH_PERSISTENT_METADATA
	setclienttagprop(c);
	#endif // PATCH_PERSISTENT_METADATA
}

void
moveorplace(const Arg *arg) {
	#if PATCH_FOCUS_FOLLOWS_MOUSE
	checkmouseoverclient();
	#endif // PATCH_FOCUS_FOLLOWS_MOUSE
	if ((!selmon->lt[selmon->sellt]->arrange || (selmon->sel && selmon->sel->isfloating)))
		movemouse(&(Arg){.i = 0});
	else
		placemouse(arg);
}

#if PATCH_MOVE_TILED_WINDOWS
void
movetiled(const Arg *arg)
{
	#if PATCH_FOCUS_FOLLOWS_MOUSE
	checkmouseoverclient();
	#endif // PATCH_FOCUS_FOLLOWS_MOUSE
	Client *d, *c;
	if (!(c = selmon->sel) || c->isfloating || (c->isfullscreen
		#if PATCH_FLAG_FAKEFULLSCREEN
		&& c->fakefullscreen != 1
		#endif // PATCH_FLAG_FAKEFULLSCREEN
		)
		#if PATCH_SHOW_DESKTOP
		|| c->isdesktop
		#endif // PATCH_SHOW_DESKTOP
	) return;

	#if PATCH_MOUSE_POINTER_WARPING || PATCH_FOCUS_FOLLOWS_MOUSE
	int px, py;
	getrelativeptr(c, &px, &py);
	#endif // PATCH_MOUSE_POINTER_WARPING || PATCH_FOCUS_FOLLOWS_MOUSE

	int count = arg->i;
	if (!count)
		return;

	if (count > 0) {
		// down;
		while (count) {
			if (!(d = nexttiled(c->next)))
				break;
			detach(c);
			c->next = d->next;
			d->next = c;
			--count;
		}
	}
	else if (count < 0) {
		// up;
		while (count) {
			if (!(d = prevtiled(c)))
				break;
			detach(c);
			Client *t = prevtiled(d);
			if (t) {
				c->next = t->next;
				t->next = c;
			}
			else {
				c->next = c->mon->clients;
				c->mon->clients = c;
			}
			++count;
		}
	}

	#if PATCH_CLASS_STACKING
	if (c->isstackhead)
		groupallclassstacks(c->mon);
	#endif // PATCH_CLASS_STACKING

	#if PATCH_PERSISTENT_METADATA
	setclienttagprop(c);
	#endif // PATCH_PERSISTENT_METADATA
	focus(c, 1);
	arrange(c->mon);

	#if PATCH_MOUSE_POINTER_WARPING || PATCH_FOCUS_FOLLOWS_MOUSE
	XWarpPointer(dpy, None, c->win, 0, 0, 0, 0, px, py);
	#endif // PATCH_MOUSE_POINTER_WARPING || PATCH_FOCUS_FOLLOWS_MOUSE
}
#endif // PATCH_MOVE_TILED_WINDOWS

Client *
nextstack(Client *c, int isfloating)
{
	for (; c && ((isfloating ? !c->isfloating : c->isfloating) || !ISVISIBLE(c)
		#if PATCH_FLAG_HIDDEN
		|| c->ishidden
		#endif // PATCH_FLAG_HIDDEN
		#if PATCH_FLAG_IGNORED
		|| c->isignored
		#endif // PATCH_FLAG_IGNORED
		#if PATCH_FLAG_PANEL
		|| c->ispanel
		#endif // PATCH_FLAG_PANEL
		#if PATCH_SHOW_DESKTOP
		|| c->isdesktop
		#endif // PATCH_SHOW_DESKTOP
	); c = c->snext);
	return c;
}

#if PATCH_ATTACH_BELOW_AND_NEWMASTER
Client *
nexttaggedafter(Client *c, unsigned int tags) {
	Client *walked = c;
	for(;
		walked && (walked->isfloating || !ISVISIBLEONTAG(walked, tags)
			#if PATCH_FLAG_HIDDEN
			|| walked->ishidden
			#endif // PATCH_FLAG_HIDDEN
			#if PATCH_FLAG_IGNORED
			|| walked->isignored
			#endif // PATCH_FLAG_IGNORED
			#if PATCH_FLAG_PANEL
			|| walked->ispanel
			#endif // PATCH_FLAG_PANEL
			#if PATCH_SHOW_DESKTOP
			|| walked->isdesktop
			#endif // PATCH_SHOW_DESKTOP
		);
		walked = walked->next
	);
	return walked;
}
#endif // PATCH_ATTACH_BELOW_AND_NEWMASTER

#if PATCH_CLIENT_OPACITY
void
opacity(Client *c, int focused)
{
	if (c->isfullscreen)
		setopacity(c, 0);
	else
		setopacity(c, focused ?
			(c->opacity < 0 ? c->mon->activeopacity : c->opacity) :
			(c->unfocusopacity < 0 ? c->mon->inactiveopacity : c->unfocusopacity)
		);
	#if PATCH_MODAL_SUPPORT
	if (ismodalparent(c) || c->ismodal)
		for (Client *s = c->mon->stack; s; s = s->snext)
			if (s->ultparent == c->ultparent)
				setopacity(s, focused ?
					(s->opacity < 0 ? s->mon->activeopacity : s->opacity) :
					(s->unfocusopacity < 0 ? s->mon->inactiveopacity : s->unfocusopacity)
				);
	#endif // PATCH_MODAL_SUPPORT
}
#endif // PATCH_CLIENT_OPACITY

#if PATCH_MOVE_TILED_WINDOWS || PATCH_FLAG_HIDDEN
Client *
prevtiled(Client *c)
{
	Client *r = NULL;
	for (Client *i = c->mon->clients; i && i != c; i = i->next)
		if (!(i->isfloating || !ISVISIBLE(i)
			#if PATCH_FLAG_HIDDEN
			|| i->ishidden
			#endif // PATCH_FLAG_HIDDEN
			#if PATCH_FLAG_IGNORED
			|| i->isignored
			#endif // PATCH_FLAG_IGNORED
			#if PATCH_FLAG_PANEL
			|| i->ispanel
			#endif // PATCH_FLAG_PANEL
			#if PATCH_SHOW_DESKTOP
			|| i->isdesktop
			#endif // PATCH_SHOW_DESKTOP
			#if PATCH_CLASS_STACKING
			|| i->stackhead
			#endif // PATCH_CLASS_STACKING
		)) r = i;
	return r;
}
#endif // PATCH_MOVE_TILED_WINDOWS || PATCH_FLAG_HIDDEN

Client *
nexttiled(Client *c)
{
	for (; c && (c->isfloating || !ISVISIBLE(c)
		#if PATCH_FLAG_HIDDEN
		|| c->ishidden
		#endif // PATCH_FLAG_HIDDEN
		#if PATCH_FLAG_IGNORED
		|| c->isignored
		#endif // PATCH_FLAG_IGNORED
		#if PATCH_FLAG_PANEL
		|| c->ispanel
		#endif // PATCH_FLAG_PANEL
		#if PATCH_SHOW_DESKTOP
		|| c->isdesktop
		#endif // PATCH_SHOW_DESKTOP
		#if PATCH_CLASS_STACKING
		|| c->stackhead
		#endif // PATCH_CLASS_STACKING
	); c = c->next);
	return c;
}

#if PATCH_CLASS_STACKING
Client *
nexttiledall(Client *c)
{
	for (; c && (c->isfloating || !ISVISIBLE(c)
		#if PATCH_FLAG_HIDDEN
		|| c->ishidden
		#endif // PATCH_FLAG_HIDDEN
		#if PATCH_FLAG_IGNORED
		|| c->isignored
		#endif // PATCH_FLAG_IGNORED
		#if PATCH_FLAG_PANEL
		|| c->ispanel
		#endif // PATCH_FLAG_PANEL
		#if PATCH_SHOW_DESKTOP
		|| c->isdesktop
		#endif // PATCH_SHOW_DESKTOP
	); c = c->next);
	return c;
}
#endif // PATCH_CLASS_STACKING

int
parselayoutjson(cJSON *layout)
{
	cJSON *unsupported = ecalloc(1, sizeof(cJSON));
	cJSON *unsupported_mon = ecalloc(1, sizeof(cJSON));
	cJSON *unsupported_tag = ecalloc(1, sizeof(cJSON));
	cJSON *string = NULL;
	cJSON *c = NULL;
	int found;
	int i;

	for (cJSON *L = layout->child; L; L = L->next) {
		if (L->string) {
			found = 0;
			for (i = 0; i < LENGTH(supported_layout_global); i++)
				if (strcmp((&supported_layout_global[i])->name, L->string) == 0) {
					found = 1;
					break;
				}

			if (!found) {
				if(strstr(L->string, "//") == L->string || strstr(L->string, "#") == L->string || strstr(L->string, ";") == L->string)
					continue;
				string = cJSON_GetObjectItemCaseSensitive(unsupported, L->string);
				if (string) cJSON_SetNumberValue(string, string->valuedouble + 1);
				else cJSON_AddNumberToObject(unsupported, L->string, 1);
				continue;
			}

			#if PATCH_FONT_GROUPS
			else if (strcmp(L->string, "bar-element-font-groups")==0) {
				if (cJSON_IsArray(L) || cJSON_IsObject(L)) {
					barelement_fontgroups_json = L;
					continue;
				}
				cJSON_AddNumberToObject(unsupported, "\"bar-element-font-groups\" must contain a JSON object or array of objects", 0);
			}
			#endif // PATCH_FONT_GROUPS

			#if PATCH_CLIENT_INDICATORS
			else if (strcmp(L->string, "client-indicators")==0) {
				if (json_isboolean(L)) {
					client_ind = L->valueint;
					continue;
				}
				cJSON_AddNumberToObject(unsupported, "\"client-indicators\" must contain a boolean value.", 0);
			}
			else if (strcmp(L->string, "client-indicator-size")==0) {
				if (cJSON_IsInteger(L)) {
					client_ind_size = L->valueint;
					continue;
				}
				cJSON_AddNumberToObject(unsupported, "\"client-indicator-size\" must contain an integer value.", 0);
			}
			else if (strcmp(L->string, "client-indicators-top")==0) {
				if (json_isboolean(L)) {
					client_ind_top = L->valueint;
					continue;
				}
				cJSON_AddNumberToObject(unsupported, "\"client-indicators-top\" must contain a boolean value.", 0);
			}
			#endif // PATCH_CLIENT_INDICATORS

			#if PATCH_FLAG_HIDDEN || PATCH_SHOW_DESKTOP
			else if (strcmp(L->string, "colours-hidden")==0) {
				if (cJSON_IsArray(L))
					if (validate_colours(L, colours[SchemeHide], NULL))
						continue;
				cJSON_AddNumberToObject(unsupported, "\"colours-hidden\" must contain an array of strings", 0);
			}
			#endif // PATCH_FLAG_HIDDEN || PATCH_SHOW_DESKTOP
			else if (strcmp(L->string, "colours-normal")==0) {
				if (cJSON_IsArray(L))
					if (validate_colours(L, colours[SchemeNorm], NULL))
						continue;
				cJSON_AddNumberToObject(unsupported, "\"colours-normal\" must contain an array of strings", 0);
			}
			else if (strcmp(L->string, "colours-selected")==0) {
				if (cJSON_IsArray(L))
					if (validate_colours(L, colours[SchemeSel], NULL))
						continue;
				cJSON_AddNumberToObject(unsupported, "\"colours-selected\" must contain an array of strings", 0);
			}
			#if PATCH_TWO_TONE_TITLE
			else if (strcmp(L->string, "colour-selected-bg2")==0) {
				if (!cJSON_IsArray(L))
					if (validate_colour(L, &colours[SchemeSel2][1]))
						continue;
				cJSON_AddNumberToObject(unsupported, "\"colour-selected-bg2\" must contain a string value", 0);
			}
			#endif // PATCH_TWO_TONE_TITLE

			#if PATCH_CLIENT_OPACITY
			else if (strcmp(L->string, "client-opacity-active")==0) {
				if (cJSON_IsNumeric(L)) {
					activeopacity = L->valuedouble;
					continue;
				}
				cJSON_AddNumberToObject(unsupported, "\"client-opacity-active\" must contain a numeric value", 0);
			}
			else if (strcmp(L->string, "client-opacity-enabled")==0) {
				if (json_isboolean(L)) {
					opacityenabled = L->valueint;
					continue;
				}
				cJSON_AddNumberToObject(unsupported, "\"client-opacity-enabled\" must contain a boolean value", 0);
			}
			else if (strcmp(L->string, "client-opacity-inactive")==0) {
				if (cJSON_IsNumeric(L)) {
					inactiveopacity = L->valuedouble;
					continue;
				}
				cJSON_AddNumberToObject(unsupported, "\"client-opacity-inactive\" must contain a numeric value", 0);
			}
			#endif // PATCH_CLIENT_OPACITY

			#if PATCH_TORCH
			else if (strcmp(L->string, "colours-torch")==0) {
				if (cJSON_IsArray(L))
					if (validate_colours(L, colours[SchemeTorch], NULL))
						continue;
				cJSON_AddNumberToObject(unsupported, "\"colours-torch\" must contain an array of strings", 0);
			}
			#endif // PATCH_TORCH

			else if (strcmp(L->string, "colours-urgent")==0) {
				if (cJSON_IsArray(L))
					if (validate_colours(L, colours[SchemeUrg], NULL))
						continue;
				cJSON_AddNumberToObject(unsupported, "\"colours-urgent\" must contain an array of strings", 0);
			}

			#if PATCH_ALTTAB
			#if PATCH_FONT_GROUPS
			else if (strcmp(L->string, "alt-tab-font-group")==0) {
				if (cJSON_IsString(L)) {
					tabFontgroup = L->valuestring;
					continue;
				}
				cJSON_AddNumberToObject(unsupported, "\"alt-tab-font-group\" must contain a string value", 0);
			}
			#endif // PATCH_FONT_GROUPS
			#if PATCH_ALTTAB_HIGHLIGHT
			else if (strcmp(L->string, "alt-tab-highlight")==0) {
				if (json_isboolean(L)) {
					tabHighlight = L->valueint;
					continue;
				}
				cJSON_AddNumberToObject(unsupported, "\"alt-tab-highlight\" must contain a boolean value", 0);
			}
			#endif // PATCH_ALTTAB_HIGHLIGHT
			else if (strcmp(L->string, "alt-tab-border")==0) {
				if (cJSON_IsInteger(L)) {
					tabBW = L->valueint;
					continue;
				}
				cJSON_AddNumberToObject(unsupported, "\"alt-tab-border\" must contain an integer value", 0);
			}
			else if (strcmp(L->string, "alt-tab-dropdown-vpad-extra")==0) {
				if (cJSON_IsInteger(L) && L->valueint >= 0) {
					tabMenuVertGap = L->valueint;
					continue;
				}
				cJSON_AddNumberToObject(unsupported, "\"alt-tab-dropdown-vpad-extra\" must contain an integer value", 0);
			}
			else if (strcmp(L->string, "alt-tab-dropdown-vpad-factor")==0) {
				if (cJSON_IsNumeric(L) && L->valuedouble >= 0) {
					tabMenuVertFactor = (float)L->valuedouble;
					continue;
				}
				cJSON_AddNumberToObject(unsupported, "\"alt-tab-dropdown-vpad-factor\" must contain a numeric value", 0);
			}
			else if (strcmp(L->string, "alt-tab-monitor-format")==0) {
				if (cJSON_IsString(L)) {
					monnumf = L->valuestring;
					continue;
				}
				cJSON_AddNumberToObject(unsupported, "\"alt-tab-monitor-format\" must contain a string value", 0);
			}
			else if (strcmp(L->string, "alt-tab-no-centre-dropdown")==0) {
				if (json_isboolean(L)) {
					tabMenuNoCentreAlign = L->valueint;
					continue;
				}
				cJSON_AddNumberToObject(unsupported, "\"no-centre-dropdown\" must contain a boolean value", 0);
			}
			else if (strcmp(L->string, "alt-tab-size")==0) {
				if (cJSON_IsString(L)) {
					char *ptr = NULL;
					unsigned int v = strtol(L->valuestring, &ptr, 10);
					if (v) {
						tabMaxW = v;
						if (ptr[0] != '\0') {
							if (ptr[0] == 'x' && ptr[1] != '\0') {
								v = strtol(&ptr[1], NULL, 10);
								if (v)
									tabMaxH = v;
								continue;
							}
						}
						else
							continue;
					}
				}
				cJSON_AddNumberToObject(unsupported, "\"alt-tab-size\" must contain a string value in the form \"WxH\", where W and H are numeric", 0);
			}
			else if (strcmp(L->string, "alt-tab-text-align")==0) {
				if (cJSON_IsInteger(L)) {
					switch (L->valueint) {
						case 0:
						case 1:
						case 2:
							tabTextAlign = L->valueint;
							continue;
					}
				}
				cJSON_AddNumberToObject(unsupported, "\"alt-tab-text-align\" must contain 0, 1 or 2", 0);
			}
			else if (strcmp(L->string, "alt-tab-x")==0) {
				if (cJSON_IsInteger(L)) {
					switch (L->valueint) {
						case 0:
						case 1:
						case 2:
							tabPosX = L->valueint;
							continue;
					}
				}
				cJSON_AddNumberToObject(unsupported, "\"alt-tab-x\" must contain 0, 1 or 2", 0);
			}
			else if (strcmp(L->string, "alt-tab-y")==0) {
				if (cJSON_IsInteger(L)) {
					switch (L->valueint) {
						case 0:
						case 1:
						case 2:
							tabPosY = L->valueint;
							continue;
					}
				}
				cJSON_AddNumberToObject(unsupported, "\"alt-tab-y\" must contain 0, 1 or 2", 0);
			}
			else if (strcmp(L->string, "colours-alt-tab-normal")==0) {
				if (cJSON_IsArray(L))
					if (validate_colours(L, colours[SchemeTabNorm], colours[SchemeNorm]))
						continue;
				cJSON_AddNumberToObject(unsupported, "\"colours-alt-tab-normal\" must contain an array of strings", 0);
			}
			else if (strcmp(L->string, "colours-alt-tab-selected")==0) {
				if (cJSON_IsArray(L))
					if (validate_colours(L, colours[SchemeTabSel], colours[SchemeSel]))
						continue;
				cJSON_AddNumberToObject(unsupported, "\"colours-alt-tab-selected\" must contain an array of strings", 0);
			}
			else if (strcmp(L->string, "colours-alt-tab-urgent")==0) {
				if (cJSON_IsArray(L))
					if (validate_colours(L, colours[SchemeTabUrg], colours[SchemeUrg]))
						continue;
				cJSON_AddNumberToObject(unsupported, "\"colours-alt-tab-urgent\" must contain an array of strings", 0);
			}
			#if PATCH_FLAG_HIDDEN
			else if (strcmp(L->string, "colours-alt-tab-hidden")==0) {
				if (cJSON_IsArray(L))
					if (validate_colours(L, colours[SchemeTabHide], colours[SchemeTabNorm]))
						continue;
				cJSON_AddNumberToObject(unsupported, "\"colours-alt-tab-hidden\" must contain an array of strings", 0);
			}
			#endif // PATCH_FLAG_HIDDEN
			#endif // PATCH_ALTTAB

			#if PATCH_CLASS_STACKING
			else if (strcmp(L->string,"class-stacking")==0) {
				if (json_isboolean(L)) {
					class_stacking = L->valueint;
					continue;
				}
				cJSON_AddNumberToObject(unsupported, "\"class-stacking\" must contain a boolean value", 0);
			}
			#endif // PATCH_CLASS_STACKING

			#if PATCH_COLOUR_BAR
			else if (strcmp(L->string, "colours-tag-bar")==0) {
				if (cJSON_IsArray(L))
					if (validate_colours(L, colours[SchemeTagBar], colours[SchemeNorm]))
						continue;
				cJSON_AddNumberToObject(unsupported, "\"colours-tag-bar\" must contain an array of strings", 0);
			}
			#if PATCH_FLAG_HIDDEN
			else if (strcmp(L->string, "colours-tag-bar-hidden")==0) {
				if (cJSON_IsArray(L))
					if (validate_colours(L, colours[SchemeTagBarHide], colours[SchemeHide]))
						continue;
				cJSON_AddNumberToObject(unsupported, "\"colours-tag-bar-hidden\" must contain an array of strings", 0);
			}
			#endif // PATCH_FLAG_HIDDEN
			else if (strcmp(L->string, "colours-tag-bar-selected")==0) {
				if (cJSON_IsArray(L))
					if (validate_colours(L, colours[SchemeTagBarSel], colours[SchemeSel]))
						continue;
				cJSON_AddNumberToObject(unsupported, "\"colours-tag-bar-selected\" must contain an array of strings", 0);
			}
			else if (strcmp(L->string, "colours-layout")==0) {
				if (cJSON_IsArray(L))
					if (validate_colours(L, colours[SchemeLayout], colours[SchemeNorm]))
						continue;
				cJSON_AddNumberToObject(unsupported, "\"colours-layout\" must contain an array of strings", 0);
			}
			else if (strcmp(L->string, "colours-title")==0) {
				if (cJSON_IsArray(L))
					if (validate_colours(L, colours[SchemeTitle], colours[SchemeNorm]))
						continue;
				cJSON_AddNumberToObject(unsupported, "\"colours-title\" must contain an array of strings", 0);
			}
			else if (strcmp(L->string, "colours-title-selected")==0) {
				if (cJSON_IsArray(L))
					if (validate_colours(L, colours[SchemeTitleSel], colours[SchemeSel]))
						continue;
				cJSON_AddNumberToObject(unsupported, "\"colours-title-selected\" must contain an array of strings", 0);
			}
			else if (strcmp(L->string, "colours-status")==0) {
				if (cJSON_IsArray(L))
					if (validate_colours(L, colours[SchemeStatus], colours[SchemeNorm]))
						continue;
				cJSON_AddNumberToObject(unsupported, "\"colours-status\" must contain an array of strings", 0);
			}
			#endif // PATCH_COLOUR_BAR

			#if PATCH_MOUSE_POINTER_HIDING
			else if (strcmp(L->string, "cursor-autohide")==0) {
				if (json_isboolean(L)) {
					cursorautohide = L->valueint;
					continue;
				}
				cJSON_AddNumberToObject(unsupported, "\"cursor-autohide\" must contain a boolean value", 0);
			}
			else if (strcmp(L->string, "cursor-autohide-delay")==0) {
				if (cJSON_IsInteger(L)) {
					cursortimeout = L->valueint;
					continue;
				}
				cJSON_AddNumberToObject(unsupported, "\"cursor-autohide-delay\" must contain an integer value", 0);
			}
			#endif // PATCH_MOUSE_POINTER_HIDING
			#if PATCH_CUSTOM_TAG_ICONS
			else if (strcmp(L->string, "show-custom-tag-icons")==0) {
				if (json_isboolean(L)) {
					showcustomtagicons = L->valueint;
					continue;
				}
				cJSON_AddNumberToObject(unsupported, "\"show-custom-tag-icons\" must contain a boolean value", 0);
			}
			else if (strcmp(L->string, "custom-tag-icons")==0) {
				if (cJSON_IsArray(L)) {
					int sz = cJSON_GetArraySize(L);
					if (sz <= 0)
						continue;
					if (sz <= LENGTH(tags)) {
						cJSON *e = NULL;
						for (int j = 0; j < sz; j++)
							if ((e = cJSON_GetArrayItem(L, j)) && cJSON_IsString(e))
								tagiconpaths[j] = e->valuestring;
						continue;
					}
					else {
						cJSON_AddNumberToObject(unsupported, "\"custom-tag-icons\" has too many elements", 0);
						continue;
					}
				}
				cJSON_AddNumberToObject(unsupported, "\"custom-tag-icons\" must contain an array of strings", 0);
			}
			#endif // PATCH_CUSTOM_TAG_ICONS
			#if PATCH_SHOW_DESKTOP
			else if (strcmp(L->string, "show-desktop")==0) {
				if (json_isboolean(L)) {
					showdesktop = L->valueint;
					continue;
				}
				cJSON_AddNumberToObject(unsupported, "\"show-desktop\" must contain a boolean value", 0);
			}
			#if PATCH_SHOW_DESKTOP_BUTTON
			else if (strcmp(L->string, "show-desktop-button-symbol")==0) {
				if (cJSON_IsString(L)) {
					showdesktop_button = L->valuestring;
					continue;
				}
				cJSON_AddNumberToObject(unsupported, "\"show-desktop-button-symbol\" must contain a string value", 0);
			}
			#endif // PATCH_SHOW_DESKTOP_BUTTON
			else if (strcmp(L->string, "show-desktop-layout-symbol")==0) {
				if (cJSON_IsString(L)) {
					desktopsymbol = L->valuestring;
					continue;
				}
				cJSON_AddNumberToObject(unsupported, "\"show-desktop-layout-symbol\" must contain a string value", 0);
			}
			#if PATCH_SHOW_DESKTOP_ONLY_WHEN_ACTIVE
			else if (strcmp(L->string, "show-desktop-when-active")==0) {
				if (json_isboolean(L)) {
					showdesktop_when_active = L->valueint;
					continue;
				}
				cJSON_AddNumberToObject(unsupported, "\"show-desktop-when-active\" must contain a boolean value", 0);
			}
			#endif // PATCH_SHOW_DESKTOP_ONLY_WHEN_ACTIVE
			#if PATCH_SHOW_DESKTOP_UNMANAGED
			else if (strcmp(L->string, "show-desktop-unmanaged")==0) {
				if (json_isboolean(L)) {
					showdesktop_unmanaged = L->valueint;
					continue;
				}
				cJSON_AddNumberToObject(unsupported, "\"show-desktop-unmanaged\" must contain a boolean value", 0);
			}
			#endif // PATCH_SHOW_DESKTOP_UNMANAGED
			#if PATCH_SHOW_DESKTOP_WITH_FLOATING
			else if (strcmp(L->string, "show-desktop-with-floating")==0) {
				if (json_isboolean(L)) {
					showdesktop_floating = L->valueint;
					continue;
				}
				cJSON_AddNumberToObject(unsupported, "\"show-desktop-with-floating\" must contain a boolean value", 0);
			}
			#endif // PATCH_SHOW_DESKTOP_WITH_FLOATING
			#endif // PATCH_SHOW_DESKTOP

			#if PATCH_WINDOW_ICONS
			#if PATCH_WINDOW_ICONS_ON_TAGS
			else if (strcmp(L->string, "show-icons-on-tags")==0) {
				if (json_isboolean(L)) {
					showiconsontags = L->valueint;
					continue;
				}
				cJSON_AddNumberToObject(unsupported, "\"show-icons-on-tags\" must contain a boolean value", 0);
			}
			#endif // PATCH_WINDOW_ICONS_ON_TAGS
			#endif // PATCH_WINDOW_ICONS

			else if (strcmp(L->string, "bar-layout")==0) {
				if (cJSON_IsArray(L)) {
					int sz = cJSON_GetArraySize(L);
					if (sz && sz <= LENGTH(barlayout)) {
						cJSON *e = NULL;
						for (int j = 0; j < LENGTH(barlayout); j++)
							barlayout[j] = 0;
						for (int j = 0, k, n = 0; j < sz; j++) {
							e = cJSON_GetArrayItem(L, j);
							if (cJSON_IsString(e)) {
								for (k = LENGTH(BarElementTypes); k; k--)
									if (BarElementTypes[k - 1].name && strcmp(BarElementTypes[k - 1].name, e->valuestring) == 0) {
										barlayout[n++] = BarElementTypes[k - 1].type;
										break;
									}
								if (!k)
									cJSON_AddStringToObject(unsupported, "\"bar-layout\" contained unknown element type", e->valuestring);
							}
						}
						continue;
					}
					else {
						cJSON_AddNumberToObject(unsupported, "\"bar-layout\" has too many elements", 0);
						continue;
					}
				}
				cJSON_AddNumberToObject(unsupported, "\"bar-layout\" must contain an array of strings", 0);
			}

			#if PATCH_STATUS_ALLOW_FIXED_MONITOR
			else if (strcmp(L->string, "status-allow-fixed-monitor")==0) {
				if (json_isboolean(L)) {
					status_allow_fixed_mon = L->valueint;
					continue;
				}
				cJSON_AddNumberToObject(unsupported, "\"status-allow-fixed-monitor\" must contain a boolean value", 0);
			}
			#endif // PATCH_STATUS_ALLOW_FIXED_MONITOR

			#if PATCH_STATUSCMD
			#if PATCH_STATUSCMD_COLOURS
			else if (strcmp(L->string, "status-colour-1")==0) {
				if (cJSON_IsString(L)) {
					colours[SchemeStatC1][ColFg] = L->valuestring;
					continue;
				}
				cJSON_AddNumberToObject(unsupported, "\"status-colour-1\" must contain a string value", 0);
			}
			else if (strcmp(L->string, "status-colour-2")==0) {
				if (cJSON_IsString(L)) {
					colours[SchemeStatC2][ColFg] = L->valuestring;
					continue;
				}
				cJSON_AddNumberToObject(unsupported, "\"status-colour-2\" must contain a string value", 0);
			}
			else if (strcmp(L->string, "status-colour-3")==0) {
				if (cJSON_IsString(L)) {
					colours[SchemeStatC3][ColFg] = L->valuestring;
					continue;
				}
				cJSON_AddNumberToObject(unsupported, "\"status-colour-3\" must contain a string value", 0);
			}
			else if (strcmp(L->string, "status-colour-4")==0) {
				if (cJSON_IsString(L)) {
					colours[SchemeStatC4][ColFg] = L->valuestring;
					continue;
				}
				cJSON_AddNumberToObject(unsupported, "\"status-colour-4\" must contain a string value", 0);
			}
			else if (strcmp(L->string, "status-colour-5")==0) {
				if (cJSON_IsString(L)) {
					colours[SchemeStatC5][ColFg] = L->valuestring;
					continue;
				}
				cJSON_AddNumberToObject(unsupported, "\"status-colour-5\" must contain a string value", 0);
			}
			else if (strcmp(L->string, "status-colour-6")==0) {
				if (cJSON_IsString(L)) {
					colours[SchemeStatC6][ColFg] = L->valuestring;
					continue;
				}
				cJSON_AddNumberToObject(unsupported, "\"status-colour-6\" must contain a string value", 0);
			}
			else if (strcmp(L->string, "status-colour-7")==0) {
				if (cJSON_IsString(L)) {
					colours[SchemeStatC7][ColFg] = L->valuestring;
					continue;
				}
				cJSON_AddNumberToObject(unsupported, "\"status-colour-7\" must contain a string value", 0);
			}
			else if (strcmp(L->string, "status-colour-8")==0) {
				if (cJSON_IsString(L)) {
					colours[SchemeStatC8][ColFg] = L->valuestring;
					continue;
				}
				cJSON_AddNumberToObject(unsupported, "\"status-colour-8\" must contain a string value", 0);
			}
			else if (strcmp(L->string, "status-colour-9")==0) {
				if (cJSON_IsString(L)) {
					colours[SchemeStatC9][ColFg] = L->valuestring;
					continue;
				}
				cJSON_AddNumberToObject(unsupported, "\"status-colour-9\" must contain a string value", 0);
			}
			else if (strcmp(L->string, "status-colour-10")==0) {
				if (cJSON_IsString(L)) {
					colours[SchemeStatC10][ColFg] = L->valuestring;
					continue;
				}
				cJSON_AddNumberToObject(unsupported, "\"status-colour-10\" must contain a string value", 0);
			}
			else if (strcmp(L->string, "status-colour-11")==0) {
				if (cJSON_IsString(L)) {
					colours[SchemeStatC11][ColFg] = L->valuestring;
					continue;
				}
				cJSON_AddNumberToObject(unsupported, "\"status-colour-11\" must contain a string value", 0);
			}
			else if (strcmp(L->string, "status-colour-12")==0) {
				if (cJSON_IsString(L)) {
					colours[SchemeStatC12][ColFg] = L->valuestring;
					continue;
				}
				cJSON_AddNumberToObject(unsupported, "\"status-colour-12\" must contain a string value", 0);
			}
			else if (strcmp(L->string, "status-colour-13")==0) {
				if (cJSON_IsString(L)) {
					colours[SchemeStatC13][ColFg] = L->valuestring;
					continue;
				}
				cJSON_AddNumberToObject(unsupported, "\"status-colour-13\" must contain a string value", 0);
			}
			else if (strcmp(L->string, "status-colour-14")==0) {
				if (cJSON_IsString(L)) {
					colours[SchemeStatC14][ColFg] = L->valuestring;
					continue;
				}
				cJSON_AddNumberToObject(unsupported, "\"status-colour-14\" must contain a string value", 0);
			}
			else if (strcmp(L->string, "status-colour-15")==0) {
				if (cJSON_IsString(L)) {
					colours[SchemeStatC15][ColFg] = L->valuestring;
					continue;
				}
				cJSON_AddNumberToObject(unsupported, "\"status-colour-15\" must contain a string value", 0);
			}
			#if PATCH_STATUSCMD_COLOURS_DECOLOURIZE
			else if (strcmp(L->string, "status-decolourize-inactive")==0) {
				if (json_isboolean(L)) {
					status_decolourize_inactive = L->valueint;
					continue;
				}
				cJSON_AddNumberToObject(unsupported, "\"status-decolourize-inactive\" must contain a boolean value", 0);
			}
			#endif // PATCH_STATUSCMD_COLOURS_DECOLOURIZE
			#endif // PATCH_STATUSCMD_COLOURS
			#endif // PATCH_STATUSCMD

			else if (strcmp(L->string, "title-align")==0) {
				if (cJSON_IsInteger(L)) {
					title_align = L->valueint;
					continue;
				}
				cJSON_AddNumberToObject(unsupported, "\"title-align\" must contain an integer value", 0);
			}

			else if (strcmp(L->string, "title-border-width")==0) {
				if (cJSON_IsInteger(L)) {
					titleborderpx = L->valueint;
					continue;
				}
				cJSON_AddNumberToObject(unsupported, "\"title-border-width\" must contain a numeric value", 0);
			}

			else if (strcmp(L->string, "top-bar")==0) {
				if (json_isboolean(L)) {
					topbar = L->valueint;
					continue;
				}
				cJSON_AddNumberToObject(unsupported, "\"top-bar\" must contain a boolean value", 0);
			}

			#if PATCH_SHOW_MASTER_CLIENT_ON_TAG
			else if (strcmp(L->string, "bar-tag-format-empty")==0) {
				if (cJSON_IsString(L)) {
					etagf = L->valuestring;
					continue;
				}
				cJSON_AddNumberToObject(unsupported, "\"bar-tag-format-empty\" must contain a string value", 0);
			}
			else if (strcmp(L->string, "bar-tag-format-populated")==0) {
				if (cJSON_IsString(L)) {
					ptagf = L->valuestring;
					continue;
				}
				cJSON_AddNumberToObject(unsupported, "\"bar-tag-format-populated\" must contain a string value", 0);
			}
			else if (strcmp(L->string, "bar-tag-format-reversed")==0) {
				if (json_isboolean(L)) {
					reverselbl = L->valueint;
					continue;
				}
				cJSON_AddNumberToObject(unsupported, "\"bar-tag-format-reversed\" must contain a boolean value", 0);
			}
			#endif // PATCH_SHOW_MASTER_CLIENT_ON_TAG

			#if PATCH_BORDERLESS_SOLITARY_CLIENTS
			else if (strcmp(L->string, "borderless-solitary")==0) {
				if (json_isboolean(L)) {
					borderless_solitary = L->valueint;
					continue;
				}
				cJSON_AddNumberToObject(unsupported, "\"borderless-solitary\" must contain a boolean value", 0);
			}
			#endif // PATCH_BORDERLESS_SOLITARY_CLIENTS

			else if (strcmp(L->string, "border-width")==0) {
				if (cJSON_IsInteger(L)) {
					borderpx = L->valueint;
					continue;
				}
				cJSON_AddNumberToObject(unsupported, "\"border-width\" must contain a numeric value", 0);
			}

			#if PATCH_WINDOW_ICONS
			#if PATCH_WINDOW_ICONS_DEFAULT_ICON
			else if (strcmp(L->string, "default-icon")==0) {
				if (cJSON_IsString(L)) {
					default_icon = L->valuestring;
					continue;
				}
				cJSON_AddNumberToObject(unsupported, "\"default-icon\" must contain a string value", 0);
			}
			#if PATCH_SHOW_DESKTOP
			else if (strcmp(L->string, "desktop-icon")==0) {
				if (cJSON_IsString(L)) {
					desktop_icon = L->valuestring;
					continue;
				}
				cJSON_AddNumberToObject(unsupported, "\"desktop-icon\" must contain a string value", 0);
			}
			#endif // PATCH_SHOW_DESKTOP
			#endif // PATCH_WINDOW_ICONS_DEFAULT_ICON
			#endif // PATCH_WINDOW_ICONS

			else if (strcmp(L->string, "default-tags")==0) {
				if (cJSON_IsArray(L)) {
					i = 0;
					for (cJSON *t = L->child; t; t = t->next)
						if (cJSON_IsString(t)) {
							tags[i++] = t->valuestring;
							if (i >= LENGTH(tags))
								break;
						}
						else {
							i = 0;
							break;
						}
					if (i)
						continue;
				}
				cJSON_AddNumberToObject(unsupported, "\"default-tags\" must contain an array of single character string values", 0);
			}

			#if PATCH_FOCUS_BORDER
			else if (strcmp(L->string, "focus-border-size")==0) {
				if (cJSON_IsInteger(L)) {
					fh = L->valueint;
					continue;
				}
				cJSON_AddNumberToObject(unsupported, "\"focus-border-size\" must contain an integer value", 0);
			}
			else if (strcmp(L->string, "focus-border-edge")==0) {
				if (cJSON_IsString(L)) {
					if (strcmp(L->valuestring, "N")==0) {
						fbpos = FOCUS_BORDER_N;
						continue;
					}
					else if (strcmp(L->valuestring, "S")==0) {
						fbpos = FOCUS_BORDER_S;
						continue;
					}
					else if (strcmp(L->valuestring, "E")==0) {
						fbpos = FOCUS_BORDER_E;
						continue;
					}
					else if (strcmp(L->valuestring, "W")==0) {
						fbpos = FOCUS_BORDER_W;
						continue;
					}
				}
				cJSON_AddNumberToObject(unsupported, "\"focus-border-edge\" must contain \"N\", \"S\", \"E\" or \"W\"", 0);
			}
			#elif PATCH_FOCUS_PIXEL
			else if (strcmp(L->string, "focus-pixel-size")==0) {
				if (cJSON_IsInteger(L)) {
					fh = L->valueint;
					continue;
				}
				cJSON_AddNumberToObject(unsupported, "\"focus-pixel-size\" must contain an integer value", 0);
			}
			else if (strcmp(L->string, "focus-pixel-corner")==0) {
				if (cJSON_IsString(L)) {
					if (strcmp(L->valuestring, "NE")==0) {
						fppos = FOCUS_PIXEL_NE;
						continue;
					}
					else if (strcmp(L->valuestring, "SE")==0) {
						fppos = FOCUS_PIXEL_SE;
						continue;
					}
					else if (strcmp(L->valuestring, "SW")==0) {
						fppos = FOCUS_PIXEL_SW;
						continue;
					}
					else if (strcmp(L->valuestring, "NW")==0) {
						fppos = FOCUS_PIXEL_NW;
						continue;
					}
				}
				cJSON_AddNumberToObject(unsupported, "\"focus-pixel-corner\" must contain \"NE\", \"SE\", \"SW\" or \"NW\"", 0);
			}
			#endif // PATCH_FOCUS_BORDER || PATCH_FOCUS_PIXEL

			#if PATCH_WINDOW_ICONS
			else if (strcmp(L->string, "icon-size")==0) {
				if (cJSON_IsInteger(L)) {
					iconsize = L->valueint;
					continue;
				}
				cJSON_AddNumberToObject(unsupported, "\"icon-size\" must contain an integer value", 0);
			}
			#if PATCH_ALTTAB
			else if (strcmp(L->string, "icon-size-big")==0) {
				if (cJSON_IsInteger(L)) {
					iconsize_big = L->valueint;
					continue;
				}
				cJSON_AddNumberToObject(unsupported, "\"icon-size-big\" must contain an integer value", 0);
			}
			#endif // PATCH_ALTTAB
			else if (strcmp(L->string, "icon-spacing")==0) {
				if (cJSON_IsInteger(L)) {
					iconspacing = L->valueint;
					continue;
				}
				cJSON_AddNumberToObject(unsupported, "\"icon-spacing\" must contain an integer value", 0);
			}
			#endif // PATCH_WINDOW_ICONS

			#if PATCH_FONT_GROUPS
			else if (strcmp(L->string, "font-groups")==0) {
				if (cJSON_IsArray(L) || cJSON_IsObject(L)) {
					fontgroups_json = L;
					continue;
				}
				cJSON_AddNumberToObject(unsupported, "\"font-groups\" must contain a JSON object or array of objects", 0);
			}
			#endif // PATCH_FONT_GROUPS

			else if (strcmp(L->string, "fonts")==0) {
				if (cJSON_IsArray(L) || cJSON_IsString(L)) {
					fonts_json = L;
					continue;
				}
				cJSON_AddNumberToObject(unsupported, "\"fonts\" must contain a string value or array of string values", 0);
			}
			#if PATCH_HIDE_VACANT_TAGS
			else if (strcmp(L->string, "hide-vacant-tags")==0) {
				if (json_isboolean(L)) {
					hidevacant = L->valueint;
					continue;
				}
				cJSON_AddNumberToObject(unsupported, "\"hide-vacant-tags\" must contain a boolean value", 0);
			}
			#endif // PATCH_HIDE_VACANT_TAGS

			#if PATCH_MIRROR_LAYOUT
			else if (strcmp(L->string, "mirror-layout")==0) {
				if (json_isboolean(L)) {
					mirror_layout = L->valueint;
					continue;
				}
				cJSON_AddNumberToObject(unsupported, "\"mirror-layout\" must contain a boolean value", 0);
			}
			#endif // PATCH_MIRROR_LAYOUT

			#if PATCH_SHOW_MASTER_CLIENT_ON_TAG
			else if (strcmp(L->string, "showmaster")==0) {
				if (json_isboolean(L)) {
					showmaster = L->valueint;
					continue;
				}
				cJSON_AddNumberToObject(unsupported, "\"showmaster\" must contain a boolean value", 0);
			}
			#endif // PATCH_SHOW_MASTER_CLIENT_ON_TAG

			#if PATCH_SYSTRAY
			else if (strcmp(L->string, "system-tray")==0) {
				if (json_isboolean(L)) {
					showsystray = L->valueint;
					continue;
				}
				cJSON_AddNumberToObject(unsupported, "\"system-tray\" must contain a boolean value", 0);
			}
			else if (strcmp(L->string, "system-tray-align")==0) {
				if (cJSON_IsInteger(L)) {
					switch (L->valueint) {
						case 0:
							systrayonleft = 1;
							continue;
						case 1:
							systrayonleft = 0;
							continue;
					}
				}
				cJSON_AddNumberToObject(unsupported, "\"system-tray-align\" must contain 0 or 1", 0);
			}
			else if (strcmp(L->string, "system-tray-pinning")==0) {
				if (cJSON_IsInteger(L)) {
					systraypinning = L->valueint;
					continue;
				}
				cJSON_AddNumberToObject(unsupported, "\"system-tray-pinning\" must contain an integer value", 0);
			}
			else if (strcmp(L->string, "system-tray-spacing")==0) {
				if (cJSON_IsInteger(L)) {
					systrayspacing = L->valueint;
					continue;
				}
				cJSON_AddNumberToObject(unsupported, "\"system-tray-spacing\" must contain an integer value", 0);
			}
			#endif // PATCH_SYSTRAY

			#if PATCH_VANITY_GAPS
			else if (strcmp(L->string, "vanity-gaps")==0) {
				if (json_isboolean(L)) {
					defgaps = L->valueint;
					continue;
				}
				cJSON_AddNumberToObject(unsupported, "\"vanity-gaps\" must contain a boolean value", 0);
			}

			else if (strcmp(L->string, "vanity-gaps-inner-h")==0) {
				if (cJSON_IsInteger(L)) {
					gappih = L->valueint;
					continue;
				}
				cJSON_AddNumberToObject(unsupported, "\"vanity-gaps-inner-h\" must contain a numeric value", 0);
			}
			else if (strcmp(L->string, "vanity-gaps-inner-v")==0) {
				if (cJSON_IsInteger(L)) {
					gappiv = L->valueint;
					continue;
				}
				cJSON_AddNumberToObject(unsupported, "\"vanity-gaps-inner-v\" must contain a numeric value", 0);
			}
			else if (strcmp(L->string, "vanity-gaps-outer-h")==0) {
				if (cJSON_IsInteger(L)) {
					gappoh = L->valueint;
					continue;
				}
				cJSON_AddNumberToObject(unsupported, "\"vanity-gaps-outer-h\" must contain a numeric value", 0);
			}
			else if (strcmp(L->string, "vanity-gaps-outer-v")==0) {
				if (cJSON_IsInteger(L)) {
					gappov = L->valueint;
					continue;
				}
				cJSON_AddNumberToObject(unsupported, "\"vanity-gaps-outer-v\" must contain a numeric value", 0);
			}
			#endif // PATCH_VANITY_GAPS

			#if PATCH_TERMINAL_SWALLOWING
			else if (strcmp(L->string, "terminal-swallowing")==0) {
				if (json_isboolean(L)) {
					terminal_swallowing = L->valueint;
					continue;
				}
				cJSON_AddNumberToObject(unsupported, "\"terminal-swallowing\" must contain a boolean value", 0);
			}
			#endif // PATCH_TERMINAL_SWALLOWING

			else if (strcmp(L->string, "urgency-hinting")==0) {
				if (json_isboolean(L)) {
					urgency = L->valueint;
					continue;
				}
				cJSON_AddNumberToObject(unsupported, "\"urgency-hinting\" must contain a boolean value", 0);
			}

			else if (strcmp(L->string, "view-on-tag")==0) {
				if (json_isboolean(L)) {
					viewontag = L->valueint;
					continue;
				}
				cJSON_AddNumberToObject(unsupported, "\"view-on-tag\" must contain a boolean value", 0);
			}

			#if PATCH_MOUSE_POINTER_WARPING
			else if (strcmp(L->string, "mouse-warping-enabled")==0) {
				if (json_isboolean(L)) {
					mousewarp_disable = !L->valueint;
					continue;
				}
				cJSON_AddNumberToObject(unsupported, "\"mouse-warping-enabled\" must contain a boolean value", 0);
			}
			#if PATCH_MOUSE_POINTER_WARPING_SMOOTH
			else if (strcmp(L->string, "mouse-warping-smoothly")==0) {
				if (json_isboolean(L)) {
					mousewarp_smoothly = L->valueint;
					continue;
				}
				cJSON_AddNumberToObject(unsupported, "\"mouse-warping-smoothly\" must contain a boolean value", 0);
			}
			#endif // PATCH_MOUSE_POINTER_WARPING_SMOOTH
			#endif // PATCH_MOUSE_POINTER_WARPING

			else if (strcmp(L->string, "monitors")==0) {
				if (!(cJSON_IsArray(L))) {
					cJSON_AddNumberToObject(unsupported, "\"monitors\" must contain an array of monitor objects", 0);
					continue;
				}

				monitors_json = L;
				for (cJSON *m = L->child; m; m = m->next) {

					// check for unsupported names in monitor objects;
					for (c = m->child; c; c = c->next) {
						if (c->string) {
							found = 0;
							for (i = 0; i < LENGTH(supported_layout_mon); i++)
								if (strcmp((&supported_layout_mon[i])->name, c->string) == 0) {
									found = 1;
									break;
								}

							if (!found) {
								if(strstr(c->string, "//") == c->string || strstr(c->string, "#") == c->string || strstr(c->string, ";") == c->string)
									continue;
								string = cJSON_GetObjectItemCaseSensitive(unsupported_mon, c->string);
								if (string) cJSON_SetNumberValue(string, string->valuedouble + 1);
								else cJSON_AddNumberToObject(unsupported_mon, c->string, 1);
								continue;
							}
							#if PATCH_LOG_DIAGNOSTICS
							else if (strcmp(c->string,"log-rules")==0 && !json_isboolean(c)) {
								cJSON_AddNumberToObject(unsupported_mon, "\"log-rules\" must contain a boolean value", 0);
							}
							#endif // PATCH_LOG_DIAGNOSTICS

							else if (strcmp(c->string, "set-bar-layout")==0) {
								if (!cJSON_IsArray(c))
									cJSON_AddNumberToObject(unsupported_mon, "\"set-bar-layout\" must contain an array of strings", 0);
								else {
									int sz = cJSON_GetArraySize(c);
									if (sz && sz <= LENGTH(barlayout)) {
										cJSON *e = NULL;
										for (int j = 0, k; j < sz; j++) {
											e = cJSON_GetArrayItem(c, j);
											if (cJSON_IsString(e)) {
												for (k = LENGTH(BarElementTypes); k; k--)
													if (BarElementTypes[k - 1].name && strcmp(BarElementTypes[k - 1].name, e->valuestring) == 0)
														break;
												if (!k)
													cJSON_AddStringToObject(unsupported_mon, "\"set-bar-layout\" contained unknown element type", e->valuestring);
											}
										}
									}
									else
										cJSON_AddNumberToObject(unsupported_mon, "\"set-bar-layout\" has too many elements", 0);
								}
							}

							#if PATCH_FONT_GROUPS
							else if (strcmp(L->string, "set-bar-element-font-groups")==0 && !(cJSON_IsArray(L) || cJSON_IsObject(L))) {
								cJSON_AddNumberToObject(unsupported_mon, "\"set-bar-element-font-groups\" must contain a JSON object or array of objects", 0);
							}
							#endif // PATCH_FONT_GROUPS

							else if (strcmp(c->string, "set-title-align")==0 && !cJSON_IsInteger(c)) {
								cJSON_AddNumberToObject(unsupported_mon, "\"set-title-align\" must contain an integer value", 0);
							}

							#if PATCH_CLASS_STACKING
							else if (strcmp(c->string,"set-class-stacking")==0 && !json_isboolean(c)) {
								cJSON_AddNumberToObject(unsupported_mon, "\"set-class-stacking\" must contain a boolean value", 0);
							}
							#endif // PATCH_CLASS_STACKING

							#if PATCH_MOUSE_POINTER_HIDING
							else if (strcmp(c->string,"set-cursor-autohide")==0 && !json_isboolean(c)) {
								cJSON_AddNumberToObject(unsupported_mon, "\"set-cursor-autohide\" must contain a boolean value", 0);
							}
							else if (strcmp(c->string,"set-cursor-hide-on-keys")==0 && !json_isboolean(c)) {
								cJSON_AddNumberToObject(unsupported_mon, "\"set-cursor-hide-on-keys\" must contain a boolean value", 0);
							}
							#endif // PATCH_MOUSE_POINTER_HIDING

							else if (strcmp(c->string,"set-default")==0 && !json_isboolean(c)) {
								cJSON_AddNumberToObject(unsupported_mon, "\"set-default\" must contain a boolean value", 0);
							}

							#if PATCH_VANITY_GAPS
							else if (strcmp(c->string,"set-enable-gaps")==0 && !json_isboolean(c)) {
								cJSON_AddNumberToObject(unsupported_mon, "\"set-enable-gaps\" must contain a boolean value", 0);
							}
							else if (strcmp(c->string, "set-gap-inner-h")==0 && !cJSON_IsInteger(c)) {
								cJSON_AddNumberToObject(unsupported_mon, "\"set-gap-inner-h\" must contain a numeric value", 0);
							}
							else if (strcmp(c->string, "set-gap-inner-v")==0 && !cJSON_IsInteger(c)) {
								cJSON_AddNumberToObject(unsupported_mon, "\"set-gap-inner-v\" must contain a numeric value", 0);
							}
							else if (strcmp(c->string, "set-gap-outer-h")==0 && !cJSON_IsInteger(c)) {
								cJSON_AddNumberToObject(unsupported_mon, "\"set-gap-outer-h\" must contain a numeric value", 0);
							}
							else if (strcmp(c->string, "set-gap-outer-v")==0 && !cJSON_IsInteger(c)) {
								cJSON_AddNumberToObject(unsupported_mon, "\"set-gap-outer-v\" must contain a numeric value", 0);
							}
							#endif // PATCH_VANITY_GAPS

							#if PATCH_CLIENT_INDICATORS
							else if (strcmp(c->string,"set-indicators-top")==0 && !json_isboolean(c)) {
								cJSON_AddNumberToObject(unsupported_mon, "\"set-indicators-top\" must contain a boolean value", 0);
							}
							#endif // PATCH_CLIENT_INDICATORS

							else if (strcmp(c->string, "set-mfact")==0 && !cJSON_IsNumber(c)) {
								cJSON_AddNumberToObject(unsupported_mon, "\"set-mfact\" must contain a numeric value", 0);
							}
							#if PATCH_MIRROR_LAYOUT
							else if (strcmp(c->string,"set-mirror-layout")==0 && !json_isboolean(c)) {
								cJSON_AddNumberToObject(unsupported_mon, "\"set-mirror-layout\" must contain a boolean value", 0);
							}
							#endif // PATCH_MIRROR_LAYOUT

							else if (strcmp(c->string, "set-nmaster")==0 && !cJSON_IsInteger(c)) {
								cJSON_AddNumberToObject(unsupported_mon, "\"set-nmaster\" must contain an integer value", 0);
							}

							#if PATCH_ALT_TAGS
							else if (strcmp(c->string,"set-quiet-alt-tags")==0 && !json_isboolean(c)) {
								cJSON_AddNumberToObject(unsupported_mon, "\"set-quiet-alt-tags\" must contain a boolean value", 0);
							}
							#endif // PATCH_ALT_TAGS

							else if (strcmp(c->string,"set-showbar")==0 && !json_isboolean(c)) {
								cJSON_AddNumberToObject(unsupported_mon, "\"set-showbar\" must contain a boolean value", 0);
							}
							#if PATCH_SHOW_MASTER_CLIENT_ON_TAG
							else if (strcmp(c->string,"set-reverse-master")==0 && !json_isboolean(c)) {
								cJSON_AddNumberToObject(unsupported_mon, "\"set-reverse-master\" must contain a boolean value", 0);
							}
							else if (strcmp(c->string,"set-showmaster")==0 && !json_isboolean(c)) {
								cJSON_AddNumberToObject(unsupported_mon, "\"set-showmaster\" must contain a boolean value", 0);
							}
							#endif // PATCH_SHOW_MASTER_CLIENT_ON_TAG

							else if (strcmp(c->string, "set-showstatus")==0 && !cJSON_IsInteger(c)) {
								cJSON_AddNumberToObject(unsupported_mon, "\"set-showstatus\" must contain an integer value", 0);
							}

							#if PATCH_VIRTUAL_MONITORS
							else if (strcmp(c->string, "set-split-enabled")==0 && !json_isboolean(c)) {
								cJSON_AddNumberToObject(unsupported_mon, "\"set-split-enabled\" must contain a boolean value", 0);
							}
							else if (strcmp(c->string, "set-split-type")==0 && !cJSON_IsInteger(c)) {
								cJSON_AddNumberToObject(unsupported_mon, "\"set-split-type\" must contain an integer value", 0);
							}
							#endif // PATCH_VIRTUAL_MONITORS

							else if (strcmp(c->string, "set-start-tag")==0 && !cJSON_IsInteger(c)) {
								cJSON_AddNumberToObject(unsupported_mon, "\"set-start-tag\" must contain an integer value", 0);
							}

							#if PATCH_SWITCH_TAG_ON_EMPTY
							else if (strcmp(c->string, "set-switch-on-empty")==0 && !cJSON_IsInteger(c)) {
								cJSON_AddNumberToObject(unsupported_mon, "\"set-switch-on-empty\" must contain an integer value", 0);
							}
							#endif // PATCH_SWITCH_TAG_ON_EMPTY

							else if (strcmp(c->string,"set-topbar")==0 && !json_isboolean(c)) {
								cJSON_AddNumberToObject(unsupported_mon, "\"set-topbar\" must contain a boolean value", 0);
							}

							if (strcmp(c->string,"tags")==0 && cJSON_IsArray(c)) {

								// check for unsupported names in tag objects;
								for (cJSON *t = c->child; t; t = t->next)
									if (cJSON_IsObject(t))
										for (cJSON *e = t->child; e; e = e->next)
											if (e->string) {
												found = 0;
												for (i = 0; i < LENGTH(supported_layout_tag); i++)
													if (strcmp((&supported_layout_tag[i])->name, e->string) == 0) {
														found = 1;
														break;
													}

												if (!found) {
													if(strstr(e->string, "//") == e->string || strstr(e->string, "#") == e->string || strstr(e->string, ";") == e->string)
														continue;
													string = cJSON_GetObjectItemCaseSensitive(unsupported_tag, e->string);
													if (string) cJSON_SetNumberValue(string, string->valuedouble + 1);
													else cJSON_AddNumberToObject(unsupported_tag, e->string, 1);
												}
											}

							}
						}
					}

				}
			}

			else if (strcmp(L->string, "process-no-sigterm")==0) {
				if (cJSON_IsArray(L)) {
					badprocs = L;
					continue;
				}
				cJSON_AddNumberToObject(unsupported, "\"process-no-sigterm\" must contain an array of strings", 0);
			}
			else if (strcmp(L->string, "process-parents")==0) {
				if (cJSON_IsArray(L)) {
					procparents = L;
					continue;
				}
				cJSON_AddNumberToObject(unsupported, "\"process-parents\" must contain an array of json objects", 0);
			}

		}
	}

	for (c = unsupported->child; c; c = c->next) {
		if (cJSON_IsString(c)) {
			logdatetime(stderr);
			fprintf(stderr, "dwm: %s: %s.\n", c->string, c->valuestring);
		}
		else if (config_warnings) {
			logdatetime(stderr);
			if (c->valueint > 1)
				fprintf(stderr, "dwm: warning: parameter ignored - not supported in main section: (%lux) \"%s\".\n", c->valueint, c->string);
			else if (c->valueint)
				fprintf(stderr, "dwm: warning: parameter ignored - not supported in main section: \"%s\".\n", c->string);
			else
				fprintf(stderr, "dwm: warning: parameter ignored - %s.\n", c->string);
		}
	}

	for (c = unsupported_mon->child; c; c = c->next) {
		if (cJSON_IsString(c)) {
			logdatetime(stderr);
			fprintf(stderr, "dwm: %s: %s.\n", c->string, c->valuestring);
		}
		else if (config_warnings) {
			logdatetime(stderr);
			if (c->valueint > 1)
				fprintf(stderr, "dwm: warning: parameter ignored - not supported in monitor section: (%lux) \"%s\".\n", c->valueint, c->string);
			else if (c->valueint)
				fprintf(stderr, "dwm: warning: parameter ignored - not supported in monitor section: \"%s\".\n", c->string);
			else
				fprintf(stderr, "dwm: warning: parameter ignored in monitor section - %s.\n", c->string);
		}
	}

	if (config_warnings) {
		for (c = unsupported_tag->child; c; c = c->next) {
			logdatetime(stderr);
			if (c->valueint > 1)
				fprintf(stderr, "dwm: warning: parameter ignored - not supported in tag section: (%lux) \"%s\".\n", c->valueint, c->string);
			else if (c->valueint)
				fprintf(stderr, "dwm: warning: parameter ignored - not supported in tag section: \"%s\".\n", c->string);
			else
				fprintf(stderr, "dwm: warning: parameter ignored in tag section - %s.\n", c->string);
		}
	}
	
	cJSON_Delete(unsupported);
	cJSON_Delete(unsupported_mon);
	cJSON_Delete(unsupported_tag);

	return 1;
}

// Apply run-time config parameters to monitor;
void
parsemon(Monitor *m, int index, int first)
{
	if (monitors_json) {

		unsigned int i;
		cJSON *l_node;
		for (cJSON *l_json = monitors_json->child; l_json; l_json = l_json->next) {

			cJSON *l_mon = cJSON_GetObjectItemCaseSensitive(l_json, "monitor");
			if (!cJSON_IsInteger(l_mon) || l_mon->valueint != index)
				continue;

			#if PATCH_LOG_DIAGNOSTICS
			m->logallrules = ((l_node = cJSON_GetObjectItemCaseSensitive(l_json, "log-rules")) && json_isboolean(l_node)) ? l_node->valueint : m->logallrules;
			#endif // PATCH_LOG_DIAGNOSTICS

			l_node = cJSON_GetObjectItemCaseSensitive(l_json, "set-bar-layout");
			if (l_node && cJSON_IsArray(l_node)) {
				int sz = cJSON_GetArraySize(l_node);
				if (sz && sz <= LENGTH(barlayout)) {
					for (int j = 0; j < LENGTH(m->barlayout); j++)
						m->barlayout[j] = 0;
					cJSON *e = NULL;
					for (int j = 0, k, n = 0; j < sz; j++) {
						e = cJSON_GetArrayItem(l_node, j);
						if (cJSON_IsString(e))
							for (k = 0; k < LENGTH(BarElementTypes); k++)
								if (BarElementTypes[k].name && strcmp(BarElementTypes[k].name, e->valuestring) == 0) {
									m->barlayout[n++] = BarElementTypes[k].type;
									break;
								}
					}
				}
			}
			m->title_align = ((l_node = cJSON_GetObjectItemCaseSensitive(l_json, "set-title-align")) && cJSON_IsInteger(l_node)) ? l_node->valueint : m->title_align;

			#if PATCH_MOUSE_POINTER_HIDING
			m->cursorautohide = ((l_node = cJSON_GetObjectItemCaseSensitive(l_json, "set-cursor-autohide")) && json_isboolean(l_node)) ? l_node->valueint : m->cursorautohide;
			m->cursorhideonkeys = ((l_node = cJSON_GetObjectItemCaseSensitive(l_json, "set-cursor-hide-on-keys")) && json_isboolean(l_node)) ? l_node->valueint : m->cursorhideonkeys;
			#endif // PATCH_MOUSE_POINTER_HIDING

			#if PATCH_ALTTAB
			m->tabBW = ((l_node = cJSON_GetObjectItemCaseSensitive(l_json, "set-alt-tab-border")) && cJSON_IsInteger(l_node)) ? l_node->valueint : m->tabBW;
			if ((l_node = cJSON_GetObjectItemCaseSensitive(l_json, "set-alt-tab-size")) && cJSON_IsString(l_node)) {
				char *ptr = NULL;
				unsigned int v = strtol(l_node->valuestring, &ptr, 10);
				if (v) {
					m->tabMaxW = v;
					if (ptr[0] != '\0') {
						if (ptr[0] == 'x' && ptr[1] != '\0') {
							v = strtol(&ptr[1], NULL, 10);
							if (v)
								m->tabMaxH = v;
						}
					}
				}
			}
			m->tabTextAlign = ((l_node = cJSON_GetObjectItemCaseSensitive(l_json, "set-alt-tab-text-align")) && cJSON_IsInteger(l_node)) ? l_node->valueint : m->tabTextAlign;
			m->tabPosX = ((l_node = cJSON_GetObjectItemCaseSensitive(l_json, "set-alt-tab-x")) && cJSON_IsInteger(l_node)) ? l_node->valueint : m->tabPosX;
			m->tabPosY = ((l_node = cJSON_GetObjectItemCaseSensitive(l_json, "set-alt-tab-y")) && cJSON_IsInteger(l_node)) ? l_node->valueint : m->tabPosY;
			#endif // PATCH_ALTTAB

			#if PATCH_FONT_GROUPS
			if ((l_node = cJSON_GetObjectItemCaseSensitive(l_json, "set-bar-element-font-groups")) && (cJSON_IsArray(l_node) || cJSON_IsObject(l_node))) {
				m->barelement_fontgroups_json = l_node;
				cJSON *el = NULL, *nom;
				int j, n = -1, fg_minbh = 0;
				Fnt *f;
				#if PATCH_CLIENT_INDICATORS
				int tagbar_bh = 0;
				#endif // PATCH_CLIENT_INDICATORS
				if(cJSON_IsArray(l_node))
					n = cJSON_GetArraySize(l_node);
				else
					el = l_node;

				// iterate through bar element font groups;
				for (i = 0; i < abs(n); i++) {
					if (n > 0)
						el = cJSON_GetArrayItem(l_node, i);
					if (!(nom = cJSON_GetObjectItemCaseSensitive(el, "bar-element")) || !cJSON_IsString(nom))
						continue;

					// check if named bar-element is current;
					for (j = LENGTH(BarElementTypes); j > 0; j--)
						if (BarElementTypes[j - 1].name &&
							strcmp(BarElementTypes[j - 1].name, nom->valuestring) == 0
							)
							break;
					if (!j)
						continue;

					if ((el = cJSON_GetObjectItemCaseSensitive(el, "font-group")) && cJSON_IsString(el) &&
						(f = drw_get_fontgroup_fonts(drw, el->valuestring))
					) {
						fg_minbh = f->h + 2;

						#if PATCH_CLIENT_INDICATORS
						if (BarElementTypes[j - 1].type == TagBar && (fg_minbh > tagbar_bh))
							tagbar_bh = fg_minbh;
						#endif // PATCH_CLIENT_INDICATORS

						if (m->minbh < fg_minbh)
							m->minbh = fg_minbh;
					}
				}
				if(!m->minbh)
					m->bh = m->minbh = minbh;
				else
					if (m->bh < m->minbh)
						m->bh = m->minbh;
				#if PATCH_CLIENT_INDICATORS
				if(!tagbar_bh)
					tagbar_bh = m->minbh;
				if (tagbar_bh + client_ind_size > m->bh)
					m->bh = tagbar_bh + client_ind_size;
				else if (tagbar_bh + (2 * client_ind_size) <= m->bh)
					client_ind_offset = 0;
				#endif // PATCH_CLIENT_INDICATORS
			}
			#endif // PATCH_FONT_GROUPS

			#if PATCH_ALT_TAGS
			m->alttagsquiet = ((l_node = cJSON_GetObjectItemCaseSensitive(l_json, "set-quiet-alt-tags")) && json_isboolean(l_node)) ? l_node->valueint : m->alttagsquiet;
			#endif // PATCH_ALT_TAGS
			#if PATCH_CLIENT_OPACITY
			m->activeopacity = ((l_node = cJSON_GetObjectItemCaseSensitive(l_json, "set-opacity-active")) && cJSON_IsNumber(l_node)) ? l_node->valuedouble : m->activeopacity;
			m->inactiveopacity = ((l_node = cJSON_GetObjectItemCaseSensitive(l_json, "set-opacity-inactive")) && cJSON_IsNumber(l_node)) ? l_node->valuedouble : m->inactiveopacity;
			#endif // PATCH_CLIENT_OPACITY
			m->isdefault = ((l_node = cJSON_GetObjectItemCaseSensitive(l_json, "set-default")) && json_isboolean(l_node)) ? l_node->valueint : m->isdefault;
			#if PATCH_HIDE_VACANT_TAGS
			m->hidevacant = ((l_node = cJSON_GetObjectItemCaseSensitive(l_json, "set-hide-vacant-tags")) && json_isboolean(l_node)) ? l_node->valueint : m->hidevacant;
			#endif // PATCH_HIDE_VACANT_TAGS
			#if PATCH_SHOW_MASTER_CLIENT_ON_TAG
			m->showmaster = ((l_node = cJSON_GetObjectItemCaseSensitive(l_json, "set-showmaster")) && json_isboolean(l_node)) ? l_node->valueint : m->showmaster;
			m->reversemaster = ((l_node = cJSON_GetObjectItemCaseSensitive(l_json, "set-reverse-master")) && json_isboolean(l_node)) ? l_node->valueint : m->reversemaster;
			m->etagf = ((l_node = cJSON_GetObjectItemCaseSensitive(l_json, "set-tag-format-empty")) && cJSON_IsString(l_node)) ? l_node->valuestring : m->etagf;
			m->ptagf = ((l_node = cJSON_GetObjectItemCaseSensitive(l_json, "set-tag-format-populated")) && cJSON_IsString(l_node)) ? l_node->valuestring : m->ptagf;
			#endif // PATCH_SHOW_MASTER_CLIENT_ON_TAG
			#if PATCH_CUSTOM_TAG_ICONS
			m->showcustomtagicons = ((l_node = cJSON_GetObjectItemCaseSensitive(l_json, "set-show-custom-tag-icons")) && json_isboolean(l_node)) ? l_node->valueint : m->showcustomtagicons;
			if ((l_node = cJSON_GetObjectItemCaseSensitive(l_json, "set-custom-tag-icons")) && cJSON_IsArray(l_node)) {
				int sz = cJSON_GetArraySize(l_node);
				if (sz && sz <= LENGTH(tags)) {
					cJSON *e = NULL;
					for (int j = 0; j < sz; j++)
						if ((e = cJSON_GetArrayItem(l_node, j)) && cJSON_IsString(e))
							m->tagiconpaths[j] = e->valuestring;
				}
			}
			#endif // PATCH_CUSTOM_TAG_ICONS
			#if PATCH_WINDOW_ICONS && PATCH_WINDOW_ICONS_ON_TAGS
			m->showiconsontags = ((l_node = cJSON_GetObjectItemCaseSensitive(l_json, "set-show-icons-on-tags")) && json_isboolean(l_node)) ? l_node->valueint : m->showiconsontags;
			#endif // PATCH_WINDOW_ICONS && PATCH_WINDOW_ICONS_ON_TAGS
			m->showstatus = ((l_node = cJSON_GetObjectItemCaseSensitive(l_json, "set-showstatus")) && cJSON_IsInteger(l_node)) ? l_node->valueint : m->showstatus;
			#if PATCH_VIRTUAL_MONITORS
			if (m->num < 1000)
				m->split = ((l_node = cJSON_GetObjectItemCaseSensitive(l_json, "set-split-type")) && cJSON_IsInteger(l_node)) ? l_node->valueint : m->split;
			#endif // PATCH_VIRTUAL_MONITORS
			#if PATCH_SWITCH_TAG_ON_EMPTY
			m->switchonempty = ((l_node = cJSON_GetObjectItemCaseSensitive(l_json, "set-switch-on-empty")) && cJSON_IsInteger(l_node)) ? l_node->valueint : m->switchonempty;
			if (m->switchonempty > LENGTH(tags))
				m->switchonempty = 0;
			#endif // PATCH_SWITCH_TAG_ON_EMPTY
			m->defaulttag = ((l_node = cJSON_GetObjectItemCaseSensitive(l_json, "set-start-tag")) && cJSON_IsInteger(l_node)) ? l_node->valueint : m->defaulttag;
			m->topbar = ((l_node = cJSON_GetObjectItemCaseSensitive(l_json, "set-topbar")) && json_isboolean(l_node)) ? l_node->valueint : m->topbar;
			#if PATCH_CLIENT_INDICATORS
			m->client_ind_top = ((l_node = cJSON_GetObjectItemCaseSensitive(l_json, "set-indicators-top")) && json_isboolean(l_node)) ? l_node->valueint : (m->client_ind_top == -1 ? m->topbar : m->client_ind_top);
			#endif // PATCH_CLIENT_INDICATORS

			if (first) {

				#if PATCH_CLASS_STACKING
				m->class_stacking = ((l_node = cJSON_GetObjectItemCaseSensitive(l_json, "set-class-stacking")) && json_isboolean(l_node)) ? l_node->valueint : m->class_stacking;
				#endif // PATCH_CLASS_STACKING
				#if PATCH_VANITY_GAPS
				m->enablegaps = ((l_node = cJSON_GetObjectItemCaseSensitive(l_json, "set-enable-gaps")) && json_isboolean(l_node)) ? l_node->valueint : m->enablegaps;
				m->gappih = ((l_node = cJSON_GetObjectItemCaseSensitive(l_json, "set-gap-inner-h")) && cJSON_IsInteger(l_node)) ? l_node->valueint : m->gappih;
				m->gappiv = ((l_node = cJSON_GetObjectItemCaseSensitive(l_json, "set-gap-inner-v")) && cJSON_IsInteger(l_node)) ? l_node->valueint : m->gappiv;
				m->gappoh = ((l_node = cJSON_GetObjectItemCaseSensitive(l_json, "set-gap-outer-h")) && cJSON_IsInteger(l_node)) ? l_node->valueint : m->gappoh;
				m->gappov = ((l_node = cJSON_GetObjectItemCaseSensitive(l_json, "set-gap-outer-v")) && cJSON_IsInteger(l_node)) ? l_node->valueint : m->gappov;
				#endif // PATCH_VANITY_GAPS
				m->mfact_def = m->mfact = ((l_node = cJSON_GetObjectItemCaseSensitive(l_json, "set-mfact")) && cJSON_IsNumber(l_node)) ? l_node->valuedouble : m->mfact;
				#if PATCH_MIRROR_LAYOUT
				m->mirror = ((l_node = cJSON_GetObjectItemCaseSensitive(l_json, "set-mirror-layout")) && json_isboolean(l_node)) ? l_node->valueint : m->mirror;
				#endif // PATCH_MIRROR_LAYOUT
				m->nmaster = ((l_node = cJSON_GetObjectItemCaseSensitive(l_json, "set-nmaster")) && cJSON_IsInteger(l_node)) ? l_node->valueint : m->nmaster;
				m->showbar = ((l_node = cJSON_GetObjectItemCaseSensitive(l_json, "set-showbar")) && json_isboolean(l_node)) ? l_node->valueint : m->showbar;
				#if PATCH_VIRTUAL_MONITORS
				if (m->num < 1000)
					m->enablesplit = ((l_node = cJSON_GetObjectItemCaseSensitive(l_json, "set-split-enabled")) && json_isboolean(l_node)) ? l_node->valueint : m->enablesplit;
				#endif // PATCH_VIRTUAL_MONITORS

				#if PATCH_HIDE_VACANT_TAGS
				for (i = 0; i < LENGTH(tags); i++)
					m->alwaysvisible[i] = 0;
				#endif // PATCH_HIDE_VACANT_TAGS

				m->lt[0] = &layouts[0];
				l_node = cJSON_GetObjectItemCaseSensitive(l_json, "set-layout");
				if (l_node) {
					if (cJSON_IsString(l_node))
						l_node->valueint = layoutstringtoindex(l_node->valuestring);
					if ((l_node->valueint) && (l_node->valueint >= 0 && l_node->valueint < LENGTH(layouts)))
						m->lt[0] = &layouts[l_node->valueint];
				}
				m->lt[1] = &layouts[1 % LENGTH(layouts)];

			}

			cJSON *tags_json = cJSON_GetObjectItemCaseSensitive(l_json, "tags");

			// propagate monitor settings per tag;
			for (i = 0; i <= LENGTH(tags); i++) {
				#if PATCH_PERTAG
				#if PATCH_SWITCH_TAG_ON_EMPTY
				m->pertag->switchonempty[i] = m->switchonempty;
				#endif // PATCH_SWITCH_TAG_ON_EMPTY
				#if PATCH_MOUSE_POINTER_HIDING
				m->pertag->cursorautohide[i] = m->cursorautohide;
				m->pertag->cursorhideonkeys[i] = m->cursorhideonkeys;
				#endif // PATCH_MOUSE_POINTER_HIDING
				#if PATCH_ALT_TAGS
				m->pertag->alttagsquiet[i] = m->alttagsquiet;
				#endif // PATCH_ALT_TAGS
				if (first) {
					m->pertag->nmasters[i] = m->nmaster;
					m->pertag->mfacts_def[i] = m->pertag->mfacts[i] = m->mfact;
					m->pertag->ltidxs[i][0] = m->lt[0];
					m->pertag->showbars[i] = m->showbar;
					m->pertag->ltidxs[i][1] = m->lt[1];
					m->pertag->enablegaps[i] = m->enablegaps;
					#if PATCH_CLASS_STACKING
					m->pertag->class_stacking[i] = m->class_stacking;
					#endif // PATCH_CLASS_STACKING
				}
				#endif // PATCH_PERTAG
				if (cJSON_IsArray(tags_json)) {

					for (cJSON *t_json = tags_json->child; t_json; t_json = t_json->next) {
						l_node = cJSON_GetObjectItemCaseSensitive(t_json, "index");
						if (cJSON_IsInteger(l_node) && l_node->valueint == i) {
							#if PATCH_HIDE_VACANT_TAGS
							m->alwaysvisible[i-1] = ((l_node = cJSON_GetObjectItemCaseSensitive(t_json, "set-always-visible")) && json_isboolean(l_node)) ? l_node->valueint : m->alwaysvisible[i];
							#endif // PATCH_HIDE_VACANT_TAGS
							#if PATCH_ALT_TAGS
							m->tags[i-1] = ((l_node = cJSON_GetObjectItemCaseSensitive(t_json, "set-tag-text")) && cJSON_IsString(l_node)) ? l_node->valuestring : m->tags[i-1];
							#endif // PATCH_ALT_TAGS
							#if PATCH_PERTAG
							#if PATCH_ALT_TAGS
							m->pertag->alttagsquiet[i] = ((l_node = cJSON_GetObjectItemCaseSensitive(t_json, "set-quiet-alt-tags")) && json_isboolean(l_node)) ? l_node->valueint : m->alttagsquiet;
							#endif // PATCH_ALT_TAGS
							#if PATCH_MOUSE_POINTER_HIDING
							m->pertag->cursorautohide[i] = ((l_node = cJSON_GetObjectItemCaseSensitive(t_json, "set-cursor-autohide")) && json_isboolean(l_node)) ? l_node->valueint : m->cursorautohide;
							m->pertag->cursorhideonkeys[i] = ((l_node = cJSON_GetObjectItemCaseSensitive(t_json, "set-cursor-hide-on-keys")) && json_isboolean(l_node)) ? l_node->valueint : m->cursorhideonkeys;
							#endif // PATCH_MOUSE_POINTER_HIDING
							#if PATCH_SWITCH_TAG_ON_EMPTY
							m->pertag->switchonempty[i] = ((l_node = cJSON_GetObjectItemCaseSensitive(t_json, "set-switch-on-empty")) && cJSON_IsInteger(l_node)) ? l_node->valueint : m->switchonempty;
							if (m->pertag->switchonempty[i] > LENGTH(tags))
								m->pertag->switchonempty[i] = 0;
							#endif // PATCH_SWITCH_TAG_ON_EMPTY
							
							if (first) {
								#if PATCH_CLASS_STACKING
								m->pertag->class_stacking[i] = ((l_node = cJSON_GetObjectItemCaseSensitive(t_json, "set-class-stacking")) && json_isboolean(l_node)) ? l_node->valueint : m->class_stacking;
								#endif // PATCH_CLASS_STACKING
								m->pertag->enablegaps[i] = ((l_node = cJSON_GetObjectItemCaseSensitive(t_json, "set-enable-gaps")) && json_isboolean(l_node)) ? l_node->valueint : m->enablegaps;
								m->pertag->mfacts_def[i] = m->pertag->mfacts[i] = ((l_node = cJSON_GetObjectItemCaseSensitive(t_json, "set-mfact")) && cJSON_IsNumber(l_node)) ? l_node->valuedouble : m->mfact;
								m->pertag->nmasters[i] = ((l_node = cJSON_GetObjectItemCaseSensitive(t_json, "set-nmaster")) && cJSON_IsInteger(l_node)) ? l_node->valueint : m->nmaster;
								m->pertag->showbars[i] = ((l_node = cJSON_GetObjectItemCaseSensitive(t_json, "set-showbar")) && json_isboolean(l_node)) ? l_node->valueint : m->showbar;
								
								l_node = cJSON_GetObjectItemCaseSensitive(t_json, "set-layout");
								if (l_node) {
									if (cJSON_IsString(l_node))
										l_node->valueint = layoutstringtoindex(l_node->valuestring);
									if ((l_node->valueint) && (l_node->valueint >= 0 && l_node->valueint < LENGTH(layouts))) {
										if (l_node->valueint == 1) {
											m->pertag->ltidxs[i][1] = m->pertag->ltidxs[i][0];
											m->pertag->ltidxs[i][0] = &layouts[l_node->valueint];
										}
										else m->pertag->ltidxs[i][0] = &layouts[l_node->valueint];
									}
								}

							}
							#endif // PATCH_PERTAG
							break;
						}
					}

				}

			}
			#if PATCH_PERTAG
			#if PATCH_MOUSE_POINTER_HIDING
			m->cursorautohide = m->pertag->cursorautohide[1];
			m->cursorhideonkeys = m->pertag->cursorhideonkeys[1];
			#endif // PATCH_MOUSE_POINTER_HIDING
			#if PATCH_ALT_TAGS
			m->alttagsquiet = m->pertag->alttagsquiet[1];
			#endif // PATCH_ALT_TAGS
			#if PATCH_SWITCH_TAG_ON_EMPTY
			m->switchonempty = m->pertag->switchonempty[1];
			#endif // PATCH_SWITCH_TAG_ON_EMPTY
			if (first) {
				m->lt[0] = m->pertag->ltidxs[1][0];
				m->lt[1] = m->pertag->ltidxs[1][1];
				m->mfact_def = m->mfact = m->pertag->mfacts[1];
				m->nmaster = m->pertag->nmasters[1];
				m->showbar = m->pertag->showbars[1];
				m->enablegaps = m->pertag->enablegaps[1];
				#if PATCH_CLASS_STACKING
				m->class_stacking = m->pertag->class_stacking[1];
				#endif // PATCH_CLASS_STACKING
			}
			#endif // PATCH_PERTAG
		}
	}
	#if PATCH_FONT_GROUPS
	if (!m->bh)
		m->bh = bh;
	if (!m->minbh)
		m->minbh = minbh;
	#endif // PATCH_FONT_GROUPS
	if (!first)
		return;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstringop-truncation"
	strncpy(m->ltsymbol, m->lt[0]->symbol, sizeof m->ltsymbol);
#pragma GCC diagnostic pop
}

int
parserulesjson(cJSON *rules)
{
	cJSON *unsupported = ecalloc(1, sizeof(cJSON));
	cJSON *unsupported_values = ecalloc(1, sizeof(cJSON));
	unsigned int unmatchable = 0;
	cJSON *c = NULL;
	cJSON *next = NULL;
	cJSON *stack = NULL;
	cJSON *last = NULL;

	const char *supported_rules_type_errors[] = {
		[R_A]    = "an array",
		[R_BOOL] = "a boolean value (true/false)",
		[R_I]    = "an integer",
		[R_N]    = "a numeric value",
		[R_S]    = "a text string",
	};
	char buffer[256];

	for (cJSON *r = rules->child; r; ) {

		cJSON *tags = cJSON_GetObjectItemCaseSensitive(r, "set-tags-mask");
		if (cJSON_IsString(tags) && (tags->valuestring != NULL)) {
			unsigned int t = (unsigned int) ParseExpression(tags->valuestring);
			cJSON *newvalue = cJSON_CreateInteger(t);
			if (newvalue)
				cJSON_ReplaceItemInObjectCaseSensitive(r, "set-tags-mask", newvalue);
		}

		// check for unsupported names;
		for (c = r->child; c; c = next) {
			next = c->next;
			if (c->string) {
				int found = 0;
				for (int i = 0; i < LENGTH(supported_rules); i++)
					if (strcmp((&supported_rules[i])->name, c->string) == 0) {

						found = 0;
						int types = (&supported_rules[i])->types;
						if (types != R_IGNORE) {
							if (cJSON_IsArray(c) && !(types & R_A))
								strncpy(buffer, "does not support arrays", sizeof buffer - 1);
							else if (cJSON_IsString(c) && !(types & R_S))
								strncpy(buffer, "does not support text strings", sizeof buffer - 1);
							else if (cJSON_IsNumber(c) && !(types & R_N))
								strncpy(buffer, "does not support non-integer numeric values", sizeof buffer - 1);
							else if (cJSON_IsInteger(c) && !(types & R_I)) {
								if (!(json_isboolean(c) && (types & R_BOOL)))
									strncpy(buffer, "does not support integers", sizeof buffer - 1);
								else
									found = 1;
							}
							else if (cJSON_IsBool(c) && !(types & R_BOOL))
								strncpy(buffer, "does not support boolean values", sizeof buffer - 1);
							else
								found = 1;

							if (!found) {
								size_t len = strlen(buffer);
								if (types & R_A)
									snprintf(
										buffer + len, sizeof buffer - len, "; expects %s (or %s)",
										supported_rules_type_errors[types & ~R_A], supported_rules_type_errors[R_A]
									);
								else
									snprintf(buffer + len, sizeof buffer - len, "; expects %s", supported_rules_type_errors[types]);

								cJSON_AddStringToObject(unsupported_values, c->string, buffer);
							}
						}
						else
							found = 1;

						if (!found) {
							cJSON_Delete(cJSON_DetachItemViaPointer(r, c));
							found = 1;
						}

						break;
					}

				if (!found) {
					if(strstr(c->string, "//") == c->string || strstr(c->string, "#") == c->string || strstr(c->string, ";") == c->string) {
						cJSON_Delete(cJSON_DetachItemViaPointer(r, c));
						continue;
					}
					cJSON *string = cJSON_GetObjectItemCaseSensitive(unsupported, c->string);
					if (string) cJSON_SetNumberValue(string, string->valuedouble + 1);
					else cJSON_AddNumberToObject(unsupported, c->string, 1);
					cJSON_Delete(cJSON_DetachItemViaPointer(r, c));
				}
			}
		}

		if (!(
				STRINGMATCHABLE(r, "if-class") ||
				STRINGMATCHABLE(r, "if-instance") ||
				STRINGMATCHABLE(r, "if-role") ||
				STRINGMATCHABLE(r, "if-title") ||
				STRINGMATCHABLE(r, "if-not-class") ||
				STRINGMATCHABLE(r, "if-not-instance") ||
				STRINGMATCHABLE(r, "if-not-role") ||
				STRINGMATCHABLE(r, "if-not-title") ||
				STRINGMATCHABLE(r, "if-parent-title")
		))
			unmatchable++;

		c = cJSON_GetObjectItemCaseSensitive(r, "exclusive");
		if (json_isboolean(c) && (c->valueint)) {
			// move exclusive rules to the stack holder;
			next = r->next;
			if (stack) {
				stack->next = cJSON_DetachItemViaPointer(rules, r);
				stack->next->prev = stack;
				stack = stack->next;
			}
			else stack = cJSON_DetachItemViaPointer(rules, r);
			r = next;
			continue;
		}
		last = r;
		r = r->next;
	}

	// move any exclusive rules back to the bottom of the list;
	if (stack) {
		for (; stack->prev; stack = stack->prev);
		if (last) {
			last->next = stack;
			stack->prev = last;
		}
		else rules->child = stack;
	}

	if (config_warnings) {
		if (unmatchable > 1) {
			logdatetime(stderr);
			fprintf(stderr, "dwm: warning: rule skipped - missing string matching criteria (%ux).\n", unmatchable);
		}
		else if (unmatchable) {
			logdatetime(stderr);
			fprintf(stderr, "dwm: warning: rule skipped - missing string matching criteria.\n");
		}

		for (c = unsupported->child; c; c = c->next) {
			logdatetime(stderr);
			if (c->valueint > 1)
				fprintf(stderr, "dwm: warning: rule parameter ignored - not supported: (%lux) \"%s\".\n", c->valueint, c->string);
			else
				fprintf(stderr, "dwm: warning: rule parameter ignored - not supported: \"%s\".\n", c->string);
		}

		for (c = unsupported_values->child; c; c = c->next) {
			logdatetime(stderr);
			fprintf(stderr, "dwm: warning: rule parameter ignored - \"%s\" %s.\n", c->string, c->valuestring);
		}
	}

	cJSON_Delete(unsupported);
	cJSON_Delete(unsupported_values);

	return 1;
}

void
placemouse(const Arg *arg)
{
	#if PATCH_FOCUS_FOLLOWS_MOUSE
	checkmouseoverclient();
	#endif // PATCH_FOCUS_FOLLOWS_MOUSE
	int x, y, px, py, ocx, ocy, nx = -9999, ny = -9999, freemove = 0;
	Client *c, *r = NULL, *at, *prevr;
	Monitor *m;
	XEvent ev;
	XWindowAttributes wa;
	Time lasttime = 0;
	int attachmode, prevattachmode;
	attachmode = prevattachmode = -1;

	if (!(c = selmon->sel) || c->dormant || !c->mon->lt[c->mon->sellt]->arrange) /* no support for placemouse when floating layout is used */
		return;
	if (c->isfullscreen
		#if PATCH_FLAG_FAKEFULLSCREEN
		&& c->fakefullscreen != 1
		#endif // PATCH_FLAG_FAKEFULLSCREEN
		) /* no support placing fullscreen windows by mouse */
		return;
	restack(selmon);
	prevr = c;
	if (XGrabPointer(dpy, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync,
		None, cursor[CurMove]->cursor, CurrentTime) != GrabSuccess)
		return;

	c->isfloating &= ~1;
	c->beingmoved = 1;

	XGetWindowAttributes(dpy, c->win, &wa);
	ocx = wa.x;
	ocy = wa.y;

	if (arg->i == 2) // warp cursor to client centre
		XWarpPointer(dpy, None, c->win, 0, 0, 0, 0, WIDTH(c) / 2, HEIGHT(c) / 2);

	if (!getrootptr(&x, &y))
		return;

	#if PATCH_FOCUS_BORDER || PATCH_FOCUS_PIXEL
	if (focuswin)
		XUnmapWindow(dpy, focuswin);
	#endif // PATCH_FOCUS_BORDER || PATCH_FOCUS_PIXEL

	do {
		XMaskEvent(dpy, MOUSEMASK|ExposureMask|SubstructureRedirectMask, &ev);
		switch (ev.type) {
		case ConfigureRequest:
		case Expose:
		case MapRequest:
			handler[ev.type](&ev);
			break;
		case MotionNotify:
			if ((ev.xmotion.time - lasttime) <= (1000 / 60))
				continue;
			lasttime = ev.xmotion.time;

			nx = ocx + (ev.xmotion.x - x);
			ny = ocy + (ev.xmotion.y - y);

			if (!freemove && (abs(nx - ocx) > snap || abs(ny - ocy) > snap))
				freemove = 1;

			if (freemove)
				XMoveWindow(dpy, c->win, nx, ny);

			if ((m = recttomon(ev.xmotion.x, ev.xmotion.y, 1, 1)) && m != selmon)
				selmon = m;

			if (arg->i == 1) { // tiled position is relative to the client window centre point
				px = nx + wa.width / 2;
				py = ny + wa.height / 2;
			} else { // tiled position is relative to the mouse cursor
				px = ev.xmotion.x;
				py = ev.xmotion.y;
			}

			r = recttoclient(px, py, 1, 1, False);

			if (!r || r == c)
				break;

			attachmode = 0; // below
			if (((float)(r->y + r->h - py) / r->h) > ((float)(r->x + r->w - px) / r->w)) {
				if (abs(r->y - py) < r->h / 2)
					attachmode = 1; // above
			} else if (abs(r->x - px) < r->w / 2)
				attachmode = 1; // above

			if ((r && r != prevr) || (attachmode != prevattachmode)) {
				detachstack(c);
				detach(c);
				if (c->mon != r->mon) {
					#if PATCH_SHOW_DESKTOP
					#if PATCH_SHOW_DESKTOP_ONLY_WHEN_ACTIVE
					int nc = 0;
					if (getdesktopclient(c->mon, &nc) && !nc && !c->mon->showdesktop) {
						c->mon->showdesktop = 1;
						arrange(c->mon);
					}
					else
					#endif // PATCH_SHOW_DESKTOP_ONLY_WHEN_ACTIVE
					#endif // PATCH_SHOW_DESKTOP
					arrangemon(c->mon);
					c->tags = r->mon->tagset[r->mon->seltags];
				}

				c->mon = r->mon;
				c->monindex = r->mon->num;
				r->mon->sel = r;

				if (attachmode) {
					if (r == r->mon->clients)
						attach(c);
					else {
						for (at = r->mon->clients; at->next != r; at = at->next);
						c->next = at->next;
						at->next = c;
					}
				} else {
					c->next = r->next;
					r->next = c;
				}

				attachstack(c);
				arrangemon(r->mon);
				prevr = r;
				prevattachmode = attachmode;
			}
			break;
		}
	} while (ev.type != ButtonRelease);
	XUngrabPointer(dpy, CurrentTime);

	if ((m = recttomon(ev.xmotion.x, ev.xmotion.y, 1, 1)) && m != c->mon) {
		sendmon(c, m, c, 0);
		c->monindex = c->mon->num;
		selmon = c->mon;
		if (m != c->mon)
			nx = -9999;
	}

	focus(c, 1);
	c->beingmoved = 0;

	if (nx != -9999)
		resize(c, nx, ny, c->w, c->h, 0);

	snapchildclients(c, 1);

	//arrangemon(c->mon);
	arrange(NULL);

	#if PATCH_FOCUS_BORDER || PATCH_FOCUS_PIXEL
	if (focuswin) {
		drawfocusborder(0);
		XMapWindow(dpy, focuswin);
	}
	#endif // PATCH_FOCUS_BORDER || PATCH_FOCUS_PIXEL
}

#if PATCH_MOUSE_POINTER_WARPING || PATCH_FOCUS_FOLLOWS_MOUSE
int
pointoverbar(Monitor *m, int x, int y, int check_clients)
{
	unsigned int num;
	Window d1, d2, *wins = NULL;
	int a, w = 1, h = 1;
	int i;
	Client *c;

	if (x < 0 && y < 0)
		getrootptr(&x, &y);
	/*
	Monitor *m;

	// is the point over a bar;
	for (m = mons; m; m = m->next)
		if (m->barvisible &&
			x >= m->mx && x <= m->mx+m->mw &&
			y >= (m->topbar ? m->my : m->my+m->mh-bh) &&
			y <= (m->topbar ? m->my+bh : m->my+m->mh)
		)
		break;
	// not over a bar;
	if (!m)
		return 0;
	*/
	if (!m->barvisible ||
		x < m->mx || x > m->mx+m->mw ||
		y < (m->topbar ? m->my : m->my+m->mh-bh) ||
		y > (m->topbar ? m->my+bh : m->my+m->mh)
		)
		return 0;

	// XQueryTree returns windows in stacking order top to bottom;
	if (check_clients && XQueryTree(dpy, root, &d1, &d2, &wins, &num)) {
		if (num > 0) {
			for (i = 0; i < num; i++) {
				if ((c = wintoclient(wins[i])) && ISVISIBLE(c)
					#if PATCH_FLAG_HIDDEN
					&& !c->ishidden
					#endif // PATCH_FLAG_HIDDEN
				) {
					if ((a = INTERSECTC(x, y, w, h, c)))
						return 0;
				}
			}
		}
		if (wins)
			XFree(wins);
	}

	return 1;
}
#endif // PATCH_MOUSE_POINTER_WARPING || PATCH_FOCUS_FOLLOWS_MOUSE

void
populate_charcode_map(void)
{
	int keycodes_length = 0, idx = 0;
	int keycode, group, groups, level, modmask, num_map;
	int keycode_high, keycode_low;
	int keysyms_per_keycode;

	XDisplayKeycodes(dpy, &keycode_low, &keycode_high);
	XModifierKeymap *modmap = XGetModifierMapping(dpy);
	KeySym *keysyms = XGetKeyboardMapping(
		dpy, keycode_low,
		keycode_high - keycode_low + 1,
		&keysyms_per_keycode

	);
	XFree(keysyms);

	// Add 2 to the size because the range [low, high] is inclusive;
	// Add 2 more for tab (\t) and newline (\n);
	keycodes_length = ((keycode_high - keycode_low) + 1) * keysyms_per_keycode;

	charcodes = calloc(keycodes_length, sizeof(charcodemap_t));
	XkbDescPtr desc = XkbGetMap(dpy, XkbAllClientInfoMask, XkbUseCoreKbd);

	for (keycode = keycode_low; keycode <= keycode_high; keycode++) {
		groups = XkbKeyNumGroups(desc, keycode);
		for (group = 0; group < groups; group++) {
			XkbKeyTypePtr key_type = XkbKeyKeyType(desc, keycode, group);
			for (level = 0; level < key_type->num_levels; level++) {
				KeySym keysym = XkbKeycodeToKeysym(dpy, keycode, group, level);
				modmask = 0;

				for (num_map = 0; num_map < key_type->map_count; num_map++) {
					XkbKTMapEntryRec map = key_type->map[num_map];
					if (map.active && map.level == level) {
						modmask = map.mods.mask;
						break;
					}
				}

				charcodes[idx].key = (wchar_t)xkb_keysym_to_utf32(keysym);
				charcodes[idx].code = keycode;
				charcodes[idx].group = group;
				charcodes[idx].modmask = modmask | keycode_to_modifier(modmap, keycode);
				charcodes[idx].symbol = keysym;

				idx++;
			}
		}
	}
	charcodes_len = idx;
	XkbFreeKeyboard(desc, 0, 1);
	XFreeModifiermap(modmap);
}

#if PATCH_IPC
void
print_socket_reply(void)
{
	IPCMessageType reply_type;
	uint32_t reply_size;
	char *reply;

	read_socket(&reply_type, &reply_size, &reply);

	if (reply) {
		printf("%.*s\n", reply_size, reply);
		fflush(stdout);
		free(reply);
	}
}
#endif // PATCH_IPC

void
print_supported_json(FILE *f, const supported_json array[], const size_t len, const char *title, const char *indent)
{
	size_t i;
	size_t colw = 0;
	size_t w;
	struct winsize window_size = {0};
	unsigned int wrap_length = WRAP_LENGTH;
	if (ioctl(fileno(f), TIOCGWINSZ, &window_size) != -1) {
		wrap_length = window_size.ws_col;
	}

	for (i = 0; i < len; i++) {
		w = strlen(array[i].name);
		if (w > colw)
			colw = w;
	}

	print_wrap(f, wrap_length, NULL, -1, title, NULL, NULL, NULL);

	for (i = 0; i < len; i++) {
		print_wrap(f, wrap_length, indent, colw, array[i].name, " - ", NULL, array[i].help);
	}
	fputs("\n", f);
}

void
print_supported_rules_json(FILE *f, const supported_rules_json array[], const size_t len, const char *title, const char *indent)
{
	size_t i;
	size_t colw = 0;
	size_t w;
	struct winsize window_size = {0};
	unsigned int wrap_length = WRAP_LENGTH;
	if (ioctl(fileno(f), TIOCGWINSZ, &window_size) != -1) {
		wrap_length = window_size.ws_col;
	}

	for (i = 0; i < len; i++) {
		w = strlen(array[i].name);
		if (w > colw)
			colw = w;
	}

	print_wrap(f, wrap_length, NULL, -1, title, NULL, NULL, NULL);

	for (i = 0; i < len; i++) {
		print_wrap(f, wrap_length, indent, colw, array[i].name, " - ", NULL, array[i].help);
	}
	fputs("\n", f);
}

// print 1 - 2 columns, wrapping at col1_size and/or terminal width;
void
print_wrap(FILE *f,
	size_t line_length,
	const char *indent,
	size_t col1_size,
	const char *col1_text,
	const char *line1_gap,
	const char *normal_gap,
	const char *col2_text
) {
	size_t usable = line_length - (indent ? strlen(indent) : 0);
	size_t col2_size;

	size_t gapsize = (line1_gap ? strlen(line1_gap) : 1);

	if (normal_gap) {
		col2_size = strlen(normal_gap);
		if (col2_size > gapsize)
			gapsize = col2_size;
	}

	char gap[gapsize + 1];

	size_t col2_usable = usable - gapsize;
	if (col1_size == -1 || col1_size > usable) {
		col1_size = usable;
		col2_size = 0;
		if (!col2_text)
			col2_usable = 0;
	}
	else if (col2_text) {
		col2_usable = col2_size = (col2_usable - col1_size);
	}
	else
		col2_size = col2_usable = 0;

	char c1buff[col1_size + 1];
	char c2buff[col2_usable + 1];

	int first = 1;
	int c1done = 0;
	int c2done = (col2_text ? 0 : 2);
	size_t col1_index = 0;
	size_t col2_index = 0;
	size_t i;

	if (!c2done) {
		if (line1_gap)
			strncpy(gap, line1_gap, gapsize + 1);	// first line gap is line1_gap;
		else if (normal_gap)
			strncpy(gap, normal_gap, gapsize + 1);	// first line gap is normal_gap;
		else {
			// first line gap will be a single space;
			for (i = 0; i < gapsize; i++)
				gap[i] = ' ';
			gap[i] = '\0';
		}
	}

	while (!c1done || !c2done) {
		if (!c1done) {
			// column 1 not finished, fill column 1 buffer;
			c1done = line_to_buffer(col1_text, c1buff, sizeof c1buff, col1_size, &col1_index);
		}
		if (!c2done && (col2_size || c1done == 2)) {
			// column 2 not finished AND: column 2 in column format OR column 1 is finished;
			c2done = line_to_buffer(col2_text, c2buff, sizeof c2buff, col2_usable, &col2_index);
		}

		if (col2_text && col2_size && c2done != 2 && c2buff[0] != '\0') {
			// column 2 exists in column format and is not an empty row;
			i = strlen(c1buff);
			if (i < (sizeof c1buff - 1)) {
				// pad column 1 buffer with spaces
				for (; i < (sizeof c1buff - 1); i++)
					c1buff[i] = ' ';
				c1buff[i] = '\0';
			}
		}

		// output;

		if (c2done == 2) {
			// column 2 is finished;
			if (c1done == 2)
				return;
			fprintf(f, "%s%s\n", indent ? indent : "", c1buff);	// print column 1 output only;
		}
		else if (col2_size) {
			// column 2 is not finished and is in column format;
			if (c2buff[0] == '\0') {
				// column 2 has an empty row;
				if (c1done == 2 || c1buff[0] == '\0')
					fputs("\n", f);
				else
					fprintf(f, "%s%s\n", indent ? indent : "", c1buff);	// print column 1 output only;
			}
			else
				fprintf(f, "%s%s%s%s\n", indent ? indent : "", c1buff, gap, c2buff);	// print columns 1 and 2;
		}
		else if (col2_text) {
			// column 2 is not finished and is not in column format;
			if (c1done == 2)
				fprintf(f, "%s%s%s\n", indent ? indent : "", gap, c2buff);	// print column 2 only;
			else
				fprintf(f, "%s%s\n", indent ? indent : "", c1buff);			// print column 1 only;
		}

		if (first && (col2_size || c1done == 2)) {
			// first line AND: in column format OR column 1 is finished;
			first = 0;
			// replace the first line gap with normal_gap or spaces;
			if (normal_gap)
				strncpy(gap, normal_gap, gapsize + 1);
			else {
				for (i = 0; i < gapsize; i++)
					gap[i] = ' ';
				gap[i] = '\0';
			}
		}
		if (c1done == 1) {
			if (!c2done && col2_size && col2_text) {
				// column 1 is finished and column 2 is not finished and is in column format;
				// - fill column 1 with spaces to maintain column 2 alignment;
				for (i = 0; i < col1_size; i++)
					c1buff[i] = ' ';
				c1buff[i] = '\0';
			}
			c1done = 2;
		}
		if (c2done == 1) {
			// column 2 is finished;
			c2buff[0] = '\0';
			c2done = 2;
		}
	}
}


void
propertynotify(XEvent *e)
{
	Client *c;
	Window trans;
	XPropertyEvent *ev = &e->xproperty;

	#if PATCH_SYSTRAY
	if ((c = wintosystrayicon(ev->window))) {
		if (ev->atom == XA_WM_NORMAL_HINTS) {
			updatesizehints(c);
			updatesystrayicongeom(c, c->w, c->h);
		}
		else
			updatesystrayiconstate(c, ev);
		updatesystray(1);
		resizebarwin(selmon);
	}
	#endif // PATCH_SYSTRAY

	if ((ev->window == root) && (ev->atom == XA_WM_NAME))
		updatestatus();
	else if (ev->state == PropertyDelete)
		return; /* ignore */
	else if ((c = wintoclient(ev->window))
		#if PATCH_CROP_WINDOWS
		|| (c = cropwintoclient(ev->window))
		#endif // PATCH_CROP_WINDOWS
	) {
		#if PATCH_CROP_WINDOWS
		if (c->crop)
			c = c->crop;
		#endif // PATCH_CROP_WINDOWS
		switch(ev->atom) {
			default: break;
			case XA_WM_TRANSIENT_FOR:
				if (!c->isfloating && (XGetTransientForHint(dpy, c->win, &trans)) &&
					(c->isfloating = (wintoclient(trans)) != NULL))
					arrange(c->mon);
				break;
			case XA_WM_NORMAL_HINTS:
				c->hintsvalid = 0;
				break;
			case XA_WM_HINTS:
				updatewmhints(c);
				drawbar(c->mon, 0);
				break;
		}
		if (ev->atom == XA_WM_NAME || ev->atom == netatom[NetWMName]) {
			if (c->ruledefer == 1) {
				char oldtitle[256];
				strncpy(oldtitle, c->name, sizeof oldtitle);
				updatetitle(c, 1);
				if (strcmp(c->name, oldtitle) == 0)
					return;
				if (c == c->mon->sel)
					drawbar(c->mon, 0);
				applyrulesdeferred(c, oldtitle);
			}
			else {
				updatetitle(c, 1);
				if (c == c->mon->sel)
					drawbar(c->mon, 0);
			}
		}
		#if PATCH_WINDOW_ICONS
		else if (ev->atom == netatom[NetWMIcon]) {
			updateicon(c);
			if (c == c->mon->sel)
				drawbar(c->mon, 0);
		}
		#endif // PATCH_WINDOW_ICONS
		else if (ev->atom == netatom[NetWMState]) {
			if (updatewindowstate(c)) {
				if (selmon->sel == c && !ISVISIBLE(c))
					focus(NULL, 0);
			}
		}
		else if (ev->atom == netatom[NetWMWindowType]) {
			updatewindowtype(c);
			if (selmon->sel == c && !ISVISIBLE(c))
				focus(NULL, 0);
		}
	}
}

#if PATCH_ALTTAB
void
quietunmap(Window w)
{
	static XWindowAttributes ra, ca;

	// more or less taken directly from blackbox's hide() function
	XGrabServer(dpy);
	XGetWindowAttributes(dpy, root, &ra);
	XGetWindowAttributes(dpy, w, &ca);
	// prevent UnmapNotify events
	XSelectInput(dpy, root, ra.your_event_mask & ~SubstructureNotifyMask);
	XSelectInput(dpy, w, ca.your_event_mask & ~StructureNotifyMask);
	XUnmapWindow(dpy, w);
	XSelectInput(dpy, root, ra.your_event_mask);
	XSelectInput(dpy, w, ca.your_event_mask);
	XUngrabServer(dpy);
}
#endif // PATCH_ALTTAB

void
quit(const Arg *arg)
{
	running = 0;
}

void
raisewin(Monitor *m, Window w, int above_bar)
{
	Client *c;

	if (!m)
		if (!(m = wintomon(w)))
			return;

	XWindowChanges wc;
	wc.stack_mode = Below;

	#if PATCH_TORCH
	if (torchwin) {
		wc.sibling = torchwin;
		XRaiseWindow(dpy, wc.sibling);
		XConfigureWindow(dpy, w, CWSibling|CWStackMode, &wc);
		return;
	}
	else
	#endif // PATCH_TORCH
	{
		if (w == m->barwin) {
			if ((c = getmontopclient(m))) {
				wc.sibling = c->win;
				wc.stack_mode = Above;
				XConfigureWindow(dpy, w, CWSibling|CWStackMode, &wc);
			}
		}
		else {
			wc.sibling = m->barwin;
			if (above_bar)
				wc.stack_mode = Above;
			XConfigureWindow(dpy, w, CWSibling|CWStackMode, &wc);
		}
	}

}

void
raiseclient(Client *c)
{
	if (c) {
		raisewin(c->mon, c->win, 0
			#if PATCH_FLAG_PANEL
			| c->ispanel
			#endif // PATCH_FLAG_PANEL
			#if PATCH_FLAG_GAME
			| (c->isgame && c->isfullscreen)
			#endif // PATCH_FLAG_GAME
		);

		#if PATCH_FOCUS_BORDER || PATCH_FOCUS_PIXEL
		if (c == selmon->sel && focuswin) {
			if (c->isfullscreen
				#if PATCH_FLAG_FAKEFULLSCREEN
				&& c->fakefullscreen != 1
				#endif // PATCH_FLAG_FAKEFULLSCREEN
				)
				XMoveResizeWindow(dpy, focuswin, 0, -fh - 1, fh, fh);
			else {
				XWindowChanges wc;
				wc.stack_mode = Above;
				wc.sibling = c->win;
				XConfigureWindow(dpy, focuswin, CWSibling|CWStackMode, &wc);
			}
		}
		#endif // PATCH_FOCUS_BORDER || PATCH_FOCUS_PIXEL
	}
}

#if PATCH_IPC
int
read_socket(IPCMessageType *msg_type, uint32_t *msg_size, char **msg)
{
	int ret = -1;

	while (ret != 0) {
		ret = recv_message((uint8_t *)msg_type, msg_size, (uint8_t **)msg);

		if (ret < 0) {
			// Try again (non-fatal error)
			if (ret == -1 && (errno == EINTR || errno == EAGAIN))
				continue;

			logdatetime(stderr);
			fprintf(stderr, "dwm: Error receiving response from socket. The connection might have been lost.\n");
			msg_size = 0;
			*msg = NULL;
			break;
		}
	}

	return 0;
}
#endif // PATCH_IPC

Client *
recttoclient(int x, int y, int w, int h, int onlyfocusable)
{
	Client *c, *r = NULL;
	int a, area = 0;

	for (c = nextstack(selmon->stack, True); c; c = nextstack(c->snext, True))
		if (!MINIMIZED(c) && (a = INTERSECTC(x, y, w, h, c)) && (onlyfocusable ? !c->neverfocus : True))
			return c;

	if (selmon->lt[selmon->sellt]->arrange == monocle
		#if PATCH_LAYOUT_DECK
		|| selmon->lt[selmon->sellt]->arrange == deck
		#endif // PATCH_LAYOUT_DECK
	) {
		for (c = nextstack(selmon->stack, False); c; c = nextstack(c->snext, False))
			if ((a = INTERSECTC(x, y, w, h, c)) && (onlyfocusable ? !c->neverfocus : True))
				return c;
	}
	else {
		for (c = nexttiled(selmon->clients); c; c = nexttiled(c->next))
			if ((a = INTERSECTC(x, y, w, h, c)) > area && (onlyfocusable ? !c->neverfocus : True)) {
				area = a;
				r = c;
			}
	}
	return r;
}

Monitor *
recttomon(int x, int y, int w, int h)
{
	Monitor *m, *r = selmon;
	int a, area = 0;

	for (m = mons; m; m = m->next)
		if ((a = INTERSECT(x, y, w, h, m)) > area) {
			area = a;
			r = m;
		}
	return r;
}

#if PATCH_IPC
int
recv_message(uint8_t *msg_type, uint32_t *reply_size, uint8_t **reply)
{
	uint32_t read_bytes = 0;
	const int32_t to_read = sizeof(dwm_ipc_header_t);
	char header[to_read];
	char *walk = header;

	// Try to read header
	while (read_bytes < to_read) {
		ssize_t n = read(sock_fd, header + read_bytes, to_read - read_bytes);

		if (n == 0) {
			if (read_bytes == 0) {
				fprintf(stderr,
					"dwm: Unexpectedly reached EOF while reading header. "
					"Read %" PRIu32 " bytes, expected %" PRIu32 " total bytes.\n",
					read_bytes, to_read
				);
				return -2;
			} else {
				fprintf(stderr,
					"dwm: Unexpectedly reached EOF while reading header. "
					"Read %" PRIu32 " bytes, expected %" PRIu32 " total bytes.\n",
					read_bytes, to_read
				);
				return -3;
			}
		} else if (n == -1) {
			return -1;
		}

		read_bytes += n;
	}

	// Check if magic string in header matches
	if (memcmp(walk, IPC_MAGIC, IPC_MAGIC_LEN) != 0) {
		fprintf(stderr, "dwm: Invalid magic string. Got '%.*s', expected '%s'\n", IPC_MAGIC_LEN, walk, IPC_MAGIC);
		return -3;
	}

	walk += IPC_MAGIC_LEN;

	// Extract reply size
	memcpy(reply_size, walk, sizeof(uint32_t));
	walk += sizeof(uint32_t);

	// Extract message type
	memcpy(msg_type, walk, sizeof(uint8_t));
	walk += sizeof(uint8_t);

	(*reply) = malloc(*reply_size);

	// Extract payload
	read_bytes = 0;
	while (read_bytes < *reply_size) {
		ssize_t n = read(sock_fd, *reply + read_bytes, *reply_size - read_bytes);

		if (n == 0) {
			fprintf(stderr,
				"Unexpectedly reached EOF while reading payload. "
				"Read %" PRIu32 " bytes, expected %" PRIu32 " bytes.\n",
				read_bytes, *reply_size
			);
			free(*reply);
			return -2;
		} else if (n == -1) {
			if (errno == EINTR || errno == EAGAIN)
				continue;
			free(*reply);
			return -1;
		}

		read_bytes += n;
	}

	return 0;
}
#endif // PATCH_IPC

#if PATCH_MOUSE_POINTER_WARPING
void
refocuspointer(const Arg *arg)
{
	#if PATCH_FOCUS_FOLLOWS_MOUSE
	checkmouseoverclient();
	#endif // PATCH_FOCUS_FOLLOWS_MOUSE
	if (selmon->sel)
		#if PATCH_MOUSE_POINTER_WARPING_SMOOTH
		warptoclient(selmon->sel, 0, 1);
		#else // NO PATCH_MOUSE_POINTER_WARPING_SMOOTH
		warptoclient(selmon->sel, 1);
		#endif // PATCH_MOUSE_POINTER_WARPING_SMOOTH
	#if PATCH_MOUSE_POINTER_HIDING
	hidecursor();
	#endif // PATCH_MOUSE_POINTER_HIDING
}
#endif // PATCH_MOUSE_POINTER_WARPING

void
reload(const Arg *arg)
{
	logdatetime(stderr);
	fputs("dwm: received reload() signal.\n", stderr);
	running = -1;
}

int
reload_rules(void)
{
	rules_json = parsejsonfile(rules_filename, "rules");
	if (rules_json)
		return (parserulesjson(rules_json));
	return 0;
}

#if PATCH_FLAG_TITLE || PATCH_SHOW_MASTER_CLIENT_ON_TAG || PATCH_ALTTAB || PATCH_WINDOW_ICONS_CUSTOM_ICONS || PATCH_FLAG_PARENT
void
compost(void **link, cJSON *parent, cJSON *item, const char *string)
{
	size_t sz;
	cJSON *c = item, *cc = NULL;
	if (rules_compost) {
		sz = cJSON_GetArraySize(rules_compost);
		for (size_t i = 0; i < sz; i++) {
			cc = cJSON_GetArrayItem(rules_compost, i);
			if (c && c == cc)
				return;
			if (string && cJSON_GetStringValue(cc) == string)
				return;
		}
	}
	else
		rules_compost = cJSON_CreateArray();
	if (c) {
		c = cJSON_DetachItemViaPointer(parent, item);
		*link = c;
	}
	else {
		c = cJSON_CreateString(string);
		*link = c->valuestring;
	}
	cJSON_AddItemToArray(rules_compost, c);
}
#endif // PATCH_FLAG_TITLE || PATCH_SHOW_MASTER_CLIENT_ON_TAG || PATCH_ALTTAB || PATCH_WINDOW_ICONS_CUSTOM_ICONS || PATCH_FLAG_PARENT

#if PATCH_FLAG_TITLE || PATCH_SHOW_MASTER_CLIENT_ON_TAG || PATCH_ALTTAB || PATCH_WINDOW_ICONS_CUSTOM_ICONS || PATCH_FLAG_PARENT
void
uncompost(cJSON *item, const char *string)
{
	if (!rules_compost)
		return;
	cJSON *c = item, *cc = NULL;
	size_t sz = cJSON_GetArraySize(rules_compost);
	for (size_t i = 0; i < sz; i++) {
		cc = cJSON_GetArrayItem(rules_compost, i);
		if ((c && c == cc) || (string && cJSON_GetStringValue(cc) == string)) {
			cJSON_DeleteItemFromArray(rules_compost, i);
			--sz;
			break;
		}
	}
	if (!sz) {
		cJSON_Delete(rules_compost);
		rules_compost = NULL;
	}
}
#endif // PATCH_FLAG_TITLE || PATCH_SHOW_MASTER_CLIENT_ON_TAG || PATCH_ALTTAB || PATCH_WINDOW_ICONS_CUSTOM_ICONS || PATCH_FLAG_PARENT

void
reloadrules(const Arg *arg)
{
	int success;
	logdatetime(stderr);
	fputs("dwm: reloading rules from json file...\n", stderr);
	if (rules_json) {
		#if PATCH_FLAG_TITLE || PATCH_SHOW_MASTER_CLIENT_ON_TAG || PATCH_ALTTAB || PATCH_WINDOW_ICONS_CUSTOM_ICONS || PATCH_FLAG_PARENT
		logdatetime(stderr);
		fputs("dwm: composting links to stale JSON data from affected clients.\n", stderr);
		for (Monitor *m = mons; m; m = m->next)
			for (Client *c = m->clients; c; c = c->next) {
				#if PATCH_FLAG_TITLE
				if (c->displayname)
					compost((void **)&c->displayname, NULL, NULL, c->displayname);
				#endif // PATCH_FLAG_TITLE
				#if PATCH_SHOW_MASTER_CLIENT_ON_TAG
				if (c->dispclass)
					compost((void **)&c->dispclass, NULL, NULL, c->dispclass);
				#endif // PATCH_SHOW_MASTER_CLIENT_ON_TAG
				#if PATCH_ALTTAB
				if (c->grpclass)
					compost((void **)&c->grpclass, NULL, NULL, c->grpclass);
				#endif // PATCH_ALTTAB
				#if PATCH_WINDOW_ICONS && PATCH_WINDOW_ICONS_CUSTOM_ICONS
				if (c->icon_file)
					compost((void **)&c->icon_file, NULL, NULL, c->icon_file);
				#endif // PATCH_WINDOW_ICONS && PATCH_WINDOW_ICONS_CUSTOM_ICONS
				#if PATCH_FLAG_PARENT
				if (c->parent_condition_node) {
					if (c->parent_is)
						compost((void **)&c->parent_is, c->parent_condition_node, c->parent_is, NULL);
					if (c->parent_begins)
						compost((void **)&c->parent_begins, c->parent_condition_node, c->parent_begins, NULL);
					if (c->parent_contains)
						compost((void **)&c->parent_contains, c->parent_condition_node, c->parent_contains, NULL);
					if (c->parent_ends)
						compost((void **)&c->parent_ends, c->parent_condition_node, c->parent_ends, NULL);
					c->parent_condition_node = NULL;
				}
				#endif // PATCH_FLAG_PARENT
			}
		if (rules_compost) {
			logdatetime(stderr);
			fprintf(stderr, "dwm: composted rule data items: %u\n", cJSON_GetArraySize(rules_compost));
		}
		#endif // PATCH_FLAG_TITLE || PATCH_SHOW_MASTER_CLIENT_ON_TAG || PATCH_ALTTAB || PATCH_WINDOW_ICONS_CUSTOM_ICONS || PATCH_FLAG_PARENT
		cJSON_Delete(rules_json);
	}
	success = reload_rules();
	logdatetime(stderr);
	if (success)
		fputs("dwm: successfully parsed the rules JSON file.\n", stderr);
	else
		fputs("dwm: errors occurred while loading or parsing the rules JSON file.\n", stderr);
}

void
removelinks(Client *c)
{
	long index = INT_MAX;
	Client *cc, *up = NULL;
	Monitor *m;

	// now reset any remaining child clients parent/ult-parent links;
	for (m = mons; m; m = m->next)
		for (cc = m->clients; cc; cc = cc->next) {
			if (cc->prevsel == c)
				cc->prevsel = NULL;
			if (cc->parent == c)
				cc->parent = c->parent;
			if (cc->ultparent == c && cc->index < index) {
				index = cc->index;
				up = cc;
			}
		}
	// if this client is an ultimate parent, replace references to this client as ultimate parent
	// using this client's earliest child as replacement
	if (up) {
		for (m = mons; m; m = m->next)
			for (cc = m->clients; cc; cc = cc->next)
				if (cc->ultparent == c)
					cc->ultparent = up;
	}

}

#if PATCH_SYSTRAY
void
removesystrayicon(Client *i)
{
	Client **ii;

	if (!showsystray || !i)
		return;
	for (ii = &systray->icons; *ii && *ii != i; ii = &(*ii)->next);
	if (ii)
		*ii = i->next;
	free(i);
}
#endif // PATCH_SYSTRAY

#if PATCH_FOCUS_PIXEL && !PATCH_FOCUS_BORDER
void
repelfocusborder(void)
{
	int rot = 0;
	if (selmon->sel)
		rot = (selmon->sel->h > selmon->sel->w) ? 1 : 0;
	switch (fpcurpos) {
		case FOCUS_PIXEL_NE:
			fpcurpos = rot ? FOCUS_PIXEL_NW : FOCUS_PIXEL_SE;
			break;
		case FOCUS_PIXEL_SE:
			fpcurpos = rot ? FOCUS_PIXEL_SW : FOCUS_PIXEL_NE;
			break;
		case FOCUS_PIXEL_NW:
			fpcurpos = rot ? FOCUS_PIXEL_NE : FOCUS_PIXEL_SW;
			break;
		case FOCUS_PIXEL_SW:
			fpcurpos = rot ? FOCUS_PIXEL_SE : FOCUS_PIXEL_NW;
			break;
		default:
			fpcurpos = fppos;
	}
	drawfocusborder(0);
}
#endif // PATCH_FOCUS_PIXEL && !PATCH_FOCUS_BORDER

void
rescan(const Arg *arg)
{
	long count1 = 0, count2 = 0;
	Monitor *m;
	for (m = mons; m; m = m->next)
		for (Client *c = m->clients; c && ++count1; c = c->next);
	logdatetime(stderr);
	fprintf(stderr, "dwm: rescanning for clients...\n");
	scan();
	for (m = mons; m; m = m->next)
		for (Client *c = m->clients; c && ++count2; c = c->next);
	logdatetime(stderr);
	fprintf(stderr, "dwm: rescanning complete - found %lu more clients.\n", (count2 - count1));
}

void
resize(Client *c, int x, int y, int w, int h, int interact)
{
	if (
		#if PATCH_FLAG_IGNORED
		c->isignored ||
		#endif // PATCH_FLAG_IGNORED
		#if PATCH_FLAG_PANEL
		c->ispanel ||
		#endif // PATCH_FLAG_PANEL
		applysizehints(c, &x, &y, &w, &h, interact)
	) {
		if (!interact && c->isfloating &&
			(
				!c->isfullscreen
				#if PATCH_FLAG_FAKEFULLSCREEN
				|| c->fakefullscreen == 1
				#endif // PATCH_FLAG_FAKEFULLSCREEN
			)
		) {
			if (w + 2*c->bw > c->mon->ww)
				w = c->mon->ww - 2*c->bw;
			if (h + 2*c->bw > c->mon->wh)
				h = c->mon->wh - 2*c->bw;
			if (x < c->mon->wx)
				x = c->mon->wx;
			else if (x + w + 2*c->bw > c->mon->wx + c->mon->ww)
				x = c->mon->wx + c->mon->ww - w - 2*c->bw;
			if (y < c->mon->wy)
				y = c->mon->wy;
			else if (y + h + 2*c->bw > c->mon->wy + c->mon->wh)
				y = c->mon->wy + c->mon->wh - h - 2*c->bw;
		}
		resizeclient(c, x, y, w, h, !interact);
	}
}

void
resizebarwin(Monitor *m) {
//	unsigned int w = m->mw;
//	if (showsystray && m == systraytomon(m) && !systrayonleft)
//		w =- getsystraywidth();
//	XMoveResizeWindow(dpy, m->barwin, m->mx, m->by, w, bh);
}

void
resizeclient(Client *c, int x, int y, int w, int h, int save_old)
{
	XWindowChanges wc;
	#if PATCH_FOCUS_BORDER || PATCH_FOCUS_PIXEL
	XWindowChanges fwc;
	#endif // PATCH_FOCUS_BORDER || PATCH_FOCUS_PIXEL

	if (save_old) {
		c->oldx = c->x;
		c->oldy = c->y;
		c->oldw = c->w;
		c->oldh = c->h;
	}
	c->x = wc.x = x;
	c->y = wc.y = y;
	c->w = wc.width = w;
	c->h = wc.height = h;

	if (c->beingmoved)
		return;

	#if PATCH_CLASS_STACKING
	if (c->stackhead)
		wc.border_width = solitary(c->stackhead) ? 0 : c->bw;
	else
	#endif // PATCH_CLASS_STACKING
	{
		wc.border_width = c->bw;
		if (solitary(c)) {
			c->w = wc.width += c->bw * 2;
			c->h = wc.height += c->bw * 2;
			wc.border_width = 0;
		}

		if (
			#if PATCH_FLAG_PANEL
			c->ispanel ||
			#endif // PATCH_FLAG_PANEL
			#if PATCH_FLAG_IGNORED
			c->isignored ||
			#endif // PATCH_FLAG_IGNORED
			0)
			c->bw = wc.border_width = 0;
	}

	#if PATCH_FLAG_FLOAT_ALIGNMENT || PATCH_FOCUS_BORDER || PATCH_FOCUS_PIXEL
	if (ISVISIBLE(c)) {
		#if PATCH_FLAG_FLOAT_ALIGNMENT
		// nail it to no border & y=0:
		if (alignfloat(c, c->floatalignx, c->floataligny)) {
			wc.x = c->x;
			wc.y = c->y;
		}
		#endif // PATCH_FLAG_FLOAT_ALIGNMENT

		#if PATCH_FOCUS_BORDER || PATCH_FOCUS_PIXEL
		fwc.stack_mode = Above;
		fwc.sibling = c->win;
		if (focuswin && selmon->sel == c) {
			#if PATCH_FOCUS_BORDER
			if (!wc.border_width || (c->isfullscreen
				#if PATCH_FLAG_FAKEFULLSCREEN
				&& c->fakefullscreen != 1
				#endif // PATCH_FLAG_FAKEFULLSCREEN
				)
				#if PATCH_FLAG_PANEL
				|| c->ispanel
				#endif // PATCH_FLAG_PANEL
				)
				XMoveResizeWindow(dpy, focuswin, 0, -fh - 1, fh, fh);
			else {
				unsigned int size = 0;
				switch (fbpos) {
					case FOCUS_BORDER_E:
						if (!c->isfloating || c->x + WIDTH(c) < c->mon->wx + c->mon->ww) {
							size = c->isfloating ? MIN(fh, MAX(c->mon->wx + c->mon->ww - WIDTH(c) - c->x, 1)) : fh;
							XMoveResizeWindow(dpy, focuswin, c->x + WIDTH(c) - (!c->isfloating ? size : 0), c->y, size, HEIGHT(c));
							if (!c->isfloating)
								wc.width -= fh;
						}
						break;
					case FOCUS_BORDER_S:
						if (!c->isfloating || c->y + HEIGHT(c) < c->mon->wy + c->mon->wh) {
							size = c->isfloating ? MIN(fh, MAX(c->mon->wy + c->mon->wh - HEIGHT(c) - c->y, 1)) : fh;
							XMoveResizeWindow(dpy, focuswin, c->x, c->y + HEIGHT(c) - (!c->isfloating ? size : 0), WIDTH(c), size);
							if (!c->isfloating)
								wc.height -= fh;
						}
						break;
					case FOCUS_BORDER_W:
						if (!c->isfloating || c->x > c->mon->wx) {
							size = c->isfloating ? MIN(fh, MAX(c->mon->wx + c->mon->ww - WIDTH(c) - c->x, 1)) : fh;
							XMoveResizeWindow(dpy, focuswin, c->x - (c->isfloating ? size : 0), c->y, size, HEIGHT(c));
							if (!c->isfloating) {
								wc.x += fh; wc.width -= fh;
							}
						}
						break;
					default:
					case FOCUS_BORDER_N:
						if (!c->isfloating || c->y > c->mon->wy) {
							size = c->isfloating ? MIN(fh, MAX(c->mon->wy + c->mon->wh - HEIGHT(c) - c->y, 1)) : fh;
							XMoveResizeWindow(dpy, focuswin, c->x, c->y - (c->isfloating ? size : 0), WIDTH(c), size);
							if (!c->isfloating) {
								wc.y += fh; wc.height -= fh;
							}
						}
				}
				if (size)
					XConfigureWindow(dpy, focuswin, CWSibling|CWStackMode, &fwc);
				else
					XMoveResizeWindow(dpy, focuswin, 0, -fh - 1, fh, fh);
			}
			#elif PATCH_FOCUS_PIXEL
			if ((c->isfullscreen
				#if PATCH_FLAG_FAKEFULLSCREEN
				&& c->fakefullscreen != 1
				#endif // PATCH_FLAG_FAKEFULLSCREEN
				)
				#if PATCH_FLAG_PANEL
				|| c->ispanel
				#endif // PATCH_FLAG_PANEL
				)
				XMoveResizeWindow(dpy, focuswin, 0, -fh - 1, fh, fh);
			else {
				int fhadj = fh;
				if (c->w >= c->h && fhadj >= (c->h / 2))
					fhadj = (c->h / 2) - 1;
				else if (c->h > c->w && fhadj >= (c->w / 2))
					fhadj = (c->w / 2) - 1;
				if (fhadj < 1)
					fhadj = 1;
				if (!fpcurpos)
					fpcurpos = fppos;
				switch (fpcurpos) {
					case FOCUS_PIXEL_SW:
						XMoveResizeWindow(dpy, focuswin,
							c->x + wc.border_width,
							c->y + c->h + wc.border_width - fhadj - 2,
							fhadj, fhadj
						);
						break;
					case FOCUS_PIXEL_NW:
						XMoveResizeWindow(dpy, focuswin,
							c->x + wc.border_width,
							c->y + wc.border_width,
							fhadj, fhadj
						);
						break;
					case FOCUS_PIXEL_NE:
						XMoveResizeWindow(dpy, focuswin,
							c->x + c->w + wc.border_width - fhadj - 2,
							c->y + wc.border_width,
							fhadj, fhadj
						);
						break;
					default:
					case FOCUS_PIXEL_SE:
						XMoveResizeWindow(dpy, focuswin,
							c->x + c->w + wc.border_width - fhadj - 2,
							c->y + c->h + wc.border_width - fhadj - 2,
							fhadj, fhadj
						);
				}
				XConfigureWindow(dpy, focuswin, CWSibling|CWStackMode, &fwc);
			}
			#endif // PATCH_FOCUS_BORDER || PATCH_FOCUS_PIXEL
		}
		#endif // PATCH_FOCUS_BORDER || PATCH_FOCUS_PIXEL
	}
	else
	#else // NO PATCH_FLAG_FLOAT_ALIGNMENT || PATCH_FOCUS_BORDER || PATCH_FOCUS_PIXEL
	if (!ISVISIBLE(c))
	#endif // PATCH_FLAG_FLOAT_ALIGNMENT || PATCH_FOCUS_BORDER || PATCH_FOCUS_PIXEL
		wc.x = c->w * -2;

	XConfigureWindow(dpy, c->win, CWX|CWY|CWWidth|CWHeight|CWBorderWidth, &wc);
	configure(c);

	#if 0 // PATCH_FLAG_FAKEFULLSCREEN
	if (c->fakefullscreen == 1)
		XSync(dpy, True);
	else
	#endif // PATCH_FLAG_FAKEFULLSCREEN
		XSync(dpy, False);

	if (!(nonstop & 1)
		#if PATCH_FLAG_IGNORED
		&& !c->isignored
		#endif // PATCH_FLAG_IGNORED
		#if PATCH_FLAG_PANEL
		&& !c->ispanel
		#endif // PATCH_FLAG_PANEL
		) {
		if (c->isfloating && c->parent && c->parent->mon == c->mon && (!c->isfullscreen
			#if PATCH_FLAG_FAKEFULLSCREEN
			|| c->fakefullscreen == 1
			#endif // PATCH_FLAG_FAKEFULLSCREEN
		)) {
			c->sfxo = (float)(c->x - c->parent->x + c->w / 2) / (c->parent->w / 2);
			c->sfyo = (float)(c->y - c->parent->y + c->h / 2) / (c->parent->h / 2);
		}
		snapchildclients(c, 0);
	}
}

#if PATCH_CLIENT_OPACITY
void
setopacity(Client *c, double opacity)
{
	if (opacityenabled && opacity > 0 && opacity < 1
		#if PATCH_FLAG_PANEL
		&& !c->ispanel
		#endif // PATCH_FLAG_PANEL
		#if PATCH_SHOW_DESKTOP
		&& !c->isdesktop
		#endif // PATCH_SHOW_DESKTOP
	) {
		unsigned long real_opacity[] = { opacity * 0xffffffff };
		XChangeProperty(
			dpy, c->win, netatom[NetWMWindowsOpacity], XA_CARDINAL,
			32, PropModeReplace, (unsigned char *)real_opacity, 1
		);
	} else
		XDeleteProperty(dpy, c->win, netatom[NetWMWindowsOpacity]);
}
#endif // PATCH_CLIENT_OPACITY

int
skipnextkeyevent(int type, unsigned int keycode, unsigned int state, unsigned long serial)
{
	XEvent xev;
	if (!XPending(dpy))
		return 0;
	XPeekEvent(dpy, &xev);
	if (xev.type != type || xev.xkey.keycode != keycode || CLEANMASK(xev.xkey.state) != state || xev.xkey.serial != serial)
		return 0;
	XNextEvent(dpy, &xev);
	return 1;
}

void
snapchildclients(Client *p, int quiet)
{

	Client *c;
	Monitor *m;

	if (p->isfullscreen
		#if PATCH_FLAG_FAKEFULLSCREEN
		&& p->fakefullscreen != 1
		#endif // PATCH_FLAG_FAKEFULLSCREEN
		) return;

	/* snap child windows */
	m = p->mon;
	//for (m = mons; m; m = m->next)
	{
		for (c = m->clients; c; c = c->next)
		{
			if (c->isfloating && (!c->isfullscreen
					#if PATCH_FLAG_FAKEFULLSCREEN
					|| c->fakefullscreen == 1
					#endif // PATCH_FLAG_FAKEFULLSCREEN
				)
				#if PATCH_FLAG_IGNORED
				&& !c->isignored
				#endif // PATCH_FLAG_IGNORED
				#if PATCH_FLAG_PANEL
				&& !c->ispanel
				#endif // PATCH_FLAG_PANEL
				&& c->parent == p
				&& c != p
				&& c->mon == p->mon
			) {
				c->x = MAX(
						MIN(
							(((c->sfxo == 0 ? 1 : c->sfxo) * c->parent->w / 2) + c->parent->x - c->w / 2),
							(c->mon->wx + c->mon->ww - c->w)
						),
						c->mon->wx
					);
				c->y = MAX(
						MIN(
							(((c->sfyo == 0 ? 1 : c->sfyo) * c->parent->h / 2) + c->parent->y - c->h / 2),
							(c->mon->wy + c->mon->wh - c->h)
						),
						c->mon->wy
					);
				if (!quiet)
					XMoveWindow(dpy, c->win, c->x, c->y);

				snapchildclients(c, quiet);
			}
		}
	}
}

#if PATCH_FLAG_FOLLOW_PARENT
int
tagsatellites(Client *p) {

	int changes = 0;

	/* child windows to match parent tags */
	for (Monitor *m = mons; m; m = m->next)
		for (Client *c = m->clients; c; c = c->next)
		{
			if (c->parent == p
				&& c != p
				&& c->followparent && !c->toplevel
				#if PATCH_FLAG_IGNORED
				&& !c->isignored
				#endif // PATCH_FLAG_IGNORED
				#if PATCH_FLAG_PANEL
				&& !c->ispanel
				#endif // PATCH_FLAG_PANEL
			) {
				changes += tagsatellites(c);
				if (c->tags != p->tags) {
					c->tags = p->tags;
					#if PATCH_PERSISTENT_METADATA
					setclienttagprop(c);
					#endif // PATCH_PERSISTENT_METADATA
					changes++;
				}
			}
		}

	return changes;
}
#endif // PATCH_FLAG_FOLLOW_PARENT

int
tagtoindex(unsigned int tag)
{
	for (int i = 0; i < LENGTH(tags); i++)
		if (tag & (1 << i))
			return i + 1;
	return 0;
}

#if PATCH_TERMINAL_SWALLOWING
Client *
termforwin(const Client *w)
{
	Client *c;
	Monitor *m;

	if (!w->pid || w->isterminal)
		return NULL;

	for (m = mons; m; m = m->next) {
		for (c = m->clients; c; c = c->next) {
			if (c->isterminal && !c->swallowing && c->pid && isdescprocess(c->pid, w->pid))
				return c;
		}
	}

	return NULL;
}
#endif // PATCH_TERMINAL_SWALLOWING

#if PATCH_SHOW_MASTER_CLIENT_ON_TAG && ((PATCH_WINDOW_ICONS && PATCH_WINDOW_ICONS_ON_TAGS) || PATCH_CUSTOM_TAG_ICONS)
void
textwithicon(char *text, Picture icon, unsigned int icw, unsigned int ich,
	char *placeholder_text, int x, int y, unsigned int w, unsigned int h,
	int offsetx,
	#if PATCH_CLIENT_INDICATORS
	int offsety,
	#endif // PATCH_CLIENT_INDICATORS
	int invert)
{
	int i, il = 0, ir = 0, len = strlen(text);
	unsigned int posx = 0, plw = 0;
	char buffer[len + 1 + strlen(placeholder_text)];

	strncpy(buffer, text, sizeof buffer - 1);

	for (i = 0; i < len - 1; i++)
		if (text[i] == '%' && text[i + 1] == 's') {
			il = i - 1;
			ir = i + 2;
			buffer[i] = '\0';
			break;
		}

	if (!il && !ir)
		drw_text(
			drw, x, y, w, h, offsetx, 0,
			#if PATCH_CLIENT_INDICATORS
			offsety,
			#endif // PATCH_CLIENT_INDICATORS
			1, text, invert
		);
	else {
		drw_text(
			drw, x, y, w, h, offsetx, 0,
			#if PATCH_CLIENT_INDICATORS
			offsety,
			#endif // PATCH_CLIENT_INDICATORS
			1, buffer, invert
		);
		posx = offsetx + TEXTW(buffer) - lrpad;
		w -= posx;
		posx += x;
		if (icon) {
			drw_pic(
				drw, posx, y + (h - ich) / 2
				#if PATCH_CLIENT_INDICATORS
				+ offsety
				#endif // PATCH_CLIENT_INDICATORS
				, icw, ich, icon
			);
			posx += icw;
			w -= icw;
		}
		else {
			drw_text(
				drw, posx, y, w, h, 0, 0,
				#if PATCH_CLIENT_INDICATORS
				offsety,
				#endif // PATCH_CLIENT_INDICATORS
				1, placeholder_text, invert
			);
			plw = drw_fontset_getwidth(drw, placeholder_text);
			posx += plw;
			w -= plw;
		}
		drw_text(
			drw, posx, y, w, h, 0, 0,
			#if PATCH_CLIENT_INDICATORS
			offsety,
			#endif // PATCH_CLIENT_INDICATORS
			1, buffer + ir, invert
		);

	}
}
#endif // PATCH_SHOW_MASTER_CLIENT_ON_TAG && ((PATCH_WINDOW_ICONS && PATCH_WINDOW_ICONS_ON_TAGS) || PATCH_CUSTOM_TAG_ICONS)

#if PATCH_FLAG_ALWAYSONTOP
void
togglealwaysontop(const Arg *arg)
{
	#if PATCH_FOCUS_FOLLOWS_MOUSE
	checkmouseoverclient();
	#endif // PATCH_FOCUS_FOLLOWS_MOUSE
	if (!selmon->sel)
		return;
	#if PATCH_SHOW_DESKTOP
	if (selmon->sel->isdesktop)
		return;
	#endif // PATCH_SHOW_DESKTOP
	selmon->sel->alwaysontop = !selmon->sel->alwaysontop;
	publishwindowstate(selmon->sel);
	restack(selmon);
}
#endif // PATCH_FLAG_ALWAYSONTOP

#if PATCH_ALT_TAGS
void
togglealttags(const Arg *arg)
{
	#if PATCH_FOCUS_FOLLOWS_MOUSE
	checkmouseovermonitor(selmon);
	#endif // PATCH_FOCUS_FOLLOWS_MOUSE
	selmon->alttags = !selmon->alttags;
	drawbar(selmon, 0);
	#if PATCH_MOUSE_POINTER_HIDING
	if (selmon->alttags)
		showcursor();
	#endif // PATCH_MOUSE_POINTER_HIDING
}
#endif // PATCH_ALT_TAGS

#if PATCH_CONSTRAIN_MOUSE
void
toggleconstrain(const Arg *arg)
{
	if (!xfixes_support)
		return;
	#if PATCH_FOCUS_FOLLOWS_MOUSE
	checkmouseovermonitor(selmon);
	#endif // PATCH_FOCUS_FOLLOWS_MOUSE
	if (barrierLeft) {
		if (constrained)
			destroybarriermon();
	}
	else
		createbarriermon(selmon);
}
#endif // PATCH_CONSTRAIN_MOUSE

#if PATCH_FLAG_GAME
void
toggleisgame(const Arg *arg)
{
	#if PATCH_FOCUS_FOLLOWS_MOUSE
	checkmouseoverclient();
	#endif // PATCH_FOCUS_FOLLOWS_MOUSE
	if (!selmon->sel)
		return;

	#if PATCH_SHOW_DESKTOP
	if (selmon->sel->isdesktop || selmon->sel->ondesktop)
		return;
	#endif // PATCH_SHOW_DESKTOP

	selmon->sel->isgame ^= 1;

	restack(selmon);
}
#endif // PATCH_FLAG_GAME

#if PATCH_TORCH
void
toggletorch(const Arg *arg)
{
	#if PATCH_FLAG_PANEL
	Monitor *m;
	#endif // PATCH_FLAG_PANEL
	if (torchwin) {

		XUnmapWindow(dpy, torchwin);
		XDestroyWindow(dpy, torchwin);
		torchwin = 0;

		for (m = mons; m; m = m->next)
			showhidebar(m);
		drawbars();

		XUngrabKeyboard(dpy, CurrentTime);
		XUngrabPointer(dpy, CurrentTime);

		XSync(dpy, False);
	}
	else if (running == 1) {

		if (!grabinputs(1, 1, cursor[CurInvisible]->cursor)) {
			#if PATCH_LOG_DIAGNOSTICS
			logdatetime(stderr);
			fprintf(stderr, "dwm: warning: toggletorch() unable to grab inputs, aborting.\n");
			#endif
			return;
		}

		XWindowAttributes wa;
		XGetWindowAttributes(dpy, root, &wa);

		XSetWindowAttributes swa;
		#if PATCH_ALPHA_CHANNEL
		if (useargb) {
			swa.override_redirect = True;
			swa.background_pixel = 0;
			swa.border_pixel = 0;
			swa.colormap = cmap;
			swa.event_mask = ExposureMask;
			torchwin = XCreateWindow(dpy, root, 0, 0, wa.width, wa.height, 0, depth,
									CopyFromParent, visual,
									CWOverrideRedirect|CWBackPixel|CWBorderPixel|CWColormap|CWEventMask, &swa);
		}
		else
		#endif // PATCH_ALPHA_CHANNEL
		{
			swa.override_redirect = True;
			swa.background_pixmap = ParentRelative;
			swa.event_mask = ExposureMask;
			torchwin = XCreateWindow(dpy, root, 0, 0, wa.width, wa.height, 0, DefaultDepth(dpy, screen),
									CopyFromParent, DefaultVisual(dpy, screen),
									CWOverrideRedirect|CWBackPixmap|CWEventMask, &swa);
		}
		XClassHint ch = {"dwm", "dwm-torch"};
		XSetClassHint(dpy, torchwin, &ch);
		XDefineCursor(dpy, torchwin, cursor[CurNormal]->cursor);
		XMapRaised(dpy, torchwin);

		drw_setscheme(drw, scheme[SchemeTorch]);
		drw_rect(drw, 0, 0, wa.width, wa.height, 1, arg->ui);
		drw_map(drw, torchwin, 0, 0, wa.width, wa.height);

		for (m = mons; m; m = m->next)
			showhidebar(m);

		XSync(dpy, True);

	}
}
#endif // PATCH_TORCH

#if PATCH_SYSTRAY
void
resizerequest(XEvent *e)
{
	logdatetime(stderr);
	fprintf(stderr, "debug: resizerequest()\n");

	XResizeRequestEvent *ev = &e->xresizerequest;
	Client *i;
	if ((i = wintosystrayicon(ev->window))) {
		updatesystrayicongeom(i, ev->width, ev->height);
		resizebarwin(selmon);
		updatesystray(1);
	}
	else if ((i = wintoclient(ev->window)) && i->isfloating) {

		logdatetime(stderr);
		fprintf(stderr, "debug: resizerequest (send_event=%s) from \"%s\"\n", i->name, ev->send_event ? "True" : "False");

	}
}
#endif // PATCH_SYSTRAY

void
resizemouse(const Arg *arg)
{
	#if PATCH_FOCUS_FOLLOWS_MOUSE
	checkmouseoverclient();
	#endif // PATCH_FOCUS_FOLLOWS_MOUSE
	int opx, opy, ocx, ocy, och, ocw, nx, ny, nw, nh;
	Client *c;
	Monitor *m;
	XEvent ev;
	int horizcorner, vertcorner;
	unsigned int dui;
	Window dummy;
	Time lasttime = 0;

	if (!(c = selmon->sel))
		return;
	#if PATCH_SHOW_DESKTOP
	if (c->isdesktop)
		return;
	#endif // PATCH_SHOW_DESKTOP
	if (c->isfullscreen
		#if PATCH_FLAG_FAKEFULLSCREEN
		&& c->fakefullscreen != 1
		#endif // PATCH_FLAG_FAKEFULLSCREEN
		) /* no support resizing fullscreen windows by mouse */
		return;
	#if PATCH_CROP_WINDOWS
	if (arg->i == 1) {
		if (!c->isfloating)
			return;
		cropwindow(c);
	}
	#endif // PATCH_CROP_WINDOWS
	restack(selmon);
	ocx = c->x;
	ocy = c->y
		#if PATCH_FOCUS_BORDER
		+ fh
		#endif // PATCH_FOCUS_BORDER
	;
	och = c->h;
	ocw = c->w;
	if (!XQueryPointer(dpy, c->win, &dummy, &dummy, &opx, &opy, &nx, &ny, &dui))
		return;
	horizcorner = nx < c->w / 2;
	vertcorner  = ny < c->h / 2;
	if (XGrabPointer(dpy, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync,
		None, cursor[horizcorner | (vertcorner << 1)]->cursor, CurrentTime) != GrabSuccess)
		return;
	#if PATCH_FOCUS_BORDER || PATCH_FOCUS_PIXEL
	if (focuswin)
		XUnmapWindow(dpy, focuswin);
	#endif // PATCH_FOCUS_BORDER || PATCH_FOCUS_PIXEL
	do {
		XMaskEvent(dpy, MOUSEMASK|ExposureMask|SubstructureRedirectMask, &ev);
		switch(ev.type) {
		case ConfigureRequest:
		case Expose:
		case MapRequest:
			handler[ev.type](&ev);
			break;
		case MotionNotify:
			if ((ev.xmotion.time - lasttime) <= (1000 / 60))
				continue;
			lasttime = ev.xmotion.time;

			nx = horizcorner ? (ocx + ev.xmotion.x - opx) : c->x;
			ny = vertcorner ? (ocy + ev.xmotion.y - opy) : c->y;
			nw = MAX(horizcorner ? (ocx + ocw - nx) : (ocw + (ev.xmotion.x - opx)), 1);
			nh = MAX(vertcorner ? (ocy + och - ny) : (och + (ev.xmotion.y - opy)), 1);

			#if PATCH_CROP_WINDOWS
			if (arg->i == 1 && c->crop) {
				if (nw > c->crop->w)
					nw = c->crop->w;
				if (nh > c->crop->h)
					nh = c->crop->h;
				resize(c, nx, ny, nw, nh, 1);
				cropresize(c);
				break;
			}
			#endif // PATCH_CROP_WINDOWS
			if (c->mon->wx + nw >= selmon->wx && c->mon->wx + nw <= selmon->wx + selmon->ww
			&& c->mon->wy + nh >= selmon->wy && c->mon->wy + nh <= selmon->wy + selmon->wh)
			{
				if (!c->isfloating && selmon->lt[selmon->sellt]->arrange
				&& (abs(nw - c->w) > snap || abs(nh - c->h) > snap))
					togglefloatingex(c);
			}
			if (!selmon->lt[selmon->sellt]->arrange || c->isfloating)
				resize(c, nx, ny, nw, nh, 1);
			break;
		}
	} while (ev.type != ButtonRelease);

	XUngrabPointer(dpy, CurrentTime);
	while (XCheckMaskEvent(dpy, EnterWindowMask, &ev));
	if ((m = recttomon(c->x, c->y, c->w, c->h)) != selmon) {
		sendmon(c, m, c, 0);
		c->monindex = c->mon->num;
		selmon = c->mon;
		focus(NULL, 0);
	}
	#if PATCH_FOCUS_BORDER
	else
		focus(c, 1);
	#endif // PATCH_FOCUS_BORDER

	if (1
		#if PATCH_FLAG_IGNORED
		&& !c->isignored
		#endif // PATCH_FLAG_IGNORED
		#if PATCH_FLAG_PANEL
		&& !c->ispanel
		#endif // PATCH_FLAG_PANEL
		) {
		if (c->isfloating && c->parent && c->parent->mon == c->mon && (!c->isfullscreen
			#if PATCH_FLAG_FAKEFULLSCREEN
			|| c->fakefullscreen == 1
			#endif // PATCH_FLAG_FAKEFULLSCREEN
		)) {
			c->sfxo = (float)(c->x - c->parent->x + c->w / 2) / (c->parent->w / 2);
			c->sfyo = (float)(c->y - c->parent->y + c->h / 2) / (c->parent->h / 2);
		}
		snapchildclients(c, 0);
	}
	#if PATCH_FOCUS_BORDER || PATCH_FOCUS_PIXEL
	if (focuswin)
		XMapWindow(dpy, focuswin);
	#endif // PATCH_FOCUS_BORDER || PATCH_FOCUS_PIXEL

}

#if PATCH_DRAG_FACTS
void
resizeorfacts(const Arg *arg)
{
	#if PATCH_FOCUS_FOLLOWS_MOUSE
	checkmouseoverclient();
	#endif // PATCH_FOCUS_FOLLOWS_MOUSE
	Monitor *m = selmon;

	if (!m->sel)
		return;

	if (!m->lt[m->sellt]->arrange || m->sel->isfloating)
		resizemouse(arg);
	else
		dragfact(arg);
}
#endif // PATCH_DRAG_FACTS

void
showhidebar(Monitor *m)
{
	Client *c;

	int visible = updatebarpos(m);

	#if PATCH_SYSTRAY
	if (showsystray && systray && m == systraytomon(m)) {
		if (visible) {
			raisewin(m, systray->win, True);
		}
		else {
			XLowerWindow(dpy, systray->win);
		}
	}
	#endif // PATCH_SYSTRAY

	#if PATCH_FLAG_PANEL
	// show/hide panel windows with m->barvisible value;
	for (c = m->clients; c; c = c->next)
		if (c->ispanel && ISVISIBLE(c)
			#if PATCH_FLAG_HIDDEN
			&& !c->ishidden
			#endif // PATCH_FLAG_HIDDEN
			#if PATCH_FLAG_IGNORED
			&& !c->isignored
			#endif // PATCH_FLAG_IGNORED
		) {
			if (visible
				#if PATCH_TORCH
				&& !torchwin
				#endif // PATCH_TORCH
			) {
				unminimize(c);

				XWindowChanges wc;
				wc.sibling = m->barwin;
				wc.stack_mode = Above;
				XConfigureWindow(dpy, c->win, CWSibling|CWStackMode, &wc);
			}
			else
				minimize(c);
		}
	#endif // PATCH_FLAG_PANEL

	#if PATCH_TORCH
	if (torchwin) XRaiseWindow(dpy, torchwin);
	#endif // PATCH_TORCH
}


void
restack(Monitor *m)
{
	if (running != 1)
		return;

	Client *c;
	Client *raised;
	XEvent ev;
	XWindowChanges wc;
	Window w = m->barwin;

	if (m->sel && (!validate_pid(m->sel) || m->sel->dormant))
		m->sel = NULL;

	raised = m->sel && ((m == selmon && (
		focusedontoptiled
		|| m->sel->isfloating
		#if PATCH_FLAG_ALWAYSONTOP
		|| m->sel->alwaysontop
		#endif // PATCH_FLAG_ALWAYSONTOP
		) && (1
			#if PATCH_FLAG_PANEL
			&& !m->sel->ispanel
			#endif // PATCH_FLAG_PANEL
			#if PATCH_SHOW_DESKTOP
			&& !m->sel->isdesktop
			#endif // PATCH_SHOW_DESKTOP
		)) || (
			m->sel->isfullscreen
			#if PATCH_FLAG_FAKEFULLSCREEN
			&& m->sel->fakefullscreen != 1
			#endif // PATCH_FLAG_FAKEFULLSCREEN
		)
		#if PATCH_FLAG_GAME
		|| (m->sel->isgame && m->sel->isfullscreen)
		#endif // PATCH_FLAG_GAME
	) ? m->sel : NULL;

	#if PATCH_ALTTAB
	#if PATCH_ALTTAB_HIGHLIGHT
	if (tabHighlight && altTabMon && altTabMon->isAlt && !(altTabMon->isAlt & ALTTAB_MOUSE)) {

		#if PATCH_CLASS_STACKING
		if (altTabMon->highlight && altTabMon->highlight->stackhead
			#if PATCH_SHOW_DESKTOP
			&& !altTabMon->highlight->isdesktop
			#endif // PATCH_SHOW_DESKTOP
		)
			raised = altTabMon->highlight;
		else
		#endif // PATCH_CLASS_STACKING
		if (altTabMon->highlight && altTabMon->highlight->mon == m
			#if PATCH_SHOW_DESKTOP
			&& !altTabMon->highlight->isdesktop
			#endif // PATCH_SHOW_DESKTOP
		)
			raised = altTabMon->highlight;
		else
			raised = NULL;
	}
	#endif // PATCH_ALTTAB_HIGHLIGHT
	#endif // PATCH_ALTTAB

	wc.stack_mode = Below;
	wc.sibling = m->barwin;

	Client *s;
	for (s = m->clients; s && s->snext; s = s->snext);
	if (s) {

		#if PATCH_FLAG_PAUSE_ON_INVISIBLE
		for (c = m->stack; c; c = c->snext) {
			if (c->pauseinvisible && c->pid) {
				if (ISVISIBLE(c)
					#if PATCH_FLAG_HIDDEN
					&& !c->ishidden
					#endif // PATCH_FLAG_HIDDEN
					&& (
					(c->isfullscreen
						#if PATCH_FLAG_FAKEFULLSCREEN
						&& c->fakefullscreen != 1
						#endif // PATCH_FLAG_FAKEFULLSCREEN
					) || m->sel == c || (
						m->lt[m->sellt]->arrange != monocle &&
						!(m->sel && m->sel->isfullscreen
							#if PATCH_FLAG_FAKEFULLSCREEN
							&& m->sel->fakefullscreen != 1
							#endif // PATCH_FLAG_FAKEFULLSCREEN
						)
					)
				)) {
					if (c->pauseinvisible == -1) {
						kill (c->pid, SIGCONT);
						c->pauseinvisible = 1;
						#if PATCH_PAUSE_PROCESS
						c->paused = 0;
						#endif // PATCH_PAUSE_PROCESS
						DEBUG("client continued: \"%s\".\n", c->name);
					}
				}
				else if (c->pauseinvisible == 1
						#if PATCH_HANDLE_SIGNALS
						&& !closing
						#endif // PATCH_HANDLE_SIGNALS
				) {
					kill (c->pid, SIGSTOP);
					c->pauseinvisible = -1;
					#if PATCH_PAUSE_PROCESS
					c->paused = 1;
					#endif // PATCH_PAUSE_PROCESS
					DEBUG("client stopped: \"%s\".\n", c->name);
				}
			}
		}
		#endif // PATCH_FLAG_PAUSE_ON_INVISIBLE

		// top layer are fullscreen clients;
		for (c = m->stack; c; c = c->snext)
			if (ISVISIBLE(c) && c->isfullscreen
				#if PATCH_FLAG_FAKEFULLSCREEN
				&& c->fakefullscreen != 1
				#endif // PATCH_FLAG_FAKEFULLSCREEN
				#if PATCH_FLAG_IGNORED
				&& !c->isignored
				#endif // PATCH_FLAG_IGNORED
				#if PATCH_FLAG_HIDDEN
				&& !c->ishidden
				#endif // PATCH_FLAG_HIDDEN
				#if PATCH_FLAG_PANEL
				&& !c->ispanel
				#endif // PATCH_FLAG_PANEL
				#if PATCH_SHOW_DESKTOP
				&& !c->isdesktop
				#endif // PATCH_SHOW_DESKTOP
			) {
				XConfigureWindow(dpy, c->win, CWSibling|CWStackMode, &wc);
				wc.sibling = c->win;
			}

		#if PATCH_FLAG_ALWAYSONTOP
		// marker under which to put floating alwaysontop windows;
		if (raised && raised->isfloating && raised->alwaysontop)
			w = wc.sibling;

		// next layer up are floating alwaysontop;
		for (c = m->stack; c; c = c->snext)
			if (c->isfloating && ISVISIBLE(c)
				&& ((
					m->lt[m->sellt]->arrange != monocle
					#if PATCH_LAYOUT_DECK
					&& m->lt[m->sellt]->arrange != deck
					#endif // PATCH_LAYOUT_DECK
				) || !c->parent || m->sel == c->parent)
				&& c->alwaysontop
				&& (!c->isfullscreen
					#if PATCH_FLAG_FAKEFULLSCREEN
					|| c->fakefullscreen == 1
					#endif // PATCH_FLAG_FAKEFULLSCREEN
				)
				#if PATCH_FLAG_PANEL
				&& !c->ispanel
				#endif // PATCH_FLAG_PANEL
				#if PATCH_FLAG_IGNORED
				&& !c->isignored
				#endif // PATCH_FLAG_IGNORED
				#if PATCH_FLAG_HIDDEN
				&& !c->ishidden
				#endif // PATCH_FLAG_HIDDEN
				#if PATCH_SHOW_DESKTOP
				&& !c->isdesktop
				#endif // PATCH_SHOW_DESKTOP
			) {
				XConfigureWindow(dpy, c->win, CWSibling|CWStackMode, &wc);
				wc.sibling = c->win;
			}

		#if PATCH_FLAG_IGNORED
		// next layer up are ignored alwaysontop;
		for (c = m->stack; c; c = c->snext)
			if (c->isignored && c->isfloating && c->alwaysontop
				#if PATCH_FLAG_HIDDEN
				&& !c->ishidden
				#endif // PATCH_FLAG_HIDDEN
				#if PATCH_SHOW_DESKTOP
				&& !c->isdesktop
				#endif // PATCH_SHOW_DESKTOP
			) {
				XConfigureWindow(dpy, c->win, CWSibling|CWStackMode, &wc);
				wc.sibling = c->win;
			}
		#endif // PATCH_FLAG_IGNORED
		#endif // PATCH_FLAG_ALWAYSONTOP

		// marker under which to put tiled ontop windows;
		if (raised && !raised->isfloating)
			w = wc.sibling;

		// next layer up are floating not alwaysontop;
		for (c = m->stack; c; c = c->snext)
			if (c->isfloating && ISVISIBLE(c)
				&& ((
					m->lt[m->sellt]->arrange != monocle
					#if PATCH_LAYOUT_DECK
					&& m->lt[m->sellt]->arrange != deck
					#endif // PATCH_LAYOUT_DECK
				) || !c->parent || m->sel == c->parent)
				&& (!c->isfullscreen
					#if PATCH_FLAG_FAKEFULLSCREEN
					|| c->fakefullscreen == 1
					#endif // PATCH_FLAG_FAKEFULLSCREEN
				)
				#if PATCH_FLAG_ALWAYSONTOP
				&& !c->alwaysontop
				#endif // PATCH_FLAG_ALWAYSONTOP
				#if PATCH_FLAG_PANEL
				&& !c->ispanel
				#endif // PATCH_FLAG_PANEL
				#if PATCH_FLAG_IGNORED
				&& !c->isignored
				#endif // PATCH_FLAG_IGNORED
				#if PATCH_FLAG_HIDDEN
				&& !c->ishidden
				#endif // PATCH_FLAG_HIDDEN
				#if PATCH_SHOW_DESKTOP
				&& !c->isdesktop
				#endif // PATCH_SHOW_DESKTOP
			) {
				XConfigureWindow(dpy, c->win, CWSibling|CWStackMode, &wc);
				wc.sibling = c->win;
			}

		#if PATCH_FLAG_IGNORED
		// almost bottom of the pile clients are ignored & not alwaysontop;
		for (c = m->stack; c; c = c->snext)
			if (c->isignored && c->isfloating
				#if PATCH_FLAG_ALWAYSONTOP
				&& !c->alwaysontop
				#endif // PATCH_FLAG_ALWAYSONTOP
				#if PATCH_FLAG_HIDDEN
				&& !c->ishidden
				#endif // PATCH_FLAG_HIDDEN
				#if PATCH_SHOW_DESKTOP
				&& !c->isdesktop
				#endif // PATCH_SHOW_DESKTOP
			) {
				XConfigureWindow(dpy, c->win, CWSibling|CWStackMode, &wc);
				wc.sibling = c->win;
			}
		#endif // PATCH_FLAG_IGNORED

		// bottom of the pile are the tiled clients;
		//if (m->lt[m->sellt]->arrange) {
			for (c = m->stack; c; c = c->snext)
				if (!c->isfloating && ISVISIBLE(c)
					&& (!c->isfullscreen
						#if PATCH_FLAG_FAKEFULLSCREEN
						|| c->fakefullscreen == 1
						#endif // PATCH_FLAG_FAKEFULLSCREEN
					)
					#if PATCH_FLAG_PANEL
					&& !c->ispanel
					#endif // PATCH_FLAG_PANEL
					#if PATCH_FLAG_IGNORED
					&& !c->isignored
					#endif // PATCH_FLAG_IGNORED
					#if PATCH_FLAG_HIDDEN
					&& !c->ishidden
					#endif // PATCH_FLAG_HIDDEN
					#if PATCH_SHOW_DESKTOP
					&& !c->isdesktop
					#endif // PATCH_SHOW_DESKTOP
				) {
					XConfigureWindow(dpy, c->win, CWSibling|CWStackMode, &wc);
					wc.sibling = c->win;
				}
		//}

		// put any floating clients above their parents;
		// for monocle layout mode;
		if (m->lt[m->sellt]->arrange == monocle
			#if PATCH_LAYOUT_DECK
			|| m->lt[m->sellt]->arrange == deck
			#endif // PATCH_LAYOUT_DECK
		) {
			wc.stack_mode = Above;
			for (c = m->stack; c; c = c->snext)
				if (c->isfloating && ISVISIBLE(c) && c->parent
					&& (!c->isfullscreen
						#if PATCH_FLAG_FAKEFULLSCREEN
						|| c->fakefullscreen == 1
						#endif // PATCH_FLAG_FAKEFULLSCREEN
					)
					#if PATCH_FLAG_PANEL
					&& !c->ispanel
					#endif // PATCH_FLAG_PANEL
					#if PATCH_FLAG_IGNORED
					&& !c->isignored
					#endif // PATCH_FLAG_IGNORED
					#if PATCH_FLAG_HIDDEN
					&& !c->ishidden
					#endif // PATCH_FLAG_HIDDEN
				) {
					wc.sibling = c->parent->win;
					XConfigureWindow(dpy, c->win, CWSibling|CWStackMode, &wc);
				}
			wc.stack_mode = Below;
		}

	}

	drawbar(m, 0);

	// raise the selected/highighted client if applicable;
	if (raised) {
		if (!raised->isfloating)
			raisewin(raised->mon, raised->win, False);
		/*
		if (raised->isfloating
			#if PATCH_FLAG_ALWAYSONTOP
			&& raised->alwaysontop
			#endif // PATCH_FLAG_ALWAYSONTOP
			)
			raisewin(raised->mon, raised->win, False);
		else
		*/
		{
			// keep floating children close to their parents;
			if (raised->isfloating && (
				m->lt[m->sellt]->arrange == monocle
				#if PATCH_LAYOUT_DECK
				|| m->lt[m->sellt]->arrange == deck
				#endif // PATCH_LAYOUT_DECK
			)) {
				wc.sibling = w;
				XConfigureWindow(dpy, raised->win, CWSibling|CWStackMode, &wc);
				wc.sibling = raised->win;
				c = raised;
				while ((c = c->parent)) {
					if (c->mon == raised->mon && ISVISIBLE(c)) {
						XConfigureWindow(dpy, c->win, CWSibling|CWStackMode, &wc);
						wc.sibling = c->win;
					}
				}
			}
		}
		#if PATCH_SCAN_OVERRIDE_REDIRECTS
		wc.sibling = raised->win;
		#endif // PATCH_SCAN_OVERRIDE_REDIRECTS
	}
	#if PATCH_SCAN_OVERRIDE_REDIRECTS
	else
		wc.sibling = m->barwin;
	#endif // PATCH_SCAN_OVERRIDE_REDIRECTS

	#if PATCH_SCAN_OVERRIDE_REDIRECTS
	if (orlist) {
		// override_redirects to be right at the very top;
		for (c = orlist; c; c = c->next) {
			wc.stack_mode = Above;
			XConfigureWindow(dpy, c->win, CWSibling|CWStackMode, &wc);
			wc.sibling = c->win;
		}
	}
	#endif // PATCH_SCAN_OVERRIDE_REDIRECTS

	#if PATCH_FOCUS_BORDER || PATCH_FOCUS_PIXEL
	if (selmon == m && m->sel && focuswin) {
		if (m->sel->isfullscreen
			#if PATCH_FLAG_FAKEFULLSCREEN
			&& m->sel->fakefullscreen != 1
			#endif // PATCH_FLAG_FAKEFULLSCREEN
		)
			XLowerWindow(dpy, focuswin);
		else {
			wc.stack_mode = Above;
			wc.sibling = m->sel->win;
			XConfigureWindow(dpy, focuswin, CWSibling|CWStackMode, &wc);
		}
	}
	#endif // PATCH_FOCUS_BORDER || PATCH_FOCUS_PIXEL

	XSync(dpy, False);
	while (XCheckMaskEvent(dpy, EnterWindowMask, &ev));

	#if PATCH_TORCH
	if (torchwin) XRaiseWindow(dpy, torchwin);
	#endif // PATCH_TORCH
}

void
run(void)
{
	#if PATCH_IPC
	int event_count = 0;
	const int MAX_EVENTS = 10;
	struct epoll_event events[MAX_EVENTS];

	XSync(dpy, False);

	/* main event loop */
	while (running == 1) {
		event_count = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);

		for (int i = 0; i < event_count; i++) {
			if (running != 1)
				break;
			int event_fd = events[i].data.fd;
			//DEBUG("Got event from fd %d\n", event_fd);

			if (event_fd == dpy_fd) {
				// -1 means EPOLLHUP
				if (handlexevent(events + i) == -1) {
					fprintf(stderr, "\nEPOLLUP\n");
					return;
				}
			}
			else if (event_fd == ipc_get_sock_fd()) {
				ipc_handle_socket_epoll_event(events + i);
			}
			else if (ipc_is_client_registered(event_fd)){
				if (ipc_handle_client_epoll_event(
						events + i, mons, &lastselmon, selmon, tags, LENGTH(tags), layouts, LENGTH(layouts)
					) < 0) {
					logdatetime(stderr);
					fprintf(stderr, "dwm: Error handling IPC event on fd %d\n", event_fd);
				}
			}
			else {
				logdatetime(stderr);
				fprintf(stderr,
					"dwm: Got event from unknown fd %d, ptr %p, u32 %d, u64 %lu with events %d\n",
					event_fd, events[i].data.ptr, events[i].data.u32, events[i].data.u64, events[i].events
				);
			}
		}
	}

	#elif PATCH_HANDLE_SIGNALS
	XEvent ev;
	//#if PATCH_FOCUS_FOLLOWS_MOUSE || PATCH_MOUSE_POINTER_HIDING
	#if PATCH_MOUSE_POINTER_HIDING
	XGenericEventCookie *cookie;
	#endif // PATCH_MOUSE_POINTER_HIDING
	//#endif // PATCH_FOCUS_FOLLOWS_MOUSE || PATCH_MOUSE_POINTER_HIDING
	XSync(dpy, False);
	/* main event loop */
	while (running == 1) {
		//#if PATCH_FOCUS_FOLLOWS_MOUSE || PATCH_MOUSE_POINTER_HIDING
		#if PATCH_MOUSE_POINTER_HIDING
		cookie = &ev.xcookie;
		#endif // PATCH_MOUSE_POINTER_HIDING
		//#endif // PATCH_FOCUS_FOLLOWS_MOUSE || PATCH_MOUSE_POINTER_HIDING

		struct pollfd pfd = {
			.fd = ConnectionNumber(dpy),
			.events = POLLIN,
		};
		int pending = XPending(dpy) > 0 || poll(&pfd, 1, -1) > 0;

		if (running != 1)
			break;
		if (!pending)
			continue;

		XNextEvent(dpy, &ev);

		#if PATCH_MOUSE_POINTER_HIDING
		if (ev.type == motion_type) {
			if (!cursor_always_hide)
				showcursor();
			#if 0 //PATCH_FOCUS_FOLLOWS_MOUSE
			if (selmon->sel)
				checkmouseoverclient();
			#endif // PATCH_FOCUS_FOLLOWS_MOUSE
			break;
		}
		else if (ev.type == key_release_type) {
			if ((!selmon && cursorhideonkeys)
			|| (!selmon->sel && selmon->cursorhideonkeys)
			|| (selmon->sel && (selmon->sel->cursorhideonkeys == 1 || (selmon->sel->cursorhideonkeys == -1 && selmon->cursorhideonkeys)))) {
				unsigned int state = 0;
				if (cursor_ignore_mods) {
					// extract modifier state;
					// xinput device event;
					XDeviceKeyEvent *key = (XDeviceKeyEvent *) &ev;
					if ((state = (key->keycode == 9)))
						showcursor();
					else
						state = (key->state & cursor_ignore_mods);
				}
				if (!state)
					hidecursor();
			}
		}
		else if (ev.type == button_press_type || ev.type == button_release_type) {
			if (!cursor_always_hide)
				showcursor();
		}
		else if (ev.type == device_change_type) {
			XDevicePresenceNotifyEvent *xdpe = (XDevicePresenceNotifyEvent *)&ev;
			if (last_device_change != xdpe->serial) {
				snoop_root();
				last_device_change = xdpe->serial;
			}
		}
		else if (ev.type == GenericEvent) {
			/* xi2 raw event */
			XGetEventData(dpy, cookie);
			XIDeviceEvent *xie = (XIDeviceEvent *)cookie->data;

			switch (xie->evtype) {
				case XI_RawMotion:
				#if 0
					#if PATCH_FOCUS_FOLLOWS_MOUSE
					if (selmon->sel)
						checkmouseoverclient();
					#endif // PATCH_FOCUS_FOLLOWS_MOUSE
				#endif

				case XI_RawButtonPress:
					if (ignore_scroll && ((xie->detail >= 4 && xie->detail <= 7) || xie->event_x == xie->event_y))
						break;
					if (!cursor_always_hide)
						showcursor();
					break;

				case XI_RawButtonRelease:
					break;

				default:
					DEBUG("unknown XI event type %d\n", xie->evtype);
			}

			XFreeEventData(dpy, cookie);
		}
		else if (ev.type == (timer_sync_event + XSyncAlarmNotify)) {
			XSyncAlarmNotifyEvent *alarm_e = (XSyncAlarmNotifyEvent *)&ev;
			if (alarm_e->alarm == cursor_idle_alarm) {
				if (cursortimeout) {
					int hide = 0;

					if ((!selmon && cursorautohide)
					|| (!selmon->sel && selmon->cursorautohide))
						hide = 1;
					else {
						int x, y;
						Client *sel = NULL;
						if (getrootptr(&x, &y))
							sel = getclientatcoords(x, y, False);
						if (!sel)
							sel = selmon->sel;
						if (selmon->sel && sel == selmon->sel && (sel->cursorautohide == 1 || (sel->cursorautohide == -1 && sel->mon->cursorautohide)))
							hide = 1;
					}
					if (hide) {
						//DEBUG("idle counter reached %dms, hiding cursor\n", XSyncValueLow32(alarm_e->counter_value));
						hidecursor();
					}
					#if DEBUGGING
					else {
						//DEBUG("idle counter reached %dms\n", XSyncValueLow32(alarm_e->counter_value));
					}
					#endif // DEBUGGING
				}
			}
		}
		else
		#else // NO PATCH_MOUSE_POINTER_HIDING
		#if PATCH_FOCUS_FOLLOWS_MOUSE
		#if 0
		// sometimes enternotify events don't get picked up, this is a reasonable workaround;
		// BROKEN - tiled clients with popups that overlap other clients will lose focus when pointer strays over the overlapped client;
		if (ev.type == motion_type) {
			if (selmon->sel)
				checkmouseoverclient();
			break;
		}
		else
		if (ev.type == device_change_type) {
			XDevicePresenceNotifyEvent *xdpe = (XDevicePresenceNotifyEvent *)&ev;
			if (last_device_change != xdpe->serial) {
				snoop_root();
				last_device_change = xdpe->serial;
			}
		}
		else if (ev.type == GenericEvent) {
			/* xi2 raw event */
			XGetEventData(dpy, cookie);
			XIDeviceEvent *xie = (XIDeviceEvent *)cookie->data;
			if (xie->evtype == XI_RawMotion && selmon->sel)
				checkmouseoverclient();
			XFreeEventData(dpy, cookie);
		}
		#endif
		#endif // PATCH_FOCUS_FOLLOWS_MOUSE
		#endif // PATCH_MOUSE_POINTER_HIDING

		if (handler[ev.type])
			handler[ev.type](&ev); /* call handler */
	}
	#else // NO PATCH_IPC
	XEvent ev;
	//#if PATCH_FOCUS_FOLLOWS_MOUSE || PATCH_MOUSE_POINTER_HIDING
	#if PATCH_MOUSE_POINTER_HIDING
	XGenericEventCookie *cookie;
	#endif // PATCH_MOUSE_POINTER_HIDING
	//#endif // PATCH_FOCUS_FOLLOWS_MOUSE || PATCH_MOUSE_POINTER_HIDING

	// main event loop;
	XSync(dpy, False);
	while (running == 1) {

		//#if PATCH_FOCUS_FOLLOWS_MOUSE || PATCH_MOUSE_POINTER_HIDING
		#if PATCH_MOUSE_POINTER_HIDING
		cookie = &ev.xcookie;
		#endif // PATCH_MOUSE_POINTER_HIDING
		//#endif // PATCH_FOCUS_FOLLOWS_MOUSE || PATCH_MOUSE_POINTER_HIDING
		if (XNextEvent(dpy, &ev))
			break;

		#if PATCH_MOUSE_POINTER_HIDING
		if (ev.type == motion_type) {
			if (!cursor_always_hide)
				showcursor();
			#if 0 //PATCH_FOCUS_FOLLOWS_MOUSE
			if (selmon->sel)
				checkmouseoverclient();
			#endif // PATCH_FOCUS_FOLLOWS_MOUSE
			break;
		}
		else if (ev.type == key_release_type) {
			if ((!selmon && cursorhideonkeys)
			|| (!selmon->sel && selmon->cursorhideonkeys)
			|| (selmon->sel && (selmon->sel->cursorhideonkeys == 1 || (selmon->sel->cursorhideonkeys == -1 && selmon->cursorhideonkeys)))) {
				unsigned int state = 0;
				if (cursor_ignore_mods) {
					// extract modifier state;
					// xinput device event;
					XDeviceKeyEvent *key = (XDeviceKeyEvent *) &ev;
					if ((state = (key->keycode == 9)))
						showcursor();
					else
						state = (key->state & cursor_ignore_mods);
				}
				if (!state)
					hidecursor();
			}
		}
		else if (ev.type == button_press_type || ev.type == button_release_type) {
			if (!cursor_always_hide)
				showcursor();
		}
		else if (ev.type == device_change_type) {
			XDevicePresenceNotifyEvent *xdpe = (XDevicePresenceNotifyEvent *)&ev;
			if (last_device_change != xdpe->serial) {
				snoop_root();
				last_device_change = xdpe->serial;
			}
		}
		else if (ev.type == GenericEvent) {
			/* xi2 raw event */
			XGetEventData(dpy, cookie);
			XIDeviceEvent *xie = (XIDeviceEvent *)cookie->data;

			switch (xie->evtype) {
				case XI_RawMotion:
				#if 0
					#if PATCH_FOCUS_FOLLOWS_MOUSE
					if (selmon->sel)
						checkmouseoverclient();
					#endif // PATCH_FOCUS_FOLLOWS_MOUSE
				#endif

				case XI_RawButtonPress:
					if (ignore_scroll && ((xie->detail >= 4 && xie->detail <= 7) || xie->event_x == xie->event_y))
						break;
					if (!cursor_always_hide)
						showcursor();
					break;

				case XI_RawButtonRelease:
					break;

				default:
					DEBUG("unknown XI event type %d\n", xie->evtype);
			}

			XFreeEventData(dpy, cookie);
		}
		else if (ev.type == (timer_sync_event + XSyncAlarmNotify)) {
			XSyncAlarmNotifyEvent *alarm_e = (XSyncAlarmNotifyEvent *)&ev;
			if (alarm_e->alarm == cursor_idle_alarm) {
				if (cursortimeout) {
					int hide = 0;

					if ((!selmon && cursorautohide)
					|| (!selmon->sel && selmon->cursorautohide))
						hide = 1;
					else {
						int x, y;
						Client *sel = NULL;
						if (getrootptr(&x, &y))
							sel = getclientatcoords(x, y, False);
						if (!sel)
							sel = selmon->sel;
						if (selmon->sel && sel == selmon->sel && (sel->cursorautohide == 1 || (sel->cursorautohide == -1 && sel->mon->cursorautohide)))
							hide = 1;
					}
					if (hide) {
						//DEBUG("idle counter reached %dms, hiding cursor\n", XSyncValueLow32(alarm_e->counter_value));
						hidecursor();
					}
					#if DEBUGGING
					else {
						//DEBUG("idle counter reached %dms\n", XSyncValueLow32(alarm_e->counter_value));
					}
					#endif // DEBUGGING
				}
			}
		}
		else
		#else // NO PATCH_MOUSE_POINTER_HIDING
		#if PATCH_FOCUS_FOLLOWS_MOUSE
		#if 0
		// sometimes enternotify events don't get picked up, this is a reasonable workaround;
		// BROKEN - tiled clients with popups that overlap other clients will lose focus when pointer strays over the overlapped client;
		if (ev.type == motion_type) {
			if (selmon->sel)
				checkmouseoverclient();
			break;
		}
		else
		if (ev.type == device_change_type) {
			XDevicePresenceNotifyEvent *xdpe = (XDevicePresenceNotifyEvent *)&ev;
			if (last_device_change != xdpe->serial) {
				snoop_root();
				last_device_change = xdpe->serial;
			}
		}
		else if (ev.type == GenericEvent) {
			/* xi2 raw event */
			XGetEventData(dpy, cookie);
			XIDeviceEvent *xie = (XIDeviceEvent *)cookie->data;
			if (xie->evtype == XI_RawMotion && selmon->sel)
				checkmouseoverclient();
			XFreeEventData(dpy, cookie);
		}
		#endif
		#endif // PATCH_FOCUS_FOLLOWS_MOUSE
		#endif // PATCH_MOUSE_POINTER_HIDING

		if (handler[ev.type]) {
			#if PATCH_LOG_DIAGNOSTICS
			if (logdiagnostics_event(ev))
			#endif // PATCH_LOG_DIAGNOSTICS
			handler[ev.type](&ev); /* call handler */
		}
	}
	#endif // PATCH_IPC

	XSync(dpy, False);
}

#if PATCH_IPC
int
run_command(char *name, char *args[], int argc)
{
	if (connect_to_socket() == -1) {
		logdatetime(stderr);
		fprintf(stderr, "dwm: Failed to connect to socket \"%s\".\n", ipcsockpath);
		return 0;
	}

	cJSON *json = cJSON_CreateObject();
	cJSON_AddStringToObject(json, "command", name);
	cJSON *array = cJSON_AddArrayToObject(json, "args");
	for (int i = 0; i < argc; i++) {
		cJSON *item;
		if (is_signed_int(args[i]))
			item = cJSON_CreateInteger(atoll(args[i]));
		else if (is_float(args[i]))
			item = cJSON_CreateNumber(atof(args[i]));
		else if (is_hex(args[i]))
			item = cJSON_CreateInteger(strtol(args[i],NULL,16));
		else
			item = cJSON_CreateString(args[i]);
		cJSON_AddItemToArray(array, item);
	}

	// Message format:
	// {
	//   "command": "<name>",
	//   "args": [ ... ]
	// }

	char *msg = cJSON_PrintUnformatted(json);
	size_t msg_size = strlen(msg) + 1;
	send_message(IPC_TYPE_RUN_COMMAND, msg_size, (uint8_t *)msg);

	if (!ipc_ignore_reply)
		print_socket_reply();
	else
		flush_socket_reply();

	cJSON_free(msg);
	cJSON_Delete(json);

	return 1;
}
#endif // PATCH_IPC

void
scan(void)
{
	int i;
	unsigned int num;
	Window d1, d2, *wins = NULL;
	XWindowAttributes wa;
	Client *c = NULL;
	Monitor *m = NULL, *mdef = NULL;

	if (XQueryTree(dpy, root, &d1, &d2, &wins, &num)) {

		nonstop = 1;
		if (num > 0) {

			#if PATCH_PERSISTENT_METADATA
			int format;
			unsigned long *data, n, extra, index;
			Atom atom;

			// restore clients in (reverse) order based on the index stored during cleanup();
			// so that the client list is in the same order after a manual restart (as much as possible);
			Window wlist[num];
			for (i = 0; i < num; wlist[i++] = 0);
			#if DEBUGGING
			int wcount = 0;
			#endif // DEBUGGING
			for (i = 0; i < num; i++) {

				if ((c = wintoclient(wins[i]))) {
					DEBUG("scan - window 0x%lx already linked to client \"%s\".\n", wins[i], c->name);
					continue;
				}

				if (!XGetWindowAttributes(dpy, wins[i], &wa)
					#if !PATCH_SCAN_OVERRIDE_REDIRECTS
					|| wa.override_redirect
					#endif // PATCH_SCAN_OVERRIDE_REDIRECTS
					|| !wa.depth
					|| XGetTransientForHint(dpy, wins[i], &d1)
					)
					continue;
				long state = getstate(wins[i]);
				if (wa.map_state == IsViewable || state == IconicState) {
					index = 0;

					if (XGetWindowProperty(dpy, wins[i], netatom[NetClientInfo], 0L, 1L, False, XA_CARDINAL,
							&atom, &format, &n, &extra, (unsigned char **)&data) == Success && n > 0) {
						index = *data;
					}
					if (n > 0)
						XFree(data);
					if (index < 1 || index > num || wlist[index - 1]) {
						if (n > 0) {
							logdatetime(stderr);
							fprintf(stderr, "dwm: client index out of range or already taken, mapping out of order (0x%lx).\n", wins[i]);
						}
						manage(wins[i], &wa);
						continue;
					}
					#if DEBUGGING
					wcount++;
					#endif // DEBUGGING
					wlist[index - 1] = wins[i];
				}
				else {
					char name[256];
					if (!gettextprop(wins[i], netatom[NetWMName], name, sizeof name))
						gettextprop(wins[i], XA_WM_NAME, name, sizeof name);
					if (name[0] == '\0')
						strcpy(name, "<no name>");

					if (state < 0) {
						DEBUG("dwm: unable to get WM_STATE property (0x%lx: %s).\n", wins[i], name);
					}
					else
						DEBUG("dwm: map_state(0x%x) not IsViewable or state(0x%lx) not IconicState (0x%lx: %s).\n", wa.map_state, state, wins[i], name);
				}
			}

			#if DEBUGGING
			if (wcount) {
				wcount = 0;
				logdatetime(stderr);
				fprintf(stderr, "debug: ordered manage of non-transient clients:\n");
			}
			#endif // DEBUGGING
			for (i = num - 1; i >= 0; i--)
				if (wlist[i]) {
					DEBUG("%i: 0x%lx\n", i, wlist[i]);
					if (XGetWindowAttributes(dpy, wlist[i], &wa))
						manage(wlist[i], &wa);
					wlist[i] = 0;
				}

			for (i = 0; i < num; i++) { /* now the transients */
				if ((c = wintoclient(wins[i]))) {
					DEBUG("scan - transient window 0x%lx already linked to client \"%s\".\n", wins[i], c->name);
					continue;
				}

				int result = XGetWindowAttributes(dpy, wins[i], &wa);
				if (!result) {
					logdatetime(stderr);
					fprintf(stderr, "dwm: unable to get window attributes (0x%lx).\n", wins[i]);
				}
				if (!result)
				//if (!XGetWindowAttributes(dpy, wins[i], &wa))
					continue;
				if (XGetTransientForHint(dpy, wins[i], &d1)
				&& (wa.map_state == IsViewable || getstate(wins[i]) == IconicState)) {
					index = 0;

					if (XGetWindowProperty(dpy, wins[i], netatom[NetClientInfo], 0L, 1L, False, XA_CARDINAL,
							&atom, &format, &n, &extra, (unsigned char **)&data) == Success && n > 0) {
						index = *data;
					}
					else index = 0;
					if (n > 0)
						XFree(data);

					if (index < 1 || index > num || wlist[index - 1]) {
						if (n > 0) {
							logdatetime(stderr);
							fprintf(stderr, "dwm: transient client index out of range or already taken, mapping out of order (0x%lx).\n", wins[i]);
						}
						manage(wins[i], &wa);
						continue;
					}
					#if DEBUGGING
					wcount++;
					#endif // DEBUGGING
					wlist[index - 1] = wins[i];
				}
			}
			#if DEBUGGING
			if (wcount) {
				logdatetime(stderr);
				fprintf(stderr, "debug: ordered manage of transient clients:\n");
			}
			#endif // DEBUGGING
			for (i = num - 1; i >= 0; i--)
				if (wlist[i]) {
					DEBUG("%i: 0x%lx\n", i, wlist[i]);
					if (XGetWindowAttributes(dpy, wlist[i], &wa))
						manage(wlist[i], &wa);
				}
			#else // NO PATCH_PERSISTENT_METADATA
			for (i = 0; i < num; i++) {
				if ((c = wintoclient(wins[i]))) {
					DEBUG("scan - window 0x%lx already linked to client \"%s\".\n", wins[i], c->name);
					continue;
				}

				if (!XGetWindowAttributes(dpy, wins[i], &wa)
				#if !PATCH_SCAN_OVERRIDE_REDIRECTS
				|| wa.override_redirect
				#endif // PATCH_SCAN_OVERRIDE_REDIRECTS
				|| !wa.depth || XGetTransientForHint(dpy, wins[i], &d1))
					continue;
				if (wa.map_state == IsViewable || getstate(wins[i]) == IconicState)
					manage(wins[i], &wa);
			}
			for (i = 0; i < num; i++) { /* now the transients */
				if ((c = wintoclient(wins[i]))) {
					DEBUG("scan - transient window 0x%lx already linked to client \"%s\".\n", wins[i], c->name);
					continue;
				}
				if (!XGetWindowAttributes(dpy, wins[i], &wa))
					continue;
				if (XGetTransientForHint(dpy, wins[i], &d1)
				&& (wa.map_state == IsViewable || getstate(wins[i]) == IconicState))
					manage(wins[i], &wa);
			}
			#endif // PATCH_PERSISTENT_METADATA
		}
		if (wins)
			XFree(wins);

		if (!selmon->sel) {

			if (selmon->clients && !selmon->sel) {
				for (c = selmon->clients; c && (
					#if PATCH_FLAG_HIDDEN
					c->ishidden ||
					#endif // PATCH_FLAG_HIDDEN
					#if PATCH_FLAG_IGNORED
					c->isignored ||
					#endif // PATCH_FLAG_IGNORED
					#if PATCH_FLAG_PANEL
					c->ispanel ||
					#endif // PATCH_FLAG_PANEL
					#if PATCH_SHOW_DESKTOP
					c->isdesktop ||
					#endif // PATCH_SHOW_DESKTOP
				0); c = c->next);
				selmon->sel = c;
			}

			for (m = mons; m; m = m->next) {
				if (m->isdefault)
					mdef = m;
				if (m->defaulttag)
					viewmontag(m, (1 << (m->defaulttag - 1)), 1);
			}
			focus(NULL, 0);
		}

		nonstop = 0;
		arrange(NULL);

		if (mdef) {
			unfocus(selmon->sel, 0);
			selmon = mdef;
			XWarpPointer(dpy, None, root, 0, 0, 0, 0, selmon->wx + (selmon->ww / 2), selmon->wy + (selmon->wh / 2));
			focus(NULL, 0);
			#if PATCH_MOUSE_POINTER_WARPING
			if (selmon->sel)
			#if PATCH_MOUSE_POINTER_WARPING_SMOOTH
				warptoclient(selmon->sel, 0, 1);
			#else // NO PATCH_MOUSE_POINTER_WARPING_SMOOTH
				warptoclient(selmon->sel, 1);
			#endif // PATCH_MOUSE_POINTER_WARPING_SMOOTH
			#endif // PATCH_MOUSE_POINTER_WARPING
			drawbars();
		}

	}
}

#if PATCH_IPC
int
send_message(IPCMessageType msg_type, uint32_t msg_size, uint8_t *msg)
{
	dwm_ipc_header_t header = {.magic = IPC_MAGIC_ARR, .size = msg_size, .type = msg_type};

	size_t header_size = sizeof(dwm_ipc_header_t);
	size_t total_size = header_size + msg_size;

	uint8_t buffer[total_size];

	// Copy header to buffer
	memcpy(buffer, &header, header_size);
	// Copy message to buffer
	memcpy(buffer + header_size, msg, header.size);

	write_socket(buffer, total_size);

	return 0;
}
#endif // PATCH_IPC

int
sendevent(Window w, Atom proto, int mask, long d0, long d1, long d2, long d3, long d4)
{
	int n;
	Atom *protocols, mt;
	int exists = 0;
	XEvent ev;

	if (proto == wmatom[WMTakeFocus] || proto == wmatom[WMDelete]) {
		mt = wmatom[WMProtocols];
		if (XGetWMProtocols(dpy, w, &protocols, &n)) {
			while (!exists && n--)
				exists = protocols[n] == proto;
			XFree(protocols);
		}
	}
	else {
		exists = True;
		mt = proto;
	}

	if (exists) {
		ev.type = ClientMessage;
		ev.xclient.window = w;
		ev.xclient.message_type = mt;
		ev.xclient.format = 32;
		ev.xclient.data.l[0] = d0;
		ev.xclient.data.l[1] = d1;
		ev.xclient.data.l[2] = d2;
		ev.xclient.data.l[3] = d3;
		ev.xclient.data.l[4] = d4;
		XSendEvent(dpy, w, False, mask, &ev);
	}
	return exists;
}

void
sendmon(Client *c, Monitor *m, Client *leader, int force)
{
	if (c->mon == m)
		return;
	#if PATCH_SHOW_DESKTOP
	if (c->ondesktop || c->isdesktop)
		return;
	#endif // PATCH_SHOW_DESKTOP
	#if PATCH_FLAG_FOLLOW_PARENT
	if (c->followparent && c->parent && !c->toplevel && !c->fosterparent && !force)
		return;
	#endif // PATCH_FLAG_FOLLOW_PARENT

	float mfwo = 1.0f, mfho = 1.0f;
	if (c->isfloating && !force) {
		mfwo = (float) (c->x - c->mon->mx + c->w / 2) / (c->mon->mw / 2);
		mfho = (float) (c->y - c->mon->my + c->h / 2) / (c->mon->mh / 2);
	}

	#if PATCH_SHOW_DESKTOP
	#if PATCH_SHOW_DESKTOP_ONLY_WHEN_ACTIVE
	Monitor *mon = c->mon;
	int nc = 0;
	#endif // PATCH_SHOW_DESKTOP_ONLY_WHEN_ACTIVE
	#endif // PATCH_SHOW_DESKTOP
	int sel = (leader ? (leader == c->mon->sel ? 1 : 0) : 0);
	#if PATCH_FOCUS_FOLLOWS_MOUSE && !PATCH_KEY_HOLD
	int px, py;
	unsigned int cw = 1, ch = 1;
	float sfw, sfh;
	if (sel) {
		getrelativeptr(leader, &px, &py);
		cw = leader->w;
		ch = leader->h;
	}
	#endif // PATCH_FOCUS_FOLLOWS_MOUSE && !PATCH_KEY_HOLD

	#if PATCH_MODAL_SUPPORT
	if (c->ismodal) {
		Client *cc, *p, *child = c;
		for (cc = c->snext; cc && cc->snext; cc = cc->snext);
		// prevent client moves via resize() triggering updates of
		// the client's parent offset coordinates, sfxo & sfyo
		nonstop = 1;
		for (; cc; cc = p) {
			p = cc->sprev;
			if (cc->ultparent == c->ultparent && ISVISIBLE(cc)) {
				if (cc->isfloating && (cc == c || !child || child->parent != cc)) {
					mfwo = (float) (cc->x - cc->mon->mx + cc->w / 2) / (cc->mon->mw / 2);
					mfho = (float) (cc->y - cc->mon->my + cc->h / 2) / (cc->mon->mh / 2);
				}
				detach(cc);
				detachstack(cc);
				cc->mon = m;
				cc->monindex = m->num;
				cc->tags = m->tagset[m->seltags]; 	// assign tags of target monitor
				#if PATCH_ATTACH_BELOW_AND_NEWMASTER
				attachBelow(cc);
				attachstackBelow(cc);
				#else // NO PATCH_ATTACH_BELOW_AND_NEWMASTER
				attach(cc);
				attachstack(cc);
				#endif // PATCH_ATTACH_BELOW_AND_NEWMASTER
				if (cc->isfloating) {
					// try to move the client relative to its immediate child;
					if (child && child->parent == cc) {
						cc->x = (child->x + child->w / 2) - ((child->sfxo == 0 ? 1 : child->sfxo) * cc->w / 2);
						cc->y = (child->y + child->h / 2) - ((child->sfyo == 0 ? 1 : child->sfyo) * cc->h / 2);
					}
					else {
						cc->x = ((mfwo * m->mw / 2) + m->mx - cc->w / 2);
						cc->y = ((mfho * m->mh / 2) + m->my - cc->h / 2);
					}
					// ensure floating clients are within the monitor bounds;
					resize(cc, cc->x, cc->y, cc->w, cc->h, False);
				}
				#if PATCH_PERSISTENT_METADATA
				setclienttagprop(cc);
				#endif // PATCH_PERSISTENT_METADATA
				child = cc;
			}
		}
		// normal coord tracking behaviour continues;
		nonstop = 0;
		snapchildclients(c->ultparent, 1);
	}
	else
	#endif // PATCH_MODAL_SUPPORT
	{
		#if PATCH_FLAG_FOLLOW_PARENT || PATCH_MODAL_SUPPORT
		monsatellites(c, m);
		#endif // PATCH_FLAG_FOLLOW_PARENT || PATCH_MODAL_SUPPORT

		//unfocus(c, 1);
		detach(c);
		detachstack(c);
		c->mon = m;
		c->monindex = m->num;
		c->tags = m->tagset[m->seltags]; /* assign tags of target monitor */
		#if PATCH_CLASS_STACKING
		if(!attach_stackhead(c))
		#endif // PATCH_CLASS_STACKING
		{
			#if PATCH_ATTACH_BELOW_AND_NEWMASTER
			if (m->lt[m->sellt]->arrange != &monocle &&
				(!c->isfullscreen
				#if PATCH_FLAG_FAKEFULLSCREEN
				|| c->fakefullscreen == 1
				#endif // PATCH_FLAG_FAKEFULLSCREEN
			)) {
				attachBelow(c);
				attachstackBelow(c);
			}
			else
			#endif // PATCH_ATTACH_BELOW_AND_NEWMASTER
			{
				attach(c);
				attachstackex(c);
				m->sel = c;
			}
		}
		// move the client if it is floating;
		if (c->isfloating) {
			if (!c->parent || c->parent->mon != c->mon) {
				if (!force) {
					c->x = ((mfwo * m->mw / 2) + m->mx - c->w / 2);
					c->y = ((mfho * m->mh / 2) + m->my - c->h / 2);
				}
				if (c->w + c->bw*2 < m->mw) {
					if (c->x + c->w + c->bw*2 > m->mx + m->mw)
						c->x = m->mx + m->mw - c->w - c->bw*2;
					else if (c->x < m->mx)
						c->x = m->mx;
				}
				if (c->h + c->bw*2 < m->mh) {
					if (c->y + c->h + c->bw*2 > m->my + m->mh)
						c->y = m->my + m->mh - c->h - c->bw*2;
					else if (c->y < m->my)
						c->y = m->my;
				}
				resizeclient(c, c->x, c->y, c->w, c->h, 0);
			}
			else if (c->parent)
				snapchildclients(c->parent, 0);
		}
		#if PATCH_PERSISTENT_METADATA
		setclienttagprop(c);
		#endif // PATCH_PERSISTENT_METADATA

		#if PATCH_FLAG_FOLLOW_PARENT
		snapchildclients(c, 1);
		#endif // PATCH_FLAG_FOLLOW_PARENT
	}

	#if PATCH_SHOW_DESKTOP
	if (showdesktop && m->showdesktop != c->ondesktop) {
		#if PATCH_SHOW_DESKTOP_WITH_FLOATING
		if (!showdesktop_floating ||
			!c->isfloating || (
				c->isfullscreen
				#if PATCH_FLAG_FAKEFULLSCREEN
				&& c->fakefullscreen != 1
				#endif // PATCH_FLAG_FAKEFULLSCREEN
			)
		)
		#endif // PATCH_SHOW_DESKTOP_WITH_FLOATING
			m->showdesktop = c->ondesktop;
	}
	#if PATCH_SHOW_DESKTOP_ONLY_WHEN_ACTIVE
	if (showdesktop && showdesktop_when_active && getdesktopclient(mon, &nc) && !nc && !mon->showdesktop) {
		mon->showdesktop = 1;
		if (!sel)
			arrange(mon);
	}
	#endif // PATCH_SHOW_DESKTOP_ONLY_WHEN_ACTIVE
	#endif // PATCH_SHOW_DESKTOP

	#if PATCH_KEY_HOLD
	arrange(NULL);
	if (sel)
		focus(NULL, 0);
	#else // NO PATCH_KEY_HOLD
	if (sel) {
		m->sel = leader;
		selmon = m;

		arrange(NULL);
		focus(sel ? leader : NULL, 1);

		#if PATCH_FOCUS_FOLLOWS_MOUSE
		if (cw != 0 && ch != 0) {
			sfw = ((float) leader->w / cw);
			sfh = ((float) leader->h / ch);
			XWarpPointer(dpy, None, leader->win, 0, 0, 0, 0, px*sfw, py*sfh);
		}
		#endif // PATCH_FOCUS_FOLLOWS_MOUSE
	}
	#endif // PATCH_KEY_HOLD

	if (c->isfullscreen
		#if PATCH_FLAG_FAKEFULLSCREEN
		&& c->fakefullscreen != 1
		#endif // PATCH_FLAG_FAKEFULLSCREEN
		) {
		resizeclient(c, c->mon->mx, c->mon->my, c->mon->mw, c->mon->mh, 0);
		raiseclient(c);
		c->mon->sel = c;
		for (int i = 0; i < LENGTH(tags); i++)
			if (c->tags & (1 << i))
				c->mon->focusontag[i] = c;
		drawbar(c->mon, 1);
	}
}


#if PATCH_MOUSE_POINTER_HIDING
int
set_idle_alarm(void)
{
	XSyncAlarmAttributes attr;
	XSyncValue value;
	unsigned int flags;

	XSyncQueryCounter(dpy, counter_idletime, &value);

	attr.trigger.counter = counter_idletime;
	attr.trigger.test_type = XSyncPositiveComparison;
	attr.trigger.value_type = XSyncRelative;
	XSyncIntsToValue(&attr.trigger.wait_value, cursortimeout * 1000, (unsigned long)(cursortimeout * 1000) >> 32);
	XSyncIntToValue(&attr.delta, 0);

	flags = XSyncCACounter | XSyncCATestType | XSyncCAValue | XSyncCADelta;

	if (cursor_idle_alarm)
		XSyncDestroyAlarm(dpy, cursor_idle_alarm);

	cursor_idle_alarm = XSyncCreateAlarm(dpy, flags, &attr);
	return cursor_idle_alarm;
}
#endif // PATCH_MOUSE_POINTER_HIDING

void
publishwindowstate(Client *c)
{
	Atom state[NetWMFullscreen - NetWMState];
	int i = 0;
	if (c->isurgent)
		state[i++] = netatom[NetWMAttention];
	if (c->isfullscreen)
		state[i++] = netatom[NetWMFullscreen];
	#if PATCH_FLAG_ALWAYSONTOP
	if (c->alwaysontop)
		state[i++] = netatom[NetWMStaysOnTop];
	#endif // PATCH_FLAG_ALWAYSONTOP
	#if PATCH_FLAG_HIDDEN
	if (c->ishidden)
		state[i++] = netatom[NetWMHidden];
	#endif // PATCH_FLAG_HIDDEN
	#if PATCH_MODAL_SUPPORT
	if (c->ismodal)
		state[i++] = netatom[NetWMModal];
	#endif // PATCH_MODAL_SUPPORT
	#if PATCH_FLAG_STICKY
	if (c->issticky)
		state[i++] = netatom[NetWMSticky];
	#endif // PATCH_FLAG_STICKY

	XChangeProperty(dpy, c->win, netatom[NetWMState], XA_ATOM, 32,
			PropModeReplace, (unsigned char *) state, i);
}

#if PATCH_FLAG_ALWAYSONTOP
void
setalwaysontop(Client *c, int alwaysontop)
{
	if ((alwaysontop && c->alwaysontop) || (!alwaysontop && !c->alwaysontop))
		return;

	c->alwaysontop = alwaysontop;
	publishwindowstate(c);
	if (!alwaysontop)
		arrange(c->mon);
}
#endif // PATCH_FLAG_ALWAYSONTOP

void
setclientstate(Client *c, long state)
{
	long data[] = { state, None };

	#if PATCH_CROP_WINDOWS
	if (c->crop)
		c = c->crop;
	#endif // PATCH_CROP_WINDOWS
	XChangeProperty(dpy, c->win, wmatom[WMState], wmatom[WMState], 32,
		PropModeReplace, (unsigned char *)data, 2);
}

#if PATCH_EWMH_TAGS
void
setcurrentdesktop(void){
	long data[] = { 0 };
	XChangeProperty(dpy, root, netatom[NetCurrentDesktop], XA_CARDINAL, 32, PropModeReplace, (unsigned char *)data, 1);
}
#endif // PATCH_EWMH_TAGS

void
setdefaultcolours(char *colours[3], char *defaults[3])
{
	if (!defaults || !colours)
		return;
	for (int i = 0; i < 3; i++)
		if (!colours[i] && defaults[i])
			colours[i] = defaults[i];
}

void
setdefaultvalues(Client *c)
{
	c->toplevel = 0;
	c->fosterparent = 0;
	#if PATCH_FLAG_TITLE
	c->displayname = NULL;
	#endif // PATCH_FLAG_TITLE
	c->autofocus = 1;
	#if PATCH_MOUSE_POINTER_HIDING
	c->cursorautohide = -1;
	c->cursorhideonkeys = -1;
	#endif // PATCH_MOUSE_POINTER_HIDING
	#if PATCH_SHOW_MASTER_CLIENT_ON_TAG
	c->dispclass = NULL;
	#endif // PATCH_SHOW_MASTER_CLIENT_ON_TAG
	#if PATCH_ALTTAB
	c->grpclass = NULL;
	#endif // PATCH_ALTTAB
	#if PATCH_CLASS_STACKING
	c->stackclass = NULL;
	#endif // PATCH_CLASS_STACKING
	c->tags = 0;
	#if PATCH_FLAG_CAN_LOSE_FOCUS
	c->canlosefocus = 0;
	#endif // PATCH_FLAG_CAN_LOSE_FOCUS
	#if PATCH_FLAG_CENTRED
	c->iscentred = 0;
	c->iscentred_override = -1;
	#endif // PATCH_FLAG_CENTRED
	#if PATCH_SHOW_DESKTOP
	c->isdesktop = -1;
	#endif // PATCH_SHOW_DESKTOP
	#if PATCH_MOUSE_POINTER_WARPING
	c->focusdx = 1;
	c->focusdy = 1;
	#if PATCH_MOUSE_POINTER_WARPING_RECALL
	c->focusabs = 0;
	c->lastdx = 1;
	c->lastdy = 1;
	c->nolastcoords = 1;
	#endif // PATCH_MOUSE_POINTER_WARPING_RECALL
	#endif // PATCH_MOUSE_POINTER_WARPING
	#if PATCH_ATTACH_BELOW_AND_NEWMASTER
	c->newmaster = 0;
	#endif // PATCH_ATTACH_BELOW_AND_NEWMASTER
	#if PATCH_PAUSE_PROCESS
	c->paused = 0;
	#endif // PATCH_PAUSE_PROCESS
	c->isurgent = 0;
	#if PATCH_FLAG_IGNORED
	c->isignored = 0;
	#endif // PATCH_FLAG_IGNORED
	#if PATCH_FLAG_PANEL
	c->ispanel = 0;
	#endif // PATCH_FLAG_PANEL
	c->isfloating_override = -1;
	#if PATCH_FLAG_FLOAT_ALIGNMENT
	c->floatingx = -1;
	c->floatingy = -1;
	c->floatalignx = -1;
	c->floataligny = -1;
	#endif // PATCH_FLAG_FLOAT_ALIGNMENT
	#if PATCH_TERMINAL_SWALLOWING
	c->isterminal = 0;
	c->noswallow = 0;
	#endif // PATCH_TERMINAL_SWALLOWING
	#if PATCH_FLAG_ACTIVATION_CLICK
	c->activationclick = 0;
	#endif // PATCH_FLAG_ACTIVATION_CLICK
	#if PATCH_FLAG_ALWAYSONTOP
	c->alwaysontop = 0;
	#endif // PATCH_FLAG_ALWAYSONTOP
	#if PATCH_FLAG_GAME
	c->isgame = 0;
	#if PATCH_FLAG_GAME_STRICT
	c->isgamestrict = 0;
	#endif // PATCH_FLAG_GAME_STRICT
	#endif // PATCH_FLAG_GAME
	c->isfullscreen = 0;
	c->lostfullscreen = 0;
	#if PATCH_FLAG_FAKEFULLSCREEN
	c->fakefullscreen = fakefullscreen_by_default;
	#endif // PATCH_FLAG_FAKEFULLSCREEN
	#if PATCH_MODAL_SUPPORT
	c->ismodal = 0;
	c->ismodal_override = -1;
	#endif // PATCH_MODAL_SUPPORT
	#if PATCH_CLIENT_OPACITY
	c->opacity = -1;
	c->unfocusopacity = -1;
	#endif // PATCH_CLIENT_OPACITY
	#if PATCH_FLAG_NEVER_FOCUS
	c->neverfocus_override = -1;
	#endif // PATCH_FLAG_NEVER_FOCUS
	#if PATCH_FLAG_NEVER_FULLSCREEN
	c->neverfullscreen = 0;
	#endif // PATCH_FLAG_NEVER_FULLSCREEN
	#if PATCH_FLAG_NEVER_MOVE
	c->nevermove = 0;
	#endif // PATCH_FLAG_NEVER_MOVE
	#if PATCH_FLAG_NEVER_RESIZE
	c->neverresize = 0;
	#endif // PATCH_FLAG_NEVER_RESIZE
	c->sfxo = 0;
	c->sfyo = 0;
	c->sfx = -1;
	c->sfy = -1;
	c->sfw = -1;
	c->sfh = -1;
	#if PATCH_FLAG_PARENT
	c->parent_late = -1;
	c->neverparent = 0;
	#endif // PATCH_FLAG_PARENT
	#if PATCH_FLAG_PAUSE_ON_INVISIBLE
	c->pauseinvisible = 0;
	#endif // PATCH_FLAG_PAUSE_ON_INVISIBLE
	c->prevsel = NULL;
}

#if PATCH_EWMH_TAGS
void setdesktopnames(void){
	XTextProperty text;
	Xutf8TextListToTextProperty(dpy, tags, LENGTH(tags), XUTF8StringStyle, &text);
	XSetTextProperty(dpy, root, &text, netatom[NetDesktopNames]);
}
#endif // PATCH_EWMH_TAGS

void
setfocus(Client *c)
{
	if (!c)
		return;
	#if PATCH_CROP_WINDOWS
	if (c->crop)
		c = c->crop;
	#endif // PATCH_CROP_WINDOWS

	// once the client has focus, set autofocus=1 (used in guessnextfocus);
	c->autofocus = 1;

	#if PATCH_FLAG_PAUSE_ON_INVISIBLE
	if (c->pauseinvisible == -1 && c->pid) {
		kill (c->pid, SIGCONT);
		c->pauseinvisible = 1;
		#if PATCH_PAUSE_PROCESS
		c->paused = 0;
		#endif // PATCH_PAUSE_PROCESS
		DEBUG("client continued: \"%s\".\n", c->name);
	}
	#endif // PATCH_FLAG_PAUSE_ON_INVISIBLE

	#if PATCH_FLAG_GAME
	if (c->isgame && MINIMIZED(c))
		unminimize(c);
	#endif // PATCH_FLAG_GAME
	if (c->isfullscreen) {
		#if PATCH_FLAG_GAME
		if (c->isgame) {
			showhidebar(c->mon);
			setclientstate(c, NormalState);
			#if PATCH_FLAG_GAME_STRICT
			if (game && game != c)
				unfocus(game, 0);
			game = c;
			#endif // PATCH_FLAG_GAME_STRICT
			createbarrier(c);
		}
		else destroybarrier();
		#endif // PATCH_FLAG_GAME
		#if PATCH_FLAG_FAKEFULLSCREEN
		if (c->fakefullscreen != 1)
		#endif // PATCH_FLAG_FAKEFULLSCREEN
		{
			resizeclient(c, c->mon->mx, c->mon->my, c->mon->mw, c->mon->mh, 0);
			raiseclient(c);
		}
	}
	#if PATCH_FLAG_GAME
	else destroybarrier();
	#endif // PATCH_FLAG_GAME
	if (!c->isfullscreen && c->isfloating
		#if PATCH_SHOW_DESKTOP
		&& !c->isdesktop
		#endif // PATCH_SHOW_DESKTOP
		)
		raiseclient(c);

	#if PATCH_FLAG_GAME && PATCH_FLAG_GAME_STRICT
	if (game && !ISVISIBLE(game))
		game = NULL;
	#endif // PATCH_FLAG_GAME && PATCH_FLAG_GAME_STRICT

	if (!c->neverfocus
		#if PATCH_FLAG_PANEL
		&& !c->ispanel
		#endif // PATCH_FLAG_PANEL
	) {
		XSetInputFocus(dpy, c->win, RevertToPointerRoot, CurrentTime);
		XChangeProperty(dpy, root, netatom[NetActiveWindow],
			XA_WINDOW, 32, PropModeReplace,
			(unsigned char *) &(c->win), 1);
		sendevent(c->win, wmatom[WMTakeFocus], NoEventMask, wmatom[WMTakeFocus], CurrentTime, 0, 0, 0);
		#if PATCH_FLAG_ACTIVATION_CLICK
		if (c->activationclick) {
			struct timespec ts = { .tv_sec = 0, .tv_nsec = 1000000 };
			int dummy, root_x, root_y;
			unsigned int mask;
			Window wdummy;
			XQueryPointer(dpy, root, &wdummy, &wdummy, &root_x, &root_y, &dummy, &dummy, &mask);

			XButtonEvent xbpe;
			xbpe.window = c->win;
			xbpe.button = c->activationclick;
			xbpe.display = dpy;
			xbpe.root = root;
			xbpe.same_screen = True;
			xbpe.state = mask;
			xbpe.subwindow = None;
			xbpe.time = CurrentTime;
			xbpe.type = ButtonPress;
			XTranslateCoordinates(dpy, xbpe.root, xbpe.window, xbpe.x_root, xbpe.y_root, &xbpe.x, &xbpe.y, &xbpe.subwindow);
			XSendEvent(dpy, c->win, True, ButtonPressMask, (XEvent *)&xbpe);
			XFlush(dpy);
			nanosleep(&ts, NULL);
			xbpe.type = ButtonRelease;
			XSendEvent(dpy, c->win, True, ButtonReleaseMask, (XEvent *)&xbpe);
			XFlush(dpy);
		}
		#endif // PATCH_FLAG_ACTIVATION_CLICK
	}

	#if PATCH_TORCH
	if (torchwin) XRaiseWindow(dpy, torchwin);
	#endif // PATCH_TORCH
}

void
setfullscreen(Client *c, int fullscreen)
{
	#if PATCH_CROP_WINDOWS
	if (c->crop)
		c = c->crop;
	#endif // PATCH_CROP_WINDOWS
	#if PATCH_FLAG_NEVER_FULLSCREEN
	if (fullscreen && c->neverfullscreen)
		return;
	#endif // PATCH_FLAG_NEVER_FULLSCREEN
	//fprintf(stderr, "debug: setfullscreen(\"%s\", %i); c->isfullscreen:%i; game:%s\n", c->name, fullscreen, c->isfullscreen, game ? game->name : "<none>");
	#if PATCH_FLAG_FAKEFULLSCREEN
	XEvent ev;
	int savestate = 0, restorestate = 0, restorefakefullscreen = 0;

	if ((c->fakefullscreen == 0 && fullscreen && !c->isfullscreen) // normal fullscreen
			|| (c->fakefullscreen == 2 && fullscreen)) // fake fullscreen --> actual fullscreen
		savestate = 1; // go actual fullscreen
	else if ((c->fakefullscreen == 0 && !fullscreen && c->isfullscreen) // normal fullscreen exit
			|| (c->fakefullscreen >= 2 && !fullscreen)) // fullscreen exit --> fake fullscreen
		restorestate = 1; // go back into tiled

	/* If leaving fullscreen and the window was previously fake fullscreen (2), then restore
	 * that while staying in fullscreen. The exception to this is if we are in said state, but
	 * the client itself disables fullscreen (3) then we let the client go out of fullscreen
	 * while keeping fake fullscreen enabled (as otherwise there will be a mismatch between the
	 * client and the window manager's perception of the client's fullscreen state). */
	if (c->fakefullscreen == 2 && !fullscreen && c->isfullscreen) {
		restorefakefullscreen = 1;
		c->isfullscreen = 1;
		fullscreen = 1;
	}

	if (fullscreen != c->isfullscreen) { // only send property change if necessary
		c->isfullscreen = fullscreen;
		publishwindowstate(c);
	}

	#if PATCH_CLIENT_OPACITY
	if (c->isfullscreen)
		setopacity(c, 0);
	#endif // PATCH_CLIENT_OPACITY

	#if PATCH_FLAG_GAME
	if (c->isgame) {
		if (fullscreen)
			XSelectInput(dpy, c->win, FocusChangeMask|PropertyChangeMask|StructureNotifyMask|ResizeRedirectMask);
		else
			XSelectInput(dpy, c->win, EnterWindowMask|FocusChangeMask|PropertyChangeMask|StructureNotifyMask|ResizeRedirectMask);
	}
	#endif // PATCH_FLAG_GAME

	/* Some clients, e.g. firefox, will send a client message informing the window manager
	 * that it is going into fullscreen after receiving the above signal. This has the side
	 * effect of this function (setfullscreen) sometimes being called twice when toggling
	 * fullscreen on and off via the window manager as opposed to the application itself.
	 * To protect against obscure issues where the client settings are stored or restored
	 * when they are not supposed to we add an additional bit-lock on the old state so that
	 * settings can only be stored and restored in that precise order. */

	if (savestate && !(c->oldstate & (1 << 1))) {
		DEBUG("setfullscreen(\"%s\", %i): c->isfullscreen:%i c->dormant:%i\n", c->name, fullscreen, c->isfullscreen, c->dormant);
		c->oldbw = c->bw;
		c->oldstate = c->isfloating | (1 << 1);
		c->bw = 0;
		c->isfloating = 1;
		c->fakefullscreen = 0;

		// if this window isn't visible, don't change it yet
		//if ((c->tags & c->mon->tagset[c->mon->seltags]) > 0) {
		if (MINIMIZED(c) || ISVISIBLE(c))
			resizeclient(c, c->mon->mx, c->mon->my, c->mon->mw, c->mon->mh, c->isfloating);
		else {
			if (c->isfloating) {
				c->oldx = c->x;
				c->oldy = c->y;
				c->oldw = c->w;
				c->oldh = c->h;
			}
			c->x = c->mon->mx;
			c->y = c->mon->my;
			c->w = c->mon->mw;
			c->h = c->mon->mh;
		}

		if (c->dormant || running != 1)
			return;

		if (ISVISIBLE(c) && !MINIMIZED(c)) {
			#if PATCH_FLAG_GAME
			if (c->mon == selmon && selmon->sel != c)
				unfocus(selmon->sel, 0);
			c->mon->sel = c;
			raiseclient(c);
			if (c->isgame)
				grabbuttons(c, 1);	// re-grab in case of modified status;
			setfocus(c);
			#endif // PATCH_FLAG_GAME
			drawbar(c->mon, 1);
		}
	} else if (restorestate && (c->oldstate & (1 << 1))) {
		c->bw = c->oldbw;
		c->isfloating = c->oldstate = c->oldstate & 1;
		if (restorefakefullscreen || c->fakefullscreen == 3)
			c->fakefullscreen = 1;
		#if PATCH_FLAG_GAME
		if (c->isgame)
			destroybarrier();
		#endif // PATCH_FLAG_GAME
		/* The client may have been moved to another monitor whilst in fullscreen which if tiled
		 * we address by doing a full arrange of tiled clients. If the client is floating then the
		 * height and width may be larger than the monitor's window area, so we cap that by
		 * ensuring max / min values. */
		if (c->isfloating) {
			c->x = MAX(c->mon->wx, c->oldx);
			c->y = MAX(c->mon->wy, c->oldy);
			c->w = MIN(c->mon->wx + c->mon->ww - c->x - 2*c->bw, c->oldw);
			c->h = MIN(c->mon->wy + c->mon->wh - c->y - 2*c->bw, c->oldh);

			if (!ISVISIBLE(c))
				return;

			resizeclient(c, c->x, c->y, c->w, c->h, 0);
			if (running == 1)
				restack(c->mon);
		} else {
			if (!ISVISIBLE(c) || running != 1)
				return;
			arrange(c->mon);
		}
		#if PATCH_FLAG_GAME
		if (c->isgame)
			grabbuttons(c, selmon->sel == c ? 1 : 0);	// re-grab in case of modified status;
		#endif // PATCH_FLAG_GAME

	} else if (ISVISIBLE(c)) {
		if (c->isfullscreen
			#if PATCH_FLAG_FAKEFULLSCREEN
			&& c->fakefullscreen != 1
			#endif // PATCH_FLAG_FAKEFULLSCREEN
		) {
			resizeclient(c, c->mon->mx, c->mon->my, c->mon->mw, c->mon->mh, 0);
			DEBUG("Resized to screen size c:%s\n", c->name);
		}
		else {
			unsigned int bw = (solitary(c) ? c->bw : 0);
			resizeclient(c, c->x, c->y, c->w - 2*bw, c->h - 2*bw, 0);
		}
	}

	/* Exception: if the client was in actual fullscreen and we exit out to fake fullscreen
	 * mode, then the focus would sometimes drift to whichever window is under the mouse cursor
	 * at the time. To avoid this we ask X for all EnterNotify events and just ignore them.
	 */
	if (!c->isfullscreen)
		while (XCheckMaskEvent(dpy, EnterWindowMask, &ev));
	#else // NO PATCH_FLAG_FAKEFULLSCREEN

	#if PATCH_FLAG_GAME
	if (c->isgame) {
		if (fullscreen)
			XSelectInput(dpy, c->win, FocusChangeMask|PropertyChangeMask|StructureNotifyMask|ResizeRedirectMask);
		else
			XSelectInput(dpy, c->win, EnterWindowMask|FocusChangeMask|PropertyChangeMask|StructureNotifyMask|ResizeRedirectMask);
	}
	#endif // PATCH_FLAG_GAME

	if (fullscreen && !c->isfullscreen) {
		c->isfullscreen = 1;
		publishwindowstate(c);
		c->oldbw = c->bw;
		c->oldstate = c->isfloating;
		c->bw = 0;
		c->isfloating = 1;

		// if this window isn't visible, don't change it yet
		if (MINIMIZED(c) || ISVISIBLE(c))
			resizeclient(c, c->mon->mx, c->mon->my, c->mon->mw, c->mon->mh, c->isfloating);
		else {
			if (c->isfloating) {
				c->oldx = c->x;
				c->oldy = c->y;
				c->oldw = c->w;
				c->oldh = c->h;
			}
			c->x = c->mon->mx;
			c->y = c->mon->my;
			c->w = c->mon->mw;
			c->h = c->mon->mh;
		}

		if (c->dormant || running != 1)
			return;

		if (ISVISIBLE(c) && !MINIMIZED(c)) {
			#if PATCH_FLAG_GAME
			if (c->mon == selmon && selmon->sel != c)
				unfocus(selmon->sel, 0);
			c->mon->sel = c;
			raiseclient(c);
			if (c->isgame)
				grabbuttons(c, 1);	// re-grab in case of modified status;
			setfocus(c);
			#endif // PATCH_FLAG_GAME
			drawbar(c->mon, 1);
		}

	} else if (!fullscreen && c->isfullscreen) {
		c->isfullscreen = 0;
		publishwindowstate(c);
		c->bw = c->oldbw;
		c->isfloating = c->oldstate;

		#if PATCH_FLAG_GAME
		if (c->isgame)
			destroybarrier();
		#endif // PATCH_FLAG_GAME
		/* The client may have been moved to another monitor whilst in fullscreen which if tiled
		 * we address by doing a full arrange of tiled clients. If the client is floating then the
		 * height and width may be larger than the monitor's window area, so we cap that by
		 * ensuring max / min values. */
		if (c->isfloating) {
			c->x = MAX(c->mon->wx, c->oldx);
			c->y = MAX(c->mon->wy, c->oldy);
			c->w = MIN(c->mon->wx + c->mon->ww - c->x - 2*c->bw, c->oldw);
			c->h = MIN(c->mon->wy + c->mon->wh - c->y - 2*c->bw, c->oldh);

			if (!ISVISIBLE(c))
				return;

			resizeclient(c, c->x, c->y, c->w, c->h, 0);
			if (running == 1)
				restack(c->mon);
		} else {
			if (!ISVISIBLE(c) || running != 1)
				return;
			arrange(c->mon);
		}
		#if PATCH_FLAG_GAME
		if (c->isgame)
			grabbuttons(c, selmon->sel == c ? 1 : 0);	// re-grab in case of modified status;
		#endif // PATCH_FLAG_GAME

	} else {
		if (c->isfullscreen
			#if PATCH_FLAG_FAKEFULLSCREEN
			&& c->fakefullscreen != 1
			#endif // PATCH_FLAG_FAKEFULLSCREEN
		) {
			resizeclient(c, c->mon->mx, c->mon->my, c->mon->mw, c->mon->mh, 0);
			DEBUG("Resized to screen size c:%s\n", c->name);
		}
		else {
			unsigned int bw = (solitary(c) ? c->bw : 0);
			resizeclient(c, c->x, c->y, c->w - 2*bw, c->h - 2*bw, 0);
		}
	}
	#endif // PATCH_FLAG_FAKEFULLSCREEN

	XSync(dpy, False);
}

#if PATCH_FLAG_HIDDEN
void
sethidden(Client *c, int hidden, int rearrange)
{
	if (hidden)
		minimize(c);
	else
		unminimize(c);

	c->ishidden = hidden;
	publishwindowstate(c);
	if (rearrange && (!c->isfloating || !hidden))
		arrange(c->mon);
}
#endif // PATCH_FLAG_HIDDEN

void
setlayout(const Arg *arg)
{
	#if PATCH_FOCUS_FOLLOWS_MOUSE
	checkmouseovermonitor(selmon);
	#endif // PATCH_FOCUS_FOLLOWS_MOUSE
	setlayoutex(arg);
	#if PATCH_MOUSE_POINTER_WARPING
	if (selmon->sel)
		#if PATCH_MOUSE_POINTER_WARPING_SMOOTH
		warptoclient(selmon->sel, 1, 0);
		#else // NO PATCH_MOUSE_POINTER_WARPING_SMOOTH
		warptoclient(selmon->sel, 0);
		#endif // PATCH_MOUSE_POINTER_WARPING_SMOOTH
	#elif PATCH_FOCUS_FOLLOWS_MOUSE
	if (selmon->sel && !ismouseoverclient(selmon->sel) && !pointoverbar(selmon, -1, -1, 1)) {
		if (
			selmon->sel->isfullscreen
			#if PATCH_FLAG_FAKEFULLSCREEN
			&& selmon->sel->fakefullscreen != 1
			#endif // PATCH_FLAG_FAKEFULLSCREEN
			)
			XWarpPointer(dpy, None, root, 0, 0, 0, 0, selmon->mx + (selmon->mw / 2), selmon->my + (selmon->mh / 2));
		else
			XWarpPointer(dpy, None, root, 0, 0, 0, 0, selmon->sel->x + (selmon->sel->w / 2), selmon->sel->y + (selmon->sel->h / 2));
	}
	#endif // PATCH_MOUSE_POINTER_WARPING || PATCH_FOCUS_FOLLOWS_MOUSE
}

void
setlayoutex(const Arg *arg)
{
	#if PATCH_SHOW_DESKTOP
	if (selmon->showdesktop)
		return;
	#endif // PATCH_SHOW_DESKTOP

	const Layout *v = NULL;
	if (arg->v) {
		int i = layoutstringtoindex(arg->v);
		if (i >= 0 && i < LENGTH(layouts))
			v = &layouts[i];
	}
	if (v != selmon->lt[selmon->sellt])
		#if PATCH_PERTAG
		selmon->sellt = selmon->pertag->sellts[selmon->pertag->curtag] ^= 1;
		#else // NO PATCH_PERTAG
		selmon->sellt ^= 1;
		#endif // PATCH_PERTAG
	if (v)
		setlayoutreplace(&((Arg){ .v = v }));
	else
		setlayoutreplace(&((Arg){0}));

}

void
setlayoutmouse(const Arg *arg)
{
	#if PATCH_FOCUS_FOLLOWS_MOUSE
	checkmouseovermonitor(selmon);
	#endif // PATCH_FOCUS_FOLLOWS_MOUSE
	setlayoutex(arg);
}

void
setlayoutreplace(const Arg *arg)
{
	#if PATCH_FOCUS_FOLLOWS_MOUSE
	checkmouseovermonitor(selmon);
	#endif // PATCH_FOCUS_FOLLOWS_MOUSE
	if (arg && arg->v)
		#if PATCH_PERTAG
		selmon->lt[selmon->sellt] = selmon->pertag->ltidxs[selmon->pertag->curtag][selmon->sellt] = (Layout *)arg->v;
		#else // NO PATCH_PERTAG
		selmon->lt[selmon->sellt] = (Layout *)arg->v;
		#endif // PATCH_PERTAG
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstringop-truncation"
	strncpy(selmon->ltsymbol, selmon->lt[selmon->sellt]->symbol, sizeof selmon->ltsymbol);
#pragma GCC diagnostic pop
	if (selmon->sel)
		arrange(selmon);
	else
		drawbar(selmon, 0);
}

#if PATCH_EWMH_TAGS
void
setnumdesktops(void)
{
	long data[] = { LENGTH(tags) };
	XChangeProperty(dpy, root, netatom[NetNumberOfDesktops], XA_CARDINAL, 32, PropModeReplace, (unsigned char *)data, 1);
}
#endif // PATCH_EWMH_TAGS


#if PATCH_CFACTS
void
setcfact(const Arg *arg) {
	#if PATCH_FOCUS_FOLLOWS_MOUSE
	checkmouseoverclient();
	#endif // PATCH_FOCUS_FOLLOWS_MOUSE
	float f;
	Client *c;

	#if PATCH_SHOW_DESKTOP
	if (selmon->showdesktop)
		return;
	#endif // PATCH_SHOW_DESKTOP

	c = selmon->sel;

	if(!arg || !c || !selmon->lt[selmon->sellt]->arrange)
		return;
	f = arg->f + c->cfact;
	if(arg->f == 0.0)
		f = 1.0;
	else if(f < 0.25 || f > 4.0)
		return;
	c->cfact = f;
	#if PATCH_PERSISTENT_METADATA
	setclienttagprop(c);
	#endif // PATCH_PERSISTENT_METADATA
	arrange(selmon);
	#if PATCH_FOCUS_BORDER || PATCH_FOCUS_PIXEL
	if (focuswin)
		focus(NULL, 0);
	#endif // PATCH_FOCUS_BORDER || PATCH_FOCUS_PIXEL
}
#endif // PATCH_CFACTS

// arg > 1.0 will set mfact absolutely;
// otherwise, the value of mfact will be snapped to the
// nearest value divisible by the absolute value of arg
void
setmfact(const Arg *arg)
{
	#if PATCH_FOCUS_FOLLOWS_MOUSE
	checkmouseovermonitor(selmon);
	#endif // PATCH_FOCUS_FOLLOWS_MOUSE
	float f;
	int mfi, argi, ri;

	#if PATCH_SHOW_DESKTOP
	if (selmon->showdesktop)
		return;
	#endif // PATCH_SHOW_DESKTOP

	if (!arg || !selmon->lt[selmon->sellt]->arrange)
		return;
	if (arg->f == 0.0)
		f = selmon->mfact_def;
	else if (arg->f >= 1.0)
		f = arg->f - 1.0;
	else {
		f = arg->f + selmon->mfact;

		mfi = (int)roundf(f * 1000000.0f);
		argi = (int)roundf(arg->f * 1000000.0f);
		ri = mfi % argi;
		if (ri) {
			if (argi > 0)
				mfi -= ri;
			else
				mfi += abs(argi) - ri;
		}
		f = (float)mfi/1000000.0f;
	}
	if (f < 0.05 || f > 0.95)
		return;
	#if PATCH_PERTAG
	selmon->mfact = selmon->pertag->mfacts[selmon->pertag->curtag] = f;
	#else // NO PATCH_PERTAG
	selmon->mfact = f;
	#endif // PATCH_PERTAG
	arrange(selmon);
	#if PATCH_FOCUS_BORDER || PATCH_FOCUS_PIXEL
	if (focuswin)
		focus(NULL, 0);
	#endif // PATCH_FOCUS_BORDER || PATCH_FOCUS_PIXEL
}

#if PATCH_FLAG_STICKY
void
setsticky(Client *c, int sticky)
{
	if ((sticky && c->issticky) || (!sticky && !c->issticky))
		return;

	c->issticky = sticky;
	publishwindowstate(c);
	if (!sticky)
		arrange(c->mon);
}
#endif // PATCH_FLAG_STICKY

int
setup(void)
{
	int i;
	XSetWindowAttributes wa;
	#if PATCH_FOCUS_BORDER || PATCH_FOCUS_PIXEL
	XSetWindowAttributes fwa;
	#endif // PATCH_FOCUS_BORDER || PATCH_FOCUS_PIXEL
	#if PATCH_FONT_GROUPS
	int j, n = -1, fg_minbh = 0;
	#if PATCH_CLIENT_INDICATORS
	int tagbar_bh;
	#endif // PATCH_CLIENT_INDICATORS
	cJSON *el, *nom;
	Fnt *f;
	#endif // PATCH_FONT_GROUPS

	Atom utf8string;
	struct sigaction sa;

	/* do not transform children into zombies when they terminate */
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_NOCLDSTOP | SA_NOCLDWAIT | SA_RESTART;
	sa.sa_handler = SIG_IGN;
	sigaction(SIGCHLD, &sa, NULL);

	/* clean up any zombies (inherited from .xinitrc etc) immediately */
	while (waitpid(-1, NULL, WNOHANG) > 0);

	#if PATCH_HANDLE_SIGNALS
	/* add signal handlers */
	signal(SIGHUP, sighup);
	signal(SIGTERM, sigterm);
	signal(SIGRELOAD_RESCAN, sigreload);
	signal(SIGRELOAD_RULES, sigreloadrules);
	#endif // PATCH_HANDLE_SIGNALS

	/* init screen */
	screen = DefaultScreen(dpy);
	sw = DisplayWidth(dpy, screen);
	sh = DisplayHeight(dpy, screen);
	root = RootWindow(dpy, screen);
	#if PATCH_ALPHA_CHANNEL
	if (has_compositor(dpy, screen))
		xinitvisual();
	drw = drw_create(dpy, screen, root, sw, sh, useargb, visual, depth, cmap);
	#else // NO PATCH_ALPHA_CHANNEL
	drw = drw_create(dpy, screen, root, sw, sh);
	#endif // PATCH_ALPHA_CHANNEL

	if (!fonts_json || !(drw->fonts = drw_fontset_create_json(drw, fonts_json)))
		if (!(drw->fonts = drw_fontset_create(drw, fonts, LENGTH(fonts)))) {
			logdatetime(stderr);
			fputs("no fonts could be loaded.\n", stderr);
			return 0;
		}
	lrpad = LRPAD(drw->fonts);
	minbh = drw->fonts->h + 2;
	bh = minbh
		#if PATCH_CLIENT_INDICATORS
		+ (client_ind ? client_ind_size : 0)
		#endif // PATCH_CLIENT_INDICATORS
	;
	#if PATCH_CLIENT_INDICATORS
	client_ind_offset = client_ind ? ((client_ind_size + 1) / 2) : 0;
	#endif // PATCH_CLIENT_INDICATORS
	#if PATCH_FONT_GROUPS
	if (fontgroups_json && drw_populate_fontgroups(drw, fontgroups_json) && barelement_fontgroups_json) {

		#if PATCH_CLIENT_INDICATORS
		tagbar_bh = minbh;
		#endif // PATCH_CLIENT_INDICATORS

		if (cJSON_IsArray(barelement_fontgroups_json))
			n = cJSON_GetArraySize(barelement_fontgroups_json);
		else
			el = barelement_fontgroups_json;

		// iterate through bar element font groups;
		for (i = 0; i < abs(n); i++) {
			if (n > 0)
				el = cJSON_GetArrayItem(barelement_fontgroups_json, i);
			if (!(nom = cJSON_GetObjectItemCaseSensitive(el, "bar-element")) || !cJSON_IsString(nom))
				continue;

			// check if named bar-element is current;
			for (j = LENGTH(BarElementTypes); j > 0; j--)
				if (BarElementTypes[j - 1].name &&
					strcmp(BarElementTypes[j - 1].name, nom->valuestring) == 0
					)
					break;
			if (!j)
				continue;

			if ((el = cJSON_GetObjectItemCaseSensitive(el, "font-group")) && cJSON_IsString(el) &&
				(f = drw_get_fontgroup_fonts(drw, el->valuestring))
			) {
				fg_minbh = f->h + 2;

				#if PATCH_CLIENT_INDICATORS
				if (BarElementTypes[j - 1].type == TagBar && (fg_minbh > tagbar_bh))
					tagbar_bh = fg_minbh;
				#endif // PATCH_CLIENT_INDICATORS

				if (minbh < fg_minbh)
					minbh = fg_minbh;
			}
		}
		if (bh < minbh)
			bh = minbh;
		#if PATCH_CLIENT_INDICATORS
		if (tagbar_bh + client_ind_size > bh)
			bh = tagbar_bh + client_ind_size;
		else if (tagbar_bh + (2 * client_ind_size) <= bh)
			client_ind_offset = 0;
		#endif // PATCH_CLIENT_INDICATORS
	}
	#endif // PATCH_FONT_GROUPS
	updategeom();

	#if PATCH_FLAG_GAME
	int fixes_opcode, fixes_event_base, fixes_error_base;
	if (XQueryExtension(dpy, "XFIXES", &fixes_opcode, &fixes_event_base, &fixes_error_base))
		xfixes_support = 1;
	else
		fprintf(stderr, "No support for XFixes PointerBarrier.\n");
	#endif // PATCH_FLAG_GAME

	/* init atoms */
	utf8string = XInternAtom(dpy, "UTF8_STRING", False);
	wmatom[WMProtocols] = XInternAtom(dpy, "WM_PROTOCOLS", False);
	wmatom[WMDelete] = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
	wmatom[WMState] = XInternAtom(dpy, "WM_STATE", False);
	wmatom[WMTakeFocus] = XInternAtom(dpy, "WM_TAKE_FOCUS", False);
	wmatom[WMWindowRole] = XInternAtom(dpy, "WM_WINDOW_ROLE", False);
	netatom[NetActiveWindow] = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
	netatom[NetSupported] = XInternAtom(dpy, "_NET_SUPPORTED", False);
	netatom[NetSystemTray] = XInternAtom(dpy, "_NET_SYSTEM_TRAY_S0", False);
	netatom[NetSystemTrayOP] = XInternAtom(dpy, "_NET_SYSTEM_TRAY_OPCODE", False);
	netatom[NetSystemTrayOrientation] = XInternAtom(dpy, "_NET_SYSTEM_TRAY_ORIENTATION", False);
	netatom[NetSystemTrayOrientationHorz] = XInternAtom(dpy, "_NET_SYSTEM_TRAY_ORIENTATION_HORZ", False);
	netatom[NetSystemTrayVisual] = XInternAtom(dpy, "_NET_SYSTEM_TRAY_VISUAL", False);
	netatom[NetWMName] = XInternAtom(dpy, "_NET_WM_NAME", False);
	netatom[NetWMAttention] = XInternAtom(dpy, "_NET_WM_STATE_DEMANDS_ATTENTION", False);
	#if PATCH_WINDOW_ICONS
	netatom[NetWMIcon] = XInternAtom(dpy, "_NET_WM_ICON", False);
	#endif // PATCH_WINDOW_ICONS
	netatom[NetWMState] = XInternAtom(dpy, "_NET_WM_STATE", False);
	netatom[NetWMCheck] = XInternAtom(dpy, "_NET_SUPPORTING_WM_CHECK", False);
	netatom[NetWMFullscreen] = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);
	#if PATCH_FLAG_STICKY
    netatom[NetWMSticky] = XInternAtom(dpy, "_NET_WM_STATE_STICKY", False);
	#endif // PATCH_FLAG_STICKY
	#if PATCH_FLAG_ALWAYSONTOP
	netatom[NetWMStaysOnTop] = XInternAtom(dpy, "_NET_WM_STATE_STAYS_ON_TOP", False);
	#endif // PATCH_FLAG_ALWAYSONTOP
	#if PATCH_FLAG_HIDDEN
	netatom[NetWMHidden] = XInternAtom(dpy, "_NET_WM_STATE_HIDDEN", False);
	#endif // PATCH_FLAG_HIDDEN
	#if PATCH_MODAL_SUPPORT
	netatom[NetWMModal] = XInternAtom(dpy, "_NET_WM_STATE_MODAL", False);
	#endif // PATCH_MODAL_SUPPORT
	netatom[NetWMWindowType] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
	netatom[NetWMWindowTypeDialog] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DIALOG", False);
	netatom[NetWMWindowTypeSplash] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_SPLASH", False);
	netatom[NetWMWindowTypeDock] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DOCK", False);
	#if PATCH_ALTTAB
	netatom[NetWMWindowTypeMenu] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_MENU", False);
	netatom[NetWMWindowTypePopupMenu] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_POPUP_MENU", False);
	#endif // PATCH_ALTTAB
	#if PATCH_EWMH_TAGS
	netatom[NetDesktopViewport] = XInternAtom(dpy, "_NET_DESKTOP_VIEWPORT", False);
	netatom[NetNumberOfDesktops] = XInternAtom(dpy, "_NET_NUMBER_OF_DESKTOPS", False);
	netatom[NetCurrentDesktop] = XInternAtom(dpy, "_NET_CURRENT_DESKTOP", False);
	netatom[NetDesktopNames] = XInternAtom(dpy, "_NET_DESKTOP_NAMES", False);
	#endif // PATCH_EWMH_TAGS
	#if PATCH_SHOW_DESKTOP
	netatom[NetWMWindowTypeDesktop] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DESKTOP", False);
	#endif // PATCH_SHOW_DESKTOP
	#if PATCH_LOG_DIAGNOSTICS
	netatom[NetWMAbove] = XInternAtom(dpy, "_NET_WM_STATE_ABOVE", False);
	netatom[NetWMBelow] = XInternAtom(dpy, "_NET_WM_STATE_BELOW", False);
	netatom[NetWMMaximizedH] = XInternAtom(dpy, "_NET_WM_STATE_MAXIMIZED_HORZ", False);
	netatom[NetWMMaximizedV] = XInternAtom(dpy, "_NET_WM_STATE_MAXIMIZED_VERT", False);
	netatom[NetWMShaded] = XInternAtom(dpy, "_NET_WM_STATE_SHADED", False);
	netatom[NetWMSkipPager] = XInternAtom(dpy, "_NET_WM_STATE_SKIP_PAGER", False);
	netatom[NetWMSkipTaskbar] = XInternAtom(dpy, "_NET_WM_STATE_SKIP_TASKBAR", False);
	#endif // PATCH_LOG_DIAGNOSTICS
	netatom[NetClientList] = XInternAtom(dpy, "_NET_CLIENT_LIST", False);
	netatom[NetClientInfo] = XInternAtom(dpy, "_NET_CLIENT_INFO", False);
	xatom[Manager] = XInternAtom(dpy, "MANAGER", False);
	xatom[Xembed] = XInternAtom(dpy, "_XEMBED", False);
	xatom[XembedInfo] = XInternAtom(dpy, "_XEMBED_INFO", False);
	#if PATCH_CLIENT_OPACITY
	netatom[NetWMWindowsOpacity] = XInternAtom(dpy, "_NET_WM_WINDOW_OPACITY", False);
	#endif // PATCH_CLIENT_OPACITY
	/* init cursors */
	cursor[CurNormal] = drw_cur_create(drw, XC_left_ptr);
	cursor[CurResize] = drw_cur_create(drw, XC_sizing);
	cursor[CurResizeH] = drw_cur_create(drw, XC_sb_h_double_arrow);
	cursor[CurResizeV] = drw_cur_create(drw, XC_sb_v_double_arrow);
	cursor[CurResizeBR] = drw_cur_create(drw, XC_bottom_right_corner);
	cursor[CurResizeBL] = drw_cur_create(drw, XC_bottom_left_corner);
	cursor[CurResizeTR] = drw_cur_create(drw, XC_top_right_corner);
	cursor[CurResizeTL] = drw_cur_create(drw, XC_top_left_corner);
	cursor[CurMove] = drw_cur_create(drw, XC_fleur);
	cursor[CurDragFact] = drw_cur_create(drw, XC_rightbutton);
	cursor[CurScroll] = drw_cur_create(drw, XC_double_arrow);
	#if PATCH_ALTTAB
	cursor[CurBusy] = drw_cur_create(drw, XC_watch);
	#endif // PATCH_ALTTAB
	#if PATCH_TORCH
	cursor[CurInvisible] = drw_cur_create(drw, -1);
	#endif // PATCH_TORCH

	#if PATCH_COLOUR_BAR
	setdefaultcolours(colours[SchemeTagBar], colours[SchemeNorm]);
	#if PATCH_FLAG_HIDDEN
	setdefaultcolours(colours[SchemeTagBarHide], colours[SchemeHide]);
	#endif // PATCH_FLAG_HIDDEN
	setdefaultcolours(colours[SchemeTagBarSel], colours[SchemeSel]);
	setdefaultcolours(colours[SchemeLayout], colours[SchemeNorm]);
	setdefaultcolours(colours[SchemeTitle], colours[SchemeNorm]);
	setdefaultcolours(colours[SchemeTitleSel], colours[SchemeSel]);
	setdefaultcolours(colours[SchemeStatus], colours[SchemeNorm]);
	#endif // PATCH_COLOUR_BAR
	#if PATCH_RAINBOW_TAGS
	setdefaultcolours(colours[SchemeTag1], colours[
		#if PATCH_COLOUR_BAR
		SchemeTagBarSel
		#else // NO PATCH_COLOUR_BAR
		SchemeSel
		#endif // PATCH_COLOUR_BAR
	]);
	setdefaultcolours(colours[SchemeTag2], colours[
		#if PATCH_COLOUR_BAR
		SchemeTagBarSel
		#else // NO PATCH_COLOUR_BAR
		SchemeSel
		#endif // PATCH_COLOUR_BAR
	]);
	setdefaultcolours(colours[SchemeTag3], colours[
		#if PATCH_COLOUR_BAR
		SchemeTagBarSel
		#else // NO PATCH_COLOUR_BAR
		SchemeSel
		#endif // PATCH_COLOUR_BAR
	]);
	setdefaultcolours(colours[SchemeTag4], colours[
		#if PATCH_COLOUR_BAR
		SchemeTagBarSel
		#else // NO PATCH_COLOUR_BAR
		SchemeSel
		#endif // PATCH_COLOUR_BAR
	]);
	setdefaultcolours(colours[SchemeTag5], colours[
		#if PATCH_COLOUR_BAR
		SchemeTagBarSel
		#else // NO PATCH_COLOUR_BAR
		SchemeSel
		#endif // PATCH_COLOUR_BAR
	]);
	setdefaultcolours(colours[SchemeTag6], colours[
		#if PATCH_COLOUR_BAR
		SchemeTagBarSel
		#else // NO PATCH_COLOUR_BAR
		SchemeSel
		#endif // PATCH_COLOUR_BAR
	]);
	setdefaultcolours(colours[SchemeTag7], colours[
		#if PATCH_COLOUR_BAR
		SchemeTagBarSel
		#else // NO PATCH_COLOUR_BAR
		SchemeSel
		#endif // PATCH_COLOUR_BAR
	]);
	setdefaultcolours(colours[SchemeTag8], colours[
		#if PATCH_COLOUR_BAR
		SchemeTagBarSel
		#else // NO PATCH_COLOUR_BAR
		SchemeSel
		#endif // PATCH_COLOUR_BAR
	]);
	setdefaultcolours(colours[SchemeTag9], colours[
		#if PATCH_COLOUR_BAR
		SchemeTagBarSel
		#else // NO PATCH_COLOUR_BAR
		SchemeSel
		#endif // PATCH_COLOUR_BAR
	]);
	#endif // PATCH_RAINBOW_TAGS
	#if PATCH_ALTTAB
	setdefaultcolours(colours[SchemeTabNorm], colours[SchemeNorm]);
	setdefaultcolours(colours[SchemeTabSel], colours[SchemeSel]);
	setdefaultcolours(colours[SchemeTabUrg], colours[SchemeUrg]);
	#if PATCH_FLAG_HIDDEN
	setdefaultcolours(colours[SchemeTabHide], colours[SchemeTabNorm]);
	#endif // PATCH_FLAG_HIDDEN
	#endif // PATCH_ALTTAB

	/* init appearance */
	scheme = ecalloc(LENGTH(colours), sizeof(Clr *));
	for (i = 0; i < LENGTH(colours); i++)
		scheme[i] = drw_scm_create(drw, colours[i], 3);

	#if PATCH_CUSTOM_TAG_ICONS
	dummyc = ecalloc(1, sizeof(Client));
	#endif // PATCH_CUSTOM_TAG_ICONS

	#if PATCH_SYSTRAY
	/* init system tray */
	if (showsystray)
		updatesystray(0);
	#endif // PATCH_SYSTRAY
	/* init bars */
	updatebars();
	updatestatus();
	/* supporting window for NetWMCheck */
	wmcheckwin = XCreateSimpleWindow(dpy, root, 0, 0, 1, 1, 0, 0, 0);
	XChangeProperty(dpy, wmcheckwin, netatom[NetWMCheck], XA_WINDOW, 32,
		PropModeReplace, (unsigned char *) &wmcheckwin, 1);
	XChangeProperty(dpy, wmcheckwin, netatom[NetWMName], utf8string, 8,
		PropModeReplace, (unsigned char *) "dwm", 3);
	XChangeProperty(dpy, root, netatom[NetWMCheck], XA_WINDOW, 32,
		PropModeReplace, (unsigned char *) &wmcheckwin, 1);

	#if PATCH_FOCUS_BORDER || PATCH_FOCUS_PIXEL
	/* focus window */
	if (fh) {
		fwa.override_redirect = 1;
		fwa.background_pixel = scheme[SchemeSel][ColBorder].pixel;
		#if PATCH_FOCUS_BORDER
		fwa.event_mask = BUTTONMASK;
		focuswin = XCreateWindow(dpy, root, -1, -1, 1, 1, 0,
			DefaultDepth(dpy, screen),
			InputOutput, DefaultVisual(dpy, screen),
			CWOverrideRedirect|CWBackPixel|CWEventMask, &fwa
		);
		#elif PATCH_FOCUS_PIXEL
		// make the background slightly transparent;
		fwa.background_pixel = (fwa.background_pixel &~ 0xFF000000L) | 0xA0000000L;
		fwa.border_pixel = 0x40000000L;
		fwa.event_mask = PointerMotionMask;
		#if PATCH_ALPHA_CHANNEL
		if (useargb) {
			fwa.colormap = cmap;
			focuswin = XCreateWindow(
				dpy, root, -2, -2, 1, 1, 1, depth, InputOutput, visual,
				CWOverrideRedirect|CWBackPixel|CWBorderPixel|CWColormap|CWEventMask, &fwa
			);
		}
		else
		#endif // PATCH_ALPHA_CHANNEL
		focuswin = XCreateWindow(dpy, root, -2, -2, 1, 1, 1,
			DefaultDepth(dpy, screen),
			InputOutput, DefaultVisual(dpy, screen),
			CWOverrideRedirect|CWBackPixel|CWBorderPixel|CWEventMask, &fwa
		);
		fh += 2;	// account for border of 1 px;
		#endif // PATCH_FOCUS_BORDER || PATCH_FOCUS_PIXEL
		XClassHint ch = {"dwm", "dwm-hud"};
		XSetClassHint(dpy, focuswin, &ch);
		XMapWindow(dpy, focuswin);
	}
	#endif // PATCH_FOCUS_BORDER || PATCH_FOCUS_PIXEL

	/* EWMH support per view */
	XChangeProperty(dpy, root, netatom[NetSupported], XA_ATOM, 32,
		PropModeReplace, (unsigned char *) netatom, NetLast);
	#if PATCH_EWMH_TAGS
	setnumdesktops();
	setcurrentdesktop();
	setdesktopnames();
	setviewport();
	#endif // PATCH_EWMH_TAGS
	XDeleteProperty(dpy, root, netatom[NetClientList]);
	XDeleteProperty(dpy, root, netatom[NetClientInfo]);
	/* select events */
	wa.cursor = cursor[CurNormal]->cursor;
	wa.event_mask = SubstructureRedirectMask|SubstructureNotifyMask
		|ButtonPressMask|PointerMotionMask|EnterWindowMask
		|LeaveWindowMask|StructureNotifyMask|PropertyChangeMask
		;
	XChangeWindowAttributes(dpy, root, CWEventMask|CWCursor, &wa);
	XSelectInput(dpy, root, wa.event_mask);
	grabkeys();
	focus(NULL, 0);

	populate_charcode_map();

	#if PATCH_IPC
	return (setupepoll());
	#endif // PATCH_IPC
	return 1;
}

#if PATCH_IPC
int
setupepoll(void)
{
	int result = 0;
	epoll_fd = epoll_create1(0);
	dpy_fd = ConnectionNumber(dpy);
	struct epoll_event dpy_event;

	// Initialize struct to 0
	memset(&dpy_event, 0, sizeof(dpy_event));

	DEBUG("Display socket is fd %d\n", dpy_fd);

	if (epoll_fd == -1) {
		logdatetime(stderr);
		fputs("dwm: Failed to create epoll file descriptor", stderr);
		return result;
	}

	dpy_event.events = EPOLLIN;
	dpy_event.data.fd = dpy_fd;
	if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, dpy_fd, &dpy_event)) {
		logdatetime(stderr);
		fputs("dwm: Failed to add display file descriptor to epoll", stderr);
		close(epoll_fd);
		return result;
	}

	if (ipcsockpath)
		free(ipcsockpath);
	ipcsockpath = expandenv(socketpath);
	if (!ipcsockpath || (strchr(socketpath, '$') && strlen(ipcsockpath) == strlen(socketpath))) {
		logdatetime(stderr);
		if (ipcsockpath) {
			fprintf(stderr, "dwm: Failed to expand socket path: \"%s\"; using fallback \"%s\".\n", socketpath, socketpath_fallback);
			free(ipcsockpath);
		}
		else
			fprintf(stderr, "dwm: Unable to evaluate socket path: \"%s\"; using fallback \"%s\".\n", socketpath, socketpath_fallback);
		ipcsockpath = strdup(socketpath_fallback);
	}
	if (ipc_init(ipcsockpath, epoll_fd, ipccommands, LENGTH(ipccommands)) < 0) {
		logdatetime(stderr);
		fputs("dwm: Failed to initialize IPC\n", stderr);
	}
	else
		result = 1;
	return result;

}
#endif // PATCH_IPC

#if PATCH_MOUSE_POINTER_HIDING
void
setup_sync_counters(void)
{
	int i;
	XSyncSystemCounter *counters;
	int error;
	int major, minor, ncounters;

	if (XSyncQueryExtension(dpy, &timer_sync_event, &error) != True) {
		logdatetime(stderr);
		fputs("dwm: no X sync extension available;", stderr);
		#if PATCH_MOUSE_POINTER_HIDING
		if (cursortimeout)
			fputs(" no cursor autohiding available;", stderr);
		#endif // PATCH_MOUSE_POINTER_HIDING
		fputs("\n", stderr);
	}
	else {
		XSyncInitialize(dpy, &major, &minor);

		counters = XSyncListSystemCounters(dpy, &ncounters);
		for (i = 0; i < ncounters; i++) {
			#if PATCH_MOUSE_POINTER_HIDING
			if (!strcmp(counters[i].name, "IDLETIME"))
				counter_idletime = counters[i].counter;
			#endif // PATCH_MOUSE_POINTER_HIDING
		}
		XSyncFreeSystemCounterList(counters);

		if (!counter_idletime) {
			logdatetime(stderr);
			fputs("dwm: no X idle counter;", stderr);
			#if PATCH_MOUSE_POINTER_HIDING
			if (cursortimeout)
				fputs(" no cursor autohiding available;", stderr);
			#endif // PATCH_MOUSE_POINTER_HIDING
			fputs("\n", stderr);
		}
	}
}
#endif // PATCH_MOUSE_POINTER_HIDING

void
seturgent(Client *c, int urg)
{
	XWMHints *wmh;

	c->isurgent = urgency ? urg : 0;
	if (!(wmh = XGetWMHints(dpy, c->win)))
		return;
	wmh->flags = urg ? (wmh->flags | XUrgencyHint) : (wmh->flags & ~XUrgencyHint);
	XSetWMHints(dpy, c->win, wmh);
	XFree(wmh);
}

#if PATCH_EWMH_TAGS
void
setviewport(void)
{
	long data[] = { 0, 0 };
	XChangeProperty(dpy, root, netatom[NetDesktopViewport], XA_CARDINAL, 32, PropModeReplace, (unsigned char *)data, 2);
}
#endif // PATCH_EWMH_TAGS

#if PATCH_MOUSE_POINTER_HIDING
void
showcursor(void)
{
	if (cursortimeout) {
		set_idle_alarm();
	}

	if (!cursorhiding)
		return;

	if (cursormove_x >= 0 && cursormove_y >= 0) {
		XWarpPointer(dpy, None, root, 0, 0, 0, 0, cursormove_x, cursormove_y);
		cursormove_x = -1;
		cursormove_y = -1;
	}
#if 0
	if (cursormove && cursormove_x != -1 && cursormove_y != -1)
		XWarpPointer(dpy, None, root, 0, 0, 0, 0,
		    cursormove_x, cursormove_y);
#endif
	XFixesShowCursor(dpy, root);
	cursorhiding = 0;
}
#endif // PATCH_MOUSE_POINTER_HIDING

void
showhide(Client *c, int client_only)
{
	if (!c)
		return;
	#if PATCH_FLAG_IGNORED
	if (c->isignored)
		return;
	#endif // PATCH_FLAG_IGNORED
	if ((nonstop & 1) || ISVISIBLE(c)) {
		/* show clients top down */
		#if PATCH_FLAG_GAME || PATCH_FLAG_HIDDEN || PATCH_FLAG_PANEL
		if (c->autohide
			#if PATCH_FLAG_HIDDEN
			&& !c->ishidden
			#endif // PATCH_FLAG_HIDDEN
			)
			unminimize(c);
		#endif // PATCH_FLAG_GAME || PATCH_FLAG_HIDDEN || PATCH_FLAG_PANEL

		#if PATCH_SHOW_DESKTOP
		if (c->isdesktop) {
			XLowerWindow(dpy, c->win);
			c->x = c->mon->wx;
			c->y = c->mon->wy;
			c->w = c->mon->ww;
			c->h = c->mon->wh;
		}
		#endif // PATCH_SHOW_DESKTOP

		#if PATCH_CLIENT_OPACITY
		opacity(c, (
			c->mon->sel == c
			#if PATCH_ALTTAB
			#if PATCH_ALTTAB_HIGHLIGHT
			&& (!altTabMon || !altTabMon->isAlt || !altTabMon->highlight)
			#endif // PATCH_ALTTAB_HIGHLIGHT
			#endif // PATCH_ALTTAB
		));
		#endif // PATCH_CLIENT_OPACITY

		if (
			(!c->mon->lt[c->mon->sellt]->arrange ||	(
				c->isfloating
				#if PATCH_SHOW_DESKTOP
				&& !c->isdesktop
				#endif // PATCH_SHOW_DESKTOP
			)) && (
				!c->isfullscreen
				#if PATCH_FLAG_FAKEFULLSCREEN
				|| c->fakefullscreen == 1
				#endif // PATCH_FLAG_FAKEFULLSCREEN
			))
			resizeclient(c, c->x, c->y, c->w, c->h, 0);

		#if PATCH_FLAG_GAME
		else if (c->isgame && c->isfullscreen) {
			XWindowAttributes wa;
			if (!XGetWindowAttributes(dpy, c->win, &wa) || wa.x + wa.width < 0)
				XMoveResizeWindow(dpy, c->win, c->mon->mx, c->mon->my, c->mon->mw, c->mon->mh);
		}
		else
		#endif // PATCH_FLAG_GAME
		XMoveResizeWindow(dpy, c->win, c->x, c->y, c->w, c->h);

		if (c->snext != c && !client_only)
			showhide(c->snext, 0);
	} else {
		/* hide clients bottom up */
		if (c->snext != c && !client_only)
			showhide(c->snext, 0);
		#if PATCH_FLAG_GAME
		if (c->isgame && c->isfullscreen)
			XLowerWindow(dpy, c->win);
		else
		#endif // PATCH_FLAG_GAME
		XMoveWindow(dpy, c->win, WIDTH(c) * -2, c->y);
		#if PATCH_FLAG_GAME || PATCH_FLAG_HIDDEN || PATCH_FLAG_PANEL
		if (c->autohide
			#if PATCH_FLAG_HIDDEN
			&& !c->ishidden
			#endif // PATCH_FLAG_HIDDEN
			)
			minimize(c);
		#endif // PATCH_FLAG_GAME || PATCH_FLAG_HIDDEN || PATCH_FLAG_PANEL
	}
}

#if PATCH_HANDLE_SIGNALS
#if PATCH_MODAL_SUPPORT
Client *
getfirstmodal(Monitor *m)
{
	for (Client *c = m->stack; c; c = c->snext)
		if (c->ismodal)
			return c;
	return NULL;
}
#endif // PATCH_MODAL_SUPPORT
#endif // PATCH_HANDLE_SIGNALS

#if PATCH_HANDLE_SIGNALS
void
sighup(int unused)
{
	logdatetime(stderr);
	if (closing) {
		closing = 0;
		fputs("dwm: received second SIGHUP, cancelling.\n", stderr);
		return;
	}
	else {
		closing = 1;
		fputs("dwm: received SIGHUP\n", stderr);
	}

	Monitor *m;
	Client *c = NULL;
	int cnt = 0;

	// reset sigtermcount;
	for (m = mons; m; m = m->next)
		for (c = m->clients; c; c = c->next)
			c->sigtermcount = 0;

	if (procparents) {
		cnt = cJSON_GetArraySize(procparents);
		for (int i = 0; i < cnt; ++i) {
			cJSON *pp = cJSON_GetArrayItem(procparents, i);
			if (!pp)
				continue;
			cJSON *pp_count = cJSON_GetObjectItemCaseSensitive(pp, "sigtermcount");
			if (pp_count)
				cJSON_SetIntValue(pp_count, 0L);
		}
	}
	cnt = 0;

	#if PATCH_MODAL_SUPPORT
	int modalchild = 0;
	int focus = 1;

	for (m = selmon; m; focus = 0) {
		if ((c = getfirstmodal(m)))
			activateclient(c, focus);
		if (m->next == selmon)
			break;
		if (!m->next && selmon != mons)
			m = mons;
		else
			m = m->next;
	}
	if (c)
		return;
	#endif // PATCH_MODAL_SUPPORT

	for (m = mons; m; m = m->next)
		for (c = m->stack; c; c = c->snext)
		{
			if (c->dormant
				#if PATCH_TERMINAL_SWALLOWING
				|| c->isterminal
				#endif // PATCH_TERMINAL_SWALLOWING
				#if PATCH_SHOW_DESKTOP
				|| c->isdesktop || c->ondesktop
				#endif // PATCH_SHOW_DESKTOP
				#if PATCH_FLAG_PANEL
				|| c->ispanel
				#endif // PATCH_FLAG_PANEL
				#if PATCH_FLAG_IGNORED
				|| c->isignored
				#endif // PATCH_FLAG_IGNORED
			) continue;
			++cnt;
			#if PATCH_MODAL_SUPPORT
			if (!c->ismodal)
			{
				modalchild = 0;
				if (!c->ultparent || c->ultparent == c) {
					for (Client *s = c->mon->stack; s; s = s->snext)
						if (s->ultparent == c->ultparent && s->ismodal) {
							modalchild = 1;
							break;
						}
				}
				if (!modalchild) {
					logdatetime(stderr);
					fprintf(stderr, "dwm: attempting SIGTERM on client \"%s\" (pid:%u)\n", c->name, c->pid);
					killclientex(c, 1);
				}
				#if 0
				else {
					logdatetime(stderr);
					fprintf(stderr, "dwm: ignoring \"%s\" (pid:%u) (ultparent:\"%s\")\n", c->name, c->pid, c->ultparent->name);
				}
				#endif
			}
			#else // NO PATCH_MODAL_SUPPORT
			logdatetime(stderr);
			fprintf(stderr, "dwm: attempting SIGTERM on client \"%s\" (pid:%u)\n", c->name, c->pid);
			killclientex(c, 1);
			#endif // PATCH_MODAL_SUPPORT
		}

	if (!cnt) {
		running = 0;
		return;
	}
}

void
sigreload(int unused)
{
	logdatetime(stderr);
	fputs("dwm: received reload signal\n", stderr);
	running = -1;
}

void
sigreloadrules(int unused)
{
	logdatetime(stderr);
	fputs("dwm: received reloadrules signal\n", stderr);
	Arg a = {.i = 0};
	reloadrules(&a);
}

void
sigterm(int unused)
{
	logdatetime(stderr);
	fputs("dwm: received SIGTERM\n", stderr);
	#if PATCH_HANDLE_SIGNALS
	if (!running && killable) {
		pid_t dwmpid = getpid();
		kill(dwmpid, SIGKILL);
	}
	else
	#endif // PATCH_HANDLE_SIGNALS
	running = 0;
}
#endif // PATCH_HANDLE_SIGNALS

#if PATCH_STATUSCMD
#if 0
void
sigchld(int unused)
{
	if (signal(SIGCHLD, sigchld) == SIG_ERR)
		die("can't install SIGCHLD handler:");
	while (0 < waitpid(-1, NULL, WNOHANG));
}
#endif
void
sigstatusbar(const Arg *arg)
{
	union sigval sv;

	if (!statussig)
		return;
	sv.sival_int = arg->i;
	if ((statuspid = getstatusbarpid()) <= 0)
		return;

	sigqueue(statuspid, SIGRTMIN+statussig, sv);
}
#endif // PATCH_STATUSCMD


//#if PATCH_FOCUS_FOLLOWS_MOUSE || PATCH_MOUSE_POINTER_HIDING
#if PATCH_MOUSE_POINTER_HIDING
void
snoop_root(void)
{
	if (snoop_xinput(root) == 0) {
		#if PATCH_MOUSE_POINTER_HIDING
		logdatetime(stderr);
		fprintf(stderr, "dwm: no XInput devices found.\n");
		#endif // PATCH_MOUSE_POINTER_HIDING
	}
}

int
snoop_xinput(Window win)
{
	int opcode, event, error, numdevs, i, j;
	int major, minor, rc, rawmotion = 0;
	int ev = 0;
	unsigned char mask[(XI_LASTEVENT + 7)/8];
	XDeviceInfo *devinfo = NULL;
	XInputClassInfo *ici;
	XDevice *device;
	XIEventMask evmasks[1];
	XEventClass class_presence;

	if (!XQueryExtension(dpy, "XInputExtension", &opcode, &event, &error)) {
		logdatetime(stderr);
		fprintf(stderr, "dwm: XInput extension not available.\n");
		return 0;
	}

	/*
	 * If we support xinput 2, use that for raw motion and button events to
	 * get pointer data when the cursor is over a Chromium window.  We
	 * could also use this to get raw key input and avoid the other XInput
	 * stuff, but we may need to be able to examine the key value later to
	 * filter out ignored keys.
	 */
	major = minor = 2;
	rc = XIQueryVersion(dpy, &major, &minor);
	if (rc != BadRequest) {
		memset(mask, 0, sizeof(mask));

		XISetMask(mask, XI_RawMotion);
		#if PATCH_MOUSE_POINTER_HIDING
		XISetMask(mask, XI_RawButtonPress);
		#endif // PATCH_MOUSE_POINTER_HIDING
		evmasks[0].deviceid = XIAllMasterDevices;
		evmasks[0].mask_len = sizeof(mask);
		evmasks[0].mask = mask;

		XISelectEvents(dpy, win, evmasks, 1);
		XFlush(dpy);

		rawmotion = 1;
		logdatetime(stderr);
		fprintf(stderr, "dwm: using XInput raw motion events.\n");
	}

	devinfo = XListInputDevices(dpy, &numdevs);
	XEventClass event_list[numdevs * 2];
	for (i = 0; i < numdevs; i++) {
		if (devinfo[i].use != IsXExtensionKeyboard &&
		    devinfo[i].use != IsXExtensionPointer)
			continue;

		if (!(device = XOpenDevice(dpy, devinfo[i].id)))
			break;

		for (ici = device->classes, j = 0; j < devinfo[i].num_classes; ici++, j++) {
			switch (ici->input_class) {
			#if PATCH_MOUSE_POINTER_HIDING
			case KeyClass:
				DEBUG("attaching to keyboard device %s (use %d)\n", devinfo[i].name, devinfo[i].use);

				//DeviceKeyPress(device, key_press_type, event_list[ev]); ev++;
				DeviceKeyRelease(device, key_release_type, event_list[ev]); ev++;
				break;

			case ButtonClass:
				if (rawmotion)
					continue;

				DEBUG("attaching to buttoned device %s (use %d)\n", devinfo[i].name, devinfo[i].use);

				DeviceButtonPress(device, button_press_type, event_list[ev]); ev++;
				DeviceButtonRelease(device, button_release_type, event_list[ev]); ev++;
				break;
			#endif // PATCH_MOUSE_POINTER_HIDING
			case ValuatorClass:
				if (rawmotion)
					continue;

				DEBUG("attaching to pointing device %s (use %d)\n", devinfo[i].name, devinfo[i].use);

				DeviceMotionNotify(device, motion_type, event_list[ev]); ev++;
				break;
			}
		}

		XCloseDevice(dpy, device);

		if (XSelectExtensionEvent(dpy, win, event_list, ev)) {
			logdatetime(stderr);
			fprintf(stderr, "dwm: error selecting extension events.\n");
			ev = 0;
			goto done;
		}
	}

	DevicePresence(dpy, device_change_type, class_presence);
	if (XSelectExtensionEvent(dpy, win, &class_presence, 1)) {
		fprintf(stderr, "error selecting extension events\n.");
		ev = 0;
		goto done;
	}

done:
	if (devinfo != NULL)
	   XFreeDeviceList(devinfo);

	return ev;
}
#endif // PATCH_MOUSE_POINTER_HIDING
//#endif // PATCH_FOCUS_FOLLOWS_MOUSE || PATCH_MOUSE_POINTER_HIDING

int
solitary(Client *c)
{
	return
		#if PATCH_BORDERLESS_SOLITARY_CLIENTS
		borderless_solitary ? (
			((nexttiled(c->mon->clients) == c && !nexttiled(c->next))
			|| &monocle == c->mon->lt[c->mon->sellt]->arrange
			|| (c->isfullscreen
				#if PATCH_FLAG_FAKEFULLSCREEN
				&& c->fakefullscreen != 1
				#endif // PATCH_FLAG_FAKEFULLSCREEN
			))
			&& (!c->isfloating || (c->isfullscreen
				#if PATCH_FLAG_FAKEFULLSCREEN
				&& c->fakefullscreen != 1
				#endif // PATCH_FLAG_FAKEFULLSCREEN
			))
			&& NULL != c->mon->lt[c->mon->sellt]->arrange
		) :
		#endif // PATCH_BORDERLESS_SOLITARY_CLIENTS
		(c->isfullscreen
			#if PATCH_FLAG_FAKEFULLSCREEN
			&& c->fakefullscreen != 1
			#endif // PATCH_FLAG_FAKEFULLSCREEN
		)
	;
}

void
spawn(const Arg *arg)
{
	spawnex(arg->v, False);
}

pid_t
spawnex(const void *v, int keyhelp)
{
	struct sigaction sa;
	pid_t pid;
	char buffer[256];
	char bigbuff[32768];
	size_t b = 0, i, j;
	size_t buffsize = sizeof buffer - 1;
	int k;
	KeySym keysym;
	unsigned int mod;
	const char *description;
	XClassHint ch = { NULL, NULL };
	int x, y;
	Monitor *m = selmon;
	Client *c = NULL;

	if (keyhelp) {

#define TEST_KEY_MASK(MASK,TEXT) \
	if (mod & MASK) { \
		strncpy(buffer + j, TEXT"-", buffsize - j); \
		j += strlen(TEXT"-"); \
	}
#define APPEND_BUFFER(KEYSYM,TEXT) \
	case KEYSYM: \
		strncpy(buffer + j, TEXT, buffsize - j); \
		j += strlen(TEXT); \
		break;

		for (i = 0; i < LENGTH(keys); i++) {

			mod = keys[i].mod;
			keysym = keys[i].keysym;
			description = keys[i].description;
			j = 0;

			TEST_KEY_MASK(Mod4Mask, "Super")
			TEST_KEY_MASK(ShiftMask, "Shift")
			TEST_KEY_MASK(ControlMask, "Control")
			TEST_KEY_MASK(Mod1Mask, "Alt")

			if (j) {
				buffer[j - 1] = ' ';
				buffer[j++] = '+';
				buffer[j++] = ' ';
			}

			for (k = 0; k < charcodes_len; k++)
				if (charcodes[k].symbol == keysym) {
					switch (keysym) {
						APPEND_BUFFER(XF86AudioLowerVolume, "Volume_Down")
						APPEND_BUFFER(XF86AudioMute, "Volume_Mute")
						APPEND_BUFFER(XF86AudioRaiseVolume, "Volume_Up")
						APPEND_BUFFER(XF86AudioPlay, "Audio_Play")
						APPEND_BUFFER(XF86AudioStop, "Audio_Stop")
						APPEND_BUFFER(XF86AudioPrev, "Audio_Previous")
						APPEND_BUFFER(XF86AudioNext, "Audio_Next")
						APPEND_BUFFER(XK_space, "Space")
						APPEND_BUFFER(XK_ISO_Level3_Shift, "ISO_L3_Shift")
						APPEND_BUFFER(XK_BackSpace, "BackSpace")
						APPEND_BUFFER(XK_Tab, "Tab")
						APPEND_BUFFER(XK_Pause, "Pause")
						APPEND_BUFFER(XK_Scroll_Lock, "Scroll_Lock")
						APPEND_BUFFER(XK_Escape, "Escape")
						APPEND_BUFFER(XK_Home, "Home")
						APPEND_BUFFER(XK_Page_Up, "Page_Up")
						APPEND_BUFFER(XK_Page_Down, "Page_Down")
						APPEND_BUFFER(XK_End, "End")
						APPEND_BUFFER(XK_KP_Left, "KP_Left")
						APPEND_BUFFER(XK_KP_Up, "KP_Up")
						APPEND_BUFFER(XK_KP_Right, "KP_Right")
						APPEND_BUFFER(XK_KP_Down, "KP_Down")
						APPEND_BUFFER(XK_KP_Page_Down, "KP_Page_Down")
						APPEND_BUFFER(XK_KP_Page_Up, "KP_Page_Up")
						APPEND_BUFFER(XK_KP_Home, "KP_Home")
						APPEND_BUFFER(XK_KP_End, "KP_End")
						APPEND_BUFFER(XK_KP_Enter, "KP_Enter")
						APPEND_BUFFER(XK_Left, "Cursor_Left")
						APPEND_BUFFER(XK_Up, "Cursor_Up")
						APPEND_BUFFER(XK_Right, "Cursor_Right")
						APPEND_BUFFER(XK_Down, "Cursor_Down")
						APPEND_BUFFER(XK_Insert, "Insert")
						APPEND_BUFFER(XK_Menu, "Menu")
						APPEND_BUFFER(XK_Num_Lock, "Num_Lock")
						APPEND_BUFFER(XK_KP_Multiply, "KP_Multiply")
						APPEND_BUFFER(XK_KP_Add, "KP_Add")
						APPEND_BUFFER(XK_KP_Subtract, "KP_Subtract")
						APPEND_BUFFER(XK_KP_Decimal, "KP_Decimal")
						APPEND_BUFFER(XK_KP_Divide, "KP_Divide")
						APPEND_BUFFER(XK_KP_0, "KP_0")
						APPEND_BUFFER(XK_KP_1, "KP_1")
						APPEND_BUFFER(XK_KP_2, "KP_2")
						APPEND_BUFFER(XK_KP_3, "KP_3")
						APPEND_BUFFER(XK_KP_4, "KP_4")
						APPEND_BUFFER(XK_KP_5, "KP_5")
						APPEND_BUFFER(XK_KP_6, "KP_6")
						APPEND_BUFFER(XK_KP_7, "KP_7")
						APPEND_BUFFER(XK_KP_8, "KP_8")
						APPEND_BUFFER(XK_KP_9, "KP_9")
						APPEND_BUFFER(XK_F1, "F1")
						APPEND_BUFFER(XK_F2, "F2")
						APPEND_BUFFER(XK_F3, "F3")
						APPEND_BUFFER(XK_F4, "F4")
						APPEND_BUFFER(XK_F5, "F5")
						APPEND_BUFFER(XK_F6, "F6")
						APPEND_BUFFER(XK_F7, "F7")
						APPEND_BUFFER(XK_F8, "F8")
						APPEND_BUFFER(XK_F9, "F9")
						APPEND_BUFFER(XK_F10, "F10")
						APPEND_BUFFER(XK_F11, "F11")
						APPEND_BUFFER(XK_F12, "F12")
						APPEND_BUFFER(XK_F13, "F13")
						APPEND_BUFFER(XK_F14, "F14")
						APPEND_BUFFER(XK_F15, "F15")
						APPEND_BUFFER(XK_F16, "F16")
						APPEND_BUFFER(XK_F17, "F17")
						APPEND_BUFFER(XK_F18, "F18")
						APPEND_BUFFER(XK_F19, "F19")
						APPEND_BUFFER(XK_F20, "F20")
						APPEND_BUFFER(XK_F21, "F21")
						APPEND_BUFFER(XK_F22, "F22")
						APPEND_BUFFER(XK_F23, "F23")
						APPEND_BUFFER(XK_F24, "F24")
						APPEND_BUFFER(XK_F25, "F25")
						APPEND_BUFFER(XK_F26, "F26")
						APPEND_BUFFER(XK_F27, "F27")
						APPEND_BUFFER(XK_F28, "F28")
						APPEND_BUFFER(XK_F29, "F29")
						APPEND_BUFFER(XK_F30, "F30")
						APPEND_BUFFER(XK_F31, "F31")
						APPEND_BUFFER(XK_F32, "F32")
						APPEND_BUFFER(XK_F33, "F33")
						APPEND_BUFFER(XK_F34, "F34")
						APPEND_BUFFER(XK_F35, "F35")
						APPEND_BUFFER(XK_Control_L, "L_Control")
						APPEND_BUFFER(XK_Control_R, "R_Control")
						APPEND_BUFFER(XK_Alt_L, "L_Alt")
						APPEND_BUFFER(XK_Alt_R, "R_Alt")
						APPEND_BUFFER(XK_Super_L, "L_Super")
						APPEND_BUFFER(XK_Super_R, "R_Super")
						APPEND_BUFFER(XK_Hyper_L, "L_Hyper")
						APPEND_BUFFER(XK_Hyper_R, "R_Hyper")
						APPEND_BUFFER(XK_Delete, "Delete")
						APPEND_BUFFER(XK_Return, "Return")
						APPEND_BUFFER(XK_Sys_Req, "Sys_Req")
						APPEND_BUFFER(XK_Print, "Print_Screen")
						default:
							buffer[j++] = toupper(charcodes[k].key);
					}
					break;
				}

			#if PATCH_KEY_HOLD
			if (mod & ModKeyHoldMask) {
				strncpy(buffer + j, " [HELD]", buffsize - j);
				j += 6;
			}
			#endif // PATCH_KEY_HOLD
			buffer[++j] = '\0';

			strncpy(bigbuff + b, buffer, sizeof bigbuff - b);
			b += strlen(buffer) + 1;
			bigbuff[b - 1] = '\t';

			strncpy(bigbuff + b, description, sizeof bigbuff - b);
			b += strlen(description) + 1;
			bigbuff[b - 1] = '\n';
			bigbuff[b] = '\0';
		}

		setenv("KEYS", bigbuff, 1);

	}
	if (m) {
		snprintf(buffer, sizeof buffer, "%u", bh); setenv("BAR_HEIGHT", buffer, 1);
		snprintf(buffer, sizeof buffer, "%i", m->topbar ? 0 : m->mh - bh); setenv("BAR_Y", buffer, 1);
		snprintf(buffer, sizeof buffer, "%u", m->num); setenv("MONITOR", buffer, 1);
		snprintf(buffer, sizeof buffer, "%u", m->mh); setenv("MONITOR_HEIGHT", buffer, 1);
		snprintf(buffer, sizeof buffer, "%u", m->mw); setenv("MONITOR_WIDTH", buffer, 1);
		snprintf(buffer, sizeof buffer, "%i", m->mx); setenv("MONITOR_X", buffer, 1);
		snprintf(buffer, sizeof buffer, "%i", m->my); setenv("MONITOR_Y", buffer, 1);
		c = m->sel;
	} else {
		setenv("BAR_HEIGHT", "", 1);
		setenv("BAR_Y", "", 1);
		setenv("MONITOR", "", 1);
		setenv("MONITOR_HEIGHT", "", 1);
		setenv("MONITOR_WIDTH", "", 1);
		setenv("MONITOR_X", "", 1);
		setenv("MONITOR_Y", "", 1);
	}
	if (m && c) {
		XGetClassHint(dpy, c->win, &ch);
		#if PATCH_FLAG_ALWAYSONTOP
		setenv("CLIENT_ALWAYSONTOP", c->alwaysontop ? "1" : "0", 1);
		#endif // PATCH_FLAG_ALWAYSONTOP
		setenv("CLIENT_CLASS", ch.res_class ? ch.res_class : "", 1);
		#if PATCH_SHOW_DESKTOP
		setenv("CLIENT_DESKTOP", c->isdesktop ? "1" : "0", 1);
		#endif // PATCH_SHOW_DESKTOP
		setenv("CLIENT_FLOATING", c->isfloating ? "1" : "0", 1);
		#if PATCH_FLAG_GAME
		setenv("CLIENT_GAME", c->isgame ? "1" : "0", 1);
		#endif // PATCH_FLAG_GAME
		snprintf(buffer, sizeof buffer, "%u", c->h); setenv("CLIENT_HEIGHT", buffer, 1);
		setenv("CLIENT_INSTANCE", ch.res_name ? ch.res_name : "", 1);
		setenv("CLIENT_NAME", c->name, 1);
		#if PATCH_SHOW_DESKTOP
		setenv("CLIENT_ONDESKTOP", c->ondesktop ? "1" : "0", 1);
		#endif // PATCH_SHOW_DESKTOP
		#if PATCH_FLAG_PANEL
		setenv("CLIENT_PANEL", c->ispanel ? "1" : "0", 1);
		#endif // PATCH_FLAG_PANEL
		snprintf(buffer, sizeof buffer, "%u", c->pid); setenv("CLIENT_PID", buffer, 1);
		gettextprop(c->win, wmatom[WMWindowRole], buffer, sizeof buffer); setenv("CLIENT_ROLE", buffer, 1);
		#if PATCH_FLAG_STICKY
		setenv("CLIENT_STICKY", c->issticky ? "1" : "0", 1);
		#endif // PATCH_FLAG_STICKY
		snprintf(buffer, sizeof buffer, "%u", c->tags); setenv("CLIENT_TAGS", buffer, 1);
		snprintf(buffer, sizeof buffer, "%u", c->w); setenv("CLIENT_WIDTH", buffer, 1);
		snprintf(buffer, sizeof buffer, "%i", (c->x - m->mx)); setenv("CLIENT_X", buffer, 1);
		snprintf(buffer, sizeof buffer, "%i", (c->y - m->my)); setenv("CLIENT_Y", buffer, 1);
		snprintf(buffer, sizeof buffer, "0x%lx", c->win); setenv("WINDOW", buffer, 1);
		if (ch.res_class)
			XFree(ch.res_class);
		if (ch.res_name)
			XFree(ch.res_name);
	} else {
		#if PATCH_FLAG_ALWAYSONTOP
		setenv("CLIENT_ALWAYSONTOP", "", 1);
		#endif // PATCH_FLAG_ALWAYSONTOP
		setenv("CLIENT_CLASS", "", 1);
		#if PATCH_SHOW_DESKTOP
		setenv("CLIENT_DESKTOP", "", 1);
		#endif // PATCH_SHOW_DESKTOP
		setenv("CLIENT_FLOATING", "", 1);
		#if PATCH_FLAG_GAME
		setenv("CLIENT_GAME", "", 1);
		#endif // PATCH_FLAG_GAME
		setenv("CLIENT_HEIGHT", "", 1);
		setenv("CLIENT_INSTANCE", "", 1);
		setenv("CLIENT_NAME", "", 1);
		#if PATCH_SHOW_DESKTOP
		setenv("CLIENT_ONDESKTOP", "", 1);
		#endif // PATCH_SHOW_DESKTOP
		#if PATCH_FLAG_PANEL
		setenv("CLIENT_PANEL", "", 1);
		#endif // PATCH_FLAG_PANEL
		setenv("CLIENT_PID", "", 1);
		setenv("CLIENT_ROLE", "", 1);
		#if PATCH_FLAG_STICKY
		setenv("CLIENT_STICKY", "", 1);
		#endif // PATCH_FLAG_STICKY
		setenv("CLIENT_TAGS", "", 1);
		setenv("CLIENT_WIDTH", "", 1);
		setenv("CLIENT_X", "", 1);
		setenv("CLIENT_Y", "", 1);
		setenv("WINDOW", "", 1);
	}
	if (getrootptr(&x, &y)) {
		c = getclientatcoords(x, y, False);
		setenv("CLIENT_UNDER_CURSOR", (c ? c->name : ""), 1);
		if (c) {
			setenv("CLIENT_UNDER_CURSOR", c->name, 1);
			snprintf(buffer, sizeof buffer, "0x%lx", c->win); setenv("WINDOW_UNDER_CURSOR", buffer, 1);
		}
		else {
			setenv("CLIENT_UNDER_CURSOR", "", 1);
			setenv("WINDOW_UNDER_CURSOR", "", 1);
		}

		if (m) {
			x -= m->mx;
			y -= m->my;
		}
		snprintf(buffer, sizeof buffer, "%i", x); setenv("CURSOR_X", buffer, 1);
		snprintf(buffer, sizeof buffer, "%i", y); setenv("CURSOR_Y", buffer, 1);
	} else {
		setenv("CURSOR_X", "", 1);
		setenv("CURSOR_Y", "", 1);
		setenv("CLIENT_UNDER_CURSOR", "", 1);
		setenv("WINDOW_UNDER_CURSOR", "", 1);
	}

	if (v == dmenucmd)
		dmenumon[0] = '0' + selmon->num;
	if ((pid = fork()) == 0) {
		if (dpy)
			close(ConnectionNumber(dpy));
		setsid();

		sigemptyset(&sa.sa_mask);
		sa.sa_flags = 0;
		sa.sa_handler = SIG_DFL;
		sigaction(SIGCHLD, &sa, NULL);

		execvp(((char **)v)[0], (char **)v);
		logdatetime(stderr);
		fprintf(stderr, "dwm: execvp '%s' failed:", ((char **)v)[0]);
		exit(EXIT_SUCCESS);
	}
	return pid;
}

void
spawnhelp(const Arg *arg)
{
	spawnex(arg->v, True);
}

#if PATCH_ALTTAB
int
strcmpbynum(const char *s1, const char *s2) {
	for (;;) {
		if (*s2 == '\0')
			return *s1 != '\0';
		else if (*s1 == '\0')
			return -1;
		else if (!(isdigit(*s1) && isdigit(*s2))) {
			if (*s1 != *s2)
				return (int)tolower(*s1) - (int)tolower(*s2);
			else
				(++s1, ++s2);
		} else {
			char *lim1, *lim2;
			unsigned long n1 = strtoul(s1, &lim1, 10);
			unsigned long n2 = strtoul(s2, &lim2, 10);
			if (n1 > n2)
					return 1;
			else if (n1 < n2)
					return -1;
			s1 = lim1;
			s2 = lim2;
		}
	}
}
#endif // PATCH_ALTTAB

#if PATCH_IPC
int
subscribe(const char *event)
{
	if (connect_to_socket() == -1) {
		logdatetime(stderr);
		fprintf(stderr, "dwm: Failed to connect to socket \"%s\".\n", ipcsockpath);
		return 0;
	}

	char *msg;
	size_t msg_size;

	cJSON *gen = cJSON_CreateObject();

	// Message format:
	// {
	//   "event": "<event>",
	//   "action": "subscribe"
	// }

	cJSON_AddStringToObject(gen, "event", event);
	cJSON_AddStringToObject(gen, "action", "subscribe");

	msg = cJSON_PrintUnformatted(gen);
	msg_size = strlen(msg) + 1;

	send_message(IPC_TYPE_SUBSCRIBE, msg_size, (uint8_t *)msg);

	if (!ipc_ignore_reply)
		print_socket_reply();
	else
		flush_socket_reply();

	cJSON_free(msg);
	cJSON_Delete(gen);

	return 0;
}
#endif // PATCH_IPC

#if PATCH_TERMINAL_SWALLOWING
Client *
swallowingclient(Window w)
{
	Client *c;
	Monitor *m;

	for (m = mons; m; m = m->next) {
		for (c = m->clients; c; c = c->next) {
			if (c->swallowing && c->swallowing->win == w)
				return c;
		}
	}

	return NULL;
}
#endif // PATCH_TERMINAL_SWALLOWING

void
swapmon(const Arg *arg)
{
	Monitor *m;
	for (m = mons; m && m->num != arg->ui; m = m->next);
	if (m)
		viewmontag(m, 0, 0);
}

#if PATCH_PERSISTENT_METADATA
void
setclienttagprop(Client *c)
{
	setclienttagpropex(c, 0);
}
void
setclienttagpropex(Client *c, int index)
{
	if (
		#if PATCH_FLAG_PANEL
		c->ispanel ||
		#endif // PATCH_FLAG_PANEL
		#if PATCH_FLAG_IGNORED
		c->isignored ||
		#endif // PATCH_FLAG_IGNORED
		0)
		return;
	int bw = (
		c->isfullscreen
		#if PATCH_FLAG_FAKEFULLSCREEN
		&& c->fakefullscreen != 1
		#endif // PATCH_FLAG_FAKEFULLSCREEN
		? c->oldbw : c->bw
	);

	int x, y;
	if (c->isfloating) {
		x = c->x;
		y = c->y;
	}
	else {
		x = c->sfx;
		y = c->sfy;
	}
	x -= c->mon->mx;
	y -= c->mon->my;
	long data[] = {
		(long) index,	// placeholder for index within Window array;
		(long) c->tags,
		(long) (c->monindex == -1 ? c->mon->num : c->monindex),
		(long) (c->isfloating & ~(1 << 1)),
		(long) (x),
		(long) (y),
		(long) (c->isfloating ? c->w : c->sfw),
		(long) (c->isfloating ? c->h : c->sfh),
		(long) (c->sfxo * 1000),
		(long) (c->sfyo * 1000),
		(long) (bw == borderpx ? 0 : bw + 1)
		#if PATCH_CFACTS
		,(long) (c->cfact * 100)
		#else // NO PATCH_CFACTS
		,0L
		#endif // PATCH_CFACTS
		#if PATCH_FLAG_FAKEFULLSCREEN
		,(long) c->fakefullscreen
		#else // NO PATCH_FLAG_FAKEFULLSCREEN
		,0L
		#endif // PATCH_FLAG_FAKEFULLSCREEN
		#if PATCH_FLAG_HIDDEN
		,(long) c->ishidden
		#else // NO PATCH_FLAG_HIDDEN
		,0L
		#endif // PATCH_FLAG_HIDDEN
		#if PATCH_SHOW_DESKTOP
		,(long) c->isdesktop
		,(long) c->ondesktop
		#else // NO PATCH_SHOW_DESKTOP
		,0L
		,0L
		#endif // PATCH_SHOW_DESKTOP
	};
	XChangeProperty(dpy, c->win, netatom[NetClientInfo], XA_CARDINAL, 32,
			PropModeReplace, (unsigned char *) data, LENGTH(data));
}
#endif // PATCH_PERSISTENT_METADATA

#if PATCH_ALTTAB
void
altTab(int direction, int first)
{
	Client *next = NULL;
	// move to next window;
	if (!altTabActive && direction) {
		altTabActive = 1;
		direction = 0;
	}
	altTabMon->altTabN += direction;
	if (altTabMon->altTabN < 0)
		altTabMon->altTabN = altTabMon->nTabs - 1;
	if (altTabMon->altTabN >= altTabMon->nTabs)
		altTabMon->altTabN = 0;
	next = altTabMon->altsnext[altTabMon->altTabN];
	highlight(next);

	#if PATCH_ALTTAB_HIGHLIGHT
	if (!(altTabMon->isAlt & ALTTAB_MOUSE) &&
		(altTabMon->isAlt & ALTTAB_SELMON_MASK) &&
		tabHighlight
	) {
		#if PATCH_MOUSE_POINTER_WARPING
		#if PATCH_MOUSE_POINTER_WARPING_SMOOTH
		warptoclient(next, (!first || altTabMon->sel != next) ? 1 : 0, 1);
		#else // NO PATCH_MOUSE_POINTER_WARPING_SMOOTH
		warptoclient(next, 1);
		#endif // PATCH_MOUSE_POINTER_WARPING_SMOOTH
		#endif // PATCH_MOUSE_POINTER_WARPING
	}
	#endif // PATCH_ALTTAB_HIGHLIGHT

	drawTab(altTabMon, 1, 0);
}

void
altTabEnd(void)
{
	#if PATCH_MOUSE_POINTER_WARPING
	warptoclient_stop_flag = 1;
	#endif // PATCH_MOUSE_POINTER_WARPING

	if (!altTabMon) return;
	if (altTabMon->isAlt) {

		#if 0
		/*
		* move all clients between 1st and choosen position,
		* one down in stack and put choosen client to the first position 
		* so they remain in right order for the next time that alt-tab is used
		*/
		if (altTabMon->nTabs > 1 && altTabMon->highlight) {
			if (altTabMon->altTabN != 0) { // if user picked original client do nothing;
				Client *buff = altTabMon->altsnext[altTabMon->altTabN];
				if (altTabMon->altTabN > 1)
					for (int i = altTabMon->altTabN;i > 0;i--)
						altTabMon->altsnext[i] = altTabMon->altsnext[i - 1];
				else // swap them if there are just 2 clients;
					altTabMon->altsnext[altTabMon->altTabN] = altTabMon->altsnext[0];
				altTabMon->altsnext[0] = buff;
			}
		}
		#endif

		// turn off/destroy the window;
		highlight(NULL);
		altTabMon->isAlt = 0;
		altTabMon->nTabs = 0;
	}
	else
		altTabMon->highlight = NULL;
	if (altTabMon->altsnext) {
		free(altTabMon->altsnext); // free list of clients;
		altTabMon->altsnext = NULL;
	}

	#if PATCH_WINDOW_ICONS
	freealticons();
	#endif // PATCH_WINDOW_ICONS

	if (altTabMon->tabwin) {
		XUnmapWindow(dpy, altTabMon->tabwin);
		XDestroyWindow(dpy, altTabMon->tabwin);
		altTabMon->tabwin = None;
	}
	altTabMon = NULL;

	#if PATCH_ALT_TAGS
	if (selmon->alttags)
		selmon->alttags = 0;
	#endif // PATCH_ALT_TAGS

	drawbars();
}

void
drawTab(Monitor *m, int active, int first)
{
	#if PATCH_FLAG_HIDDEN
	char buffer[256];
	#endif // PATCH_FLAG_HIDDEN
	Client *c;
	int tabswitcher = ((m->isAlt & ALTTAB_MOUSE) == 0);
	int tab_lrpad = lrpad;
	int tab_minh = minbh;

	int boxs, boxw, lpad = 0, rpad = 0;
	if (!tabswitcher) {
		boxs = drw->fonts->h / 9;
		boxw = drw->fonts->h / 6 + 2;
		lpad = (2 * boxs + boxw);
	}

	altTabActive = active;

	unsigned int bw = (tabswitcher ? m->tabBW : 2);

	#if PATCH_FONT_GROUPS
	if ((tabswitcher && drw_select_fontgroup(drw, tabFontgroup))
	|| (!tabswitcher && apply_barelement_fontgroup(m, WinTitle)))
	{
		tab_minh = drw->selfonts->h + 2;
		tab_lrpad = 3 * drw->selfonts->h / 4;
		if (!tabswitcher) {
			boxs = (drw->selfonts ? drw->selfonts : drw->fonts)->h;
			boxw = boxs / 6 + 2;
			boxs /= 9;
			lpad = 2 * boxs + boxw;
		}
	}
	#endif // PATCH_FONT_GROUPS
	if (tabswitcher)
		lpad = tab_lrpad / 2;
	else {
		lpad = MAX(lpad, (tab_lrpad / 2));
	}
	rpad = tab_lrpad / 2;
	#if PATCH_WINDOW_ICONS
	int pad = (tabswitcher ? tab_lrpad : 0);
	#endif // PATCH_WINDOW_ICONS

	if (first) {
		XSetWindowAttributes wa;
		#if PATCH_ALPHA_CHANNEL
		if (useargb) {
			wa.override_redirect = True;
			wa.background_pixel = 0;
			wa.border_pixel = scheme[SchemeTabNorm][ColBorder].pixel;
			wa.colormap = cmap;
			wa.event_mask = ButtonPressMask|ExposureMask;
		}
		else
		#endif // PATCH_ALPHA_CHANNEL
		{
			wa.override_redirect = True;
			wa.background_pixmap = ParentRelative;
			wa.background_pixel = 0;
			wa.border_pixel = scheme[SchemeTabNorm][ColBorder].pixel;
			wa.event_mask = ButtonPressMask|ExposureMask;
		};

		// decide position of tabwin;
		int posX = m->mx;
		int posY = m->my;

		if (tabswitcher) {
			m->maxWTab = MIN((m->tabMaxW + 2*bw), m->mw);
			#if PATCH_WINDOW_ICONS
			m->maxHTab = MIN((((MAX(iconsize_big, tab_minh) + tab_lrpad*2) * m->nTabs)+2*bw), MIN((m->tabMaxH + 2*bw), m->mh));
			#else // NO PATCH_WINDOW_ICONS
			m->maxHTab = MIN((((tab_minh + tab_lrpad*2) * m->nTabs)+2*bw), MIN((m->tabMaxH + 2*bw), m->mh));
			#endif // PATCH_WINDOW_ICONS
			if (m->tabPosX == 0)
				posX += 0;
			if (m->tabPosX == 1)
				posX += (m->mw / 2) - (m->maxWTab / 2);
			if (m->tabPosX == 2)
				posX += m->mw - m->maxWTab;

			m->tih = (m->maxHTab - 2*bw) / m->nTabs;
			m->maxHTab = (m->tih * m->nTabs) + 2*bw;

			if (m->tabPosY == 0)
				posY += 0;
			if (m->tabPosY == 1)
				posY += (m->mh / 2) - (m->maxHTab / 2);
			if (m->tabPosY == 2)
				posY += m->mh - m->maxHTab;
		}
		else {

			if ((tab_lrpad * tabMenuVertFactor) + tabMenuVertGap >= bw)
				#if PATCH_FONT_GROUPS
				if ((tab_minh + (tab_lrpad * tabMenuVertFactor) + tabMenuVertGap) >= (m->bh - 2*bw))
				#else // NO PATCH_FONT_GROUPS
				if ((tab_minh + (tab_lrpad * tabMenuVertFactor) + tabMenuVertGap) >= (bh - 2*bw))
				#endif // PATCH_FONT_GROUPS
					tab_minh -= bw;

			unsigned int tw = 0;
			#if PATCH_WINDOW_ICONS
			unsigned int icw = 0;
			#endif // PATCH_WINDOW_ICONS
			// determine maximum text width;
			for (unsigned int w = 0, i = 0; i < m->nTabs; i++) {
				c = m->altsnext[i];
				if(!c
					#if PATCH_FLAG_IGNORED
					|| c->isignored
					#endif // PATCH_FLAG_IGNORED
					) continue;
				//if (!ISVISIBLE(c) && !(m->isAlt & ALTTAB_ALL_TAGS)) continue;

				#if PATCH_WINDOW_ICONS
				if (!c->alticon)
					c->alticon = geticonprop(
						#if PATCH_WINDOW_ICONS_DEFAULT_ICON || PATCH_WINDOW_ICONS_CUSTOM_ICONS
						c,
						#endif // PATCH_WINDOW_ICONS_DEFAULT_ICON || PATCH_WINDOW_ICONS_CUSTOM_ICONS
						c->win, &c->alticw, &c->altich, MIN((tab_minh - pad), iconsize)
					);
				if (!icw && c->alticon)
					icw = c->alticw + iconspacing;
				#endif // PATCH_WINDOW_ICONS

				#if PATCH_FLAG_HIDDEN
				if (c->ishidden) {
					appendhidden(m, c->name, buffer, 256);
					w = drw_fontset_getwidth(drw, buffer) + rpad + lpad;
				}
				else
				#endif // PATCH_FLAG_HIDDEN
				w = drw_fontset_getwidth(drw, c->name) + rpad + lpad;
				if ((m->isAlt & ALTTAB_ALL_MONITORS) && mons->next && (c->mon != m || altTabMon->isAlt & ALTTAB_SORTED))
					w += drw_fontset_getwidth(drw, c->mon->numstr) + tab_lrpad / 2;
				if (w > tw)
					tw = w;
			}
			#if PATCH_WINDOW_ICONS
			tw += icw + (iconspacing);
			#endif // PATCH_WINDOW_ICONS

			m->maxWTab = MIN(
							MAX(m->bar[WinTitle].w, tw),
							m->mw
						);
			m->maxHTab = MIN(
							(((tab_lrpad * tabMenuVertFactor) + tabMenuVertGap + tab_minh) * m->nTabs)+2*bw,
							(m->tabMaxH + (m->mh / 2) - (m->tabMaxH / 2)) < m->mh ? (m->tabMaxH + (m->mh / 2) - (m->tabMaxH / 2)) : m->mh
						);

			if (!tabswitcher)
				tw -= 2*bw;

			switch (m->title_align) {
				case 2:
					posX += (m->bar[WinTitle].x + m->bar[WinTitle].w - m->maxWTab);
					break;
				case 1:
					posX += m->bar[WinTitle].x + ((int)m->bar[WinTitle].w - (int)m->maxWTab) / 2;
					break;
				default:
				case 0:
					posX += m->bar[WinTitle].x;
			}
			if (posX < m->mx)
				posX = m->mx;
			if ((first = m->mw - ((posX - m->mx) + m->maxWTab)) < 0)
				posX += first;

			m->tih = (m->maxHTab - 2*bw) / m->nTabs;
			m->maxHTab = (m->tih * m->nTabs) + 2*bw;

			if (!m->topbar) {
				#if PATCH_FONT_GROUPS
				posY += m->mh - m->maxHTab - (m->isAlt & ALTTAB_OFFSET_MENU ? m->bh : 0);
				#else // NO PATCH_FONT_GROUPS
				posY += m->mh - m->maxHTab - (m->isAlt & ALTTAB_OFFSET_MENU ? bh : 0);
				#endif // PATCH_FONT_GROUPS
				m->isAlt |= ALTTAB_BOTTOMBAR;
			}
			else if (m->isAlt & ALTTAB_OFFSET_MENU)
				#if PATCH_FONT_GROUPS
				posY += m->bh;
				#else // NO PATCH_FONT_GROUPS
				posY += bh;
				#endif // PATCH_FONT_GROUPS
			m->tx = posX+bw;
			m->ty = posY+bw;
		}

		m->maxWTab -= 2*bw;
		m->maxHTab -= 2*bw;

		// create tabwin;
		#if PATCH_ALPHA_CHANNEL
		if (useargb)
			m->tabwin = XCreateWindow(dpy, root, posX, posY, m->maxWTab, m->maxHTab, bw, depth,
									CopyFromParent, visual,
									CWOverrideRedirect|CWBackPixel|CWBorderPixel|CWColormap|CWEventMask, &wa);
		else
		#endif // PATCH_ALPHA_CHANNEL
		m->tabwin = XCreateWindow(dpy, root, posX, posY, m->maxWTab, m->maxHTab, bw, DefaultDepth(dpy, screen),
								CopyFromParent, DefaultVisual(dpy, screen),
								CWOverrideRedirect|CWBackPixmap|CWEventMask, &wa);
		XClassHint ch = {"dwm", (tabswitcher ? "dwm-alttab-switcher" : "dwm-client-switcher")};
		XSetClassHint(dpy, m->tabwin, &ch);
		if (!tabswitcher)
			XChangeProperty(dpy, m->tabwin, netatom[NetWMWindowType], XA_ATOM, 32, PropModeReplace,
				m->topbar ?
					((unsigned char *)&(netatom[NetWMWindowTypeMenu])) :
						((unsigned char *)&(netatom[NetWMWindowTypePopupMenu]))
			, 1);
		XDefineCursor(dpy, m->tabwin, cursor[CurNormal]->cursor);

		XSetWindowBackground(dpy, m->tabwin, 0L);
		XClearWindow(dpy, m->tabwin);

		XMapRaised(dpy, m->tabwin);
	}

	int h = m->tih;
	int x = 0;
	int y = 0;

	int vTabs = 0;
	int sStart = 0;
	int sEnd = 0;

	int minh = (tabswitcher ? (tab_minh + tab_lrpad) : tab_minh);

	if (h < minh) {
		h = minh;
		// number of visible tabs in the scroll area;
		m->vTabs = vTabs = (m->maxHTab / h);

		// calculate scroll offset;
		for (int i = 0;i < m->nTabs;i++) {
			c = m->altsnext[i];
			if(!c
				#if PATCH_FLAG_IGNORED
				|| c->isignored
				#endif // PATCH_FLAG_IGNORED
				) continue;

			if (c == m->highlight) {
				y = i;
				break;
			}
		}

		if (y) {
			// selected tab;
			int sTab = ++y;

			// start of scroll zone;
			sStart = ((vTabs / 2) + 1);
			// end of scroll zone;
			sEnd = (m->nTabs - ((vTabs > 2) ? (sStart - 1) : 0));

			if (sTab <= sStart)
				y = 0;
			else if (sTab >= sEnd) {
				y = (m->nTabs - vTabs);
			}
			else {
				y -= sStart;
			}

			sStart = y;
			y *= -h;
		}
	}
	else m->vTabs = m->nTabs;

	// offset if we're upside-down;
	int oy = (tabswitcher || !(m->isAlt & ALTTAB_BOTTOMBAR) ? y : m->maxHTab - y - h);
	unsigned int ox;	// caption offset ox;
	int fw;				// full width of entry;
	int tw;				// text width of caption;
	int tw_mon = 0;		// text width (without padding) of monnumf caption;
	int w;				// width of caption area;
	int ow;				// width of right padding;
	unsigned int align = m->tabTextAlign;
	if (!tabswitcher) {
		align = m->title_align;
		if (align == 1 && tabMenuNoCentreAlign) {
			align = 0;
			lpad = tab_lrpad / 2;
		}
		lpad -= bw;
	}

	m->altTabIndex = m->altTabVStart = -1;
	// draw all clients into tabwin;
	for (int i = 0;i < m->nTabs;i++) {
		c = m->altsnext[i];
		if(!c
			#if PATCH_FLAG_IGNORED
			|| c->isignored
			#endif // PATCH_FLAG_IGNORED
			) continue;

		// no need to draw entries we can't see;
		if (y > -h) {
			if (m->altTabVStart == -1)
				m->altTabVStart = i;
			drw_setscheme(drw, scheme[
				(c == m->highlight && altTabActive) ? SchemeTabSel :
					(c->isurgent ? SchemeTabUrg :
						#if PATCH_FLAG_HIDDEN
						c->ishidden ? SchemeTabHide :
						#endif // PATCH_FLAG_HIDDEN
						SchemeTabNorm
					)
				]
			);

			x = ox = ow = 0;
			fw = tw = 0;
			#if PATCH_FLAG_HIDDEN
			if (c->ishidden) {
				appendhidden(m, c->name, buffer, 256);
				fw = tw = drw_fontset_getwidth(drw, buffer) + rpad + lpad;
			}
			else
			#endif // PATCH_FLAG_HIDDEN
				fw = tw = drw_fontset_getwidth(drw, c->name) + rpad + lpad;
			if ((m->isAlt & ALTTAB_ALL_MONITORS) && mons->next && (c->mon != m || altTabMon->isAlt & ALTTAB_SORTED))
				tw_mon = drw_fontset_getwidth(drw, c->mon->numstr) + tab_lrpad / 2;
			fw += tw_mon;
			ow = rpad;
			#if PATCH_WINDOW_ICONS
			if (!c->alticon)
				c->alticon = geticonprop(
					#if PATCH_WINDOW_ICONS_DEFAULT_ICON || PATCH_WINDOW_ICONS_CUSTOM_ICONS
					c,
					#endif // PATCH_WINDOW_ICONS_DEFAULT_ICON || PATCH_WINDOW_ICONS_CUSTOM_ICONS
					c->win, &c->alticw, &c->altich, MIN((h - pad), (tabswitcher ? iconsize_big : iconsize))
				);
			if (c->alticon) {
				fw += c->alticw + iconspacing;
				if (align == 0)
					ox += c->alticw + iconspacing;
				else if (align == 2)
					ow += c->alticw + iconspacing;
			}
			#endif // PATCH_WINDOW_ICONS

			w = m->maxWTab;
			if (fw > w) {
				tw -= (fw - w);
				fw = w;
			}

			if (align == 1)
				ox = ((unsigned int)(w - fw) / 2) + (fw - tw - tw_mon);
			else if (align == 2)
				ox = (unsigned int)(w - fw) + bw
					#if PATCH_WINDOW_ICONS
					- (c->alticon ? iconspacing : 0)
					#endif // PATCH_WINDOW_ICONS
				;
			ox += lpad;

			if ((m->isAlt & ALTTAB_ALL_MONITORS) && mons->next && (c->mon != m || altTabMon->isAlt & ALTTAB_SORTED)) {
				x = ox;
				if (align == 2)
					x += tw - lpad;
				#if PATCH_BIDIRECTIONAL_TEXT
				apply_fribidi(c->mon->numstr);
				#endif // PATCH_BIDIRECTIONAL_TEXT
				drw_text(
					drw, 0, oy, w, h, x, ow,
					#if PATCH_CLIENT_INDICATORS
					0,
					#endif // PATCH_CLIENT_INDICATORS
					m->title_align,
					#if PATCH_BIDIRECTIONAL_TEXT
					fribidi_text,
					#else // NO PATCH_BIDIRECTIONAL_TEXT
					c->mon->numstr,
					#endif // PATCH_BIDIRECTIONAL_TEXT
					0
				);
				if (align == 2) {
					w = x;
					x = ow = 0;
					ox = (unsigned int)(m->maxWTab - fw) + rpad;
				}
				else {
					ox = 0;
					x += tw_mon;
					w -= x;
				}
			}

			#if PATCH_BIDIRECTIONAL_TEXT
			apply_fribidi(
				#if PATCH_FLAG_HIDDEN
				c->ishidden ? buffer :
				#endif // PATCH_FLAG_HIDDEN
				c->name
			);
			#endif // PATCH_BIDIRECTIONAL_TEXT
			drw_text(
				drw, x, oy, w, h, ox, ow,
				#if PATCH_CLIENT_INDICATORS
				0,
				#endif // PATCH_CLIENT_INDICATORS
				m->title_align,
				#if PATCH_BIDIRECTIONAL_TEXT
				fribidi_text,
				#else // NO PATCH_BIDIRECTIONAL_TEXT
				#if PATCH_FLAG_HIDDEN
				c->ishidden ? buffer :
				#endif // PATCH_FLAG_HIDDEN
				c->name,
				#endif // PATCH_BIDIRECTIONAL_TEXT
				0
			);

			#if PATCH_WINDOW_ICONS
			if (c->alticon) {
				x += ox;
				switch (align) {
					case 1:
						x = ((m->maxWTab - fw) / 2) + lpad; // + lrpad / 2;
						break;
					case 2:
						x = m->maxWTab - lpad - c->alticw;
						break;
					default:
					case 0:
						x = lpad;
				}
				drw_pic(drw, x, oy + (h - c->altich)/2, c->alticw, c->altich, c->alticon);
			}
			#endif // PATCH_WINDOW_ICONS
		}
		if (c == m->highlight)
			m->altTabIndex = i;
		y += h;
		oy = (tabswitcher || !(m->isAlt & ALTTAB_BOTTOMBAR) ? y : m->maxHTab - y - h);
		if (y > m->maxHTab) break;
	}
	if (y < m->maxHTab) {
		drw_setscheme(drw, scheme[SchemeTabNorm]);
		drw_rect(drw, 0, oy, m->maxWTab, h, 1, 1);
	}

	if (vTabs) {
		drw_setscheme(drw, scheme[SchemeTabNorm]);
		drw_rect(drw,
			(align == 2 ? tab_lrpad : (m->maxWTab - tab_lrpad)),
			(tab_lrpad * tabMenuVertFactor) + tabMenuVertGap, 10, m->maxHTab - tab_lrpad, 0, 0
		);
		h = (m->maxHTab - tab_lrpad - (tab_lrpad * tabMenuVertFactor) - tabMenuVertGap);
		sEnd = (h * (sStart + vTabs) / m->nTabs) - (h * sStart / m->nTabs);
		drw_rect(drw,
			(align == 2 ? (tab_lrpad + 3) : (m->maxWTab - tab_lrpad + 3)),
			((tab_lrpad * tabMenuVertFactor) + tabMenuVertGap) + 3 + (h * sStart / m->nTabs),
			4, (sEnd > 0 ? sEnd : 1), 1, 0
		);
	}
	else
		drw_setscheme(drw, scheme[SchemeTabNorm]);
	drw_map(drw, m->tabwin, 0, 0, m->maxWTab, m->maxHTab);

	#if PATCH_FONT_GROUPS
	drw_select_fontgroup(drw, NULL);
	lrpad = drw->fonts->lrpad;
	#endif // PATCH_FONT_GROUPS
}

void
altTabStart(const Arg *arg)
{
	if (running != 1)
		return;

	#if PATCH_FOCUS_FOLLOWS_MOUSE
	checkmouseovermonitor(selmon);
	#endif // PATCH_FOCUS_FOLLOWS_MOUSE

	Monitor *selm = selmon, *m = selmon;
	Client *c, *s = NULL;
	unsigned int mon_mask = (arg->ui & ALTTAB_SELMON_MASK);
	if (mon_mask != ALTTAB_SELMON_MASK) {
		for (m = mons; m; m = m->next)
			if (m->num == mon_mask)
				break;
		if (!m)
			m = selmon;
	}

	#if PATCH_MOUSE_POINTER_HIDING
	showcursor();
	#endif // PATCH_MOUSE_POINTER_HIDING

	m->altsnext = NULL;
	if (m->tabwin)
		altTabEnd();

	if (m->isAlt) {
		altTabEnd();
	} else {

		// Can't traverse similar class clients, without a starting selection;
		if ((arg->ui & ALTTAB_SAME_CLASS) && !m->sel)
			return;

		altTabMon = m;
		mon_mask = (altTabMon == selm ? 1 : 0);
		altTabMon->isAlt = (mon_mask ? (arg->ui | ALTTAB_SELMON_MASK) : (arg->ui & ~ALTTAB_SELMON_MASK));
		if (altTabMon->isAlt & ALTTAB_ALL_MONITORS) {
			#if PATCH_CONSTRAIN_MOUSE && PATCH_FOCUS_FOLLOWS_MOUSE
			if (constrained)
				altTabMon->isAlt = altTabMon->isAlt & ~ALTTAB_ALL_MONITORS;
			else
			#endif // PATCH_CONSTRAIN_MOUSE && PATCH_FOCUS_FOLLOWS_MOUSE
			if (mons->next) {
				int mcount = 0;
				#if PATCH_VIRTUAL_MONITORS
				char monnum[5];
				#else // NO PATCH_VIRTUAL_MONITORS
				char monnum[3];
				#endif // PATCH_VIRTUAL_MONITORS
				for (Monitor *m = mons; m && ++mcount; m = m->next);
				for (Monitor *m = mons; m; m = m->next) {
					#if PATCH_VIRTUAL_MONITORS
					snprintf(monnum, sizeof monnum, (mcount > 10) ? "%02d" : "%01d", m->num % 1000);
					if (m->num >= 1000) {
						int i;
						for (i = 0; monnum[i] != '\0' && i < (sizeof monnum) - 2; i++);
						monnum[i++] = '.';
						monnum[i++] = '2';
						monnum[i] = '\0';
					}
					#else // NO PATCH_VIRTUAL_MONITORS
					snprintf(monnum, sizeof monnum, (mcount > 10) ? "%02d" : "%01d", m->num);
					#endif // PATCH_VIRTUAL_MONITORS
					snprintf(m->numstr, sizeof m->numstr, monnumf, monnum);
				}
			}
		}

		int direction = (altTabMon->isAlt & ALTTAB_REVERSE) ? -1 : +1;
		altTabMon->altTabN = 0;
		altTabMon->altTabSel = selmon->sel;

		#if (PATCH_MOUSE_POINTER_WARPING && !PATCH_MOUSE_POINTER_WARPING_RECALL) || PATCH_FOCUS_FOLLOWS_MOUSE
		// store mouse coords in case we cancel alt-tab or the client doesn't change;
		int px, py;
		if (!(altTabMon->isAlt & ALTTAB_MOUSE) && !getrootptr(&px, &py))
			px = 0;
		#endif // (PATCH_MOUSE_POINTER_WARPING && !PATCH_MOUSE_POINTER_WARPING_RECALL) || PATCH_FOCUS_FOLLOWS_MOUSE
		#if PATCH_MOUSE_POINTER_WARPING && PATCH_MOUSE_POINTER_WARPING_RECALL
		if (!(altTabMon->isAlt & ALTTAB_MOUSE) && selmon->sel)
			lastcoordsstore(selmon->sel);
		#endif // PATCH_MOUSE_POINTER_WARPING && PATCH_MOUSE_POINTER_WARPING_RECALL

		//m = NULL;
		#if PATCH_FLAG_HIDDEN
		unsigned int hidden = 0;
		#endif // PATCH_FLAG_HIDDEN

		altTabMon->nTabs = 0;
		for (m = mons; m; m = m->next)
			if ((altTabMon->isAlt & ALTTAB_ALL_MONITORS) || m == altTabMon) {
				for(c = m->clients; c; c = c->next) {
					if (c->neverfocus || c->dormant
						#if PATCH_FLAG_HIDDEN
						|| (c->ishidden && !(altTabMon->isAlt & ALTTAB_HIDDEN))
						#endif // PATCH_FLAG_HIDDEN
						#if PATCH_FLAG_IGNORED
						|| c->isignored
						#endif // PATCH_FLAG_IGNORED
						#if PATCH_FLAG_PANEL
						|| c->ispanel
						#endif // PATCH_FLAG_PANEL
						#if PATCH_SHOW_DESKTOP
						|| (c->isdesktop && (!(altTabMon->isAlt & ALTTAB_MOUSE) || !showdesktop
							#if PATCH_SHOW_DESKTOP_BUTTON
							|| (!m->showdesktop && drawbar_elementvisible(m, ShowDesktop))
							#endif // PATCH_SHOW_DESKTOP_BUTTON
						))
						#endif // PATCH_SHOW_DESKTOP
					) continue;
					if (
						#if PATCH_SHOW_DESKTOP
						(
							!showdesktop ? !ISVISIBLE(c) :
							(
								m->showdesktop ?
								!desktopvalidex(c, m->tagset[m->seltags], -1) :
								(
									(!ISVISIBLE(c) && !c->isdesktop)
									#if PATCH_SHOW_DESKTOP_BUTTON
									|| (c->isdesktop && drawbar_elementvisible(m, ShowDesktop))
									#endif // PATCH_SHOW_DESKTOP_BUTTON
								)
							)
						)
						#else // NO PATCH_SHOW_DESKTOP
						!ISVISIBLE(c)
						#endif // PATCH_SHOW_DESKTOP
						&& !(altTabMon->isAlt & ALTTAB_ALL_TAGS)) continue;
					if (!s) s = c;

					#if PATCH_FLAG_HIDDEN
					if (c->ishidden)
						++hidden;
					#endif // PATCH_FLAG_HIDDEN
					++altTabMon->nTabs;
				}
			}

		if (altTabMon->nTabs <=
			(
				(altTabMon->isAlt & ALTTAB_MOUSE) &&
				!(altTabMon->isAlt & ALTTAB_ALL_MONITORS || altTabMon->isAlt & ALTTAB_ALL_TAGS)
				#if PATCH_SHOW_DESKTOP
				&& !(showdesktop && altTabMon->showdesktop)
				#endif // PATCH_SHOW_DESKTOP
				? 1 : 0
			)
			#if PATCH_FLAG_HIDDEN
			&& !(hidden && (altTabMon->isAlt & ALTTAB_HIDDEN))
			#endif // PATCH_FLAG_HIDDEN
		) {
			#if PATCH_FLAG_GAME
			if (s && s->isgame && s->isfullscreen && MINIMIZED(s) && altTabMon->nTabs == 1
				&& (altTabMon->isAlt & (ALTTAB_SELMON_MASK | ALTTAB_MOUSE))
				&& !(altTabMon->isAlt & (ALTTAB_ALL_MONITORS | ALTTAB_ALL_TAGS | ALTTAB_SAME_CLASS))
			) {
				//unfocus(s, 1);
				focus(s, 0);
			}
			#endif // PATCH_FLAG_GAME
			altTabEnd(); // end the alt-tab functionality;
			return;
		}

		altTabMon->altsnext = (Client **) malloc(altTabMon->nTabs * sizeof(Client *));

		int listIndex = 0;
		int same;
		const char *sel_class;
		XClassHint sel_ch = { NULL, NULL };
		if (altTabMon->altTabSel) {
			if (!(sel_class = altTabMon->altTabSel->grpclass)) {
				XGetClassHint(dpy, altTabMon->altTabSel->win, &sel_ch);
				sel_class = sel_ch.res_class ? sel_ch.res_class : broken;
			}
		}
		else sel_class = broken;

		m = altTabMon;
		while (m) {
			// add clients to the list;
			int first = 1;
			for(c = (m == altTabMon && m->sel) ? m->sel : m->stack; ; c = c->snext) {

				if (m == altTabMon && m->sel && m->sel != m->stack) {
					if (!c)
						c = m->stack;
					else if (c == m->sel && !first)
						break;
				}
				else if (!c)
					break;
				first = 0;

				if (c->neverfocus || c->dormant
					#if PATCH_FLAG_HIDDEN
					|| (c->ishidden && !(altTabMon->isAlt & ALTTAB_HIDDEN))
					#endif // PATCH_FLAG_HIDDEN
					#if PATCH_FLAG_IGNORED
					|| c->isignored
					#endif // PATCH_FLAG_IGNORED
					#if PATCH_FLAG_PANEL
					|| c->ispanel
					#endif // PATCH_FLAG_PANEL
					#if PATCH_SHOW_DESKTOP
					|| (c->isdesktop && (!(altTabMon->isAlt & ALTTAB_MOUSE) || !showdesktop
						#if PATCH_SHOW_DESKTOP_BUTTON
						|| (!m->showdesktop && drawbar_elementvisible(m, ShowDesktop))
						#endif // PATCH_SHOW_DESKTOP_BUTTON
					))
					#endif // PATCH_SHOW_DESKTOP
				) continue;
				if (
					#if PATCH_SHOW_DESKTOP
					(
						!showdesktop ? !ISVISIBLE(c) :
						(
							m->showdesktop ?
							!desktopvalidex(c, m->tagset[m->seltags], -1) :
							(
								(!ISVISIBLE(c) && !c->isdesktop)
								#if PATCH_SHOW_DESKTOP_BUTTON
								|| (c->isdesktop && drawbar_elementvisible(m, ShowDesktop))
								#endif // PATCH_SHOW_DESKTOP_BUTTON
							)
						)
					)
					#else // NO PATCH_SHOW_DESKTOP
					!ISVISIBLE(c)
					#endif // PATCH_SHOW_DESKTOP
					&& !(altTabMon->isAlt & ALTTAB_ALL_TAGS)) continue;

				if (altTabMon->isAlt & ALTTAB_SAME_CLASS) {

					if (c->grpclass)
						same = (strcmp(sel_class,c->grpclass)==0 ? 1 : 0);
					else {
						const char *class;
						XClassHint ch = { NULL, NULL };
						XGetClassHint(dpy, c->win, &ch);
						class = ch.res_class ? ch.res_class : broken;
						same = (strcmp(sel_class,class)==0 ? 1 : 0);

						if (ch.res_class)
							XFree(ch.res_class);
						if (ch.res_name)
							XFree(ch.res_name);
					}

					if (!same) {
						--altTabMon->nTabs;
						continue;
					}
				}
				altTabMon->altsnext[listIndex++] = c;

				#if PATCH_ALTTAB_HIGHLIGHT
				// clear highlighted borders for later;
				if (tabHighlight && !(altTabMon->isAlt & ALTTAB_MOUSE) && !ISVISIBLE(c))
					XSetWindowBorder(dpy, c->win, scheme[SchemeNorm][ColBorder].pixel);
				#endif // PATCH_ALTTAB_HIGHLIGHT
			}
			if (altTabMon->isAlt & ALTTAB_ALL_MONITORS) {
				if (!(m = m->next))
					m = mons;
				if (m == altTabMon)
					break;
			}
			else break;
		}

		if (sel_ch.res_class)
			XFree(sel_ch.res_class);
		if (sel_ch.res_name)
			XFree(sel_ch.res_name);

		if (listIndex <=
			(
				(altTabMon->isAlt & ALTTAB_MOUSE)
				&& !(altTabMon->isAlt & ALTTAB_ALL_MONITORS || altTabMon->isAlt & ALTTAB_ALL_TAGS)
				#if PATCH_SHOW_DESKTOP
				&& !(showdesktop && altTabMon->showdesktop)
				#endif // PATCH_SHOW_DESKTOP
				? 1 : 0
			)
			#if PATCH_FLAG_HIDDEN
			&& (!hidden || !(altTabMon->isAlt & ALTTAB_HIDDEN))
			#endif // PATCH_FLAG_HIDDEN
		) {
			altTabEnd();
			return;
		}

		if (altTabMon->isAlt & ALTTAB_SORTED) {
			// sort the list before display;
			int done = 0;
			while (!done) {
				done = 1;
				for (int i = 0; i < listIndex - 1; i++)
					if (strcmpbynum(altTabMon->altsnext[i]->name, altTabMon->altsnext[i + 1]->name) > 0) {
						done = 0;
						c = altTabMon->altsnext[i];
						altTabMon->altsnext[i] = altTabMon->altsnext[i + 1];
						altTabMon->altsnext[i + 1] = c;
					}
			}
		}
		else if (altTabMon->isAlt & ALTTAB_SORTED_BY_MONITOR) {
			// sort the list before display;
			int done = 0;
			while (!done) {
				done = 1;
				for (int i = 0; i < listIndex - 1; i++) {
					if (
						(altTabMon->altsnext[i]->mon->num > altTabMon->altsnext[i + 1]->mon->num) ||
						(
							altTabMon->altsnext[i]->mon == altTabMon->altsnext[i + 1]->mon &&
							strcmpbynum(altTabMon->altsnext[i]->name, altTabMon->altsnext[i + 1]->name) > 0
						)
					) {
						done = 0;
						c = altTabMon->altsnext[i];
						altTabMon->altsnext[i] = altTabMon->altsnext[i + 1];
						altTabMon->altsnext[i + 1] = c;
					}
				}
			}
		}

		drawTab(altTabMon, (altTabMon->isAlt & ALTTAB_OFFSET_MENU) ? 0 : 1, 1);
		int x, y;
		int grabbed;
		if ((altTabMon->isAlt & ALTTAB_MOUSE)) {
			grabbed = grabinputs(1, 1, cursor[(altTabMon->vTabs < altTabMon->nTabs) ? CurScroll : CurNormal]->cursor);
			if (grabbed) {
				getrootptr(&x, &y);
				direction = 0;
			}
		}
		else grabbed = grabinputs(1, 1, cursor[CurBusy]->cursor);

		XEvent event;
		if (grabbed == 0) {
			altTabEnd();
		} else {

			struct timespec starttime;
			if (altTabMon->isAlt & ALTTAB_MOUSE) {
				// record point in time for button release test;
				clock_gettime(CLOCK_REALTIME, &starttime);
			}

			#if PATCH_SHOW_DESKTOP
			for (m = mons; m; m = m->next)
				if ((altTabMon->isAlt & ALTTAB_ALL_MONITORS) || m == altTabMon)
					m->altTabDesktop = m->showdesktop;
			#endif // PATCH_SHOW_DESKTOP

			#if PATCH_FOCUS_BORDER || PATCH_FOCUS_PIXEL
			if (focuswin && (altTabMon->isAlt & ALTTAB_SELMON_MASK) && selmon->sel)
				drawfocusborder(1);
			#endif // PATCH_FOCUS_BORDER || PATCH_FOCUS_PIXEL

			if (!(altTabMon->isAlt & ALTTAB_MOUSE) || !(altTabMon->isAlt & ALTTAB_OFFSET_MENU)) {
				altTabMon->highlight = altTabMon->sel;
				if (altTabMon->isAlt & (ALTTAB_SORTED | ALTTAB_SORTED_BY_MONITOR)) {
					for (int i = 0; i < listIndex; i++)
						if (altTabMon->altsnext[i] == altTabMon->sel || altTabMon->altsnext[i] == selmon->sel) {
							altTabMon->altTabN = i;
							drawTab(altTabMon, 1, 0);
							break;
						}
				}
				else
					altTab(direction, 1);
			}

			Time lasttime = 0;
			#if PATCH_MOUSE_POINTER_WARPING
			int isAltMouse = (altTabMon->isAlt & ALTTAB_MOUSE);
			#if PATCH_MOUSE_POINTER_WARPING_SMOOTH
			int smoothwarp = isAltMouse;
			#endif // PATCH_MOUSE_POINTER_WARPING_SMOOTH
			#endif // PATCH_MOUSE_POINTER_WARPING

			XButtonPressedEvent bev = { 0 };
			XKeyEvent kev = { 0 };
			while (grabbed) {
				XNextEvent(dpy, &event);
				if (event.type == KeyPress || event.type == KeyRelease || event.type == MotionNotify || event.type == ButtonPress || event.type == ButtonRelease) {
					if (altTabMon->isAlt & ALTTAB_MOUSE) {
						if (event.type == MotionNotify) {
							if ((event.xmotion.time - lasttime) <= (1000 / 60))
								continue;
							lasttime = event.xmotion.time;

							if (event.xmotion.x >= altTabMon->tx && event.xmotion.x <= (altTabMon->tx + altTabMon->maxWTab) &&
								event.xmotion.y >= altTabMon->ty && event.xmotion.y <= (altTabMon->ty + altTabMon->maxHTab)) {

									int yy = (event.xmotion.y - altTabMon->ty - 1) / altTabMon->tih;
									if (yy > altTabMon->nTabs - 1)
										yy = altTabMon->nTabs - 1;
									if (altTabMon->isAlt & ALTTAB_BOTTOMBAR)
										yy = (altTabMon->nTabs - 1 - yy);
									if (yy != altTabMon->altTabIndex || !altTabActive)
										altTab(yy - altTabMon->altTabIndex, 0);
							}
							else if (altTabActive) {
								// not highlighted;
								drawTab(altTabMon, 0, 0);
							}
						}
						else if (event.type == ButtonPress || event.type == ButtonRelease) {
							if (event.type == ButtonRelease) {
								struct timespec reltime;
								clock_gettime(CLOCK_REALTIME, &reltime);
								double result = (reltime.tv_sec - starttime.tv_sec) * 1e6 + (reltime.tv_nsec - starttime.tv_nsec) / 1e3;
								// only consider button release as functional when it's been held for a period;
								if (result < 500000)
									continue;
							}
							bev = event.xbutton;
							if (bev.button == 1 || bev.button == 3) {
								if (bev.x < altTabMon->tx || bev.x > (altTabMon->tx + altTabMon->maxWTab) ||
									bev.y < altTabMon->ty || bev.y > (altTabMon->ty + altTabMon->maxHTab)) {
									highlight(NULL);
									break;
								}
								else {
									#if PATCH_MOUSE_POINTER_WARPING
									#if PATCH_MOUSE_POINTER_WARPING_SMOOTH
									smoothwarp = (bev.button == 3) ? 1 : 0;
									#endif // PATCH_MOUSE_POINTER_WARPING_SMOOTH
									#endif // PATCH_MOUSE_POINTER_WARPING
									break;
								}
							}
							else if (
								(bev.button == 4 || bev.button == 5) &&
								(bev.x >= altTabMon->tx && bev.x <= (altTabMon->tx + altTabMon->maxWTab) &&
									bev.y >= altTabMon->ty && bev.y <= (altTabMon->ty + altTabMon->maxHTab))
							) {
								if (bev.button == 5 && altTabMon->altTabN < (altTabMon->nTabs - 1)) altTab(+1, 0);
								if (bev.button == 4 && altTabMon->altTabN > 0) altTab(-1, 0);
							}
						}
						else if (event.type == KeyPress) {
							kev = event.xkey;
							if (kev.keycode == tabEndKey) {
								highlight(NULL);
								#if PATCH_MOUSE_POINTER_WARPING
								#if PATCH_MOUSE_POINTER_WARPING_SMOOTH
								smoothwarp = 0;
								#endif // PATCH_MOUSE_POINTER_WARPING_SMOOTH
								#endif // PATCH_MOUSE_POINTER_WARPING
								break;
							}
							else if (kev.keycode == 116 && (altTabMon->altTabN < (altTabMon->nTabs - 1) || !altTabActive)) altTab(+1, 0);
							else if (kev.keycode == 111 && (altTabMon->altTabN > 0 || !altTabActive)) altTab(-1, 0);
							else if (kev.keycode == 104 || kev.keycode == 36) {
								if (altTabMon->highlight == altTabMon->altTabSel || !altTabActive)
									highlight(NULL);
								break;
							}
							else {
								DEBUG("kev.keycode == %i\n", kev.keycode);
							}
						}

					} else {
						kev = event.xkey;
						if (event.type == KeyRelease && event.xkey.keycode == tabModKey) { // if modifier key is released break cycle;
							break;
						} else if (event.xkey.keycode == tabModBackKey) {
							direction = (event.type == KeyPress) ? -1 : 1;
						} else if (event.type == KeyPress) {
							if (event.xkey.keycode == tabEndKey) {
								highlight(NULL);
								break;
							}
							else if (altTabMon->nTabs > 1) {
								if ((event.xkey.keycode == tabCycleKey && !(altTabMon->isAlt & ALTTAB_SAME_CLASS))
								||	(event.xkey.keycode == tabCycleClassKey && (altTabMon->isAlt & ALTTAB_SAME_CLASS))) { // if XK_s is pressed move to the next window;
									altTab(direction, 0);
								}
							}
						}
					}
				}
			}

			// make the alt-tab window disappear;
			quietunmap(altTabMon->tabwin);

			altTabMon->isAlt |= ALTTAB_SYSTEM_RESERVED;
			// redraw the bar;
			drawbar(altTabMon, False);

			// if the triggering event was a button or key press
			// we want to wait for the following button or key release
			// to prevent the release event passing through the active client;
			if (event.type == ButtonPress)
				same = 1;
			else if (event.type == KeyPress && kev.keycode) {
				same = 2;
			}
			else
				same = 0;

			// quit waiting for the button or key release event
			// prematurely if we get a critical event;
			while (same) {
				XNextEvent(dpy, &event);
				switch (event.type) {
					case ButtonRelease:
						if (same == 1 && event.xbutton.button == bev.button)
							same = 0;
						break;
					case KeyRelease:
						if (same == 2 && event.xkey.keycode == kev.keycode)
							same = 0;
						break;
					case CreateNotify:
					case MapNotify:
					case UnmapNotify:
					case DestroyNotify:
						XPutBackEvent(dpy, &event);
						same = 0;
						break;
				}
			}

			if (!(c = altTabMon->highlight))
				c = altTabMon->sel;
			same = (altTabMon->sel == c);

			#if PATCH_SHOW_DESKTOP
			if (showdesktop)
				for (m = mons; m; m = m->next)
					if ((altTabMon->isAlt & ALTTAB_ALL_MONITORS) || m == altTabMon)
					{
						if (c && c->mon == m
							#if PATCH_SHOW_DESKTOP_WITH_FLOATING
							&& (!showdesktop_floating
								|| !c->isfloating
								#if PATCH_MODAL_SUPPORT
								|| (c->ismodal && c->parent && !c->parent->isfloating)
								#endif // PATCH_MODAL_SUPPORT
							)
							#endif // PATCH_SHOW_DESKTOP_WITH_FLOATING
						)
							m->altTabDesktop = (c->isdesktop || c->ondesktop);
						else {
							m->showdesktop = m->altTabDesktop;
							if (m == altTabMon)
								focus(NULL, 0);
						}
					}
			#endif // PATCH_SHOW_DESKTOP

			// if current client is fullscreen, check if we should un-fullscreen it
			// before activating the newly selected client;
			if (altTabMon->isAlt & ALTTAB_SELMON_MASK
				&& c
				&& c->mon->sel
				&& c->mon->sel != c
				&& c->mon->sel->isfullscreen
				#if PATCH_FLAG_FAKEFULLSCREEN
				&& c->mon->sel->fakefullscreen != 1
				#endif // PATCH_FLAG_FAKEFULLSCREEN
				&& !c->isfloating
				&& (
					!c->isfullscreen
					#if PATCH_FLAG_FAKEFULLSCREEN
					|| c->fakefullscreen == 1
					#endif // PATCH_FLAG_FAKEFULLSCREEN
					)
				) {
				#if PATCH_FLAG_GAME
				if (c->mon->sel->isgame) {
					#if PATCH_FLAG_GAME_STRICT
					unfocus(c->mon->sel, 1 | (c->mon != selm ? (1 << 1) : 0));
					#else // NO PATCH_FLAG_GAME_STRICT
					unfocus(c->mon->sel, 1);
					#endif // PATCH_FLAG_GAME_STRICT
				}
				else
				#endif // PATCH_FLAG_GAME
				{
					c->mon->sel->lostfullscreen = 1;
					setfullscreen(c->mon->sel, 0);
				}
			}

			if (altTabMon->isAlt & ALTTAB_MOUSE) {
				if (c) {
					if (altTabMon->isAlt & ALTTAB_SELMON_MASK)
						selmon = c->mon;
					if (!ISVISIBLE(c)
						#if PATCH_SHOW_DESKTOP
						&& !(c->isdesktop || c->ondesktop)
						#endif // PATCH_SHOW_DESKTOP
					)
						viewmontag(c->mon, c->tags, 0);
				}
				else
					selmon = selm;
				selmon->altTabSelTags = 0;
			}
			else {
				for (m = (altTabMon->isAlt & ALTTAB_ALL_MONITORS) ? mons : altTabMon; m; m = (altTabMon->isAlt & ALTTAB_ALL_MONITORS) ? m->next : NULL) {
					if (m->altTabSelTags) {
						if (c && c->mon == m && !ISVISIBLEONTAG(c, m->altTabSelTags))
							viewmontag(m, c->tags, 1);
						else
							viewmontag(m, m->altTabSelTags, 1);
					}
					else if (c && c->mon == m) {
						if (!ISVISIBLE(c))
							viewmontag(m, c->tags, 1);
					}
					else
						restack(m);
					m->altTabSelTags = 0;
				}
				selmon = (c && (altTabMon->isAlt & ALTTAB_SELMON_MASK) ? c->mon : selm);

				#if PATCH_MOUSE_POINTER_WARPING || PATCH_FOCUS_FOLLOWS_MOUSE
				// if the client to be selected is the same as before the alt-tab,
				// then restore the mouse pointer to its starting position;
				if (same && px && (altTabMon->isAlt & ALTTAB_SELMON_MASK))
					XWarpPointer(dpy, None, root, 0, 0, 0, 0, px, py);
				#endif // PATCH_MOUSE_POINTER_WARPING || PATCH_FOCUS_FOLLOWS_MOUSE
			}
			altTabEnd(); // end the alt-tab functionality;

			XUngrabKeyboard(dpy, CurrentTime); // stop taking all input from keyboard;

			if (c) {
				#if PATCH_FLAG_HIDDEN
				if (c->ishidden) {
					sethidden(c, False, False);
					#if PATCH_PERSISTENT_METADATA
					setclienttagprop(c);
					#endif // PATCH_PERSISTENT_METADATA
					arrangemon(c->mon);
				}
				#endif // PATCH_FLAG_HIDDEN
				if (mon_mask)
					focus(c, 1);
				else {
					c->mon->sel = c;
					detachstackex(c);
					attachstack(c);
					arrange(c->mon);
				}
			}
			#if PATCH_MOUSE_POINTER_WARPING
			//if ((!isAltMouse || !same) && mon_mask)
			if (mon_mask && !ismouseoverclient(selmon->sel) && !pointoverbar(selmon, -1, -1, 1))
				#if PATCH_MOUSE_POINTER_WARPING_SMOOTH
				warptoclient(selmon->sel, smoothwarp, 0);
				#else // NO PATCH_MOUSE_POINTER_WARPING_SMOOTH
				warptoclient(selmon->sel, 0);
				#endif // PATCH_MOUSE_POINTER_WARPING_SMOOTH
			#elif PATCH_FOCUS_FOLLOWS_MOUSE
			if (mon_mask && !ismouseoverclient(selmon->sel) && !pointoverbar(selmon, -1, -1, 1)) {
				if (
					selmon->sel->isfullscreen
					#if PATCH_FLAG_FAKEFULLSCREEN
					&& selmon->sel->fakefullscreen != 1
					#endif // PATCH_FLAG_FAKEFULLSCREEN
					)
					XWarpPointer(dpy, None, root, 0, 0, 0, 0, selmon->mx + (selmon->mw / 2), selmon->my + (selmon->mh / 2));
				else
					XWarpPointer(dpy, None, root, 0, 0, 0, 0, selmon->sel->x + (selmon->sel->w / 2), selmon->sel->y + (selmon->sel->h / 2));
			}
			#endif // PATCH_MOUSE_POINTER_WARPING || PATCH_FOCUS_FOLLOWS_MOUSE
			XUngrabPointer(dpy, CurrentTime);
		}
	}
}
#endif // PATCH_ALTTAB

void
tag(const Arg *arg)
{
	#if PATCH_FOCUS_FOLLOWS_MOUSE
	checkmouseoverclient();
	#endif // PATCH_FOCUS_FOLLOWS_MOUSE
	Client *c;
	if (!(c = selmon->sel) || !(arg->ui & TAGMASK)
		#if PATCH_FLAG_IGNORED
		|| c->isignored
		#endif // PATCH_FLAG_IGNORED
		#if PATCH_FLAG_PANEL
		|| c->ispanel
		#endif // PATCH_FLAG_PANEL
		#if PATCH_SHOW_DESKTOP
		|| c->isdesktop
		#endif // PATCH_SHOW_DESKTOP
		)
		return;

	#if PATCH_FLAG_FOLLOW_PARENT
	if (c->followparent && c->parent && !c->toplevel && !c->fosterparent)
		return;
	#endif // PATCH_FLAG_FOLLOW_PARENT

	c->tags = arg->ui & TAGMASK;
	if (c->isfullscreen
		#if PATCH_FLAG_FAKEFULLSCREEN
		&& c->fakefullscreen != 1
		#endif // PATCH_FLAG_FAKEFULLSCREEN
	) {
		for (int i = 0; i < LENGTH(tags); i++)
			if (c->tags & (1 << i))
				selmon->focusontag[i] = c;
	}
	#if PATCH_PERSISTENT_METADATA
	setclienttagprop(c);
	#endif // PATCH_PERSISTENT_METADATA

	#if PATCH_MODAL_SUPPORT
	if (c->ismodal) {
		for (Client *p = c->snext; p; p = p->snext)
			if (p->ultparent == c->ultparent && ISVISIBLE(p)) {
				p->tags = arg->ui & TAGMASK;
				#if PATCH_PERSISTENT_METADATA
				setclienttagprop(p);
				#endif // PATCH_PERSISTENT_METADATA
			}
	}
	#if PATCH_FLAG_FOLLOW_PARENT
	else
	#endif // PATCH_FLAG_FOLLOW_PARENT
	#endif // PATCH_MODAL_SUPPORT
	#if PATCH_FLAG_FOLLOW_PARENT
	tagsatellites(c);
	#endif // PATCH_FLAG_FOLLOW_PARENT

	focus(NULL, 0);
	arrange(selmon);
	if (viewontag && ((arg->ui & TAGMASK) != TAGMASK))
		view(arg);
	if (!ISVISIBLE(c))
		if ((c = guessnextfocus(c, selmon)))
			focus(c, 0);
}

void
tagmon(const Arg *arg)
{
	#if PATCH_FOCUS_FOLLOWS_MOUSE
	checkmouseoverclient();
	#endif // PATCH_FOCUS_FOLLOWS_MOUSE
	Client *c = selmon->sel;
	if (!c || !mons->next
		#if PATCH_FLAG_IGNORED
		|| c->isignored
		#endif // PATCH_FLAG_IGNORED
		#if PATCH_FLAG_PANEL
		|| c->ispanel
		#endif // PATCH_FLAG_PANEL
		#if PATCH_SHOW_DESKTOP
		|| c->isdesktop || c->ondesktop
		#endif // PATCH_SHOW_DESKTOP
		)
		return;

	Client *sel = c;
	#if PATCH_MODAL_SUPPORT
	if (c->ismodal) {
		for (
			c = c->parent;
			c && c->ismodal && c->parent && c->mon == c->parent->mon;
			c = c->parent
		);
		if (!c)
			return;
	}
	#if PATCH_FLAG_FOLLOW_PARENT
	else
	#endif // PATCH_FLAG_FOLLOW_PARENT
	#endif // PATCH_MODAL_SUPPORT
	#if PATCH_FLAG_FOLLOW_PARENT
	if (c->followparent && !c->toplevel && !c->fosterparent) {
		for (
			c = c->parent;
			c && c->followparent && c->parent && c->mon == c->parent->mon;
			c = c->parent
		);
		if (!c)
			return;
	}
	#endif // PATCH_FLAG_FOLLOW_PARENT

	Monitor *m = dirtomon(arg->i);

	if (c->isfloating && !c->parent && (
		!c->isfullscreen
		#if PATCH_FLAG_FAKEFULLSCREEN
		|| c->fakefullscreen == 1
		#endif // PATCH_FLAG_FAKEFULLSCREEN
	)) {
		float sfx = (float) (c->x - c->mon->wx + c->bw + c->w/2) / (c->mon->ww/2);
		float sfy = (float) (c->y - c->mon->wy + c->bw + c->h/2) / (c->mon->wh/2);
		c->x = MAX(
				MIN(
					((sfx * m->ww/2) + m->wx - c->w / 2),
					(m->wx + m->ww - c->w)
				),
				m->wx
			);
		c->y = MAX(
				MIN(
					((sfy * m->wh/2) + m->wy - c->h / 2),
					(m->wy + m->wh - c->h)
				),
				m->wy
			);
	}

	sendmon(c, m, sel, 1);
}

void
togglebar(const Arg *arg)
{
	#if PATCH_FOCUS_FOLLOWS_MOUSE
	checkmouseovermonitor(selmon);
	#endif // PATCH_FOCUS_FOLLOWS_MOUSE
	togglebarex(selmon);
	arrange(selmon);
	drawbar(selmon, 0);
}

void
togglebarex(Monitor *m)
{
	#if PATCH_PERTAG
	m->showbar = m->pertag->showbars[m->pertag->curtag] = !m->showbar;
	#else // NO PATCH_PERTAG
	m->showbar = !m->showbar;
	#endif // PATCH_PERTAG
	showhidebar(m);
}

#if DEBUGGING
void
toggledebug(const Arg *arg)
{
	debug_sensitivity_on ^= 1;
	DEBUG("set debug_sensitivity_on to %i\n", debug_sensitivity_on);
}
#endif // DEBUGGING

#if PATCH_SHOW_DESKTOP
void
toggledesktop(const Arg *arg)
{
	if (!showdesktop)
		return;

	#if PATCH_FOCUS_FOLLOWS_MOUSE
	checkmouseovermonitor(selmon);
	#endif // PATCH_FOCUS_FOLLOWS_MOUSE
	int i;
	#if PATCH_SHOW_DESKTOP_ONLY_WHEN_ACTIVE
	int c = 0;
	#endif // PATCH_SHOW_DESKTOP_ONLY_WHEN_ACTIVE
	Client *d = NULL;
	Monitor *m = selmon;
	if (arg)
		if ((i = arg->i) >= 0) {
			for (m = mons; m && m->num != i; m = m->next);
			if (!m)
				m = selmon;
		}

	#if PATCH_SHOW_DESKTOP_ONLY_WHEN_ACTIVE || PATCH_SHOW_DESKTOP_UNMANAGED
	d = getdesktopclient(m, &c);
	#endif // PATCH_SHOW_DESKTOP_ONLY_WHEN_ACTIVE || PATCH_SHOW_DESKTOP_UNMANAGED

	#if PATCH_SHOW_DESKTOP_ONLY_WHEN_ACTIVE
	if (showdesktop_when_active) {
		if ((d && m->showdesktop && !c) || (!m->showdesktop && (
			#if PATCH_SHOW_DESKTOP_UNMANAGED
			showdesktop_unmanaged ? desktopwin == None :
			#endif // PATCH_SHOW_DESKTOP_UNMANAGED
			!d
		)))
			return;
	}
	#endif // PATCH_SHOW_DESKTOP_ONLY_WHEN_ACTIVE
	#if PATCH_SHOW_DESKTOP_UNMANAGED
	if (m->showdesktop && showdesktop_unmanaged && desktopwin && !c)
		return;
	#endif // PATCH_SHOW_DESKTOP_UNMANAGED

	m->showdesktop ^= 1;
	if (m->sel && m->sel->isdesktop)
		m->sel = NULL;
	arrange(m);

	if (!m->showdesktop)
		d = NULL;

	focus(d, 0);
}
#endif // PATCH_SHOW_DESKTOP

#if PATCH_FLAG_FAKEFULLSCREEN
void
togglefakefullscreen(const Arg *arg)
{
	#if PATCH_FOCUS_FOLLOWS_MOUSE
	checkmouseoverclient();
	#endif // PATCH_FOCUS_FOLLOWS_MOUSE
	Client *c = selmon->sel;
	if (!c)
		return;
	#if PATCH_FLAG_NEVER_FULLSCREEN
	if (c->neverfullscreen)
		return;
	#endif // PATCH_FLAG_NEVER_FULLSCREEN
	#if PATCH_FLAG_GAME
	if (c->isgame)
		return;
	#endif // PATCH_FLAG_GAME
	#if PATCH_SHOW_DESKTOP
	if (c->isdesktop || c->ondesktop)
		return;
	#endif // PATCH_SHOW_DESKTOP

	#if PATCH_FOCUS_FOLLOWS_MOUSE
	int prevfullscreen = c->isfullscreen;
	int prevfakefullscreen = c->fakefullscreen;
	#endif // PATCH_FOCUS_FOLLOWS_MOUSE
	if(c->isfullscreen) {
		if (c->fakefullscreen != 1) { // exit fullscreen --> fake fullscreen
			c->fakefullscreen = 2;
			setfullscreen(c, 0);
		} else if (c->fakefullscreen == 1) {
			c->fakefullscreen = 2;
			setfullscreen(c, 1);
		}
	}
	else if (c->fakefullscreen == 1)
		c->fakefullscreen = 0;
	else
		c->fakefullscreen = 1;

	#if PATCH_PERSISTENT_METADATA
	setclienttagprop(c);
	#endif // PATCH_PERSISTENT_METADATA

	#if PATCH_FOCUS_FOLLOWS_MOUSE
	if((c->fakefullscreen == 1 && c->isfullscreen && !prevfakefullscreen) || (!c->isfullscreen && prevfullscreen && !prevfakefullscreen))
		#if PATCH_MOUSE_POINTER_WARPING
		#if PATCH_MOUSE_POINTER_WARPING_SMOOTH
		warptoclient(c, 0, 0);
		#else // NO PATCH_MOUSE_POINTER_WARPING_SMOOTH
		warptoclient(c, 0);
		#endif // PATCH_MOUSE_POINTER_WARPING_SMOOTH
		#else // NO PATCH_MOUSE_POINTER_WARPING
		XWarpPointer(dpy, None, root, 0, 0, 0, 0, c->x + (c->w / 2), c->y + (c->h / 2));
		#endif // PATCH_MOUSE_POINTER_WARPING
	#endif // PATCH_FOCUS_FOLLOWS_MOUSE
}
#endif // PATCH_FLAG_FAKEFULLSCREEN

void
togglefloating(const Arg *arg)
{
	#if PATCH_FOCUS_FOLLOWS_MOUSE
	checkmouseoverclient();
	#endif // PATCH_FOCUS_FOLLOWS_MOUSE
	togglefloatingex(selmon->sel);
}
void
togglefloatingex(Client *c)
{
	int vis;
	if (!c || c->isfixed)
		return;
	#if PATCH_SHOW_DESKTOP
	if (c->isdesktop || c->ondesktop)
		return;
	#endif // PATCH_SHOW_DESKTOP
	if (c->isfullscreen
		#if PATCH_FLAG_FAKEFULLSCREEN
		&& c->fakefullscreen != 1
		#endif // PATCH_FLAG_FAKEFULLSCREEN
		) /* no support for fullscreen windows */
		return;
	c->isfloating = !c->isfloating;
	vis = ISVISIBLE(c);
	if (c->isfloating) {
		if (c->sfx == -1 && c->sfy == -1 && c->sfw == -1 && c->sfh == -1) {
			if (solitary(c) || c->mon->lt[c->mon->sellt]->arrange == monocle ) {
				c->w -= c->bw*2;
				c->h -= c->bw*2;
			}
		}
		else {
			if (c->sfw != -1)
				c->w = c->sfw;
			if (c->sfh != -1)
				c->h = c->sfh;
			if (c->sfx != -1) {
				c->x = c->sfx;
				if (c->x < c->mon->wx)
					c->x = c->mon->wx;
				else if (c->x > c->mon->wx + c->mon->ww)
					c->x = c->mon->wx + c->mon->ww - MIN(c->w, c->mon->ww);
			}
			else if (c->x + c->w > c->mon->ww) {
				if (c->w == c->sfw)
					c->x = c->mon->wx + c->mon->ww - c->w - c->bw*2;
				else
					c->w = c->mon->wx + c->mon->ww - c->x - c->bw*2;
			}
			if (c->sfy != -1) {
				c->y = c->sfy;
				if (c->y < c->mon->wy)
					c->y = c->mon->wy;
				else if (c->y > c->mon->wy + c->mon->wh)
					c->y = c->mon->wy + c->mon->wh - MIN(c->h, c->mon->wh);
			}
			else if (c->y + c->h > c->mon->wh) {
				if (c->w == c->sfw)
					c->y = c->mon->wy + c->mon->wh - c->h - c->bw*2;
				else
					c->h = c->mon->wy + c->mon->wh - c->y - c->bw*2;
			}
		}
		if (vis) {
			resize(c, c->x, c->y, c->w, c->h, False);	// restore last known float dimensions
			#if PATCH_CLASS_STACKING
			if (c->mon->class_stacking && c == c->mon->sel && c->isstackhead
				#if PATCH_FLAG_HIDDEN
				&& !c->ishidden
				#endif // PATCH_FLAG_HIDDEN
				#if PATCH_FLAG_IGNORED
				&& !c->isignored
				#endif // PATCH_FLAG_IGNORED
				#if PATCH_FLAG_PANEL
				&& !c->ispanel
				#endif // PATCH_FLAG_PANEL
				)
				XSetWindowBorder(dpy, c->win, scheme[SchemeSel][ColBorder].pixel);
			#endif // PATCH_CLASS_STACKING
		}
	}
	else {
		// save last known float dimensions;
		c->sfx = c->x;
		c->sfy = c->y;
		c->sfw = c->w;
		c->sfh = c->h;
	}
	if (!vis)
		XMoveWindow(dpy, c->win, WIDTH(c) * -2, c->y);
	#if PATCH_CROP_WINDOWS
	if (!c->isfloating && c->crop)
		cropdelete(c);
	#endif // PATCH_CROP_WINDOWS
	if (vis)
		arrange(c->mon);
	#if PATCH_PERSISTENT_METADATA
	setclienttagprop(c);
	#endif // PATCH_PERSISTENT_METADATA
	#if PATCH_FOCUS_BORDER || PATCH_FOCUS_PIXEL
	if (vis && !c->isfloating && focuswin)
		focus(NULL, 0);
	#endif // PATCH_FOCUS_BORDER || PATCH_FOCUS_PIXEL
}

void
togglefullscreen(const Arg *arg)
{
	#if PATCH_FOCUS_FOLLOWS_MOUSE
	checkmouseoverclient();
	#endif // PATCH_FOCUS_FOLLOWS_MOUSE
	Client *c = selmon->sel;
	if (!c)
		return;
	#if PATCH_SHOW_DESKTOP
	if (c->isdesktop || c->ondesktop)
		return;
	#endif // PATCH_SHOW_DESKTOP

	#if PATCH_FLAG_FAKEFULLSCREEN
	if (c->fakefullscreen == 1) { // fake fullscreen --> fullscreen
		c->fakefullscreen = 2;
		setfullscreen(c, 1);
	} else
	#endif // PATCH_FLAG_FAKEFULLSCREEN
		setfullscreen(c, !c->isfullscreen);
}

#if PATCH_MIRROR_LAYOUT
void
togglemirror(const Arg *arg)
{
	#if PATCH_FOCUS_FOLLOWS_MOUSE
	checkmouseovermonitor(selmon);
	#endif // PATCH_FOCUS_FOLLOWS_MOUSE
	if (!selmon->lt[selmon->sellt]->arrange || selmon->lt[selmon->sellt]->arrange == monocle)
		return;

	#if PATCH_MOUSE_POINTER_WARPING || PATCH_FOCUS_FOLLOWS_MOUSE
	Client *c = selmon->sel;
	if (c && !c->isfloating) {
		int px, py;
		unsigned int cw, ch;
		float sfw, sfh;
		int ok = getrelativeptr(c, &px, &py);
		cw = c->w;
		ch = c->h;

		selmon->mirror ^= 1;
		arrange(selmon);

		if (!ok)
			return;
		sfw = ((float) c->w / cw);
		sfh = ((float) c->h / ch);
		XWarpPointer(dpy, None, c->win, 0, 0, 0, 0, (int)(px*sfw), (int)(py*sfh));
	}
	else
	#endif // PATCH_MOUSE_POINTER_WARPING || PATCH_FOCUS_FOLLOWS_MOUSE
	{
		selmon->mirror ^= 1;
		arrange(selmon);
	}
}
#endif // PATCH_MIRROR_LAYOUT

#if PATCH_PAUSE_PROCESS
void
togglepause(const Arg *arg)
{
	#if PATCH_FOCUS_FOLLOWS_MOUSE
	checkmouseoverclient();
	#endif // PATCH_FOCUS_FOLLOWS_MOUSE
	Client *c;
	if (!(c = selmon->sel) || !c->pid)
		return;

	c->paused ^= 1;
	if (c->paused
		#if PATCH_HANDLE_SIGNALS
		&& !closing
		#endif // PATCH_HANDLE_SIGNALS
	)
		kill (c->pid, SIGSTOP);
	else
		kill (c->pid, SIGCONT);
}
#endif // PATCH_PAUSE_PROCESS

#if DEBUGGING
void
toggleskiprules(const Arg *arg)
{
	skip_rules = !skip_rules;
}
#endif // DEBUGGING

#if PATCH_VIRTUAL_MONITORS
void
togglesplit(const Arg *arg)
{
	#if PATCH_FOCUS_FOLLOWS_MOUSE
	checkmouseovermonitor(selmon);
	#endif // PATCH_FOCUS_FOLLOWS_MOUSE
	Monitor *m;
	if (!(m = selmon->pmon->mon1) || !m->split)
		return;
	m->enablesplit ^= 1;
	if (!updatevirtualmonitors())
		return;
	updatebars();
	updateclientmonitors();
	for (m = mons; m; m = m->next) {
		for (Client *c = m->clients; c; c = c->next)
			if (c->isfullscreen
				#if PATCH_FLAG_FAKEFULLSCREEN
				&& c->fakefullscreen != 1
				#endif // PATCH_FLAG_FAKEFULLSCREEN
				)
				resizeclient(c, m->mx, m->my, m->mw, m->mh, 0);
		resizebarwin(m);
	}
	focus(NULL, 0);
	arrange(NULL);

	#if PATCH_FOCUS_FOLLOWS_MOUSE
	if (!selmon->sel)
		XWarpPointer(dpy, None, root, 0, 0, 0, 0, selmon->wx + (selmon->ww / 2), selmon->wy + (selmon->wh / 2));
	else {
		#if PATCH_MOUSE_POINTER_WARPING
		#if PATCH_MOUSE_POINTER_WARPING_SMOOTH
		warptoclient(selmon->sel, 1, 0);
		#else // NO PATCH_MOUSE_POINTER_WARPING_SMOOTH
		warptoclient(selmon->sel, 0);
		#endif // PATCH_MOUSE_POINTER_WARPING_SMOOTH
		#else
		if (selmon->sel->isfullscreen
			#if PATCH_FLAG_FAKEFULLSCREEN
			&& selmon->sel->fakefullscreen != 1
			#endif // PATCH_FLAG_FAKEFULLSCREEN
			)
			XWarpPointer(dpy, None, root, 0, 0, 0, 0, selmon->mx + (selmon->mw / 2), selmon->my + (selmon->mh / 2));
		else
			XWarpPointer(dpy, None, root, 0, 0, 0, 0, selmon->sel->x + (selmon->sel->w / 2), selmon->sel->y + (selmon->sel->h / 2));
		#endif // PATCH_MOUSE_POINTER_WARPING
	}
	#endif // PATCH_FOCUS_FOLLOWS_MOUSE
}
#endif // PATCH_VIRTUAL_MONITORS

#if PATCH_CLASS_STACKING
void
togglestacking(const Arg *arg)
{
	#if PATCH_FOCUS_FOLLOWS_MOUSE
	checkmouseovermonitor(selmon);
	#endif // PATCH_FOCUS_FOLLOWS_MOUSE
	#if PATCH_PERTAG
	selmon->pertag->class_stacking[selmon->pertag->curtag] = !selmon->pertag->class_stacking[selmon->pertag->curtag];
	#endif // PATCH_PERTAG
	selmon->class_stacking = !selmon->class_stacking;
	arrange(selmon);
}
#endif // PATCH_CLASS_STACKING

#if PATCH_FLAG_STICKY
void
togglesticky(const Arg *arg)
{
	#if PATCH_FOCUS_FOLLOWS_MOUSE
	checkmouseoverclient();
	#endif // PATCH_FOCUS_FOLLOWS_MOUSE
	Client *c = selmon->sel;
	if (!c)
		return;
	#if PATCH_SHOW_DESKTOP
	if (c->isdesktop || c->ondesktop)
		return;
	#endif // PATCH_SHOW_DESKTOP
	#if PATCH_MODAL_SUPPORT
	if (c->ismodal)
		return;
	#endif // PATCH_MODAL_SUPPORT
    setsticky(c, !c->issticky);
	arrange(selmon);
	if (!ISVISIBLE(c)) {
		c = guessnextfocus(c, selmon);
		if (c && c->mon != selmon)
			viewmontag(c->mon, c->tags, 0);
		focus(c, 0);
		#if PATCH_MOUSE_POINTER_WARPING
		if (selmon->sel)
			#if PATCH_MOUSE_POINTER_WARPING_SMOOTH
			warptoclient(selmon->sel, 1, 0);
			#else // NO PATCH_MOUSE_POINTER_WARPING_SMOOTH
			warptoclient(selmon->sel, 0);
			#endif // PATCH_MOUSE_POINTER_WARPING_SMOOTH
		#endif // PATCH_MOUSE_POINTER_WARPING
	}
}
#endif // PATCH_FLAG_STICKY

void
toggletag(const Arg *arg)
{
	#if PATCH_FOCUS_FOLLOWS_MOUSE
	checkmouseoverclient();
	#endif // PATCH_FOCUS_FOLLOWS_MOUSE
	toggletagex(selmon->sel, arg->ui);
}
void
toggletagex(Client *c, int tagmask)
{
	unsigned int newtags;

	if (!c
		#if PATCH_FLAG_IGNORED
		|| c->isignored
		#endif // PATCH_FLAG_IGNORED
		#if PATCH_FLAG_PANEL
		|| c->ispanel
		#endif // PATCH_FLAG_PANEL
		#if PATCH_SHOW_DESKTOP
		|| c->isdesktop || c->ondesktop
		#endif // PATCH_SHOW_DESKTOP
		)
		return;
	newtags = c->tags ^ (tagmask & TAGMASK);
	if (newtags) {
		c->tags = newtags;
		#if PATCH_PERSISTENT_METADATA
		setclienttagprop(c);
		#endif // PATCH_PERSISTENT_METADATA
		focus(NULL, 0);
		arrange(c->mon);
	}
}

void
toggleview(const Arg *arg)
{
	#if PATCH_FOCUS_FOLLOWS_MOUSE
	checkmouseovermonitor(selmon);
	#endif // PATCH_FOCUS_FOLLOWS_MOUSE
	toggleviewex(selmon, arg->ui);
}

void
toggleviewex(Monitor *m, int tagmask)
{
	unsigned int newtagset = m->tagset[m->seltags] ^ (tagmask & TAGMASK);
	#if PATCH_PERTAG
	int i;
	#endif // PATCH_PERTAG

	if (newtagset) {
		m->tagset[m->seltags] = newtagset;
		#if PATCH_PERTAG
		if (newtagset == ~0) {
			m->pertag->prevtag = m->pertag->curtag;
			m->pertag->curtag = 0;
		}

		/* test if the user did not select the same tag */
		if (!(newtagset & 1 << (m->pertag->curtag - 1))) {
			m->pertag->prevtag = m->pertag->curtag;
			for (i = 0; !(newtagset & 1 << i); i++) ;
			m->pertag->curtag = i + 1;
		}

		/* apply settings for this view */
		m->nmaster = m->pertag->nmasters[m->pertag->curtag];
		m->mfact = m->pertag->mfacts[m->pertag->curtag];
		m->mfact_def = m->pertag->mfacts_def[m->pertag->curtag];
		m->sellt = m->pertag->sellts[m->pertag->curtag];
		m->lt[m->sellt] = m->pertag->ltidxs[m->pertag->curtag][m->sellt];
		m->lt[m->sellt^1] = m->pertag->ltidxs[m->pertag->curtag][m->sellt^1];

		#if PATCH_ALT_TAGS
		m->alttagsquiet = m->pertag->alttagsquiet[m->pertag->curtag];
		#endif // PATCH_ALT_TAGS

		if (m->showbar != m->pertag->showbars[m->pertag->curtag])
			togglebarex(m);
		#endif // PATCH_PERTAG

		focus(NULL, 0);
		arrange(m);
	}
	#if PATCH_EWMH_TAGS
	if (m == selmon)
		updatecurrentdesktop();
	#endif // PATCH_EWMH_TAGS
}

#if PATCH_WINDOW_ICONS
#if PATCH_ALTTAB
void
freealticons(void)
{
	for (Monitor *m = mons; m; m = m->next)
		for (Client *c = m->clients; c; c = c->next)
			if (c->alticon) {
				XRenderFreePicture(dpy, c->alticon);
				c->alticon = None;
			}
}
#endif // PATCH_ALTTAB

void
freeicon(Client *c)
{
	if (c->icon) {
		XRenderFreePicture(dpy, c->icon);
		c->icon = None;
	}
	#if PATCH_ALTTAB
	if (c->alticon) {
		XRenderFreePicture(dpy, c->alticon);
		c->alticon = None;
	}
	#endif // PATCH_ALTTAB
	#if PATCH_WINDOW_ICONS_ON_TAGS
	if (c->tagicon) {
		XRenderFreePicture(dpy, c->tagicon);
		c->tagicon = None;
	}
	#endif // PATCH_WINDOW_ICONS_ON_TAGS
}
#endif // PATCH_WINDOW_ICONS

void
unfocus(Client *c, int setfocus)
{
	if (!c)
		return;

	#if PATCH_MOUSE_POINTER_HIDING
	showcursor();
	#endif // PATCH_MOUSE_POINTER_HIDING

	#if PATCH_FLAG_GAME
	if (c->isgame && c->isfullscreen && !MINIMIZED(c)) {
		destroybarrier();
		#if PATCH_FLAG_GAME_STRICT
		if (!c->isgamestrict && (setfocus & (1 << 1)))
			setclientstate(c, IconicState);
		else
		#endif // PATCH_FLAG_GAME_STRICT
		{
			minimize(c);
			#if PATCH_FLAG_GAME_STRICT
			if (game == c)
				game = NULL;
			#endif // PATCH_FLAG_GAME_STRICT
		}
	}
	#if PATCH_FLAG_GAME_STRICT
	//if (c->isgame && !c->isgamestrict && setfocus & (1 << 1))
	//	setfocus = 0;
	//else
		setfocus &= ~(1 << 1);
	#endif // PATCH_FLAG_GAME_STRICT
	#endif // PATCH_FLAG_GAME

	grabbuttons(c, 0);
	#if PATCH_CLIENT_OPACITY
	opacity(c, 0);
	#endif // PATCH_CLIENT_OPACITY

	//if (!solitary(c))
	XSetWindowBorder(dpy, c->win, scheme[SchemeNorm][ColBorder].pixel);
	#if PATCH_FOCUS_BORDER
	#if PATCH_SHOW_DESKTOP
	if (desktopvalid(c))
	#endif // PATCH_SHOW_DESKTOP
	XMoveResizeWindow(dpy, c->win, c->x, c->y, c->w, c->h);
	#elif PATCH_FOCUS_PIXEL
	fpcurpos = 0;
	#endif // PATCH_FOCUS_BORDER || PATCH_FOCUS_PIXEL

	publishwindowstate(c);	// reset window state;

	if (setfocus) {
		XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
		XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
	}
}

#if PATCH_FLAG_HIDDEN
void
unhidewin(const Arg *arg) {
	#if PATCH_FOCUS_FOLLOWS_MOUSE
	checkmouseovermonitor(selmon);
	#endif // PATCH_FOCUS_FOLLOWS_MOUSE
	Monitor *m = selmon;

	#if PATCH_SHOW_DESKTOP
	unsigned int n = 0;
	#endif // PATCH_SHOW_DESKTOP

	// show all hidden windows in this tag
	for (Client *c = m->clients; c; c = c->next)
		if (c->ishidden && ISVISIBLEONTAG(c, m->tagset[m->seltags])) {
			#if PATCH_SHOW_DESKTOP
			n++;
			#endif // PATCH_SHOW_DESKTOP
			sethidden(c, False, False);
			#if PATCH_PERSISTENT_METADATA
			setclienttagprop(c);
			#endif // PATCH_PERSISTENT_METADATA
		}

	#if PATCH_SHOW_DESKTOP
	if (m->showdesktop && n)
		m->showdesktop = 0;
	#endif // PATCH_SHOW_DESKTOP

	arrangemon(m);

	focus(NULL, 0);

	#if PATCH_FOCUS_FOLLOWS_MOUSE
	int x, y;
	if (getrootptr(&x, &y)) {
		Client *c = recttoclient(x, y, 1, 1, True);
		if (c != selmon->sel)
			focus(c, 0);
	}
	#endif // PATCH_FOCUS_FOLLOWS_MOUSE

}
#endif // PATCH_FLAG_HIDDEN

void
unmanage(Client *c, int destroyed, int cleanup)
{
	DEBUG("unmanage(\"%s\" Win: 0x%lx, destroyed=%i, cleanup=%i)\n", c->name, c->win, destroyed, cleanup);

	#if PATCH_CLIENT_OPACITY
	setopacity(c, 0);
	#endif // PATCH_CLIENT_OPACITY

	#if PATCH_CROP_WINDOWS
	if (c->crop)
		c = c->crop;
	#endif // PATCH_CROP_WINDOWS

	XWindowChanges wc;
	int wasfocused = (
		ISVISIBLE(c)
		#if PATCH_FLAG_PANEL
		&& !c->ultparent->ispanel
		#endif // PATCH_FLAG_PANEL
		&& c == selmon->sel
	);

	#if PATCH_FLAG_PAUSE_ON_INVISIBLE
	if (c->pauseinvisible == -1 && c->pid) {
		kill (c->pid, SIGCONT);
		c->pauseinvisible = 1;
		#if PATCH_PAUSE_PROCESS
		c->paused = 0;
		#endif // PATCH_PAUSE_PROCESS
	}
	#endif // PATCH_FLAG_PAUSE_ON_INVISIBLE

	#if PATCH_FLAG_PARENT
	// Clear JSON object references;
	c->parent_begins = NULL;
	c->parent_contains = NULL;
	c->parent_ends = NULL;
	c->parent_is = NULL;
	#endif // PATCH_FLAG_PARENT

	#if PATCH_FLAG_IGNORED
	if (c->isignored) {
		logdatetime(stderr);
		fprintf(stderr, "debug: Unmapping ignored: %s\n", c->name);
	}
	#endif // PATCH_FLAG_IGNORED

	#if PATCH_FLAG_TITLE
	if (c->displayname)
		uncompost(NULL, c->displayname);
	#endif // PATCH_FLAG_TITLE
	#if PATCH_SHOW_MASTER_CLIENT_ON_TAG
	if (c->dispclass)
		uncompost(NULL, c->dispclass);
	#endif // PATCH_SHOW_MASTER_CLIENT_ON_TAG
	#if PATCH_ALTTAB
	if (c->grpclass)
		uncompost(NULL, c->grpclass);
	#endif // PATCH_ALTTAB
	#if PATCH_WINDOW_ICONS && PATCH_WINDOW_ICONS_CUSTOM_ICONS
	if (c->icon_file)
		uncompost(NULL, c->icon_file);
	#endif // PATCH_WINDOW_ICONS && PATCH_WINDOW_ICONS_CUSTOM_ICONS
	#if PATCH_FLAG_PARENT
	if (!c->parent_condition_node) {
		if (c->parent_is)
			uncompost(c->parent_is, NULL);
		if (c->parent_begins)
			uncompost(c->parent_begins, NULL);
		if (c->parent_contains)
			uncompost(c->parent_contains, NULL);
		if (c->parent_ends)
			uncompost(c->parent_ends, NULL);
	}
	#endif // PATCH_FLAG_PARENT

	#if PATCH_TERMINAL_SWALLOWING
	if (c->swallowing) {
		if (c->mon->sel == c->swallowing)
			c->mon->sel = c;
		unswallow(c);
		return;
	}

	Client *s = swallowingclient(c->win);
	if (s) {
		#if PATCH_WINDOW_ICONS
		freeicon(s);
		#endif // PATCH_WINDOW_ICONS
		free(s->swallowing);
		s->swallowing = NULL;
		if (!cleanup) {
			arrange(s->mon);
			if (wasfocused)
				focus(NULL, 0);
		}
		return;
	}
	#endif // PATCH_TERMINAL_SWALLOWING

	#if PATCH_FLAG_GAME
	if (c->isgame)
		unminimize(c);
	#endif // PATCH_FLAG_GAME
	#if PATCH_FLAG_HIDDEN
	if (c->ishidden)
		sethidden(c, False, False);
	#endif // PATCH_FLAG_HIDDEN
	#if PATCH_FLAG_GAME
	if (c->isgame && c->isfullscreen
		#if PATCH_CONSTRAIN_MOUSE
		&& !constrained
		#endif // PATCH_CONSTRAIN_MOUSE
		)
		destroybarrier();
	#endif // PATCH_FLAG_GAME

	#if PATCH_MOUSE_POINTER_WARPING
	if (!c->isfloating && wasfocused)
		warptoclient_stop_flag = 1;
	#endif // PATCH_MOUSE_POINTER_WARPING

	#if PATCH_FOCUS_BORDER
	if (wasfocused && focuswin)
		drawfocusborder(1);
	#elif PATCH_FOCUS_PIXEL
	if (wasfocused && focuswin)
		fpcurpos = 0;
	#endif // PATCH_FOCUS_BORDER || PATCH_FOCUS_PIXEL

	detach(c);
	detachstack(c);
	removelinks(c);
	#if PATCH_WINDOW_ICONS
	freeicon(c);
	#endif // PATCH_WINDOW_ICONS

	#if PATCH_PERSISTENT_METADATA
	if (!cleanup)
		XDeleteProperty(dpy, c->win, netatom[NetClientInfo]);
	#endif // PATCH_PERSISTENT_METADATA

	if (!destroyed) {
		grabbuttons(c, 0);
		XSetWindowBorder(dpy, c->win, scheme[SchemeNorm][ColBorder].pixel);
		if (!cleanup && wasfocused) {
			XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
			XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
		}
		wc.border_width = c->oldbw;
		XGrabServer(dpy); /* avoid race conditions */
		XSetErrorHandler(xerrordummy);
		XSelectInput(dpy, c->win, NoEventMask);
		XConfigureWindow(dpy, c->win, CWBorderWidth, &wc); /* restore border */
		XUngrabButton(dpy, AnyButton, AnyModifier, c->win);
		publishwindowstate(c);	// reset window state;
		setclientstate(c, WithdrawnState);
		XSync(dpy, False);
		XSetErrorHandler(xerror);
		XUngrabServer(dpy);
	}

	while(!cleanup)
	{
		#if PATCH_SHOW_DESKTOP
		#if PATCH_SHOW_DESKTOP_ONLY_WHEN_ACTIVE
		if (showdesktop && showdesktop_when_active && c->isdesktop && c->mon->showdesktop)
			c->mon->showdesktop = 0;
		#endif // PATCH_SHOW_DESKTOP_ONLY_WHEN_ACTIVE
		#endif // PATCH_SHOW_DESKTOP
		arrange(c->mon);

		#if PATCH_HANDLE_SIGNALS
		if (closing) {
			Client *nc = c;
			// was the client that closed visible?
			if ((c->tags & c->mon->tagset[c->mon->seltags]) || c->issticky) {
				// check if any other clients are visible on the tag selection;
				for (nc = c->mon->clients; nc && (
					!ISVISIBLEONTAG(nc, c->tags)
					#if PATCH_FLAG_PANEL
					|| nc->ispanel
					#endif // PATCH_FLAG_PANEL
					#if PATCH_SHOW_DESKTOP
					|| nc->isdesktop || nc->ondesktop
					#endif // PATCH_SHOW_DESKTOP
				); nc = nc->next);
			}
			if (!nc) {
				// nothing visible so switch to the most appropriate client;
				#if PATCH_MODAL_SUPPORT
				//viewmontag(c->mon, (1 << (c->mon->switchonempty - 1)), 0);
				if ((nc = getfirstmodal(c->mon))) {
					activateclient(nc, wasfocused);
					break;
				}
				else
				#endif // PATCH_MODAL_SUPPORT
				for (nc = c->mon->stack; nc; nc = nc->snext) {
					if (!nc->dormant
						#if PATCH_FLAG_PANEL
						&& !nc->ispanel
						#endif // PATCH_FLAG_PANEL
						#if PATCH_SHOW_DESKTOP
						&& !nc->isdesktop && (!nc->ondesktop || nc->mon->showdesktop)
						#endif // PATCH_SHOW_DESKTOP
					) {
						activateclient(nc, wasfocused);
						break;
					}
				}
				if (nc)
					break;
			}
		}
		#endif // PATCH_HANDLE_SIGNALS

		#if PATCH_SWITCH_TAG_ON_EMPTY
		Client *cc = c;
		if (c->mon->switchonempty && ((c->tags & c->mon->tagset[c->mon->seltags]) || c->issticky)) {
			for (cc = c->mon->clients; cc && (
				!ISVISIBLEONTAG(cc, c->tags)
				#if PATCH_FLAG_PANEL
				|| cc->ispanel
				#endif // PATCH_FLAG_PANEL
				#if PATCH_SHOW_DESKTOP
				|| cc->isdesktop || (cc->ondesktop && !cc->mon->showdesktop)
				#endif // PATCH_SHOW_DESKTOP
			); cc = cc->next);
			if (!cc)
				viewmontag(c->mon, (1 << (c->mon->switchonempty - 1)), 0);
		}
		if (cc)
		#endif // PATCH_SWITCH_TAG_ON_EMPTY
		{
			Client *sel;
			if (wasfocused) {
				#if PATCH_FOCUS_FOLLOWS_MOUSE
				if (c->mon == selmon) {
					Monitor *pm = NULL;
					int px, py;
					if (getrootptr(&px, &py))
						pm = recttomon(px, py, 1, 1);
					if (pm && pm != selmon)
						selmon = pm;
				}
				#endif // PATCH_FOCUS_FOLLOWS_MOUSE
				waitforclearkeyboard();
				if ((c->mon != selmon && (sel = selmon->sel)) || !(sel = guessnextfocus(c, c->mon)))
					focus(NULL, 0);
				else {
					if (sel->mon != selmon) {
						sel->mon->sel = sel;
						viewmontag(sel->mon, sel->tags, 0);
						focus(NULL, 0);
					}
					else focus(sel, 0);
				}
			}
		}
		#if PATCH_MOUSE_POINTER_WARPING
		if (c->mon == selmon) {
			if (c->mon->sel)
				#if PATCH_MOUSE_POINTER_WARPING_SMOOTH
				warptoclient(c->mon->sel, 1, -1);
				#else // NO PATCH_MOUSE_POINTER_WARPING_SMOOTH
				warptoclient(c->mon->sel, -1);
				#endif // PATCH_MOUSE_POINTER_WARPING_SMOOTH
		}
		#endif // PATCH_MOUSE_POINTER_WARPING

		break;
	}
	updateclientlist();

	free(c);

	#if PATCH_HANDLE_SIGNALS
	if (closing) {
		int cnt = 0;
		for (Monitor *m = mons; m; m = m->next) {
			for (c = m->stack; c; c = c->snext) {
				if (c->dormant
					#if PATCH_TERMINAL_SWALLOWING
					|| c->isterminal
					#endif // PATCH_TERMINAL_SWALLOWING
					#if PATCH_SHOW_DESKTOP
					|| c->isdesktop || (c->ondesktop && !c->mon->showdesktop)
					#endif // PATCH_SHOW_DESKTOP
					#if PATCH_FLAG_PANEL
					|| c->ispanel
					#endif // PATCH_FLAG_PANEL
					#if PATCH_FLAG_IGNORED
					|| c->isignored
					#endif // PATCH_FLAG_IGNORED
				) continue;
				++cnt;
				break;
			}
			if (cnt)
				break;
		}
		if (!cnt) {
			closing = 0;
			running = 0;
		}
	}
	#endif // PATCH_HANDLE_SIGNALS
}

void
unmapnotify(XEvent *e)
{
	Client *c;
	XUnmapEvent *ev = &e->xunmap;

	#if PATCH_SHOW_DESKTOP
	#if PATCH_SHOW_DESKTOP_UNMANAGED
	if (showdesktop && showdesktop_unmanaged && desktopwin == ev->window) {
		desktopwin = None;
		desktoppid = 0;
		return;
	}
	else
	#endif // PATCH_SHOW_DESKTOP_UNMANAGED
	#endif // PATCH_SHOW_DESKTOP
	if ((c = wintoclient(ev->window))
		#if PATCH_CROP_WINDOWS
		|| (c = cropwintoclient(ev->window))
		#endif // PATCH_CROP_WINDOWS
	) {
		#if PATCH_FLAG_GAME
		#if PATCH_FLAG_GAME_STRICT
		if (c == game)
			game = NULL;
		#endif // PATCH_FLAG_GAME_STRICT
		#endif // PATCH_FLAG_GAME
		#if PATCH_FOCUS_BORDER
		if (focuswin && selmon->sel == c)
			drawfocusborder(1);
		#endif // PATCH_FOCUS_BORDER
		if (ev->send_event) {
			setclientstate(c, WithdrawnState);
		}
		else
			unmanage(c, 0, 0);
	}
	#if PATCH_SYSTRAY
	else if ((c = wintosystrayicon(ev->window))) {
		/* KLUDGE! sometimes icons occasionally unmap their windows, but do
		 * _not_ destroy them. We map those windows back */
		XMapRaised(dpy, c->win);
		updatesystray(1);
	}
	#endif // PATCH_SYSTRAY
	#if PATCH_SCAN_OVERRIDE_REDIRECTS
	else if ((c = wintoorclient(ev->window))) {
		if (ev->send_event)
			setclientstate(c, WithdrawnState);
		else {
			detachor(c);
			free(c);
		}
	}
	#endif // PATCH_SCAN_OVERRIDE_REDIRECTS
	else {
		#if DEBUGGING
		if (1
			#if PATCH_ALTTAB
			&& (!altTabMon || altTabMon->tabwin != ev->window)
			#endif // PATCH_ALTTAB
			#if PATCH_TORCH
			&& (!torchwin || torchwin != ev->window)
			#endif // PATCH_TORCH
			#if PATCH_SYSTRAY
			&& (!systray || !systray->win || systray->win != ev->window)
			#endif // PATCH_SYSTRAY
		) {
			//logdatetime(stderr);
			//fprintf(stderr, "debug: unmapnotify unmanaged window: 0x%lx.\n", ev->window);
		}
		#endif // DEBUGGING
	}
}

#if PATCH_FLAG_GAME || PATCH_FLAG_HIDDEN || PATCH_FLAG_PANEL
void
unminimize(Client *c)
{
	if (!c)
		return;

	if (MINIMIZED(c)) {
		#if PATCH_FLAG_GAME
		if (c->isgame && c->isfullscreen) {
			c->x = c->mon->mx;
			c->y = c->mon->my;
		}
		#endif // PATCH_FLAG_GAME
		XMoveWindow(dpy, c->win, c->x, c->y);
		XMapWindow(dpy, c->win);
	}
	setclientstate(c, NormalState);
}
#endif // PATCH_FLAG_GAME || PATCH_FLAG_HIDDEN || PATCH_FLAG_PANEL

int
updatebarpos(Monitor *m)
{
	int visible =
		#if PATCH_ALTTAB && PATCH_ALTTAB_HIGHLIGHT
		!(!tabHighlight || !altTabMon || !altTabMon->isAlt || !altTabMon->highlight || !altTabMon->highlight->isfullscreen
			#if PATCH_FLAG_FAKEFULLSCREEN
			|| altTabMon->highlight->fakefullscreen == 1
			#endif // PATCH_FLAG_FAKEFULLSCREEN
		) ? 0 :
		#endif // PATCH_ALTTAB && PATCH_ALTTAB_HIGHLIGHT
		#if PATCH_TORCH
		torchwin ? 0 :
		#endif // PATCH_TORCH
		m->barvisible;

	m->wy = m->my;
	m->wh = m->mh;
	if (m->showbar) {
		#if PATCH_FONT_GROUPS
		m->wh -= m->bh;
		m->wy = m->topbar ? m->wy + m->bh : m->wy;
		#else // NO PATCH_FONT_GROUPS
		m->wh -= bh;
		m->wy = m->topbar ? m->wy + bh : m->wy;
		#endif // PATCH_FONT_GROUPS
	}
	if (visible)
		m->by = m->topbar ? m->my : m->my + m->wh;
	else
		#if PATCH_FONT_GROUPS
		m->by = -m->bh;
		#else // NO PATCH_FONT_GROUPS
		m->by = -bh;
		#endif // PATCH_FONT_GROUPS

	#if PATCH_FONT_GROUPS
	XMoveResizeWindow(dpy, m->barwin, m->mx, m->by, m->mw, m->bh/* *2*/);
	#else // NO PATCH_FONT_GROUPS
	XMoveResizeWindow(dpy, m->barwin, m->mx, m->by, m->mw, bh/* *2*/);
	#endif // PATCH_FONT_GROUPS

	#if PATCH_SYSTRAY
	if(showsystray && systray && m == systraytomon(m)) {
		XWindowChanges wc;
		wc.y = m->by;
		XConfigureWindow(dpy, systray->win, CWY, &wc);
	}
	#endif // PATCH_SYSTRAY

	return visible;
}

void
updatebars(void)
{
	Monitor *m;
	XSetWindowAttributes wa;
	#if PATCH_ALPHA_CHANNEL
	if (useargb) {
		wa.override_redirect = True;
		wa.background_pixel = 0;
		wa.border_pixel = 0;
		wa.colormap = cmap;
		wa.event_mask = ButtonPressMask|ExposureMask;
	}
	else
	#endif // PATCH_ALPHA_CHANNEL
	{
		wa.override_redirect = True;
		wa.background_pixmap = ParentRelative;
		wa.event_mask = ButtonPressMask|ExposureMask;
	}
	XClassHint ch = {"dwm", "dwm"};
	for (m = mons; m; m = m->next) {
		if (m->barwin) {
			resizebarwin(m);
			continue;
		}
		#if PATCH_ALPHA_CHANNEL
		if (useargb)
			m->barwin = XCreateWindow(dpy, root, m->wx, m->by, m->ww, bh/* *2*/, 0, depth,
					InputOutput, visual,
					CWOverrideRedirect|CWBackPixel|CWBorderPixel|CWColormap|CWEventMask, &wa);
		else
		#endif // PATCH_ALPHA_CHANNEL
		m->barwin = XCreateWindow(dpy, root, m->wx, m->by, m->ww, bh/* *2*/, 0, DefaultDepth(dpy, screen),
				CopyFromParent, DefaultVisual(dpy, screen),
				CWOverrideRedirect|CWBackPixmap|CWEventMask, &wa);
		XDefineCursor(dpy, m->barwin, cursor[CurNormal]->cursor);
		#if PATCH_SYSTRAY
		if (showsystray && systray && m == systraytomon(m))
			XMapRaised(dpy, systray->win);
		#endif // PATCH_SYSTRAY
		XMapRaised(dpy, m->barwin);
		XSetClassHint(dpy, m->barwin, &ch);
	}
}

void
updateclientlist(void)
{
	Client *c;
	Monitor *m;

	XDeleteProperty(dpy, root, netatom[NetClientList]);
	for (m = mons; m; m = m->next)
		for (c = m->clients; c; c = c->next)
			XChangeProperty(dpy, root, netatom[NetClientList],
				XA_WINDOW, 32, PropModeAppend,
				(unsigned char *) &(c->win), 1);
}

void
updateclientmonitors(void)
{
	Monitor *mm;

	#if PATCH_STATUS_ALLOW_FIXED_MONITOR
	Monitor *last_status_mon, *status_mon;
	status_mon = NULL;
	status_always_on = status_allow_fixed_mon ? mons : NULL;
	#endif // PATCH_STATUS_ALLOW_FIXED_MONITOR

	// move any clients that want a monitor that they are not currently on;
	for (Monitor *m = mons; m; m = m->next)
	{
		#if PATCH_STATUS_ALLOW_FIXED_MONITOR
		if (status_allow_fixed_mon && status_always_on) {
			last_status_mon = status_mon;
			if (m->showstatus)
				status_mon = status_always_on = m;
			if (last_status_mon && last_status_mon != status_mon)
				status_always_on = NULL;
		}
		#endif // PATCH_STATUS_ALLOW_FIXED_MONITOR

		for (Client *cc, *c = m->clients; c; c = cc) {
			cc = c->next;
			if (c->monindex == m->num || c->monindex == -1)
				continue;
			for (mm = mons; mm; mm = mm->next)
				if (mm->num == c->monindex)
					break;
			if (!mm)
				continue;
			if (c == m->clients)
				m->clients = cc;
			else {
				for (cc = m->clients; cc && cc->next && cc->next != c; cc = cc->next);
				cc = cc->next = c->next;
			}
			detachstack(c);
			c->mon = mm;
			#if PATCH_ATTACH_BELOW_AND_NEWMASTER
			attachBelow(c);
			//attachstack(c);
			attachstackBelow(c);
			#else // NO PATCH_ATTACH_BELOW_AND_NEWMASTER
			attach(c);
			attachstack(c);
			#endif // PATCH_ATTACH_BELOW_AND_NEWMASTER
		}
	}
}

#if PATCH_EWMH_TAGS
void
updatecurrentdesktop(void){
	long rawdata[] = { selmon->tagset[selmon->seltags] };
	int i=0;
	while (*rawdata >> (i+1)) {
		i++;
	}
	// i holds highest active tag number;
	long data[] = { i };
	XChangeProperty(dpy, root, netatom[NetCurrentDesktop], XA_CARDINAL, 32, PropModeReplace, (unsigned char *)data, 1);
}
#endif // PATCH_EWMH_TAGS

int
updategeom(void)
{
	int dirty = 0;
	Monitor *m;

	#if PATCH_CONSTRAIN_MOUSE
	if (constrained) {
		
	}
	#endif // PATCH_CONSTRAIN_MOUSE
#ifdef XINERAMA
	if (XineramaIsActive(dpy)) {
		int i, j, n, nn;
		#if PATCH_VIRTUAL_MONITORS
		int index = 0;
		PMonitor *pm;
		#else // NO PATCH_VIRTUAL_MONITORS
		#endif // PATCH_VIRTUAL_MONITORS
		XineramaScreenInfo *info = XineramaQueryScreens(dpy, &nn);
		XineramaScreenInfo *unique = NULL;

		#if PATCH_VIRTUAL_MONITORS
		for (n = 0, pm = pmons; pm; pm = pm->next, n++);
		#else // NO PATCH_VIRTUAL_MONITORS
		for (n = 0, m = mons; m; m = m->next, n++);
		#endif // PATCH_VIRTUAL_MONITORS
		/* only consider unique geometries as separate screens */
		unique = ecalloc(nn, sizeof(XineramaScreenInfo));
		for (i = 0, j = 0; i < nn; i++)
			if (isuniquegeom(unique, j, &info[i]))
				memcpy(&unique[j++], &info[i], sizeof(XineramaScreenInfo));
		XFree(info);
		nn = j;

		#if PATCH_VIRTUAL_MONITORS
		for (pm = pmons; pm; pm = pm->next) {
			if (pm->mon1) index++;
		}
		for (m = mons; m && m->next; m = m->next);
		for (pm = pmons; pm && pm->next; pm = pm->next);
		/* new monitors if nn > n */
		for (i = 0, pm = NULL; i < nn; i++) {
			if (i >= n) {
				dirty = 1;
				if (pm)
					pm = pm->next = createpmon();
				else
					pm = pmons = createpmon();
			} else {
				if (!pm)
					pm = pmons;
				else
					pm = pm->next;
			}
			if (unique[i].x_org != pm->mx || unique[i].y_org != pm->my
			|| unique[i].width != pm->mw || unique[i].height != pm->mh)
			{
				dirty = 1;
				pm->mx = unique[i].x_org;
				pm->my = unique[i].y_org;
				pm->mw = unique[i].width;
				pm->mh = unique[i].height;
			}
		}
		/* removed monitors if n > nn */
		for (i = 0, pm = pmons; pm && i < nn; pm = pm->next, i++);
		for (; pm && i < n; pm = pm->next, i++)
			if (pm != pmons)
				pm->disappeared = 1;

		dirty |= updatevirtualmonitors();

		#else // NO PATCH_VIRTUAL_MONITORS
		for (i = n; i < nn; i++) {
			for (m = mons; m && m->next; m = m->next);
			if (m)
				m->next = createmon();
			else
				mons = createmon();
		}
		for (i = 0, m = mons; i < nn && m; m = m->next, i++)
			if (i >= n
			|| unique[i].x_org != m->mx || unique[i].y_org != m->my
			|| unique[i].width != m->mw || unique[i].height != m->mh)
			{
				dirty = 1;
				m->num = i;
				m->mx = m->wx = unique[i].x_org;
				m->my = m->wy = unique[i].y_org;
				m->mw = m->ww = unique[i].width;
				m->mh = m->wh = unique[i].height;
				updatebarpos(m);
			}
		/* removed monitors if n > nn */
		for (i = nn; i < n; i++) {
			for (m = mons; m && m->next; m = m->next);
			while ((c = m->clients)) {
				dirty = 1;
				m->clients = c->next;
				if (c->monindex == -1)
					c->monindex = c->mon->num;
				detachstack(c);
				c->mon = mons;
				#if PATCH_ATTACH_BELOW_AND_NEWMASTER
				attachBelow(c);
				//attachstack(c);
				attachstackBelow(c);
				#else // NO PATCH_ATTACH_BELOW_AND_NEWMASTER
				attach(c);
				attachstack(c);
				#endif // PATCH_ATTACH_BELOW_AND_NEWMASTER
			}
			if (m == selmon)
				selmon = mons;
			cleanupmon(m);
		}
		#endif // PATCH_VIRTUAL_MONITORS
		free(unique);
		updateclientmonitors();

	} else
#endif /* XINERAMA */
	{ /* default monitor setup */

		#if PATCH_VIRTUAL_MONITORS
		if (!pmons)
			pmons = createpmon();
		if (pmons->mw != sw || pmons->mh != sh) {
			pmons->mw = sw;
			pmons->mh = sh;
			dirty = 1;
		}
		dirty |= updatevirtualmonitors();

		#else // NO PATCH_VIRTUAL_MONITORS
		if (!mons)
			mons = createmon();
		if (mons->mw != sw || mons->mh != sh) {
			dirty = 1;
			mons->mw = mons->ww = sw;
			mons->mh = mons->wh = sh;
			updatebarpos(mons);
		}
		#endif // PATCH_VIRTUAL_MONITORS
		updateclientmonitors();

	}
	if (dirty) {
		selmon = mons;
		selmon = wintomon(root);
	}
	return dirty;
}

void
updatenumlockmask(void)
{
	unsigned int i, j;
	XModifierKeymap *modmap;

	numlockmask = 0;
	modmap = XGetModifierMapping(dpy);
	for (i = 0; i < 8; i++)
		for (j = 0; j < modmap->max_keypermod; j++)
			if (modmap->modifiermap[i * modmap->max_keypermod + j]
				== XKeysymToKeycode(dpy, XK_Num_Lock))
				numlockmask = (1 << i);
	XFreeModifiermap(modmap);
}

void
updatesizehints(Client *c)
{
	long msize;
	XSizeHints size;

	if (!XGetWMNormalHints(dpy, c->win, &size, &msize))
		/* size is uninitialized, ensure that size.flags aren't used */
		size.flags = PSize;
	if (size.flags & PBaseSize) {
		c->basew = size.base_width;
		c->baseh = size.base_height;
	} else if (size.flags & PMinSize) {
		c->basew = size.min_width;
		c->baseh = size.min_height;
	} else
		c->basew = c->baseh = 0;
	if (size.flags & PResizeInc) {
		c->incw = size.width_inc;
		c->inch = size.height_inc;
	} else
		c->incw = c->inch = 0;
	if (size.flags & PMaxSize) {
		c->maxw = size.max_width;
		c->maxh = size.max_height;
	} else
		c->maxw = c->maxh = 0;
	if (size.flags & PMinSize) {
		c->minw = size.min_width;
		c->minh = size.min_height;
	} else if (size.flags & PBaseSize) {
		c->minw = size.base_width;
		c->minh = size.base_height;
	} else
		c->minw = c->minh = 0;
	if (size.flags & PAspect) {
		c->mina = (float)size.min_aspect.y / size.min_aspect.x;
		c->maxa = (float)size.max_aspect.x / size.max_aspect.y;
	} else
		c->maxa = c->mina = 0.0;
	c->isfixed = (c->maxw && c->maxh && c->maxw == c->minw && c->maxh == c->minh);
	c->hintsvalid = 1;
}

void
updatestatus(void)
{
	Monitor *m = selmon;

	if (!gettextprop(root, XA_WM_NAME, stext, sizeof(stext)))
		strcpy(stext, DWM_VERSION_STRING_SHORT); // no shining of dwm version thru panel, when transparent
	else if (m->showstatus == -1
			#if PATCH_STATUS_ALLOW_FIXED_MONITOR
			&& !status_always_on
			#endif // PATCH_STATUS_ALLOW_FIXED_MONITOR
		)
		return;

	#if PATCH_STATUS_ALLOW_FIXED_MONITOR
	if (m->showstatus != 1 && status_always_on)
		m = status_always_on;
	#endif // PATCH_STATUS_ALLOW_FIXED_MONITOR

	if (m->showstatus && (
		#if PATCH_ALT_TAGS
		m->alttags ||
		#endif // PATCH_ALT_TAGS
		!m->sel ||
		!(m->sel->isfullscreen
			#if PATCH_FLAG_FAKEFULLSCREEN
			&& m->sel->fakefullscreen != 1
			#endif // PATCH_FLAG_FAKEFULLSCREEN
		))) {
		#if PATCH_SYSTRAY
		if (showsystray && m == systraytomon(m))
			updatesystray(1);
		else
		#endif // PATCH_SYSTRAY
			//drawbars();
			drawbar(m, 1);
	}
}


#if PATCH_SYSTRAY
void
updatesystrayicongeom(Client *i, int w, int h)
{
	if (i) {
		i->h = bh;
		if (w == h)
			i->w = bh;
		else if (h == bh)
			i->w = w;
		else
			i->w = (int) ((float)bh * ((float)w / (float)h));
		applysizehints(i, &(i->x), &(i->y), &(i->w), &(i->h), False);
		/* force icons into the systray dimensions if they don't want to */
		if (i->h > bh) {
			if (i->w == i->h)
				i->w = bh;
			else
				i->w = (int) ((float)bh * ((float)i->w / (float)i->h));
			i->h = bh;
		}
	}
}

void
updatesystrayiconstate(Client *i, XPropertyEvent *ev)
{
	long flags;
	int code = 0;

	if (!showsystray || !i || ev->atom != xatom[XembedInfo] ||
			!(flags = getatomprop(i, xatom[XembedInfo])))
		return;

	if (flags & XEMBED_MAPPED && !i->tags) {
		i->tags = 1;
		code = XEMBED_WINDOW_ACTIVATE;
		XMapRaised(dpy, i->win);
		setclientstate(i, NormalState);
	}
	else if (!(flags & XEMBED_MAPPED) && i->tags) {
		i->tags = 0;
		code = XEMBED_WINDOW_DEACTIVATE;
		XUnmapWindow(dpy, i->win);
		setclientstate(i, WithdrawnState);
	}
	else
		return;
	sendevent(i->win, xatom[Xembed], StructureNotifyMask, CurrentTime, code, 0, systray->win, XEMBED_EMBEDDED_VERSION);
}

void
updatesystray(int updatebar)
{
	if (!showsystray)
		return;

	XSetWindowAttributes wa;
	XWindowChanges wc;
	Client *i;
	Monitor *m = systraytomon(NULL);

	unsigned int x = m->mx + m->bar[StatusText].x;
	//unsigned int sw = TEXTW(m->showstatus == -1 ? DWM_VERSION_STRING_SHORT : stext) - lrpad + systrayspacing;
	unsigned int w = 1;

	if (!systrayonleft && m->showstatus)
		x += m->bar[StatusText].w;

	if (!systray) {
		/* init systray */
		if (!(systray = (Systray *)calloc(1, sizeof(Systray))))
			die("fatal: could not malloc() %u bytes\n", sizeof(Systray));

		wa.override_redirect = True;
		wa.event_mask = ButtonPressMask|ExposureMask;
		#if PATCH_ALPHA_CHANNEL
		if (useargb) {
			wa.background_pixel = 0;
			wa.border_pixel = 0;
			wa.colormap = cmap;
			systray->win = XCreateWindow(dpy, root, x, m->by, w, bh, 0, depth,
							InputOutput, visual,
							CWOverrideRedirect|CWBackPixel|CWBorderPixel|CWColormap|CWEventMask, &wa);
		}
		else
		#endif // PATCH_ALPHA_CHANNEL
		{
			wa.background_pixel = scheme[SchemeNorm][ColBg].pixel;
			systray->win = XCreateSimpleWindow(dpy, root, x, m->by, w, bh, 0, 0, scheme[SchemeSel][ColBg].pixel);
		}
		XSelectInput(dpy, systray->win, SubstructureNotifyMask);
		XChangeProperty(dpy, systray->win, netatom[NetSystemTrayOrientation], XA_CARDINAL, 32,
				PropModeReplace, (unsigned char *)&netatom[NetSystemTrayOrientationHorz], 1);
		//XChangeProperty(dpy, systray->win, netatom[NetSystemTrayOrientation], XA_CARDINAL, 32,
		//		PropModeReplace, (unsigned char *)&systrayorientation, 1);
		#if PATCH_ALPHA_CHANNEL
		if (useargb)
			XChangeProperty(dpy, systray->win, netatom[NetSystemTrayVisual], XA_VISUALID, 32,
					PropModeReplace, (unsigned char *)&visual->visualid, 1);
		#endif // PATCH_ALPHA_CHANNEL
		XChangeProperty(dpy, systray->win, netatom[NetWMWindowType], XA_ATOM, 32,
				PropModeReplace, (unsigned char *)&netatom[NetWMWindowTypeDock], 1);
		#if PATCH_ALPHA_CHANNEL
		if (!useargb)
		#endif // PATCH_ALPHA_CHANNEL
		XChangeWindowAttributes(dpy, systray->win, CWEventMask|CWOverrideRedirect|CWBackPixel, &wa);
		XMapRaised(dpy, systray->win);
		XSetSelectionOwner(dpy, netatom[NetSystemTray], systray->win, CurrentTime);
		if (XGetSelectionOwner(dpy, netatom[NetSystemTray]) == systray->win) {
			sendevent(root, xatom[Manager], StructureNotifyMask, CurrentTime, netatom[NetSystemTray], systray->win, 0, 0);
			XSync(dpy, False);
		}
		else {
			logdatetime(stderr);
			fprintf(stderr, "dwm: unable to obtain system tray.\n");
			free(systray);
			systray = NULL;
			return;
		}
	}

	for (w = 0, i = systray->icons; i; i = i->next) {
		//wa.background_pixel = 0;
		wa.background_pixel = scheme[SchemeNorm][ColBg].pixel;
		XChangeWindowAttributes(dpy, i->win, CWBackPixel, &wa);
		XMapRaised(dpy, i->win);
		w += systrayspacing;
		i->x = w;
		XMoveResizeWindow(dpy, i->win, i->x, 0, i->w, i->h);
		w += i->w;
		if (i->mon != m)
			i->mon = m;
	}
	w = w ? w + systrayspacing : 1;
	x -= systrayonleft ? -1 : (m->showstatus ? w : 0);

	XMoveResizeWindow(dpy, systray->win, x, m->by, w, bh);
	drw_setscheme(drw, scheme[SchemeNorm]);
	drw_rect(drw, 0, 0, w, bh, 1, 1);
	drw_map(drw, systray->win, 0, 0, w, bh);
	wc.x = x;
	wc.y = m->by;
	wc.width = w;
	wc.height = bh;
	wc.stack_mode = Above;
	wc.sibling = m->barwin;
	XConfigureWindow(dpy, systray->win, CWX|CWY|CWWidth|CWHeight|CWSibling|CWStackMode, &wc);
	XMapWindow(dpy, systray->win);
	XMapSubwindows(dpy, systray->win);
	XSync(dpy, False);

	if (updatebar)
		drawbar(m, 1);
}
#endif // PATCH_SYSTRAY

void
updatetitle(Client *c, int fixempty)
{
	#if PATCH_FLAG_TITLE
	if (c->displayname) {
		strcpy(c->name, c->displayname);
		return;
	}
	#endif // PATCH_SHOW_DESKTOP
	if (!gettextprop(c->win, netatom[NetWMName], c->name, sizeof c->name))
		gettextprop(c->win, XA_WM_NAME, c->name, sizeof c->name);
	if (fixempty && c->name[0] == '\0') /* hack to mark broken clients */
		strcpy(c->name, broken);
}

#if PATCH_WINDOW_ICONS
void
updateicon(Client *c)
{
	freeicon(c);
	c->icon = geticonprop(
		#if PATCH_WINDOW_ICONS_DEFAULT_ICON || PATCH_WINDOW_ICONS_CUSTOM_ICONS
		c,
		#endif // PATCH_WINDOW_ICONS_DEFAULT_ICON || PATCH_WINDOW_ICONS_CUSTOM_ICONS
		c->win, &c->icw, &c->ich, iconsize
	);
}
#endif // PATCH_WINDOW_ICONS

#if PATCH_VIRTUAL_MONITORS
int
updatevirtualmonitors(void)
{
	int dirty = 0, index = 0, update1 = 0, update2 = 0;
	PMonitor *pm, *pp;
	Monitor *m;
	Client *c;

	for (m = mons; m && m->next; m = m->next);
	for (pm = pmons; pm; pm = pm->next, index++)
	{
		update1 = update2 = 0;

		if (pm->disappeared) {
			index--;
			continue;
		}

		// create mon1 if it doesn't exist, else re-parse its config;
		if (!pm->mon1) {
			if (m)
				m = m->next = createmon(index);
			else
				m = mons = createmon(index);
			pm->mon1 = m;
			m->pmon = pm;
			dirty = 1;
		}
		else
			parsemon(pm->mon1, index, 0);

		if (pm->mon2 && (!pm->mon1->split || !pm->mon1->enablesplit || index > 999)) {
			// mon2 exists but should be removed;
			dirty = 1;
			update1 = 1;
			// remove pm->mod2;
			while ((c = pm->mon2->clients)) {
				dirty = 1;
				pm->mon2->clients = c->next;
				if (c->monindex == -1)
					c->monindex = c->mon->num;
				detachstack(c);
				c->mon = pm->mon1;
				#if PATCH_ATTACH_BELOW_AND_NEWMASTER
				attachBelow(c);
				//attachstack(c);
				attachstackBelow(c);
				#else // NO PATCH_ATTACH_BELOW_AND_NEWMASTER
				attach(c);
				attachstack(c);
				#endif // PATCH_ATTACH_BELOW_AND_NEWMASTER
			}
			if (pm->mon2 == selmon)
				selmon = pm->mon1;
			cleanupmon(pm->mon2);
			pm->mon2 = NULL;
		}
		else if (index > 999)
			update1 = 1;
		else if (pm->mon2) {
			// mon2 exists legitimately, so re-parse its config;
			update2 = 1;
			parsemon(pm->mon2, pm->mon2->num, 0);
		}
		else if (pm->mon1->split && pm->mon1->enablesplit) {
			dirty = 1;
			update2 = 1;
			if (m)
				m = m->next = createmon(index + 1000);
			else
				m = mons = createmon(index + 1000);
			pm->mon2 = m;
			m->pmon = pm;
		}
		else
			update1 = 1;

		if (update1) {
			// update pm->mon1 geometry;
			pm->mon1->mx = pm->mon1->wx = pm->mx;
			pm->mon1->my = pm->mon1->wy = pm->my;
			pm->mon1->mw = pm->mon1->ww = pm->mw;
			pm->mon1->mh = pm->mon1->wh = pm->mh;
			updatebarpos(pm->mon1);
		}
		else if (update2) {
			// update pm->mon1 and pm->mon2 geometry;
			if (pm->mon1->split == 1) {
				pm->mon2->mw = pm->mon2->ww = pm->mw / 2;
				pm->mon2->mx = pm->mon2->wx = pm->mx + pm->mon2->mw;
				pm->mon2->my = pm->mon2->wy = pm->my;
				pm->mon2->mh = pm->mon2->wh = pm->mh;
				pm->mon1->mx = pm->mon1->wx = pm->mx;
				pm->mon1->my = pm->mon1->wy = pm->my;
				pm->mon1->mw = pm->mon1->ww = pm->mon2->mw;
				pm->mon1->mh = pm->mon1->wh = pm->mh;
			}
			else if (pm->mon1->split == 2) {
				pm->mon2->mw = pm->mon2->ww = pm->mw;
				pm->mon2->mx = pm->mon2->wx = pm->mx;
				pm->mon2->mh = pm->mon2->wh = pm->mh / 2;
				pm->mon2->my = pm->mon2->wy = pm->my + pm->mon2->mh;
				pm->mon1->mx = pm->mon1->wx = pm->mx;
				pm->mon1->my = pm->mon1->wy = pm->my;
				pm->mon1->mw = pm->mon1->ww = pm->mw;
				pm->mon1->mh = pm->mon1->wh = pm->mon2->mh;
			}
			updatebarpos(pm->mon1);
			updatebarpos(pm->mon2);
		}
	}

	// deal with removed physical monitors;
	for (pp = NULL, pm = pmons; pm; pp = pm, pm = pm->next) {

		if (!pm->disappeared)
			continue;

		if (!pp)
			pp = pmons = pm->next;
		else
			pp->next = pm->next;

		for (m = pm->mon2 ? pm->mon2 : pm->mon1; m; m = (m == pm->mon1 ? NULL : pm->mon1)) {
			while ((c = m->clients)) {
				dirty = 1;
				m->clients = c->next;
				if (c->monindex == -1)
					c->monindex = c->mon->num;
				detachstack(c);
				c->mon = pp->mon1;
				#if PATCH_ATTACH_BELOW_AND_NEWMASTER
				attachBelow(c);
				//attachstack(c);
				attachstackBelow(c);
				#else // NO PATCH_ATTACH_BELOW_AND_NEWMASTER
				attach(c);
				attachstack(c);
				#endif // PATCH_ATTACH_BELOW_AND_NEWMASTER
			}
			if (m == selmon)
				selmon = pp->mon1;
			cleanupmon(m);
		}
		free(pm);
	}

	return dirty;
}
#endif // PATCH_VIRTUAL_MONITORS

int
updatewindowstate(Client *c)
{
	Atom da, atom = None;
	Atom req = XA_ATOM;
	Atom *state = NULL;
	int di;
	unsigned long dl = 0, after = sizeof atom;
	unsigned char *p = NULL;

	int urgent = 0,
		fullscreen = 0
		#if PATCH_FLAG_ALWAYSONTOP
		, alwaysontop = 0
		#endif // PATCH_FLAG_ALWAYSONTOP
		#if PATCH_FLAG_HIDDEN
		, hidden = 0
		#endif // PATCH_FLAG_HIDDEN
		#if PATCH_MODAL_SUPPORT
		, modal = 0
		#endif // PATCH_MODAL_SUPPORT
		#if PATCH_FLAG_STICKY
		, sticky = 0
		#endif // PATCH_FLAG_STICKY
	;

	do {
		if (XGetWindowProperty(
				dpy, c->win, netatom[NetWMState], 0L, after,
				False, req, &da, &di, &dl, &after, &p
			) == Success && p
		) {

			state = (Atom *)p;

			for (int i = 0; i < dl; i++) {
				if (state[i] == netatom[NetWMAttention])
					urgent = 1;
				else if (state[i] == netatom[NetWMFullscreen])
					fullscreen = 1;
				#if PATCH_FLAG_ALWAYSONTOP
				else if (state[i] == netatom[NetWMStaysOnTop])
					alwaysontop = 1;
				#endif // PATCH_FLAG_ALWAYSONTOP
				#if PATCH_FLAG_HIDDEN
				else if (state[i] == netatom[NetWMHidden])
					hidden = 1;
				#endif // PATCH_FLAG_HIDDEN
				#if PATCH_FLAG_STICKY
				else if (state[i] == netatom[NetWMSticky])
					sticky = 1;
				#endif // PATCH_FLAG_STICKY
				#if PATCH_MODAL_SUPPORT
				else if (c->ismodal_override != 0 && state[i] == netatom[NetWMModal])
					modal = 1;
				#endif // PATCH_MODAL_SUPPORT
			}

			XFree(state);
		}
	}
	while (after);

	di = 0;
	if (c->isurgent != urgent && urgency) {
		seturgent(c, 1);
		di = 1;
	}
	if (c->isfullscreen != fullscreen) {
		setfullscreen(c, fullscreen);
		di = 1;
	}
	#if PATCH_FLAG_ALWAYSONTOP
	if (c->alwaysontop != alwaysontop) {
		setalwaysontop(c, alwaysontop);
		di = 1;
	}
	#endif // PATCH_FLAG_ALWAYSONTOP
	#if PATCH_FLAG_HIDDEN
	if (c->ishidden != hidden) {
		sethidden(c, 1, True);
		di = 1;
	}
	#endif // PATCH_FLAG_HIDDEN
	#if PATCH_FLAG_STICKY
	if (c->issticky != sticky) {
		setsticky(c, 1);
		di = 1;
	}
	#endif // PATCH_FLAG_STICKY
	#if PATCH_MODAL_SUPPORT
	if (c->ismodal_override == -1) {
		c->ismodal = modal;
		di = 1;
	}
	#endif // PATCH_MODAL_SUPPORT

	return di;
}

void
updatewindowtype(Client *c)
{
	Atom wtype = getatomprop(c, netatom[NetWMWindowType]);
	if (wtype == netatom[NetWMWindowTypeDialog] && c->isfloating_override != 0) {
		#if PATCH_FLAG_CENTRED
		if (c->iscentred_override == -1)
			c->iscentred = 2;
		#endif // PATCH_FLAG_CENTRED
		c->isfloating = 1;
	}
	else if (wtype == netatom[NetWMWindowTypeSplash] && c->isfloating_override != 0) {
		c->autofocus =
			#if PATCH_SHOW_DESKTOP
			(showdesktop
				#if PATCH_SHOW_DESKTOP_WITH_FLOATING
				&& !showdesktop_floating
				#endif // PATCH_SHOW_DESKTOP_WITH_FLOATING
				&& !c->ondesktop
			) ? -1 :
			#endif // PATCH_SHOW_DESKTOP
			0
		;
		c->bw = c->oldbw;	// from manage() this will be the border the owner wanted;
		#if PATCH_FLAG_CENTRED
		if (c->iscentred_override == -1)
			c->iscentred = 1;
		#endif // PATCH_FLAG_CENTRED
		c->isfloating = 1;
		#if PATCH_FLAG_NEVER_FOCUS
		c->neverfocus_override =
		#endif // PATCH_FLAG_NEVER_FOCUS
		c->neverfocus = 1;
		#if PATCH_FLAG_PARENT
		c->neverparent = 1;
		#endif // PATCH_FLAG_PARENT
	}
	else if (wtype == netatom[NetWMWindowTypeDock]) {
		c->autofocus = 0;
		c->isfloating = 1;
		#if PATCH_FLAG_PANEL
		c->ispanel = 1;
		#endif // PATCH_FLAG_PANEL
	}
}

void
updatewmhints(Client *c)
{
	XWMHints *wmh;

	if ((wmh = XGetWMHints(dpy, c->win))) {
		if (c == selmon->sel && wmh->flags & XUrgencyHint) {
			wmh->flags &= ~XUrgencyHint;
			XSetWMHints(dpy, c->win, wmh);
		} else
			c->isurgent = (wmh->flags & XUrgencyHint) ? (urgency ? 1 : 0) : 0;
		#if PATCH_FLAG_NEVER_FOCUS
		if (c->neverfocus_override == -1) {
		#endif // PATCH_FLAG_NEVER_FOCUS
			if (wmh->flags & InputHint) {
				c->neverfocus = !wmh->input;
				#if PATCH_LOG_DIAGNOSTICS
				if (c->neverfocus) {
					logdatetime(stderr);
					fprintf(stderr,
						"note: neverfocus set by client: mon:%u",
						c->mon->num
					);
					logdiagnostics_client_common(c, " ", "");
					fprintf(stderr,
						" (%ix%i+%ix%i:%ix%i) (pid:%i)\n",
						c->w, c->h, c->x, c->y, (c->x - c->mon->mx), (c->y - c->mon->my), c->pid
					);
				}
				#endif // PATCH_LOG_DIAGNOSTICS
			}
			else
				c->neverfocus = 0;
		#if PATCH_FLAG_NEVER_FOCUS
		}
		#endif // PATCH_FLAG_NEVER_FOCUS
		XFree(wmh);
	}
}

#if PATCH_TWO_TONE_TITLE
int
validate_colour(cJSON *string, char **colour)
{
	if (cJSON_IsArray(string) || !cJSON_IsString(string))
		return 0;

	int j;
	size_t len = 0;
	char col[8];
	if (string->valuestring[0] == '#') {
		// validate colours #rrggbb, #rrggbbaa, or #rgb;
		len = strlen(string->valuestring);
		if (len == 4) {
			col[0] = '#';
			for (j = 1; j < 4; j++)
				col[2 * j - 1] = col[2 * j] = string->valuestring[j];
			string = cJSON_CreateString(col);
		}
		else if (len != 7 && len != 9)
			return 0;
		for (j = 1; j < len; j++) {
			if (!isxdigit(string->valuestring[j]))
				return 0;
		}
	}
	*colour = string->valuestring;
	return 1;
}
#endif // PATCH_TWO_TONE_TITLE
int
validate_colours(cJSON *array, char *colours[3], char *defaults[3])
{
	if (!cJSON_IsArray(array))
		return 0;

	int i = 0, j;
	size_t len = 0;
	char col[8];
	cJSON *n = NULL;
	for (cJSON *c = array->child; c && i < 4; c = c->next, ++i) {
		if (cJSON_IsString(c)) {
			if (c->valuestring[0] == '#') {
				// validate colours #rrggbb, #rrggbbaa, or #rgb;
				len = strlen(c->valuestring);
				if (len == 4) {
					col[0] = '#';
					for (j = 1; j < 4; j++)
						col[2 * j - 1] = col[2 * j] = c->valuestring[j];
					n = cJSON_CreateString(col);
					cJSON_ReplaceItemViaPointer(array, c, n);
					c = n;
				}
				else if (len != 7 && len != 9)
					return 0;
				for (j = 1; j < len; j++) {
					if (!isxdigit(c->valuestring[j]))
						return 0;
				}
			}
			colours[i] = c->valuestring;
		}
		else if (cJSON_IsNull(c)) {
			if (!colours[i] && !(defaults && defaults[i]))
				return 0;
		}
		else
			return 0;
	}
	return 1;
}

pid_t
validate_pid(Client *c)
{
	if (!validclient(c))
		return 0;
	int ret;
	if (c->pid && (ret = kill(c->pid, 0)) && ret == -1 && errno == ESRCH) {

		// only ignore clients without class/instance
		int ignore = 0;
		XClassHint ch = { NULL, NULL };
		XGetClassHint(dpy, c->win, &ch);
		if (ch.res_name) {
			if (!strlen(ch.res_name))
				ignore++;
			XFree(ch.res_name);
		}
		else ignore++;
		if (ch.res_class) {
			if (!strlen(ch.res_class))
				ignore++;
			XFree(ch.res_class);
		}
		else ignore++;
		if (ignore == 2) {
			logdatetime(stderr);
			fprintf(stderr,
				"debug: kill(%u, 0) == %i, client:\"%s\" was missing class/instance!\n",
				c->pid, ret, c->name
			);
			c->pid = 0;
			// this client may not be a parent without a valid process;
			removelinks(c);
		}
	}
	return (c->pid);
}

int
validclient(Client *c)
{
	if (!c)
		return 0;
	for (Monitor *m = mons; m; m = m->next)
		for (Client *cc = m->clients; cc; cc = cc->next)
			if (cc == c)
				return 1;
	return 0;
}

void
view(const Arg *arg)
{
	#if PATCH_FOCUS_FOLLOWS_MOUSE
	checkmouseovermonitor(selmon);
	#endif // PATCH_FOCUS_FOLLOWS_MOUSE
	viewmontag(selmon, arg->ui, 1);
}
void
viewmontag(Monitor *m, unsigned int tagmask, int switchmon)
{
	Client *sel = NULL;
	if ((tagmask & TAGMASK) == m->tagset[m->seltags])
		return;
	#if PATCH_ALTTAB
	if (altTabMon && altTabMon->isAlt && (altTabMon->isAlt & ALTTAB_MOUSE) == 0) {
		if (!m->altTabSelTags)
			m->altTabSelTags = m->tagset[m->seltags];
	}
	else
	#endif // PATCH_ALTTAB
	{
		#if PATCH_SHOW_DESKTOP
		if (!showdesktop || !m->showdesktop)
		#endif // PATCH_SHOW_DESKTOP
		{
			int i = tagtoindex(m->tagset[m->seltags]);
			if (i)
				m->focusontag[i - 1] = m->sel;
		}
		m->seltags ^= 1; /* toggle sel tagset */
	}
	if (tagmask & TAGMASK) {
		m->tagset[m->seltags] = tagmask & TAGMASK;
		#if PATCH_PERTAG
		m->pertag->prevtag = m->pertag->curtag;

		if (tagmask == ~0)
			m->pertag->curtag = 0;
		else {
			int i;
			for (i = 0; !(tagmask & 1 << i); i++);
			m->pertag->curtag = i + 1;
		}
	}
	else {
		unsigned int tmptag = m->pertag->prevtag;
		m->pertag->prevtag = m->pertag->curtag;
		m->pertag->curtag = tmptag;
		#endif // PATCH_PERTAG
	}

	#if PATCH_PERTAG
	m->nmaster = m->pertag->nmasters[m->pertag->curtag];
	m->mfact = m->pertag->mfacts[m->pertag->curtag];
	m->mfact_def = m->pertag->mfacts_def[m->pertag->curtag];
	m->sellt = m->pertag->sellts[m->pertag->curtag];
	m->lt[m->sellt] = m->pertag->ltidxs[m->pertag->curtag][m->sellt];
	m->lt[m->sellt^1] = m->pertag->ltidxs[m->pertag->curtag][m->sellt^1];

	#if PATCH_ALT_TAGS
	m->alttagsquiet = m->pertag->alttagsquiet[m->pertag->curtag];
	#endif // PATCH_ALT_TAGS

	#if PATCH_CLASS_STACKING
	m->class_stacking = m->pertag->class_stacking[m->pertag->curtag];
	#endif // PATCH_CLASS_STACKING

	#if PATCH_SWITCH_TAG_ON_EMPTY
	m->switchonempty = m->pertag->switchonempty[m->pertag->curtag];
	#endif // PATCH_SWITCH_TAG_ON_EMPTY

	if (m->showbar != m->pertag->showbars[m->pertag->curtag])
		togglebarex(m);
	#endif // PATCH_PERTAG

	if (nonstop & 1) {
		arrange(m);
		return;
	}

	if (switchmon || m == selmon)
	{

		#if PATCH_SHOW_DESKTOP
		m->showdesktop = 0;
		#endif // PATCH_SHOW_DESKTOP

		#if PATCH_ALTTAB
		if (!altTabMon)
		#endif // PATCH_ALTTAB
		{
			int i;
			if (
				#if PATCH_SHOW_DESKTOP
				(!showdesktop || !m->showdesktop) &&
				#endif // PATCH_SHOW_DESKTOP
				(i = tagtoindex(m->tagset[m->seltags])) &&
				m->focusontag[i - 1] &&
				m->focusontag[i - 1]->mon == m &&
				ISVISIBLE(m->focusontag[i - 1])
				)
				sel = m->focusontag[i - 1];
			else
				focus(NULL, 0);

			#if PATCH_SHOW_DESKTOP
			if (showdesktop && !m->sel) {
				Client *c, *d = NULL;
				unsigned int t = 0;	//, f = 0; // tiled and floating counts;
				for (c = m->clients; c; c = c->next)
				{
					if (c->isdesktop) {
						if (!d)
							d = c;
					}
					else if (ISVISIBLE(c)
						#if PATCH_FLAG_HIDDEN
						&& !c->ishidden
						#endif // PATCH_FLAG_HIDDEN
						#if PATCH_FLAG_NEVER_FOCUS
						&& !c->neverfocus
						#endif // PATCH_FLAG_NEVER_FOCUS
						#if PATCH_FLAG_PANEL
						&& !c->ispanel
						#endif // PATCH_FLAG_PANEL
					) {
						if (!c->isfloating
							|| !c->isfullscreen
							#if PATCH_FLAG_FAKEFULLSCREEN
							|| c->fakefullscreen != 1
							#endif // PATCH_FLAG_FAKEFULLSCREEN
						)
							t++;
						#if 0
						else
							#if PATCH_MODAL_SUPPORT
							if (!c->ismodal || !c->parent || c->parent->isfloating)
							#endif // PATCH_MODAL_SUPPORT
							f++;
						#endif
					}
				}
				#if PATCH_SHOW_DESKTOP_UNMANAGED
				if (showdesktop_unmanaged && desktopwin && !t) {
					m->showdesktop = 1;
					m->sel = NULL;
				}
				else
				#endif // PATCH_SHOW_DESKTOP_UNMANAGED
				if (d && !t) {
					m->showdesktop = 1;
					m->sel = d;
				}
			}
			#endif // PATCH_SHOW_DESKTOP
		}
		arrange(m);
		if (sel)
			focus(sel, 1);
		/*
		if (m->sel
			#if PATCH_ALTTAB
			&& !altTabMon
			#endif // PATCH_ALTTAB
			)
			focus(m->sel, 0);
		*/
		#if PATCH_EWMH_TAGS
		if (m == selmon)
			updatecurrentdesktop();
		#endif // PATCH_EWMH_TAGS

		#if PATCH_MOUSE_POINTER_WARPING
		// if bit 31 of arg.ui is set, view was changed by clicking the TagBar, so don't warp pointer;
		if (m->sel && !(tagmask & (1 << 31)) && !m->sel->isfullscreen
			#if PATCH_ALTTAB
			&& !altTabMon
			#endif // PATCH_ALTTAB
			)
			#if PATCH_MOUSE_POINTER_WARPING_SMOOTH
			warptoclient(m->sel, 0, 0);
			#else // NO PATCH_MOUSE_POINTER_WARPING_SMOOTH
			warptoclient(m->sel, 0);
			#endif // PATCH_MOUSE_POINTER_WARPING_SMOOTH
		#endif // PATCH_MOUSE_POINTER_WARPING
	}
	else
	#if PATCH_ALTTAB
	if (!altTabMon)
	#endif // PATCH_ALTTAB
	{
		m->sel = getfocusable(m, m->sel, 0);
		arrange(m);
	}
}

#if PATCH_KEY_HOLD
void
viewkeyholdclient(const Arg *arg)
{
	Client *c = keyholdclient;
	keyholdclient = NULL;
	if (!validclient(c)
		#if PATCH_FLAG_HIDDEN
		|| c->ishidden
		#endif // PATCH_FLAG_HIDDEN
		#if PATCH_FLAG_IGNORED
		|| c->isignored
		#endif // PATCH_FLAG_IGNORED
		#if PATCH_FLAG_PANEL
		|| c->ispanel
		#endif // PATCH_FLAG_PANEL
		#if PATCH_SHOW_DESKTOP
		|| c->isdesktop || c->ondesktop
		#endif // PATCH_SHOW_DESKTOP
		)
		return;

	if (!ISVISIBLE(c))
		viewmontag(c->mon, c->tags, 0);
	if (c->mon != selmon)
		focusmonex(c->mon);
	focus(c, 1);
	#if PATCH_FOCUS_FOLLOWS_MOUSE
	#if PATCH_MOUSE_POINTER_WARPING
	#if PATCH_MOUSE_POINTER_WARPING_SMOOTH
	warptoclient(c, 0, 1);
	#else // NO PATCH_MOUSE_POINTER_WARPING_SMOOTH
	warptoclient(c, 1);
	#endif // PATCH_MOUSE_POINTER_WARPING_SMOOTH
	#endif // PATCH_MOUSE_POINTER_WARPING
	#endif // PATCH_FOCUS_FOLLOWS_MOUSE
}
#endif // PATCH_KEY_HOLD

void
viewactive(const Arg *arg)
{
	if (!arg->i)
		return;
	#if PATCH_FOCUS_FOLLOWS_MOUSE
	checkmouseovermonitor(selmon);
	#endif // PATCH_FOCUS_FOLLOWS_MOUSE
	viewactiveex(selmon, arg->i);
}

void
viewactivenext(const Arg *arg)
{
	// arg->ui is monitor index number;
	Monitor *m;
	for (m = mons; arg->ui != m->num && m; m = m->next);
	if (m)
		viewactiveex(m, 1);
}
void
viewactiveprev(const Arg *arg)
{
	// arg->ui is monitor index number;
	Monitor *m;
	for (m = mons; arg->ui != m->num && m; m = m->next);
	if (m)
		viewactiveex(m, -1);
}

void
viewactiveex(Monitor *m, int direction)
{
	Client *c = NULL;
	int done = 0;
	signed int i;
	signed int active = 0;
	static signed int taglength = LENGTH(tags);

	// ascertain first active tag
	for (i = 0; i < taglength; i++)
		if (m->tagset[m->seltags] & (1 << i)) {
			active = i;
			break;
		}

	// new active starting tag will be current active ± 1

	active += (direction / abs(direction));

	// if new active is out of tag range, loop round
	if (active >= taglength)
		active = 0;
	if (active < 0)
		active = taglength-1;

	#if PATCH_HIDE_VACANT_TAGS
	if (m->alwaysvisible[active]) {
		if (abs(direction) == 2)
			viewmontag(m, (1 << active | 1 << 31), 0);
		else
			viewmontag(m, (1 << active), 0);
		return;
	}
	#endif // PATCH_HIDE_VACANT_TAGS

	for (done = 0; done < 2; active = (direction > 0) ? 0 : (taglength - 1))
	{
		// find next tag with clients and view it
		for (i = active; i >= 0 && i < taglength; i += direction)
			for (c = m->clients; c; c = c->next)
			{
				if (c->tags & (1 << i)
					#if PATCH_FLAG_IGNORED
					&& !c->isignored
					#endif // PATCH_FLAG_IGNORED
					#if PATCH_FLAG_PANEL
					&& !c->ispanel
					#endif // PATCH_FLAG_PANEL
				)
				{
					if (abs(direction) == 2)
						viewmontag(m, (1 << i | 1 << 31), 0);
					else
						viewmontag(m, (1 << i), 0);
					return;
				}
			}
		++done;
	}

}

void
waitforclearkeyboard(void)
{
	int clear = 0;
	struct timespec ts = { .tv_sec = 0, .tv_nsec = 1000000 };
	char keys_return[32];
	while (!clear) {
		clear = 1;
		XQueryKeymap(dpy, keys_return);
		for (int i = 0; i < 32; i++) {
			if (keys_return[i] != 0) {
				clear = 0;
				break;
			}
		}
		if (!clear)
			nanosleep(&ts, NULL);
	}
}

#if PATCH_MOUSE_POINTER_WARPING
#if PATCH_MOUSE_POINTER_WARPING_SMOOTH
typedef struct w2c_data w2c_data;
struct w2c_data {
	Display *dpy;
	Window root;
	// client window;
	Window win;
	// target area rectangle;
	int x;
	int y;
	int width;
	int height;
	// target pointer location;
	int targetx;
	int targety;
	//Cursor cursor;
	int force;
	int grab;
	Cursor cursor;	// if grab is non-zero;
};
void *
warppointer(void *arg)
{
	w2c_data *data = arg;
	Display *dpy = data->dpy;
	Window root = data->root;
	Window win = data->win;
	// target area coords;
	int tx = data->x;
	int ty = data->y;
	int tw = data->width;
	int th = data->height;
	// target pointer coords;
	int tpx = data->targetx;
	int tpy = data->targety;
	int force = data->force;
	int grab = data->grab;
	Cursor cursor = data->cursor;
	free(data);

	int px, py;			// starting pointer coords;

	// if the pointer is ungrabbable, we should not warp;
	if (grab)
		if (XGrabPointer(dpy, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync,
			None, cursor, CurrentTime) != GrabSuccess) return NULL;

	#define PI 3.14159265358979323846

	int warpx, warpy, curx, cury, lastx = 0, lasty = 0;

	getrootptr(&px, &py);

	int steps = MAX(abs(px-tpx),abs(py-tpy));
	// adjust delay time to account for approx cpu processing delay;
	unsigned long t = ((unsigned long)MOUSE_WARP_MILLISECONDS - 500L);
	t = ((t>0 ? t : 1) * 1000000L);

	struct timespec ts = { .tv_sec = 0, .tv_nsec = 0 };
	for (int i = steps; i > 0; i--) {
		if (warptoclient_stop_flag)
			break;
		warpx = (px + 2*(steps-i+1) * (tpx - px) / (2 * steps));
		warpy = (py + 2*(steps-i+1) * (tpy - py) / (2 * steps));
		if (abs(warpx-lastx) || abs(warpy-lasty))
		{
			// only warp when the position is different from last iteration;
			XWarpPointer(dpy, None, root, 0, 0, 0, 0, warpx, warpy);
			XSync(dpy, False);
			lastx = warpx;
			lasty = warpy;
		}
		ts.tv_nsec = (unsigned long)((t / steps)*((cos(1.8*PI*(steps-i+1)/steps)+1)/2));
		nanosleep(&ts, NULL);

		if (force == 1)
			continue;
		// stop moving prematurely if the mouse was moved and the pointer is over the target area
		if (getrootptr(&curx, &cury) && (curx != warpx || cury != warpy)
			#if PATCH_FOCUS_FOLLOWS_MOUSE
			&& (curx >= tx && curx <= tx + tw && cury >= ty && cury <= ty + th)
			#endif // PATCH_FOCUS_FOLLOWS_MOUSE
			)
			warptoclient_stop_flag = 1;
	}
	if (grab)
		XUngrabPointer(dpy, CurrentTime);

	if (warptoclient_stop_flag) {
		warptoclient_stop_flag = 0;
		return NULL;
	}
	if (win != None)
		XWarpPointer(dpy, None, win, 0, 0, 0, 0, tpx - tx, tpy - ty);
	else
		XWarpPointer(dpy, None, root, 0, 0, 0, 0, tpx, tpy);

	return NULL;
}

void
warptoclient(Client *c, int smoothly, int force)
#else // NO PATCH_MOUSE_POINTER_WARPING_SMOOTH
void
warptoclient(Client *c, int force)
#endif // PATCH_MOUSE_POINTER_WARPING_SMOOTH
{
	if (mousewarp_disable)
		return;

	int px, py;			// starting pointer coords;
	int tx, ty, tw, th; // target area coords;
	int tpx, tpy;		// target pointer coords;
	Monitor *pm = NULL;	// starting pointer bounding monitor;

	if (getrootptr(&px, &py))
		pm = recttomon(px, py, 1, 1);

	#if DEBUGGING
	if (c && c->dispclass && strcmp(c->dispclass, "NMS Editor")==0) {
		logdatetime(stderr);
		fprintf(stderr, "debug: selmon:%i ISVISIBLE(c):%u ispanel:%i neverfocus:%i\n", (selmon ? selmon->num : -1), ISVISIBLE(c), c->ispanel, c->neverfocus);
	}
	#endif // DEBUGGING

	if ((nonstop & 1) || !selmon)
		return;
	else if (c) {
		if (c->neverfocus ||
			#if PATCH_FLAG_PANEL
			c->ispanel ||
			#endif // PATCH_FLAG_PANEL
			!ISVISIBLE(c)
			)
			return;

		XWindowAttributes wa;
		XGetWindowAttributes(dpy, c->win, &wa);
		if (wa.map_state != IsViewable)
			return;
	}
	#if PATCH_CONSTRAIN_MOUSE
	if (constrained && c && pm != c->mon)
		return;
	#endif // PATCH_CONSTRAIN_MOUSE

	#if PATCH_MOUSE_POINTER_WARPING_RECALL
	if (c && c != selmon->sel
		#if PATCH_ALTTAB
		&& !altTabMon
		#endif // PATCH_ALTTAB
		)
		lastcoordsstore(selmon->sel);
	#endif // PATCH_MOUSE_POINTER_WARPING_RECALL

	// get target zone;
	if (c) {
		if (c->isfullscreen
			#if PATCH_FLAG_FAKEFULLSCREEN
			&& c->fakefullscreen != 1
			#endif // PATCH_FLAG_FAKEFULLSCREEN
		) {
			tx = selmon->mx;
			ty = selmon->my;
			tw = selmon->mw;
			th = selmon->mh;
			tpx = tx + tw / 2;
			tpy = ty + th / 2;
		} else {
			tx = c->x + c->bw;
			ty = c->y + c->bw;
			tw = c->w;
			th = c->h;
			#if PATCH_ALTTAB
			if (altTabMon) {
				tpx = c->x + c->bw + c->w/2;
				tpy = c->y + c->bw + c->h/2;
			}
			else
			#endif // NO PATCH_ALTTAB
			{
				#if PATCH_MOUSE_POINTER_WARPING_RECALL
				lastcoordsrecall(c, 0, 0, &tpx, &tpy);
				#else // NO PATCH_MOUSE_POINTER_WARPING_RECALL
				if (c->focusabs) {
					tpx = c->x + c->bw + c->focusdx + (c->focusdx < 0 ? c->w : 0);
					tpy = c->y + c->bw + c->focusdy + (c->focusdy < 0 ? c->h : 0);
				} else {
					tpx = c->x + c->bw + c->focusdx * c->w/2 + (c->focusdx < 0 ? c->w : 0);
					tpy = c->y + c->bw + c->focusdy * c->h/2 + (c->focusdy < 0 ? c->h : 0);
				}
				#endif // PATCH_MOUSE_POINTER_WARPING_RECALL
			}
		}
	}
	else if (selmon) {
		tx = selmon->wx;
		ty = selmon->wy;
		tw = selmon->ww;
		th = selmon->wh;
		tpx = tx + tw / 2;
		tpy = ty + th / 2;
	}

	#if DEBUGGING
	if (c && c->dispclass && strcmp(c->dispclass, "NMS Editor")==0) {
		logdatetime(stderr);
		fprintf(stderr, "debug: tx:%i ty:%i tw:%i th:%i tpx:%i tpy:%i\n", tx,ty,tw,th,tpx,tpy);
	}
	#endif // DEBUGGING

	#if PATCH_MOUSE_POINTER_WARPING_SMOOTH
	if (!mousewarp_smoothly)
		smoothly = 0;
	#endif // PATCH_MOUSE_POINTER_WARPING_SMOOTH
	// can't go smoothly if we can't find mouse pointer coords;
	// and no warp required if already over the client;
	else if (!pm || (nonstop & 1))
		#if PATCH_MOUSE_POINTER_WARPING_SMOOTH
		smoothly = 0
		#endif // PATCH_MOUSE_POINTER_WARPING_SMOOTH
		;
	else if (force < 1 && px >= tx && px <= (tx + tw) && py >= ty && py <= (ty + th)) {
		if (c && selmon->sel != c)
			focus(c, 0);
		return;
	}

	// no warping if current view is monocle AND pointer is already within the bounds of the monitor;
	if ((c && !c->isfloating && c->mon->lt[c->mon->sellt]->arrange == monocle && pm == c->mon)
		|| (c && c->mon->showbar && pointoverbar(c->mon, px, py, force == -1 ? 0 : 1))
	) {
		if (selmon->sel != c)
			focus(c, 0);
		return;
	}

	// don't warp if client at coords is not the target client;
	if (getclientatcoords(tpx, tpy, True) != c) {
		focus(c, 0);
		return;
	}

	#if PATCH_MOUSE_POINTER_WARPING_SMOOTH
	#if PATCH_FLAG_GAME
	Client *g;
	for (Monitor *m = mons; m; m = m->next) {
		for (g = m->clients; g && !(g->isgame && g->isfullscreen); g = g->next);
		if (g) {
			smoothly = 0;
			break;
		}
	}
	#endif // PATCH_FLAG_GAME
	if (smoothly)
	{
		warptoclient_stop_flag = 0;

		pthread_t th;
		w2c_data *th_data = ecalloc(1, sizeof(w2c_data));
		th_data->dpy = dpy;
		th_data->root = root;
		th_data->win = c ? c->win : None;
		th_data->x = tx;
		th_data->y = ty;
		th_data->width = tw;
		th_data->height = th;
		th_data->targetx = tpx;
		th_data->targety = tpy;
		th_data->force = force;
		if ((th_data->grab = (smoothly && force == 1) ? 0 : 1))
			th_data->cursor = cursor[CurNormal]->cursor;
		pthread_create(&th, NULL, warppointer, th_data);
		if (selmon->sel != c)
			focus(c, 0);
		return;
	}
	else
	#endif // PATCH_MOUSE_POINTER_WARPING_SMOOTH
	{
		if (c)
			XWarpPointer(dpy, None, c->win, 0, 0, 0, 0, tpx - tx, tpy - ty);
		else
			XWarpPointer(dpy, None, root, 0, 0, 0, 0, tpx, tpy);
		XSync(dpy, False);
		if (selmon->sel != c)
			focus(c, 0);
	}
}
#endif // PATCH_MOUSE_POINTER_WARPING

#if PATCH_EXTERNAL_WINDOW_ACTIVATION
int switcher_status = 0;
typedef struct war_data war_data;
struct war_data {
	pid_t pid;
};
void *
wait_and_reset(void *arg)
{
	pid_t wpid;
	war_data *data = arg;
	pid_t pid = data->pid;
	free(data);

	while ((wpid = waitpid(pid, &switcher_status, 0)) > 0);
	enable_switching = 0;
	return NULL;
}
void
window_switcher(const Arg *arg)
{
	#if PATCH_FOCUS_FOLLOWS_MOUSE
	checkmouseovermonitor(selmon);
	#endif // PATCH_FOCUS_FOLLOWS_MOUSE
	pthread_t th;
	pid_t pid = spawnex(arg->v, False);
	enable_switching = 1;
	war_data *th_data = ecalloc(1, sizeof(war_data));
	th_data->pid = pid;
	pthread_create(&th, NULL, wait_and_reset, th_data);
}
#endif // PATCH_EXTERNAL_WINDOW_ACTIVATION

pid_t
winpid(Window w)
{

	pid_t result = 0;

#ifdef __linux__
	xcb_res_client_id_spec_t spec = {0};
	spec.client = w;
	spec.mask = XCB_RES_CLIENT_ID_MASK_LOCAL_CLIENT_PID;

	xcb_generic_error_t *e = NULL;
	xcb_res_query_client_ids_cookie_t c = xcb_res_query_client_ids(xcon, 1, &spec);
	xcb_res_query_client_ids_reply_t *r = xcb_res_query_client_ids_reply(xcon, c, &e);

	if (!r)
		return (pid_t)0;

	xcb_res_client_id_value_iterator_t i = xcb_res_query_client_ids_ids_iterator(r);
	for (; i.rem; xcb_res_client_id_value_next(&i)) {
		spec = i.data->spec;
		if (spec.mask & XCB_RES_CLIENT_ID_MASK_LOCAL_CLIENT_PID) {
			uint32_t *t = xcb_res_client_id_value_value(i.data);
			result = *t;
			break;
		}
	}

	free(r);

	if (result == (pid_t)-1)
		result = 0;

#endif /* __linux__ */

#ifdef __OpenBSD__
        Atom type;
        int format;
        unsigned long len, bytes;
        unsigned char *prop;
        pid_t ret;

        if (XGetWindowProperty(dpy, w, XInternAtom(dpy, "_NET_WM_PID", 0), 0, 1, False, AnyPropertyType, &type, &format, &len, &bytes, &prop) != Success || !prop)
               return 0;

        ret = *(pid_t*)prop;
        XFree(prop);
        result = ret;

#endif /* __OpenBSD__ */
	return result;
}

int
isdescprocess(pid_t p, pid_t c)
{
	while (p != c && c != 0)
		c = getparentprocess(c);

	return (int)c;
}

#if PATCH_IPC
int
is_float(const char *s)
{
	size_t len = strlen(s);
	int is_dot_used = 0;
	int is_minus_used = 0;

	// Floats can only have one decimal point in between or digits
	// Optionally, floats can also be below zero (negative)
	for (int i = 0; i < len; i++) {
		if (isdigit(s[i]))
			continue;
		else if (!is_dot_used && s[i] == '.' && i != 0 && i != len - 1) {
			is_dot_used = 1;
			continue;
		} else if (!is_minus_used && s[i] == '-' && i == 0) {
			is_minus_used = 1;
			continue;
		} else
			return 0;
	}

	return 1;
}

int
is_hex(const char *s)
{
	size_t len = strlen(s);

	if (len > 2) {
		if (s[0] == '0' && s[1] == 'x') {
			for (int i = 2; i < len; i++)
				if (!isxdigit(s[i]))
					return 0;
			return 1;
		}
	}
	return 0;
}

int
is_signed_int(const char *s)
{
	size_t len = strlen(s);

	// Signed int can only have digits and a negative sign at the start
	for (int i = 0; i < len; i++) {
		if (isdigit(s[i]))
			continue;
		else if (i == 0 && s[i] == '-') {
			continue;
		} else
			return 0;
	}

	return 1;
}

int
is_unsigned_int(const char *s)
{
	size_t len = strlen(s);

	// Unsigned int can only have digits
	for (int i = 0; i < len; i++) {
		if (isdigit(s[i]))
			continue;
		else
			return 0;
	}

	return 1;
}
#endif // PATCH_IPC

Client *
wintoclient(Window w)
{
	Client *c;
	Monitor *m;

	if (w == root
		#if PATCH_SHOW_DESKTOP
		#if PATCH_SHOW_DESKTOP_UNMANAGED
		|| (showdesktop && showdesktop_unmanaged && w == desktopwin)
		#endif // PATCH_SHOW_DESKTOP_UNMANAGED
		#endif // PATCH_SHOW_DESKTOP
		)
		return NULL;

	for (m = mons; m; m = m->next)
		for (c = m->clients; c; c = c->next)
			if (c->win == w)
				return c;
	return NULL;
}
#if PATCH_SCAN_OVERRIDE_REDIRECTS
Client *
wintoorclient(Window w)
{
	Client *c;

	if (w == root
		#if PATCH_SHOW_DESKTOP
		#if PATCH_SHOW_DESKTOP_UNMANAGED
		|| (showdesktop && showdesktop_unmanaged && w == desktopwin)
		#endif // PATCH_SHOW_DESKTOP_UNMANAGED
		#endif // PATCH_SHOW_DESKTOP
		)
		return NULL;

	for (c = orlist; c; c = c->next)
		if (c->win == w)
			return c;
	return NULL;
}
#endif // PATCH_SCAN_OVERRIDE_REDIRECTS

#if PATCH_SYSTRAY
Client *
wintosystrayicon(Window w) {
	Client *i;

	if (!showsystray || !w)
		return NULL;
	for (i = systray->icons; i && i->win != w; i = i->next) ;
	return i;
}
#endif // PATCH_SYSTRAY

Monitor *
wintomon(Window w)
{
	int x, y;
	Client *c;
	Monitor *m;

	if ((w == root
		#if PATCH_SHOW_DESKTOP
		#if PATCH_SHOW_DESKTOP_UNMANAGED
		|| (showdesktop && showdesktop_unmanaged && w == desktopwin)
		#endif // PATCH_SHOW_DESKTOP_UNMANAGED
		#endif // PATCH_SHOW_DESKTOP
		) && getrootptr(&x, &y))
		return recttomon(x, y, 1, 1);
	#if PATCH_FOCUS_BORDER
	else if (w == focuswin && selmon->sel)
		return selmon;
	#endif // PATCH_FOCUS_BORDER
	#if PATCH_SYSTRAY
	else if (systray && w == systray->win)
		return systraytomon(NULL);
	#endif // PATCH_SYSTRAY
	for (m = mons; m; m = m->next)
		if (w == m->barwin)
			return m;
	if ((c = wintoclient(w))
		#if PATCH_CROP_WINDOWS
		|| (c = cropwintoclient(w))
		#endif // PATCH_CROP_WINDOWS
		)
		return c->mon;
	return selmon;
}

#if PATCH_IPC
ssize_t
write_socket(const void *buf, size_t count)
{
	size_t written = 0;

	while (written < count) {
		const ssize_t n = write(sock_fd, ((uint8_t *)buf) + written, count - written);

		if (n == -1) {
			if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
				continue;
			else
				return n;
		}
		written += n;
	}
	return written;
}
#endif // PATCH_IPC

/* There's no way to check accesses to destroyed windows, thus those cases are
 * ignored (especially on UnmapNotify's). Other types of errors call Xlibs
 * default error handler, which may call exit. */
int
xerror(Display *dpy, XErrorEvent *ee)
{
	if (ee->error_code == BadWindow
	|| (ee->request_code == X_SetInputFocus && ee->error_code == BadMatch)
	|| (ee->request_code == X_PolyText8 && ee->error_code == BadDrawable)
	|| (ee->request_code == X_PolyFillRectangle && ee->error_code == BadDrawable)
	|| (ee->request_code == X_PolySegment && ee->error_code == BadDrawable)
	|| (ee->request_code == X_ConfigureWindow && ee->error_code == BadMatch)
	|| (ee->request_code == X_GrabButton && ee->error_code == BadAccess)
	|| (ee->request_code == X_GrabKey && ee->error_code == BadAccess)
	|| (ee->request_code == X_CopyArea && ee->error_code == BadDrawable))
	{
		if (ee->error_code == BadWindow) {
			Client *c;
			if ((c = wintoclient(ee->resourceid))) {
				c->dormant = -1;
				c->isfloating = c->isfloating_override = 1;
			}
		}
		return 0;
	}
	if (ee->error_code & FirstExtensionError)
		/* error requesting input on a particular xinput device */
		return 0;

	logdatetime(stderr);
	fprintf(stderr, "dwm: fatal error: request code=%d, error code=%d\n",
		ee->request_code, ee->error_code);
	return xerrorxlib(dpy, ee); /* may call exit */
}

int
xerrordummy(Display *dpy, XErrorEvent *ee)
{
	return 0;
}

/* Startup Error handler to check if another window manager
 * is already running. */
int
xerrorstart(Display *dpy, XErrorEvent *ee)
{
	die("dwm: another window manager is already running");
	return -1;
}

#if PATCH_ALPHA_CHANNEL
int
has_compositor(Display *dpy, int screen) {
    char prop_name[20];
    snprintf(prop_name, 20, "_NET_WM_CM_S%d", screen);
    Atom prop_atom = XInternAtom(dpy, prop_name, False);
    return XGetSelectionOwner(dpy, prop_atom) != None;
}

void
xinitvisual()
{
    XVisualInfo *infos;
	XRenderPictFormat *fmt;
	int nitems;
	int i;

	XVisualInfo tpl = {
        .screen = screen,
		.depth = 32,
		.class = TrueColor
	};
	long masks = VisualScreenMask | VisualDepthMask | VisualClassMask;

	infos = XGetVisualInfo(dpy, masks, &tpl, &nitems);
	visual = NULL;
	for(i = 0; i < nitems; i ++) {
        fmt = XRenderFindVisualFormat(dpy, infos[i].visual);
		if (fmt->type == PictTypeDirect && fmt->direct.alphaMask) {
            visual = infos[i].visual;
			depth = infos[i].depth;
			cmap = XCreateColormap(dpy, root, visual, AllocNone);
			useargb = 1;
			break;
        }
    }

	XFree(infos);

	if (! visual) {
        visual = DefaultVisual(dpy, screen);
		depth = DefaultDepth(dpy, screen);
		cmap = DefaultColormap(dpy, screen);
    }
}
#endif // PATCH_ALPHA_CHANNEL

#if PATCH_SYSTRAY
Monitor *
systraytomon(Monitor *m) {
	Monitor *t;
	if(systraypinning == -1) {
		if(!m)
			return selmon;
		return m == selmon ? m : NULL;
	}
	for (t = mons; t && t->num != systraypinning && t->next; t = t->next);
	if (t->num == systraypinning)
		return t;
	else
		return (systraypinningfailfirst ? mons : t);
}
#endif // PATCH_SYSTRAY

void
zoom(const Arg *arg)
{
	#if PATCH_FOCUS_FOLLOWS_MOUSE
	checkmouseoverclient();
	#endif // PATCH_FOCUS_FOLLOWS_MOUSE
	Client *c = selmon->sel;
	Client *t = c;
	Client *f = c;

	if (!selmon->lt[selmon->sellt]->arrange || !c || c->isfloating)
		return;
	if (c == nexttiled(selmon->clients) && !(t = nexttiled(c->next)))
		return;

	if (nexttiled(selmon->clients) == f)
		f = t;
	else
		f = c;

	#if PATCH_MOUSE_POINTER_WARPING || PATCH_FOCUS_FOLLOWS_MOUSE
	int ok = 0;
	int px, py;
	unsigned int cw, ch;
	float sfw, sfh;
	if (f == c) {
		ok = getrelativeptr(c, &px, &py);
		cw = c->w;
		ch = c->h;
	}
	#endif // PATCH_MOUSE_POINTER_WARPING || PATCH_FOCUS_FOLLOWS_MOUSE

	detach(t);
	attach(t);
	focus(f, 1);
	arrange(t->mon);

	#if PATCH_MOUSE_POINTER_WARPING || PATCH_FOCUS_FOLLOWS_MOUSE
	if (!ok)
		return;
	sfw = ((float) c->w / cw);
	sfh = ((float) c->h / ch);
	XWarpPointer(dpy, None, c->win, 0, 0, 0, 0, (int)(px*sfw), (int)(py*sfh));
	#endif // PATCH_MOUSE_POINTER_WARPING || PATCH_FOCUS_FOLLOWS_MOUSE

}

char *
readfile(const char *filename, const char *filetype)
{
	int success = 0;
	char *source = NULL;

	FILE *fp = fopen(filename, "r");
	if (fp != NULL) {

		// Go to the end of the file.;
		if (fseek(fp, 0L, SEEK_END) == 0) {

			// Get the size of the file.;
			long bufsize = ftell(fp);
			if (bufsize > 0) {

				// Allocate our buffer to that size.;
				source = malloc(sizeof(char) * (bufsize + 1));

				// Go back to the start of the file.;
				if (fseek(fp, 0L, SEEK_SET) == 0) {

					// Read the entire file into memory.;
					size_t newLen = fread(source, sizeof(char), bufsize, fp);
					if ( ferror( fp ) != 0 ) {
						logdatetime(stderr);
						fprintf(stderr, "dwm: Error reading %s file \"%s\".\n", filetype, filename);
					} else {
						source[newLen++] = '\0'; /* Just to be safe. */
						success = 1;
					}
				}
				if (!success)
					free(source);
			}
			else {
				logdatetime(stderr);
				if (bufsize == -1)
					fprintf(stderr, "dwm: Error reading size of %s file \"%s\".\n", filetype, filename);
				else
					fprintf(stderr, "dwm: The %s file \"%s\" appears to be empty.\n", filetype, filename);
			}

		}
		else {
			logdatetime(stderr);
			fprintf(stderr, "dwm: Unable to seek within the %s file: \"%s\".\n", filetype, filename);
		}
		fclose(fp);
	}
	else {
		logdatetime(stderr);
		fprintf(stderr, "dwm: Unable to open the %s file: \"%s\".\n", filetype, filename);
	}

	return (success ? source : NULL);
}

cJSON *
parsejsonfile(const char *filename, const char *filetype)
{
	cJSON *json = NULL;

	char *data = readfile(filename, filetype);
	if (data) {
		json = cJSON_Parse(data);
		const char *error_ptr = cJSON_GetErrorPtr();
		if (error_ptr != NULL) {
			logdatetime(stderr);
			fprintf(stderr, "dwm: Error while parsing the %s JSON file \"%s\" before: %s\n", filetype, filename, error_ptr);
		}
		else if (!json) {
			logdatetime(stderr);
			fprintf(stderr, "dwm: Error parsing the %s JSON file \"%s\".\n", filetype, filename);
		}
		free(data);
	}
	return json;
}

int
main(int argc, char *argv[], char *envp[])
{
	FILE *f = stdout;
	int r = 0, l = 0;
	#if PATCH_IPC
	int ipc = 0;
	unsigned int wrap_length = WRAP_LENGTH;
	struct winsize window_size = {0};
	if (ioctl(fileno(f), TIOCGWINSZ, &window_size) != -1) {
		wrap_length = window_size.ws_col;
	}
	#endif // PATCH_IPC

	coloursbackup = malloc(sizeof(colours));
	memcpy(coloursbackup, colours, sizeof(colours));
	#if PATCH_CUSTOM_TAG_ICONS
	tagiconpathsbackup = malloc(sizeof(tagiconpaths));
	memcpy(tagiconpathsbackup, tagiconpaths, sizeof(tagiconpaths));
	#endif // PATCH_CUSTOM_TAG_ICONS

reload:
	for (int i = 0; i < LENGTH(colourflags); i++) {
		if (colourflags[i]) {
			if (colourflags[i] & 1)
				colours[i][ColFg] = NULL;
			if (colourflags[i] & 2)
				colours[i][ColBg] = NULL;
			if (colourflags[i] & 4)
				colours[i][ColBorder] = NULL;
		}
	}

	if (argc > 1) {

		for (int i = 1; i < argc; i++) {
			if (!strcmp("-v", argv[i]))
				die(DWM_VERSION_STRING_LONG);

			else if (!strcmp("-u", argv[i]))
				urgency = False;

			else if (!strcmp("-w", argv[i]))
				config_warnings ^= 1;

			else if (!strcmp("-r", argv[i])) {
				if ((r = ++i) >= argc)
					return(usage("error: No rules file specified after -r switch."));
			}

			else if (!strcmp("-l", argv[i])) {
				if ((l = ++i) >= argc)
					return(usage("error: No layout file specified after -l switch."));
			}

			#if PATCH_SYSTRAY
			else if (!strcmp("-n", argv[i]))
				showsystray = 0;
			#endif // PATCH_SYSTRAY

			else if (!strcmp("-h", argv[i])) {

				print_supported_json(f,
					supported_layout_global,
					LENGTH(supported_layout_global),
					"layout-file.json supported names:\n=================================\n\n    main section:\n    -------------",
					"        "
				);
				print_supported_json(f,
					supported_layout_mon,
					LENGTH(supported_layout_mon),
					"    monitor sections:\n    -----------------",
					"        "
				);
				print_supported_json(f,
					supported_layout_tag,
					LENGTH(supported_layout_tag),
					"    tags sections (per monitor):\n    ----------------------------",
					"        "
				);
				print_supported_rules_json(f,
					supported_rules,
					LENGTH(supported_rules),
					"\nrules-file.json supported names:\n================================\n",
					"    "
				);

				usage(NULL);
				#if PATCH_IPC
				if (LENGTH(ipccommands)) {

					const char *indent = "    ";
					print_wrap(f, wrap_length, NULL, -1, "IPC verbs:", NULL, NULL, NULL);
					print_wrap(f, wrap_length, indent, 27 , "find_dwm_client [name]", indent, NULL,
						"Find a DWM client Window whose name or class/instance match the specified name"
					);
					print_wrap(f, wrap_length, indent, 27 , "get_dwm_client [Window ID]", indent, NULL,
						"Return DWM client properties for the specified window (defaults to the active window)"
					);
					print_wrap(f, wrap_length, indent, 27 , "get_layouts", indent, NULL, "Return a list of layouts");
					print_wrap(f, wrap_length, indent, 27 , "get_monitors", indent, NULL, "Return monitor properties");
					print_wrap(f, wrap_length, indent, 27 , "get_tags", indent, NULL, "Return a list of all tags");
					print_wrap(f, wrap_length, indent, 27 , "run_command", indent, NULL, "Runs an IPC command");
					print_wrap(f, wrap_length, indent, 27 , "subscribe <event> ...", indent, NULL, "Subscribe to the specified events");

					print_wrap(f, wrap_length, NULL, -1, "\nIPC events:", NULL, NULL, NULL);
					print_wrap(f, wrap_length, indent, -1, IPC_EVENT_STRING_CLIENT_FOCUS_CHANGE, NULL, NULL, NULL);
					print_wrap(f, wrap_length, indent, -1, IPC_EVENT_STRING_FOCUSED_STATE_CHANGE, NULL, NULL, NULL);
					print_wrap(f, wrap_length, indent, -1, IPC_EVENT_STRING_FOCUSED_TITLE_CHANGE, NULL, NULL, NULL);
					print_wrap(f, wrap_length, indent, -1, IPC_EVENT_STRING_LAYOUT_CHANGE, NULL, NULL, NULL);
					print_wrap(f, wrap_length, indent, -1, IPC_EVENT_STRING_MONITOR_FOCUS_CHANGE, NULL, NULL, NULL);
					print_wrap(f, wrap_length, indent, -1, IPC_EVENT_STRING_TAG_CHANGE, NULL, NULL, NULL);

					print_wrap(f, wrap_length, NULL, -1, "\nIPC commands:", NULL, NULL, NULL);
					for (i = 0; i < LENGTH(ipccommands); i++)
						print_wrap(f, wrap_length, indent, -1, ipccommands[i].name, NULL, NULL, NULL);
					fputs("\n", stdout);
				}
				#endif // PATCH_IPC
				return EXIT_SUCCESS;
			}

			#if PATCH_IPC
			else if (!strcmp("-p", argv[i])) {
				if (i + 1 >= argc)
					return(usage("error: No socket path specified after -p switch."));
				++i;
				socketpath = argv[i];
			}
			else if (!strcmp("-s", argv[i])) {
				if (i + 1 >= argc)
					return(usage("error: No IPC verb specified after -s switch."));
				ipc = 1;
			}
			else if (ipc) {

				if (!strcmp("--ignore-reply", argv[i]))
					ipc_ignore_reply = 1;

				else if (!strcmp("get_monitors", argv[i])) {
					get_monitors();
					return EXIT_SUCCESS;
				}

				else if (!strcmp("get_tags", argv[i])) {
					get_tags();
					return EXIT_SUCCESS;
				}

				else if (!strcmp("get_layouts", argv[i])) {
					get_layouts();
					return EXIT_SUCCESS;
				}

				else if (!strcmp("get_dwm_client", argv[i])) {
					if (++i >= argc) {
						//return(usage("error: No window ID specified after -s get_dwm_client."));
						get_dwm_client(None);
						return EXIT_SUCCESS;
					}
					else if (is_unsigned_int(argv[i]) || is_hex(argv[i])) {
						Window win = (argv[i][1] == 'x') ? strtol(argv[i], NULL, 16) : atol(argv[i]);
						get_dwm_client(win);
						return EXIT_SUCCESS;
					}
					else
						return(usage("error: Window ID specified (after -s get_dwm_client) must be an unsigned integer."));
				}

				else if (!strcmp("find_dwm_client", argv[i])) {
					if (++i >= argc) {
						return(usage("error: No search string specified after -s find_dwm_client."));
					}
					else {
						find_dwm_client(argv[i]);
						return EXIT_SUCCESS;
					}
				}

				else if (!strcmp("run_command", argv[i])) {
					if (++i >= argc)
						return(usage("error: No IPC command specified after -s run_command."));

					char *command = argv[i];
					// Command arguments are everything after command name
					char **command_args = argv + ++i;
					// Number of command arguments
					int command_argc = argc - i;
					if (run_command(command, command_args, command_argc))
						return EXIT_SUCCESS;
					else
						return EXIT_FAILURE;
				}

				else if (!strcmp("subscribe", argv[i])) {
					if (++i >= argc)
						return(usage("error: No IPC events specified after -s subscribe."));
					for (int j = i; j < argc; j++) subscribe(argv[j]);
					while (1) {
						print_socket_reply();
					}
				}

			}
			#endif // PATCH_IPC


			else
				return (usage(NULL));
		}
	}

	// ***********************************************************************;
	// check if environment works;
	const char *env = "XDG_RUNTIME_DIR";
	if (!getenv(env)) {
		logdatetime(stderr);
		fputs("dwm: environment failed.", stderr);
		rc = 1;
		goto finish;
	}
	// ***********************************************************************;


	#if PATCH_LOG_DIAGNOSTICS
	logdatetime(stderr);
	fputs(DWM_VERSION_STRING_LONG" starting...", stderr);
	#if DEBUGGING
	fputs(" (DEBUGGING is on)", stderr);
	#endif // DEBUGGING
	fputs("\n", stderr);
	#endif // PATCH_LOG_DIAGNOSTICS

	if (l) {
		layout_json = parsejsonfile(argv[l], "layout");
		if (layout_json)
			parselayoutjson(layout_json);
	}
	if (r) {
		rules_filename = argv[r];
		reload_rules();
	}

	if (!setlocale(LC_CTYPE, "") || !XSupportsLocale()) {
		logdatetime(stderr);
		fputs("dwm: warning: no locale support\n", stderr);
	}
	if (!(dpy = XOpenDisplay(NULL)))
		die("dwm: cannot open display");
	if (!(xcon = XGetXCBConnection(dpy)))
		die("dwm: cannot get xcb connection");

	checkotherwm();
#ifdef __OpenBSD__
	if (pledge("stdio rpath proc exec ps", NULL) == -1)
		die("pledge");
#endif /* __OpenBSD__ */

	for (int i = 0; i < LENGTH(colours); i++) {
		colourflags[i] = 0;
		if (!colours[i][ColFg])
			colourflags[i] |= 1;
		if (!colours[i][ColBg])
			colourflags[i] |= 2;
		if (!colours[i][ColBorder])
			colourflags[i] |= 4;
	}

	if (!setup()) {
		rc = 1;
		goto finish;
	}

	scan();
	//#if PATCH_FOCUS_FOLLOWS_MOUSE || PATCH_MOUSE_POINTER_HIDING
	#if PATCH_MOUSE_POINTER_HIDING
	snoop_root();
	#endif // PATCH_MOUSE_POINTER_HIDING
	//#endif // PATCH_FOCUS_FOLLOWS_MOUSE || PATCH_MOUSE_POINTER_HIDING
	#if PATCH_MOUSE_POINTER_HIDING
	setup_sync_counters();
	if (cursortimeout > 0) {
		// starts the timer;
		showcursor();
	}
	if (cursor_always_hide)
		hidecursor();
	#endif // PATCH_MOUSE_POINTER_HIDING
	#if PATCH_LOG_DIAGNOSTICS
	logdatetime(stderr);
	fputs(DWM_VERSION_STRING_LONG" ready.", stderr);
	#if DEBUGGING
	fputs(" (DEBUGGING is on)", stderr);
	#endif // DEBUGGING
	#if PATCH_IPC
	fprintf(stderr, " (IPC socket \"%s\" FD:%d)", ipcsockpath, sock_fd);
	#endif // PATCH_IPC
	fputs("\n", stderr);
	#endif // PATCH_LOG_DIAGNOSTICS
	run();

finish:
	#if PATCH_IPC
	ipc_cleanup();
	if (ipcsockpath) {
		free(ipcsockpath);
		ipcsockpath = NULL;
	}
	#endif // PATCH_IPC

	cleanup();

	fonts_json = NULL;
	monitors_json = NULL;
	if (layout_json)
		cJSON_Delete(layout_json);
	if (rules_json)
		cJSON_Delete(rules_json);
	if (rules_compost)
		cJSON_Delete(rules_compost);

	killable = 1;

fprintf(stderr, "debug: pre XCloseDisplay()\n");
	//XGrabServer(dpy);
	XCloseDisplay(dpy);
	//XUngrabServer(dpy);

logdatetime(stderr);
fputs("dwm: waiting for any child processes...\n", stderr);

	while (waitpid(-1, NULL, WNOHANG) > 0);

logdatetime(stderr);
fputs("dwm: finished.\n", stderr);

	// reload;
	if (running == -1) {
		#if PATCH_CUSTOM_TAG_ICONS
		memcpy(tagiconpaths, tagiconpathsbackup, sizeof(tagiconpaths));
		#endif // PATCH_CUSTOM_TAG_ICONS
		memcpy(colours, coloursbackup, sizeof(colours));
		running = 1;
		logdatetime(stderr);
		fputs("dwm: reloading...\n", stderr);
		goto reload;
	}

	free(coloursbackup);

	return rc;
}

int
usage(const char * err_text)
{
	FILE *f = (err_text ? stderr : stdout);
	struct winsize window_size = {0};
	unsigned int wrap_length = WRAP_LENGTH;
	if (ioctl(fileno(f), TIOCGWINSZ, &window_size) != -1) {
		wrap_length = window_size.ws_col;
	}

	if (err_text)
		fprintf(f, "%s\n", err_text);
	print_wrap(f, wrap_length, NULL, 10,
		"\nusage: dwm", NULL, NULL, "\n[-h] [-v] [-w] [-r <rules-file.json>] [-l <layout-file.json>] [-u]"
		#if PATCH_SYSTRAY
		" [-n]"
		#endif // PATCH_SYSTRAY
		#if PATCH_IPC
		"\n[-p <socket-path>] [-s <verb> [command [args]]]"
		#endif // PATCH_IPC
		"\n"
	);

	const char *indent = "    ";
	print_wrap(f, wrap_length, indent, 2, "-h", indent, NULL, "display usage and accepted configuration paramters");
	print_wrap(f, wrap_length, indent, 2, "-v", indent, NULL, "display version information");
	print_wrap(f, wrap_length, indent, 2, "-w", indent, NULL, "toggle display of JSON config warnings");
	#if PATCH_SYSTRAY
	print_wrap(f, wrap_length, indent, 2, "-n", indent, NULL, "disable dwm system tray functionality");
	#endif // PATCH_SYSTRAY
	print_wrap(f, wrap_length, indent, 2, "-r", indent, NULL, "use the rules defined in the specified JSON rules file");
	print_wrap(f, wrap_length, indent, 2, "-l", indent, NULL, "use the layout configuration defined in the specified JSON layout file");
	print_wrap(f, wrap_length, indent, 2, "-u", indent, NULL, "disable client urgency hinting");
	#if PATCH_IPC
	print_wrap(f, wrap_length, indent, 2, "-p", indent, NULL, "path to unix socket");
	print_wrap(f, wrap_length, indent, 2, "-s", indent, NULL, "send request to running instance via socket");
	#endif // PATCH_IPC
	fputs("\n", f);

	return (err_text ? EXIT_FAILURE : EXIT_SUCCESS);
}
