#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <openssl/hmac.h>

namespace Adapter {
namespace Zerodha {

/**
 * @class TOTP
 * @brief Time-based One-Time Password implementation (RFC 6238)
 * 
 * This class provides TOTP generation for two-factor authentication
 * without requiring external Python libraries.
 */
class TOTP {
public:
    /**
     * @brief Constructor
     * @param secret Base32 encoded secret key
     * @param digits Number of digits in the generated code (default: 6)
     * @param period Time step in seconds (default: 30)
     * @param algorithm Hash algorithm to use (default: SHA1)
     */
    TOTP(const std::string& secret, int digits = 6, int period = 30)
        : secret_(secret), digits_(digits), period_(period) {}
    
    /**
     * @brief Generate the current TOTP code
     * @return The generated TOTP code
     */
    std::string now() const {
        // Get current time
        const auto now = std::chrono::system_clock::now();
        return generate_code(now);
    }
    
    /**
     * @brief Generate a TOTP code for a specific time
     * @param time Time to generate the code for
     * @return The generated TOTP code
     */
    std::string generate_code(const std::chrono::system_clock::time_point& time) const {
        // Convert time to counter value (seconds since epoch / period)
        uint64_t counter = std::chrono::duration_cast<std::chrono::seconds>(
            time.time_since_epoch()).count() / period_;
        
        return generate_code_from_counter(counter);
    }
    
private:
    std::string secret_;
    int digits_;
    int period_;
    
    /**
     * @brief Generate a TOTP code from a counter value
     * @param counter Counter value
     * @return The generated TOTP code
     */
    std::string generate_code_from_counter(uint64_t counter) const {
        // Decode the base32 secret
        std::vector<uint8_t> key = base32_decode(secret_);
        if (key.empty()) {
            throw std::runtime_error("Invalid base32 secret key");
        }
        
        // Convert counter to big-endian bytes
        unsigned char counter_bytes[8] = {0};
        for (int i = 7; i >= 0; i--) {
            counter_bytes[i] = counter & 0xFF;
            counter >>= 8;
        }
        
        // Compute HMAC-SHA1
        unsigned char hmac[EVP_MAX_MD_SIZE];
        unsigned int hmac_len;
        HMAC(EVP_sha1(), key.data(), key.size(), counter_bytes, 8, hmac, &hmac_len);
        
        // Dynamic truncation
        int offset = hmac[hmac_len - 1] & 0x0F;
        int binary = ((hmac[offset] & 0x7F) << 24) |
                    ((hmac[offset + 1] & 0xFF) << 16) |
                    ((hmac[offset + 2] & 0xFF) << 8) |
                    (hmac[offset + 3] & 0xFF);
        
        // Generate N-digit code
        int code = binary % static_cast<int>(std::pow(10, digits_));
        
        // Format with leading zeros
        std::stringstream ss;
        ss << std::setw(digits_) << std::setfill('0') << code;
        return ss.str();
    }
    
    /**
     * @brief Decode a base32 encoded string
     * @param base32_str Base32 encoded string
     * @return Decoded bytes
     */
    std::vector<uint8_t> base32_decode(const std::string& base32_str) const {
        // Define the Base32 character set (RFC 4648)
        static const std::string base32_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
        
        // Clean up the input string (remove spaces, convert to uppercase)
        std::string input = base32_str;
        input.erase(std::remove_if(input.begin(), input.end(), ::isspace), input.end());
        std::transform(input.begin(), input.end(), input.begin(), ::toupper);
        
        // Add padding if needed
        while (input.size() % 8 != 0) {
            input += '=';
        }
        
        // Prepare output buffer
        std::vector<uint8_t> result;
        result.reserve(input.size() * 5 / 8);
        
        // Process all characters
        uint64_t buffer = 0;
        int bits_in_buffer = 0;
        
        for (char c : input) {
            // Skip padding characters
            if (c == '=') {
                continue;
            }
            
            // Convert character to 5-bit value
            size_t val = base32_chars.find(c);
            if (val == std::string::npos) {
                // Invalid character
                return std::vector<uint8_t>();
            }
            
            // Add 5 bits to the buffer
            buffer = (buffer << 5) | val;
            bits_in_buffer += 5;
            
            // Process complete bytes
            while (bits_in_buffer >= 8) {
                bits_in_buffer -= 8;
                result.push_back((buffer >> bits_in_buffer) & 0xFF);
            }
        }
        
        return result;
    }
};

} // namespace Zerodha
} // namespace Adapter