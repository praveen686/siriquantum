#pragma once

#include <sstream>

#include "common/types.h"
#include "common/lf_queue.h"

using namespace Common;

namespace Exchange {
  /// Type of the order response sent by the exchange to the trading client.
  enum class ClientResponseType : uint8_t {
    INVALID = 0,
    ACCEPTED = 1,
    REJECTED = 2,
    CANCELED = 3,
    FILLED = 4,
    CANCEL_REJECTED = 5,
    PARTIALLY_FILLED = 6
  };

  inline std::string clientResponseTypeToString(ClientResponseType type) {
    switch (type) {
      case ClientResponseType::ACCEPTED:
        return "ACCEPTED";
      case ClientResponseType::REJECTED:
        return "REJECTED";
      case ClientResponseType::CANCELED:
        return "CANCELED";
      case ClientResponseType::FILLED:
        return "FILLED";
      case ClientResponseType::PARTIALLY_FILLED:
        return "PARTIALLY_FILLED";
      case ClientResponseType::CANCEL_REJECTED:
        return "CANCEL_REJECTED";
      case ClientResponseType::INVALID:
        return "INVALID";
    }
    return "UNKNOWN";
  }

  /// Reason for client response - e.g. why a particular request was rejected.
  enum class ClientResponseRejectReason : uint8_t {
    INVALID = 0,
    NONE = 1,
    INVALID_QUANTITY = 2,
    INVALID_PRICE = 3,
    INVALID_TICKER = 4,
    INVALID_ORDER_ID = 5,
    DUPLICATE_ORDER_ID = 6,
    RISK_REJECT = 7
  };

  inline std::string clientResponseRejectReasonToString(ClientResponseRejectReason reject_reason) {
    switch (reject_reason) {
      case ClientResponseRejectReason::NONE:
        return "NONE";
      case ClientResponseRejectReason::INVALID_QUANTITY:
        return "INVALID_QUANTITY";
      case ClientResponseRejectReason::INVALID_PRICE:
        return "INVALID_PRICE";
      case ClientResponseRejectReason::INVALID_TICKER:
        return "INVALID_TICKER";
      case ClientResponseRejectReason::INVALID_ORDER_ID:
        return "INVALID_ORDER_ID";
      case ClientResponseRejectReason::DUPLICATE_ORDER_ID:
        return "DUPLICATE_ORDER_ID";
      case ClientResponseRejectReason::RISK_REJECT:
        return "RISK_REJECT";
      case ClientResponseRejectReason::INVALID:
        return "INVALID";
    }
    return "UNKNOWN";
  }

  /// These structures go over the wire / network, so the binary structures are packed to remove system dependent extra padding.
#pragma pack(push, 1)

  /// Client response structure used internally by the matching engine.
  struct MEClientResponse {
    ClientResponseType type_ = ClientResponseType::INVALID;
    ClientResponseRejectReason reject_reason_ = ClientResponseRejectReason::INVALID;

    ClientId client_id_ = ClientId_INVALID;
    TickerId ticker_id_ = TickerId_INVALID;
    OrderId order_id_ = OrderId_INVALID;
    Side side_ = Side::INVALID;
    Price price_ = Price_INVALID;
    Qty exec_qty_ = Qty_INVALID; // Execution or filled quantity.
    Qty leaves_qty_ = Qty_INVALID; // Remaining quantity that has to be filled.

    auto toString() const {
      std::stringstream ss;
      ss << "MEClientResponse"
         << " ["
         << "type:" << clientResponseTypeToString(type_)
         << " reject-reason:" << clientResponseRejectReasonToString(reject_reason_)
         << " client:" << clientIdToString(client_id_)
         << " ticker:" << tickerIdToString(ticker_id_)
         << " oid:" << orderIdToString(order_id_)
         << " side:" << sideToString(side_)
         << " exec-qty:" << qtyToString(exec_qty_)
         << " leaves-qty:" << qtyToString(leaves_qty_)
         << " price:" << priceToString(price_)
         << "]";
      return ss.str();
    }
  };

  /// Client response structure published over the network by the order server.
  struct OSClientResponse {
    size_t seq_num_ = 0;
    MEClientResponse me_client_response_;

    auto toString() const {
      std::stringstream ss;
      ss << "OSClientResponse"
         << " ["
         << "seq:" << seq_num_
         << " " << me_client_response_.toString()
         << "]";
      return ss.str();
    }
  };

#pragma pack(pop) // Undo the packed binary structure directive moving forward.

  /// Lock free queues of matching engine client response messages.
  typedef LFQueue<MEClientResponse> ClientResponseLFQueue;
}