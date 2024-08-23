
#include "launch.h"

#include "cpu/xiangshan/xiangshan.h"
#include "cpu/pipeline5/pipeline5.h"

#include "cache/moesi/l1cachev2.h"
#include "cache/moesi/lastlevelcache.h"
#include "cache/moesi/dmaasl1.h"
#include "cache/moesi/memnode.h"

#include "bus/routetable.h"
#include "bus/symmulcha.h"

#include "sys/multicore.h"

#include "simroot.h"
#include "configuration.h"

using simcpu::CPUSystemInterface;
using simcpu::cpup5::PipeLine5CPU;
using simcpu::xs::XiangShanCPU;

using simcache::moesi::L1CacheMoesiDirNoiV2;
using simcache::moesi::LLCMoesiDirNoi;
using simcache::moesi::DMAL1MoesiDirNoi;
using simcache::moesi::MemoryNode;

using simcache::MemCtrlLineAddrMap;

using simbus::BusPortT;
using simbus::BusPortMapping;
using simbus::BusRouteTable;
using simbus::SymmetricMultiChannelBus;

namespace launch {




}
