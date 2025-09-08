#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>

#include "plusminus.h"

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

void kill_window(const Arg *arg) {
	(void)arg;

	if (active_window == None || !window_exists(active_window)) {
		log_message(stdout, LOG_DEBUG, "No active window to kill");
		return;
	}

	XKillClient(dpy, active_window);
	XFlush(dpy);
	log_message(stdout, LOG_DEBUG, "Force killed window 0x%lx", active_window);
}

void fullscreen(const Arg *arg) {
	(void)arg;

	if (active_window == None || !window_exists(active_window)) {
		log_message(stdout, LOG_DEBUG, "No active window to toggle fullscreen");
		return;
	}

	// Send a client message to request fullscreen toggle
	XEvent event;
	memset(&event, 0, sizeof(event));
	event.type = ClientMessage;
	event.xclient.window = active_window;
	event.xclient.message_type = XInternAtom(dpy, "_NET_WM_STATE", False);
	event.xclient.format = 32;
	event.xclient.data.l[0] = 2; // _NET_WM_STATE_TOGGLE
	event.xclient.data.l[1] = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);
	event.xclient.data.l[2] = 0;
	event.xclient.data.l[3] = 0;
	event.xclient.data.l[4] = 0;

	XSendEvent(dpy, DefaultRootWindow(dpy), False, SubstructureRedirectMask | SubstructureNotifyMask, &event);
	XFlush(dpy);
	log_message(stdout, LOG_DEBUG, "Sent fullscreen toggle request for window 0x%lx", active_window);
}

void window_vmaximize(const Arg *arg) {
	(void)arg;

	if (active_window == None || !window_exists(active_window)) {
		log_message(stdout, LOG_DEBUG, "No active window to vertically maximize");
		return;
	}

	// Check if this window is already vertically maximized
	int index = find_vmaximize_window(active_window);
	if (index >= 0) {
		// Restore original dimensions
		MaximizeState *state = &vmaximize_windows[index];
		XMoveResizeWindow(dpy, active_window, state->x, state->y, state->width, state->height);
		XFlush(dpy);
		remove_vmaximize_window(active_window);
		log_message(stdout, LOG_DEBUG, "Restored window 0x%lx from vertical maximize", active_window);
		return;
	}

	// Check if we have space for another maximized window
	if (vmaximize_count >= MAX_MAXIMIZE_WINDOWS) {
		log_message(stdout, LOG_DEBUG, "Maximum number of vertically maximized windows reached");
		return;
	}

	XWindowAttributes attr;
	if (!XGetWindowAttributes(dpy, active_window, &attr)) {
		log_message(stdout, LOG_DEBUG, "Failed to get window attributes for 0x%lx", active_window);
		return;
	}

	// Store original dimensions
	MaximizeState *state = &vmaximize_windows[vmaximize_count];
	state->window = active_window;
	state->x = attr.x;
	state->y = attr.y;
	state->width = attr.width;
	state->height = attr.height;
	vmaximize_count++;

	// Get screen dimensions
	int screen_height = DisplayHeight(dpy, DefaultScreen(dpy));

	// Calculate new dimensions - keep width, maximize height
	// Account for border size
	int border_width = attr.border_width;

	int new_x = attr.x;
	int new_y = 0;
	int new_width = attr.width;
	int new_height = screen_height - (2 * border_width);

	// Ensure minimum height
	if (new_height < 50) {
		new_height = 50;
	}

	XMoveResizeWindow(dpy, active_window, new_x, new_y, new_width, new_height);
	XFlush(dpy);

	log_message(stdout, LOG_DEBUG, "Vertically maximized window 0x%lx to height %d", active_window, new_height);
}

void window_hmaximize(const Arg *arg) {
	(void)arg;

	if (active_window == None || !window_exists(active_window)) {
		log_message(stdout, LOG_DEBUG, "No active window to horizontally maximize");
		return;
	}

	// Check if this window is already horizontally maximized
	int index = find_hmaximize_window(active_window);
	if (index >= 0) {
		// Restore original dimensions
		MaximizeState *state = &hmaximize_windows[index];
		XMoveResizeWindow(dpy, active_window, state->x, state->y, state->width, state->height);
		XFlush(dpy);
		remove_hmaximize_window(active_window);
		log_message(stdout, LOG_DEBUG, "Restored window 0x%lx from horizontal maximize", active_window);
		return;
	}

	// Check if we have space for another maximized window
	if (hmaximize_count >= MAX_MAXIMIZE_WINDOWS) {
		log_message(stdout, LOG_DEBUG, "Maximum number of horizontally maximized windows reached");
		return;
	}

	XWindowAttributes attr;
	if (!XGetWindowAttributes(dpy, active_window, &attr)) {
		log_message(stdout, LOG_DEBUG, "Failed to get window attributes for 0x%lx", active_window);
		return;
	}

	// Store original dimensions
	MaximizeState *state = &hmaximize_windows[hmaximize_count];
	state->window = active_window;
	state->x = attr.x;
	state->y = attr.y;
	state->width = attr.width;
	state->height = attr.height;
	hmaximize_count++;

	// Get screen dimensions
	int screen_width = DisplayWidth(dpy, DefaultScreen(dpy));

	// Calculate new dimensions - keep height, maximize width
	// Account for border size
	int border_width = attr.border_width;

	int new_x = 0;
	int new_y = attr.y;
	int new_width = screen_width - (2 * border_width);
	int new_height = attr.height;

	// Ensure minimum width
	if (new_width < 50) {
		new_width = 50;
	}

	XMoveResizeWindow(dpy, active_window, new_x, new_y, new_width, new_height);
	XFlush(dpy);

	log_message(stdout, LOG_DEBUG, "Horizontally maximized window 0x%lx to width %d", active_window, new_width);
}
