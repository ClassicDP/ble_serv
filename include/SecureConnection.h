#ifndef SecureConnection_H
#define SecureConnection_H

#include <Arduino.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/pk.h>
#include <mbedtls/rsa.h>
#include <mbedtls/md.h>
#include <mbedtls/error.h>
#include "aes.hpp"  // include the TinyAES header
#include <unordered_map>
#include <vector>
#include <string>

class SecureConnection {
public:
    SecureConnection() {
        mbedtls_entropy_init(&entropy);
        mbedtls_ctr_drbg_init(&ctr_drbg);
        const char *pers = "gen_key";
        mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, (const unsigned char *) pers, strlen(pers));
    }

    std::string generateRandomField()
    {
        std::string result;
        int size = 8 + random(24);
        for (int i =0; i < size; i++)
        {
            result += (char)(random(90)+32);
        }
        return result;
    }

    static std::string str2hex (std::string input)
    {
        const char* hexDigits = "0123456789abcdef";
        std::string output;
        output.reserve(input.size() * 2); // Reserve space for the output string

        for (unsigned char c : input) {
            output.push_back(hexDigits[c >> 4]); // Get the upper 4 bits
            output.push_back(hexDigits[c & 0x0F]); // Get the lower 4 bits
        }
        return output;    
    }
    static std::string hex2str (std::string input)
    {
        std::string output;
        output.reserve(input.size() / 2); // Reserve space for the output string

        for (size_t i = 0; i < input.size(); i += 2) {
            char highNibble = input[i];
            char lowNibble = input[i + 1];

            // Convert hex characters to their numeric values
            highNibble = (highNibble > '9') ? (highNibble - 'a' + 10) : (highNibble - '0');
            lowNibble = (lowNibble > '9') ? (lowNibble - 'a' + 10) : (lowNibble - '0');

            output.push_back((highNibble << 4) | lowNibble); // Combine the two nibbles
        }
        return output;        
    }
    void generateRSAKeys(const std::string& uuid) {
        mbedtls_pk_context pk;
        mbedtls_pk_init(&pk);
        mbedtls_pk_setup(&pk, mbedtls_pk_info_from_type(MBEDTLS_PK_RSA));
        mbedtls_rsa_gen_key(mbedtls_pk_rsa(pk), mbedtls_ctr_drbg_random, &ctr_drbg, 2048, 65537);

        unsigned char publicKey[1600];
        unsigned char privateKey[3200]; // Увеличим размер буфера для приватного ключа
        size_t len = sizeof(publicKey);
        mbedtls_pk_write_pubkey_pem(&pk, publicKey, len);

        len = sizeof(privateKey);
        mbedtls_pk_write_key_pem(&pk, privateKey, len);

        keys[uuid] = {std::vector<uint8_t>(publicKey, publicKey + strlen((char*)publicKey)),
                      std::vector<uint8_t>(privateKey, privateKey + strlen((char*)privateKey))};
        mbedtls_pk_free(&pk);

        // Debug: print partial keys
        Serial.print("Public Key (partial): ");
        Serial.write((char*)publicKey, 100); // print first 100 characters
        Serial.println();
        Serial.print("Private Key (partial): ");
        Serial.write((char*)privateKey, 100); // print first 100 characters
        Serial.println();
    }

    void generateAESKey(const std::string& uuid) {
        uint8_t key[16];
        mbedtls_ctr_drbg_random(&ctr_drbg, key, sizeof(key));
        aesKeys[uuid] = std::vector<uint8_t>(key, key + sizeof(key));
        Serial.print("Generated AES Key: ");
        printHex(aesKeys[uuid]);
    }

    std::string encryptMessageAES(const std::string& message, const std::string& uuid) {
        if (aesKeys.find(uuid) == aesKeys.end()) {
            return "Key not found";
        }

        uint8_t iv[16];
        mbedtls_ctr_drbg_random(&ctr_drbg, iv, sizeof(iv));

        AES_ctx ctx{};
        AES_init_ctx_iv(&ctx, aesKeys[uuid].data(), iv);

        std::vector<uint8_t> buffer(message.begin(), message.end());
        size_t padding_len = 16 - (buffer.size() % 16);
        buffer.insert(buffer.end(), padding_len, static_cast<uint8_t>(padding_len));

        AES_CBC_encrypt_buffer(&ctx, buffer.data(), buffer.size());

        std::string encryptedMessage(reinterpret_cast<char*>(iv), sizeof(iv));
        encryptedMessage.append(reinterpret_cast<char*>(buffer.data()), buffer.size());

        Serial.print("IV for AES: ");
        printHex(std::vector<uint8_t>(iv, iv + sizeof(iv)));
        Serial.print("Encrypted Message (partial): ");
        printHex(std::vector<uint8_t>(buffer.begin(), buffer.begin() + 16)); // print first 16 bytes

        return encryptedMessage;
    }

    std::string decryptMessageAES(const std::string& encryptedMessage, const std::string& uuid) {
        if (aesKeys.find(uuid) == aesKeys.end()) {
            return "Key not found";
        }

        uint8_t iv[16];
        memcpy(iv, encryptedMessage.data(), 16);

        AES_ctx ctx;
        AES_init_ctx_iv(&ctx, aesKeys[uuid].data(), iv);

        std::vector<uint8_t> buffer(encryptedMessage.begin() + 16, encryptedMessage.end());

        AES_CBC_decrypt_buffer(&ctx, buffer.data(), buffer.size());

        size_t padding_len = buffer.back();
        if (padding_len == 0 || padding_len > 16) {
            return "Decryption padding is incorrect";
        }
        buffer.resize(buffer.size() - padding_len);

        Serial.print("Decrypted Message (partial): ");
        printHex(std::vector<uint8_t>(buffer.begin(), buffer.begin() + 16)); // print first 16 bytes

        return {std::string(buffer.begin(), buffer.end())};
    }

    std::string encryptMessageRSA(const std::vector<uint8_t>& message, const std::string& uuid) {
        if (keys.find(uuid) == keys.end()) {
            return "Key not found";
        }

        mbedtls_pk_context pk;
        mbedtls_pk_init(&pk);
        std::string publicKeyStr = vectorToString(keys[uuid].first);
        int ret = mbedtls_pk_parse_public_key(&pk, reinterpret_cast<const unsigned char*>(publicKeyStr.c_str()), publicKeyStr.length() + 1);
        if (ret != 0) {
            mbedtls_pk_free(&pk);
            char error_buf[100];
            mbedtls_strerror(ret, error_buf, 100);
            return "Public key parse failed: " + std::string(error_buf);
        }

        std::vector<uint8_t> output(512);
        size_t output_len;
        ret = mbedtls_pk_encrypt(&pk, message.data(), message.size(), output.data(), &output_len, output.size(), mbedtls_ctr_drbg_random, &ctr_drbg);
        if (ret != 0) {
            mbedtls_pk_free(&pk);
            char error_buf[100];
            mbedtls_strerror(ret, error_buf, 100);
            return "Encryption failed: " + std::string(error_buf);
        }

        mbedtls_pk_free(&pk);

        Serial.print("Encrypted RSA Message (partial): ");
        printHex(std::vector<uint8_t>(output.begin(), output.begin() + 16)); // print first 16 bytes

        return {std::string(reinterpret_cast<char *>(output.data()), output_len)};
    }

    std::vector<uint8_t> decryptMessageRSA(const std::string& encryptedMessage, const std::string& uuid) {
        if (keys.find(uuid) == keys.end()) {
            return stringToVector("Key not found");
        }

        mbedtls_pk_context pk;
        mbedtls_pk_init(&pk);
        std::string privateKeyStr = vectorToString(keys[uuid].second);
        int ret = mbedtls_pk_parse_key(&pk, reinterpret_cast<const unsigned char*>(privateKeyStr.c_str()), privateKeyStr.length() + 1, nullptr, 0);
        if (ret != 0) {
            mbedtls_pk_free(&pk);
            char error_buf[100];
            mbedtls_strerror(ret, error_buf, 100);
            return stringToVector("Private key parse failed: " + std::string(error_buf));
        }

        std::vector<uint8_t> output(512);
        size_t output_len;
        ret = mbedtls_pk_decrypt(&pk, (const unsigned char*)encryptedMessage.c_str(), encryptedMessage.length(), output.data(), &output_len, output.size(), mbedtls_ctr_drbg_random, &ctr_drbg);
        if (ret != 0) {
            mbedtls_pk_free(&pk);
            char error_buf[100];
            mbedtls_strerror(ret, error_buf, 100);
            return stringToVector("Decryption failed: " + std::string(error_buf));
        }

        mbedtls_pk_free(&pk);

        // Ensure the length is correct for the AES key
        if (output_len != 16) {
            return stringToVector("Decryption failed: incorrect AES key length");
        }

        Serial.print("Decrypted RSA Message (partial): ");
        printHex(std::vector<uint8_t>(output.begin(), output.begin() + 16)); // print first 16 bytes

        return {std::vector<uint8_t>(output.begin(), output.begin() + output_len)};
    }

    static std::string generatePublicKeyHash(const std::vector<uint8_t>& publicKey, size_t bitLength) {
        unsigned char hash[32];
        mbedtls_md_context_t md_ctx;
        mbedtls_md_init(&md_ctx);
        mbedtls_md_setup(&md_ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 0);
        mbedtls_md_starts(&md_ctx);
        mbedtls_md_update(&md_ctx, publicKey.data(), publicKey.size());
        mbedtls_md_finish(&md_ctx, hash);
        mbedtls_md_free(&md_ctx);

        std::string binaryHash;
        for (size_t i = 0; i < bitLength; ++i) {
            size_t byteIndex = i / 8;
            size_t bitIndex = i % 8;
            binaryHash += ((hash[byteIndex] >> (7 - bitIndex)) & 1) ? '1' : '0';
        }

        return binaryHash;
    }

    static std::string vectorToString(const std::vector<uint8_t>& vec) {
        return {std::string(vec.begin(), vec.end())};
    }

    static std::vector<uint8_t> stringToVector(const std::string& str) {
        return {std::vector<uint8_t>(str.begin(), str.end())};
    }

    static void printHex(const std::vector<uint8_t>& vec) {
        for (size_t i = 0; i < vec.size() && i < 16; ++i) { // limit output to first 16 bytes
            if (vec[i] < 0x10) {
                Serial.print("0");
            }
            Serial.print(vec[i], HEX);
        }
        Serial.println();
    }

    std::unordered_map<std::string, std::pair<std::vector<uint8_t>, std::vector<uint8_t>>> keys;
    std::unordered_map<std::string, std::vector<uint8_t>> aesKeys;

private:
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
};

#endif
