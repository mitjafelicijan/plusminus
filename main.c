#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdbool.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <X11/XF86keysym.h>
#include <X11/XKBlib.h>
#include <X11/cursorfont.h>
#include <X11/Xft/Xft.h>

#include "plusminus.h"
#include "config.h"

Display *dpy;
static Window root;
Window active_window = None;
static XWindowAttributes attr;
static XButtonEvent start;
static XEvent ev;
static int screen;

// Fullscreen state tracking.
static Window fullscreen_window = None;
static int fullscreen_x, fullscreen_y, fullscreen_width, fullscreen_height;

MaximizeState vmaximize_windows[MAX_MAXIMIZE_WINDOWS];
int vmaximize_count = 0;

MaximizeState hmaximize_windows[MAX_MAXIMIZE_WINDOWS];
int hmaximize_count = 0;

unsigned long number_of_desktops = 9;
unsigned long current_desktop = 1;
unsigned long active_border;
unsigned long inactive_border;
unsigned long sticky_active_border;
unsigned long sticky_inactive_border;

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
static Atom _NET_WM_STATE;
static Atom _NET_WM_STATE_FULLSCREEN;
static Atom _NET_ACTIVE_WINDOW;

static int ignore_x_error(Display *dpy, XErrorEvent *err) {
	(void)dpy;
	(void)err;
	return 0;
}

static void force_display_redraw(void) {
	XClearArea(dpy, root, 0, 0, 1, 1, True);
	XFlush(dpy);
}

void execute_shortcut(const char *command) {
	if (!command || strlen(command) == 0) {
		log_message(stderr, LOG_WARNING, "Empty command provided to execute_shortcut");
		return;
	}

	pid_t pid = fork();
	if (pid == -1) {
		log_message(stderr, LOG_ERROR, "Failed to fork process for command: %s", command);
		return;
	}

	if (pid == 0) {
		execl("/bin/sh", "sh", "-c", command, (char *)NULL);
		log_message(stderr, LOG_ERROR, "Failed to execute command: %s", command);
		exit(1);
	} else {
		log_message(stdout, LOG_DEBUG, "Executed command in background: %s", command);
	}
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

static void set_active_window_property(Window window) {
	if (window != None) {
		XChangeProperty(dpy, root, _NET_ACTIVE_WINDOW, XA_WINDOW, 32, PropModeReplace, (unsigned char *)&window, 1);
	} else {
		XDeleteProperty(dpy, root, _NET_ACTIVE_WINDOW);
	}
	XFlush(dpy);
}

// Helper functions for maximize state management.
int find_vmaximize_window(Window window) {
	for (int i = 0; i < vmaximize_count; i++) {
		if (vmaximize_windows[i].window == window) {
			return i;
		}
	}
	return -1;
}

int find_hmaximize_window(Window window) {
	for (int i = 0; i < hmaximize_count; i++) {
		if (hmaximize_windows[i].window == window) {
			return i;
		}
	}
	return -1;
}

void remove_vmaximize_window(Window window) {
	int index = find_vmaximize_window(window);
	if (index >= 0) {
		for (int i = index; i < vmaximize_count - 1; i++) {
			vmaximize_windows[i] = vmaximize_windows[i + 1];
		}
		vmaximize_count--;
	}
}

void remove_hmaximize_window(Window window) {
	int index = find_hmaximize_window(window);
	if (index >= 0) {
		for (int i = index; i < hmaximize_count - 1; i++) {
			hmaximize_windows[i] = hmaximize_windows[i + 1];
		}
		hmaximize_count--;
	}
}

void update_borders(Window new_active) {
	if (active_window != None && active_window != new_active) {
		if (window_exists(active_window)) {
			unsigned long border_color;
			if (get_window_desktop(active_window) == 0) {
				border_color = sticky_inactive_border;
			} else {
				border_color = inactive_border;
			}
			XSetWindowBorder(dpy, active_window, border_color);
		}
	}

	if (new_active != None) {
		if (window_exists(new_active)) {
			unsigned long border_color;
			if (get_window_desktop(new_active) == 0) {
				border_color = sticky_active_border;
			} else {
				border_color = active_border;
			}
			XSetWindowBorder(dpy, new_active, border_color);
		} else {
			new_active = None;
		}
	}

	active_window = new_active;
	set_active_window_property(active_window);
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
		// This is a workaround to avoid crashing the program.
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
	if (desktop < 1 || desktop > number_of_desktops) return;

	current_desktop = desktop;

	unsigned long value = desktop;
	XChangeProperty(dpy, root, _NET_CURRENT_DESKTOP, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&value, 1);

	Atom type;
	int format;
	unsigned long nitems, bytes_after;
	unsigned char *data = NULL;

	Window first_window_on_desktop = None;
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

				if (window_desktop == 0 || window_desktop == current_desktop) {
					log_message(stdout, LOG_DEBUG, "Mapping window 0x%lx (desktop %lu)", w, window_desktop);
					XMapWindow(dpy, w);
					if (first_window_on_desktop == None && window_desktop != 0) {
						first_window_on_desktop = w;
					}
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
			unsigned long border_color;
			if (get_window_desktop(active_window) == 0) {
				border_color = sticky_inactive_border;
			} else {
				border_color = inactive_border;
			}
			XSetWindowBorder(dpy, active_window, border_color);
		}
		active_window = None;
		set_active_window_property(active_window);
	}

	// Activate the first window on the new desktop.
	if (first_window_on_desktop != None) {
		XRaiseWindow(dpy, first_window_on_desktop);
		XSetInputFocus(dpy, first_window_on_desktop, RevertToPointerRoot, CurrentTime);
		update_borders(first_window_on_desktop);
		log_message(stdout, LOG_DEBUG, "Activated first window 0x%lx on desktop %lu", first_window_on_desktop, desktop);
	}

	log_message(stdout, LOG_DEBUG, "Switched to desktop %lu", desktop);
	force_display_redraw();
}

void draw_desktop_number(void) {
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

void draw_current_time(void) {
	int width = DisplayWidth(dpy, screen) - 40;
	int x = 10;
	int y = 10;
	char text[50];

	time_t now = time(NULL);
	struct tm *tm_info = localtime(&now);
	strftime(text, sizeof(text), time_format, tm_info);

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

static int is_fullscreen(Window window) {
	Atom type;
	int format;
	unsigned long nitems, bytes_after;
	unsigned char *data = NULL;
	int fullscreen = 0;

	if (XGetWindowProperty(dpy, window, _NET_WM_STATE, 0, 1024, False, XA_ATOM, &type, &format, &nitems, &bytes_after, &data) == Success) {
		if (data && type == XA_ATOM && format == 32) {
			Atom *states = (Atom *)data;
			for (unsigned long i = 0; i < nitems; i++) {
				if (states[i] == _NET_WM_STATE_FULLSCREEN) {
					fullscreen = 1;
					break;
				}
			}
		}
		if (data) XFree(data);
	}

	return fullscreen;
}

static void set_fullscreen(Window window, int fullscreen) {
	if (fullscreen) {
		XWindowAttributes attr;
		XGetWindowAttributes(dpy, window, &attr);
		fullscreen_x = attr.x;
		fullscreen_y = attr.y;
		fullscreen_width = attr.width;
		fullscreen_height = attr.height;

		XSetWindowBorderWidth(dpy, window, 0);
		XMoveResizeWindow(dpy, window, 0, 0, DisplayWidth(dpy, screen), DisplayHeight(dpy, screen));

		XChangeProperty(dpy, window, _NET_WM_STATE, XA_ATOM, 32, PropModeReplace, (unsigned char *)&_NET_WM_STATE_FULLSCREEN, 1);
		fullscreen_window = window;

		log_message(stdout, LOG_DEBUG, "Window 0x%lx set to fullscreen", window);
	} else {
		XSetWindowBorderWidth(dpy, window, border_size);
		XMoveResizeWindow(dpy, window, fullscreen_x, fullscreen_y, fullscreen_width, fullscreen_height);

		XDeleteProperty(dpy, window, _NET_WM_STATE);
		fullscreen_window = None;

		log_message(stdout, LOG_DEBUG, "Window 0x%lx restored from fullscreen", window);
	}

	XFlush(dpy);
}

static void toggle_fullscreen(Window window) {
	if (window == None || !window_exists(window)) {
		log_message(stdout, LOG_DEBUG, "No valid window to toggle fullscreen");
		return;
	}

	if (is_fullscreen(window)) {
		set_fullscreen(window, 0);
	} else {
		set_fullscreen(window, 1);
	}
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
	_NET_WM_STATE = XInternAtom(dpy, "_NET_WM_STATE", False);
	_NET_WM_STATE_FULLSCREEN = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);
	_NET_ACTIVE_WINDOW = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);

	// Set number of desktops and current desktop.
	XChangeProperty(dpy, root, _NET_NUMBER_OF_DESKTOPS, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&number_of_desktops, 1);
	XChangeProperty(dpy, root, _NET_CURRENT_DESKTOP, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&current_desktop, 1);

	XUngrabButton(dpy, AnyButton, AnyModifier, root);

	// Grab keys for keybinds.
	for (unsigned int i = 0; i < LENGTH(keybinds); i++) {
		KeyCode keycode = XKeysymToKeycode(dpy, keybinds[i].keysym);
		if (keycode) {
			XGrabKey(dpy, keycode, keybinds[i].mod, root, True, GrabModeAsync, GrabModeAsync);
			log_message(stdout, LOG_DEBUG, "Grabbed key: mod=0x%x, keysym=0x%lx", keybinds[i].mod, keybinds[i].keysym);
		}
	}

	// Grab keys for shortcuts.
	for (unsigned int i = 0; i < LENGTH(shortcuts); i++) {
		KeyCode keycode = XKeysymToKeycode(dpy, shortcuts[i].keysym);
		if (keycode) {
			XGrabKey(dpy, keycode, shortcuts[i].mod, root, True, GrabModeAsync, GrabModeAsync);
			log_message(stdout, LOG_DEBUG, "Grabbed shortcut: mod=0x%x, keysym=0x%lx, command=%s", shortcuts[i].mod, shortcuts[i].keysym, shortcuts[i].cmd);
		}
	}

	// Grab keys for window dragging (with MODKEY).
	XGrabButton(dpy, 1, MODKEY, root, True, ButtonPressMask|ButtonReleaseMask|PointerMotionMask, GrabModeAsync, GrabModeAsync, None, None);
	XGrabButton(dpy, 3, MODKEY, root, True, ButtonPressMask|ButtonReleaseMask|PointerMotionMask, GrabModeAsync, GrabModeAsync, None, None);

	// Prepare border colors.
	Colormap cmap = DefaultColormap(dpy, screen);
	XColor active_color, inactive_color, sticky_active_color, sticky_inactive_color, dummy;
	active_border= BlackPixel(dpy, screen);
	inactive_border= BlackPixel(dpy, screen);
	sticky_active_border = BlackPixel(dpy, screen);
	sticky_inactive_border = BlackPixel(dpy, screen);

	if (XAllocNamedColor(dpy, cmap, active_border_color, &active_color, &dummy)) {
		active_border = active_color.pixel;
	}

	if (XAllocNamedColor(dpy, cmap, inactive_border_color, &inactive_color, &dummy)) {
		inactive_border = inactive_color.pixel;
	}

	if (XAllocNamedColor(dpy, cmap, sticky_active_border_color, &sticky_active_color, &dummy)) {
		sticky_active_border = sticky_active_color.pixel;
	}

	if (XAllocNamedColor(dpy, cmap, sticky_inactive_border_color, &sticky_inactive_color, &dummy)) {
		sticky_inactive_border = sticky_inactive_color.pixel;
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

						Window root_return, child_return;
						int root_x, root_y, win_x, win_y;
						unsigned int mask;

						if (XQueryPointer(dpy, root, &root_return, &child_return, &root_x, &root_y, &win_x, &win_y, &mask)) {
							int new_x = root_x - (check_attr.width / 2);
							int new_y = root_y - (check_attr.height / 2);
							int screen_width = DisplayWidth(dpy, screen);
							int screen_height = DisplayHeight(dpy, screen);

							if (new_x < 0) new_x = 0;
							if (new_y < 0) new_y = 0;
							if (new_x + check_attr.width > screen_width) new_x = screen_width - check_attr.width;
							if (new_y + check_attr.height > screen_height) new_y = screen_height - check_attr.height;

							XMoveWindow(dpy, window, new_x, new_y);
							log_message(stdout, LOG_DEBUG, "Positioned new window 0x%lx at cursor (%d, %d)", window, root_x, root_y);
						}
					}

					XMapWindow(dpy, window);
					log_message(stdout, LOG_DEBUG, "Window 0x%lx mapped", window);

					// Make the new window active and focused.
					XRaiseWindow(dpy, window);
					XSetInputFocus(dpy, window, RevertToPointerRoot, CurrentTime);
					update_borders(window);
					log_message(stdout, LOG_DEBUG, "Window 0x%lx raised and focused", window);


					add_to_client_list(window);
					set_window_desktop(window, current_desktop);

					// Update border color based on desktop (sticky windows get violet border).
					unsigned long border_color;
					if (get_window_desktop(window) == 0) {
						border_color = sticky_active_border;
					} else {
						border_color = active_border;
					}
					XSetWindowBorder(dpy, window, border_color);
				} break;

			case DestroyNotify:
				{
					if (ev.xdestroywindow.window == active_window) {
						update_borders(None);
						log_message(stdout, LOG_DEBUG, "Window 0x%lx destroyed", ev.xdestroywindow.window);
					}

					if (ev.xdestroywindow.window == fullscreen_window) {
						fullscreen_window = None;
						log_message(stdout, LOG_DEBUG, "Fullscreen window 0x%lx destroyed", ev.xdestroywindow.window);
					}

					// Remove from vertical maximize tracking.
					for (int i = 0; i < vmaximize_count; i++) {
						if (vmaximize_windows[i].window == ev.xdestroywindow.window) {
							// Shift remaining elements left.
							for (int j = i; j < vmaximize_count - 1; j++) {
								vmaximize_windows[j] = vmaximize_windows[j + 1];
							}
							vmaximize_count--;
							log_message(stdout, LOG_DEBUG, "Vertically maximized window 0x%lx destroyed", ev.xdestroywindow.window);
							break;
						}
					}

					// Remove from horizontal maximize tracking.
					for (int i = 0; i < hmaximize_count; i++) {
						if (hmaximize_windows[i].window == ev.xdestroywindow.window) {
							// Shift remaining elements left.
							for (int j = i; j < hmaximize_count - 1; j++) {
								hmaximize_windows[j] = hmaximize_windows[j + 1];
							}
							hmaximize_count--;
							log_message(stdout, LOG_DEBUG, "Horizontally maximized window 0x%lx destroyed", ev.xdestroywindow.window);
							break;
						}
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
					if (follow_focus) {
						Window entered_window = ev.xcrossing.window;
						if (entered_window != root && ev.xcrossing.mode == NotifyNormal) {
							if (entered_window != None && entered_window != active_window) {
								XRaiseWindow(dpy, entered_window);
								XSetInputFocus(dpy, entered_window, RevertToPointerRoot, CurrentTime);
								update_borders(entered_window);
							}
						}
					}
				} break;

			case KeyPress:
				{
					KeySym keysym = XkbKeycodeToKeysym(dpy, ev.xkey.keycode, 0, 0);

					// Check keybinds first.
					for (unsigned int i = 0; i < LENGTH(keybinds); i++) {
						if (keysym == keybinds[i].keysym && (ev.xkey.state & (Mod1Mask|Mod2Mask|Mod3Mask|Mod4Mask|ControlMask|ShiftMask)) == keybinds[i].mod) {
							keybinds[i].func(&keybinds[i].arg);
							break;
						}
					}

					// Check shortcuts.
					for (unsigned int i = 0; i < LENGTH(shortcuts); i++) {
						if (keysym == shortcuts[i].keysym && (ev.xkey.state & (Mod1Mask|Mod2Mask|Mod3Mask|Mod4Mask|ControlMask|ShiftMask)) == shortcuts[i].mod) {
							execute_shortcut(shortcuts[i].cmd);
							break;
						}
					}
				} break;

			case ButtonPress:
				{
					if (ev.xbutton.subwindow != None) {
						if (ev.xbutton.state & MODKEY) {
							XGetWindowAttributes(dpy, ev.xbutton.subwindow, &attr);
							start = ev.xbutton;

							// Raise and focus the window.
							XRaiseWindow(dpy, ev.xbutton.subwindow);
							XSetInputFocus(dpy, ev.xbutton.subwindow, RevertToPointerRoot, CurrentTime);
							update_borders(ev.xbutton.subwindow);

							// Set appropriate cursor for dragging.
							if (start.button == 1) {
								log_message(stdout, LOG_DEBUG, "Setting cursor to move");
								XDefineCursor(dpy, start.subwindow, cursor_move);
							} else if (start.button == 3) {
								log_message(stdout, LOG_DEBUG, "Setting cursor to resize");
								XDefineCursor(dpy, start.subwindow, cursor_resize);
							}
							log_message(stdout, LOG_DEBUG, "MODKEY click on window 0x%lx - dragging enabled", ev.xbutton.subwindow);
						}
						XFlush(dpy);
					}
				} break;

			case ButtonRelease:
				{
					if (start.subwindow != None) {
						// MODKEY drag release: restore cursor.
						if (start.state & MODKEY) {
							XDefineCursor(dpy, start.subwindow, None);
						}
						XFlush(dpy);
					}
				} break;

			case MotionNotify:
				{
					if (start.subwindow != None && (start.state & MODKEY)) {
						int xdiff = ev.xmotion.x_root - start.x_root;
						int ydiff = ev.xmotion.y_root - start.y_root;

						XMoveResizeWindow(dpy, start.subwindow,
								attr.x + (start.button == 1 ? xdiff : 0),
								attr.y + (start.button == 1 ? ydiff : 0),
								MAX(50, attr.width  + (start.button == 3 ? xdiff : 0)),
								MAX(50, attr.height + (start.button == 3 ? ydiff : 0)));
					}
				} break;

			case ClientMessage:
				{
					if (ev.xclient.message_type == _NET_WM_STATE) {
						Atom action = ev.xclient.data.l[0];
						Atom state = ev.xclient.data.l[1];
						Window window = ev.xclient.window;

						if (state == _NET_WM_STATE_FULLSCREEN) {
							if (action == 1) { // _NET_WM_STATE_ADD
								set_fullscreen(window, 1);
							} else if (action == 0) { // _NET_WM_STATE_REMOVE
								set_fullscreen(window, 0);
							} else if (action == 2) { // _NET_WM_STATE_TOGGLE
								toggle_fullscreen(window);
							}
						}
					} else if (ev.xclient.message_type == _NET_ACTIVE_WINDOW) {
						Window window = ev.xclient.data.l[0];
						if (window != None && window_exists(window)) {
							// Check if window is on current desktop.
							unsigned long window_desktop = get_window_desktop(window);
							if (window_desktop == current_desktop) {
								XRaiseWindow(dpy, window);
								XSetInputFocus(dpy, window, RevertToPointerRoot, CurrentTime);
								update_borders(window);
								log_message(stdout, LOG_DEBUG, "Activated window 0x%lx via _NET_ACTIVE_WINDOW", window);
							} else {
								// Switch to the window's desktop first.
								switch_desktop(window_desktop);
								XRaiseWindow(dpy, window);
								XSetInputFocus(dpy, window, RevertToPointerRoot, CurrentTime);
								update_borders(window);
								log_message(stdout, LOG_DEBUG, "Activated window 0x%lx on desktop %lu via _NET_ACTIVE_WINDOW", window, window_desktop);
							}
						}
					}
				}
				break;

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
