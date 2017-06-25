// Copyright (c) 2016 Alexander Gallego. All rights reserved.
//
// std
#include <chrono>
#include <iostream>
// seastar
#include <core/app-template.hh>
#include <core/distributed.hh>
#include <core/sleep.hh>
#include <net/api.hh>
// smf
#include "flatbuffers/rpc_generated.h"
#include "platform/log.h"
#include "rpc/rpc_handle_router.h"
#include "rpc/rpc_recv_context.h"
#include "rpc/rpc_server.h"
#include "utils/random.h"

// templates
#include "flatbuffers/demo_service.smf.fb.h"

class storage_service : public smf_gen::demo::SmfStorage {
  virtual seastar::future<smf::rpc_typed_envelope<smf_gen::demo::Response>> Get(
    smf::rpc_recv_typed_context<smf_gen::demo::Request> &&rec) final {
    smf::rpc_typed_envelope<smf_gen::demo::Response> data;
    data.envelope.set_status(200);
    return seastar::
      make_ready_future<smf::rpc_typed_envelope<smf_gen::demo::Response>>(
        std::move(data));
  }
};


int main(int args, char **argv, char **env) {
  DLOG_DEBUG("About to start the RPC test");
  seastar::distributed<smf::rpc_server> rpc;

  seastar::app_template app;

  smf::random rand;
  uint16_t    random_port = rand.next() % std::numeric_limits<uint16_t>::max();
  return app.run(args, argv, [&]() -> seastar::future<int> {
    DLOG_DEBUG("Setting up at_exit hooks");
    seastar::engine().at_exit([&] { return rpc.stop(); });

    smf::random          r;
    smf::rpc_server_args sargs;
    sargs.rpc_port     = random_port;
    sargs.recv_timeout = std::chrono::milliseconds(1);  // immediately
    return rpc.start(sargs)
      .then([&rpc] {
        return rpc.invoke_on_all(
          &smf::rpc_server::register_service<storage_service>);
      })
      .then([&rpc] {
        DLOG_DEBUG("Invoking rpc start on all cores");
        return rpc.invoke_on_all(&smf::rpc_server::start);
      })
      .then([&random_port] {
        DLOG_DEBUG("Sening only header ane expecting timeout");
        auto local =
          seastar::socket_address(sockaddr_in{AF_INET, INADDR_ANY, {0}});

        return seastar::engine()
          .net()
          .connect(seastar::make_ipv4_address(random_port), local,
                   seastar::transport::TCP)
          .then([](auto skt) {
            auto conn =
              seastar::make_lw_shared<smf::rpc_connection>(std::move(skt));

            uint32_t kHeaderSize = sizeof(smf::fbs::rpc::Header);

            smf::fbs::rpc::Header header(300 /* 300 bytes*/,
                                         smf::fbs::rpc::Flags::Flags_NONE,
                                         1234234 /*random checksum*/);
            seastar::temporary_buffer<char> header_buf(kHeaderSize);
            std::copy(reinterpret_cast<char *>(&header),
                      reinterpret_cast<char *>(&header) + kHeaderSize,
                      header_buf.get_write());
            return conn->ostream.write(std::move(header_buf))
              .then([conn] { return conn->ostream.flush(); })
              .then([conn] {
                // NOTE: This test will block forever until there is an
                // exception with a timeout!

                // block reading the input
                return smf::rpc_recv_context::parse(conn.get(), nullptr)
                  .then(
                    [conn](auto x) { return seastar::make_ready_future<>(); });
              })
              .finally([conn] {});
          });
      })
      .then([] {
        DLOG_DEBUG("Exiting");
        return seastar::make_ready_future<int>(0);
      });
  });
}
