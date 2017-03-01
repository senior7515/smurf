// Copyright (c) 2016 Alexander Gallego. All rights reserved.
//
#pragma once

#include <experimental/optional>
#include <memory>
#include <utility>

#include <boost/filesystem.hpp>
#include <core/temporary_buffer.hh>

#include "filesystem/wal_writer_utils.h"
#include "histogram/histogram.h"

namespace smf {

struct reader_stats {
  reader_stats() {}
  reader_stats(reader_stats &&o) noexcept : total_reads(o.total_reads),
                                            total_bytes(o.total_bytes),
                                            total_flushes(o.total_flushes),
                                            hist(std::move(o.hist)) {}
  reader_stats(const reader_stats &o)
    : total_reads(o.total_reads)
    , total_bytes(o.total_bytes)
    , total_flushes(o.total_flushes) {
    hist = std::make_unique<histogram>();
    *hist += *o.hist;
  }
  reader_stats &operator+=(const reader_stats &o) noexcept {
    total_reads += o.total_reads;
    total_bytes += o.total_bytes;
    total_flushes += o.total_flushes;
    *hist += *o.hist;
    return *this;
  }
  reader_stats &operator=(const reader_stats &o) {
    reader_stats r(o);
    std::swap(r, *this);
    return *this;
  }

  uint64_t                        total_reads{0};
  uint64_t                        total_bytes{0};
  uint64_t                        total_flushes{0};
  std::unique_ptr<smf::histogram> hist = std::make_unique<histogram>();
};

struct writer_stats {
  writer_stats() {}
  writer_stats(writer_stats &&o) noexcept
    : total_writes(o.total_writes),
      total_bytes(o.total_bytes),
      total_invalidations(o.total_invalidations),
      hist(std::move(o.hist)) {}
  writer_stats(const writer_stats &o)
    : total_writes(o.total_writes)
    , total_bytes(o.total_bytes)
    , total_invalidations(o.total_invalidations) {
    hist = std::make_unique<histogram>();
    *hist += *o.hist;
  }
  writer_stats &operator+=(const writer_stats &o) noexcept {
    total_writes += o.total_writes;
    total_bytes += o.total_bytes;
    total_invalidations += o.total_invalidations;
    *hist += *o.hist;
    return *this;
  }
  writer_stats &operator=(const writer_stats &o) {
    writer_stats w(o);
    std::swap(w, *this);
    return *this;
  }
  uint64_t                        total_writes{0};
  uint64_t                        total_bytes{0};
  uint64_t                        total_invalidations{0};
  std::unique_ptr<smf::histogram> hist = std::make_unique<histogram>();
};

struct cache_stats {
  uint64_t total_reads{0};
  uint64_t total_writes{0};
  uint64_t total_invalidations{0};

  uint64_t total_bytes_written{0};
  uint64_t total_bytes_read{0};
  uint64_t total_bytes_invalidated{0};


  cache_stats &operator+=(const cache_stats &o) noexcept {
    total_reads += o.total_reads;
    total_writes += o.total_writes;
    total_invalidations += o.total_invalidations;

    total_bytes_written += o.total_bytes_written;
    total_bytes_read += o.total_bytes_read;
    total_bytes_invalidated += o.total_bytes_invalidated;

    return *this;
  }
};

// this should probably be a sharded<wal_otps> &
// like the tcp server no?
struct wal_opts {
  static sstring canonical_dir(const sstring &directory) {
    return boost::filesystem::canonical(directory.c_str()).string();
  }

  explicit wal_opts(sstring log_directory) : directory(log_directory) {}
  wal_opts(wal_opts &&o) noexcept : directory(std::move(o.directory)),
                                    cache_size(o.cache_size),
                                    rstats(std::move(o.rstats)),
                                    wstats(std::move(o.wstats)),
                                    cstats(std::move(o.cstats)) {}
  wal_opts(const wal_opts &o)
    : directory(canonical_dir(o.directory))
    , cache_size(o.cache_size)
    , rstats(o.rstats)
    , wstats(o.wstats)
    , cstats(o.cstats) {}
  wal_opts &operator=(const wal_opts &o) {
    wal_opts wo(o);
    std::swap(wo, *this);
    return *this;
  }
  const sstring  directory;
  const uint64_t cache_size = wal_file_size_aligned() * 2;
  reader_stats   rstats;
  writer_stats   wstats;
  cache_stats    cstats;
};

}  // namespace smf
