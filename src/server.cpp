#include "header.h"

char charset[] = "BCDFGHJKMPQRTVWXY2346789";

void unpack2003(uint32_t *osfamily, uint32_t *hash, uint32_t *sig, uint32_t *prefix, uint32_t *raw)
{
	osfamily[0] = raw[0] & 0x7ff;
	hash[0] = ((raw[0] >> 11) | (raw[1] << 21)) & 0x7fffffff;
	sig[0] = (raw[1] >> 10) | (raw[2] << 22);
	sig[1] = ((raw[2] >> 10) | (raw[3] << 22)) & 0x3fffffff;
	prefix[0] = (raw[3] >> 8) & 0x3ff;
}

void pack2003(uint32_t *raw, uint32_t *osfamily, uint32_t *hash, uint32_t *sig, uint32_t *prefix)
{
	raw[0] = osfamily[0] | (hash[0] << 11);
	raw[1] = (hash[0] >> 21) | (sig[0] << 10);
	raw[2] = (sig[0] >> 22) | (sig[1] << 10);
	raw[3] = (sig[1] >> 22) | (prefix[0] << 8);
}

int verify2003(EC_GROUP *ec, EC_POINT *generator, EC_POINT *public_key, char *cdkey)
{
	char key[25];
	BN_CTX *ctx = BN_CTX_new();

	for (int i = 0, k = 0; i < strlen(cdkey); i++) {
		for (int j = 0; j < 24; j++) {
			if (cdkey[i] != '-' && cdkey[i] == charset[j]) {
				key[k++] = j;
				break;
			}
			assert(j < 24);
		}
		if (k >= 25) break;
	}

    uint32_t bkey[4] = {0};
    uint32_t osfamily[1], hash[1], sig[2], prefix[1];

	unbase24(bkey, key);

	printf("%.8ix %.8ix %.8ix %.8ix\n", bkey[3], bkey[2], bkey[1], bkey[0]);
	unpack2003(osfamily, hash, sig, prefix, bkey);
	
	printf("OS Family: %iu\nHash: %.8ix\nSig: %.8ix %.8ix\nPrefix: %.8ix\n", osfamily[0], hash[0], sig[1], sig[0], prefix[0]);

    uint8_t buf[FIELD_BYTES_2003], md[20];
    uint32_t h1[2];
	SHA_CTX h_ctx;
	
	/* h1 = SHA-1(5D || OS Family || Hash || Prefix || 00 00) */
	SHA1_Init(&h_ctx);
	buf[0] = 0x5d;
	buf[1] = osfamily[0] & 0xff;
	buf[2] = (osfamily[0] & 0xff00) >> 8;
	buf[3] = hash[0] & 0xff;
	buf[4] = (hash[0] & 0xff00) >> 8;
	buf[5] = (hash[0] & 0xff0000) >> 16;
	buf[6] = (hash[0] & 0xff000000) >> 24;
	buf[7] = prefix[0] & 0xff;
	buf[8] = (prefix[0] & 0xff00) >> 8;
	buf[9] = buf[10] = 0;
	SHA1_Update(&h_ctx, buf, 11);
	SHA1_Final(md, &h_ctx);
	h1[0] = md[0] | (md[1] << 8) | (md[2] << 16) | (md[3] << 24);
	h1[1] = (md[4] | (md[5] << 8) | (md[6] << 16) | (md[7] << 24)) >> 2;
	h1[1] &= 0x3FFFFFFF;
	printf("h1: %.8ix %.8ix\n", h1[1], h1[0]);
	
	BIGNUM *s, *h, *x, *y;
	x = BN_new();
	y = BN_new();
	endian((uint8_t *)sig, 8);
	endian((uint8_t *)h1, 8);
	s = BN_bin2bn((uint8_t *)sig, 8, nullptr);
	h = BN_bin2bn((uint8_t *)h1, 8, nullptr);

	EC_POINT *r = EC_POINT_new(ec);
	EC_POINT *t = EC_POINT_new(ec);
	/* r = sig*(sig*generator + h1*public_key) */
	EC_POINT_mul(ec, t, nullptr, generator, s, ctx);
	EC_POINT_mul(ec, r, nullptr, public_key, h, ctx);
	EC_POINT_add(ec, r, r, t, ctx);
	EC_POINT_mul(ec, r, nullptr, r, s, ctx);
	EC_POINT_get_affine_coordinates(ec, r, x, y, ctx);

    uint32_t h2[1];
	/* h2 = SHA-1(79 || OS Family || r.x || r.y) */
	SHA1_Init(&h_ctx);
	buf[0] = 0x79;
	buf[1] = osfamily[0] & 0xff;
	buf[2] = (osfamily[0] & 0xff00) >> 8;
	SHA1_Update(&h_ctx, buf, 3);
	
	memset(buf, 0, FIELD_BYTES_2003);
	BN_bn2bin(x, buf);
	endian((uint8_t *)buf, FIELD_BYTES_2003);
	SHA1_Update(&h_ctx, buf, FIELD_BYTES_2003);
	
	memset(buf, 0, FIELD_BYTES_2003);
	BN_bn2bin(y, buf);
	endian((uint8_t *)buf, FIELD_BYTES_2003);
	SHA1_Update(&h_ctx, buf, FIELD_BYTES_2003);
	
	SHA1_Final(md, &h_ctx);
	h2[0] = (md[0] | (md[1] << 8) | (md[2] << 16) | (md[3] << 24)) & 0x7fffffff;
	printf("Calculated hash: %.8ix\n", h2[0]);
	
	BN_free(s);
	BN_free(h);
	BN_free(x);
	BN_free(y);
	EC_POINT_free(r);
	EC_POINT_free(t);
	BN_CTX_free(ctx);
	
	if (h2[0] == hash[0]) {
		printf("Key VALID\n");
		return 1;
	}
	else {
		printf("Key invalid\n");
		return 0;
	}
}

void generate2003(char *pkey, EC_GROUP *ec, EC_POINT *generator, BIGNUM *order, BIGNUM *priv, uint32_t *osfamily, uint32_t *prefix)
{
	BN_CTX *ctx = BN_CTX_new();

	BIGNUM *k = BN_new();
	BIGNUM *s = BN_new();
	BIGNUM *x = BN_new();
	BIGNUM *y = BN_new();
	BIGNUM *b = BN_new();
	EC_POINT *r = EC_POINT_new(ec);

    uint32_t bkey[4];
    uint8_t buf[FIELD_BYTES_2003], md[20];
    uint32_t h1[2];
    uint32_t hash[1], sig[2];
	
	SHA_CTX h_ctx;
	
	for (;;) {
		/* r = k*generator */
		BN_pseudo_rand(k, FIELD_BITS_2003, -1, 0);
		EC_POINT_mul(ec, r, nullptr, generator, k, ctx);
		EC_POINT_get_affine_coordinates_GFp(ec, r, x, y, ctx);
			
		/* hash = SHA-1(79 || OS Family || r.x || r.y) */
		SHA1_Init(&h_ctx);
		buf[0] = 0x79;
		buf[1] = osfamily[0] & 0xff;
		buf[2] = (osfamily[0] & 0xff00) >> 8;
		SHA1_Update(&h_ctx, buf, 3);
		
		memset(buf, 0, FIELD_BYTES_2003);
		BN_bn2bin(x, buf);
		endian((uint8_t *)buf, FIELD_BYTES_2003);
		SHA1_Update(&h_ctx, buf, FIELD_BYTES_2003);
		
		memset(buf, 0, FIELD_BYTES_2003);
		BN_bn2bin(y, buf);
		endian((uint8_t *)buf, FIELD_BYTES_2003);
		SHA1_Update(&h_ctx, buf, FIELD_BYTES_2003);
		
		SHA1_Final(md, &h_ctx);
		hash[0] = (md[0] | (md[1] << 8) | (md[2] << 16) | (md[3] << 24)) & 0x7fffffff;
			
		/* h1 = SHA-1(5D || OS Family || Hash || Prefix || 00 00) */
		SHA1_Init(&h_ctx);
		buf[0] = 0x5d;
		buf[1] = osfamily[0] & 0xff;
		buf[2] = (osfamily[0] & 0xff00) >> 8;
		buf[3] = hash[0] & 0xff;
		buf[4] = (hash[0] & 0xff00) >> 8;
		buf[5] = (hash[0] & 0xff0000) >> 16;
		buf[6] = (hash[0] & 0xff000000) >> 24;
		buf[7] = prefix[0] & 0xff;
		buf[8] = (prefix[0] & 0xff00) >> 8;
		buf[9] = buf[10] = 0;
		SHA1_Update(&h_ctx, buf, 11);
		SHA1_Final(md, &h_ctx);
		h1[0] = md[0] | (md[1] << 8) | (md[2] << 16) | (md[3] << 24);
		h1[1] = (md[4] | (md[5] << 8) | (md[6] << 16) | (md[7] << 24)) >> 2;
		h1[1] &= 0x3FFFFFFF;
		printf("h1: %.8ix %.8ix\n", h1[1], h1[0]);
	
		/* s = ( -h1*priv + sqrt( (h1*priv)^2 + 4k ) ) / 2 */
		endian((uint8_t *)h1, 8);
		BN_bin2bn((uint8_t *)h1, 8, b);
		BN_mod_mul(b, b, priv, order, ctx);
		BN_copy(s, b);
		BN_mod_sqr(s, s, order, ctx);
		BN_lshift(k, k, 2);
		BN_add(s, s, k);
		BN_mod_sqrt(s, s, order, ctx);
		BN_mod_sub(s, s, b, order, ctx);
		if (BN_is_odd(s)) {
			BN_add(s, s, order);
		}
		BN_rshift1(s, s);
		sig[0] = sig[1] = 0;
		BN_bn2bin(s, (uint8_t *)sig);
		endian((uint8_t *)sig, BN_num_bytes(s));
		if (sig[1] < 0x40000000) break;
	}
	pack2003(bkey, osfamily, hash, sig, prefix);
	printf("OS family: %iu\nHash: %.8ix\nSig: %.8ix %.8ix\nPrefix: %.8ix\n", osfamily[0], hash[0], sig[1], sig[0], prefix[0]);
	printf("%.8ix %.8ix %.8ix %.8ix\n", bkey[3], bkey[2], bkey[1], bkey[0]);

    base24(pkey, bkey);
	
	BN_free(k);
	BN_free(s);
	BN_free(x);
	BN_free(y);
	BN_free(b);
	EC_POINT_free(r);

	BN_CTX_free(ctx);
	
}

int main()
{
	BIGNUM *a, *b, *p, *gx, *gy, *pubx, *puby, *n, *priv;
	BN_CTX *ctx = BN_CTX_new();
	
	a = BN_new();
	b = BN_new();
	p = BN_new();
	gx = BN_new();
	gy = BN_new();
	pubx = BN_new();
	puby = BN_new();
	n = BN_new();
	priv = BN_new();

	/* Windows Sever 2003 VLK */
	BN_set_word(a, 1);
	BN_set_word(b, 0);
	BN_hex2bn(&p,    "C9AE7AED19F6A7E100AADE98134111AD8118E59B8264734327940064BC675A0C682E19C89695FBFA3A4653E47D47FD7592258C7E3C3C61BBEA07FE5A7E842379");
	BN_hex2bn(&gx,   "85ACEC9F9F9B456A78E43C3637DC88D21F977A9EC15E5225BD5060CE5B892F24FEDEE574BF5801F06BC232EEF2161074496613698D88FAC4B397CE3B475406A7");
	BN_hex2bn(&gy,   "66B7D1983F5D4FE43E8B4F1E28685DE0E22BBE6576A1A6B86C67533BF72FD3D082DBA281A556A16E593DB522942C8DD7120BA50C9413DF944E7258BDDF30B3C4");
	BN_hex2bn(&pubx, "90BF6BD980C536A8DB93B52AA9AEBA640BABF1D31BEC7AA345BB7510194A9B07379F552DA7B4A3EF81A9B87E0B85B5118E1E20A098641EE4CCF2045558C98C0E");
	BN_hex2bn(&puby, "6B87D1E658D03868362945CDD582E2CF33EE4BA06369E0EFE9E4851F6DCBEC7F15081E250D171EA0CC4CB06435BCFCFEA8F438C9766743A06CBD06E7EFB4C3AE");
	BN_hex2bn(&n,    "4CC5C56529F0237D"); // from mskey 4in1
	BN_hex2bn(&priv, "2606120F59C05118");
	
	
	EC_GROUP *ec = EC_GROUP_new_curve_GFp(p, a, b, ctx);
	EC_POINT *g = EC_POINT_new(ec);
	EC_POINT_set_affine_coordinates_GFp(ec, g, gx, gy, ctx);
	EC_POINT *pub = EC_POINT_new(ec);
	EC_POINT_set_affine_coordinates_GFp(ec, pub, pubx, puby, ctx);
	
	assert(EC_POINT_is_on_curve(ec, g, ctx) == 1);
	assert(EC_POINT_is_on_curve(ec, pub, ctx) == 1);

    char pkey[25];
    uint32_t osfamily[1], prefix[1];
	
	osfamily[0] = 1280;
	RAND_pseudo_bytes((uint8_t *)prefix, 4);
	prefix[0] &= 0x3ff;
	
	do {
		generate2003(pkey, ec, g, n, priv, osfamily, prefix);
	} while (!verify2003(ec, g, pub, pkey));
	
	print_product_key(pkey);
    std::cout << std::endl << std::endl;

	BN_CTX_free(ctx);
	
	return 0;
}
