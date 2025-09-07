#define MODKEY Mod1Mask

static char *font_name = "Berkeley Mono:style=Bold:pixelsize=16:antialias=true:autohint=true";
static int border_size = 3;
static char *active_border_color = "orange";
static char *inactive_border_color = "gray";

static Keybinds keybinds[] = {
	/* Mask                 KeySym      Function           Argument     */
	{ MODKEY,               XK_Left,    move_window_x,     { .i = -50 } },
	{ MODKEY,               XK_Right,   move_window_x,     { .i = +50 } },
	{ MODKEY,               XK_Up,      move_window_y,     { .i = -50 } },
	{ MODKEY,               XK_Down,    move_window_y,     { .i = +50 } },
	{ MODKEY | ShiftMask,   XK_Left,    resize_window_x,   { .i = -50 } },
	{ MODKEY | ShiftMask,   XK_Right,   resize_window_x,   { .i = +50 } },
	{ MODKEY | ShiftMask,   XK_Up,      resize_window_y,   { .i = -50 } },
	{ MODKEY | ShiftMask,   XK_Down,    resize_window_y,   { .i = +50 } },
};
