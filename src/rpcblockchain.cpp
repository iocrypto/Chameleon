// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "main.h"
#include "bitcoinrpc.h"
#include "kernel.h"
#include "util.h"
#include <cmath>

using namespace json_spirit;
using namespace std;

extern void spj(const CScript& scriptPubKey, Object& out, bool fIncludeHex);
extern void TxToJSON(const CTransaction& tx, const uint256 hashBlock, json_spirit::Object& entry);
extern enum Checkpoints::CPMode CheckpointsMode;

double GetDifficulty(const CBlockIndex* blockindex)
{
    // Floating point number that is a multiple of the minimum difficulty,
    // minimum difficulty = 1.0.
    if (blockindex == NULL)
    {
        if (pindexBest == NULL)
            return 1.0;
        else
            blockindex = GetLastBlockIndex(pindexBest, false);
    }

    return nBitsToDifficulty(blockindex->nBits);
}

double GetPoWMHashPS(int nBlocks, int nHeight)
{
    if ((nHeight < 0) || (nHeight > pindexBest->nHeight))
    {
        nHeight = pindexBest->nHeight;
    }
    int nPowHeight = GetPowHeight(pindexBest);
    if (nPowHeight >= LAST_POW_BLOCK)
        return 0;

    int nPoWInterval = 72;
    int64_t nTargetSpacingWorkMin = 30, nTargetSpacingWork = 30;

    CBlockIndex* pindex = FindBlockByHeight(nHeight);
    CBlockIndex* pindexPrevWork = FindBlockByHeight(nHeight);

    double currentDifficulty = 0.0;
    bool first = true;

    while (pindexPrevWork && (nBlocks > 0))
    {
        if (pindexPrevWork->IsProofOfWork())
        {
            if (first)
            {
                currentDifficulty = GetDifficulty(pindexPrevWork);
            }
            int64_t nActualSpacingWork = pindex->GetBlockTime() - pindexPrevWork->GetBlockTime();
            nTargetSpacingWork = ((nPoWInterval - 1) * nTargetSpacingWork + nActualSpacingWork + nActualSpacingWork) / (nPoWInterval + 1);
            nTargetSpacingWork = max(nTargetSpacingWork, nTargetSpacingWorkMin);
            pindex = pindexPrevWork;
            --nBlocks;
        }

        pindexPrevWork = pindexPrevWork->pprev;
    }

    return currentDifficulty * 4294.967296 / nTargetSpacingWork;
}

double GetPoSKernelPS(int nHeight)
{
    int nPoSInterval = 72;
    double dStakeKernelsTriedAvg = 0;
    int nStakesHandled = 0, nStakesTime = 0;

    CBlockIndex* pindex = pindexBest;;
    CBlockIndex* pindexPrevStake = NULL;

    while (nHeight > 0 && (pindex->nHeight >= nHeight) && pindex->pprev)
    {
        pindex = pindex->pprev;
    }

    while (pindex && nStakesHandled < nPoSInterval)
    {
        if (pindex->IsProofOfStake())
        {
            dStakeKernelsTriedAvg += GetDifficulty(pindex) * 4294967296.0;
            nStakesTime += pindexPrevStake ? (pindexPrevStake->nTime - pindex->nTime) : 0;
            pindexPrevStake = pindex;
            nStakesHandled++;
        }

        pindex = pindex->pprev;
    }

    double result = 0;

    if (nStakesTime)
        result = dStakeKernelsTriedAvg / nStakesTime;

    if (IsProtocolV2(nBestHeight))
        result *= STAKE_TIMESTAMP_MASK + 1;

    return result;
}

Object blockToJSON(const CBlock& block, const CBlockIndex* blockindex, bool fPrintTransactionDetail)
{
    Object result;
    result.push_back(Pair("hash", block.GetHash().GetHex()));
    CMerkleTx txGen(block.vtx[0]);
    txGen.SetMerkleBranch(&block);
    result.push_back(Pair("confirmations", (int)txGen.GetDepthInMainChain()));
    result.push_back(Pair("size", (int)::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION)));
    result.push_back(Pair("height", blockindex->nHeight));
    result.push_back(Pair("version", block.nVersion));
    result.push_back(Pair("merkleroot", block.hashMerkleRoot.GetHex()));
    result.push_back(Pair("mint", ValueFromAmount(blockindex->nMint)));
    result.push_back(Pair("time", (int64_t)block.GetBlockTime()));
    result.push_back(Pair("nonce", (uint64_t)block.nNonce));
    result.push_back(Pair("bits", strprintf("%08x", block.nBits)));
    result.push_back(Pair("difficulty", GetDifficulty(blockindex)));
    result.push_back(Pair("blocktrust", leftTrim(blockindex->GetBlockTrust().GetHex(), '0')));
    result.push_back(Pair("chaintrust", leftTrim(blockindex->nChainTrust.GetHex(), '0')));
    if (blockindex->pprev)
        result.push_back(Pair("previousblockhash", blockindex->pprev->GetBlockHash().GetHex()));
    if (blockindex->pnext)
        result.push_back(Pair("nextblockhash", blockindex->pnext->GetBlockHash().GetHex()));

    result.push_back(Pair("flags", strprintf("%s%s", blockindex->IsProofOfStake()? "proof-of-stake" : "proof-of-work", blockindex->GeneratedStakeModifier()? " stake-modifier": "")));
    result.push_back(Pair("proofhash", blockindex->hashProof.GetHex()));
    result.push_back(Pair("entropybit", (int)blockindex->GetStakeEntropyBit()));
    result.push_back(Pair("modifier", strprintf("%016"PRIx64, blockindex->nStakeModifier)));
    result.push_back(Pair("modifierchecksum", strprintf("%08x", blockindex->nStakeModifierChecksum)));
    Array txinfo;
    BOOST_FOREACH (const CTransaction& tx, block.vtx)
    {
        if (fPrintTransactionDetail)
        {
            Object entry;

            entry.push_back(Pair("txid", tx.GetHash().GetHex()));
            TxToJSON(tx, 0, entry);

            txinfo.push_back(entry);
        }
        else
            txinfo.push_back(tx.GetHash().GetHex());
    }

    result.push_back(Pair("tx", txinfo));

    if (block.IsProofOfStake())
        result.push_back(Pair("signature", HexStr(block.vchBlockSig.begin(), block.vchBlockSig.end())));

    return result;
}
Object blockToJSON_(const CBlock& block, const CBlockIndex* blockindex, bool txDetails = false)
{
    Object result;
    result.push_back(Pair("hash", block.GetHash().GetHex()));
    int confirmations = -1;
    // Only report confirmations if the block is on the main chain
    CMerkleTx txGen(block.vtx[0]);
    txGen.SetMerkleBranch(&block);
    result.push_back(Pair("confirmations", (int)txGen.GetDepthInMainChain()));
    result.push_back(Pair("size", (int)::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION)));
    result.push_back(Pair("height", blockindex->nHeight));
    result.push_back(Pair("version", block.nVersion));
    result.push_back(Pair("merkleroot", block.hashMerkleRoot.GetHex()));
    Array txs;
    BOOST_FOREACH (const CTransaction& tx, block.vtx) {
        if (txDetails) {
            Object objTx;
            TxToJSON(tx, uint256(0), objTx);
            txs.push_back(objTx);
        } else
            txs.push_back(tx.GetHash().GetHex());
    }
    result.push_back(Pair("tx", txs));
    result.push_back(Pair("time", block.GetBlockTime()));
    result.push_back(Pair("nonce", (uint64_t)block.nNonce));
    result.push_back(Pair("bits", strprintf("%08x", block.nBits)));
    result.push_back(Pair("difficulty", GetDifficulty(blockindex)));
    result.push_back(Pair("chainwork", blockindex->nChainTrust.GetHex()));

    if (blockindex->pprev)
        result.push_back(Pair("previousblockhash", blockindex->pprev->GetBlockHash().GetHex()));
    result.push_back(Pair("nextblockhash", blockindex->pnext->GetBlockHash().GetHex()));
    return result;
}

Value getbestblockhash(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getbestblockhash\n"
            "Returns the hash of the best block in the longest block chain.");

    return hashBestChain.GetHex();
}

Value getblockcount(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getblockcount\n"
            "Returns the number of blocks in the longest block chain.");

    return nBestHeight;
}

Value getpowblocks(const Array& params, bool fHelp)
{
    if (fHelp || params.size() > 1)
        throw runtime_error(
            "getpowblocks [block]\n"
            "Returns the number of PoW blocks mined in the longest block chain at height `block`, defaults to best height.");

    int nHeight = (params.size() > 0) ? params[0].get_int() : nBestHeight;
    if (nHeight < 0 || nHeight > nBestHeight)
        throw runtime_error("Block number out of range.");

    CBlockIndex* block = FindBlockByHeight(nHeight);
    return GetPowHeight(block);
}

Value getpowblocksleft(const Array& params, bool fHelp)
{
    if (fHelp || params.size() > 1)
        throw runtime_error(
            "getpowblocksleft [block]\n"
            "Returns the number of PoW blocks left for mining in the longest block chain at height `block`, defaults to best height..");

    int nHeight = (params.size() > 0) ? params[0].get_int() : nBestHeight;
    if (nHeight < 0 || nHeight > nBestHeight)
        throw runtime_error("Block number out of range.");

    CBlockIndex* block = FindBlockByHeight(nHeight);
    int powHeight = GetPowHeight(block);

    return (powHeight > LAST_POW_BLOCK) ?
        0 : LAST_POW_BLOCK - powHeight;
}

// TODO: Move somewhere more accessible
double GetBlocktime(CBlockIndex * block, int blocks,
                    bool proofOfWork, bool proofOfStake)
{
    if (!block || !(block->pprev) || (blocks <= 0)
        || (!proofOfStake && !proofOfWork))
    {
        return 0.0;
    }

    CBlockIndex * end = block->pprev;
    int checked = blocks;
    while (end->pprev && (checked > 0))
    {
        if ((proofOfStake && end->IsProofOfStake()) ||
            (proofOfWork && end->IsProofOfWork()))
        {
            --checked;
        }
        end = end->pprev;
    }
    return abs(static_cast<double>(block->nTime - end->nTime)) / static_cast<double>(blocks);
}

Value getpowtimeleft(const Array& params, bool fHelp)
{
    if (fHelp || params.size() > 2)
        throw runtime_error(
            "getpowtimeleft [time-unit] [block]\n"
            "Returns the number of PoW blocks left for mining in the longest block chain at height `block`, defaults to best height..");

    int nHeight = (params.size() > 1) ? params[1].get_int() : nBestHeight;

    if (nHeight < 0 || nHeight > nBestHeight)
        throw runtime_error("Block number out of range.");

    int divisor = 1;

    if (params.size() > 0)
    {
        std::string timeUnit = params[0].get_str();
        if (timeUnit == "d" || timeUnit == "days")
        {
            divisor = 60 * 60 * 24;
        }
        else if (timeUnit == "h" || timeUnit == "hours")
        {
            divisor = 60 * 60;
        }
        else if (timeUnit == "m" || timeUnit == "minutes")
        {
            divisor = 60;
        }
        else if (timeUnit == "s" || timeUnit == "seconds")
        {
            divisor = 1;
        }
        else
        {
            throw runtime_error("The time-unit should be one of {d,h,m,s,days,hours,minutes,seconds}.");
        }
    }
    CBlockIndex* block = FindBlockByHeight(nHeight);
    int powHeight = GetPowHeight(block);
    int powLeft = (powHeight > LAST_POW_BLOCK) ?
        0 : LAST_POW_BLOCK - powHeight;
    double blocktime = GetBlocktime(block, 300, true, false);
    return (blocktime * powLeft) / divisor;
}

Value getdifficulty(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getdifficulty\n"
            "Returns the difficulty as a multiple of the minimum difficulty.");

    Object obj;
    obj.push_back(Pair("proof-of-work",        GetDifficulty()));
    obj.push_back(Pair("proof-of-stake",       GetDifficulty(GetLastBlockIndex(pindexBest, true))));
    obj.push_back(Pair("search-interval",      (int)nLastCoinStakeSearchInterval));
    return obj;
}


Value settxfee(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 1 || AmountFromValue(params[0]) < MIN_TX_FEE)
        throw runtime_error(
            "settxfee <amount>\n"
            "<amount> is a real and is rounded to the nearest 0.01");

    nTransactionFee = AmountFromValue(params[0]);
    nTransactionFee = (nTransactionFee / CENT) * CENT;  // round to cent

    return true;
}

Value getrawmempool(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getrawmempool\n"
            "Returns all transaction ids in memory pool.");

    vector<uint256> vtxid;
    mempool.queryHashes(vtxid);

    Array a;
    BOOST_FOREACH(const uint256& hash, vtxid)
        a.push_back(hash.ToString());

    return a;
}

Value getblockhash(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "getblockhash <index>\n"
            "Returns hash of block in best-block-chain at <index>.");

    int nHeight = params[0].get_int();
    if (nHeight < 0 || nHeight > nBestHeight)
        throw runtime_error("Block number out of range.");

    CBlockIndex* pblockindex = FindBlockByHeight(nHeight);
    return pblockindex->phashBlock->GetHex();
}

Value getblock(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
            "getblock <hash> [txinfo]\n"
            "txinfo optional to print more detailed tx info\n"
            "Returns details of a block with given block-hash.");

    std::string strHash = params[0].get_str();
    uint256 hash(strHash);


    if (mapBlockIndex.count(hash) == 0)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");

    CBlock block;
    CBlockIndex* pblockindex = mapBlockIndex[hash];
    block.ReadFromDisk(pblockindex, true);
    if (params.size() == 2 && params[1].get_bool() == false) {
        CDataStream ssBlock(SER_NETWORK, PROTOCOL_VERSION);
        ssBlock << block;
        std::string strHex = HexStr(ssBlock.begin(), ssBlock.end());
        return strHex;
    }

    return blockToJSON(block, pblockindex, false);
}

Value getblockbynumber(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
            "getblockbynumber <number> [txinfo]\n"
            "txinfo optional to print more detailed tx info\n"
            "Returns details of a block with given block-number.");

    int nHeight = params[0].get_int();
    if (nHeight < 0 || nHeight > nBestHeight)
        throw runtime_error("Block number out of range.");

    CBlock block;
    CBlockIndex* pblockindex = mapBlockIndex[hashBestChain];
    while (pblockindex->nHeight > nHeight)
        pblockindex = pblockindex->pprev;

    uint256 hash = *pblockindex->phashBlock;

    pblockindex = mapBlockIndex[hash];
    block.ReadFromDisk(pblockindex, true);

    return blockToJSON(block, pblockindex, params.size() > 1 ? params[1].get_bool() : false);
}

// ppcoin: get information of sync-checkpoint
Value getcheckpoint(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getcheckpoint\n"
            "Show info of synchronized checkpoint.\n");

    Object result;
    CBlockIndex* pindexCheckpoint;

    result.push_back(Pair("synccheckpoint", Checkpoints::hashSyncCheckpoint.ToString().c_str()));
    pindexCheckpoint = mapBlockIndex[Checkpoints::hashSyncCheckpoint];
    result.push_back(Pair("height", pindexCheckpoint->nHeight));
    result.push_back(Pair("timestamp", DateTimeStrFormat(pindexCheckpoint->GetBlockTime()).c_str()));

    // Check that the block satisfies synchronized checkpoint
    if (CheckpointsMode == Checkpoints::STRICT)
        result.push_back(Pair("policy", "strict"));

    if (CheckpointsMode == Checkpoints::ADVISORY)
        result.push_back(Pair("policy", "advisory"));

    if (CheckpointsMode == Checkpoints::PERMISSIVE)
        result.push_back(Pair("policy", "permissive"));

    if (mapArgs.count("-checkpointkey"))
        result.push_back(Pair("checkpointmaster", true));

    return result;
}

Value gettxout(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 2 || params.size() > 3)
        throw runtime_error(
            "gettxout \"txid\" n ( includemempool )\n"
            "\nReturns details about an unspent transaction output.\n"
            "\nArguments:\n"
            "1. \"txid\"       (string, required) The transaction id\n"
            "2. n              (numeric, required) vout value\n"
            "3. includemempool  (boolean, optional) Whether to included the mem pool\n"
            "\nResult:\n"
            "{\n"
            "  \"bestblock\" : \"hash\",    (string) the block hash\n"
            "  \"confirmations\" : n,       (numeric) The number of confirmations\n"
            "  \"value\" : x.xxx,           (numeric) The transaction value in btc\n"
            "  \"scriptPubKey\" : {         (json object)\n"
            "     \"asm\" : \"code\",       (string) \n"
            "     \"hex\" : \"hex\",        (string) \n"
            "     \"reqSigs\" : n,          (numeric) Number of required signatures\n"
            "     \"type\" : \"pubkeyhash\", (string) The type, eg pubkeyhash\n"
            "     \"addresses\" : [          (array of string) array of bitcoin addresses\n"
            "        \"bitcoinaddress\"     (string) bitcoin address\n"
            "        ,...\n"
            "     ]\n"
            "  },\n"
            "  \"version\" : n,            (numeric) The version\n"
            "  \"coinbase\" : true|false   (boolean) Coinbase or not\n"
            "  \"coinstake\" : true|false  (boolean) Coinstake or not\n"
            "}\n"
        );

    LOCK(cs_main);

    Object ret;

    uint256 hash;
    hash.SetHex(params[0].get_str());
    int n = params[1].get_int();
    bool mem = true;
    if (params.size() == 3)
        mem = params[2].get_bool();

    CTransaction tx;
    uint256 hashBlock = 0;
    if (!GetTransaction(hash, tx, hashBlock, mem))
      return Value::null;  

    if (n<0 || (unsigned int)n>=tx.vout.size() || tx.vout[n].IsNull())
      return Value::null;

    if (hashBlock == 0)
    {
      ret.push_back(Pair("bestblock", pindexBest->GetBlockHash().GetHex()));
      ret.push_back(Pair("confirmations", 0));
    }
    else
    {
      map<uint256, CBlockIndex*>::iterator mi = mapBlockIndex.find(hashBlock);
      if (mi != mapBlockIndex.end() && (*mi).second)
      {
        CBlockIndex* pindex = (*mi).second;
        if (pindex->IsInMainChain())
        {
          bool isSpent=false;
          CBlockIndex* p = pindex;
          p=p->pnext;
          for (; p; p = p->pnext)
          {
            CBlock block;
            CBlockIndex* pblockindex = mapBlockIndex[p->GetBlockHash()];
            block.ReadFromDisk(pblockindex, true);
            BOOST_FOREACH(const CTransaction& tx, block.vtx)
            {
              BOOST_FOREACH(const CTxIn& txin, tx.vin)
              {
                if( hash == txin.prevout.hash &&
                   (int64_t)txin.prevout.n )
                {
                  printf("spent at block %s\n", block.GetHash().GetHex().c_str());
                  isSpent=true; break;
                }
              }

              if(isSpent) break;
            }

            if(isSpent) break;
          }

          if(isSpent)
            return Value::null;

          ret.push_back(Pair("confirmations", pindexBest->nHeight - pindex->nHeight + 1));
        }
        else
          return Value::null;
      }
      ret.push_back(Pair("bestblock", hashBlock.GetHex()));
    }

    ret.push_back(Pair("value", ValueFromAmount(tx.vout[n].nValue)));
    Object o;
    spj(tx.vout[n].scriptPubKey, o, true);
    ret.push_back(Pair("scriptPubKey", o));
    ret.push_back(Pair("coinbase", tx.IsCoinBase()));
    ret.push_back(Pair("coinstake", tx.IsCoinStake()));

    return ret;
}
Value getblockchaininfo(const Array& params, bool fHelp)
{
   if (fHelp || params.size() != 0)
        throw runtime_error(
            "getblockchaininfo\n"
            "Returns an object containing various state info regarding block chain processing.\n"
            "\nResult:\n"
            "{\n"
            "  \"chain\": \"xxxx\",        (string) current network name as defined in BIP70 (main, test, regtest)\n"
            "  \"blocks\": xxxxxx,         (numeric) the current number of blocks processed in the server\n"
            "  \"headers\": xxxxxx,        (numeric) the current number of headers we have validated\n"
            "  \"bestblockhash\": \"...\", (string) the hash of the currently best block\n"
            "  \"difficulty\": xxxxxx,     (numeric) the current difficulty\n"
            "  \"verificationprogress\": xxxx, (numeric) estimate of verification progress [0..1]\n"
            "  \"chainwork\": \"xxxx\"     (string) total amount of work in active chain, in hexadecimal\n"
            "}\n"
            "\nExamples:\n" 
            );

    Object obj;
    if(fTestNet)
      obj.push_back(Pair("chain", "test"));
    else
      obj.push_back(Pair("chain", "main"));

    obj.push_back(Pair("blocks", (int)nBestHeight));
    //XXXX obj.push_back(Pair("headers", pindexBestHeader ? pindexBestHeader->nHeight : -1));
    //XXXX obj.push_back(Pair("bestblockhash", chainActive.Tip()->GetBlockHash().GetHex()));
    obj.push_back(Pair("difficulty", GetDifficulty(GetLastBlockIndex(pindexBest, true))));
    //XXXX obj.push_back(Pair("verificationprogress", Checkpoints::GuessVerificationProgress(chainActive.Tip())));
    //XXXX obj.push_back(Pair("chainwork", chainActive.Tip()->nChainWork.GetHex()));
    return obj;
}
Value getnetworkinfo(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getnetworkinfo\n"
            "Returns an object containing various state info regarding P2P networking.\n"
            "\nResult:\n"
            "{\n"
            "  \"version\": xxxxx,                      (numeric) the server version\n"
            "  \"subversion\": \"/Pivx Core:x.x.x.x/\",     (string) the server subversion string\n"
            "  \"protocolversion\": xxxxx,              (numeric) the protocol version\n"
            "  \"localservices\": \"xxxxxxxxxxxxxxxx\", (string) the services we offer to the network\n"
            "  \"timeoffset\": xxxxx,                   (numeric) the time offset\n"
            "  \"connections\": xxxxx,                  (numeric) the number of connections\n"
            "  \"networks\": [                          (array) information per network\n"
            "  {\n"
            "    \"name\": \"xxx\",                     (string) network (ipv4, ipv6 or onion)\n"
            "    \"limited\": true|false,               (boolean) is the network limited using -onlynet?\n"
            "    \"reachable\": true|false,             (boolean) is the network reachable?\n"
            "    \"proxy\": \"host:port\"               (string) the proxy that is used for this network, or empty if none\n"
            "  }\n"
            "  ,...\n"
            "  ],\n"
            "  \"relayfee\": x.xxxxxxxx,                (numeric) minimum relay fee for non-free transactions in pivx/kb\n"
            "  \"localaddresses\": [                    (array) list of local addresses\n"
            "  {\n"
            "    \"address\": \"xxxx\",                 (string) network address\n"
            "    \"port\": xxx,                         (numeric) network port\n"
            "    \"score\": xxx                         (numeric) relative score\n"
            "  }\n"
            "  ,...\n"
            "  ]\n"
            "}\n"
            "\nExamples:\n" 
            );

    Object obj;
    obj.push_back(Pair("version", CLIENT_VERSION));
    obj.push_back(Pair("subversion",
        FormatSubVersion(CLIENT_NAME, CLIENT_VERSION, std::vector<string>())));
    obj.push_back(Pair("protocolversion", PROTOCOL_VERSION));
    //XXXX obj.push_back(Pair("localservices", strprintf("%016x", nLocalServices)));
    obj.push_back(Pair("timeoffset", GetTimeOffset()));
    obj.push_back(Pair("connections", (int)vNodes.size()));
    //XXXX obj.push_back(Pair("networks", GetNetworksInfo()));
    //XXXX obj.push_back(Pair("relayfee", ValueFromAmount(::minRelayTxFee.GetFeePerK())));
    //XXXX Array localAddresses;
    //XXXX {
    //XXXX     LOCK(cs_mapLocalHost);
    //XXXX     BOOST_FOREACH (const PAIRTYPE(CNetAddr, LocalServiceInfo) & item, mapLocalHost) {
    //XXXX         Object rec;
    //XXXX         rec.push_back(Pair("address", item.first.ToString()));
    //XXXX         rec.push_back(Pair("port", item.second.nPort));
    //XXXX         rec.push_back(Pair("score", item.second.nScore));
    //XXXX         localAddresses.push_back(rec);
    //XXXX     }
    //XXXX }
    //XXXX obj.push_back(Pair("localaddresses", localAddresses));
    return obj;
}
Object blockHeaderToJSON(const CBlock& block, const CBlockIndex* blockindex)
{
    Object result;
    result.push_back(Pair("version", block.nVersion));
    if (blockindex->pprev)
        result.push_back(Pair("previousblockhash", blockindex->pprev->GetBlockHash().GetHex()));
    result.push_back(Pair("merkleroot", block.hashMerkleRoot.GetHex()));
    result.push_back(Pair("time", block.GetBlockTime()));
    result.push_back(Pair("bits", strprintf("%08x", block.nBits)));
    result.push_back(Pair("nonce", (uint64_t)block.nNonce));
    return result;
}

Value getblockheader(const Array& params, bool fHelp)
{
  // header
  //  static const int CURRENT_VERSION = 8;
  //  int nVersion;
  //  uint256 hashPrevBlock;
  //  uint256 hashMerkleRoot;
  //  unsigned int nTime;
  //  unsigned int nBits;
  //  unsigned int nNonce;

    std::string strHash = params[0].get_str();
    uint256 hash(strHash);

    bool fVerbose = true;
    if (params.size() > 1)
        fVerbose = params[1].get_bool();

    if (mapBlockIndex.count(hash) == 0)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");

    CBlock block;
    CBlockIndex* pblockindex = mapBlockIndex[hash];

    if(!block.ReadFromDisk(pblockindex, true))
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Can't read block from disk");
    return blockHeaderToJSON(block, pblockindex);
}
