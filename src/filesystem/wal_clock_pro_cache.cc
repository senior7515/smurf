// Copyright (c) 2016 Alexander Gallego. All rights reserved.
//
#include "filesystem/wal_clock_pro_cache.h"
// std
#include <algorithm>
#include <utility>
// third party
#include <boost/iterator/counting_iterator.hpp>
// smf
#include "filesystem/file_size_utils.h"
#include "filesystem/wal_pretty_print_utils.h"
#include "hashing/hashing_utils.h"
#include "platform/log.h"

namespace smf {

static uint32_t round_up(int64_t file_size, size_t alignment) {
  uint32_t ret = file_size / alignment;
  if (file_size % alignment != 0) { ret += 1; }
  return ret;
}

wal_clock_pro_cache::wal_clock_pro_cache(
  seastar::lw_shared_ptr<seastar::file> f,
  int64_t                               initial_size,
  uint32_t                              max_pages_in_memory)
  : disk_dma_alignment(f->disk_read_dma_alignment())
  , file_size_(initial_size)
  , number_of_pages_(round_up(initial_size, disk_dma_alignment))
  , file_(std::move(f)) {
  using cache_t = smf::clock_pro_cache<uint64_t, page_data>;
  cache_        = std::make_unique<cache_t>(max_resident_pages_);
}
void wal_clock_pro_cache::update_file_size_by(uint64_t delta) {
  file_size_ += delta;
  number_of_pages_ = round_up(file_size_, disk_dma_alignment);
}

seastar::future<> wal_clock_pro_cache::close() {
  auto f = file_;  // must hold file until handle closes
  return f->close().finally([f] {});
}

/// intrusive containers::iterators are not default no-throw move
/// constructible, so we have to get the ptr to the data which is lame
///
seastar::future<wal_clock_pro_cache::chunk_t_ptr>
wal_clock_pro_cache::clock_pro_get_page(uint32_t                          page,
                                        const seastar::io_priority_class &pc) {
  if (cache_->contains(page)) {
    return seastar::make_ready_future<chunk_t_ptr>(cache_->get_page(page));
  }
  // we fetch things dynamiclly. so just fill up the buffer before eviction
  // agorithm kicks in. nothing fancy
  //
  // if we just let the clock-pro work w/out pre-allocating, you end up w/ one
  // or 2 pages that you are moving between hot and cold and test
  //
  if (cache_->size() < number_of_pages_) {
    return fetch_page(page, pc).then([this, page](auto chunk) {
      cache_->set(std::move(chunk));
      return seastar::make_ready_future<chunk_t_ptr>(
        cache_->get_chunk_ptr(page));
    });
  }
  cache_->run_cold_hand();
  cache_->run_hot_hand();
  cache_->fix_hands();

  // next add page to list
  return fetch_page(page, pc).then([this, page](auto chunk) {
    cache_->set(std::move(chunk));
    return seastar::make_ready_future<chunk_t_ptr>(cache_->get_chunk_ptr(page));
  });
}


seastar::future<wal_clock_pro_cache::chunk_t> wal_clock_pro_cache::fetch_page(
  const uint32_t &page, const seastar::io_priority_class &pc) {
  static const uint64_t kAlignment = file_->disk_read_dma_alignment();
  const uint64_t page_offset_begin = static_cast<uint64_t>(page) * kAlignment;

  auto bufptr = seastar::allocate_aligned_buffer<char>(kAlignment, kAlignment);
  auto fut = file_->dma_read(page_offset_begin, bufptr.get(), kAlignment, pc);

  return std::move(fut).then(
    [page, bufptr = std::move(bufptr)](auto size) mutable {
      LOG_THROW_IF(size > kAlignment, "Read more than 1 page");
      chunk_t c(page, page_data(size, std::move(bufptr)));
      return seastar::make_ready_future<chunk_t>(std::move(c));
    });
}

static uint64_t copy_page_data(seastar::temporary_buffer<char> *      buf,
                               const int64_t &                        offset,
                               const int64_t &                        size,
                               const wal_clock_pro_cache::chunk_t_ptr chunk,
                               const size_t &alignment) {
  LOG_THROW_IF(chunk->data.buf_size == 0, "Bad page");
  const int64_t bottom_page_offset = offset > 0 ? offset % alignment : 0;
  const int64_t buffer_offset =
    bottom_page_offset > 0 ? (bottom_page_offset % (chunk->data.buf_size)) : 0;

  const char *   begin_src = chunk->data.data.get() + buffer_offset;
  char *         begin_dst = buf->get_write() + (buf->size() - size);
  const uint64_t step_size =
    std::min<int64_t>(chunk->data.buf_size - buffer_offset, size);
  const char *end_src = begin_src + step_size;

  LOG_THROW_IF(step_size == 0, "Invalid range to copy. step: {}", step_size);
  std::copy(begin_src, end_src, begin_dst);
  return step_size;
}

seastar::future<seastar::temporary_buffer<char>>
wal_clock_pro_cache::read_exactly(int64_t                           offset,
                                  int64_t                           size,
                                  const seastar::io_priority_class &pc) {
  struct buf_req {
    buf_req(int64_t _offset, int64_t _size, const seastar::io_priority_class &p)
      : offset(_offset), size(_size), pc(p) {}

    void update_step(uint64_t step) {
      size -= step;
      offset += step;
    }

    int64_t                           offset;
    int64_t                           size;
    const seastar::io_priority_class &pc;
  };
  static const size_t kAlignment = file_->disk_read_dma_alignment();
  auto buf = seastar::make_lw_shared<seastar::temporary_buffer<char>>(size);
  auto req = seastar::make_lw_shared<buf_req>(offset, size, pc);
  auto max_pages = 1 + (req->size / kAlignment);
  LOG_THROW_IF(max_pages > number_of_pages_,
               "Request asked to read more pages, than available on disk");
  return seastar::repeat([buf, this, req]() mutable {
           auto page = offset_to_page(req->offset, kAlignment);
           return this->clock_pro_get_page(page, req->pc)
             .then([buf, req](auto chunk) {
               if (req->size <= 0) { return seastar::stop_iteration::yes; }
               auto step = copy_page_data(buf.get(), req->offset, req->size,
                                          chunk, kAlignment);
               req->update_step(step);
               return seastar::stop_iteration::no;
             });
         })
    .then([buf] {
      return seastar::make_ready_future<seastar::temporary_buffer<char>>(
        buf->share());
    });
}

static void validate_header(const wal::wal_header &hdr,
                            const int64_t &        file_size) {
  LOG_THROW_IF(hdr.checksum() == 0 || hdr.size() == 0,
               "Could not read header from file");
  LOG_THROW_IF(hdr.size() > file_size,
               "Header asked for more data than file_size. Header:{}", hdr);
}

static wal::wal_header get_header(const seastar::temporary_buffer<char> &buf) {
  static const uint32_t kHeaderSize = sizeof(wal::wal_header);
  wal::wal_header       hdr;
  LOG_THROW_IF(buf.size() < kHeaderSize, "Could not read header. Only read",
               buf.size());
  char *hdr_ptr = reinterpret_cast<char *>(&hdr);
  std::copy(buf.get(), buf.get() + kHeaderSize, hdr_ptr);
  return hdr;
}

static void validate_payload(const wal::wal_header &                hdr,
                             const seastar::temporary_buffer<char> &buf) {
  LOG_THROW_IF(buf.size() != hdr.size(),
               "Could not read header. Only read {} bytes", buf.size());
  uint32_t checksum = xxhash_32(buf.get(), buf.size());
  LOG_THROW_IF(checksum != hdr.checksum(),
               "bad header: {}, missmatching checksums. "
               "expected checksum: {}",
               hdr, checksum);
}

seastar::future<wal_read_reply> wal_clock_pro_cache::read(wal_read_request r) {
  wal_read_reply retval;
  LOG_THROW_IF(r.data->offset > file_size_, "Invalid offset range for file");
  static const uint32_t kHeaderSize = sizeof(wal::wal_header);
  auto logerr                       = [r, retval](auto eptr) {
    LOG_ERROR("Caught exception: {}. Request: {}, Reply:{}", eptr, r, retval);
    return seastar::stop_iteration::no;
  };
  return seastar::repeat([r, this, retval, logerr]() mutable {
           if (retval.size() >= r.data->max_size()) {
             return seastar::make_ready_future<seastar::stop_iteration>(
               seastar::stop_iteration::yes);
           }
           auto next_offset = r.data->offset() + retval.data->next_offset();
           return this->read_exactly(next_offset, kHeaderSize, req->pc)
             .then([this, r, retval, next_offset, logerr](auto buf) mutable {
               auto hdr = get_header(buf);
               next_offset += kHeaderSize;
               validate_header(hdr, file_size_);
               return this->read_exactly(next_offset, hdr.size(), r->pc)
                 .then([retval, hdr, this, r, logerr](auto buf) mutable {
                   validate_payload(hdr, buf);

                   auto ptr = std::make_unique<wal::tx_get_fragmentT>();
                   ptr->hdr = std::make_unique<wal::wal_header>(std::move(hdr));
                   // copy to the std::vector
                   ptr->fragment.reserve(buf.size());
                   std::copy(buf.get(), buf.get() + buf.size(),
                             std::back_inserster(ptr->fragment));
                   retval.data.gets.push_back(std::move(ptr));
                   return seastar::stop_iteration::no;
                 })
                 .handle_exception(logerr);
             })
             .handle_exception(logerr);
         })
    .then([retval] {
      // retval keeps a lw_shared_ptr
      return seastar::make_ready_future<wal_read_reply>(retval);
    });
}

}  // namespace smf
