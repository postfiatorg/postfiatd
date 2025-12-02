//------------------------------------------------------------------------------
/*
    This file is part of postfiatd
    Copyright (c) 2024 PostFiat Developers

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <xrpl/protocol/OrchardBundle.h>

// Include the generated cxx bridge header
#include "orchard-postfiat/src/ffi/bridge.rs.h"

#include <cstring>
#include <stdexcept>

namespace ripple {

//------------------------------------------------------------------------------
// OrchardBundleWrapper implementation
//------------------------------------------------------------------------------

OrchardBundleWrapper::OrchardBundleWrapper(
    std::unique_ptr<rust::Box<::OrchardBundle>> bundle)
    : inner_(std::move(bundle))
{
}

OrchardBundleWrapper::~OrchardBundleWrapper() = default;

OrchardBundleWrapper::OrchardBundleWrapper(OrchardBundleWrapper&&) noexcept = default;

OrchardBundleWrapper&
OrchardBundleWrapper::operator=(OrchardBundleWrapper&&) noexcept = default;

std::optional<OrchardBundleWrapper>
OrchardBundleWrapper::parse(Slice const& data)
{
    try
    {
        // Create a rust::Slice from our Slice
        rust::Slice<const uint8_t> rust_data{
            reinterpret_cast<const uint8_t*>(data.data()),
            data.size()
        };

        // Call the Rust parsing function
        auto bundle = ::orchard_bundle_parse(rust_data);

        // Wrap in our C++ wrapper
        auto wrapper = std::unique_ptr<rust::Box<::OrchardBundle>>(
            new rust::Box<::OrchardBundle>(std::move(bundle))
        );

        return OrchardBundleWrapper(std::move(wrapper));
    }
    catch (...)
    {
        // Parsing failed
        return std::nullopt;
    }
}

Blob
OrchardBundleWrapper::serialize() const
{
    auto rust_vec = ::orchard_bundle_serialize(**inner_);

    Blob result;
    result.reserve(rust_vec.size());
    result.assign(rust_vec.begin(), rust_vec.end());

    return result;
}

bool
OrchardBundleWrapper::isPresent() const
{
    return ::orchard_bundle_is_present(**inner_);
}

bool
OrchardBundleWrapper::isValid() const
{
    return ::orchard_bundle_is_valid(**inner_);
}

std::int64_t
OrchardBundleWrapper::getValueBalance() const
{
    return ::orchard_bundle_get_value_balance(**inner_);
}

uint256
OrchardBundleWrapper::getAnchor() const
{
    auto anchor_bytes = ::orchard_bundle_get_anchor(**inner_);

    uint256 result;
    std::memcpy(result.data(), anchor_bytes.data(), 32);

    return result;
}

std::vector<uint256>
OrchardBundleWrapper::getNullifiers() const
{
    // Get flattened nullifiers (32 bytes per nullifier)
    auto rust_nullifiers_flat = ::orchard_bundle_get_nullifiers(**inner_);

    // Each nullifier is 32 bytes
    std::size_t num_nullifiers = rust_nullifiers_flat.size() / 32;
    std::vector<uint256> result;
    result.reserve(num_nullifiers);

    for (std::size_t i = 0; i < num_nullifiers; ++i)
    {
        uint256 nullifier;
        std::memcpy(
            nullifier.data(),
            rust_nullifiers_flat.data() + (i * 32),
            32
        );
        result.push_back(nullifier);
    }

    return result;
}

std::size_t
OrchardBundleWrapper::numActions() const
{
    return ::orchard_bundle_num_actions(**inner_);
}

bool
OrchardBundleWrapper::verifyProof(uint256 const& sighash) const
{
    std::array<uint8_t, 32> sighash_bytes;
    std::memcpy(sighash_bytes.data(), sighash.data(), 32);

    return ::orchard_verify_bundle_proof(
        **inner_,
        sighash_bytes
    );
}

rust::Box<::OrchardBundle> const&
OrchardBundleWrapper::getRustBundle() const
{
    return *inner_;
}

//------------------------------------------------------------------------------
// OrchardBatchVerifier implementation
//------------------------------------------------------------------------------

OrchardBatchVerifier::OrchardBatchVerifier()
    : inner_(new rust::Box<::OrchardBatchVerifier>(
        ::orchard_batch_verify_init()))
{
}

OrchardBatchVerifier::~OrchardBatchVerifier() = default;

OrchardBatchVerifier::OrchardBatchVerifier(OrchardBatchVerifier&&) noexcept = default;

OrchardBatchVerifier&
OrchardBatchVerifier::operator=(OrchardBatchVerifier&&) noexcept = default;

void
OrchardBatchVerifier::add(
    OrchardBundleWrapper const& bundle,
    uint256 const& sighash)
{
    std::array<uint8_t, 32> sighash_bytes;
    std::memcpy(sighash_bytes.data(), sighash.data(), 32);

    // Clone the bundle for the batch verifier
    auto bundle_clone = ::orchard_bundle_box_clone(*bundle.getRustBundle());

    ::orchard_batch_verify_add(
        **inner_,
        std::move(bundle_clone),
        sighash_bytes
    );
}

bool
OrchardBatchVerifier::verify()
{
    return ::orchard_batch_verify_finalize(
        std::move(*inner_)
    );
}

}  // namespace ripple
