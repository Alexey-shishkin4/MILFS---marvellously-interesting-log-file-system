#include "headers/segment_io.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <limits>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <utility>

namespace {

bool valid_record_type(uint16_t type) {
    switch (static_cast<RecordType>(type)) {
        case RecordType::Inode:
        case RecordType::Data:
        case RecordType::Dirent:
        case RecordType::Checkpoint:
        case RecordType::Tombstone:
            return true;
    }
    return false;
}

bool all_zero(const void* data, std::size_t size) {
    const auto* p = static_cast<const unsigned char*>(data);
    return std::all_of(p, p + size, [](unsigned char c) { return c == 0; });
}

} // namespace

SegmentIO::~SegmentIO() { close(); }

SegmentIO::SegmentIO(SegmentIO&& other) noexcept
    : fd_(other.fd_), sb_(other.sb_), image_path_(std::move(other.image_path_)) {
    other.fd_ = -1;
}

SegmentIO& SegmentIO::operator=(SegmentIO&& other) noexcept {
    if (this != &other) {
        close();
        fd_ = other.fd_;
        sb_ = other.sb_;
        image_path_ = std::move(other.image_path_);
        other.fd_ = -1;
    }
    return *this;
}

FsError SegmentIO::open_or_create(const std::string& image_path,
                                  const Superblock& superblock,
                                  bool truncate_existing) {
    close();

    image_path_ = image_path;
    sb_ = superblock;

    fd_ = ::open(image_path_.c_str(), O_RDWR | O_CREAT, 0644);
    if (fd_ < 0) {
        return FsError::IoError;
    }

    struct stat st {};
    if (::fstat(fd_, &st) != 0) {
        close();
        return FsError::IoError;
    }

    const auto desired_size = static_cast<off_t>(sb_.disk_size_bytes);
    const bool file_empty = (st.st_size == 0);
    const bool needs_format = truncate_existing || file_empty;

    if (needs_format) {
        if (::ftruncate(fd_, desired_size) != 0) {
            close();
            return FsError::IoError;
        }

        FsError err = write_superblock();
        if (err != FsError::Ok) {
            close();
            return err;
        }

        err = flush();
        if (err != FsError::Ok) {
            close();
            return err;
        }
    } else {
        if (st.st_size < static_cast<off_t>(sizeof(Superblock))) {
            close();
            return FsError::Internal;
        }

        Superblock on_disk{};
        FsError err = read_superblock(on_disk);
        if (err != FsError::Ok) {
            close();
            return err;
        }

        if (on_disk.version != sb_.version ||
            on_disk.block_size_bytes != sb_.block_size_bytes ||
            on_disk.blocks_per_segment != sb_.blocks_per_segment ||
            on_disk.segment_count != sb_.segment_count ||
            on_disk.disk_size_bytes != sb_.disk_size_bytes) {
            close();
            return FsError::Internal;
        }

        if (st.st_size < desired_size) {
            if (::ftruncate(fd_, desired_size) != 0) {
                close();
                return FsError::IoError;
            }
        }
    }

    return FsError::Ok;
}


void SegmentIO::close() noexcept {
    if (fd_ >= 0) {
        log_buffer_.flush_to_disk(fd_);
        ::close(fd_);
        fd_ = -1;
    }
}

FsError SegmentIO::append_record(Allocator& allocator,
                                 RecordType type,
                                 uint64_t key,
                                 uint32_t owner_inode,
                                 uint32_t logical_block_index,
                                 const std::byte* payload,
                                 std::size_t payload_size,
                                 LogAddress& out_addr,
                                 RecordHeader* out_header) {
    if (!is_open()) {
        return FsError::IoError;
    }

    if (payload_size > std::numeric_limits<uint32_t>::max()) {
        return FsError::NoSpace;
    }

    if (payload_size > 0 && payload == nullptr) {
        return FsError::Internal;
    }

    Allocation alloc{};
    FsError err = allocator.allocate_record(static_cast<uint32_t>(payload_size), alloc);
    if (err != FsError::Ok) {
        return err;
    }

    RecordHeader hdr{};
    hdr.type = static_cast<uint16_t>(type);
    hdr.flags = RecordFlags::kNone;
    hdr.key = key;
    hdr.seq_no = allocator.next_record_seq_no();
    hdr.payload_size_bytes = static_cast<uint32_t>(payload_size);
    hdr.total_size_bytes = record_total_size(sb_.block_size_bytes, hdr.payload_size_bytes);
    hdr.owner_inode = owner_inode;
    hdr.logical_block_index = logical_block_index;

    if (hdr.total_size_bytes > sizeof(RecordHeader) + hdr.payload_size_bytes) {
        hdr.flags = static_cast<uint16_t>(hdr.flags | RecordFlags::kHasPadding);
    }

    const uint64_t write_off = block_offset_bytes(alloc.addr.segment_id, alloc.addr.block_index);
    if (write_off + hdr.total_size_bytes > sb_.disk_size_bytes) {
        return FsError::NoSpace;
    }

    const SegmentHeader* active = allocator.active_segment();
    if (active != nullptr && alloc.addr.segment_id != 0) {
        err = write_segment_header(*active);
        if (err != FsError::Ok) {
            return err;
        }
    }

    err = write_all_at(&hdr, sizeof(hdr), write_off);
    if (err != FsError::Ok) {
        return err;
    }

    if (payload_size > 0) {
        err = write_all_at(payload, payload_size, write_off + sizeof(hdr));
        if (err != FsError::Ok) {
            return err;
        }
    }

    const uint32_t written = static_cast<uint32_t>(sizeof(RecordHeader) + payload_size);
    const uint32_t padding = hdr.total_size_bytes - written;
    if (padding > 0) {
        std::vector<std::byte> zeros(padding);
        err = write_all_at(zeros.data(), zeros.size(), write_off + written);
        if (err != FsError::Ok) {
            return err;
        }
    }

    out_addr = alloc.addr;
    if (out_header != nullptr) {
        *out_header = hdr;
    }
    return FsError::Ok;
}

FsError SegmentIO::read_record(const LogAddress& addr,
                               RecordHeader& out_header,
                               std::vector<std::byte>& out_payload) const {
    out_payload.clear();

    if (!is_open()) {
        return FsError::IoError;
    }

    if (addr.segment_id >= sb_.segment_count || addr.block_index >= sb_.blocks_per_segment) {
        return FsError::NotFound;
    }

    const uint64_t read_off = block_offset_bytes(addr.segment_id, addr.block_index);
    if (read_off + sizeof(RecordHeader) > sb_.disk_size_bytes) {
        return FsError::NotFound;
    }

    RecordHeader hdr{};
    FsError err = read_all_at(&hdr, sizeof(hdr), read_off);
    if (err != FsError::Ok) {
        return err;
    }

    if (all_zero(&hdr, sizeof(hdr))) {
        return FsError::NotFound;
    }

    if (!valid_record_type(hdr.type)) {
        return FsError::Internal;
    }

    if (hdr.total_size_bytes < sizeof(RecordHeader) + hdr.payload_size_bytes) {
        return FsError::Internal;
    }

    if (hdr.total_size_bytes != record_total_size(sb_.block_size_bytes, hdr.payload_size_bytes)) {
        return FsError::Internal;
    }

    if (addr.block_index + record_blocks(sb_.block_size_bytes, hdr.payload_size_bytes) >
        sb_.blocks_per_segment) {
        return FsError::Internal;
    }

    if (read_off + hdr.total_size_bytes > sb_.disk_size_bytes) {
        return FsError::Internal;
    }

    out_payload.resize(hdr.payload_size_bytes);
    if (!out_payload.empty()) {
        err = read_all_at(out_payload.data(), out_payload.size(), read_off + sizeof(hdr));
        if (err != FsError::Ok) {
            out_payload.clear();
            return err;
        }
    }

    out_header = hdr;
    return FsError::Ok;
}

FsError SegmentIO::flush() {
    if (!is_open()) {
        return FsError::IoError;
    }
    
    FsError err = log_buffer_.flush_to_disk(fd_);
    if (err != FsError::Ok) {
        return err;
    }

    if (::fsync(fd_) != 0) {
        return FsError::IoError;
    }
    return FsError::Ok;
}

uint64_t SegmentIO::segment_offset_bytes(uint32_t segment_id) const {
    return static_cast<uint64_t>(segment_id) * sb_.blocks_per_segment * sb_.block_size_bytes;
}

uint64_t SegmentIO::block_offset_bytes(uint32_t segment_id, uint32_t block_index) const {
    return segment_offset_bytes(segment_id) +
           static_cast<uint64_t>(block_index) * sb_.block_size_bytes;
}

FsError SegmentIO::write_all_at(const void* data, std::size_t size, uint64_t offset) {
    return log_buffer_.append(offset, data, size, fd_);
}

FsError SegmentIO::read_all_at(void* data, std::size_t size, uint64_t offset) const {
    return log_buffer_.read_with_cache(offset, data, size, fd_);
}

FsError SegmentIO::write_superblock() {
    std::vector<std::byte> block(sb_.block_size_bytes);
    std::memcpy(block.data(), &sb_, sizeof(sb_));
    return write_all_at(block.data(), block.size(), 0);
}

FsError SegmentIO::read_superblock(Superblock& out) const {
    return read_all_at(&out, sizeof(out), 0);
}

FsError SegmentIO::write_segment_header(const SegmentHeader& header) {
    if (header.segment_id >= sb_.segment_count) {
        return FsError::Internal;
    }
    if (sizeof(SegmentHeader) > sb_.block_size_bytes) {
        return FsError::Internal;
    }

    std::vector<std::byte> block(sb_.block_size_bytes);
    std::memcpy(block.data(), &header, sizeof(header));
    return write_all_at(block.data(), block.size(), segment_offset_bytes(header.segment_id));
}
