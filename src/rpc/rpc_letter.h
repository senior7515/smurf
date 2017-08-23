// Copyright (c) 2017 Alexander Gallego. All rights reserved.
//
#pragma once

#include <memory>
#include <unordered_map>

#include <core/sstring.hh>
#include <core/temporary_buffer.hh>

#include "flatbuffers/rpc_generated.h"
#include "platform/log.h"
#include "platform/macros.h"
#include "rpc/rpc_header_utils.h"
#include "rpc/rpc_letter_concepts.h"

namespace smf {

// needed for the union
enum rpc_letter_type : uint8_t {
  rpc_letter_type_payload,
  rpc_letter_type_binary
};


struct rpc_letter {
  rpc_letter_type dtype;
  rpc::header     header{};
  // used by HTTP-like payloads. get the builder, set the headers, etc
  // This is the normal use case.
  // eventually this will turn into the `this->body`
  // by the RPC before sending it over the wire
  //
  std::unique_ptr<rpc::payloadT> payload;
  // Used by filters / automation / etc
  // contains ALL the body of the data. That is to say
  // smf.fbs.rpc.Payload data type.
  // including headers, et al
  //
  // Typically only `smf` internals will use this.
  // The receiving end will close/abort/etc the connection if this buffer
  // is not correctly formatted as a smf.fbs.rpc.Payload table.
  //
  seastar::temporary_buffer<char> body;

  rpc_letter();
  rpc_letter &operator=(rpc_letter &&l);
  rpc_letter(rpc_letter &&l);
  ~rpc_letter();
  SMF_DISALLOW_COPY_AND_ASSIGN(rpc_letter);


  void mutate_payload_to_binary();


  // Does 2 copies.
  // First copy:  it converts it into a byte array in flatbuffers-aligned
  // format.
  // Second copy: Next it moves it into the payload
  //
  template <typename RootType>
  requires FlatBuffersNativeTable<RootType> static rpc_letter
  native_table_to_rpc_letter(const typename RootType::NativeTableType &t) {
    auto let = rpc_letter{};
    rpc_letter::serialize_type_into_letter<RootType>(t, &let);
    return std::move(let);
  }

  template <typename RootType>
  requires FlatBuffersNativeTable<RootType> static void
  serialize_type_into_letter(const typename RootType::NativeTableType &t,
                             rpc_letter *                              let) {
    // clean up the builder first
    auto &builder = rpc_letter::local_builder();
    // Might want to keep a moving average of the memory usg
    // so that we can actually reclaim memory. by re-setting it to a smaller
    // buffer
    //
    builder.Clear();
    // first copy into this local_builder
    builder.Finish(RootType::Pack(builder, &t, nullptr));
    // second copy - into user_buf
    let->payload->body.reserve(builder.GetSize());
    const char *p = reinterpret_cast<const char *>(builder.GetBufferPointer());
    std::copy(p, p + builder.GetSize(), std::back_inserter(let->payload->body));
    checksum_rpc_payload(
      let->header, reinterpret_cast<const char *>(let->payload->body.data()),
      let->payload->body.size());
    DLOG_THROW_IF(builder.GetSize() != let->payload->body.size(),
                  "Error coyping types into envelope");
  }

  static flatbuffers::FlatBufferBuilder &local_builder() {
    static thread_local flatbuffers::FlatBufferBuilder fbb{};
    return fbb;
  }
};

}  // namespace smf
