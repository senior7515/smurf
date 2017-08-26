// Copyright (c) 2016 Alexander Gallego. All rights reserved.
//
#pragma once

#include <core/sstring.hh>

namespace smf {
/// \brief return 64MB aligned to ::sysconf(_SC_PAGESIZE)
uint64_t wal_file_size_aligned();

/// \brief return write ahead name w/ given prefix and current epoch
seastar::sstring wal_file_name(const seastar::sstring &directory,
                               const seastar::sstring &prefix,
                               uint64_t                epoch);

}  // namespace smf
