// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2014-2017 The Vertcoin Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "pow.h"

#include "arith_uint256.h"
#include "bignum.h"
#include "chain.h"
#include "primitives/block.h"
#include "uint256.h"

static CBigNum bnProofOfWorkLimit(~uint256_old(0) >> 20);

unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params)
{
    static const int64_t        BlocksTargetSpacing  = 2.5 * 60; // 2.5 minutes
    unsigned int                TimeDaySeconds       = 60 * 60 * 24;
    int64_t                     PastSecondsMin       = TimeDaySeconds * 0.25;
    int64_t                     PastSecondsMax       = TimeDaySeconds * 7;
    uint64_t                    PastBlocksMin        = PastSecondsMin / BlocksTargetSpacing;
    uint64_t                    PastBlocksMax        = PastSecondsMax / BlocksTargetSpacing;


	int nHeight = pindexLast->nHeight + 1;
	if(Params().NetworkIDString() == CBaseChainParams::TESTNET)
	{
	    if(nHeight < 2116) {
	    	return GetNextWorkRequired_Bitcoin(pindexLast, pblock, params);
	    }

        // Set this to the testnet fork block
        // Testnet set to fork at 100
        if(pindexLast->nHeight+1 == 100)
        {
            LogPrintf("Getting diff at %i. Diff = 0\n", pindexLast->nHeight+1);
            return 0x1e0ffff0;
        }

  	    // testnet to 12 block difficulty adjustment interval
        if ((pindexLast->nHeight+1) % params.nKGWInterval != 0)
        {
	        CBigNum bnNew;
	        bnNew.SetCompact(pindexLast->nBits);
	        if (bnNew > bnProofOfWorkLimit) { bnNew = bnProofOfWorkLimit; }
            	LogPrintf("Testnet Difficulty Retarget - Kimoto Gravity Well\n");
	        return bnNew.GetCompact();
        }
}
	else
	{
		if (nHeight < 26754) {
		    return GetNextWorkRequired_Bitcoin(pindexLast, pblock, params);
		}
		else if (nHeight == 208301) {
	   	    return 0x1e0ffff0;
		}
        return KimotoGravityWell(pindexLast, pblock, BlocksTargetSpacing, PastBlocksMin, PastBlocksMax);
	}
    return KimotoGravityWell(pindexLast, pblock, BlocksTargetSpacing, PastBlocksMin, PastBlocksMax);
}

unsigned int GetNextWorkRequired_Bitcoin(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params)
{
    assert(pindexLast != nullptr);
    unsigned int nProofOfWorkLimit = UintToArith256(params.powLimit).GetCompact();

    // Only change once per difficulty adjustment interval
    if ((pindexLast->nHeight+1) % params.DifficultyAdjustmentInterval() != 0)
    {
        if (params.fPowAllowMinDifficultyBlocks)
        {
            // Special difficulty rule for testnet:
            // If the new block's timestamp is more than 2* 10 minutes
            // then allow mining of a min-difficulty block.
            if (pblock->GetBlockTime() > pindexLast->GetBlockTime() + params.nPowTargetSpacing*2)
                return nProofOfWorkLimit;
            else
            {
                // Return the last non-special-min-difficulty-rules-block
                const CBlockIndex* pindex = pindexLast;
                while (pindex->pprev && pindex->nHeight % params.DifficultyAdjustmentInterval() != 0 && pindex->nBits == nProofOfWorkLimit)
                    pindex = pindex->pprev;
                return pindex->nBits;
            }
        }
        return pindexLast->nBits;
    }

    // Go back by what we want to be 14 days worth of blocks
    int nHeightFirst = pindexLast->nHeight - (params.DifficultyAdjustmentInterval()-1);
    assert(nHeightFirst >= 0);
    const CBlockIndex* pindexFirst = pindexLast->GetAncestor(nHeightFirst);
    assert(pindexFirst);

    return CalculateNextWorkRequired(pindexLast, pindexFirst->GetBlockTime(), params);
}

unsigned int KimotoGravityWell(const CBlockIndex* pindexLast, const CBlockHeader *pblock, uint64_t TargetBlocksSpacingSeconds, uint64_t PastBlocksMin, uint64_t PastBlocksMax) {
    /* current difficulty formula - kimoto gravity well */
    const CBlockIndex *BlockLastSolved                                = pindexLast;
    const CBlockIndex *BlockReading                                = pindexLast;
    uint64_t                                PastBlocksMass                                = 0;
    int64_t                                PastRateActualSeconds                = 0;
    int64_t                                PastRateTargetSeconds                = 0;
    double                                PastRateAdjustmentRatio                = double(1);
    CBigNum                                PastDifficultyAverage;
    CBigNum                                PastDifficultyAveragePrev;
    double                                EventHorizonDeviation;
    double                                EventHorizonDeviationFast;
    double                                EventHorizonDeviationSlow;

    if (BlockLastSolved == NULL || BlockLastSolved->nHeight == 0 || (uint64_t)BlockLastSolved->nHeight < PastBlocksMin) { return UintToArith256(Params().GetConsensus().powLimit).GetCompact(); }

        for (unsigned int i = 1; BlockReading && BlockReading->nHeight > 0; i++) {
            if (PastBlocksMax > 0 && i > PastBlocksMax) { break; }
            PastBlocksMass++;

            if (i == 1)        { PastDifficultyAverage.SetCompact(BlockReading->nBits); }
            else                { PastDifficultyAverage = ((CBigNum().SetCompact(BlockReading->nBits) - PastDifficultyAveragePrev) / i) + PastDifficultyAveragePrev; }
            PastDifficultyAveragePrev = PastDifficultyAverage;

            PastRateActualSeconds                        = BlockLastSolved->GetBlockTime() - BlockReading->GetBlockTime();
            PastRateTargetSeconds                        = TargetBlocksSpacingSeconds * PastBlocksMass;
            PastRateAdjustmentRatio                        = double(1);
            if (PastRateActualSeconds < 0) { PastRateActualSeconds = 0; }
            if (PastRateActualSeconds != 0 && PastRateTargetSeconds != 0) {
                PastRateAdjustmentRatio                        = double(PastRateTargetSeconds) / double(PastRateActualSeconds);
            }
            EventHorizonDeviation                        = 1 + (0.7084 * std::pow((double(PastBlocksMass)/double(144)), -1.228));
            EventHorizonDeviationFast                = EventHorizonDeviation;
            EventHorizonDeviationSlow                = 1 / EventHorizonDeviation;

            if (PastBlocksMass >= PastBlocksMin) {
                    if ((PastRateAdjustmentRatio <= EventHorizonDeviationSlow) || (PastRateAdjustmentRatio >= EventHorizonDeviationFast)) { assert(BlockReading); break; }
            }
            if (BlockReading->pprev == NULL) { assert(BlockReading); break; }
            BlockReading = BlockReading->pprev;
        }

        CBigNum bnNew(PastDifficultyAverage);
        if (PastRateActualSeconds != 0 && PastRateTargetSeconds != 0) {
                bnNew *= PastRateActualSeconds;
                bnNew /= PastRateTargetSeconds;
        }

        if (bnNew > bnProofOfWorkLimit) {
	    bnNew = bnProofOfWorkLimit;
	}

    return bnNew.GetCompact();
}

unsigned int CalculateNextWorkRequired(const CBlockIndex* pindexLast, int64_t nFirstBlockTime, const Consensus::Params& params)
{
    if (params.fPowNoRetargeting)
        return pindexLast->nBits;

    // Limit adjustment step
    int64_t nActualTimespan = pindexLast->GetBlockTime() - nFirstBlockTime;
    if (nActualTimespan < params.nPowTargetTimespan/4)
        nActualTimespan = params.nPowTargetTimespan/4;
    if (nActualTimespan > params.nPowTargetTimespan*4)
        nActualTimespan = params.nPowTargetTimespan*4;

    // Retarget
    const arith_uint256 bnPowLimit = UintToArith256(params.powLimit);
    arith_uint256 bnNew;
    bnNew.SetCompact(pindexLast->nBits);
    bnNew *= nActualTimespan;
    bnNew /= params.nPowTargetTimespan;

    if (bnNew > bnPowLimit)
        bnNew = bnPowLimit;

    return bnNew.GetCompact();
}

bool CheckProofOfWork(uint256 hash, unsigned int nBits, const Consensus::Params& params)
{
    bool fNegative;
    bool fOverflow;
    arith_uint256 bnTarget;

    bnTarget.SetCompact(nBits, &fNegative, &fOverflow);

    // Check range
    if (fNegative || bnTarget == 0 || fOverflow || bnTarget > UintToArith256(params.powLimit))
        return false;

    // Check proof of work matches claimed amount
    if (UintToArith256(hash) > bnTarget)
        return false;

    return true;
}
