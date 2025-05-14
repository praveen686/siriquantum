# Zerodha Authentication

This directory contains components for authenticating with the Zerodha API using a pure C++ implementation.

## Components

- `zerodha_authenticator.h/cpp` - C++ authenticator for Zerodha API
- `totp.h` - Time-based One-Time Password (TOTP) implementation in C++

## Authentication Flow

The authentication flow is fully implemented in C++ and follows these steps:

1. Check for cached token and use it if valid
2. If no valid cached token exists:
   - Generate a request token by automating the login process:
     - Login with user credentials
     - Submit TOTP code for two-factor authentication
     - Follow the redirect to extract request token
   - Convert the request token to an access token
   - Cache the access token for future use

## Usage

### Environment Variables

The authenticator expects the following environment variables to be set in a `.env` file:

```
ZKITE_API_KEY=your_api_key
ZKITE_API_SECRET=your_api_secret
ZKITE_USER_ID=your_user_id
ZKITE_PWD=your_password
ZKITE_TOTP_SECRET=your_totp_secret
```

### Using the C++ Authenticator

```cpp
#include "adapters/zerodha/auth/zerodha_authenticator.h"

// Create a logger
Common::Logger logger("my_app.log");

// Create the authenticator from environment variables
auto auth = Adapter::Zerodha::ZerodhaAuthenticator::from_env(&logger);

// Authenticate with Zerodha
std::string token = auth.authenticate();
if (!token.empty()) {
    // Use the token for API calls
}

// Check if we have a valid token
if (auth.is_authenticated()) {
    // Token is valid
}

// Force refresh a token
token = auth.refresh_session();
```

## Testing

A test program is provided in the `tests` directory:

```sh
# Run the test
./scripts/test_zerodha_auth.sh
```

## Dependencies

- C++ libraries: 
  - libcurl for HTTP requests
  - OpenSSL for cryptographic operations
  - nlohmann_json for JSON parsing