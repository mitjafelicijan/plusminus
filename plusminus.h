#ifndef PLUSMINUS_H
#define PLUSMINUS_H

#include <X11/Xlib.h>

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define LENGTH(x) (sizeof(x) / sizeof((x)[0]))

#define COLOR_INFO     "\x1B[0m"   // White
#define COLOR_DEBUG    "\x1B[36m"  // Cyan
#define COLOR_WARNING  "\x1B[33m"  // Yellow
#define COLOR_ERROR    "\x1B[31m"  // Red
#define COLOR_RESET    "\x1B[0m"

typedef struct {
	int i;
	const char *s;
} Arg;

typedef struct {
	unsigned int mod;
	KeySym keysym;
	void (*func)(const Arg *);
	Arg arg;
} Keybinds;

typedef struct {
	unsigned int mod;
	KeySym keysym;
	const char *cmd;
} Shortcut;

typedef enum {
	LOG_INFO,
	LOG_DEBUG,
	LOG_WARNING,
	LOG_ERROR,
} LogLevel;

void set_log_level(LogLevel level);
LogLevel get_log_level_from_env(void);
void log_message(FILE *stream, LogLevel level, const char* format, ...);

// External variables.
extern Display *dpy;
extern Window active_window;
extern unsigned long number_of_desktops;
extern unsigned long active_border;
extern unsigned long inactive_border;

// Maximize state tracking
#define MAX_MAXIMIZE_WINDOWS 32

typedef struct {
	Window window;
	int x, y, width, height;
} MaximizeState;

extern MaximizeState vmaximize_windows[MAX_MAXIMIZE_WINDOWS];
extern int vmaximize_count;
extern MaximizeState hmaximize_windows[MAX_MAXIMIZE_WINDOWS];
extern int hmaximize_count;

// External functions.
int window_exists(Window w);
unsigned long get_window_desktop(Window w);
void set_window_desktop(Window window, unsigned long desktop);
void switch_desktop(unsigned long desktop);
void update_borders(Window new_active);
void add_to_client_list(Window window);
void remove_from_client_list(Window window);
void draw_desktop_number(void);
void draw_current_time(void);
void execute_shortcut(const char *command);

// Function implementations.
void move_window_x(const Arg *arg);
void move_window_y(const Arg *arg);
void resize_window_x(const Arg *arg);
void resize_window_y(const Arg *arg);
void switch_to_desktop(const Arg *arg);
void move_to_desktop(const Arg *arg);
void kill_window(const Arg *arg);
void fullscreen(const Arg *arg);
void window_vmaximize(const Arg *arg);
void window_hmaximize(const Arg *arg);
void window_snap_up(const Arg *arg);
void window_snap_down(const Arg *arg);
void window_snap_right(const Arg *arg);
void window_snap_left(const Arg *arg);

// Helper functions for maximize state management
int find_vmaximize_window(Window window);
int find_hmaximize_window(Window window);
void remove_vmaximize_window(Window window);
void remove_hmaximize_window(Window window);

#endif // PLUSMINUS_H
