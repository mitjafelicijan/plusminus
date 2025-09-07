#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h>
#include <X11/cursorfont.h>
#include <X11/Xft/Xft.h>

#include "plusminus.h"
#include "config.h"

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define LENGTH(x) (sizeof(x) / sizeof((x)[0]))

static Display *dpy;
static Window root;
static Window active_window = None;
static XWindowAttributes attr;
static XButtonEvent start;
static XEvent ev;
static int screen;

static unsigned long number_of_desktops = 9;
static unsigned long current_desktop = 1;
static unsigned long active_border;
static unsigned long inactive_border;

static Cursor cursor_default;
static Cursor cursor_move;
static Cursor cursor_resize;

static Visual *visual;
static Colormap colormap;
static XftDraw *xft_draw;
static XftFont *xft_font;
static XftColor xft_color;
static XGlyphInfo extents;

static Atom _NET_WM_DESKTOP;
static Atom _NET_CURRENT_DESKTOP;
static Atom _NET_NUMBER_OF_DESKTOPS;
static Atom _NET_CLIENT_LIST;

static int ignore_x_error(Display *dpy, XErrorEvent *err) {
	(void)dpy;
	(void)err;
	return 0;
}

static void force_display_redraw(void) {
	XClearArea(dpy, root, 0, 0, 1, 1, True);
	XFlush(dpy);
}

static int window_exists(Window w) {
	if (w == None) return 0;
	XErrorHandler old = XSetErrorHandler(ignore_x_error);
	XWindowAttributes attr;
	Status s = XGetWindowAttributes(dpy, w, &attr);
	XSync(dpy, False);
	XSetErrorHandler(old);
	return s != 0;
}

static void update_borders(Window new_active) {
	if (active_window != None && active_window != new_active) {
		if (window_exists(active_window)) {
			XSetWindowBorder(dpy, active_window, inactive_border);
		}
	}

	if (new_active != None) {
		if (window_exists(new_active)) {
			XSetWindowBorder(dpy, new_active, active_border);
		} else {
			new_active = None;
		}
	}

	active_window = new_active;
}

static void add_to_client_list(Window window) {
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

static void remove_from_client_list(Window window) {
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

static void set_window_desktop(Window window, unsigned long desktop) {
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

static unsigned long get_window_desktop(Window w) {
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

static void switch_desktop(unsigned long desktop) {
	if (desktop < 1 || desktop > number_of_desktops) return;

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
			XSetWindowBorder(dpy, active_window, inactive_border);
		}
		active_window = None;
	}

	log_message(stdout, LOG_DEBUG, "Switched to desktop %lu", desktop);
	force_display_redraw();
}

static void draw_desktop_number(void) {
	char text[50];
	snprintf(text, sizeof(text), "%lu", current_desktop);

	XftTextExtentsUtf8(dpy, xft_font, (FcChar8 *)text, strlen(text), &extents);
	XClearArea(dpy, root, DisplayWidth(dpy, screen) - 30, 10, extents.width + 10, extents.height + 10, False);

	XftColor blue_color;
	XRenderColor render_blue = {0, 0, 0xFFFF, 0xFFFF};
	XftColorAllocValue(dpy, DefaultVisual(dpy, screen), DefaultColormap(dpy, screen), &render_blue, &blue_color);

	XftDrawRect(xft_draw, &blue_color, DisplayWidth(dpy, screen) - 30, 10, extents.width + 10, extents.height + 10);
	XftDrawStringUtf8(xft_draw, &xft_color, xft_font, DisplayWidth(dpy, screen) - 25, 10 + xft_font->ascent, (FcChar8 *)text, strlen(text));

	XftColorFree(dpy, DefaultVisual(dpy, screen), DefaultColormap(dpy, screen), &blue_color);
	XFlush(dpy);
}

static void draw_current_time(void) {
	int width = DisplayWidth(dpy, screen) - 40;
	int x = 10;
	int y = 10;
	char text[50];

	time_t now = time(NULL);
	struct tm *tm_info = localtime(&now);
	strftime(text, sizeof(text), "%A %d.%m.%Y %H:%M:%S", tm_info);

	XftTextExtentsUtf8(dpy, xft_font, (FcChar8 *)text, strlen(text), &extents);
	/* XClearArea(dpy, root, x, y, extents.width, extents.height, False); */
	XClearArea(dpy, root, 0, 0,  DisplayWidth(dpy, screen) - 30, 50, False);
	XftDrawStringUtf8(xft_draw, &xft_color, xft_font, width - (xft_font->max_advance_width * strlen(text)) - x, y + xft_font->ascent, (FcChar8 *)text, strlen(text));

	XFlush(dpy);
}

static void* expose_timer_thread(void* arg) {
	(void)arg;

	for(;;) {
		sleep(1);

		if (dpy != NULL) {
			XEvent event;
			memset(&event, 0, sizeof(event));

			event.type = Expose;
			event.xexpose.window = root;
			event.xexpose.x = 0;
			event.xexpose.y = 0;
			event.xexpose.width = 1;
			event.xexpose.height = 1;
			event.xexpose.count = 0;

			// This is thread-safe - XSendEvent is designed for this.
			XSendEvent(dpy, root, False, ExposureMask, &event);
			XFlush(dpy);
		}
	}
	return NULL;
}

void move_window_x(const Arg *arg) {
	if (active_window != None) {
		XWindowAttributes attr;
		XGetWindowAttributes(dpy, active_window, &attr);
		XMoveWindow(dpy, active_window, attr.x + arg->i, attr.y);
		log_message(stdout, LOG_DEBUG, "Move window 0x%lx on X by %d", active_window, arg->i);
	}
}

void move_window_y(const Arg *arg) {
	if (active_window != None) {
		XWindowAttributes attr;
		XGetWindowAttributes(dpy, active_window, &attr);
		XMoveWindow(dpy, active_window, attr.x, attr.y + arg->i);
		log_message(stdout, LOG_DEBUG, "Move window 0x%lx on Y by %d", active_window, arg->i);
	}
}

void resize_window_x(const Arg *arg) {
	if (active_window != None && window_exists(active_window)) {
		XWindowAttributes attr;
		XGetWindowAttributes(dpy, active_window, &attr);
		XResizeWindow(dpy, active_window, MAX(1, attr.width + arg->i), attr.height);
		log_message(stdout, LOG_DEBUG, "Resize window 0x%lx on X by %d", active_window, arg->i);
	}
}

void resize_window_y(const Arg *arg) {
	if (active_window != None && window_exists(active_window)) {
		XWindowAttributes attr;
		XGetWindowAttributes(dpy, active_window, &attr);
		XResizeWindow(dpy, active_window, attr.width, MAX(1, attr.height + arg->i));
		log_message(stdout, LOG_DEBUG, "Resize window 0x%lx on Y by %d", active_window, arg->i);
	}
}

void switch_to_desktop(const Arg *arg) {
	log_message(stdout, LOG_DEBUG, "Switching to desktop %lu", arg->i);
	switch_desktop(arg->i);
}

void move_to_desktop(const Arg *arg) {
	if (active_window == None || !window_exists(active_window)) {
		log_message(stdout, LOG_DEBUG, "No active window to move");
		return;
	}

	unsigned long target_desktop = arg->i;
	if (target_desktop < 1 || target_desktop > number_of_desktops) {
		log_message(stdout, LOG_DEBUG, "Invalid desktop number: %lu", target_desktop);
		return;
	}

	unsigned long current_desktop = get_window_desktop(active_window);
	if (current_desktop == target_desktop) {
		log_message(stdout, LOG_DEBUG, "Window already on desktop %lu", target_desktop);
		return;
	}

	set_window_desktop(active_window, target_desktop);
	log_message(stdout, LOG_DEBUG, "Moved window 0x%lx from desktop %lu to desktop %lu", active_window, current_desktop, target_desktop);

	XUnmapWindow(dpy, active_window);
	log_message(stdout, LOG_DEBUG, "Unmapped window 0x%lx", active_window);

	if (target_desktop != current_desktop) {
		XSetWindowBorder(dpy, active_window, inactive_border);
	} else {
		XSetWindowBorder(dpy, active_window, active_border);
	}

	XFlush(dpy);
}

int main(void) {
	set_log_level(get_log_level_from_env());

	dpy = XOpenDisplay(NULL);
	if (!dpy) {
		fprintf(stderr, "cannot open display\n");
		return 1;
	}

	screen = DefaultScreen(dpy);
	root = RootWindow(dpy, screen);

	// Create cursors.
	cursor_default = XCreateFontCursor(dpy, XC_left_ptr);
	cursor_move = XCreateFontCursor(dpy, XC_fleur);
	cursor_resize  = XCreateFontCursor(dpy, XC_sizing);

	// Set default cursor for root.
	XDefineCursor(dpy, root, cursor_default);

	// Set fonts and visual stuff.
	visual = DefaultVisual(dpy, screen);
	colormap = DefaultColormap(dpy, screen);
	xft_draw = XftDrawCreate(dpy, root, visual, colormap);

	xft_font = XftFontOpenName(dpy, screen, font_name);
	if (!xft_font) {
		xft_font = XftFontOpenName(dpy, screen, "monospace-12");
	}

	XRenderColor render_color = {0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF};
	XftColorAllocValue(dpy, visual, colormap, &render_color, &xft_color);

	// Initialize EWMH atoms for multiple desktop support.
	_NET_WM_DESKTOP = XInternAtom(dpy, "_NET_WM_DESKTOP", False);
	_NET_CURRENT_DESKTOP = XInternAtom(dpy, "_NET_CURRENT_DESKTOP", False);
	_NET_NUMBER_OF_DESKTOPS = XInternAtom(dpy, "_NET_NUMBER_OF_DESKTOPS", False);
	_NET_CLIENT_LIST = XInternAtom(dpy, "_NET_CLIENT_LIST", False);

	// Set number of desktops and current desktop.
	XChangeProperty(dpy, root, _NET_NUMBER_OF_DESKTOPS, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&number_of_desktops, 1);
	XChangeProperty(dpy, root, _NET_CURRENT_DESKTOP, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&current_desktop, 1);

	for (unsigned int i = 0; i < LENGTH(keybinds); i++) {
		KeyCode keycode = XKeysymToKeycode(dpy, keybinds[i].keysym);
		if (keycode) {
			XGrabKey(dpy, keycode, keybinds[i].mod, root, True, GrabModeAsync, GrabModeAsync);
			log_message(stdout, LOG_DEBUG, "Grabbed key: mod=0x%x, keysym=0x%lx", keybinds[i].mod, keybinds[i].keysym);
		}
	}

	// Grab keys for window dragging.
	XGrabButton(dpy, 1, MODKEY, root, True, ButtonPressMask|ButtonReleaseMask|PointerMotionMask, GrabModeAsync, GrabModeAsync, None, None);
	XGrabButton(dpy, 3, MODKEY, root, True, ButtonPressMask|ButtonReleaseMask|PointerMotionMask, GrabModeAsync, GrabModeAsync, None, None);

	// Prepare border colors.
	Colormap cmap = DefaultColormap(dpy, screen);
	XColor active_color, inactive_color, dummy;
	active_border= BlackPixel(dpy, screen);
	inactive_border= BlackPixel(dpy, screen);

	if (XAllocNamedColor(dpy, cmap, active_border_color, &active_color, &dummy)) {
		active_border = active_color.pixel;
	}

	if (XAllocNamedColor(dpy, cmap, inactive_border_color, &inactive_color, &dummy)) {
		inactive_border = inactive_color.pixel;
	}

	// Root window input selection masks.
	XSelectInput(dpy, root,
			SubstructureRedirectMask | SubstructureNotifyMask |
			FocusChangeMask | EnterWindowMask | LeaveWindowMask |
			ButtonPressMask | ExposureMask);

	start.subwindow = None;

	// Starts Expose ticker for updating widgets.
	pthread_t timer_tid;
	if (pthread_create(&timer_tid, NULL, expose_timer_thread, NULL) != 0) {
		fprintf(stderr, "failed to create timer thread\n");
	} else {
		pthread_detach(timer_tid);
	}

	for(;;) {
		XNextEvent(dpy, &ev);

		switch (ev.type) {
			case MapRequest:
				{
					Window window = ev.xmaprequest.window;
					XSetWindowBorderWidth(dpy, window, border_size);
					XSetWindowBorder(dpy, window, inactive_border);

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
					for (unsigned int i = 0; i < LENGTH(keybinds); i++) {
						if (keysym == keybinds[i].keysym && (ev.xkey.state & (Mod1Mask|ControlMask|ShiftMask)) == keybinds[i].mod) {
							keybinds[i].func(&keybinds[i].arg);
							break;
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

			case Expose:
				if (ev.xexpose.window == root) {
					draw_desktop_number();
					draw_current_time();
				}
				break;

			default:
				break;
		}
	}

	XFreeCursor(dpy, cursor_default);
	XFreeCursor(dpy, cursor_move);
	XFreeCursor(dpy, cursor_resize);

	XftColorFree(dpy, visual, colormap, &xft_color);
	if (xft_font) XftFontClose(dpy, xft_font);
	XftDrawDestroy(xft_draw);
	XFlush(dpy);

	return 0;
}
