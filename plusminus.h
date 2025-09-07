#ifndef PLUSMINUS_H
#define PLUSMINUS_H

#include <X11/Xlib.h>

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
} Shortcut;

typedef enum {
	LOG_INFO,
	LOG_DEBUG,
	LOG_WARNING,
	LOG_ERROR,
} LogLevel;

#define COLOR_INFO     "\x1B[0m"   // White
#define COLOR_DEBUG    "\x1B[36m"  // Cyan
#define COLOR_WARNING  "\x1B[33m"  // Yellow
#define COLOR_ERROR    "\x1B[31m"  // Red
#define COLOR_RESET    "\x1B[0m"

void set_log_level(LogLevel level);
LogLevel get_log_level_from_env(void);
void log_message(FILE *stream, LogLevel level, const char* format, ...);

void move_window_x(const Arg *arg);
void move_window_y(const Arg *arg);
void resize_window_x(const Arg *arg);
void resize_window_y(const Arg *arg);
void switch_to_desktop(const Arg *arg);

#endif // PLUSMINUS_H
