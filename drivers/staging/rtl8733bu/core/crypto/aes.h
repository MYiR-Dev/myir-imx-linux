/*
 * AES functions
 * Copyright (c) 2003-2006, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef AES_H
#define AES_H

#define AES_BLOCK_SIZE 16

void * rtl_aes_encryp_init(const u8 *key, size_t len);
int rtl_aes_encryp(void *ctx, const u8 *plain, u8 *crypt);
void rtl_aes_encryp_deinit(void *ctx);
void * aes_decrypt_init(const u8 *key, size_t len);
int aes_decrypt(void *ctx, const u8 *crypt, u8 *plain);
void aes_decrypt_deinit(void *ctx);

#endif /* AES_H */
