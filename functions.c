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
