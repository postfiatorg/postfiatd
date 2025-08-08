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

#include <xrpld/app/misc/ComplianceBlacklist.h>
#include <xrpld/core/Config.h>
#include <xrpl/basics/Log.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/Protocol.h>

#include <boost/algorithm/string.hpp>

namespace ripple {
namespace ComplianceBlacklist {

std::unordered_set<AccountID>
parseBlacklist(Blob const& domain)
{
    std::unordered_set<AccountID> blacklist;
    
    if (domain.empty())
        return blacklist;
    
    // Convert blob to string
    std::string domainStr(domain.begin(), domain.end());
    
    // Split by comma
    std::vector<std::string> addresses;
    boost::split(addresses, domainStr, boost::is_any_of(","));
    
    // Parse each address
    for (auto const& addr : addresses)
    {
        auto trimmed = boost::trim_copy(addr);
        if (!trimmed.empty())
        {
            if (auto account = parseBase58<AccountID>(trimmed))
            {
                blacklist.insert(*account);
            }
        }
    }
    
    return blacklist;
}

std::optional<std::string>
encodeBlacklist(std::unordered_set<AccountID> const& blacklist)
{
    if (blacklist.empty())
        return "";
    
    std::string result;
    bool first = true;
    
    for (auto const& account : blacklist)
    {
        if (!first)
            result += ",";
        result += toBase58(account);
        first = false;
    }
    
    // Check size limit
    if (result.size() > maxDomainLength)
        return std::nullopt;
    
    return result;
}

bool
isBlacklisted(
    ReadView const& view,
    Config const& config,
    AccountID const& account)
{
    auto complianceAccount = getComplianceAccount(config);
    if (!complianceAccount)
        return false;
    
    // Read the compliance account from ledger
    auto const sle = view.read(keylet::account(*complianceAccount));
    if (!sle || !sle->isFieldPresent(sfDomain))
        return false;
    
    // Parse the blacklist from domain field
    auto blacklist = parseBlacklist(sle->getFieldVL(sfDomain));
    
    return blacklist.find(account) != blacklist.end();
}

std::optional<AccountID>
getComplianceAccount(Config const& config)
{
    if (config.COMPLIANCE_ACCOUNT.empty())
        return std::nullopt;
    
    return parseBase58<AccountID>(config.COMPLIANCE_ACCOUNT);
}

std::unordered_set<AccountID>
getBlacklist(ReadView const& view, Config const& config)
{
    auto complianceAccount = getComplianceAccount(config);
    if (!complianceAccount)
        return {};
    
    // Read the compliance account from ledger
    auto const sle = view.read(keylet::account(*complianceAccount));
    if (!sle || !sle->isFieldPresent(sfDomain))
        return {};
    
    return parseBlacklist(sle->getFieldVL(sfDomain));
}

} // namespace ComplianceBlacklist
} // namespace ripple