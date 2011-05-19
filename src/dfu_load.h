#ifndef _SAM7DFU_H
#define _SAM7DFU_H

int dfuload_do_upload(struct dfu_if *dif, int xfer_size, int fd);
int dfuload_do_dnload(struct dfu_if *dif, int xfer_size, int fd);

#endif
