// CacheProfile.cpp
// PIN tool: private lock-free L1/L2 per thread, locked inclusive shared LLC

#include "pin.H"
#include <iostream>
#include <vector>
#include <stdint.h>
#include <string>
#include <fstream>
#include <unordered_map>
#include <algorithm>
#include <limits>
#include <cassert>

using std::string;
using std::vector;

/* ---------- KNOBS ---------- */

KNOB<UINT64> KnobL1Size(KNOB_MODE_WRITEONCE, "pintool", "l1_size", "32768", "L1 size");
KNOB<UINT32> KnobL1Assoc(KNOB_MODE_WRITEONCE, "pintool", "l1_assoc", "8", "L1 assoc");
KNOB<UINT32> KnobL1Line(KNOB_MODE_WRITEONCE, "pintool", "l1_line", "64", "L1 line");

KNOB<UINT64> KnobL2Size(KNOB_MODE_WRITEONCE, "pintool", "l2_size", "262144", "L2 size");
KNOB<UINT32> KnobL2Assoc(KNOB_MODE_WRITEONCE, "pintool", "l2_assoc", "8", "L2 assoc");
KNOB<UINT32> KnobL2Line(KNOB_MODE_WRITEONCE, "pintool", "l2_line", "64", "L2 line");

KNOB<UINT64> KnobLLCSize(KNOB_MODE_WRITEONCE, "pintool", "llc_size", "8388608", "LLC size");
KNOB<UINT32> KnobLLCAssoc(KNOB_MODE_WRITEONCE, "pintool", "llc_assoc", "16", "LLC assoc");
KNOB<UINT32> KnobLLCLine(KNOB_MODE_WRITEONCE, "pintool", "llc_line", "64", "LLC line");

KNOB<UINT64> KnobPeriod(KNOB_MODE_WRITEONCE, "pintool", "period", "1000000", "instruction period");
KNOB<string> KnobOutput(KNOB_MODE_WRITEONCE, "pintool", "output", "data.csv", "CSV output");

static const uint64_t NO_EVICT = std::numeric_limits<uint64_t>::max();

/* ---------- Cache model ---------- */

struct CacheEntry {
    uint64_t tag;
    uint64_t last_used;
};

class CacheLevel {
public:
    void Init(uint64_t size, uint32_t assoc_, uint32_t line) {
        assoc = assoc_;
        line_size = line;
        sets = size / (assoc * line_size);
        if (sets == 0) sets = 1;
        cache.assign(sets, vector<CacheEntry>());
        access = miss = ts = 0;
    }

    bool Probe(uint64_t addr) {
        uint64_t line_addr = addr / line_size;
        uint64_t set = line_addr % sets;
        uint64_t tag = line_addr / sets;

        access++;
        ts++;

        for (auto &e : cache[set]) {
            if (e.tag == tag) {
                e.last_used = ts;
                return true;
            }
        }
        miss++;
        return false;
    }

    uint64_t InsertLine(uint64_t addr) {
        uint64_t line_addr = addr / line_size;
        uint64_t set = line_addr % sets;
        uint64_t tag = line_addr / sets;

        auto &s = cache[set];
        ts++;

        if (s.size() < assoc) {
            s.push_back({tag, ts});
            return NO_EVICT;
        }

        size_t lru = 0;
        for (size_t i = 1; i < s.size(); i++)
            if (s[i].last_used < s[lru].last_used)
                lru = i;

        uint64_t evicted_line = (s[lru].tag * sets + set) * line_size;
        s[lru] = {tag, ts};
        return evicted_line;
    }

    void InvalidateLine(uint64_t addr) {
        uint64_t line_addr = addr / line_size;
        uint64_t set = line_addr % sets;
        uint64_t tag = line_addr / sets;

        auto &s = cache[set];
        for (size_t i = 0; i < s.size(); i++) {
            if (s[i].tag == tag) {
                s.erase(s.begin() + i);
                return;
            }
        }
    }

    bool ContainsLine(uint64_t addr) const {
        uint64_t line_addr = addr / line_size;
        uint64_t set = line_addr % sets;
        uint64_t tag = line_addr / sets;

        for (const auto &e : cache[set])
            if (e.tag == tag) return true;
        return false;
    }

    uint64_t Access() const { return access; }
    uint64_t Miss() const { return miss; }
    uint32_t Line() const { return line_size; }

private:
    uint32_t assoc = 0, line_size = 64;
    uint64_t sets = 1;
    uint64_t access = 0, miss = 0, ts = 0;
    vector<vector<CacheEntry>> cache;
};

/* ---------- Per-thread state ---------- */

struct ThreadState {
    CacheLevel L1, L2;
    UINT64 inst = 0, lastInst = 0;
    UINT64 lastL1A = 0, lastL1M = 0;
    UINT64 lastL2A = 0, lastL2M = 0;
    UINT64 lastLLCA = 0, lastLLCM = 0;
};

/* ---------- Global state ---------- */

static std::unordered_map<THREADID, ThreadState> threads;
static CacheLevel LLC;
static PIN_LOCK llcLock;
static PIN_LOCK logLock;
static std::ofstream logFile;
bool gotROI = false;

/* ---------- Thread lifecycle ---------- */

VOID ThreadStart(THREADID tid, CONTEXT *, INT32, VOID *) {
    ThreadState &ts = threads[tid];
    ts.L1.Init(KnobL1Size.Value(), KnobL1Assoc.Value(), KnobL1Line.Value());
    ts.L2.Init(KnobL2Size.Value(), KnobL2Assoc.Value(), KnobL2Line.Value());
}

/* ---------- Logging ---------- */

VOID LogIfNeeded(THREADID tid) {
    ThreadState &ts = threads[tid];
    if (ts.inst - ts.lastInst < KnobPeriod.Value()) return;

    PIN_GetLock(&logLock, tid + 1);

    logFile << tid << "," << ts.inst << "," 
            << ts.L1.Access() - ts.lastL1A << "," << ts.L1.Miss() - ts.lastL1M << "," << ((ts.L1.Miss() - ts.lastL1M)/((ts.L1.Access() - ts.lastL1A)*1.0))*100.0 << ","
            << ts.L2.Access() - ts.lastL2A << "," << ts.L2.Miss() - ts.lastL2M << "," << ((ts.L2.Miss() - ts.lastL2M)/((ts.L2.Access() - ts.lastL2A)*1.0))*100.0 << ","
            << LLC.Access() - ts.lastLLCA << "," << LLC.Miss() - ts.lastLLCM << "," << ((LLC.Miss() - ts.lastLLCM)/((LLC.Access() - ts.lastLLCA)*1.0))*100.0 << "\n";

    ts.lastInst = ts.inst;
    ts.lastL1A = ts.L1.Access(); ts.lastL1M = ts.L1.Miss();
    ts.lastL2A = ts.L2.Access(); ts.lastL2M = ts.L2.Miss();
    ts.lastLLCA = LLC.Access();  ts.lastLLCM = LLC.Miss();

    PIN_ReleaseLock(&logLock);
}

/* ---------- Analysis ---------- */

BOOL ShouldInstrument() {
    if (gotROI) return true;
    else return false;
}

VOID CountInst(THREADID tid) {
    threads[tid].inst++;
    LogIfNeeded(tid);
}

VOID ProcessMem(THREADID tid, ADDRINT addr, UINT32 size) {
    ThreadState &ts = threads[tid];

    uint32_t line = std::min(ts.L1.Line(), std::min(ts.L2.Line(), KnobLLCLine.Value()));

    uint64_t start = addr;
    uint64_t end = addr + (size ? size : 1) - 1;

    for (uint64_t a = (start / line) * line; a <= end; a += line) {

        if (ts.L1.Probe(a)) {
            assert(ts.L2.ContainsLine(a) || LLC.ContainsLine(a));
            continue;
        }

        if (ts.L2.Probe(a)) {
            uint64_t ev = ts.L1.InsertLine(a);
            (void)ev;
            assert(ts.L2.ContainsLine(a));
            continue;
        }

        PIN_GetLock(&llcLock, tid + 1);

        if (LLC.Probe(a)) {
            uint64_t ev2 = ts.L2.InsertLine(a);
            if (ev2 != NO_EVICT) {
                ts.L1.InvalidateLine(ev2);
                assert(!ts.L1.ContainsLine(ev2));
            }
            uint64_t ev1 = ts.L1.InsertLine(a);
            (void)ev1;
            PIN_ReleaseLock(&llcLock);
            continue;
        }

        uint64_t ev_llc = LLC.InsertLine(a);
        if (ev_llc != NO_EVICT) {
            for (auto &p : threads) {
                p.second.L2.InvalidateLine(ev_llc);
                p.second.L1.InvalidateLine(ev_llc);
                assert(!p.second.L2.ContainsLine(ev_llc));
                assert(!p.second.L1.ContainsLine(ev_llc));
            }
        }

        PIN_ReleaseLock(&llcLock);

        uint64_t ev2 = ts.L2.InsertLine(a);
        if (ev2 != NO_EVICT) {
            ts.L1.InvalidateLine(ev2);
            assert(!ts.L1.ContainsLine(ev2));
        }

        uint64_t ev1 = ts.L1.InsertLine(a);
        (void)ev1;
    }
}

VOID RecordRead(THREADID tid, ADDRINT addr, UINT32 size) {
    ProcessMem(tid, addr, size);
}

VOID RecordWrite(THREADID tid, ADDRINT addr, UINT32 size) {
    ProcessMem(tid, addr, size);
}

/* ---------- Instrumentation ---------- */

VOID Instruction(INS ins, VOID *) {

    if (INS_Mnemonic(ins) == "XCHG" && INS_OperandReg(ins, 0) == REG_ECX && INS_OperandReg(ins, 1) == REG_ECX) {
        std::cerr << "ROI Begin in PIN Extraction.\n";
        gotROI = true;
    }

    INS_InsertIfCall(ins, IPOINT_BEFORE, (AFUNPTR)ShouldInstrument, IARG_END);
    INS_InsertThenCall(ins, IPOINT_BEFORE, (AFUNPTR)CountInst, IARG_THREAD_ID, IARG_END);

    if (INS_IsMemoryRead(ins)) {
        INS_InsertIfCall(ins, IPOINT_BEFORE, (AFUNPTR)ShouldInstrument, IARG_END);
        INS_InsertThenCall(ins, IPOINT_BEFORE, (AFUNPTR)RecordRead, IARG_THREAD_ID, IARG_MEMORYREAD_EA, IARG_MEMORYREAD_SIZE, IARG_END);
    }

    if (INS_IsMemoryWrite(ins)) {
        INS_InsertIfCall(ins, IPOINT_BEFORE, (AFUNPTR)ShouldInstrument, IARG_END);
        INS_InsertThenCall(ins, IPOINT_BEFORE, (AFUNPTR)RecordWrite, IARG_THREAD_ID, IARG_MEMORYWRITE_EA, IARG_MEMORYWRITE_SIZE, IARG_END);
    }
}

/* ---------- Fini ---------- */

VOID Fini(INT32, VOID *) {
    logFile.close();
}

/* ---------- Main ---------- */

int main(int argc, char *argv[]) {
    if (PIN_Init(argc, argv)) return 1;

    PIN_InitLock(&llcLock);
    PIN_InitLock(&logLock);

    LLC.Init(KnobLLCSize.Value(), KnobLLCAssoc.Value(), KnobLLCLine.Value());

    logFile.open(KnobOutput.Value().c_str());
    logFile << std::setfill(' ');
    logFile << "Thread_ID,Inst_Count,L1_Access,L1_Misses,L1 MR,L2_Access,L2_Misses,L2_MR,LLC_Access,LLC_Misses,LLC_MR\n";

    PIN_AddThreadStartFunction(ThreadStart, nullptr);
    INS_AddInstrumentFunction(Instruction, nullptr);
    PIN_AddFiniFunction(Fini, nullptr);

    PIN_StartProgram();
    return 0;
}
