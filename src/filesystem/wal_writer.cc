// Copyright (c) 2016 Alexander Gallego. All rights reserved.
//
// source header
#include "filesystem/wal_writer.h"
#include <algorithm>
#include <memory>
#include <utility>
// third party
#include <core/reactor.hh>
// smf
#include "filesystem/wal_head_file_functor.h"
#include "filesystem/wal_name_extractor_utils.h"
#include "filesystem/wal_writer_node.h"
#include "filesystem/wal_writer_utils.h"
#include "platform/log.h"
#include "seastar_io/priority_manager.h"
#include "utils/time_utils.h"

namespace smf {

wal_writer::wal_writer(seastar::sstring topic_dir, uint32_t topic_partition)
  : directory(topic_dir), partition(topic_partition) {}

wal_writer::wal_writer(wal_writer &&o) noexcept
  : directory(std::move(o.directory))
  , partition(std::move(o.partition))
  , writer_(std::move(o.writer_)) {}

seastar::sstring wal_writer::work_directory() const {
  return directory + "." + seastar::to_sstring(partition);
}

seastar::future<> wal_writer::close() {
  if (writer_) { return writer_->close(); }
  // writer can be null if after creation, but before open()
  // is called there is a system_error or an exception
  // so we never call open. We still need to wind down and close()
  // or attempt to close all
  return seastar::make_ready_future<>();
}

seastar::future<> wal_writer::open_empty_dir(seastar::sstring prefix) {
  auto                 id = seastar::to_sstring(seastar::engine().cpu_id());
  wal_writer_node_opts wo;
  wo.wstats = wstats_;
  wo.prefix = prefix + id;
  writer_   = std::make_unique<wal_writer_node>(std::move(wo));
  return writer_->open();
}

seastar::future<> wal_writer::open_non_empty_dir(seastar::sstring last_file,
                                                 seastar::sstring prefix) {
  auto epoch = wal_name_extractor_utils::extract_epoch(last_file);
  DLOG_TRACE("epoch extracted: {}, filename:{}", epoch, last_file);
  return seastar::open_file_dma(last_file, seastar::open_flags::ro)
    .then([this, prefix, epoch](seastar::file f) {
      auto last = seastar::make_lw_shared<seastar::file>(std::move(f));
      return last->size().then([this, prefix, last, epoch](uint64_t size) {
        auto id = seastar::to_sstring(seastar::engine().cpu_id());
        wal_writer_node_opts wo;
        wo.wstats = wstats_;
        wo.prefix = prefix + id;
        wo.epoch  = epoch + size;
        writer_   = std::make_unique<wal_writer_node>(std::move(wo));
        return last->close().then([last, this] { return writer_->open(); });
      });
    });
}

seastar::future<> wal_writer::do_open() {
  return seastar::open_directory(work_directory())
    .then([this](seastar::file f) {
      auto l = seastar::make_lw_shared<wal_head_file_max_functor>(std::move(f));
      return l->done().then([l, this]() {
        seastar::sstring run_prefix =
          seastar::to_sstring(time_now_micros()) + ":";
        if (l->last_file.empty()) { return open_empty_dir(run_prefix); }
        return open_non_empty_dir(l->last_file, run_prefix);
      });
    });
}

seastar::future<> wal_writer::open() {
  return seastar::file_exists(work_directory()).then([this](bool dir_exists) {
    if (dir_exists) { return do_open(); }
    return seastar::make_directory(work_directory()).then([this] {
      return do_open();
    });
  });
}

seastar::future<wal_write_reply> wal_writer::append(
  seastar::lw_shared_ptr<wal_write_projection> projection) {
  return writer_->append(std::move(req));
}


}  // namespace smf
