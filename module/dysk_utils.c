#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/string.h>
//Hash
#include <linux/crypto.h>
#include <crypto/hash.h>
#include <crypto/sha.h>
// Time
#include <linux/time.h>



#include "dysk_utils.h"
// Date Processing
const char* day_names[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
const char* month_names[] ={"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

// Base64 Processing
static const unsigned char base64_table[65] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";


// Date in UTC formatted as RFC1123
inline int utc_RFC1123_date(char* buf, size_t len)
{
	struct timeval now;
  struct tm tm_val;
	char day[8]  = {0};
	char hour[8] = {0};
	char min[8]  = {0};
	char sec[8]  = {0};

	memset(buf, 0, len);
  do_gettimeofday(&now);
  time_to_tm(now.tv_sec, 0, &tm_val);

	sprintf(day, tm_val.tm_mday < 10 ? "0%d" : "%d", tm_val.tm_mday);
	sprintf(hour, tm_val.tm_hour < 10 ? "0%d" : "%d", tm_val.tm_hour);
	sprintf(min, tm_val.tm_min < 10 ? "0%d" : "%d", tm_val.tm_min);
	sprintf(sec, tm_val.tm_sec < 10 ? "0%d" : "%d", tm_val.tm_sec);

	return snprintf(buf, len, "%s, %s %s %lu %s:%s:%s GMT", day_names[tm_val.tm_wday], day, month_names[tm_val.tm_mon], (tm_val.tm_year + 1900), hour, min, sec);
}

// IPv4 as unsigned int
unsigned int inet_addr(char* ip)
{
  int a, b, c, d;
  char addr[4];

  sscanf(ip, "%d.%d.%d.%d", &a, &b, &c, &d);
  addr[0] = a;
  addr[1] = b;
  addr[2] = c;
  addr[3] = d;

  return *(unsigned int *)addr;
}

/*
---------------------------------
Hashing
---------------------------------
*/

#define SHASH_DESC_ON_STACK(shash, ctx)				  \
	char __##shash##_desc[sizeof(struct shash_desc) +	  \
		crypto_shash_descsize(ctx)] CRYPTO_MINALIGN_ATTR; \
	struct shash_desc *shash = (struct shash_desc *)__##shash##_desc

inline int calc_hash(struct crypto_shash *tfm, unsigned char* digest, const unsigned char* buf, unsigned int buflen)
{
	SHASH_DESC_ON_STACK(desc, tfm);
	int err;

	desc->tfm = tfm;
	desc->flags = 0;
	err = crypto_shash_digest(desc, buf, buflen, digest);
	// Tested for 4.4 - should be forward compat
	memzero_explicit(desc, sizeof(*desc) + crypto_shash_descsize(desc->tfm));
	return err;
}

inline int calc_hmac(struct crypto_shash *tfm, unsigned char* digest, const unsigned char* key, unsigned int keylen, const unsigned char* buf, unsigned int buflen)
{
	int err;
	err = crypto_shash_setkey(tfm, key, keylen);
	if (!err) err = calc_hash(tfm, digest, buf, buflen);
	return err;
}

/*
----------------------------------
base64: a modified version of: http://web.mit.edu/freebsd/head/contrib/wpa/src/utils/base64.c
----------------------------------
*/
unsigned char* base64_encode(const unsigned char *src, size_t len, size_t *out_len)
{
	unsigned char *out, *pos;
	const unsigned char *end, *in;
	size_t olen;

	olen = len * 4 / 3 + 4; /* 3-byte blocks to 4-byte */
	olen++; /* nul termination */

	if (olen < len) return NULL; /* integer overflow */

	out = kmalloc(olen, GFP_KERNEL);
	if (out == NULL) return NULL;
	memset(out, 0, olen);

	end = src + len;
	in = src;
	pos = out;
	while (end - in >= 3) {
		*pos++ = base64_table[in[0] >> 2];
		*pos++ = base64_table[((in[0] & 0x03) << 4) | (in[1] >> 4)];
		*pos++ = base64_table[((in[1] & 0x0f) << 2) | (in[2] >> 6)];
		*pos++ = base64_table[in[2] & 0x3f];
		in += 3;
		}

	if (end - in) {
		*pos++ = base64_table[in[0] >> 2];
		if (end - in == 1) {
			*pos++ = base64_table[(in[0] & 0x03) << 4];
			*pos++ = '=';
		} else {
			*pos++ = base64_table[((in[0] & 0x03) << 4) | (in[1] >> 4)];
			*pos++ = base64_table[(in[1] & 0x0f) << 2];
		}
		*pos++ = '=';
	}

	*pos = '\0';
	if (out_len) *out_len = pos - out;
	return out;
}

inline unsigned char* base64_decode(const unsigned char *src, size_t len, size_t *out_len)
{
	unsigned char dtable[256], *out, *pos, block[4], tmp;
	size_t i, count, olen;
	int pad = 0;

	memset(dtable, 0x80, 256);
	for (i = 0; i < sizeof(base64_table) - 1; i++)
		dtable[base64_table[i]] = (unsigned char) i;
	dtable['='] = 0;

	count = 0;
	for (i = 0; i < len; i++) {
		if (dtable[src[i]] != 0x80)
			count++;
	}

	if (count == 0 || count % 4)
		return NULL;

	olen = count / 4 * 3;
	pos = out = kmalloc(olen, GFP_KERNEL);
	if (out == NULL)
		return NULL;

	memset(out, 0, olen);

	count = 0;
	for (i = 0; i < len; i++) {
		tmp = dtable[src[i]];
		if (tmp == 0x80)
			continue;

		if (src[i] == '=')
			pad++;
		block[count] = tmp;
		count++;
		if (count == 4) {
			*pos++ = (block[0] << 2) | (block[1] >> 4);
			*pos++ = (block[1] << 4) | (block[2] >> 2);
			*pos++ = (block[2] << 6) | block[3];
			count = 0;
			if (pad) {
				if (pad == 1)
					pos--;
				else if (pad == 2)
					pos -= 2;
				else {
					/* Invalid padding */
					kfree(out);
					return NULL;
				}
				break;
			}
		}
	}

	*out_len = pos - out;
	return out;
}
// Finds something, copies everything before it to [to]
inline int get_until(char *haystack, const char *until, char *to, size_t max)
{
	char *offset;
	int length;

	//offset = strnstr(haystack, until, max);
	offset = strnstr(haystack, until, max);
	if(!offset) return -1;

	length = offset - haystack;
	if(0 == length) return length;

	memcpy(to, haystack, length);
	return length;
}

