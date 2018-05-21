#ifndef _DYSK_UTILS_H
#define _DYSK_UTILS_H



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



int utc_RFC1123_date(char *buf, size_t len);

//IPv4 as unsigned int
unsigned int inet_addr(char *ip);

// Calc a HMAC
int calc_hmac(struct crypto_shash *tfm, unsigned char *digest, const unsigned char *key, unsigned int keylen, const unsigned char *buf, unsigned int buflen);

// Base64
unsigned char *base64_encode(const unsigned char *src, size_t len, size_t *out_len);
unsigned char *base64_decode(const unsigned char *src, size_t len, size_t *out_len);

// Finds something, copies everything before it to [to] returns len of copied or -1
int get_until(char *haystack, const char *until, char *to, size_t max);


#endif


