/*
 * dfu-util
 *
 * (C) 2007 by OpenMoko, Inc.
 * Written by Harald Welte <laforge@openmoko.org>
 *
 * Based on existing code of dfu-programmer-0.4
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <stdio.h>
#include <string.h>
#include <getopt.h>
#include <usb.h>
#include <errno.h>

#include "dfu.h"
#include "usb_dfu.h"
//#include "config.h"


int debug;

static int verbose = 0;

#define DFU_INTF_FLAG_DFU	0x0001	/* DFU Mode, (not Runtime) */
#define DFU_IFF_VENDOR		0x0100
#define DFU_IFF_PRODUCT		0x0200
#define DFU_IFF_CONFIG		0x0400
#define DFU_IFF_IFACE		0x0800
#define DFU_IFF_ALT		0x1000

struct usb_vendprod {
	u_int16_t vendor;
	u_int16_t product;
};

struct dfu_if {
	u_int16_t vendor;
	u_int16_t product;
	u_int8_t configuration;
	u_int8_t interface;
	u_int8_t altsetting;
	u_int8_t flags;

	struct usb_device *dev;
	struct usb_dev_handle *dev_handle;
};

static int _assign_first_cb(struct dfu_if *dif, void *v)
{
	struct dfu_if **v_dif = v;
	*v_dif = dif;

	/* return a value that makes find_dfu_if return immediately */
	return 1;
}

/* Find a DFU interface (and altsetting) in a given device */
static int find_dfu_if(struct usb_device *dev, int (*handler)(struct dfu_if *, void *), void *v)
{
	struct usb_config_descriptor *cfg;
	struct usb_interface_descriptor *intf;
	struct usb_interface *uif;
	struct dfu_if _dif, *dfu_if = &_dif;
	int cfg_idx, intf_idx, alt_idx;
	int rc;
	
	for (cfg_idx = 0; cfg_idx < dev->descriptor.bNumConfigurations;
	     cfg_idx++) {
		cfg = &dev->config[cfg_idx];
		for (intf_idx = 0; intf_idx < cfg->bNumInterfaces;
		     intf_idx++) {
			uif = &cfg->interface[intf_idx];
			for (alt_idx = 0;
			     alt_idx < uif->num_altsetting; alt_idx++) {
				intf = &uif->altsetting[alt_idx];
				if (intf->bInterfaceClass == 0xfe &&
				    intf->bInterfaceSubClass == 1) {
					dfu_if->dev = dev;
					dfu_if->vendor =
						dev->descriptor.idVendor;
					dfu_if->product =
						dev->descriptor.idProduct;
					dfu_if->configuration = cfg_idx;
					dfu_if->interface = intf_idx;
					dfu_if->altsetting = 
						intf->bAlternateSetting;
					if (intf->bInterfaceProtocol == 2)
						dfu_if->flags = 
							DFU_INTF_FLAG_DFU;
					else
						dfu_if->flags = 0;
					if (!handler)
						return 1;
					rc = handler(dfu_if, v);
					if (rc != 0)
						return rc;
				}
			}
		}
	}

	return 0;
}

static int print_dfu_if(struct dfu_if *dfu_if, void *v)
{
	struct usb_device *dev = dfu_if->dev;

	printf("Found DFU %s: [0x%04x:0x%04x] devnum=%u, cfg=%u, intf=%u, alt=%u\n", 
	       dfu_if->flags & DFU_INTF_FLAG_DFU ? "DFU" : "Runtime",
	       dev->descriptor.idVendor, dev->descriptor.idProduct,
	       dev->devnum, dfu_if->configuration, dfu_if->interface,
	       dfu_if->altsetting);

	return 0;
}

static int _count_cb(struct dfu_if *dif, void *v)
{
	int *count = v;

	(*count)++;

	return 0;
}

/* Count DFU interfaces within a single device */
static int count_dfu_interfaces(struct usb_device *dev)
{
	int num_found = 0;

	find_dfu_if(dev, &_count_cb, (void *) &num_found);

	return num_found;
}

/* Count DFU capable devices within system */
static int count_dfu_devices(struct dfu_if *dif)
{
	struct usb_bus *usb_bus;
	struct usb_device *dev;
	int num_found = 0;

	/* Walk the tree and find our device. */
	for (usb_bus = usb_get_busses(); NULL != usb_bus;
	     usb_bus = usb_bus->next) {
		for (dev = usb_bus->devices; NULL != dev; dev = dev->next) {
			if (!dif || 
			    (dif->flags & (DFU_IFF_VENDOR|DFU_IFF_PRODUCT) == 0) ||
			    (dev->descriptor.idVendor == dif->vendor &&
			     dev->descriptor.idProduct == dif->product)) {
				dif->dev = dev;
				if (count_dfu_interfaces(dev) >= 1)
					num_found++;
			}
		}
	}
	return num_found;
}

static int list_dfu_interfaces(void)
{
	struct usb_bus *usb_bus;
	struct usb_device *dev;

	/* Walk the tree and find our device. */
	for (usb_bus = usb_get_busses(); NULL != usb_bus;
	     usb_bus = usb_bus->next ) {
		for (dev = usb_bus->devices; NULL != dev; dev = dev->next) {
			find_dfu_if(dev, &print_dfu_if, NULL);
		}
	}
}

static int parse_vendprod(struct usb_vendprod *vp, const char *str)
{
	unsigned long vend, prod;
	const char *colon;

	colon = strchr(str, ':');
	if (!colon || strlen(colon) < 2)
		return -EINVAL;

	vend = strtoul(str, NULL, 16);
	prod = strtoul(colon+1, NULL, 16);

	if (vend > 0xffff || prod > 0xffff)
		return -EINVAL;

	vp->vendor = vend;
	vp->product = prod;

	return 0;
}

static void help(void)
{
	printf("Usage: dfu-util [options] ...\n"
		"  -h --help\t\t\tPrint this help message\n"
		"  -V --version\t\tPrint the version number\n"
		"  -l --list\t\tList the currently attached DFU capable USB devices\n"
		"  -d --device vendor:product\tSpecify Vendor/Product ID of DFU device\n"
		"  -c --cfg config_nr\t\tSpecify the Configuration of DFU device\n"
		"  -i --intf intf_nr\t\tSpecify the DFU Interface number\n"
		"  -a --alt alt_nr\t\tSpecify the Altseting of the DFU Interface\n"
		"  -U --upload file\tRead firmware from device into <file>\n"
		"  -D --download file\tWrite firmware from <file> into device\n"
		);
}

static struct option opts[] = {
	{ "help", 0, 0, 'h' },
	{ "version", 0, 0, 'V' },
	{ "verbose", 0, 0, 'v' },
	{ "list", 0, 0, 'l' },
	{ "device", 1, 0, 'd' },
	{ "configuration", 1, 0, 'c' },
	{ "cfg", 1, 0, 'c' },
	{ "interface", 1, 0, 'i' },
	{ "intf", 1, 0, 'i' },
	{ "altsetting", 1, 0, 'a' },
	{ "alt", 1, 0, 'a' },
	{ "upload", 1, 0, 'U' },
	{ "download", 1, 0, 'D' },
};

enum mode {
	MODE_NONE,
	MODE_UPLOAD,
	MODE_DOWNLOAD,
};

int main(int argc, char **argv)
{
	struct usb_vendprod vendprod;
	struct dfu_if _dif, *dif = &_dif;
	int num_devs;
	int num_ifs;
	enum mode mode;
	struct dfu_status status;
	struct usb_dfu_func_descriptor func_dfu;
	
	printf("dfu-util - (C) 2007 by OpenMoko Inc.\n"
	       "This program is Free Software and has ABSOLUTELY NO WARRANTY\n\n");

	memset(dif, 0, sizeof(*dif));

	usb_init();
	usb_find_busses();
	usb_find_devices();

	while (1) {
		int c, option_index = 0;
		c = getopt_long(argc, argv, "hVvld:c:i:a:UD", opts, &option_index);
		if (c == -1)
			break;

		switch (c) {
		case 'h':
			help();
			exit(0);
			break;
		case 'V':
			/* FIXME */
			break;
		case 'v':
			verbose = 1;
			break;
		case 'l':
			list_dfu_interfaces();
			exit(0);
			break;
		case 'd':
			/* Parse device */
			if (parse_vendprod(&vendprod, optarg) < 0) {
				fprintf(stderr, "unable to parse `%s'\n", optarg);
				exit(2);
			}
			dif->vendor = vendprod.vendor;
			dif->product = vendprod.product;
			dif->flags |= (DFU_IFF_VENDOR | DFU_IFF_PRODUCT);
			break;
		case 'c':
			/* Configuration */
			dif->configuration = atoi(optarg);
			dif->flags |= DFU_IFF_CONFIG;
			break;
		case 'i':
			/* Interface */
			dif->interface = atoi(optarg);
			dif->flags |= DFU_IFF_IFACE;
			break;
		case 'a':
			/* Interface Alternate Setting */
			dif->altsetting = atoi(optarg);
			dif->flags |= DFU_IFF_ALT;
			break;
		case 'U':
			mode = MODE_UPLOAD;
			break;
		case 'D':
			mode = MODE_DOWNLOAD;
			break;
		default:
			help();
			exit(2);
		}
	}

	num_devs = count_dfu_devices(dif);
	if (num_devs == 0) {
		fprintf(stderr, "No DFU capable USB device found\n");
		exit(1);
	} else if (num_devs > 1) {
		/* We cannot safely support more than one DFU capable device
		 * with same vendor/product ID, since during DFU we need to do
		 * a USB bus reset, after which the target device will get a
		 * new address */
		fprintf(stderr, "More than one DFU capable USB device found, "
		       "you might try `--list' and then disconnect all but one "
		       "device\n");
		exit(3);
	}
	/* We have exactly one device. It's usb_device is now in dif->dev */

	if (mode == MODE_NONE) {
		fprintf(stderr, "You need to specify one of -D or -U\n");
		help();
		exit(2);
	}

	/* FIXME: check if there's only one interface/altsetting and use it */
	if (!dif->flags & (DFU_IFF_CONFIG|DFU_IFF_ALT) != DFU_IFF_CONFIG|DFU_IFF_ALT) {
		fprintf(stderr, "You have to specify --cfg and --alt!\n");
		help();
		exit(1);
	}

retry:
	/* FIXME: need to re-scan and re-select same device after reset*/
	if (find_dfu_if(dif->dev, NULL, NULL) != 1) {
		fprintf(stderr, "Can't find device anymore !?!\n");
		exit(1);
	}

	printf("Opening USB Device...\n");
	dif->dev_handle = usb_open(dif->dev);
	if (!dif->dev_handle) {
		fprintf(stderr, "Cannot open device: %s\n", usb_strerror());
		exit(1);
	}

	printf("Setting Configuration...\n");
	if (usb_set_configuration(dif->dev_handle, dif->configuration) < 0) {
		fprintf(stderr, "Cannot set configuration: %s\n", usb_strerror());
		exit(1);
	}

	printf("Claiming USB Interface...\n");
	if (usb_claim_interface(dif->dev_handle, dif->interface) < 0) {
		fprintf(stderr, "Cannot claim interface: %s\n", usb_strerror());
		exit(1);
	}

	printf("Setting Alternate Setting ...\n");
	if (usb_set_altinterface(dif->dev_handle, dif->altsetting) < 0) {
		fprintf(stderr, "Cannot set alternate interface: %s\n",
			usb_strerror());
		exit(1);
	}

	printf("Determining device status: ");
	if (dfu_get_status(dif->dev_handle, dif->interface, &status ) < 0) {
		fprintf(stderr, "error get_status: %s\n", usb_strerror());
		exit(1);
	}
	printf("state = %d, status = %d\n", status.bState, status.bStatus);

	switch (status.bState) {
	case STATE_APP_IDLE:
		printf("Device in Runtime Mode, send DFU detach request...\n");
		if (dfu_detach(dif->dev_handle, dif->interface, 1000) < 0) {
			fprintf(stderr, "error detaching: %s\n", usb_strerror());
			exit(1);
			break;
		}
		printf("Resetting USB...\n");
		if (usb_reset(dif->dev_handle) < 0) {
			fprintf(stderr, "error resetting after detach: %s\n", usb_strerror());
		}
		sleep(2);
		goto retry;
		break;
	case STATE_DFU_ERROR:
		printf("dfuERROR, clearing status\n");
		if (dfu_clear_status(dif->dev_handle, dif->interface) < 0) {
			fprintf(stderr, "error clear_status: %s\n", usb_strerror());
			exit(1);
			break;
		}
		goto retry;
		break;
	case STATE_DFU_IDLE:
		printf("dfuIDLE, continuing\n");
		break;
	}

	{
		int ret;
		
		ret = usb_get_descriptor(dif->dev_handle, 0x21, dif->interface,
					 &func_dfu, sizeof(func_dfu));
		if (ret < 0) {
			fprintf(stderr, "error clear_status: %s\n",
				usb_strerror());
			exit(1);
		}

		printf("wTransferSize = 0x%04x\n", func_dfu.wTransferSize);
			
	}
	
	if (DFU_STATUS_OK != status.bStatus ) {
		/* Clear our status & try again. */
		dfu_clear_status(dif->dev_handle, dif->interface);
		dfu_get_status(dif->dev_handle, dif->interface, &status);

		if (DFU_STATUS_OK != status.bStatus) {
			fprintf(stderr, "Error: %d\n", status.bStatus);
			exit(1);
		}
        }
#if 0
	sam7dfu_do_upload(usb_handle, interface, func_dfu.wTransferSize, 
			  "/tmp/dfu_upload.img");
	//sam7dfu_do_dnload(usb_handle, interface, 252,
	sam7dfu_do_dnload(usb_handle, interface, func_dfu.wTransferSize, 
			  "/tmp/dfu_upload.img");
#endif

	exit(0);
}

