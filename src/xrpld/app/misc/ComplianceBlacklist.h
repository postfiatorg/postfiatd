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

#ifndef RIPPLE_APP_MISC_COMPLIANCEBLACKLIST_H_INCLUDED
#define RIPPLE_APP_MISC_COMPLIANCEBLACKLIST_H_INCLUDED

#include <xrpld/ledger/ReadView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/STLedgerEntry.h>

#include <string>
#include <unordered_set>
#include <vector>

namespace ripple {

class Config;

/**
 * Utilities for managing compliance blacklist stored in the domain field
 * of the compliance account.
 * 
 * Format: comma-separated list of account addresses
 * Example: "rAccount1,rAccount2,rAccount3"
 */
namespace ComplianceBlacklist {

/**
 * Parse blacklist from domain field data
 * @param domain The raw domain field data
 * @return Set of blacklisted account IDs
 */
std::unordered_set<AccountID>
parseBlacklist(Blob const& domain);

/**
 * Encode blacklist to domain field format
 * @param blacklist Set of account IDs to blacklist
 * @return Encoded string for domain field, or nullopt if too large
 */
std::optional<std::string>
encodeBlacklist(std::unordered_set<AccountID> const& blacklist);

/**
 * Check if an account is blacklisted
 * @param view The ledger view
 * @param config The config containing compliance account
 * @param account The account to check
 * @return true if the account is blacklisted
 */
bool
isBlacklisted(
    ReadView const& view,
    Config const& config,
    AccountID const& account);

/**
 * Get the configured compliance account
 * @param config The application config
 * @return The compliance account ID if configured, nullopt otherwise
 */
std::optional<AccountID>
getComplianceAccount(Config const& config);

/**
 * Get all blacklisted accounts
 * @param view The ledger view
 * @param config The config containing compliance account
 * @return Set of blacklisted account IDs
 */
std::unordered_set<AccountID>
getBlacklist(ReadView const& view, Config const& config);

} // namespace ComplianceBlacklist
} // namespace ripple

#endif