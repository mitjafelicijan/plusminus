# Tiny X11 Window Manager

## Requirements

- C compiler
- GNU Make
- pkg-config

X11 and Freetype development libraries are required.

```sh
sudo xbps-install libX11-devel freetype-devel
```

## Compilation

```sh
# Build normally without optimizations and without debug symbols.
CC=clang make

# Builds with debug symbols.
DEBUG=1 make

# Compile with -On optimizations.
OPTIMIZE=1 make
OPTIMIZE=2 make
OPTIMIZE=3 make
```

## Inspiration

- https://www.youtube.com/watch?v=JZcMLjnm1ps
- https://wumbo.net/symbols/plus-minus/
- https://cvsweb.openbsd.org/xenocara/app/cwm/
- https://st.suckless.org/
