AM_CFLAGS = -Wall
bin_PROGRAMS = dfu-util dfu-util_static
dfu_util_SOURCES = main.c sam7dfu.c dfu.c dfu.h
dfu_util_LDFLAGS = -lusb

dfu_util_static_SOURCES = main.c sam7dfu.c dfu.c dfu.h
dfu_util_static_LDFLAGS = -static -lusb

# commands.c commands.h sam7dfu.c

