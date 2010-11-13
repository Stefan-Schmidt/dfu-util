AM_CFLAGS = -Wall

dfu-version.h:
	echo -e '#ifndef DFU_UTIL_VERSION' \
	'\n#define DFU_UTIL_VERSION "'`git show --format="%H" -s HEAD`'"' \
	'\n#endif' > dfu-version.h
BUILT_SOURCES = dfu-version.h

bin_PROGRAMS = dfu-util
dfu_util_SOURCES = main.c \
		dfu_load.c \
		dfu_load.h \
		dfu.c \
		dfu.h \
		usb_dfu.h

EXTRA_DIST = dfu-version.h
