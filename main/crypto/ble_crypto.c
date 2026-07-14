#include "ble_crypto.h"
#include <string.h>
#include "esp_log.h"
#include "esp_random.h"
#include "mbedtls/ecdh.h"
#include "mbedtls/hkdf.h"
#include "mbedtls/chachapoly.h"
#include "mbedtls/gcm.h"
#include "mbedtls/base64.h"

static const char *TAG = "BLE_CRYPTO";

// 런타임 중 유지될 세션 상태 구조체
typedef struct {
    mbedtls_ecdh_context ecdh;
    uint8_t session_key[32]; 
    bool is_encrypted;       
} ble_session_t;

static ble_session_t g_ble_session;

// ESP32 하드웨어 난수 생성기 연동
static int hw_rng(void *p_rng, unsigned char *buf, size_t len) {
    esp_fill_random(buf, len);
    return 0;
}

esp_err_t ble_crypto_init_and_get_pubkey(char *out_base64_pubkey, size_t max_len) {
    int ret;
    uint8_t raw_pubkey[32];
    size_t olen = 0;

    memset(&g_ble_session, 0, sizeof(ble_session_t));
    mbedtls_ecdh_init(&g_ble_session.ecdh);

    // 1. Curve25519 셋업
    ret = mbedtls_ecdh_setup(&g_ble_session.ecdh, MBEDTLS_ECP_DP_CURVE25519);
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to setup Curve25519");
        return ESP_FAIL;
    }

    // 2. [mbedTLS 3.x 공식 API] 키쌍 생성 및 공개키 추출 (구조체 접근 X)
    ret = mbedtls_ecdh_make_public(&g_ble_session.ecdh, &olen, raw_pubkey, sizeof(raw_pubkey), hw_rng, NULL); 
    if (ret != 0 || olen != 32) {
        ESP_LOGE(TAG, "Failed to generate public key");
        return ESP_FAIL;
    }

    // 3. Base64 인코딩
    size_t b64_olen = 0;
    ret = mbedtls_base64_encode((unsigned char *)out_base64_pubkey, max_len, &b64_olen, raw_pubkey, olen);
    if (ret != 0) return ESP_FAIL;

    ESP_LOGI(TAG, "Device public key generated successfully.");
    return ESP_OK;
}

esp_err_t ble_crypto_compute_session_key(const char *app_base64_pubkey) {
    int ret;
    uint8_t app_raw_pubkey[32];
    size_t olen = 0;

    // 1. 앱의 공개키 Base64 디코딩
    ret = mbedtls_base64_decode(app_raw_pubkey, sizeof(app_raw_pubkey), &olen, (const unsigned char *)app_base64_pubkey, strlen(app_base64_pubkey));
    if (ret != 0 || olen != 32) {
        ESP_LOGE(TAG, "Invalid App public key format");
        return ESP_FAIL;
    }

    // 2. [mbedTLS 3.x 공식 API] 앱의 공개키를 Context에 주입
    ret = mbedtls_ecdh_read_public(&g_ble_session.ecdh, app_raw_pubkey, olen);
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to read app public key");
        return ESP_FAIL;
    }

    // 3. [mbedTLS 3.x 공식 API] 공유 비밀(Shared Secret) S 계산
    uint8_t shared_secret[32];
    size_t secret_len = 0;
    ret = mbedtls_ecdh_calc_secret(&g_ble_session.ecdh, &secret_len, shared_secret, sizeof(shared_secret), hw_rng, NULL);
    if (ret != 0 || secret_len != 32) {
        ESP_LOGE(TAG, "Failed to calculate shared secret");
        return ESP_FAIL;
    }

    // 4. HKDF-SHA256(S) 로 AES 세션키 도출
    ret = mbedtls_hkdf(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 
                       NULL, 0,               
                       shared_secret, secret_len,     
                       NULL, 0,               
                       g_ble_session.session_key, 32); 
    
    if (ret != 0) {
        ESP_LOGE(TAG, "HKDF key derivation failed");
        return ESP_FAIL;
    }

    g_ble_session.is_encrypted = true; 
    
    // 핸드셰이크 개인키는 즉시 파기 (Forward Secrecy 보장)
    mbedtls_ecdh_free(&g_ble_session.ecdh); 
    
    ESP_LOGI(TAG, "ECDH Handshake successful. Session Key generated.");
    return ESP_OK;
}

esp_err_t ble_crypto_decrypt(const uint8_t *ciphertext, size_t cipher_len,
                             const uint8_t *iv, const uint8_t *tag,
                             uint8_t *out_plaintext, size_t *out_len) {
    if (!g_ble_session.is_encrypted) {
        ESP_LOGE(TAG, "Session is not encrypted yet");
        return ESP_ERR_INVALID_STATE;
    }

    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);

    int ret = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, g_ble_session.session_key, 256); 
    if (ret != 0) goto exit;

    // AES-256-GCM 복호화 및 인증
    ret = mbedtls_gcm_auth_decrypt(&gcm, cipher_len, 
                                   iv, 12,          
                                   NULL, 0,         
                                   tag, 16,         
                                   ciphertext, out_plaintext);
    
    if (ret == 0) {
        *out_len = cipher_len;
    } else {
        ESP_LOGE(TAG, "Decryption or Authentication failed");
    }

exit:
    mbedtls_gcm_free(&gcm);
    return (ret == 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t ble_crypto_encrypt(const uint8_t *plaintext, size_t plain_len,
                             uint8_t *out_iv, uint8_t *out_tag, uint8_t *out_ciphertext) {
    if (!g_ble_session.is_encrypted) {
        ESP_LOGE(TAG, "Session is not encrypted yet");
        return ESP_ERR_INVALID_STATE;
    }

    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);

    // IV(Nonce) 생성: ESP32 하드웨어 RNG 사용
    esp_fill_random(out_iv, 12); 

    int ret = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, g_ble_session.session_key, 256);
    if (ret != 0) goto exit;

    // AES-256-GCM 암호화 연산
    ret = mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT, plain_len, 
                                    out_iv, 12, NULL, 0, 
                                    plaintext, out_ciphertext, 16, out_tag);
    if (ret != 0) {
        ESP_LOGE(TAG, "Encryption failed");
    }

exit:
    mbedtls_gcm_free(&gcm);
    return (ret == 0) ? ESP_OK : ESP_FAIL;
}