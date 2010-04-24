AM_CFLAGS = -Wall

dfu-version.h:
	echo -e '#ifndef DFU_UTIL_VERSION' \
	'\n#define DFU_UTIL_VERSION "'`svnversion -n`'"' \
	'\n#endif' > dfu-version.h
BUILT_SOURCES = dfu-version.h

bin_PROGRAMS = dfu-util dfu-util_static
dfu_util_SOURCES = main.c \
		sam7dfu.c \
		sam7dfu.h \
		dfu.c \
		dfu.h \
		usb_dfu.h

dfu_util_static_SOURCES = main.c \
			sam7dfu.c \
			sam7dfu.h \
			dfu.c \
			dfu.h \
			usb_dfu.h

dfu_util_static_LDFLAGS = -static

EXTRA_DIST = dfu-version.h
