
#ifndef _DFU_FILE_H
#define _DFU_FILE_H

struct dfu_file {
    const char *name;
    FILE *filep;
    off_t size;
    /* From DFU suffix fields */
    uint32_t dwCRC;
    unsigned char suffixlen;
    uint16_t bcdDFU;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
};

int parse_dfu_suffix(struct dfu_file *file);
int generate_dfu_suffix(struct dfu_file *file);

#endif
