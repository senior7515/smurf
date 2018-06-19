// Copyright (c) 2016 Alexander Gallego. All rights reserved.
//
#pragma once
#include <utility>
// seastar
#include <core/iostream.hh>
#include <core/shared_ptr.hh>
#include <net/api.hh>

#include "smf/macros.h"
#include "smf/rpc_connection_limits.h"

namespace smf {

/// \brief this class is called a *lot*
/// to test if we should keep consuming or not.
/// Do not make virtual. Instead use type embedding.
/// Currently only the rpc_server_connection is used - instead of doing
/// seastar::shared_ptr we can use seastar::lw_shared_ptr and have
/// effectively no cost of pointer ownership.
///
class rpc_connection final {
 public:
  explicit rpc_connection(seastar::connected_socket fd,
    seastar::socket_address address,
    seastar::lw_shared_ptr<rpc_connection_limits> conn_limits = nullptr)
    : socket(std::move(fd))
    , remote_address(address)
    , istream(socket.input())
    , ostream(socket.output())
    , limits(conn_limits) {}

  seastar::connected_socket socket;
  const seastar::socket_address remote_address;
  seastar::input_stream<char> istream;
  seastar::output_stream<char> ostream;
  seastar::lw_shared_ptr<rpc_connection_limits> limits;
  uint32_t istream_active_parser{0};

  inline void
  disable() {
    enabled_ = false;
  }
  inline bool
  is_enabled() const {
    return enabled_;
  }
  inline bool
  is_valid() {
    return !istream.eof() && !has_error() && enabled_;
  }
  inline bool
  has_error() const {
    return error_.operator bool();
  }
  inline void
  set_error(const char *e) {
    error_ = seastar::sstring(e);
  }

  inline seastar::sstring
  get_error() const {
    return error_.value();
  }

  ~rpc_connection() {}

  SMF_DISALLOW_COPY_AND_ASSIGN(rpc_connection);

 private:
  std::experimental::optional<seastar::sstring> error_;
  bool enabled_{true};
};
}  // namespace smf
