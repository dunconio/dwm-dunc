# dwm-dunc-6.5
[![GitHub version](https://img.shields.io/github/v/tag/dunconio/dwm-dunc-6.5?label=version&style=flat-square)](https://github.com/dunconio/dwm-dunc-6.5/releases/latest)
[![GitHub stars](https://img.shields.io/github/stars/dunconio/dwm-dunc-6.5.svg?style=flat-square)](https://github.com/dunconio/dwm-dunc-6.5/stargazers)
![GitHub code size in bytes](https://img.shields.io/github/languages/code-size/dunconio/dwm-dunc-6.5?style=flat-square)
[![GitHub contributors](https://img.shields.io/github/contributors/dunconio/dwm-dunc-6.5.svg?style=flat-square)](https://github.com/dunconio/dwm-dunc-6.5/graphs/contributors)
[![GitHub issues](https://img.shields.io/github/issues/dunconio/dwm-dunc-6.5.svg?style=flat-square)](https://github.com/dunconio/dwm-dunc-6.5/issues)
[![GitHub license](https://img.shields.io/github/license/dunconio/dwm-dunc-6.5.svg?style=flat-square)](https://github.com/dunconio/dwm-dunc-6.5/blob/dev/LICENSE)

## dwm - dynamic window manager

dwm is an extremely fast, small, and dynamic window manager for X.
This custom fork is based on dwm 6.5.

### Key Changes from stock dwm:
* The core Rule array is replaced by run-time rule processing logic, reading from a json file;
* Global settings and per-monitor/tag parameters can be read from a second json file;

### Baked-in Patches
The following functionality is based on specific named patches:
* `cfacts`:	client sizing factor support in various layouts, which can be changed at run-time;
* `dragfact` &mdash; `dragcfact` &amp; `dragmfact` combined, to give a mouse-driven way of changing master and client sizing factors;
* `vanitygaps`:	optional gaps between clients and monitor edges;
	-	includes layouts:
		-	`tile`*
		-	`monocle`*
		-	`spiral`
		-	`dwindle`
		-	`deck`
		-	`bstack`
		-	`bstackhoriz`
		-	`grid`
		-	`nrowgrid`
		-	`horizgrid`
		-	`gaplessgrid`
		-	`centredmaster`
		-	`centredfloatingmaster`

### Optional Patches
The following are optional patches, some originally based on other patches.
These can be enabled/disabled by editing `patches.h`

| Core Functionality Patch | Details |
|---|---|
| `PATCH_ALPHA_CHANNEL` | Add transparency to dwm windows and borders.<br /><ul><li>Colour schemes can be in the format #rrggbbaa</li></ul> |
| `PATCH_ALT_TAGS` | Tags can have an alternative character, which will be shown by default.<br /><ul><li>Triggering the `togglealttags` function will show the normal tag character while the key is pressed, and will temporarily reveal the bar if it is hidden.</li></ul> |
| `PATCH_ALTTAB` | Alt-tab task switcher with bells on.<ul><li>provides standard alt-tab switcher and mouse-based drop-down switcher on the WinTitle bar element;</li><li>client switching for: -</li></li><ul><li>the current tag on the current or all monitors;</li><li>clients matching the current class on the current or all monitors;</li><li>all clients on the current or all monitors;</li></ul></ul> |
| <ul>`PATCH_ALTTAB_HIGHLIGHT`</ul> | Supports highlight of clients during alt-tab switching; |
| `PATCH_ATTACH_BELOW_AND_NEWMASTER` | New clients will be added to the stack after the master client(s), unless their `newmaster` client flag is `true`; |
| `PATCH_CLIENT_INDICATORS` | Add blobs to the bar tag to indicate clients visible on that tag; |
| `PATCH_CONSTRAIN_MOUSE` | Add a toggle to constrain the mouse pointer to the current monitor; |
| `PATCH_SHOW_DESKTOP` | Add support for showing the desktop; |
| <ul>`PATCH_SHOW_DESKTOP_ONLY_WHEN_ACTIVE`</ul> | Support only showing the desktop when a desktop client exists; |
| <ul>`PATCH_SHOW_DESKTOP_WITH_FLOATING`</ul> | Allow visible floating clients to remain visible while the desktop is shown; |
| `PATCH_EWMH_TAGS` | Supports reporting workspace(tag) changes; |
| `PATCH_EXTERNAL_WINDOW_ACTIVATION` | Behaves like spawn except will allow focus stealing for the life of the spawnee; |
| `PATCH_FOCUS_BORDER` | Enlarge the top border edge of focused clients; |
| `PATCH_FOCUS_FOLLOWS_MOUSE` | Focus clients under the mouse pointer; |
| `PATCH_HIDE_VACANT_TAGS` | Hides vacant tabs (except the active tag); |
| `PATCH_IPC` | Provides socket-based message handling; |
| `PATCH_LOG_DIAGNOSTICS` | Diagnostic functions that log to `stderr`; |
| `PATCH_MIRROR_LAYOUT` | Swap the master and stack areas where applicable; |
| `PATCH_MODAL_SUPPORT` | Ensures floating modal child clients get focused appropriately |
| `PATCH_MOUSE_POINTER_HIDING` | Support for hiding the mouse pointer when idle or typing; |
| `PATCH_MOUSE_POINTER_WARPING` | Warp the mouse pointer to visible clients that take focus when created, or after another client is closed; |
| <ul>`PATCH_MOUSE_POINTER_WARPING_SMOOTH`</ul> | Use a fluid warping movement rather than instantaneous warping; |
| `PATCH_MOVE_FLOATING_WINDOWS` | Move floating clients with the keyboard; |
| `PATCH_MOVE_TILED_WINDOWS` | Move tiled clients up and down the order with the keyboard; |
| `PATCH_PERSISTENT_METADATA` | Store client metadata, restore metadata after a restart; |
| `PATCH_PERTAG` | Per-tag monitor settings; |
| `PATCH_SCAN_OVERRIDE_REDIRECTS` | Picks up `override_redirect` windows during `scan()` for diagnostic use; |
| `PATCH_SHOW_MASTER_CLIENT_ON_TAG` | Shows the first `master` client on each tag on the bar; |
| `PATCH_STATUSCMD` | Supports using `dwmblocks` or similar to manage the status bar content; |
| `PATCH_SWITCH_TAG_ON_EMPTY` | Enables tag switch if there are no visible clients; |
| `PATCH_SYSTRAY` | System tray support; |
| `PATCH_TERMINAL_SWALLOWING` | Support terminal clients (with `isterminal` flag set) being swallowed by a child client for the duration it exists; |
| `PATCH_TORCH` | Turn all of your monitors to one of two colours (like a big torch); |
| `PATCH_TWO_TONE_TITLE` | Support a second colour for the WinTitle bar element background, to create a gradient effect (like classic Windows 98/2000) |
| `PATCH_WINDOW_ICONS` | Show client icon on the WinTitle bar element;<ul><li>Supports showing icons in `PATCH_ALTTAB` alt-tab switchers</li></ul> |
| <ul>`PATCH_WINDOW_ICONS_CUSTOM_ICONS`</ul> | Enable per-client a user-specified icon; |
| <ul>`PATCH_WINDOW_ICONS_DEFAULT_ICON`</ul> | Set a default icon for clients without; |
| <ul>`PATCH_WINDOW_ICONS_LEGACY_ICCCM`</ul> | Kludge to populate window icons the old way, for legacy applications; |
| `PATCH_XFTLIB_EMOJI_WORKAROUND` | Workaround for a crash with colour emojis on some systems (set to `0` when `XftLib` version is 2.3.5 or greater); |
|- |- |



| Client Flag Patch | Details |
|---|---|
| `PATCH_FLAG_ALWAYSONTOP` | Client will appear above clients of the same type;<br>Can be toggled; |
| `PATCH_FLAG_CENTRED` | Centre floating clients relative to the monitor or the parent client; |
| `PATCH_FLAG_FAKEFULLSCREEN` | Client will be made fullscreen within its tile;<br>Can be toggled; |
| `PATCH_FLAG_FLOAT_ALIGNMENT` | Floating clients initial position and/or fixed alignment; |
| `PATCH_FLAG_FOLLOW_PARENT` | Client will follow its parent (when its parent moves tags or to a different monitor) |
| `PATCH_FLAG_GAME` |  |
| `PATCH_FLAG_GAME_STRICT` | Game clients will by default not minimize on focus loss to a different monitor; |
| `PATCH_FLAG_GREEDY_FOCUS` | Prevents a client from losing focus due to mouse movement, depends on `PATCH_FOCUS_FOLLOWS_MOUSE` |
| `PATCH_FLAG_HIDDEN` |  |
| `PATCH_FLAG_IGNORED` |  |
| `PATCH_FLAG_NEVER_FOCUS` | Rule enables override of neverfocus wm hint; |
| `PATCH_FLAG_NEVER_FULLSCREEN` | Rule prevent clients from setting themselves fullscreen; |
| `PATCH_FLAG_NEVER_MOVE` | Rule prevent clients from moving themselves; |
| `PATCH_FLAG_NEVER_RESIZE` | Rule prevent clients from resizing themselves; |
| `PATCH_FLAG_PANEL` |  |
| `PATCH_FLAG_PAUSE_ON_INVISIBLE` |  |
| `PATCH_FLAG_PARENT` | Treat client as if its parent is the specified window (of same class if rule is deferred); |
| `PATCH_FLAG_STICKY` | Client will be visible on all tags;<br>Can be toggled; |
| `PATCH_FLAG_TITLE` | Displayed title is specified by user; |
|-|-|


### Requirements

In order to build dwm you need the Xlib header files.
	

### Installation

Edit config.mk to match your local setup (dwm is installed into
the /usr/local namespace by default).

Afterwards enter the following command to build and install dwm (if
necessary as root):

	make clean install


### Running dwm

Add the following line to your .xinitrc to start dwm using startx:

    exec dwm

In order to connect dwm to a specific display, make sure that
the DISPLAY environment variable is set correctly, e.g.:

    DISPLAY=foo.bar:1 exec dwm

(This will start dwm on display :1 of the host foo.bar.)

In order to display status info in the bar, you can do something
like this in your .xinitrc:

    while xsetroot -name "`date` `uptime | sed 's/.*,//'`"
    do
    	sleep 1
    done &
    exec dwm


### Configuration

The configuration of dwm is done by: -
<ol>
<li>Creating a custom config.h
and (re)compiling the source code;</li>
<li>Passing JSON configuration files as parameters at run-time;</li>
</ol>


### Usage help

```
layout-file.json supported names:
    global section:
        alt-tab-border                alt-tab switcher border width in pixels
        alt-tab-highlight             alt-tab switcher highlights clients during selection
        alt-tab-monitor-format        printf style format of monitor identifier using %s as placeholder
        alt-tab-size                  maximum size of alt-tab switcher (WxH)
        alt-tab-x                     alt-tab switcher position - 0 left, 1 centre, 2 right
        alt-tab-y                     alt-tab switcher position - 0 top, 1 middle, 2 bottom
        bar-layout                    array of bar elements in order of appearance (TagBar, LtSymbol, WinTitle, StatusText)
        bar-tag-format-empty          printf style format of tag displayed when no client is assigned, using %s as placeholder
        bar-tag-format-populated      printf style format of tag displayed when one or more clients are assigned, using %s as placeholders
        bar-tag-format-reversed       true to reverse the order of tag number and master client class
        border-width                  window border width in pixels
        client-indicators             true to show indicators blobs on the edge of each tag to represent the number of clients present
        client-indicator-size         size in pixels of client indicators
        client-indicators-top         true to show indicators at the top of the bar, false to show indicators at the bottom
        colours-layout                colour of layout indicator, in the form [<foreground>, <background>, <border>]
        colours-hidden                colour of hidden elements, in the form [<foreground>, <background>, <border>]
        colours-normal                colour of normal elements, in the form [<foreground>, <background>, <border>]
        colours-selected              colour of selected elements, in the form [<foreground>, <background>, <border>]
        colour-selected-bg2           active client title background colour 2 (right side colour of gradient)
        colours-torch                 torch colours, in the form [<foreground>, <background>, <border>]
        colours-urgent                colour of urgent elements, in the form [<foreground>, <background>, <border>]
        colours-alt-tab-hidden        colour of alt-tab switcher hidden elements, in the form [<foreground>, <background>, <border>]
        colours-alt-tab-normal        colour of alt-tab switcher elements, in the form [<foreground>, <background>, <border>]
        colours-alt-tab-selected      colour of alt-tab switcher selected elements, in the form [<foreground>, <background>, <border>]
        colours-alt-tab-urgent        colour of alt-tab switcher urgent elements, in the form [<foreground>, <background>, <border>]
        cursor-autohide               true to hide cursor when stationary or keys are pressed, for all clients
        cursor-autohide-delay         the number of seconds before a stationary cursor can be hidden, 0 to disable
        default-icon                  path to default icon file for clients without icons
        desktop-icon                  path to default icon file for desktop clients
        default-tags                  array of single character strings for the default character for each tag
        fonts                         font string or array of font strings to use
        mirror-layout                 switch master area and stack area
        monitors                      array of monitor objects (see "monitor sections")
        show-desktop                  true to enable management of desktop clients, and toggle desktop
        show-desktop-when-active      true to only allow switching to the desktop, when a desktop client exists
        show-desktop-with-floating    true to allow floating clients to be visible when showing the desktop
        system-tray                   true to enable system tray handling
        system-tray-align             align the system tray to side of the status area: 0 for left-aligned, 1 for right-aligned
        system-tray-pinning           pin system tray to specific monitor, -1 to follow the active monitor
        system-tray-spacing           number of pixels between system tray icons
        terminal-swallowing           true to enable terminal swallowing
        title-align                   active client title alignment: 0 for left-aligned, 1 for centred, 2 for right-aligned
        urgency-hinting               disable urgency hinting for clients (doesn't affect set-urgency rule functionality)
        vanity-gaps                   true for vanity gaps (default), false for no gaps between windows
        vanity-gaps-inner-h           inner horizontal gap between windows in pixels
        vanity-gaps-inner-v           inner vertical gap between windows in pixels
        vanity-gaps-outer-h           outer horizontal gap between windows and monitor edges in pixels
        vanity-gaps-outer-v           outer vertical gap between windows and monitor edges in pixels
        view-on-tag                   switch view when tagging a client
    monitor sections:
        comment                    ignored
        log-rules                  log all matching rules for this monitor
        monitor                    monitor number
        set-bar-layout             array of bar elements in order of appearance (TagBar, LtSymbol, WinTitle, StatusText)
        set-cursor-autohide        true to hide cursor when stationary on this monitor
        set-cursor-hide-on-keys    true to hide cursor when keys are pressed on this monitor
        set-default                set this monitor to be the default selected on startup
        set-enable-gaps            set to true to enable vanity gaps between clients (default)
        set-gap-inner-h            horizontal inner gap between clients
        set-gap-inner-v            vertical inner gap between clients
        set-gap-outer-h            horizontal outer gap between clients and the screen edges
        set-gap-outer-v            vertical outer gap between clients and the screen edges
        set-indicators-top         set to true to show client indicators on the top edge of the bar
        set-layout                 layout number or layout symbol
        set-mfact                  size of master client area for all tags on this monitor
        set-mirror-layout          switch master area and stack area on this monitor
        set-nmaster                number of master clients for all tags on this monitor
        set-quiet-alt-tags         don't raise the bar or show over fullscreen clients on this monitor
        set-reverse-master         set to true if the master client class should be shown before the tag indicator
        set-showbar                whether to show the bar by default on this monitor
        set-showmaster             set to true if the master client class should be shown on each tag on the bar
        set-showstatus             set to 1 if the status text should be displayed, -1 to ignore root window name changes
        set-start-tag              default tag to activate on startup
        set-switch-on-empty        switch to the specified tag when no more clients are visible under the active tag
        set-title-align            active client title alignment: 0 for left-aligned, 1 for centred, 2 for right-aligned
        set-topbar                 set to true if the bar should be at the top of the screen for this monitor
        tags                       array of tag-specific settings (see "tags sections (per monitor)")
    tags sections (per monitor):
        comment                    ignored
        index                      tag index number, usually between 1 and 9
        set-cursor-autohide        true to hide cursor when stationary on this tag
        set-cursor-hide-on-keys    true to hide cursor when keys are pressed on this tag
        set-enable-gaps            set to true to enable vanity gaps between clients
        set-layout                 layout number or layout symbol
        set-mfact                  size of master client area for this tag
        set-nmaster                number of master clients on this tag
        set-quiet-alt-tags         don't raise the bar or show over fullscreen clients on this tag
        set-showbar                whether to show the bar by default on this tag
        set-switch-on-empty        switch to the specified tag when no more clients are visible under this tag
        set-tag-char               show this text instead of the default tag text

rules-file.json supported names:
    comment                        ignored
    defer-rule                     if rule matches a client excluding its title, then wait until the title changes and reapply
    exclusive                      rule will be applied after non-exclusive rules, and other rules will not apply
    if-class-begins                substring matching from the start of class
    if-class-contains              substring matching on class
    if-class-ends                  substring matching from the end of class
    if-class-is                    exact full string matching on class
    if-desktop                     true if the client is a desktop window
    if-fixed-size                  false if the client is resizable or fullscreen, true if fixed size
    if-has-parent                  client has a parent
    if-instance-begins             substring matching from the start of instance
    if-instance-contains           substring matching on instance
    if-instance-ends               substring matching from the end of instance
    if-instance-is                 exact full string matching on instance
    if-parent-class-begins         substring matching from the start of parent's class
    if-parent-class-contains       substring matching on parent's class
    if-parent-class-ends           substring matching from the end of parent's class
    if-parent-class-is             exact full string matching on parent's class
    if-parent-instance-begins      substring matching from the start of parent's instance
    if-parent-instance-contains    substring matching on parent's instance
    if-parent-instance-ends        substring matching from the end of parent's instance
    if-parent-instance-is          exact full string matching on parent's instance
    if-parent-role-begins          substring matching from the start of parent's role
    if-parent-role-contains        substring matching on parent's role
    if-parent-role-ends            substring matching from the end of parent's role
    if-parent-role-is              exact full string matching on parent's role
    if-parent-title-begins         substring matching from the start of parent's title
    if-parent-title-contains       substring matching on parent's title
    if-parent-title-ends           substring matching from the end of parent's title
    if-parent-title-is             exact full string matching on parent's title
    if-role-begins                 substring matching from the start of role
    if-role-contains               substring matching on role
    if-role-ends                   substring matching from the end of role
    if-role-is                     exact full string matching on role
    if-title-begins                substring matching from the start of title
    if-title-contains              substring matching on title
    if-title-ends                  substring matching from the end of title
    if-title-is                    exact full string matching on title
    log-rule                       log when a client matches the rule
    set-alwaysontop                this client will appear above others (only while focused if tiled)
    set-autofocus                  whether to auto focus the client (floating clients only), defaults to true
    set-autohide                   whether to minimize/iconify the client when it should be invisible
    set-centred                    1 for centre of screen, 2 for centre of parent client
    set-cfact                      client scale factor, value between 0.25 and 4.0
    set-class-display              display this string instead of the class in tag bar
    set-class-group                use this string as class for alttab class switcher
    set-cursor-autohide            true to hide cursor when stationary while this client is focused
    set-cursor-hide-on-keys        true to hide cursor when keys are pressed while this client is focused
    set-desktop                    true to make the client a desktop window
    set-fakefullscreen             when going fullscreen this client will be constrained to its tile
    set-floating                   override the default tiling/floating behaviour for this client
    set-floating-width             floating client width at creation, integer for absolute width, fraction for relative width
    set-floating-height            floating client height at creation, integer for absolute height, fraction for relative height
    set-floating-x                 floating client initial position: fraction between 0 and 1 for relative position, OR > 1 for absolute position
    set-floating-y                 floating client initial position: fraction between 0 and 1 for relative position, OR > 1 for absolute position
    set-float-align-x              floating client fixed alignment: -1:not aligned, fraction between 0 and 1 for relative position
    set-float-align-y              floating client fixed alignment: -1:not aligned, fraction between 0 and 1 for relative position
    set-focus-origin-dx            mouse warp relative to client centre - x (float)
    set-focus-origin-dy            mouse warp relative to client centre - y (float)
    set-follow-parent              true to ensure this client's tags match its parent's, and stays on the same monitor as its parent
    set-game                       fullscreen clients will be minimized and unminimized when they lose or gain focus (on the same monitor)
    set-game-strict                fullscreen clients will be minimized and unminimized whenever they lose or gain focus
    set-greedy-focus               client won't lose focus due to mouse movement
    set-hidden                     client will be hidden by default
    set-icon                       the icon image file will be loaded and used instead of the client's icon
    set-ignored                    client will be ignored from stacking, focus, alt-tab, etc.
    set-missing-icon               the icon image file will be loaded and used for the client instead of no icon
    set-modal                      client will be marked as modal (useful when clients implements modality without involving the WM)
    set-monitor                    set monitor number (0+) for this client
    set-never-focus                prevent this application from being focused automatically
    set-never-fullscreen           prevent this application from being made fullscreen
    set-never-move                 prevent the application from moving the client
    set-never-parent               prevent the client from being treated as the parent to any other
    set-never-resize               prevent the application from resizing the client
    set-newmaster                  client always created as a new master, otherwise client goes onto the stack
    set-noswallow                  never swallow this client
    set-panel                      client is a floating panel window, whose visibility will match the bar's; excluded from mouse warp focus, stacking, alt-tab
    set-parent-begins              treat client as if its parent is the specified window (of same class if rule is deferred) - substring match from the start
    set-parent-contains            treat client as if its parent is the specified window (of same class if rule is deferred) - substring match
    set-parent-ends                treat client as if its parent is the specified window (of same class if rule is deferred) - substring match from the end
    set-parent-guess               treat client as if its parent is the client that was focused when it was mapped, or the most recently focused (use with caution)
    set-parent-is                  treat client as if its parent is the specified window (of same class if rule is deferred) - exact name match
    set-pause-on-invisible         client process will be sent SIGSTOP when not visible, and SIGCONT when visible, killed, or unmanaged
    set-sticky                     client appears on all tags
    set-tags-mask                  sets the tag(s) mask applied to the client
    set-terminal                   true to indicate this client is a terminal
    set-title                      show the specified title in place of the client's
    set-top-level                  true to indicate this client should be treated as top level (ultimate parent)
    set-urgent                     clients will be focused when created, switching tag view if necessary

usage: dwm [-h] [-v] [-r <rules-file.json>] [-l <layout-file.json>] [-u] [-n] [-s <verb> [command [args]]]
    -h    display usage and accepted configuration parameters
    -v    display version information
    -n    disable dwm system tray functionality
    -r    use the rules defined in the specified JSON rules file
    -l    use the layout configuration defined in the specified JSON layout file
    -u    disable client urgency hinting
    -s    send request to running instance via socket

IPC verbs:
    get_dwm_client [Window ID]    Return DWM client properties for the specified (or active) window
    get_layouts                   Return a list of layouts
    get_monitors                  Return monitor properties
    get_tags                      Return a list of all tags
    run_command                   Runs an IPC command
    subscribe <event> ...         Subscribe to the specified events

IPC events:
    client_focus_change_event
    focused_state_change_event
    focused_title_change_event
    layout_change_event
    monitor_focus_change_event
    tag_change_event

IPC commands:
    activate
    clearurgency
    enabletermswallow
    enableurgency
    focusmon
    focusstack
    incnmaster
    killclient
    setmfact
    tag
    tagmon
    togglefloating
    toggletag
    toggleview
    view
    quit
    zoom
```
