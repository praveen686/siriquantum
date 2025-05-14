# Scripts

This directory contains build and test scripts for the IDA Trading System.

## Available Scripts

### build.sh

Builds the entire project using CMake.

**Usage:**
```bash
./scripts/build.sh
```

**What it does:**
- Creates a `build` directory if it doesn't exist
- Runs CMake and make to build all components
- Uses parallel build to speed up compilation (-j option)

### test_zerodha_auth.sh

Builds and tests the Zerodha authentication module.

**Usage:**
```bash
./scripts/test_zerodha_auth.sh
```

**What it does:**
- Builds the project using CMake and make
- Runs the Zerodha authenticator test executable
- Reports success or failure based on test results

### test_zerodha_market_data.sh

Builds and tests the Zerodha market data adapter. Can use either environment variables or a JSON configuration file.

**Usage:**
```bash
# Run with environment variables for configuration
./scripts/test_zerodha_market_data.sh

# Run with a JSON configuration file
./scripts/test_zerodha_market_data.sh /path/to/config.json
```

**What it does:**
- Builds the project using the build.sh script
- Runs the Zerodha market data test executable
- Can use either environment variables or a specified JSON config file

### test_zerodha_order_book.sh

Builds and tests the Zerodha order book implementation.

**Usage:**
```bash
./scripts/test_zerodha_order_book.sh
```

**What it does:**
- Creates build directory if needed
- Builds the zerodha_order_book_test executable
- Sets ZKITE_LOG_LEVEL to "DEBUG"
- Runs the test executable
- The test continuously receives and processes market data until interrupted (Ctrl+C)

## Test Configuration

### Environment Variables

The test scripts use environment variables for configuration. Key variables include:

```
ZKITE_API_KEY=your_api_key
ZKITE_API_SECRET=your_api_secret
ZKITE_USER_ID=your_user_id
ZKITE_TOTP_SECRET=your_totp_secret
ZKITE_LOG_LEVEL=DEBUG
```

### JSON Configuration

Alternatively, some tests can use a JSON configuration file:

```
/home/praveen/om/siriquantum/ida/config/trading.json
```

The JSON file contains exchange credentials, trading parameters, and instrument configurations.

## Log Files

Test logs are written to the `/home/praveen/om/siriquantum/ida/logs/` directory:

- `zerodha_auth_test.log`
- `zerodha_market_data_test.log`
- `zerodha_order_book_test.log`

## Common Issues

1. **Missing Configuration**: Ensure either environment variables are set or the JSON configuration file exists.

2. **Build Errors**: Make sure all dependencies are installed. If you encounter build errors, check the CMake output.

3. **Authentication Failures**: Verify that your API credentials are correct and that your TOTP secret is valid.

4. **WebSocket Connection Issues**: Ensure you have internet connectivity and that the Zerodha API services are online.