/*
 * dfu-util
 *
 * (C) 2007-2008 by OpenMoko, Inc.
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

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <getopt.h>
#include <libusb.h>
#include <errno.h>

#include "dfu.h"
#include "usb_dfu.h"
#include "dfu_load.h"
#include "quirks.h"
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_USBPATH_H
#include <usbpath.h>
#endif

/* define a portable function for reading a 16bit little-endian word */
unsigned short get_int16_le(const void *p)
{
    const unsigned char *cp = p;

    return ( cp[0] ) | ( ((unsigned short)cp[1]) << 8 );
}

int debug;
static int verbose = 0;

#define DFU_IFF_DFU		0x0001	/* DFU Mode, (not Runtime) */
#define DFU_IFF_VENDOR		0x0100
#define DFU_IFF_PRODUCT		0x0200
#define DFU_IFF_CONFIG		0x0400
#define DFU_IFF_IFACE		0x0800
#define DFU_IFF_ALT		0x1000
#define DFU_IFF_DEVNUM		0x2000
#define DFU_IFF_PATH		0x4000

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
	int bus;
	u_int8_t devnum;
	const char *path;
	unsigned int flags;
	libusb_device *dev;

	libusb_device_handle *dev_handle;
};

static int _get_first_cb(struct dfu_if *dif, void *v)
{
	struct dfu_if *v_dif = v;

	memcpy(v_dif, dif, sizeof(*v_dif)-sizeof(libusb_device_handle *));

	/* return a value that makes find_dfu_if return immediately */
	return 1;
}

/* Find a DFU interface (and altsetting) in a given device */
static int find_dfu_if(libusb_device *dev, int (*handler)(struct dfu_if *, void *), void *v)
{
	struct libusb_device_descriptor desc;
	struct libusb_config_descriptor *cfg;
	const struct libusb_interface_descriptor *intf;
	const struct libusb_interface *uif;
	struct dfu_if _dif, *dfu_if = &_dif;
	int cfg_idx, intf_idx, alt_idx;
	int rc;

	libusb_get_device_descriptor(dev, &desc);

	memset(dfu_if, 0, sizeof(*dfu_if));

	for (cfg_idx = 0; cfg_idx < desc.bNumConfigurations;
	     cfg_idx++) {
		libusb_get_config_descriptor(dev, cfg_idx, &cfg);
		/* in some cases, noticably FreeBSD if uid != 0,
		 * the configuration descriptors are empty */
		if (!cfg)
			return 0;
		for (intf_idx = 0; intf_idx < cfg->bNumInterfaces;
		     intf_idx++) {
			uif = &cfg->interface[intf_idx];
			if (!uif)
				return 0;
			for (alt_idx = 0;
			     alt_idx < uif->num_altsetting; alt_idx++) {
				intf = &uif->altsetting[alt_idx];
				if (!intf)
					return 0;
				if (intf->bInterfaceClass == 0xfe &&
				    intf->bInterfaceSubClass == 1) {
					dfu_if->dev = dev;
					dfu_if->vendor =
						desc.idVendor;
					dfu_if->product =
						desc.idProduct;
					dfu_if->configuration = cfg_idx;
					dfu_if->interface =
						intf->bInterfaceNumber;
					dfu_if->altsetting =
						intf->bAlternateSetting;
					if (intf->bInterfaceProtocol == 2)
						dfu_if->flags |= DFU_IFF_DFU;
					else
						dfu_if->flags &= ~DFU_IFF_DFU;
					if (!handler)
						return 1;
					rc = handler(dfu_if, v);
					if (rc != 0)
						return rc;
				}
			}
		libusb_free_config_descriptor(cfg);
		}
	}

	return 0;
}

static int get_first_dfu_if(struct dfu_if *dif)
{
	return find_dfu_if(dif->dev, &_get_first_cb, (void *) dif);
}

#define MAX_STR_LEN 64

static int print_dfu_if(struct dfu_if *dfu_if, void *v)
{
	libusb_device *dev = dfu_if->dev;
	struct libusb_config_descriptor *cfg;
	int if_name_str_idx;
	unsigned char name[MAX_STR_LEN+1] = "UNDEFINED";

	libusb_get_config_descriptor(dev, dfu_if->configuration, &cfg);

	if_name_str_idx = cfg->interface[dfu_if->interface]
				.altsetting[dfu_if->altsetting].iInterface;
	if (if_name_str_idx) {
		if (!dfu_if->dev_handle)
			libusb_open(dfu_if->dev, &dfu_if->dev_handle);
		if (dfu_if->dev_handle)
			libusb_get_string_descriptor_ascii(dfu_if->dev_handle,
					      if_name_str_idx, name,
					      MAX_STR_LEN);
	}

	printf("Found %s: [0x%04x:0x%04x] devnum=%u, cfg=%u, intf=%u, "
	       "alt=%u, name=\"%s\"\n", 
	       dfu_if->flags & DFU_IFF_DFU ? "DFU" : "Runtime",
	       dfu_if->vendor, dfu_if->product, dfu_if->devnum,
		dfu_if->configuration, dfu_if->interface,
	       dfu_if->altsetting, name);

	libusb_free_config_descriptor(cfg);
	return 0;
}

static int alt_by_name(struct dfu_if *dfu_if, void *v)
{
	libusb_device *dev = dfu_if->dev;
	struct libusb_config_descriptor *cfg;
	int if_name_str_idx;
	unsigned char name[MAX_STR_LEN+1] = "UNDEFINED";

	libusb_get_config_descriptor(dev, dfu_if->configuration, &cfg);

	if_name_str_idx = cfg->interface[dfu_if->interface]
				.altsetting[dfu_if->altsetting].iInterface;
	if (!if_name_str_idx)
		return 0;
	if (!dfu_if->dev_handle)
		libusb_open(dfu_if->dev, &dfu_if->dev_handle);
	if (!dfu_if->dev_handle)
		return 0;
	if (libusb_get_string_descriptor_ascii(dfu_if->dev_handle, if_name_str_idx, name,
	     MAX_STR_LEN) < 0)
		return 0; /* should we return an error here ? */
	if (strcmp((char *)name, v))
		return 0;
	/*
	 * Return altsetting+1 so that we can use return value 0 to indicate
	 * "not found".
	 */
	return dfu_if->altsetting+1;
}

static int _count_cb(struct dfu_if *dif, void *v)
{
	int *count = v;

	(*count)++;

	return 0;
}

/* Count DFU interfaces within a single device */
static int count_dfu_interfaces(libusb_device *dev)
{
	int num_found = 0;

	find_dfu_if(dev, &_count_cb, (void *) &num_found);

	return num_found;
}


/* Iterate over all matching DFU capable devices within system */
static int iterate_dfu_devices(struct dfu_if *dif,
    int (*action)(struct libusb_device *dev, void *user), void *user)
{
	struct libusb_device_descriptor desc;
	struct libusb_device *dev;
	libusb_device **list;
	ssize_t num_devs, i;

	dev = NULL;
	num_devs = libusb_get_device_list(NULL, &list); // FIXME set context

	/* Walk the tree and find our device. */
	for (i = 0; i < num_devs; ++i) {
		uint8_t bnum = libusb_get_bus_number(list[i]);
		uint8_t dnum = libusb_get_device_address(list[i]);

		int retval;
		dev = list[i];
		libusb_get_device_descriptor(list[i], &desc);

		if (dif && (dif->flags &
		    (DFU_IFF_VENDOR|DFU_IFF_PRODUCT)) &&
		    (desc.idVendor != dif->vendor ||
		    desc.idProduct != dif->product))
			continue;
		if (dif && (dif->flags & DFU_IFF_DEVNUM) &&
		    (bnum != dif->bus || dnum != dif->devnum))
			continue;
		if (!count_dfu_interfaces(dev))
			continue;

		retval = action(dev, user);
		if (retval) {
			libusb_free_device_list(list, 0);
			return retval;
		}
	}
	libusb_free_device_list(list, 0);
	return 0;
}


static int found_dfu_device(struct libusb_device *dev, void *user)
{
	struct dfu_if *dif = user;

	dif->dev = dev;
	return 1;
}


/* Find the first DFU-capable device, save it in dfu_if->dev */
static int get_first_dfu_device(struct dfu_if *dif)
{
	return iterate_dfu_devices(dif, found_dfu_device, dif);
}


static int count_one_dfu_device(struct libusb_device *dev, void *user)
{
	int *num = user;

	(*num)++;
	return 0;
}


/* Count DFU capable devices within system */
static int count_dfu_devices(struct dfu_if *dif)
{
	int num_found = 0;

	iterate_dfu_devices(dif, count_one_dfu_device, &num_found);
	return num_found;
}


static int list_dfu_interfaces(void)
{
	libusb_device **list;
	libusb_device *dev;
	ssize_t num_devs, i;

	/* Walk the tree and find our device. */
	dev = NULL;
	num_devs = libusb_get_device_list(NULL, &list);

	for (i = 0; i < num_devs; ++i) {
		dev = list[i];
		find_dfu_if(dev, &print_dfu_if, NULL);
	}

	libusb_free_device_list(list, 1);
	return 0;
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


#ifdef HAVE_USBPATH_H

static int resolve_device_path(struct dfu_if *dif)
{
	int res;

	res = usb_path2devnum(dif->path);
	if (res < 0)
		return -EINVAL;
	if (!res)
		return 0;

	dif->bus = atoi(dif->path);
	dif->devnum = res;
	dif->flags |= DFU_IFF_DEVNUM;
	return res;
}

#else /* HAVE_USBPATH_H */

static int resolve_device_path(struct dfu_if *dif)
{
	fprintf(stderr,
	    "USB device paths are not supported by this dfu-util.\n");
	exit(1);
}

#endif /* !HAVE_USBPATH_H */

/* Look for descriptor in the configuration descriptor output */
static int usb_get_extra_descriptor(struct dfu_if *dfu_if, unsigned char type,
			unsigned char index, void *resbuf, int size)
{
	unsigned char *cbuf;
	int desclen, conflen, smallest;
	int ret;
	int config_index;
	int p = 0;
	int foundlen = 0;
	struct libusb_config_descriptor *config;

	libusb_get_configuration(dfu_if->dev_handle, &config_index);
	libusb_get_config_descriptor(dfu_if->dev, config_index, &config);

	conflen = config->wTotalLength;
	cbuf = malloc(conflen);
	ret = libusb_get_descriptor(dfu_if->dev_handle, LIBUSB_DT_CONFIG, index, cbuf, conflen);
	if (ret < conflen) {
		fprintf(stderr, "Warning: failed to retrieve complete"
			"configuration descriptor\n");
		conflen = ret;
	}
	while (p + 1 < conflen) {
		desclen = (int) cbuf[p];
		if (cbuf[p + 1] == type) {
			smallest = desclen < size ? desclen : size;
			memcpy(resbuf, &cbuf[p], smallest);
			foundlen = smallest;
			break;
		}
		p += desclen;
	}
	free(cbuf);
	if (foundlen > 1)
		return foundlen;

	libusb_free_config_descriptor(config);
	/* try to retrieve it through usb_get_descriptor directly */
	return libusb_get_descriptor(dfu_if->dev_handle, type, index, resbuf, size);
}

static void help(void)
{
	printf("Usage: dfu-util [options] ...\n"
		"  -h --help\t\t\tPrint this help message\n"
		"  -V --version\t\t\tPrint the version number\n"
		"  -l --list\t\t\tList the currently attached DFU capable USB devices\n"
		"  -d --device vendor:product\tSpecify Vendor/Product ID of DFU device\n"
		"  -p --path bus-port. ... .port\tSpecify path to DFU device\n"
		"  -c --cfg config_nr\t\tSpecify the Configuration of DFU device\n"
		"  -i --intf intf_nr\t\tSpecify the DFU Interface number\n"
		"  -a --alt alt\t\t\tSpecify the Altsetting of the DFU Interface\n"
		"\t\t\t\tby name or by number\n"
		"  -t --transfer-size\t\tSpecify the number of bytes per USB Transfer\n"
		"  -U --upload file\t\tRead firmware from device into <file>\n"
		"  -D --download file\t\tWrite firmware from <file> into device\n"
		"  -R --reset\t\t\tIssue USB Reset signalling once we're finished\n"
		);
}

static void print_version(void)
{
	printf("dfu-util version %s\n", VERSION);
}

static struct option opts[] = {
	{ "help", 0, 0, 'h' },
	{ "version", 0, 0, 'V' },
	{ "verbose", 0, 0, 'v' },
	{ "list", 0, 0, 'l' },
	{ "device", 1, 0, 'd' },
	{ "path", 1, 0, 'p' },
	{ "configuration", 1, 0, 'c' },
	{ "cfg", 1, 0, 'c' },
	{ "interface", 1, 0, 'i' },
	{ "intf", 1, 0, 'i' },
	{ "altsetting", 1, 0, 'a' },
	{ "alt", 1, 0, 'a' },
	{ "transfer-size", 1, 0, 't' },
	{ "upload", 1, 0, 'U' },
	{ "download", 1, 0, 'D' },
	{ "reset", 0, 0, 'R' },
};

enum mode {
	MODE_NONE,
	MODE_UPLOAD,
	MODE_DOWNLOAD,
};

int main(int argc, char **argv)
{
	struct usb_vendprod vendprod;
	struct dfu_if _rt_dif, _dif, *dif = &_dif;
	int num_devs;
	int num_ifs;
	unsigned int transfer_size = 0;
	unsigned int default_transfer_size = 1024;
	unsigned int host_page_size;
	enum mode mode = MODE_NONE;
	struct dfu_status status;
	struct usb_dfu_func_descriptor func_dfu;
	libusb_context *ctx;
	char *filename = NULL;
	char *alt_name = NULL; /* query alt name if non-NULL */
	char *end;
	int final_reset = 0;
	int ret;

	printf("dfu-util - (C) 2005-2008 by Weston Schmidt, Harald Welte and OpenMoko Inc.\n"
	       "This program is Free Software and has ABSOLUTELY NO WARRANTY\n\n");

	printf("dfu-util does currently only support DFU version 1.0\n\n");

	host_page_size = getpagesize();
	memset(dif, 0, sizeof(*dif));

	ret = libusb_init(&ctx);
	if (ret) {
		fprintf(stderr, "unable to initialize libusb: %i\n", ret);
		return EXIT_FAILURE;
	}

	while (1) {
		int c, option_index = 0;
		c = getopt_long(argc, argv, "hVvld:p:c:i:a:t:U:D:R", opts,
				&option_index);
		if (c == -1)
			break;

		switch (c) {
		case 'h':
			help();
			exit(0);
			break;
		case 'V':
			print_version();
			exit(0);
			break;
		case 'v':
			if (verbose) {
				libusb_set_debug(ctx, 255);
			}
			verbose = 1;
			break;
		case 'l':
			list_dfu_interfaces();
			exit(0);
			break;
		case 'd':
			/* Parse device */
			if (parse_vendprod(&vendprod, optarg) < 0) {
				fprintf(stderr, "unable to parse `%s' as a vendor:product\n", optarg);

				exit(2);
			}
			dif->vendor = vendprod.vendor;
			dif->product = vendprod.product;
			dif->flags |= (DFU_IFF_VENDOR | DFU_IFF_PRODUCT);
			break;
		case 'p':
			/* Parse device path */
			dif->path = optarg;
			dif->flags |= DFU_IFF_PATH;
			ret = resolve_device_path(dif);
			if (ret < 0) {
				fprintf(stderr, "unable to parse `%s'\n",
				    optarg);
				exit(2);
			}
			if (!ret) {
				fprintf(stderr, "cannot find `%s'\n", optarg);
				exit(1);
			}
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
			dif->altsetting = strtoul(optarg, &end, 0);
			if (*end)
				alt_name = optarg;
			dif->flags |= DFU_IFF_ALT;
			break;
		case 't':
			transfer_size = atoi(optarg);
			break;
		case 'U':
			mode = MODE_UPLOAD;
			filename = optarg;
			break;
		case 'D':
			mode = MODE_DOWNLOAD;
			filename = optarg;
			break;
		case 'R':
			final_reset = 1;
			break;
		default:
			help();
			exit(2);
		}
	}

	if (mode == MODE_NONE) {
		fprintf(stderr, "You need to specify one of -D or -U\n");
		help();
		exit(2);
	}

	if (!filename) {
		fprintf(stderr, "You need to specify a filename to -D or -U\n");
		help();
		exit(2);
	}

	dfu_init(5000);

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
	if (!get_first_dfu_device(dif))
		exit(3);

	/* We have exactly one device. Its libusb_device is now in dif->dev */

	printf("Opening DFU USB device... ");
	libusb_open(dif->dev, &dif->dev_handle);
	if (!dif->dev_handle) {
		fprintf(stderr, "Cannot open device \n");
		exit(1);
	}

	/* try to find first DFU interface of device */
	memcpy(&_rt_dif, dif, sizeof(_rt_dif));
	if (!get_first_dfu_if(&_rt_dif))
		exit(1);

	printf("ID %04x:%04x\n", _rt_dif.vendor, _rt_dif.product);

	/* find set of quirks for this device */
	set_quirks(_rt_dif.vendor, _rt_dif.product);

	if (!_rt_dif.flags & DFU_IFF_DFU) {
		/* In the 'first round' during runtime mode, there can only be one
		* DFU Interface descriptor according to the DFU Spec. */

		/* FIXME: check if the selected device really has only one */

		printf("Claiming USB DFU Runtime Interface...\n");
		if (libusb_claim_interface(_rt_dif.dev_handle, _rt_dif.interface) < 0) {
			fprintf(stderr, "Cannot claim interface %d\n",
				_rt_dif.interface);
			exit(1);
		}

		if (libusb_set_interface_alt_setting(_rt_dif.dev_handle, _rt_dif.interface, 0) < 0) {
			fprintf(stderr, "Cannot set alt interface\n");
			exit(1);
		}

		printf("Determining device status: ");
		if (dfu_get_status(_rt_dif.dev_handle, _rt_dif.interface, &status ) < 0) {
			fprintf(stderr, "error get_status\n");
			exit(1);
		}
		printf("state = %s, status = %d\n", 
		       dfu_state_to_string(status.bState), status.bStatus);
		if (!(quirks & QUIRK_POLLTIMEOUT))
			usleep(status.bwPollTimeout * 1000);

		switch (status.bState) {
		case DFU_STATE_appIDLE:
		case DFU_STATE_appDETACH:
			printf("Device really in Runtime Mode, send DFU "
			       "detach request...\n");
			if (dfu_detach(_rt_dif.dev_handle, 
				       _rt_dif.interface, 1000) < 0) {
				fprintf(stderr, "error detaching\n");
				exit(1);
				break;
			}
			printf("Resetting USB...\n");
			ret = libusb_reset_device(_rt_dif.dev_handle);
			if (ret < 0 && ret != -ENODEV)
				fprintf(stderr, "error resetting after detach\n");
			sleep(2);
			break;
		case DFU_STATE_dfuERROR:
			printf("dfuERROR, clearing status\n");
			if (dfu_clear_status(_rt_dif.dev_handle,
					     _rt_dif.interface) < 0) {
				fprintf(stderr, "error clear_status\n");
				exit(1);
				break;
			}
			break;
		default:
			fprintf(stderr, "WARNING: Runtime device already "
				"in DFU state ?!?\n");
			goto dfustate;
			break;
		}

		/* now we need to re-scan the bus and locate our device */
//		if (usb_find_devices() < 2)
//			printf("not at least 2 device changes found ?!?\n");

		if (dif->flags & DFU_IFF_PATH) {
			ret = resolve_device_path(dif);
			if (ret < 0) {
				fprintf(stderr,
				    "internal error: cannot re-parse `%s'\n",
				    dif->path);
				abort();
			}
			if (!ret) {
				fprintf(stderr,
				    "Can't resolve path after RESET?\n");
				exit(1);
			}
		}

		num_devs = count_dfu_devices(dif);
		if (num_devs == 0) {
			fprintf(stderr, "Lost device after RESET?\n");
			exit(1);
		} else if (num_devs > 1) {
			fprintf(stderr, "More than one DFU capable USB "
				"device found, you might try `--list' and "
				"then disconnect all but one device\n");
			exit(1);
		}
		if (!get_first_dfu_device(dif))
			exit(3);

		printf("Opening USB Device...\n");
		libusb_open(dif->dev, &dif->dev_handle);
		if (!dif->dev_handle) {
			fprintf(stderr, "Cannot open device\n");
			exit(1);
		}
	} else {
		/* we're already in DFU mode, so we can skip the detach/reset
		 * procedure */
	}

dfustate:
	if (alt_name) {
		int n;

		n = find_dfu_if(dif->dev, &alt_by_name, alt_name);
		if (!n) {
			fprintf(stderr, "No such Alternate Setting: \"%s\"\n",
			    alt_name);
			exit(1);
		}
		if (n < 0) {
			fprintf(stderr, "Error %d in name lookup\n", n);
			exit(1);
		}
		dif->altsetting = n-1;
	}

	print_dfu_if(dif, NULL);

	num_ifs = count_dfu_interfaces(dif->dev);
	if (num_ifs < 0) {
		fprintf(stderr, "No DFU Interface after RESET?!?\n");
		exit(1);
	} else if (num_ifs == 1) {
		if (!get_first_dfu_if(dif)) {
			fprintf(stderr, "Can't find the single available "
				"DFU IF\n");
			exit(1);
		}
	} else if (num_ifs > 1 && !(dif->flags & (DFU_IFF_IFACE|DFU_IFF_ALT))) {
		fprintf(stderr, "We have %u DFU Interfaces/Altsettings, "
			"you have to specify one via --intf / --alt options\n",
			num_ifs);
		exit(1);
	}

#if 0
	printf("Setting Configuration %u...\n", dif->configuration);
	if (usb_set_configuration(dif->dev_handle, dif->configuration) < 0) {
		fprintf(stderr, "Cannot set configuration\n");
		exit(1);
	}
#endif
	printf("Claiming USB DFU Interface...\n");
	if (libusb_claim_interface(dif->dev_handle, dif->interface) < 0) {
		fprintf(stderr, "Cannot claim interface\n");
		exit(1);
	}

	printf("Setting Alternate Setting #%d ...\n", dif->altsetting);
	if (libusb_set_interface_alt_setting(dif->dev_handle, dif->interface, dif->altsetting) < 0) {
		fprintf(stderr, "Cannot set alternate interface\n");
		exit(1);
	}

status_again:
	printf("Determining device status: ");
	if (dfu_get_status(dif->dev_handle, dif->interface, &status ) < 0) {
		fprintf(stderr, "error get_status\n");
		exit(1);
	}
	printf("state = %s, status = %d\n",
	       dfu_state_to_string(status.bState), status.bStatus);
	if (!(quirks & QUIRK_POLLTIMEOUT))
		usleep(status.bwPollTimeout * 1000);

	switch (status.bState) {
	case DFU_STATE_appIDLE:
	case DFU_STATE_appDETACH:
		fprintf(stderr, "Device still in Runtime Mode!\n");
		exit(1);
		break;
	case DFU_STATE_dfuERROR:
		printf("dfuERROR, clearing status\n");
		if (dfu_clear_status(dif->dev_handle, dif->interface) < 0) {
			fprintf(stderr, "error clear_status\n");
			exit(1);
		}
		goto status_again;
		break;
	case DFU_STATE_dfuDNLOAD_IDLE:
	case DFU_STATE_dfuUPLOAD_IDLE:
		printf("aborting previous incomplete transfer\n");
		if (dfu_abort(dif->dev_handle, dif->interface) < 0) {
			fprintf(stderr, "can't send DFU_ABORT\n");
			exit(1);
		}
		goto status_again;
		break;
	case DFU_STATE_dfuIDLE:
		printf("dfuIDLE, continuing\n");
		break;
	}

	if (!transfer_size) {
		/* Obtain DFU functional descriptor */
		ret = usb_get_extra_descriptor(dif, USB_DT_DFU,
				dif->interface, &func_dfu, sizeof(func_dfu));
		if (ret < 0) {
			fprintf(stderr, "Error obtaining DFU functional "
				"descriptor\n");
		} else {
			transfer_size = get_int16_le(&func_dfu.wTransferSize);
			printf("Device returned transfer size %i\n",
				transfer_size);
		}
	}
	/* if returned zero or not detected (and not user specified) */
	if (!transfer_size) {
		transfer_size = default_transfer_size;
		printf("Warning: Trying default transfer size %i\n",
			transfer_size);
	}
	/* limitation of Linux usbdevio */
	if (transfer_size > host_page_size) {
		transfer_size = host_page_size;
		printf("Limited transfer size to %i\n", transfer_size);
	}
	/* DFU specification */
	struct libusb_device_descriptor desc;
	libusb_get_device_descriptor(dif->dev, &desc);
	if (transfer_size < desc.bMaxPacketSize0) {
		transfer_size = desc.bMaxPacketSize0;
		printf("Adjusted transfer size to %i\n", transfer_size);
	}

	if (DFU_STATUS_OK != status.bStatus ) {
		printf("WARNING: DFU Status: '%s'\n",
			dfu_status_to_string(status.bStatus));
		/* Clear our status & try again. */
		dfu_clear_status(dif->dev_handle, dif->interface);
		dfu_get_status(dif->dev_handle, dif->interface, &status);

		if (DFU_STATUS_OK != status.bStatus) {
			fprintf(stderr, "Error: %d\n", status.bStatus);
			exit(1);
		}
		if (!(quirks & QUIRK_POLLTIMEOUT))
			usleep(status.bwPollTimeout * 1000);
        }

	switch (mode) {
	case MODE_UPLOAD:
		if (dfuload_do_upload(dif->dev_handle, dif->interface,
				  transfer_size, filename) < 0)
			exit(1);
		break;
	case MODE_DOWNLOAD:
		if (dfuload_do_dnload(dif->dev_handle, dif->interface,
				  transfer_size, filename) < 0)
			exit(1);
		break;
	default:
		fprintf(stderr, "Unsupported mode: %u\n", mode);
		exit(1);
	}

	if (final_reset) {
		if (dfu_detach(dif->dev_handle, dif->interface, 1000) < 0) {
			fprintf(stderr, "can't detach\n");
		}
		printf("Resetting USB to switch back to runtime mode\n");
		ret = libusb_reset_device(dif->dev_handle);
		if (ret < 0 && ret != -ENODEV) {
			fprintf(stderr, "error resetting after download\n");
		}
	}

	exit(0);
}

