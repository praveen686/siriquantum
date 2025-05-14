# Configuration

This directory contains configuration files for the IDA Trading System.

## Files

- `trading.template.json` - Template file showing the structure of the trading configuration
- `trading.json` - Actual configuration file used by the system (not checked into git for security reasons)

## Setup

To set up the configuration:

1. Copy the template file to create your own configuration:
   ```bash
   cp trading.template.json trading.json
   ```

2. Edit `trading.json` to add your API credentials and other configuration details:
   - Add your Zerodha API credentials
   - Add your Binance API credentials (if using Binance)
   - Set the appropriate cache directory paths
   - Configure your trading strategy parameters
   - Set up risk management parameters

## Configuration Structure

### Trading System
- `trading_mode`: "PAPER" or "LIVE"
- `active_exchange`: Which exchange to use ("ZERODHA" or "BINANCE")
- `strategy`: Trading strategy configuration

### Exchange Configuration
- `api_credentials`: API keys and secrets for the exchange
- `cache_config`: Paths for caching instrument data and tokens
- `symbol_config`: Exchange-specific symbol configuration
- `websocket_config`: WebSocket connection parameters
- `paper_trading`: Simulation parameters for paper trading

### Instruments
Array of instruments to trade with their parameters:
- `symbol`: Trading symbol
- `exchange`: Exchange identifier
- `ticker_id`: Internal identifier
- `is_futures`: Whether this is a futures contract
- `clip`: Standard trade size
- `threshold`: Signal strength threshold
- `max_position`: Maximum position size
- `max_loss`: Maximum loss per instrument

### Risk Management
- `max_daily_loss`: Maximum daily loss limit
- `max_position_value`: Maximum total position value
- `enforce_circuit_limits`: Whether to enforce exchange circuit limits
- `enforce_trading_hours`: Whether to enforce trading hour restrictions