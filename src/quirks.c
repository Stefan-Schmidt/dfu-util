/*  Simple quirk system for dfu-util
 *  Copyright 2010 Tormod Volden
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

#include "quirks.h"

int quirks = 0;

void set_quirks(unsigned long vendor, unsigned long product)
{
	/* Device returns bogus bwPollTimeout values */
	if (vendor == VENDOR_OPENMOKO ||
	    vendor == VENDOR_FIC ||
	    vendor == VENDOR_VOTI)
		quirks |= QUIRK_POLLTIMEOUT;
}
