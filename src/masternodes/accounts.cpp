// Copyright (c) 2020 DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/accounts.h>

/// @attention make sure that it does not overlap with those in masternodes.cpp/tokens.cpp/undos.cpp/accounts.cpp !!!
const unsigned char CAccountsView::ByBalanceKey::prefix = 'a';
const unsigned char CAccountsView::ByAccountHistoryKey::prefix = 'h'; // don't intersects with CMintedHeadersView::MintedHeaders::prefix due to different DB

void CAccountsView::ForEachBalance(std::function<bool(CScript const & owner, CTokenAmount const & amount)> callback, BalanceKey start) const
{
    ForEach<ByBalanceKey, BalanceKey, CAmount>([&callback] (BalanceKey const & key, CAmount const & val) {
        return callback(key.owner, CTokenAmount{key.tokenID, val});
    }, start);
}

CTokenAmount CAccountsView::GetBalance(CScript const & owner, DCT_ID tokenID) const
{
    CAmount val;
    bool ok = ReadBy<ByBalanceKey>(BalanceKey{owner, tokenID}, val);
    if (ok) {
        return CTokenAmount{tokenID, val};
    }
    return CTokenAmount{tokenID, 0};
}

Res CAccountsView::SetBalance(CScript const & owner, CTokenAmount amount)
{
    if (amount.nValue != 0) {
        WriteBy<ByBalanceKey>(BalanceKey{owner, amount.nTokenId}, amount.nValue);
    } else {
        EraseBy<ByBalanceKey>(BalanceKey{owner, amount.nTokenId});
    }
    return Res::Ok();
}

Res CAccountsView::AddBalance(CScript const & owner, CTokenAmount amount)
{
    if (amount.nValue == 0) {
        return Res::Ok();
    }
    auto balance = GetBalance(owner, amount.nTokenId);
    auto res = balance.Add(amount.nValue);
    if (!res.ok) {
        return res;
    }
    return SetBalance(owner, balance);
}

Res CAccountsView::SubBalance(CScript const & owner, CTokenAmount amount)
{
    if (amount.nValue == 0) {
        return Res::Ok();
    }
    auto balance = GetBalance(owner, amount.nTokenId);
    auto res = balance.Sub(amount.nValue);
    if (!res.ok) {
        return res;
    }
    return SetBalance(owner, balance);
}

Res CAccountsView::AddBalances(CScript const & owner, CBalances const & balances)
{
    for (const auto& kv : balances.balances) {
        auto res = AddBalance(owner, CTokenAmount{kv.first, kv.second});
        if (!res.ok) {
            return res;
        }
    }
    return Res::Ok();
}

Res CAccountsView::SubBalances(CScript const & owner, CBalances const & balances)
{
    for (const auto& kv : balances.balances) {
        auto res = SubBalance(owner, CTokenAmount{kv.first, kv.second});
        if (!res.ok) {
            return res;
        }
    }
    return Res::Ok();
}

//Res CAccountsView::SetAccountHistory(const CScript & owner, uint32_t height, const uint256 & txid)
//{
//    WriteBy<ByAccountHistoryKey>(AccountHistoryKey{owner, height}, txid);
//    return Res::Ok();
//}

//bool CAccountsView::TrackAffectedAccounts(const CUndo & undo, uint32_t height, const uint256 & txid)
//{
//    bool affected = false;
////   'CUndo' == 'std::map<TBytes, boost::optional<TBytes>> before'
//    using TKey = std::pair<unsigned char, BalanceKey>;
//    for (auto const kv : undo.before) {
//        if (kv.first.at(0) == ByBalanceKey::prefix) {
//            TKey dummy;
//            BytesToDbType(kv.first, dummy);
//            SetAccountHistory(dummy.second.owner, height, txid);
//            affected = true;
//        }
//    }
//    return affected;
//}

void CAccountsView::ForEachAccountHistory(std::function<bool(CScript const & owner, uint32_t height, uint32_t txn, uint256 const & txid, TAmounts const & diffs)> callback, AccountHistoryKey start) const
{
    using TValue = std::pair<uint256, TAmounts>;
    ForEach<ByAccountHistoryKey, AccountHistoryKey, TValue >([&callback] (AccountHistoryKey const & key, TValue const & val) {
        return callback(key.owner,key.blockHeight, key.txn, val.first, val.second);
    }, start);
}

Res CAccountsView::SetAccountHistory(const CScript & owner, uint32_t height, uint32_t txn, const uint256 & txid, TAmounts const & diff)
{
    LogPrintf("DEBUG: SetAccountHistory: owner: %s, block: %i, txn: %d, txid: %s, diffs: %ld\n", owner.GetHex().c_str(), height, txn, txid.ToString().c_str(), diff.size());

    WriteBy<ByAccountHistoryKey>(AccountHistoryKey{owner, height, txn}, std::make_pair(txid, diff));
    return Res::Ok();
}

bool CAccountsView::TrackAffectedAccounts(CStorageKV const & before, MapKV const & diff, uint32_t height, uint32_t txn, const uint256 & txid) {
    std::map<CScript, TAmounts> balancesDiff;
    using TKey = std::pair<unsigned char, BalanceKey>;

    for (auto it = diff.lower_bound({ByBalanceKey::prefix}); it != diff.end() && it->first.at(0) == ByBalanceKey::prefix; ++it) {
        CAmount oldAmount = 0, newAmount = 0;

        if (it->second) {
            BytesToDbType(*it->second, newAmount);
        }
        TBytes beforeVal;
        if (before.Read(it->first, beforeVal)) {
            BytesToDbType(beforeVal, oldAmount);
        }
        TKey balanceKey;
        BytesToDbType(it->first, balanceKey);
        balancesDiff[balanceKey.second.owner][balanceKey.second.tokenID] = newAmount - oldAmount;
    }
    for (auto const & kv : balancesDiff) {
        SetAccountHistory(kv.first, height, txn, txid, kv.second);
    }
    return true;
}
