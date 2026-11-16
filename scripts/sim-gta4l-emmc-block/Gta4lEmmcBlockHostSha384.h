/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef GTA4L_EMMC_BLOCK_HOST_SHA384_H
#define GTA4L_EMMC_BLOCK_HOST_SHA384_H
#include <openssl/evp.h>
#include <stddef.h>

typedef struct {
	EVP_MD_CTX *ctx;
} SHA384_CTX;

static inline int
SHA384_Init(SHA384_CTX *context)
{
	context->ctx = EVP_MD_CTX_new();
	return (context->ctx != NULL) &&
	    (EVP_DigestInit_ex(context->ctx, EVP_sha384(), NULL) == 1);
}

static inline int
SHA384_Update(SHA384_CTX *context, const void *data, size_t length)
{
	return EVP_DigestUpdate(context->ctx, data, length) == 1;
}

static inline int
SHA384_Final(unsigned char *digest, SHA384_CTX *context)
{
	unsigned int length = 0;
	int ok = (EVP_DigestFinal_ex(context->ctx, digest, &length) == 1) &&
	    (length == 48u);

	EVP_MD_CTX_free(context->ctx);
	context->ctx = NULL;
	return ok;
}
#endif
