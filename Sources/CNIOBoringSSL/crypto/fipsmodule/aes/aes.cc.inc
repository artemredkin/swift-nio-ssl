/*
 * Copyright 2002-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <CNIOBoringSSL_aes.h>

#include <assert.h>

#include "internal.h"


// Be aware that different sets of AES functions use incompatible key
// representations, varying in format of the key schedule, the |AES_KEY.rounds|
// value, or both. Therefore they cannot mix. Also, on AArch64, the plain-C
// code, above, is incompatible with the |aes_hw_*| functions.

void AES_encrypt(const uint8_t *in, uint8_t *out, const AES_KEY *key) {
  if (hwaes_capable()) {
    aes_hw_encrypt(in, out, key);
  } else if (vpaes_capable()) {
    vpaes_encrypt(in, out, key);
  } else {
    aes_nohw_encrypt(in, out, key);
  }
}

void AES_decrypt(const uint8_t *in, uint8_t *out, const AES_KEY *key) {
  if (hwaes_capable()) {
    aes_hw_decrypt(in, out, key);
  } else if (vpaes_capable()) {
    vpaes_decrypt(in, out, key);
  } else {
    aes_nohw_decrypt(in, out, key);
  }
}

int AES_set_encrypt_key(const uint8_t *key, unsigned bits, AES_KEY *aeskey) {
  if (bits != 128 && bits != 192 && bits != 256) {
    return -2;
  }
  if (hwaes_capable()) {
    return aes_hw_set_encrypt_key(key, bits, aeskey);
  } else if (vpaes_capable()) {
    return vpaes_set_encrypt_key(key, bits, aeskey);
  } else {
    return aes_nohw_set_encrypt_key(key, bits, aeskey);
  }
}

int AES_set_decrypt_key(const uint8_t *key, unsigned bits, AES_KEY *aeskey) {
  if (bits != 128 && bits != 192 && bits != 256) {
    return -2;
  }
  if (hwaes_capable()) {
    return aes_hw_set_decrypt_key(key, bits, aeskey);
  } else if (vpaes_capable()) {
    return vpaes_set_decrypt_key(key, bits, aeskey);
  } else {
    return aes_nohw_set_decrypt_key(key, bits, aeskey);
  }
}

#if defined(HWAES) && (defined(OPENSSL_X86) || defined(OPENSSL_X86_64))
// On x86 and x86_64, |aes_hw_set_decrypt_key|, we implement
// |aes_hw_encrypt_key_to_decrypt_key| in assembly and rely on C code to combine
// the operations.
int aes_hw_set_decrypt_key(const uint8_t *user_key, int bits, AES_KEY *key) {
  int ret = aes_hw_set_encrypt_key(user_key, bits, key);
  if (ret == 0) {
    aes_hw_encrypt_key_to_decrypt_key(key);
  }
  return ret;
}

int aes_hw_set_encrypt_key(const uint8_t *user_key, int bits, AES_KEY *key) {
  if (aes_hw_set_encrypt_key_alt_preferred()) {
    return aes_hw_set_encrypt_key_alt(user_key, bits, key);
  } else {
    return aes_hw_set_encrypt_key_base(user_key, bits, key);
  }
}
#endif

#if defined(VPAES) && defined(OPENSSL_X86)
// On x86, there is no |vpaes_ctr32_encrypt_blocks|, so we implement it
// ourselves. This avoids all callers needing to account for a missing function.
void vpaes_ctr32_encrypt_blocks(const uint8_t *in, uint8_t *out, size_t blocks,
                                const AES_KEY *key, const uint8_t iv[16]) {
  uint32_t ctr = CRYPTO_load_u32_be(iv + 12);
  uint8_t iv_buf[16], enc[16];
  OPENSSL_memcpy(iv_buf, iv, 12);
  for (size_t i = 0; i < blocks; i++) {
    CRYPTO_store_u32_be(iv_buf + 12, ctr);
    vpaes_encrypt(iv_buf, enc, key);
    CRYPTO_xor16(out, in, enc);
    ctr++;
    in += 16;
    out += 16;
  }
}
#endif

#if defined(BSAES)
void vpaes_ctr32_encrypt_blocks_with_bsaes(const uint8_t *in, uint8_t *out,
                                           size_t blocks, const AES_KEY *key,
                                           const uint8_t ivec[16]) {
  // |bsaes_ctr32_encrypt_blocks| is faster than |vpaes_ctr32_encrypt_blocks|,
  // but it takes at least one full 8-block batch to amortize the conversion.
  if (blocks < 8) {
    vpaes_ctr32_encrypt_blocks(in, out, blocks, key, ivec);
    return;
  }

  size_t bsaes_blocks = blocks;
  if (bsaes_blocks % 8 < 6) {
    // |bsaes_ctr32_encrypt_blocks| internally works in 8-block batches. If the
    // final batch is too small (under six blocks), it is faster to loop over
    // |vpaes_encrypt|. Round |bsaes_blocks| down to a multiple of 8.
    bsaes_blocks -= bsaes_blocks % 8;
  }

  AES_KEY bsaes;
  vpaes_encrypt_key_to_bsaes(&bsaes, key);
  bsaes_ctr32_encrypt_blocks(in, out, bsaes_blocks, &bsaes, ivec);
  OPENSSL_cleanse(&bsaes, sizeof(bsaes));

  in += 16 * bsaes_blocks;
  out += 16 * bsaes_blocks;
  blocks -= bsaes_blocks;

  uint8_t new_ivec[16];
  memcpy(new_ivec, ivec, 12);
  uint32_t ctr = CRYPTO_load_u32_be(ivec + 12) + bsaes_blocks;
  CRYPTO_store_u32_be(new_ivec + 12, ctr);

  // Finish any remaining blocks with |vpaes_ctr32_encrypt_blocks|.
  vpaes_ctr32_encrypt_blocks(in, out, blocks, key, new_ivec);
}
#endif  // BSAES

ctr128_f aes_ctr_set_key(AES_KEY *aes_key, int *out_is_hwaes,
                         block128_f *out_block, const uint8_t *key,
                         size_t key_bytes) {
  // This function assumes the key length was previously validated.
  assert(key_bytes == 128 / 8 || key_bytes == 192 / 8 || key_bytes == 256 / 8);
  if (hwaes_capable()) {
    aes_hw_set_encrypt_key(key, (int)key_bytes * 8, aes_key);
    if (out_is_hwaes) {
      *out_is_hwaes = 1;
    }
    if (out_block) {
      *out_block = aes_hw_encrypt;
    }
    return aes_hw_ctr32_encrypt_blocks;
  }

  if (vpaes_capable()) {
    vpaes_set_encrypt_key(key, (int)key_bytes * 8, aes_key);
    if (out_block) {
      *out_block = vpaes_encrypt;
    }
    if (out_is_hwaes) {
      *out_is_hwaes = 0;
    }
#if defined(BSAES)
    assert(bsaes_capable());
    return vpaes_ctr32_encrypt_blocks_with_bsaes;
#else
    return vpaes_ctr32_encrypt_blocks;
#endif
  }

  aes_nohw_set_encrypt_key(key, (int)key_bytes * 8, aes_key);
  if (out_is_hwaes) {
    *out_is_hwaes = 0;
  }
  if (out_block) {
    *out_block = aes_nohw_encrypt;
  }
  return aes_nohw_ctr32_encrypt_blocks;
}
