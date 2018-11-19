/* Copyright (C) Uppsala University
 *
 * This file is distributed under the terms of the GNU general Public
 * License (GPL), see the file LICENSE
 *
 * Author: Björn Wiberg <bjorn.wiberg@home.se>
 */

/*
  This code was partially borrowed from Per Kristian Hove
  <Per.Hove@math.ntnu.no>, who originally posted it to the rdesktop
  mailing list at rdesktop.org.

  It solves the problem of non-existent <endian.h> on some systems by
  generating it.

  (Compile, run, and redirect the output to endian.h.)
*/

#include <stdio.h>

int litend(void)
{
	int i = 0;
	((char *)(&i))[0] = 1;
	return (i == 1);
}

int bigend(void)
{
	return !litend();
}

int main(int argc, char **argv)
{
	printf("#ifndef _DSR_ENDIAN_H\n");
	printf("#define _DSR_ENDIAN_H\n");
	printf("#define __%s_ENDIAN_BITFIELD 1234\n",
	       litend()? "LITTLE" : "BIG");
	printf("#endif\n");
	return 0;
}
