
#ifndef _DFU_FILE_H
#define _DFU_FILE_H

#include <stdio.h>
#include "usb_dfu.h"

struct dfu_file {
    const char *name;
#ifdef ENV_WINDOWS
    FILE *fd;
#else
    int fd;
#endif
    off_t size;
    /* From DFU suffix fields */
    u_int32_t dwCRC;
    unsigned char suffixlen;
    u_int16_t bcdDFU;
    u_int16_t idVendor;
    u_int16_t idProduct;
    u_int16_t bcdDevice;
};

int parse_dfu_suffix(struct dfu_file *file);

#endif
