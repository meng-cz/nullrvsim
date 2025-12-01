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

#define FetchBlockInstNum (8)

#define FetchBlockSizeWidth (6)

#define FetchBlockAlignWidth (FetchBlockSizeWidth - 1)

#define FetchBlockSize (1UL << FetchBlockSizeWidth)

#define FetchBlockAlignSize (1UL << FetchBlockAlignWidth)

#define ResolveEntryBranchNumber (2)


#define PathTableSize (16)

#define GlobalTableSize (64)



#define BaseTableTakenCtrWidth (2)


#define MicroBTBNumEntries (32)

#define MicroBTBTagWidth (22)



#define MbtbNumEntries (8192)

#define MbtbNumWay (4)

#define MbtbTagWidth (16)

#define MbtbNumAlignBanks (FetchBlockSize/FetchBlockAlignSize)

#define NumBtbResultEntries (MbtbNumAlignBanks*MbtbNumWay)


#define ScCtrWidth (3)

#define ScNumWays (4)


#define IttageNumTables (4)

#define IttageConfidenceCntWidth (2)

#define IttageUsefulCntWidth (2)



