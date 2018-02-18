#include "utils.h"
#include "cache.h"
#include "se.h"

void trigger_se_rsa_op(void *buf, size_t size);
void trigger_se_aes_op(unsigned int op, void *dst, size_t dst_size, const void *src, size_t src_size);

/* Globals for driver. */
volatile security_engine_t *g_security_engine;

unsigned int (*g_se_callback)(void);

unsigned int g_se_modulus_sizes[KEYSLOT_RSA_MAX];
unsigned int g_se_exp_sizes[KEYSLOT_RSA_MAX];

/* Set the global security engine pointer. */
void set_security_engine_address(security_engine_t *security_engine) {
    g_security_engine = security_engine;
}

/* Get the global security engine pointer. */
security_engine_t *get_security_engine_address(void) {
    return g_security_engine;
}

void set_security_engine_callback(unsigned int (*callback)(void)) {
    if (callback == NULL || g_se_callback != NULL) {
        panic();
    }
    
    g_se_callback = callback;
}

/* Set the flags for an AES keyslot. */
void set_aes_keyslot_flags(unsigned int keyslot, unsigned int flags) {
    if (g_security_engine == NULL || keyslot >= KEYSLOT_AES_MAX) {
        panic();
    }
    
    /* Misc flags. */
    if (flags & ~0x80) {
        g_security_engine->AES_KEYSLOT_FLAGS[keyslot] = ~flags;
    }
    
    /* Disable keyslot reads. */
    if (flags & 0x80) {
        g_security_engine->AES_KEY_READ_DISABLE_REG &= ~(1 << keyslot);
    }
}

/* Set the flags for an RSA keyslot. */
void set_rsa_keyslot_flags(unsigned int keyslot, unsigned int flags) {
    if (g_security_engine == NULL || keyslot >= KEYSLOT_RSA_MAX) {
        panic();
    }
    
    /* Misc flags. */
    if (flags & ~0x80) {
        /* TODO: Why are flags assigned this way? */
        g_security_engine->RSA_KEYSLOT_FLAGS[keyslot] = (((flags >> 4) & 4) | (flags & 3)) ^ 7;
    }
    
    /* Disable keyslot reads. */
    if (flags & 0x80) {
        g_security_engine->RSA_KEY_READ_DISABLE_REG &= ~(1 << keyslot);
    }
}

void clear_aes_keyslot(unsigned int keyslot) {
    if (g_security_engine == NULL || keyslot >= KEYSLOT_AES_MAX) {
        panic();
    }
    
    /* Zero out the whole keyslot and IV. */
    for (unsigned int i = 0; i < 0x10; i++) {
        g_security_engine->AES_KEYTABLE_ADDR = (keyslot << 4) | i;
        g_security_engine->AES_KEYTABLE_DATA = 0;
    }
}

void clear_rsa_keyslot(unsigned int keyslot) {
    if (g_security_engine == NULL || keyslot >= KEYSLOT_RSA_MAX) {
        panic();
    }
    
    /* Zero out the whole keyslot. */
    for (unsigned int i = 0; i < 0x40; i++) {
        /* Select Keyslot Modulus[i] */
        g_security_engine->RSA_KEYTABLE_ADDR = (keyslot << 7) | i | 0x40;
        g_security_engine->RSA_KEYTABLE_DATA = 0;
    }
    for (unsigned int i = 0; i < 0x40; i++) {
        /* Select Keyslot Expontent[i] */
        g_security_engine->RSA_KEYTABLE_ADDR = (keyslot << 7) | i;
        g_security_engine->RSA_KEYTABLE_DATA = 0;
    }
}

void set_aes_keyslot(unsigned int keyslot, const void *key, size_t key_size) {
    if (g_security_engine == NULL || keyslot >= KEYSLOT_AES_MAX || key_size > KEYSIZE_AES_MAX) {
        panic();
    }
    
    for (size_t i = 0; i < (key_size >> 2); i++) {
        g_security_engine->AES_KEYTABLE_ADDR = (keyslot << 4) | i;
        g_security_engine->AES_KEYTABLE_DATA = read32le(key, 4 * i);
    }
}

void set_rsa_keyslot(unsigned int keyslot, const void  *modulus, size_t modulus_size, const void *exponent, size_t exp_size) {
    if (g_security_engine == NULL || keyslot >= KEYSLOT_RSA_MAX || modulus_size > KEYSIZE_RSA_MAX || exp_size > KEYSIZE_RSA_MAX) {
        panic();
    }
    
    for (size_t i = 0; i < (modulus_size >> 2); i++) {
        g_security_engine->RSA_KEYTABLE_ADDR = (keyslot << 7) | 0x40 | i;
        g_security_engine->RSA_KEYTABLE_DATA = read32be(modulus, 4 * i);
    }
    
    for (size_t i = 0; i < (exp_size >> 2); i++) {
        g_security_engine->RSA_KEYTABLE_ADDR = (keyslot << 7) | i;
        g_security_engine->RSA_KEYTABLE_DATA = read32be(exponent, 4 * i);
    }
    
    g_se_modulus_sizes[keyslot] = modulus_size;
    g_se_exp_sizes[keyslot] = exp_size;
}


void set_aes_keyslot_iv(unsigned int keyslot, const void *iv, size_t iv_size) {
    if (g_security_engine == NULL || keyslot >= KEYSLOT_AES_MAX || iv_size > 0x10) {
        panic();
    }
    
    for (size_t i = 0; i < (iv_size >> 2); i++) {
        g_security_engine->AES_KEYTABLE_ADDR = (keyslot << 4) | 8 | i;
        g_security_engine->AES_KEYTABLE_DATA = read32le(iv, 4 * i);
    }
}

void set_se_ctr(const void *ctr) {
    if (g_security_engine == NULL) {
        panic();
    }
    
    for (unsigned int i = 0; i < 4; i++) {
        g_security_engine->CRYPTO_CTR_REG[i] = read32le(ctr, i * 4);
    }
}

void decrypt_data_into_keyslot(unsigned int keyslot_dst, unsigned int keyslot_src, const void *wrapped_key, size_t wrapped_key_size) {
    if (g_security_engine == NULL || keyslot_dst >= KEYSLOT_AES_MAX || keyslot_src >= KEYSIZE_AES_MAX || wrapped_key_size > KEYSIZE_AES_MAX) {
        panic();
    }
    
    g_security_engine->CONFIG_REG = (ALG_AES_DEC | DST_KEYTAB);
    g_security_engine->CRYPTO_REG = keyslot_src << 24;
    g_security_engine->BLOCK_COUNT_REG = 0;
    g_security_engine->CRYPTO_KEYTABLE_DST_REG = keyslot_dst << 8;

    flush_dcache_range(wrapped_key, (const uint8_t *)wrapped_key + wrapped_key_size);
    trigger_se_aes_op(OP_START, NULL, 0, wrapped_key, wrapped_key_size);
}


void se_crypt_aes(unsigned int keyslot, void *dst, size_t dst_size, const void *src, size_t src_size, unsigned int config, unsigned int mode, unsigned int (*callback)(void));

void se_exp_mod(unsigned int keyslot, void *buf, size_t size, unsigned int (*callback)(void)) {
    uint8_t stack_buf[KEYSIZE_RSA_MAX];
    
    if (g_security_engine == NULL || keyslot >= KEYSLOT_RSA_MAX || size > KEYSIZE_RSA_MAX) {
        panic();
    }
    
    /* Endian swap the input. */
    for (size_t i = size; i > 0; i--) {
        stack_buf[i] = *((uint8_t *)buf + size - i);
    }
    

    g_security_engine->CONFIG_REG = (ALG_RSA | DST_RSAREG);
    g_security_engine->RSA_CONFIG = keyslot << 24;
    g_security_engine->RSA_KEY_SIZE_REG = (g_se_modulus_sizes[keyslot] >> 6) - 1;
    g_security_engine->RSA_EXP_SIZE_REG = g_se_exp_sizes[keyslot] >> 2;

    set_security_engine_callback(callback);

    flush_dcache_range(stack_buf, stack_buf + KEYSIZE_RSA_MAX);
    trigger_se_rsa_op(stack_buf, size);

    while (!(g_security_engine->INT_STATUS_REG & 2)) { /* Wait a while */ }
}
