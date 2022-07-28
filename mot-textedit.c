//
// mot-textedit.c <z64.me>
//
// a simple text editor specifically for the
// ocarina of time romhack 'master of time'
//
// read more about oot's text format below
// https://wiki.cloudmodding.com/oot/Text_Format
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <assert.h>

// hard-coded offsets are fine
#define TABLE_START  0xB849EC  // where text table resides
#define TABLE_LENGTH 0x4388    // length of text table
#define DATA_ENTRY   0x7590    // dmadata entry of strings file
#define STRINGS_MAX  0x39000   // max allowable size of strings file
                               // (if exceeded, it will overflow into the next file)

//
//
// crc copy-pasta
//
//
/* snesrc - SNES Recompiler
 *
 * Mar 23, 2010: addition by spinout to actually fix CRC if it is incorrect
 *
 * Copyright notice for this file:
 *  Copyright (C) 2005 Parasyte
 *
 * Based on uCON64's N64 checksum algorithm by Andreas Sterbenz
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

#include <assert.h>

#define ROL(i, b) (((i) << (b)) | ((i) >> (32 - (b))))
#define BYTES2LONG(b) ( (b)[0] << 24 | \
                        (b)[1] << 16 | \
                        (b)[2] <<  8 | \
                        (b)[3] )

#define N64_HEADER_SIZE  0x40
#define N64_BC_SIZE      (0x1000 - N64_HEADER_SIZE)

#define N64_CRC1         0x10
#define N64_CRC2         0x14

#define CHECKSUM_START   0x00001000
#define CHECKSUM_LENGTH  0x00100000
#define CHECKSUM_CIC6102 0xF8CA4DDC
#define CHECKSUM_CIC6103 0xA3886759
#define CHECKSUM_CIC6105 0xDF26F436
#define CHECKSUM_CIC6106 0x1FEA617A


static void gen_table(unsigned int crc_table[256])
{
	unsigned int crc, poly;
	int	i, j;

	poly = 0xEDB88320;
	for (i = 0; i < 256; i++) {
		crc = i;
		for (j = 8; j > 0; j--) {
			if (crc & 1) crc = (crc >> 1) ^ poly;
			else crc >>= 1;
		}
		crc_table[i] = crc;
	}
}


static unsigned int crc32(
	unsigned int crc_table[256]
	, unsigned char *data
	, int len
)
{
	unsigned int crc = ~0;
	int i;

	for (i = 0; i < len; i++) {
		crc = (crc >> 8) ^ crc_table[(crc ^ data[i]) & 0xFF];
	}

	return ~crc;
}


static int N64GetCIC(unsigned int crc_table[256], unsigned char *data)
{
	switch (crc32(crc_table, &data[N64_HEADER_SIZE], N64_BC_SIZE)) {
		case 0x6170A4A1: return 6101;
		case 0x90BB6CB5: return 6102;
		case 0x0B050EE0: return 6103;
		case 0x98BC2C86: return 6105;
		case 0xACC8580A: return 6106;
	}

	return 0;
}


static int N64CalcCRC(
	unsigned int crc_table[256]
	, unsigned int *crc
	, unsigned char *data
)
{
	int bootcode, i;
	unsigned int seed;
	unsigned int t1, t2, t3;
	unsigned int t4, t5, t6;
	unsigned int r, d;

	switch ((bootcode = N64GetCIC(crc_table, data))) {
		case 6101:
		case 6102:
			seed = CHECKSUM_CIC6102;
			break;
		case 6103:
			seed = CHECKSUM_CIC6103;
			break;
		case 6105:
			seed = CHECKSUM_CIC6105;
			break;
		case 6106:
			seed = CHECKSUM_CIC6106;
			break;
		default:
			return 1;
	}

	t1 = t2 = t3 = t4 = t5 = t6 = seed;

	i = CHECKSUM_START;
	while (i < (CHECKSUM_START + CHECKSUM_LENGTH)) {
		d = BYTES2LONG(&data[i]);
		if ((t6 + d) < t6)
			t4++;
		t6 += d;
		t3 ^= d;
		r = ROL(d, (d & 0x1F));
		t5 += r;
		if (t2 > d)
			t2 ^= r;
		else
			t2 ^= t6 ^ d;

		if (bootcode == 6105)
			t1 += BYTES2LONG(&data[N64_HEADER_SIZE + 0x0710 + (i & 0xFF)]) ^ d;
		else
			t1 += t5 ^ d;

		i += 4;
	}
	if (bootcode == 6103) {
		crc[0] = (t6 ^ t4) + t3;
		crc[1] = (t5 ^ t2) + t1;
	}
	else if (bootcode == 6106) {
		crc[0] = (t6 * t4) + t3;
		crc[1] = (t5 * t2) + t1;
	}
	else {
		crc[0] = t6 ^ t4 ^ t3;
		crc[1] = t5 ^ t2 ^ t1;
	}

	return 0;
}


/* recalculate rom crc */
void n64crc(void *rom)
{
	unsigned int crc_table[256];
	unsigned char CRC1[4];
	unsigned char CRC2[4];
	unsigned int crc[2];
	unsigned char *rom8 = rom;
	
	assert(rom);
	
	gen_table(crc_table);

	if (!N64CalcCRC(crc_table, crc, rom))
	{
		unsigned int kk1 = crc[0];
		unsigned int kk2 = crc[1];
		int i;
		
		for (i = 0; i < 4; ++i)
		{
			CRC1[i] = (kk1 >> (24-8*i))&0xFF;
			CRC2[i] = (kk2 >> (24-8*i))&0xFF;
		}
		
		for (i = 0; i < 4; ++i)
			*(rom8 + N64_CRC1 + i) = CRC1[i];
		
		for (i = 0; i < 4; ++i)
			*(rom8 + N64_CRC2 + i) = CRC2[i];
	}
}

//
//
// end crc copy-pasta
//
//

// wait on enter key for windows users
void waitEnterkey(void)
{
	fflush(stdin);
	getchar();
}

// display a fatal error message and abort
void die(const char *fmt, ...)
{
	va_list args;
	
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	fprintf(stderr, "\n");
	va_end(args);
	
	waitEnterkey();
	exit(EXIT_FAILURE);
}

// minimal file loader
// returns 0 on failure
// returns pointer to loaded file on success
void *loadfile(const char *fn, size_t *sz)
{
	FILE *fp;
	void *dat;
	
	// rudimentary error checking returns 0 on any error
	if (
		!fn
		|| !sz
		|| !(fp = fopen(fn, "rb"))
		|| fseek(fp, 0, SEEK_END)
		|| !(*sz = ftell(fp))
		|| fseek(fp, 0, SEEK_SET)
		|| !(dat = malloc(*sz))
		|| fread(dat, 1, *sz, fp) != *sz
		|| fclose(fp)
	)
		return 0;
	
	return dat;
}

// minimal file writer
// returns 0 on failure
// returns non-zero on success
int savefile(const char *fn, const void *dat, const size_t sz)
{
	FILE *fp;
	
	/* rudimentary error checking returns 0 on any error */
	if (
		!fn
		|| !sz
		|| !dat
		|| !(fp = fopen(fn, "wb"))
		|| fwrite(dat, 1, sz, fp) != sz
		|| fclose(fp)
	)
		return 0;
	
	return 1;
}

// read big-endian 32-bit unsigned integer
uint32_t be32(const void *data)
{
	const uint8_t *d = data;
	
	return (d[0] << 24) | (d[1] << 16) | (d[2] << 8) | d[3];
}

// write big-endian 32-bit unsigned integer
void wbe32(void *dst, uint32_t v)
{
	uint8_t *d = dst;
	
	d[0] = v >> 24;
	d[1] = v >> 16;
	d[2] = v >> 8;
	d[3] = v;
}

// get pointer to dmadata entry
uint8_t *romGetEntry(uint8_t *rom)
{
	return rom + DATA_ENTRY;
}

// get pointer to message table
uint8_t *romGetTable(uint8_t *rom)
{
	return rom + TABLE_START;
}

// get pointer to message data (strings database)
char *romGetStrings(uint8_t *rom)
{
	uint8_t *entry = romGetEntry(rom);
	
	return (char*)(rom + be32(entry));
}

// display a hexadecimal control code
void printctrl(int c)
{
	fprintf(stdout, "\\x%02x", c & 0xff);
}

// display an oot message
void printoot(const char *string)
{
	// safety
	if (!string)
		return;
	
	// For each character in the string
	for ( ; *string; ++string)
	{
		int c = *string;
		
		// if it is a printable character, print it and move on
		if (strchr(
			" !\"#$%&'()*+,-./0123456789:;<=>?@"
			"ABCDEFGHIJKLMNOPQRSTUVWXYZ[]^_`"
			"abcdefghijklmnopqrstuvwxyz{|}"
			, c
		))
		{
			fprintf(stdout, "%c", c);
			continue;
		}
		
		// otherwise, assume it's a control character
		printctrl(c);
		
		// end of message character
		if (c == 0x02)
			break;
		
		// some control characters have extra parameters
		switch (c)
		{
			// pattern: ** xx
			case 0x13: case 0x0E:
			case 0x0C: case 0x1E:
			case 0x06: case 0x11:
			case 0x05: case 0x14:
				printctrl(string[1]);
				string += 1;
				break;

			// pattern: ** xx xx
			case 0x07:
			case 0x12:
				printctrl(string[1]);
				printctrl(string[2]);
				string += 2;
				break;

			// pattern: ** xx xx xx
			case 0x15:
				printctrl(string[1]);
				printctrl(string[2]);
				printctrl(string[3]);
				string += 3;
				break;
		}
	}
	
	// newline
	fprintf(stdout, "\n");
}

// inject game text
void inject(uint8_t *rom, const char *ifn)
{
	char *strings = romGetStrings(rom);
	char *stringsPos = strings;
	uint8_t *entry = romGetEntry(rom);
	uint8_t *table = romGetTable(rom);
	const char *delim = "\r\n";
	char *in;
	char *handle;
	size_t inSz;
	int i;
	
	// load input text file
	if (!(in = loadfile(ifn, &inSz)))
		die("failed to load file '%s'", ifn);
	
	// zero-initialize strings block
	memset(strings, 0, STRINGS_MAX);
	
	// split by newlines
	handle = strtok(in, delim);
	while (handle)
	{
		// lines are formatted "[%04x]: text", so skip first 8 characters
		char *s = handle + 8;
		char *ss; // substring
		
		// debugging
		//fprintf(stderr, "%s\n", s);
		
		// update table
		wbe32(table + i * 8 + 4, ((uintptr_t)(stringsPos - strings)) | 0x07000000);
		++i;
		
		// convert control characters into bytes
		for (ss = s; *ss; ++ss)
		{
			char buf[3];
			unsigned int c;
			
			// '\' denotes a control character e.g. '\x0a'
			if (*ss != '\\')
			{
				*stringsPos = *ss;
				++stringsPos;
				continue;
			}
			
			// "\x0a"
			buf[0] = ss[2];
			buf[1] = ss[3];
			buf[2] = '\0';
			
			// convert string to value
			if (sscanf(buf, "%x", &c) != 1)
				die("control character in message \"%s\" malformatted?", handle);
			
			/* untested string modification method, left for reference
			// replace
			*ss = c;
			
			// close the gap
			memmove(ss + 1, ss + 4, strlen(ss + 4) + 1);
			*/
			
			// test: can strings be made larger?
			/*{
				static int x = 1;
				
				if (((++x) & 1) && c == 0x02)
				{
					const char *test = "hello :)";
					
					strcpy(stringsPos, test);
					stringsPos += strlen(test);
				}
			}*/
			
			// update string
			*stringsPos = c;
			++stringsPos;
			
			// skip remaining 3 characters in control code
			ss += 3;
		}
		
		// zero-terminate and align
		// XXX this zero termination is indeed necessary
		*stringsPos = '\0';
		++stringsPos;
		while (((uintptr_t)(stringsPos - strings)) & 3)
		{
			*stringsPos = '\0';
			++stringsPos;
		}
		
		// advance to next line
		handle = strtok(0, delim);
	}
	
	// round up to nearest 16 byte boundary
	while (((uintptr_t)(stringsPos - strings)) & 15)
		++stringsPos;
	
	// safety
	if (stringsPos - strings > STRINGS_MAX)
		die("'%s' contains too much text, please erase unused\n"
			"sentences and/or simplify your text so the data will fit", ifn
		);
	
	// update dmadata entry to account for resized file
	wbe32(entry + 4, be32(entry) + (uint32_t)(stringsPos - strings));
	
	// updating dmadata requires updating the crc checksum as well
	n64crc(rom);
	
	// cleanup
	free(in);
	
	// success
	fprintf(stderr, "injected text data successfully\n");
}

// dump game text
void dump(const char *ofn, uint8_t *rom)
{
	char *strings = romGetStrings(rom);
	uint8_t *table = romGetTable(rom);
	
	// redirect program output to text file
	freopen(ofn, "w", stdout);
	
	// for each entry in the text table
	for (size_t i = 0; i < TABLE_LENGTH; i += 8)
	{
		unsigned lo = be32(table + i + 4) & 0x00ffffff;
		
		// check for messages pointing to data not 32-bit aligned
		if (lo & 3)
			die("[%04x]: error: address %08x not 32-bit aligned", i / 8, lo);
		
		// display the associated message
		fprintf(stdout, "[%04x]: ", (unsigned)i / 8);
		printoot(strings + lo);
	}
	
	// display success message
	fprintf(stderr, "wrote file '%s' successfully\n", ofn);
}

// program entry point
int main(void)
{
	FILE *fp;
	const char *guideName = "MANUAL.TXT";
	const char *cleanName = "mot-clean.z64";
	const char *outName = "mot-edited.z64";
	const char *inputName = "input.txt";
	const char *outputName = "output.txt";
	void *rom;
	size_t romSz;
	
	// display/output instructions the first time it's run
	if (!fopen(guideName, "rb"))
	{
		const char *instructions =
			"welcome to mot-textedit <z64.me>\n"
			"dumping text:\n"
			" - place mot-clean.z64 in the same folder\n"
			"   as this program, then run this program\n"
			" - if successful, it will produce output.txt\n"
			" - mot-clean.z64 is a clean 'master of time' rom\n"
			"   that has been decompressed using z64decompress\n"
			"injecting text:\n"
			" - rename output.txt to input.txt\n"
			" - run this program\n"
			" - if successful, it will produce mot-edited.z64\n"
		;
		
		// product instructions
		fp = fopen(guideName, "w");
		fprintf(fp, "%s", instructions);
		fclose(fp);
		die("%s", instructions);
	}
	
	// load rom
	if (!(rom = loadfile(cleanName, &romSz)))
		die("failed to load rom '%s'", cleanName);
	
	// ensure is decompressed
	if (romSz != 0x04000000)
		die("input rom '%s' unexpected size 0x%08x; is it\n"
			"decompressed? if not, use z64decompress to produce\n"
			"a decompressed version; please read the manual\n"
			, cleanName
			, romSz
		);
	
	// hacky branching logic depending on whether certain files exist
	if ((fp = fopen(inputName, "rb")))
	{
		fclose(fp);
		inject(rom, inputName);
		fp = fopen(outName, "wb");
		if (fwrite(rom, 1, romSz, fp) != romSz)
			die("failed to write '%s'", outName);
		fclose(fp);
		fprintf(stderr, "successfully wrote '%s'", outName);
	}
	else
	{
		dump(outputName, rom);
	}
	
	// cleanup
	free(rom);
	
	// success
	waitEnterkey();
	return 0;
}

