#define MODKEY Mod1Mask

static char *font_name = "Berkeley Mono:style=Bold:pixelsize=16:antialias=true:autohint=true";
static int border_size = 3;
static char *active_border_color = "khaki";
static char *inactive_border_color = "darkgray";

// List of X11 keyboard symbol names.
// https://cgit.freedesktop.org/xorg/proto/x11proto/tree/keysymdef.h
// https://cgit.freedesktop.org/xorg/proto/x11proto/tree/XF86keysym.h

static Shortcut shortcuts[] = {
	/* Mask    KeySym                    Shell command                                         */
	{ MODKEY,  XK_Return,                "st -f \"Berkeley Mono:style=Bold:size=14\" -g 60x40" },
	{ 0,       XF86XK_AudioLowerVolume,  "pactl set-sink-volume @DEFAULT_SINK@ -5%"            },
	{ 0,       XF86XK_AudioRaiseVolume,  "pactl set-sink-volume @DEFAULT_SINK@ +5%"            },
	{ 0,       XF86XK_AudioMute,         "pactl set-sink-mute @DEFAULT_SINK@ toggle"           },
};

static Keybinds keybinds[] = {
	/* Mask                 KeySym      Function             Argument     */
	{ MODKEY,               XK_Left,    move_window_x,       { .i = -50 } },
	{ MODKEY,               XK_Right,   move_window_x,       { .i = +50 } },
	{ MODKEY,               XK_Up,      move_window_y,       { .i = -50 } },
	{ MODKEY,               XK_Down,    move_window_y,       { .i = +50 } },
	{ MODKEY | ShiftMask,   XK_Left,    resize_window_x,     { .i = -50 } },
	{ MODKEY | ShiftMask,   XK_Right,   resize_window_x,     { .i = +50 } },
	{ MODKEY | ShiftMask,   XK_Up,      resize_window_y,     { .i = -50 } },
	{ MODKEY | ShiftMask,   XK_Down,    resize_window_y,     { .i = +50 } },
	{ MODKEY,               XK_1,       switch_to_desktop,   { .i = 1 }   },
	{ MODKEY,               XK_2,       switch_to_desktop,   { .i = 2 }   },
	{ MODKEY,               XK_3,       switch_to_desktop,   { .i = 3 }   },
	{ MODKEY,               XK_4,       switch_to_desktop,   { .i = 4 }   },
	{ MODKEY,               XK_5,       switch_to_desktop,   { .i = 5 }   },
	{ MODKEY,               XK_6,       switch_to_desktop,   { .i = 6 }   },
	{ MODKEY,               XK_7,       switch_to_desktop,   { .i = 7 }   },
	{ MODKEY,               XK_8,       switch_to_desktop,   { .i = 8 }   },
	{ MODKEY,               XK_9,       switch_to_desktop,   { .i = 9 }   },
	{ MODKEY | ControlMask, XK_1,       move_to_desktop,     { .i = 1 }   },
	{ MODKEY | ControlMask, XK_2,       move_to_desktop,     { .i = 2 }   },
	{ MODKEY | ControlMask, XK_3,       move_to_desktop,     { .i = 3 }   },
	{ MODKEY | ControlMask, XK_4,       move_to_desktop,     { .i = 4 }   },
	{ MODKEY | ControlMask, XK_5,       move_to_desktop,     { .i = 5 }   },
	{ MODKEY | ControlMask, XK_6,       move_to_desktop,     { .i = 6 }   },
	{ MODKEY | ControlMask, XK_7,       move_to_desktop,     { .i = 7 }   },
	{ MODKEY | ControlMask, XK_8,       move_to_desktop,     { .i = 8 }   },
	{ MODKEY | ControlMask, XK_9,       move_to_desktop,     { .i = 9 }   },
};
