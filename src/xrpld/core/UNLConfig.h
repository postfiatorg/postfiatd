//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2024 Ripple Labs Inc.

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

#ifndef RIPPLE_CORE_UNLCONFIG_H_INCLUDED
#define RIPPLE_CORE_UNLCONFIG_H_INCLUDED

#include <string>
#include <vector>

namespace ripple {

class AmendmentTable;

/**
 * UNL Configuration - Centralized validator list definitions
 *
 * This file contains all UNL (Unique Node List) versions.
 * Each UNL update amendment gets a complete validator list here.
 *
 * IMPORTANT: This is the single source of truth for all UNL definitions.
 * Both Application.cpp (startup) and Change.cpp (dynamic reload) use these.
 */
namespace UNLConfig {

/**
 * Initial UNL (v0) - The original validator list
 */
inline std::vector<std::string> const initialValidatorsList = {
    "nHU3VNRD3cBsFwcDcKaMUoikag3RE7PS9p8L4Uj9dYQFv3zsLWdQ",
    "nHUHS6rzWd2toxnaCLLcAD6nTLUBKxBsRanjywxLeyZ2q19AmZxe",
    "nHUkbNkhJcPDnSjCuZwqcAiHJUxYvirLJt8Qy38Wyvk6Tri1cq1A",
    "nHUedN7diUp6o3p6H7f6JFSoHfwC3TFjt5YEmrMcwh6p2PYggbpv",
    "nHBiHzPq3iiJ7MxZkZ3LoBBJneRtcZAoXm5Crb985neVN6ygQ3b7"
};

/**
 * UNL Update 1 - Complete replacement list
 * Becomes active when featureUNLUpdate1 amendment is enabled
 *
 * To update: Replace this entire list with the new validators
 */
inline std::vector<std::string> const unlUpdate1List = {
    "nHU3VNRD3cBsFwcDcKaMUoikag3RE7PS9p8L4Uj9dYQFv3zsLWdQ",
    "nHUHS6rzWd2toxnaCLLcAD6nTLUBKxBsRanjywxLeyZ2q19AmZxe",
    "nHUkbNkhJcPDnSjCuZwqcAiHJUxYvirLJt8Qy38Wyvk6Tri1cq1A",
    "nHUedN7diUp6o3p6H7f6JFSoHfwC3TFjt5YEmrMcwh6p2PYggbpv",
    "nHBiHzPq3iiJ7MxZkZ3LoBBJneRtcZAoXm5Crb985neVN6ygQ3b7",
    // Add new validators here for UNL Update 1
    // Example: "nHBidG3pZK11zQD6kpNDoAhDxH6WLGui6ZxSbUx7LSqLHsgzMPec"
};

/**
 * Get the active UNL based on enabled amendments
 *
 * This function checks which UNL update amendments are enabled
 * and returns the appropriate validator list.
 *
 * @param amendmentTable Reference to the amendment table for checking enabled amendments
 * @return The active validator list
 */
std::vector<std::string> getActiveUNL(AmendmentTable const& amendmentTable);

}  // namespace UNLConfig

}  // namespace ripple

#endif  // RIPPLE_CORE_UNLCONFIG_H_INCLUDED
