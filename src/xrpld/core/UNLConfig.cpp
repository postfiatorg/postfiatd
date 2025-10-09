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

#include <xrpld/core/UNLConfig.h>
#include <xrpld/app/misc/AmendmentTable.h>
#include <xrpl/protocol/Feature.h>

namespace ripple {

namespace UNLConfig {

std::vector<std::string>
getActiveUNL(AmendmentTable const& amendmentTable)
{
    // Check for UNL update amendments in reverse chronological order
    // Always add newest amendments first to this check

    // Future UNL updates would be checked here first:
    // if (amendmentTable.isEnabled(featureUNLUpdate2))
    //     return unlUpdate2List;

    if (amendmentTable.isEnabled(featureUNLUpdate1))
        return unlUpdate1List;

    // Default to initial UNL
    return initialValidatorsList;
}

}  // namespace UNLConfig

}  // namespace ripple
