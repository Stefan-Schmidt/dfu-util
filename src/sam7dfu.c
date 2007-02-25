/* This is supposed to be a "real" DFU implementation, just as specified in the
 * USB DFU 1.0 Spec.  Not overloaded like the Atmel one...
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <usb.h>

#include "config.h"
#include "dfu.h"
#include "usb_dfu.h"

int sam7dfu_do_upload(struct usb_dev_handle *usb_handle, int interface, 
		      int xfer_size, const char *fname)
{
	int ret, fd, total_bytes;
	char *buf = malloc(xfer_size);

	if (!buf)
		return -ENOMEM;

	fd = creat(fname, 0644);
	if (fd < 0) {
		ret = fd;
		goto out_free;
	}
	
	while (1) {
		int rc, write_rc;
		rc = dfu_upload(usb_handle, interface, xfer_size, buf);
		if (rc < 0) {
			ret = rc;
			goto out_close;
		}
		write_rc = write(fd, buf, rc);
		if (write_rc < rc) {
			fprintf(stderr, "Short write: %s\n",
				strerror(errno));
			goto out_close;
		}
		total_bytes += rc;
		if (rc < xfer_size) {
			/* last block, return */
			ret = total_bytes;
			goto out_close;
		}
	}
	ret = 0;

out_close:
	close(fd);
out_free:
	free(buf);
	
	return ret;
}

int sam7dfu_do_dnload(struct usb_dev_handle *usb_handle, int interface,
		      int xfer_size, const char *fname)
{
	int ret, fd, bytes_sent = 0;
	char *buf = malloc(xfer_size);
	struct stat st;
	struct dfu_status dst;

	if (!buf)
		return -ENOMEM;

	fd = open(fname, O_RDONLY);
	if (fd < 0) {
		ret = fd;
		goto out_free;
	}
	
	ret = fstat(fd, &st);
	if (ret < 0)
		goto out_close;

	if (st.st_size <= 0 /* + DFU_HDR */) {
		fprintf(stderr, "File seems a bit too small...\n");
		ret = -EINVAL;
		goto out_close;	
	}

#if 0
	read(fd, DFU_HDR);
#endif
	printf("Starting download: [");
	while (bytes_sent < st.st_size /* - DFU_HDR */) {
		ret = read(fd, buf, xfer_size);
		if (ret < 0)
			goto out_close;
		ret = dfu_download(usb_handle, interface, ret, buf);
		if (ret < 0)
			goto out_close;
		bytes_sent += ret;
		do {
			ret = dfu_get_status(usb_handle, interface, &dst);
			if (ret < 0)
				goto out_close;
		} while (dst.bState != DFU_STATE_dfuDNLOAD_IDLE ||
			 dst.bStatus != DFU_STATUS_OK);
		putchar('#');
		usleep(5000);
		fflush(stdout);
	}

	/* send one zero sized download request to signalize end */
	ret = dfu_download(usb_handle, interface, 0, NULL);
	if (ret >= 0)
		ret = bytes_sent;
	
	printf("] finished!\n");

	/* Transition to MANIFEST_SYNC state */
	ret = dfu_get_status(usb_handle, interface, &dst);
	if (ret < 0) {
		fprintf(stderr, "unable to transition to MANIFEST state\n");
		goto out_close;
	}
		
	printf("Resetting USB...\n");
	sleep(1);
	if (usb_reset(usb_handle) < 0) {
		fprintf(stderr, "error resetting after download: %s\n", 
			usb_strerror());
	}
	printf("Done!\n");
out_close:
	close(fd);
out_free:
	free(buf);

	return ret;
}

void sam7dfu_init()
{
    dfu_debug( debug );
    dfu_init( 5000 );
}


