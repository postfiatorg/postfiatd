//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#ifndef RIPPLE_APP_MISC_NETWORK_VALIDATORS_H_INCLUDED
#define RIPPLE_APP_MISC_NETWORK_VALIDATORS_H_INCLUDED

#include <cstdint>
#include <string>
#include <vector>

namespace ripple {

/**
 * @brief Manages network-specific validator lists for different networks
 *
 * This class provides validator lists tailored for mainnet, testnet, and devnet.
 * The appropriate list is selected based on the network ID from the configuration.
 */
class NetworkValidators
{
public:
    /**
     * @brief Network types supported by the system
     */
    enum class NetworkType : std::uint32_t
    {
        DEVNET = 2024,
        TESTNET = 2025,
        MAINNET = 2026
    };

    /**
     * @brief Get the validator list for a specific network
     *
     * @param networkId The network ID (2024=devnet, 2025=testnet, 2026=mainnet)
     * @return std::vector<std::string> List of validator public keys
     */
    static std::vector<std::string>
    getValidators(std::uint32_t networkId);

    /**
     * @brief Get the mainnet validator list
     *
     * @return std::vector<std::string> List of validator public keys for mainnet
     */
    static std::vector<std::string>
    getMainnetValidators();

    /**
     * @brief Get the testnet validator list
     *
     * @return std::vector<std::string> List of validator public keys for testnet
     */
    static std::vector<std::string>
    getTestnetValidators();

    /**
     * @brief Get the devnet validator list
     *
     * @return std::vector<std::string> List of validator public keys for devnet
     */
    static std::vector<std::string>
    getDevnetValidators();
};

}  // namespace ripple

#endif  // RIPPLE_APP_MISC_NETWORK_VALIDATORS_H_INCLUDED
