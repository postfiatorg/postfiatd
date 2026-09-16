//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2020 Ripple Labs Inc.

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

#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/ledger/LedgerReplayer.h>
#include <xrpld/app/ledger/detail/LedgerReplayMsgHandler.h>
#include <xrpld/app/main/Application.h>

#include <xrpl/basics/Slice.h>
#include <xrpl/protocol/LedgerHeader.h>

#include <exception>
#include <memory>

namespace ripple {
LedgerReplayMsgHandler::LedgerReplayMsgHandler(
    Application& app,
    LedgerReplayer& replayer)
    : app_(app)
    , replayer_(replayer)
    , journal_(app.journal("LedgerReplayMsgHandler"))
{
}

protocol::TMProofPathResponse
LedgerReplayMsgHandler::processProofPathRequest(
    std::shared_ptr<protocol::TMProofPathRequest> const& msg)
{
    protocol::TMProofPathRequest& packet = *msg;
    protocol::TMProofPathResponse reply;

    if (!packet.has_key() || !packet.has_ledgerhash() || !packet.has_type() ||
        packet.ledgerhash().size() != uint256::size() ||
        packet.key().size() != uint256::size() ||
        !protocol::TMLedgerMapType_IsValid(packet.type()))
    {
        JLOG(journal_.debug()) << "getProofPath: Invalid request";
        reply.set_error(protocol::TMReplyError::reBAD_REQUEST);
        return reply;
    }
    reply.set_key(packet.key());
    reply.set_ledgerhash(packet.ledgerhash());
    reply.set_type(packet.type());

    uint256 const key(packet.key());
    uint256 const ledgerHash(packet.ledgerhash());
    auto ledger = app_.getLedgerMaster().getLedgerByHash(ledgerHash);
    if (!ledger)
    {
        JLOG(journal_.debug())
            << "getProofPath: Don't have ledger " << ledgerHash;
        reply.set_error(protocol::TMReplyError::reNO_LEDGER);
        return reply;
    }

    auto const path = [&]() -> std::optional<std::vector<Blob>> {
        switch (packet.type())
        {
            case protocol::lmACCOUNT_STATE:
                return ledger->stateMap().getProofPath(key);
            case protocol::lmTRANASCTION:
                return ledger->txMap().getProofPath(key);
            default:
                // should not be here
                // because already tested with TMLedgerMapType_IsValid()
                return {};
        }
    }();

    if (!path)
    {
        JLOG(journal_.debug()) << "getProofPath: Don't have the node " << key
                               << " of ledger " << ledgerHash;
        reply.set_error(protocol::TMReplyError::reNO_NODE);
        return reply;
    }

    // pack header
    Serializer nData(128);
    addRaw(ledger->info(), nData);
    reply.set_ledgerheader(nData.getDataPtr(), nData.getLength());
    // pack path
    for (auto const& b : *path)
        reply.add_path(b.data(), b.size());

    JLOG(journal_.debug()) << "getProofPath for the node " << key
                           << " of ledger " << ledgerHash << " path length "
                           << path->size();
    return reply;
}

ReplayMsgStatus
LedgerReplayMsgHandler::processProofPathResponse(
    std::shared_ptr<protocol::TMProofPathResponse> const& msg)
{
    protocol::TMProofPathResponse const& reply = *msg;
    if (reply.has_error())
    {
        JLOG(journal_.debug()) << "ProofPathResponse: peer reported error";
        return ReplayMsgStatus::BadData;
    }
    if (!reply.has_key() || !reply.has_ledgerhash() || !reply.has_type() ||
        !reply.has_ledgerheader() || reply.path_size() == 0 ||
        reply.ledgerhash().size() != uint256::size() ||
        reply.key().size() != uint256::size())
    {
        JLOG(journal_.debug())
            << "ProofPathResponse: malformed (missing or wrong-size fields)";
        return ReplayMsgStatus::Malformed;
    }

    if (reply.type() != protocol::lmACCOUNT_STATE)
    {
        JLOG(journal_.debug())
            << "ProofPathResponse: malformed (unsupported map type)";
        return ReplayMsgStatus::Malformed;
    }

    // deserialize the header
    LedgerHeader info;
    try
    {
        info = deserializeHeader(makeSlice(reply.ledgerheader()));
    }
    catch (std::exception const& e)
    {
        JLOG(journal_.debug())
            << "ProofPathResponse: malformed header (" << e.what() << ")";
        return ReplayMsgStatus::Malformed;
    }
    uint256 const replyHash = uint256::fromVoid(reply.ledgerhash().data());
    if (calculateLedgerHash(info) != replyHash)
    {
        JLOG(journal_.debug())
            << "ProofPathResponse: malformed (hash mismatch)";
        return ReplayMsgStatus::Malformed;
    }
    info.hash = replyHash;

    uint256 const key = uint256::fromVoid(reply.key().data());
    if (key != keylet::skip().key)
    {
        JLOG(journal_.debug())
            << "ProofPathResponse: malformed (unexpected key " << key << ")";
        return ReplayMsgStatus::Malformed;
    }

    // verify the skip list
    std::vector<Blob> path;
    path.reserve(reply.path_size());
    for (int i = 0; i < reply.path_size(); ++i)
    {
        path.emplace_back(reply.path(i).begin(), reply.path(i).end());
    }

    if (!SHAMap::verifyProofPath(info.accountHash, key, path))
    {
        JLOG(journal_.debug())
            << "ProofPathResponse: malformed (proof path verify failed)";
        return ReplayMsgStatus::Malformed;
    }

    // deserialize the SHAMapItem
    intr_ptr::SharedPtr<SHAMapTreeNode> node;
    try
    {
        node = SHAMapTreeNode::makeFromWire(makeSlice(path.front()));
    }
    catch (std::exception const& e)
    {
        JLOG(journal_.debug())
            << "ProofPathResponse: malformed SHAMap node (" << e.what() << ")";
        return ReplayMsgStatus::Malformed;
    }
    if (!node || !node->isLeaf())
    {
        JLOG(journal_.debug())
            << "ProofPathResponse: malformed (not a leaf node)";
        return ReplayMsgStatus::Malformed;
    }

    if (auto item = static_cast<SHAMapLeafNode*>(node.get())->peekItem())
    {
        replayer_.gotSkipList(info, item);
        return ReplayMsgStatus::Ok;
    }

    JLOG(journal_.debug()) << "ProofPathResponse: malformed (no SHAMapItem)";
    return ReplayMsgStatus::Malformed;
}

protocol::TMReplayDeltaResponse
LedgerReplayMsgHandler::processReplayDeltaRequest(
    std::shared_ptr<protocol::TMReplayDeltaRequest> const& msg)
{
    protocol::TMReplayDeltaRequest& packet = *msg;
    protocol::TMReplayDeltaResponse reply;

    if (!packet.has_ledgerhash() ||
        packet.ledgerhash().size() != uint256::size())
    {
        JLOG(journal_.debug()) << "getReplayDelta: Invalid request";
        reply.set_error(protocol::TMReplyError::reBAD_REQUEST);
        return reply;
    }
    reply.set_ledgerhash(packet.ledgerhash());

    uint256 const ledgerHash{packet.ledgerhash()};
    auto ledger = app_.getLedgerMaster().getLedgerByHash(ledgerHash);
    if (!ledger || !ledger->isImmutable())
    {
        JLOG(journal_.debug())
            << "getReplayDelta: Don't have ledger " << ledgerHash;
        reply.set_error(protocol::TMReplyError::reNO_LEDGER);
        return reply;
    }

    // pack header
    Serializer nData(128);
    addRaw(ledger->info(), nData);
    reply.set_ledgerheader(nData.getDataPtr(), nData.getLength());
    // pack transactions
    auto const& txMap = ledger->txMap();
    txMap.visitLeaves(
        [&](boost::intrusive_ptr<SHAMapItem const> const& txNode) {
            reply.add_transaction(txNode->data(), txNode->size());
        });

    JLOG(journal_.debug()) << "getReplayDelta for ledger " << ledgerHash
                           << " txMap hash " << txMap.getHash().as_uint256();
    return reply;
}

ReplayMsgStatus
LedgerReplayMsgHandler::processReplayDeltaResponse(
    std::shared_ptr<protocol::TMReplayDeltaResponse> const& msg)
{
    protocol::TMReplayDeltaResponse const& reply = *msg;
    if (reply.has_error())
    {
        JLOG(journal_.debug()) << "ReplayDeltaResponse: peer reported error";
        return ReplayMsgStatus::BadData;
    }
    if (!reply.has_ledgerheader() || !reply.has_ledgerhash() ||
        reply.ledgerhash().size() != uint256::size())
    {
        JLOG(journal_.debug())
            << "ReplayDeltaResponse: malformed (missing or wrong-size fields)";
        return ReplayMsgStatus::Malformed;
    }

    LedgerHeader info;
    try
    {
        info = deserializeHeader(makeSlice(reply.ledgerheader()));
    }
    catch (std::exception const& e)
    {
        JLOG(journal_.debug())
            << "ReplayDeltaResponse: malformed header (" << e.what() << ")";
        return ReplayMsgStatus::Malformed;
    }
    uint256 const replyHash = uint256::fromVoid(reply.ledgerhash().data());
    if (calculateLedgerHash(info) != replyHash)
    {
        JLOG(journal_.debug())
            << "ReplayDeltaResponse: malformed (hash mismatch)";
        return ReplayMsgStatus::Malformed;
    }
    info.hash = replyHash;

    auto numTxns = reply.transaction_size();
    std::map<std::uint32_t, std::shared_ptr<STTx const>> orderedTxns;
    SHAMap txMap(SHAMapType::TRANSACTION, app_.getNodeFamily());
    try
    {
        for (int i = 0; i < numTxns; ++i)
        {
            // deserialize:
            // -- TxShaMapItem for building a ShaMap for verification
            // -- Tx
            // -- TxMetaData for Tx ordering
            Serializer shaMapItemData(
                reply.transaction(i).data(), reply.transaction(i).size());

            SerialIter txMetaSit(makeSlice(reply.transaction(i)));
            SerialIter txSit(txMetaSit.getSlice(txMetaSit.getVLDataLength()));
            SerialIter metaSit(txMetaSit.getSlice(txMetaSit.getVLDataLength()));

            auto tx = std::make_shared<STTx const>(txSit);
            if (!tx)
            {
                JLOG(journal_.debug())
                    << "ReplayDeltaResponse: malformed (tx deserialize)";
                return ReplayMsgStatus::Malformed;
            }
            auto tid = tx->getTransactionID();
            STObject meta(metaSit, sfMetadata);
            orderedTxns.emplace(meta[sfTransactionIndex], std::move(tx));

            if (!txMap.addGiveItem(
                    SHAMapNodeType::tnTRANSACTION_MD,
                    make_shamapitem(tid, shaMapItemData.slice())))
            {
                JLOG(journal_.debug())
                    << "ReplayDeltaResponse: malformed (tx map add)";
                return ReplayMsgStatus::Malformed;
            }
        }
    }
    catch (std::exception const& e)
    {
        JLOG(journal_.debug())
            << "ReplayDeltaResponse: malformed transactions (" << e.what()
            << ")";
        return ReplayMsgStatus::Malformed;
    }

    if (txMap.getHash().as_uint256() != info.txHash)
    {
        JLOG(journal_.debug())
            << "ReplayDeltaResponse: malformed (transactions verify failed)";
        return ReplayMsgStatus::Malformed;
    }

    replayer_.gotReplayDelta(info, std::move(orderedTxns));
    return ReplayMsgStatus::Ok;
}

}  // namespace ripple
