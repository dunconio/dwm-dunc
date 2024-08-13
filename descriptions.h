
#define DESCRIPTION_ACTIVATE_MIXER					"Activate Volume Control mixer"

#if PATCH_ALTTAB
#define DESCRIPTION_ALTTAB_NORMAL					"Alt-tab switcher for visible tags on the focused monitor"
#define DESCRIPTION_ALTTAB_REVERSE					"Alt-tab switcher for visible tags on the focused monitor (reverse order)"
#define DESCRIPTION_ALTTAB_TAGS						"Alt-tab switcher for all tags on the focused monitor"
#define DESCRIPTION_ALTTAB_TAGS_REVERSE				"Alt-tab switcher for all tags on the focused monitor (reverse order)"
#define DESCRIPTION_ALTTAB_TAGS_MONS				"Alt-tab switcher for visible tags on all monitors"
#define DESCRIPTION_ALTTAB_TAGS_MONS_REVERSE		"Alt-tab switcher for visible tags on all monitors (reverse order)"
#define DESCRIPTION_ALTTAB_CLASS					"Alt-tab class switcher for visible clients on the focused monitor"
#define DESCRIPTION_ALTTAB_CLASS_REVERSE			"Alt-tab class switcher for visible clients on the focused monitor (reverse order)"
#define DESCRIPTION_ALTTAB_CLASS_TAGS				"Alt-tab class switcher for all clients on the focused monitor"
#define DESCRIPTION_ALTTAB_CLASS_TAGS_REVERSE		"Alt-tab class switcher for all clients on the focused monitor (reverse order)"
#define DESCRIPTION_ALTTAB_CLASS_TAGS_MONS			"Alt-tab class switcher for all clients on all monitors"
#define DESCRIPTION_ALTTAB_CLASS_TAGS_MONS_REVERSE	"Alt-tab class switcher for all clients on all monitors (reverse order)"
#define DESCRIPTION_ALTTAB_TAGS_1					"Alt-tab client switcher for all clients on monitor 1 (without changing monitor focus)"
#endif // PATCH_ALTTAB

#if PATCH_CFACTS
#define DESCRIPTION_CFACT_DECREASE					"Decrease the tiled client's size weighting factor"
#define DESCRIPTION_CFACT_INCREASE					"Increase the tiled client's size weighting factor"
#define DESCRIPTION_CFACT_RESET						"Reset the tiled client's size weighting factor"
#endif // PATCH_CFACTS

#define DESCRIPTION_CLEAR_URGENCY					"Clear all urgency hints"

#define DESCRIPTION_FOCUSMON_BACKWARD				"Focus the previous monitor"
#define DESCRIPTION_FOCUSMON_FORWARD				"Focus the next monitor"
#define DESCRIPTION_FOCUSSTACK_BACKWARD				"Focus the previous client"
#define DESCRIPTION_FOCUSSTACK_FORWARD				"Focus the next client"
#if PATCH_VANITY_GAPS
#define DESCRIPTION_GAPS_TOGGLE						"Toggle vanity gaps"
#define DESCRIPTION_GAPS_DEFAULT					"Reset vanity gaps"
#define DESCRIPTION_GAPS_ALL_INCREASE				"Increase all gaps"
#define DESCRIPTION_GAPS_ALL_DECREASE				"Decrease all gaps"
#define DESCRIPTION_GAPS_ALL_INNER_INCREASE			"Increase all inner gaps"
#define DESCRIPTION_GAPS_ALL_INNER_DECREASE			"Decrease all inner gaps"
#define DESCRIPTION_GAPS_ALL_OUTER_INCREASE			"Increase all outer gaps"
#define DESCRIPTION_GAPS_ALL_OUTER_DECREASE			"Decrease all outer gaps"
#define DESCRIPTION_GAPS_HORIZ_INNER_INCREASE		"Increase horizontal inner gaps"
#define DESCRIPTION_GAPS_HORIZ_INNER_DECREASE		"Decrease horizontal inner gaps"
#define DESCRIPTION_GAPS_VERT_INNER_INCREASE		"Increase vertical inner gaps"
#define DESCRIPTION_GAPS_VERT_INNER_DECREASE		"Decrease vertical inner gaps"
#define DESCRIPTION_GAPS_HORIZ_OUTER_INCREASE		"Increase horizontal outer gaps"
#define DESCRIPTION_GAPS_HORIZ_OUTER_DECREASE		"Decrease horizontal outer gaps"
#define DESCRIPTION_GAPS_VERT_OUTER_INCREASE		"Increase vertical outer gaps"
#define DESCRIPTION_GAPS_VERT_OUTER_DECREASE		"Decrease vertical outer gaps"
#endif // PATCH_VANITY_GAPS

#define DESCRIPTION_HELP							"Show keyboard shortcuts"

#if PATCH_FLAG_HIDDEN
#define DESCRIPTION_HIDE_CLIENT						"Hide (iconify) the focused client"
#define DESCRIPTION_HIDE_CLIENT_OTHERS				"Hide (iconify) all visible clients (on the focused monitor) except the focused client"
#define DESCRIPTION_UNHIDE_CLIENTS					"Unhide all visible clients on the focused monitor"
#endif // PATCH_FLAG_HIDDEN


#define DESCRIPTION_KILL_CLIENT						"Kill the client's window"
#define DESCRIPTION_KILL_GROUP						"Kill clients that match the selected client's class/instance"

#if PATCH_LOG_DIAGNOSTICS
#define DESCRIPTION_LOG_DIAGNOSTICS					"Output diagnostic info to the log for this monitor"
#define DESCRIPTION_LOG_DIAGNOSTICS_ALL				"Output diagnostic info to the log for all monitors"
#endif // PATCH_LOG_DIAGNOSTICS

#define DESCRIPTION_LOG_RULES						"Output all client rules"
#define DESCRIPTION_LOG_RULES_FLAT					"Output all client rules, 1 per line"

#define DESCRIPTION_LOG_RESTART						"Clear the log file, and re-open it"
#define DESCRIPTION_LOG_SHOW						"Open the log file"
#define DESCRIPTION_MFACT_DECREASE_1PC				"Decrease master client area proportion by 1%"
#define DESCRIPTION_MFACT_DECREASE_5PC				"Decrease master client area proportion by 5%"
#define DESCRIPTION_MFACT_INCREASE_1PC				"Increase master client area proportion by 1%"
#define DESCRIPTION_MFACT_INCREASE_5PC				"Increase master client area proportion by 5%"
#define DESCRIPTION_MFACT_RESET						"Reset master client area proportion"
#define DESCRIPTION_MUTE_GUI						"Mute an audio device or application"
#define DESCRIPTION_NMASTER_DECREASE				"Increase number of master clients"
#define DESCRIPTION_NMASTER_INCREASE				"Decrease number of master clients"

#define DESCRIPTION_RELOAD							"Reload configuration and rescan windows"
#define DESCRIPTION_RESCAN							"Rescan windows"
#define DESCRIPTION_LOGOUT							"Quit dwm and log out of the session"
#define DESCRIPTION_RESTART							"Quit dwm and restart the computer"
#define DESCRIPTION_QUIT							"Quit dwm and restart"
#define DESCRIPTION_SHUTDOWN						"Quit dwm and shut down the computer"

#if PATCH_MOUSE_POINTER_WARPING
#define DESCRIPTION_REFOCUS_POINTER					"Move the mouse pointer to the client's focus offset position"
#endif // PATCH_MOUSE_POINTER_WARPING
#if PATCH_EXTERNAL_WINDOW_ACTIVATION
#define DESCRIPTION_WINDOW_SWITCHER					"Select a window to activate"
#endif // PATCH_EXTERNAL_WINDOW_ACTIVATION

#define DESCRIPTION_RUN_APPLICATION					"Start an application"
#define DESCRIPTION_RUN_BRAVE						"Start a new Brave browsing session"
#define DESCRIPTION_RUN_BRAVE_PRIVATE				"Start a new Brave private browsing session"
#define DESCRIPTION_RUN_COMMAND						"Run a command"
#define DESCRIPTION_RUN_FIREFOX						"Start a new Firefox browsing session"
#define DESCRIPTION_RUN_FIREFOX_PRIVATE				"Start a new Firefox private browsing session"
#define DESCRIPTION_RUN_MIXER						"Start Volume Control mixer"
#define DESCRIPTION_RUN_TERMINAL					"Start Konsole terminal"
#define DESCRIPTION_RUN_THORIUM						"Start a new Thorium browsing session"
#define DESCRIPTION_RUN_THORIUM_PRIVATE				"Start a new Thorium private browsing session"
#define DESCRIPTION_RUN_THUNAR						"Start Thunar file manager"

#define DESCRIPTION_SCREENSHOT_GUI					"Start screenshot application"
#define DESCRIPTION_SCREENSHOT_0					"Take a screenshot of monitor 0"
#define DESCRIPTION_SCREENSHOT_0_OPEN				"Take a screenshot of monitor 0 and open it"

#define DESCRIPTION_SET_AUDIO_OUTPUT_1				"Change default audio device to USB Headphones"
#define DESCRIPTION_SET_AUDIO_OUTPUT_2				"Change default audio device to Speakers"

#define DESCRIPTION_SETLAYOUT_BACKWARD				"Cycle to previous layout type"
#if PATCH_LAYOUT_BSTACK
#define DESCRIPTION_SETLAYOUT_BSTACK				"Switch layout to bottom stack"
#endif // PATCH_LAYOUT_BSTACK
#if PATCH_LAYOUT_BSTACKHORIZ
#define DESCRIPTION_SETLAYOUT_BSTACKHORIZ			"Switch layout to bottom stack horizontal"
#endif // PATCH_LAYOUT_BSTACKHORIZ
#define DESCRIPTION_SETLAYOUT_FORWARD				"Cycle to next layout type"
#define DESCRIPTION_SETLAYOUT_MONOCLE				"Switch layout to monocle"
#define DESCRIPTION_SETLAYOUT_TILED					"Switch layout to tiled"

#define DESCRIPTION_SHOW_CLIENT_INFO				"Show basic client window information"

#if PATCH_SHOW_DESKTOP
#define DESCRIPTION_SHOW_DESKTOP					"Toggle showing the desktop"
#endif // PATCH_SHOW_DESKTOP

#define DESCRIPTION_SWAP_LAYOUT						"Switch to the alternate layout"
#define DESCRIPTION_SWAP_VIEW_1						"Switch to the alternate tag on monitor 1"
#define DESCRIPTION_SWAP_VIEW						"Switch to the alternate tag"

#define DESCRIPTION_TAGMON_BACKWARD					"Move the client to the previous monitor"
#define DESCRIPTION_TAGMON_FORWARD					"Move the client to the next monitor"

#define DESCRIPTION_TOGGLE_30S_TONE					"Toggle 30s tone script"

#if PATCH_ALT_TAGS
#define DESCRIPTION_TOGGLE_ALT_TAGS					"Hold to show the alternate tags (and show the bar if not visible)"
#endif // PATCH_ALT_TAGS

#if PATCH_FLAG_ALWAYSONTOP
#define DESCRIPTION_TOGGLE_ALWAYSONTOP				"Toggle the client's always-on-top flag"
#endif // PATCH_FLAG_ALWAYSONTOP

#define DESCRIPTION_TOGGLE_BAR						"Toggle visibility of the bar"
#if PATCH_CONSTRAIN_MOUSE
#define	DESCRIPTION_TOGGLE_CONSTRAIN				"Toggle constraining the mouse pointer within the focused monitor"
#endif // PATCH_CONSTRAIN_MOUSE
#if DEBUGGING
#define DESCRIPTION_TOGGLE_DEBUGGING				"Toggle execution of additional debugging code blocks"
#define DESCRIPTION_TOGGLE_SKIPRULES				"Toggle rule processing"
#endif // DEBUGGING
#if PATCH_FLAG_FAKEFULLSCREEN
#define DESCRIPTION_TOGGLE_FAKEFULLSCREEN			"Toggle fake (tiled) fullscreen"
#endif // PATCH_FLAG_FAKEFULLSCREEN
#define DESCRIPTION_TOGGLE_FLOATING					"Toggle client between tiled and floating"
#define DESCRIPTION_TOGGLE_FULLSCREEN				"Toggle client fullscreen"

#if PATCH_PAUSE_PROCESS
#define DESCRIPTION_TOGGLE_PAUSE					"Toggle the client's process stop/continue"
#endif // PATCH_PAUSE_PROCESS
#if PATCH_FLAG_STICKY
#define DESCRIPTION_TOGGLE_STICKY					"Toggle the client's sticky flag"
#endif // PATCH_FLAG_STICKY

#if PATCH_FLAG_GAME
#define DESCRIPTION_TOGGLE_GAME						"Toggle client's game flag"
#endif // PATCH_FLAG_GAME

#if PATCH_MIRROR_LAYOUT
#define DESCRIPTION_TOGGLE_MIRROR					"Toggle switching master and client areas"
#endif // PATCH_MIRROR_LAYOUT

#define DESCRIPTION_TOGGLE_NOISE					"Toggle pink noise script"

#if PATCH_TORCH
#define DESCRIPTION_TOGGLE_TORCH_DARK				"Toggle screen as a torch - dark"
#define DESCRIPTION_TOGGLE_TORCH_LIGHT				"Toggle screen as a torch - light"
#endif // PATCH_TORCH


#if PATCH_MOVE_FLOATING_WINDOWS
#define DESCRIPTION_MOVE_FLOAT_LEFT					"Move floating client left"
#define DESCRIPTION_MOVE_FLOAT_LEFT_BIG				"Move floating client left (large step)"
#define DESCRIPTION_MOVE_FLOAT_RIGHT				"Move floating client right"
#define DESCRIPTION_MOVE_FLOAT_RIGHT_BIG			"Move floating client right (large step)"
#define DESCRIPTION_MOVE_FLOAT_UP					"Move floating client up"
#define DESCRIPTION_MOVE_FLOAT_UP_BIG				"Move floating client up (large step)"
#define DESCRIPTION_MOVE_FLOAT_DOWN					"Move floating client down"
#define DESCRIPTION_MOVE_FLOAT_DOWN_BIG				"Move floating client down (large step)"
#define DESCRIPTION_MOVE_FLOAT_UPLEFT				"Move floating client up and left"
#define DESCRIPTION_MOVE_FLOAT_UPLEFT_BIG			"Move floating client up and left (large step)"
#define DESCRIPTION_MOVE_FLOAT_DOWNLEFT				"Move floating client down and left"
#define DESCRIPTION_MOVE_FLOAT_DOWNLEFT_BIG			"Move floating client down and left (large step)"
#define DESCRIPTION_MOVE_FLOAT_UPRIGHT				"Move floating client up and right"
#define DESCRIPTION_MOVE_FLOAT_UPRIGHT_BIG			"Move floating client up and right (large step)"
#define DESCRIPTION_MOVE_FLOAT_DOWNRIGHT			"Move floating client down and right"
#define DESCRIPTION_MOVE_FLOAT_DOWNRIGHT_BIG		"Move floating client down and right (large step)"
#endif // PATCH_MOVE_FLOATING_WINDOWS

#if PATCH_MOVE_TILED_WINDOWS
#define DESCRIPTION_MOVE_TILED_UP					"Move the focused tiled client up the client order"
#define DESCRIPTION_MOVE_TILED_DOWN					"Move the focused tiled client down the client order"
#endif // PATCH_MOVE_TILED_WINDOWS

#if PATCH_CLIENT_OPACITY
#define DESCRIPTION_OPACITY_ACTIVE_INCREASE			"Increase client opacity"
#define DESCRIPTION_OPACITY_ACTIVE_DECREASE			"Decrease client opacity"
#define DESCRIPTION_OPACITY_INACTIVE_INCREASE		"Increase client's unfocused opacity"
#define DESCRIPTION_OPACITY_INACTIVE_DECREASE		"Decrease client's unfocused opacity"
#endif // PATCH_CLIENT_OPACITY

#define DESCRIPTION_TAG_ALL_TAGS					"Apply all tags to the focused client"

#define DESCRIPTION_VIEWACTIVE_NEXT					"Switch to the next non-vacant tag"
#define DESCRIPTION_VIEWACTIVE_PREV					"Switch to the previous non-vacant tag"
#define DESCRIPTION_VIEWACTIVE_NEXT_1				"Switch to the next non-vacant tag on monitor 1"
#define DESCRIPTION_VIEWACTIVE_PREV_1				"Switch to the previous non-vacant tag on monitor 1"

#define DESCRIPTION_VIEW_ALL_TAGS					"View all tags"

#define DESCRIPTION_VOLUME_DOWN						"System volume down"
#define DESCRIPTION_VOLUME_MUTE						"Mute system volume"
#define DESCRIPTION_VOLUME_UP						"System volume up"

#define DESCRIPTION_XKILL							"Kill a GUI program via xkill"

#define DESCRIPTION_ZOOM							"Swap the focused client with the first master client"
