#ifndef RIPPLE_OVERLAY_MANIFESTMESSAGEDISCARD_H_INCLUDED
#define RIPPLE_OVERLAY_MANIFESTMESSAGEDISCARD_H_INCLUDED

#include <xrpld/overlay/detail/ManifestMessages.h>

#include <boost/asio/buffer.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>

namespace ripple {

// Compatibility with a 1.0.4 peer whose initial cache dump exceeds its 26-bit
// frame. Drain ONE bounded manifest dump without parsing/decompressing it.
// Never relax the ordinary protocol parser or allocate the declared length.
class ManifestMessageDiscard
{
public:
    struct Result
    {
        std::size_t consumed = 0;
        bool needHeader = false;
        bool started = false;
        bool reject = false;
        std::uint32_t wireBytes = 0;
        std::uint32_t plainBytes = 0;
    };

    template <class Buffers>
    Result
    consume(
        Buffers const& buffers,
        std::chrono::steady_clock::time_point const now =
            std::chrono::steady_clock::now())
    {
        auto const size = boost::asio::buffer_size(buffers);
        if (!size)
            return {};
        if (remaining_)
        {
            if (now >= deadline_)
                return {0, false, false, true};
            auto const n = std::min(size, remaining_);
            remaining_ -= n;
            return {n};
        }

        std::array<std::uint8_t, 10> header{};
        boost::asio::buffer_copy(boost::asio::buffer(header), buffers);
        bool const compressed = (header[0] & 0xF0) == 0x90;
        // Only legacy 28-bit uncompressed or LZ4 framing is recognizable.
        if ((header[0] & 0xF0) != 0 && !compressed)
            return {};
        std::size_t const headerSize = compressed ? 10 : 6;
        if (size < headerSize)
            return {0, true};

        auto const type = (std::uint16_t(header[4]) << 8) | header[5];
        if (type != protocol::mtMANIFESTS)
            return {};

        auto unpack = [&](std::size_t offset) {
            std::uint32_t n = 0;
            for (std::size_t i = offset; i < offset + 4; ++i)
                n = (n << 8) | header[i];
            return n;
        };
        auto const wire = unpack(0) & 0x0FFFFFFF;
        auto const plain = compressed ? unpack(6) : wire;
        if (wire <= maxManifestBatchBytes && plain <= maxManifestBatchBytes)
            return {};

        // One dump per connection, at most 128 MiB and 60 seconds. A repeated
        // dump, impossible length, or timeout is a protocol failure.
        constexpr std::size_t legacyLimit = 128 * 1024 * 1024;
        if (used_ || !wire || !plain || wire > legacyLimit ||
            plain > legacyLimit)
            return {0, false, false, true};

        used_ = true;
        deadline_ = now + std::chrono::seconds(60);
        remaining_ = headerSize + wire;
        auto const n = std::min(size, remaining_);
        remaining_ -= n;
        return {n, false, true, false, wire, plain};
    }

    bool
    expired() const
    {
        return remaining_ && std::chrono::steady_clock::now() >= deadline_;
    }

private:
    std::size_t remaining_ = 0;
    bool used_ = false;
    std::chrono::steady_clock::time_point deadline_{};
};

}  // namespace ripple

#endif
