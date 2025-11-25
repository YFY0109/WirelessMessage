/**
 * @file test_encryption.cpp
 * @brief 测试加密功能的独立程序
 */

#include <Arduino.h>
#include <WiFi.h>
#include "mbedtls/aes.h"
#include "mbedtls/md5.h"

// 简单的加密测试
void setup()
{
    Serial.begin(115200);
    delay(2000);

    Serial.println("=== Encryption Test ===");

    // 测试AES加密
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);

    uint8_t key[16] = {'T', 'e', 's', 't', 'K', 'e', 'y', '1', '2', '3', '4', '5', '6', '7', '8', '9'};
    uint8_t input[16] = {'H', 'e', 'l', 'l', 'o', ' ', 'W', 'o', 'r', 'l', 'd', '!', ' ', ' ', ' ', ' '};
    uint8_t output[16];

    // 设置密钥并加密
    int ret = mbedtls_aes_setkey_enc(&aes, key, 128);
    if (ret == 0)
    {
        ret = mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, input, output);
        if (ret == 0)
        {
            Serial.println("AES Encryption: SUCCESS");
        }
        else
        {
            Serial.println("AES Encryption: FAILED");
        }
    }
    else
    {
        Serial.println("AES Key Setup: FAILED");
    }

    mbedtls_aes_free(&aes);

    // 测试MD5哈希
    mbedtls_md5_context md5_ctx;
    uint8_t hash[16];
    String testData = "Test Data";

    mbedtls_md5_init(&md5_ctx);
    mbedtls_md5_starts(&md5_ctx);
    mbedtls_md5_update(&md5_ctx, (const unsigned char *)testData.c_str(), testData.length());
    mbedtls_md5_finish(&md5_ctx, hash);
    mbedtls_md5_free(&md5_ctx);

    Serial.print("MD5 Hash: ");
    for (int i = 0; i < 16; i++)
    {
        if (hash[i] < 16)
            Serial.print("0");
        Serial.print(hash[i], HEX);
    }
    Serial.println();

    // 测试WiFi MAC地址
    uint8_t mac[6];
    WiFi.macAddress(mac);
    Serial.print("MAC Address: ");
    for (int i = 0; i < 6; i++)
    {
        if (mac[i] < 16)
            Serial.print("0");
        Serial.print(mac[i], HEX);
        if (i < 5)
            Serial.print(":");
    }
    Serial.println();

    Serial.println("=== Test Complete ===");
}

void loop()
{
    // 空循环
    delay(1000);
}