/*
 * The contents of this file are public domain.
 *
 * Based on: lookup3.c, by Bob Jenkins, May 2006, Public Domain.
 */

#include "system.h"
#include "hash.h"

/*
 * A simple version of Bob Jenkins' lookup3.c hash.
 *
 * It is supposed to give same results as hashlittle() on little-endian
 * and hashbig() on big-endian machines.
 *
 * Speed seems comparable to Jenkins' optimized version (~ -10%).
 * Actual difference varies as it depends on cpu/compiler/libc details.
 */

/* rotate uint32 */
#define rot(x,k) (((x)<<(k)) | ((x)>>(32-(k))))

/* mix 3 32-bit values reversibly */
#define mix(a, b, c) do { \
	a -= c;  a ^= rot(c, 4);  c += b; \
	b -= a;  b ^= rot(a, 6);  a += c; \
	c -= b;  c ^= rot(b, 8);  b += a; \
	a -= c;  a ^= rot(c,16);  c += b; \
	b -= a;  b ^= rot(a,19);  a += c; \
	c -= b;  c ^= rot(b, 4);  b += a; \
} while (0)

/* final mixing of 3 32-bit values (a,b,c) into c */
#define final(a, b, c) do { \
	c ^= b; c -= rot(b,14); \
	a ^= c; a -= rot(c,11); \
	b ^= a; b -= rot(a,25); \
	c ^= b; c -= rot(b,16); \
	a ^= c; a -= rot(c, 4); \
	b ^= a; b -= rot(a,14); \
	c ^= b; c -= rot(b,24); \
} while (0)

/*
 * GCC does not know how to optimize short variable-length copies.
 * Its faster to do dumb inlined copy than call out to libc.
 */
static inline void simple_memcpy(void *dst_, const void *src_, size_t len)
{
	const uint8_t *src = src_;
	uint8_t *dst = dst_;
	while (len--)
		*dst++ = *src++;
}

/* short version - let compiler worry about memory access */
uint32_t lookup3_hash(const void *data, size_t len)
{
	uint32_t a, b, c;
	uint32_t buf[3];
	const uint8_t *p = data;

	a = b = c = 0xdeadbeef + len;
	if (len == 0)
		goto done;

	while (len > 12) {
		memcpy(buf, p, 12);
		a += buf[0];
		b += buf[1];
		c += buf[2];
		mix(a, b, c);
		p += 12;
		len -= 12;
	}

	buf[0] = buf[1] = buf[2] = 0;
	simple_memcpy(buf, p, len);
	a += buf[0];
	b += buf[1];
	c += buf[2];
	final(a, b, c);
done:
	return c;
}


/*
 * Reversible integer hash function by Thomas Wang.
 */

uint32_t hash32(uint32_t v)
{
	v = ~v + (v << 15);
	v = v ^ (v >> 12);
	v = v + (v << 2);
	v = v ^ (v >> 4);
	v = v * 2057;
	v = v ^ (v >> 16);
	return v;
}

