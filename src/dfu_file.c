/*
 * Checks for and parses a DFU suffix
 *
 * (C) 2011 Tormod Volden <debian.tormod@gmail.com>
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
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>

#include "dfu_file.h"

/* reads the fd and name member, fills in all others
   returns 0 if no DFU suffix
   returns positive if valid DFU suffix
   returns negative on file read error */
int parse_dfu_suffix(struct dfu_file *file)
{
	int ret;
	struct stat st;
	/* supported suffices are at least 16 bytes */
	unsigned char dfusuffix[16];

	file->size = 0;
	/* default values, if no valid suffix is found */
	file->dwCRC = 0;
	file->suffixlen = 0;
	file->bcdDFU = 0;
	file->idVendor = 0xffff; /* wildcard value */
	file->idProduct = 0xffff; /* wildcard value */
	file->bcdDevice = 0xffff; /* wildcard value */

	ret = fstat(file->fd, &st);
	if (ret < 0) {
		perror(file->name);
		return ret;
	}

	file->size = st.st_size;
	if (file->size < sizeof(dfusuffix)) {
		fprintf(stderr, "File too short for DFU suffix\n");
		return 0;
	}

	ret = lseek(file->fd, -sizeof(dfusuffix), SEEK_END);
	if (ret < 0) {
		fprintf(stderr, "Could not seek to DFU suffix\n");
		perror(file->name);
		goto rewind;
	}

	ret = read(file->fd, dfusuffix, sizeof(dfusuffix));
	if (ret < 0) {
		fprintf(stderr, "Could not read DFU suffix\n");
		perror(file->name);
		goto rewind;
	} else if (ret < sizeof(dfusuffix)) {
		fprintf(stderr, "Could not read whole DFU suffix\n");
		ret = -EIO;
		goto rewind;
	}

	if (dfusuffix[10] != 'D' ||
	    dfusuffix[9]  != 'F' ||
	    dfusuffix[8]  != 'U') {
		fprintf(stderr, "No valid DFU suffix signature\n");
		ret = 0;
		goto rewind;
	}

	file->dwCRC = (dfusuffix[15] << 24) +
		      (dfusuffix[14] << 16) +
		      (dfusuffix[13] << 8) +
		       dfusuffix[12];

	file->bcdDFU = (dfusuffix[7] << 8) + dfusuffix[6];
	printf("Dfu suffix version %x\n", file->bcdDFU);

	file->suffixlen = dfusuffix[11];
	if (file->suffixlen < sizeof(dfusuffix)) {
		fprintf(stderr, "Unsupported DFU suffix length %i\n",
			file->suffixlen);
		ret = 0;
		goto rewind;
	}

	file->idVendor  = (dfusuffix[5] << 8) + dfusuffix[4];
	file->idProduct = (dfusuffix[3] << 8) + dfusuffix[2];
	file->bcdDevice = (dfusuffix[1] << 8) + dfusuffix[0];

rewind:
	lseek(file->fd, 0, SEEK_SET);
	return ret;
}

