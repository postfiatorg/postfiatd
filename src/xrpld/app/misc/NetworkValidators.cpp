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

#include <xrpld/app/misc/NetworkValidators.h>

namespace ripple {

std::vector<std::string>
NetworkValidators::getMainnetValidators()
{
    // Mainnet validator list
    return {
    };
}

std::vector<std::string>
NetworkValidators::getTestnetValidators()
{
    // Testnet validator list
    return {
        "nHU3VNRD3cBsFwcDcKaMUoikag3RE7PS9p8L4Uj9dYQFv3zsLWdQ",
        "nHUHS6rzWd2toxnaCLLcAD6nTLUBKxBsRanjywxLeyZ2q19AmZxe",
        "nHUkbNkhJcPDnSjCuZwqcAiHJUxYvirLJt8Qy38Wyvk6Tri1cq1A",
        "nHUedN7diUp6o3p6H7f6JFSoHfwC3TFjt5YEmrMcwh6p2PYggbpv",
        "nHBiHzPq3iiJ7MxZkZ3LoBBJneRtcZAoXm5Crb985neVN6ygQ3b7"
    };
}

std::vector<std::string>
NetworkValidators::getDevnetValidators()
{
    // Devnet validator list
    return {
        "nHDaLH7eYFGSZZti8S75gxvzCGfMFsWqZm62LCu2b2vZr8mDfQXk",
        "nHBk8BTED5hYxykdCcrMJERo9m29HCGQdG7FhyzG7VhuGCXyzTZd",
        "nHDD1wHPvJ1JtLy52hGy2a4WCBKKiPAj9MXhgjWQ15CaH9UeaTbK",
        "nHB4HVMHW5rd6j8vWQbNDUFyoV9izK7QhQEZ3AZuQhwA1xQXzbxF"
    };
}

std::vector<std::string>
NetworkValidators::getValidators(std::uint32_t networkId)
{
    switch (static_cast<NetworkType>(networkId))
    {
    case NetworkType::MAINNET:
        return getMainnetValidators();
    case NetworkType::TESTNET:
        return getTestnetValidators();
    case NetworkType::DEVNET:
        return getDevnetValidators();
    default:
        // Default to mainnet validators for unknown network IDs
        return getMainnetValidators();
    }
}

}  // namespace ripple
