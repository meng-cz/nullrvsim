// MIT License

// Copyright (c) 2024 Meng Chengzhen, in Shandong University

// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:

// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#pragma once

#include "common.h"

namespace xsv3sys {

// ---------------------------- Fetch Configs ---------------------------- //

constexpr uint32_t FetchBlockInstNum = 8;

constexpr uint32_t FetchBlockSizeWidth = 6;
constexpr uint32_t FetchBlockAlignWidth = FetchBlockSizeWidth - 1;

constexpr uint32_t FetchBlockSize = (1u << FetchBlockSizeWidth);
constexpr uint32_t FetchBlockAlignSize = (1u << FetchBlockAlignWidth);

constexpr uint32_t ResolveEntryBranchNumber = 2;


// ---------------------------- Branch Predictor Configs ---------------------------- //



constexpr uint32_t PathTableSize = 16;
constexpr uint32_t GlobalTableSize = 64;

// ---------------------------- Micro BTB Configs ---------------------------- //

constexpr uint32_t MicroBTBNumEntries = 32;
constexpr uint32_t MicroBTBTagWidth = 22;

// ---------------------------- Main BTB Configs ---------------------------- //

constexpr uint32_t MbtbNumEntries = 8192;
constexpr uint32_t MbtbNumWay = 4;
constexpr uint32_t MbtbTagWidth = 16;

// ---------------------------- TAGE Configs ---------------------------- //

constexpr uint32_t TageNumTables = 8;
constexpr uint32_t TageHistoryLengths[TageNumTables] = {4, 9, 17, 31, 58, 109, 211, 407};

constexpr uint32_t TageNumSets = 1024;
constexpr uint32_t TageNumWays = 4;
constexpr uint32_t TageTagWidth = 13;

constexpr uint32_t TageBaseTableTakenCtrWidth = 2;
constexpr uint32_t TageTakenCtrWidth = 3;
constexpr uint32_t TageUsefulCtrWidth = 2;

constexpr uint32_t TageMaxHistoryLength = TageHistoryLengths[TageNumTables - 1];

// ---------------------------- SC Configs ---------------------------- //

constexpr uint32_t ScCtrWidth = 3;
constexpr uint32_t ScNumWays = 4;

// ---------------------------- ITTAGE Configs ---------------------------- //

constexpr uint32_t IttageNumTables = 4;
constexpr uint32_t IttageConfidenceCntWidth = 2;
constexpr uint32_t IttageUsefulCntWidth = 2;


// ---------------------------- History Configs ---------------------------- //

constexpr uint32_t BranchHistoryWidth = TageMaxHistoryLength;

}


