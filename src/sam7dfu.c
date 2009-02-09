/* This is supposed to be a "real" DFU implementation, just as specified in the
 * USB DFU 1.0 Spec.  Not overloaded like the Atmel one...
 *
 * (C) 2007-2008 by Harald Welte <laforge@gnumonks.org>
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <usb.h>

#include "config.h"
#include "dfu.h"
#include "usb_dfu.h"

/* ugly hack for Win32 */
#ifndef O_BINARY
#define O_BINARY 0
#endif

int sam7dfu_do_upload(struct usb_dev_handle *usb_handle, int interface, 
		      int xfer_size, const char *fname)
{
	int ret, fd, total_bytes = 0;
	char *buf = malloc(xfer_size);

	if (!buf)
		return -ENOMEM;

	fd = creat(fname, 0644);
	if (fd < 0) {
		perror(fname);
		ret = fd;
		goto out_free;
	}
	
	printf("bytes_per_hash=%u\n", xfer_size);
	printf("Starting upload: [");
	fflush(stdout);

	while (1) {
		int rc, write_rc;
		rc = dfu_upload(usb_handle, interface, xfer_size, buf);
		if (rc < 0) {
			ret = rc;
			goto out_close;
		}
		write_rc = write(fd, buf, rc);
		if (write_rc < rc) {
			fprintf(stderr, "Short file write: %s\n",
				strerror(errno));
			ret = total_bytes;
			goto out_close;
		}
		total_bytes += rc;
		if (rc < xfer_size) {
			/* last block, return */
			ret = total_bytes;
			goto out_close;
		}
		putchar('#');
		fflush(stdout);
	}
	ret = 0;

	printf("] finished!\n");
	fflush(stdout);

out_close:
	close(fd);
out_free:
	free(buf);
	
	return ret;
}

#define PROGRESS_BAR_WIDTH 50

int sam7dfu_do_dnload(struct usb_dev_handle *usb_handle, int interface,
		      int xfer_size, const char *fname)
{
	int ret, fd, bytes_sent = 0;
	unsigned int bytes_per_hash, hashes = 0;
	char *buf = malloc(xfer_size);
	struct stat st;
	struct dfu_status dst;

	if (!buf)
		return -ENOMEM;

	fd = open(fname, O_RDONLY|O_BINARY);
	if (fd < 0) {
		perror(fname);
		ret = fd;
		goto out_free;
	}
	
	ret = fstat(fd, &st);
	if (ret < 0) {
		perror(fname);
		goto out_close;
		perror(fname);
	}

	if (st.st_size <= 0 /* + DFU_HDR */) {
		fprintf(stderr, "File seems a bit too small...\n");
		ret = -EINVAL;
		goto out_close;	
	}

	bytes_per_hash = st.st_size / PROGRESS_BAR_WIDTH;
	if (bytes_per_hash == 0)
		bytes_per_hash = 1;
	printf("bytes_per_hash=%u\n", bytes_per_hash);
#if 0
	read(fd, DFU_HDR);
#endif
	printf("Starting download: [");
	fflush(stdout);
	while (bytes_sent < st.st_size /* - DFU_HDR */) {
		int hashes_todo;

		ret = read(fd, buf, xfer_size);
		if (ret < 0) {
			perror(fname);
			goto out_close;
		}
		ret = dfu_download(usb_handle, interface, ret, ret ? buf : NULL);
		if (ret < 0) {
			fprintf(stderr, "Error during download\n");
			goto out_close;
		}
		bytes_sent += ret;

		do {
			ret = dfu_get_status(usb_handle, interface, &dst);
			if (ret < 0) {
				fprintf(stderr, "Error during download get_status\n");
				goto out_close;
			}
			usleep(5000);
		} while (dst.bState != DFU_STATE_dfuDNLOAD_IDLE);
		if (dst.bStatus != DFU_STATUS_OK) {
			printf(" failed!\n");
			printf("state(%u) = %s, status(%u) = %s\n", dst.bState,
				dfu_state_to_string(dst.bState), dst.bStatus,
				dfu_status_to_string(dst.bStatus));
			ret = -1;
			goto out_close;
		}

		hashes_todo = (bytes_sent / bytes_per_hash) - hashes;
		hashes += hashes_todo;
		while (hashes_todo--)
			putchar('#');
		fflush(stdout);
	}

	/* send one zero sized download request to signalize end */
	ret = dfu_download(usb_handle, interface, 0, NULL);
	if (ret >= 0)
		ret = bytes_sent;
	
	printf("] finished!\n");
	fflush(stdout);

get_status:
	/* Transition to MANIFEST_SYNC state */
	ret = dfu_get_status(usb_handle, interface, &dst);
	if (ret < 0) {
		fprintf(stderr, "unable to read DFU status\n");
		goto out_close;
	}
	printf("state(%u) = %s, status(%u) = %s\n", dst.bState, 
		dfu_state_to_string(dst.bState), dst.bStatus,
		dfu_status_to_string(dst.bStatus));

	/* FIXME: deal correctly with ManifestationTolerant=0 / WillDetach bits */
	switch (dst.bState) {
	case DFU_STATE_dfuMANIFEST_SYNC:
	case DFU_STATE_dfuMANIFEST:
		/* some devices (e.g. TAS1020b) need some time before we
		 * can obtain the status */
		sleep(1);
		goto get_status;
		break;
	case DFU_STATE_dfuIDLE:
		break;
	}
#if 0
	printf("Resetting USB...\n");
	if (usb_reset(usb_handle) < 0) {
		fprintf(stderr, "error resetting after download: %s\n", 
			usb_strerror());
	}
#endif
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


