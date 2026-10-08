# bmake Makefile for Linux + neuswc
CC      ?= cc
CFLAGS   = -std=c99 -Wall -Wextra -O2
CPPFLAGS = -D_POSIX_C_SOURCE=200809L

PREFIX ?= /usr/local
BINDIR  = ${PREFIX}/bin
OUT     = minidwc
SRC     = minidwc.c

# Keep the same dependency set as DWC for maximum compatibility with neuswc.
PKGS = swc wayland-server xkbcommon libinput pixman-1 libdrm wld libudev xcb xcb-composite xcb-ewmh xcb-icccm

PKG_CFLAGS != pkg-config --cflags ${PKGS}
PKG_LIBS   != pkg-config --libs ${PKGS}

CFLAGS += ${PKG_CFLAGS}
LDLIBS += ${PKG_LIBS} -lm

all: ${OUT}

${OUT}: ${SRC}
	${CC} ${CFLAGS} ${CPPFLAGS} -o ${OUT} ${SRC} ${LDLIBS}

clean:
	rm -f ${OUT}

install: ${OUT}
	install -D -m 755 ${OUT} ${DESTDIR}${BINDIR}/${OUT}

uninstall:
	rm -f ${DESTDIR}${BINDIR}/${OUT}
