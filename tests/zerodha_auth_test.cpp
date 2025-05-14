#include <iostream>
#include <string>
#include <filesystem>

#include "common/logging.h"
#include "trading/adapters/zerodha/auth/zerodha_authenticator.h"

int main() {
    // Set up logs directory path for Zerodha
    std::filesystem::path logs_dir = "/home/praveen/om/siriquantum/ida/logs/zerodha";
    std::filesystem::create_directories(logs_dir); // Ensure directory exists
    
    // Initialize logger with path to logs directory
    Common::Logger logger((logs_dir / "auth_test.log").string());
    
    try {
        std::cout << "Starting Pure C++ Zerodha Authenticator Test..." << std::endl;
        
        // Create authenticator from JSON configuration
        std::string config_file = "/home/praveen/om/siriquantum/ida/config/trading.json";
        std::cout << "Loading configuration from " << config_file << std::endl;
        
        // Create and load environment config
        Adapter::Zerodha::EnvironmentConfig env_config(&logger, "", config_file);
        if (!env_config.load()) {
            std::cerr << "ERROR: Failed to load configuration from " << config_file << std::endl;
            return 1;
        }
        
        // Create authenticator from config
        Adapter::Zerodha::ZerodhaAuthenticator auth = 
            Adapter::Zerodha::ZerodhaAuthenticator::from_config(&logger, env_config);
        
        std::cout << "Authenticating with Zerodha..." << std::endl;
        
        // Try to authenticate
        std::string token = auth.authenticate();
        
        if (!token.empty()) {
            std::cout << "Authentication successful!" << std::endl;
            std::cout << "Access Token: " << token << std::endl << std::endl;
            std::cout << "SUCCESS: Pure C++ authenticator works without Python dependencies!" << std::endl;
            return 0;
        } else {
            std::cout << "Authentication failed." << std::endl << std::endl;
            std::cout << "ERROR: Test failed" << std::endl;
            return 1;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}