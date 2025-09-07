#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h>
#include <X11/cursorfont.h>
#include <X11/Xft/Xft.h>

#include "plusminus.h"

#define MAX(a, b) ((a) > (b) ? (a) : (b))

#define MOD Mod1Mask
#define BORDER_SIZE 4
#define ACTIVE_BORDER_COLOR "red"
#define INACTIVE_BORDER_COLOR "gray"
#define NUM_DESKTOPS 9
#define FONT_NAME "Berkeley Mono:style=Bold:size=16"

static Display *dpy;
static Window root;
static Window active_window = None;
static int screen;

static unsigned long active_border_color;
static unsigned long inactive_border_color;
static unsigned long current_desktop = 1;

static Cursor cursor_default;
static Cursor cursor_move;
static Cursor cursor_resize;

static Atom _NET_WM_DESKTOP;
static Atom _NET_CURRENT_DESKTOP;
static Atom _NET_NUMBER_OF_DESKTOPS;
static Atom _NET_CLIENT_LIST;

static int ignore_x_error(Display *dpy, XErrorEvent *err) {
	(void)dpy;
	(void)err;
	return 0;
}

void force_display_redraw(void) {
	XClearArea(dpy, root, 0, 0, 1, 1, True);
	XFlush(dpy);
}

int window_exists(Window w) {
	if (w == None) return 0;
	XErrorHandler old = XSetErrorHandler(ignore_x_error);
	XWindowAttributes attr;
	Status s = XGetWindowAttributes(dpy, w, &attr);
	XSync(dpy, False);
	XSetErrorHandler(old);
	return s != 0;
}

void update_borders(Window new_active) {
	if (active_window != None && active_window != new_active) {
		if (window_exists(active_window)) {
			XSetWindowBorder(dpy, active_window, inactive_border_color);
		}
	}

	if (new_active != None) {
		if (window_exists(new_active)) {
			XSetWindowBorder(dpy, new_active, active_border_color);
		} else {
			new_active = None;
		}
	}

	active_window = new_active;
}

void add_to_client_list(Window window) {
	Atom type;
	int format;
	unsigned long nitems, bytes_after;
	unsigned char *data = NULL;

	if (XGetWindowProperty(dpy, root, _NET_CLIENT_LIST, 0, 1024, False, XA_WINDOW, &type, &format, &nitems, &bytes_after, &data) == Success) {
		Window *windows;
		if (data) {
			windows = realloc(data, (nitems + 1) * sizeof(Window));
		} else {
			windows = malloc(sizeof(Window));
			nitems = 0;
		}
		windows[nitems] = window;
		XChangeProperty(dpy, root, _NET_CLIENT_LIST, XA_WINDOW, 32, PropModeReplace, (unsigned char *)windows, nitems + 1);
		free(windows);
	}
}

void remove_from_client_list(Window window) {
	Atom type;
	int format;
	unsigned long nitems, bytes_after;
	unsigned char *data = NULL;

	if (XGetWindowProperty(dpy, root, _NET_CLIENT_LIST, 0, 1024, False, XA_WINDOW, &type, &format, &nitems, &bytes_after, &data) == Success) {
		if (type == XA_WINDOW) {
			Window *windows = (Window *)data;
			int found = 0;

			for (unsigned long i = 0; i < nitems; i++) {
				if (windows[i] == window) {
					found = 1;
					for (unsigned long j = i; j < nitems - 1; j++) {
						windows[j] = windows[j + 1];
					}
					break;
				}
			}

			if (found) {
				XChangeProperty(dpy, root, _NET_CLIENT_LIST, XA_WINDOW, 32, PropModeReplace, (unsigned char *)windows, nitems - 1);
			}
		}
		if (data) XFree(data);
	}
}

void set_window_desktop(Window window, unsigned long desktop) {
	Atom type;
	int format;
	unsigned long nitems, bytes_after;
	unsigned char *data = NULL;

	if (XGetWindowProperty(dpy, window, _NET_WM_DESKTOP, 0, 1, False, XA_CARDINAL, &type, &format, &nitems, &bytes_after, &data) == Success) {
		if (data) XFree(data);
	}

	unsigned long value = desktop;
	XChangeProperty(dpy, window, _NET_WM_DESKTOP, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&value, 1);

	log_message(stdout, LOG_DEBUG, "Window 0x%lx assigned desktop %lu", window, desktop);
}

unsigned long get_window_desktop(Window w) {
	if (w == None || !window_exists(w)) {
		// Default to desktop 0 for invalid windows.
		return 0; 
	}

	Atom type;
	int format;
	unsigned long nitems, bytes_after;
	unsigned char *data = NULL;
	unsigned long desktop = 0;

	if (XGetWindowProperty(dpy, w, _NET_WM_DESKTOP, 0, 1, False, XA_CARDINAL, &type, &format, &nitems, &bytes_after, &data) == Success) {
		if (data && nitems > 0 && type == XA_CARDINAL && format == 32) {
			desktop = *((unsigned long *)data);
		}
		if (data) XFree(data);
	}

	return desktop;
}

void switch_desktop(unsigned long desktop) {
	if (desktop < 1 || desktop > NUM_DESKTOPS) return;

	current_desktop = desktop;

	// Update the root window property.
	unsigned long value = desktop;
	XChangeProperty(dpy, root, _NET_CURRENT_DESKTOP, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&value, 1);

	Atom type;
	int format;
	unsigned long nitems, bytes_after;
	unsigned char *data = NULL;

	// Get list of client windows.
	if (XGetWindowProperty(dpy, root, _NET_CLIENT_LIST, 0, 1024, False, XA_WINDOW, &type, &format, &nitems, &bytes_after, &data) == Success) {
		if (type == XA_WINDOW && format == 32) {
			Window *windows = (Window *)data;

			for (unsigned long i = 0; i < nitems; i++) {
				Window w = windows[i];

				if (!window_exists(w)) {
					log_message(stdout, LOG_DEBUG, "Window 0x%lx no longer exists, skipping", w);
					continue;
				}

				unsigned long window_desktop = get_window_desktop(w);
				log_message(stdout, LOG_DEBUG, "Processing window 0x%lx on desktop %lu (current: %lu)", w, window_desktop, current_desktop);

				if (window_desktop == current_desktop) {
					log_message(stdout, LOG_DEBUG, "Mapping window 0x%lx", w);
					XMapWindow(dpy, w);
				} else {
					log_message(stdout, LOG_DEBUG, "Unmapping window 0x%lx", w);
					XUnmapWindow(dpy, w);
				}
			}

			XFlush(dpy);
		}
		if (data) XFree(data);
	}

	if (active_window != None) {
		if (window_exists(active_window)) {
			XSetWindowBorder(dpy, active_window, inactive_border_color);
		}
		active_window = None;
	}

	log_message(stdout, LOG_DEBUG, "Switched to desktop %lu", desktop);
	force_display_redraw();
}

void draw_desktop_text(void) {
	Visual *visual = DefaultVisual(dpy, screen);
	Colormap colormap = DefaultColormap(dpy, screen);

	XftDraw *xft_draw = XftDrawCreate(dpy, root, visual, colormap);

	XftFont *xft_font = XftFontOpenName(dpy, screen, FONT_NAME);
	if (!xft_font) {
		xft_font = XftFontOpenName(dpy, screen, "monospace-12");
	}

	XftColor xft_color;
	XRenderColor render_color = {0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF};
	XftColorAllocValue(dpy, visual, colormap, &render_color, &xft_color);

	char text[50];
	snprintf(text, sizeof(text), "Desktop: %lu/%d", current_desktop, NUM_DESKTOPS);

	XGlyphInfo extents;
	XftTextExtentsUtf8(dpy, xft_font, (FcChar8 *)text, strlen(text), &extents);

	XClearArea(dpy, root, 10, 10, extents.width + 20, extents.height + 10, False);

	XftDrawStringUtf8(xft_draw, &xft_color, xft_font, 15, 15 + xft_font->ascent, (FcChar8 *)text, strlen(text));

	XftColorFree(dpy, visual, colormap, &xft_color);
	if (xft_font) XftFontClose(dpy, xft_font);
	XftDrawDestroy(xft_draw);
	XFlush(dpy);
}

void set_root_window_cursor(void) {
	Cursor cursor = XCreateFontCursor(dpy, XC_left_ptr);
	XDefineCursor(dpy, root, cursor);
	XFreeCursor(dpy, cursor);
	log_message(stdout, LOG_DEBUG, "Set root window cursor");
}

int main(void) {
	set_log_level(get_log_level_from_env());

	XWindowAttributes attr;
	XButtonEvent start;
	XEvent ev;

	dpy = XOpenDisplay(NULL);
	if (!dpy) {
		fprintf(stderr, "cannot open display\n");
		return 1;
	}

	screen = DefaultScreen(dpy);
	root = RootWindow(dpy, screen);

	// Create cursors
	cursor_default = XCreateFontCursor(dpy, XC_left_ptr);
	cursor_move = XCreateFontCursor(dpy, XC_fleur);
	cursor_resize  = XCreateFontCursor(dpy, XC_sizing);

	// Set default cursor for root
	XDefineCursor(dpy, root, cursor_default);

	// Initialize EWMH atoms for multiple desktop support.
	_NET_WM_DESKTOP = XInternAtom(dpy, "_NET_WM_DESKTOP", False);
	_NET_CURRENT_DESKTOP = XInternAtom(dpy, "_NET_CURRENT_DESKTOP", False);
	_NET_NUMBER_OF_DESKTOPS = XInternAtom(dpy, "_NET_NUMBER_OF_DESKTOPS", False);
	_NET_CLIENT_LIST = XInternAtom(dpy, "_NET_CLIENT_LIST", False);

	// Set number of desktops.
	unsigned long num_desktops = NUM_DESKTOPS;
	XChangeProperty(dpy, root, _NET_NUMBER_OF_DESKTOPS, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&num_desktops, 1);

	// Set current desktop.
	unsigned long current_desktop_val = 1;  // Start with desktop 1 (first workspace)
	XChangeProperty(dpy, root, _NET_CURRENT_DESKTOP, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&current_desktop_val, 1);

	// Grab keys for desktop switching.
	for (int i = 0; i < NUM_DESKTOPS; i++) {
		KeyCode keycode = XKeysymToKeycode(dpy, XK_1 + i);
		XGrabKey(dpy, keycode, MOD, root, True, GrabModeAsync, GrabModeAsync);
		log_message(stdout, LOG_DEBUG, "Grabbing Mod+%d for desktop %d (keycode: %d)", i + 1, i + 1, keycode);
	}

	// Grab keys for window dragging.
	XGrabButton(dpy, 1, MOD, root, True, ButtonPressMask|ButtonReleaseMask|PointerMotionMask, GrabModeAsync, GrabModeAsync, None, None);
	XGrabButton(dpy, 3, MOD, root, True, ButtonPressMask|ButtonReleaseMask|PointerMotionMask, GrabModeAsync, GrabModeAsync, None, None);

	// Prepare border colors.
	Colormap cmap = DefaultColormap(dpy, screen);
	XColor active_color, inactive_color, dummy;
	active_border_color = BlackPixel(dpy, screen);
	inactive_border_color = BlackPixel(dpy, screen);

	if (XAllocNamedColor(dpy, cmap, ACTIVE_BORDER_COLOR, &active_color, &dummy)) {
		active_border_color = active_color.pixel;
	}

	if (XAllocNamedColor(dpy, cmap, INACTIVE_BORDER_COLOR, &inactive_color, &dummy)) {
		inactive_border_color = inactive_color.pixel;
	}

	// Root window input selection masks.
	XSelectInput(dpy, root,
			SubstructureRedirectMask | SubstructureNotifyMask |
			FocusChangeMask | EnterWindowMask | LeaveWindowMask |
			ButtonPressMask | ExposureMask);

	start.subwindow = None;
	draw_desktop_text();

	for(;;) {
		XNextEvent(dpy, &ev);

		switch (ev.type) {
			case Expose:
				if (ev.xexpose.window == root) {
					draw_desktop_text();
				}
				break;

			case MapRequest:
				{
					Window window = ev.xmaprequest.window;
					XSetWindowBorderWidth(dpy, window, BORDER_SIZE);
					XSetWindowBorder(dpy, window, inactive_border_color);


					XWindowAttributes check_attr;
					if (XGetWindowAttributes(dpy, window, &check_attr)) {
						XSelectInput(dpy, window, EnterWindowMask | LeaveWindowMask);
					}

					XMapWindow(dpy, window);
					log_message(stdout, LOG_DEBUG, "Window 0x%lx mapped", window);

					add_to_client_list(window);
					set_window_desktop(window, current_desktop);
				} break;

			case DestroyNotify:
				{
					if (ev.xdestroywindow.window == active_window) {
						update_borders(None);
						log_message(stdout, LOG_DEBUG, "Window 0x%lx destroyed", ev.xdestroywindow.window);
					}

					remove_from_client_list(ev.xdestroywindow.window);
				} break;

			case UnmapNotify:
				{
					log_message(stdout, LOG_DEBUG, "Window 0x%lx unmapped", ev.xunmap.window);
				} break;


			case FocusIn:
				{
					if (ev.xfocus.window != root) {
						update_borders(ev.xfocus.window);
					}
				}
				break;

			case FocusOut:
				{
					if (ev.xfocus.window == active_window) {
						update_borders(None);
					}
				} break;

			case EnterNotify:
				{
					Window entered_window = ev.xcrossing.window;
					if (entered_window != root && ev.xcrossing.mode == NotifyNormal) {
						if (entered_window != None && entered_window != active_window) {
							XRaiseWindow(dpy, entered_window);
							XSetInputFocus(dpy, entered_window, RevertToPointerRoot, CurrentTime);
							update_borders(entered_window);
						}

					}
				} break;

			case KeyPress:
				{
					KeySym keysym = XkbKeycodeToKeysym(dpy, ev.xkey.keycode, 0, 0);
					if (keysym >= XK_1 && keysym <= XK_9) {
						unsigned long desktop = keysym - XK_1 + 1;
						if (desktop != current_desktop) {
							log_message(stdout, LOG_DEBUG, "Switching to desktop %lu", desktop);
							switch_desktop(desktop);
						}
					}
				} break;

			case ButtonPress:
				{
					if (ev.xbutton.subwindow != None) {
						XGetWindowAttributes(dpy, ev.xbutton.subwindow, &attr);
						start = ev.xbutton;

						XRaiseWindow(dpy, start.subwindow);
						XSetInputFocus(dpy, ev.xbutton.subwindow, RevertToPointerRoot, CurrentTime);
						update_borders(ev.xbutton.subwindow);

						if (start.button == 1) {
							log_message(stdout, LOG_DEBUG, "Setting cursor to move");
							XDefineCursor(dpy, start.subwindow, cursor_move);
						} else if (start.button == 3) {
							log_message(stdout, LOG_DEBUG, "Setting cursor to resize");
							XDefineCursor(dpy, start.subwindow, cursor_resize);
						}
						XFlush(dpy);
					}
				} break;

			case ButtonRelease:
				{
					if (start.subwindow != None) {
						// Restore default cursor on the client window
						XDefineCursor(dpy, start.subwindow, None);
						XFlush(dpy);
					}
					start.subwindow = None;
				} break;

			case MotionNotify:
				{
					if (start.subwindow != None) {
						int xdiff = ev.xbutton.x_root - start.x_root;
						int ydiff = ev.xbutton.y_root - start.y_root;

						XMoveResizeWindow(dpy, start.subwindow,
								attr.x + (start.button == 1 ? xdiff : 0),
								attr.y + (start.button == 1 ? ydiff : 0),
								MAX(1, attr.width  + (start.button == 3 ? xdiff : 0)),
								MAX(1, attr.height + (start.button == 3 ? ydiff : 0)));
					}
				} break;

			default:
				break;
		}
	}

	XFreeCursor(dpy, cursor_default);
	XFreeCursor(dpy, cursor_move);
	XFreeCursor(dpy, cursor_resize);
	return 0;
}
