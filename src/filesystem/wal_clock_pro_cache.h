// Copyright (c) 2016 Alexander Gallego. All rights reserved.
//
#pragma once
#include <cmath>
#include <list>
#include <utility>
// third party
#include <core/aligned_buffer.hh>
#include <core/file.hh>
// smf
#include "filesystem/wal_requests.h"
#include "platform/macros.h"
#include "utils/caching/clock_pro/clock_pro.h"

// TODO(agallego) - change filesize to be uint64_t
// TODO(agallego) - pass metadata to wether or not enable systemwide page cache eviction

namespace smf {
class wal_clock_pro_cache {
 public:
  struct page_data {
    using bufptr_t = std::unique_ptr<char[], seastar::free_deleter>;
    page_data(uint32_t size, bufptr_t d) : buf_size(size), data(std::move(d)) {}
    page_data(page_data &&d) noexcept
      : buf_size(std::move(d.buf_size)), data(std::move(d.data)) {}
    const uint32_t buf_size;
    bufptr_t       data;
    SMF_DISALLOW_COPY_AND_ASSIGN(page_data);
  };
  using cache_t     = smf::clock_pro_cache<uint64_t, page_data>;
  using chunk_t     = typename cache_t::chunk_t;
  using chunk_t_ptr = typename std::add_pointer<chunk_t>::type;


  /// \brief - designed to be a cache from the direct-io layer
  /// and pages fetched. It understands the wal_writer format
  wal_clock_pro_cache(seastar::lw_shared_ptr<seastar::file> f,
                      // size of the `f` file from the fstat() call
                      // need this to figure out how many pages to allocate
                      int64_t initial_size,
                      // max_pages_in_memory = 0, allows us to make a decision
                      // impl defined. right now, it chooses the max of 10% of
                      // the file or 10 pages. each page is 4096 bytes
                      uint32_t max_pages_in_memory = 0);


  const uint64_t disk_dma_alignment;


  /// \brief this is risky and is for performance reasons
  /// we do not want to fs::stat files all the time.
  /// This recomputes offsets of files that we want to read
  void update_file_size_by(uint64_t delta);
  int64_t  file_size() { return file_size_; }
  uint32_t number_of_pages() { return number_of_pages_; }
  /// \brief - return buffer for offset with size
  seastar::future<wal_read_reply> read(wal_read_request r);

  /// \brief - closes lw_share_ptr<file>
  seastar::future<> close();

 private:
  int64_t  file_size_;
  uint32_t number_of_pages_;

  /// \brief - returns a temporary buffer of size. similar to
  /// isotream.read_exactly()
  /// different than a wal_read_request for arguments since it returns **one**
  /// temp buffer
  ///
  seastar::future<seastar::temporary_buffer<char>> read_exactly(
    int64_t offset, int64_t size, const seastar::io_priority_class &pc);

  /// \breif fetch exactly one page from disk w/ dma_alignment() so that
  /// we don't pay the penalty of fetching 2 pages and discarding one
  seastar::future<chunk_t> fetch_page(const uint32_t &                  page,
                                      const seastar::io_priority_class &pc);

  /// \brief perform the clock-pro caching and eviction techniques
  /// and then return a ptr to the page
  /// intrusive containers::iterators are not default no-throw move
  /// constructible, so we have to get the ptr to the data which is lame
  ///
  seastar::future<chunk_t_ptr> clock_pro_get_page(
    uint32_t page, const seastar::io_priority_class &pc);

 private:
  seastar::lw_shared_ptr<seastar::file> file_;  // uses direct io for fetching
  // This will be removed once we hookup to the seastar allocator for memory
  // backpressure
  //
  uint32_t                 max_resident_pages_ = 10;
  std::unique_ptr<cache_t> cache_;
};


}  // namespace smf
