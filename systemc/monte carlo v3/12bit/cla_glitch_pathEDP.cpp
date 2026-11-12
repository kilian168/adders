// cla_glitch.cpp – 12-bit Carry Lookahead Adder (2-level CLA tree), gate-level switching activity
#include <systemc.h>
#include <iostream>
#include <iomanip>
#include <string>
#include <cassert>
#include <random>
#include <algorithm>
#include <cstdio>
#include <thread>
#include <vector>
#include <sys/wait.h>
#include <unistd.h>

// ==========================================
// Helper functions
// ==========================================
static sc_lv<12> make_12bit_vector(unsigned int value) {
    sc_lv<12> bits;
    for (int i = 0; i < 12; i++) bits[i] = ((value >> i) & 1U) != 0U;
    return bits;
}

static unsigned int to_unsigned_12bit(const sc_lv<12>& bits) {
    unsigned int value = 0;
    for (int i = 0; i < 12; i++)
        if (bits[i].is_01() && bits[i].to_bool()) value += (1U << i);
    return value;
}

// ==========================================
// 0b. GENERIC GATE-LEVEL PRIMITIVE
//     Independently sensitive to each input, with a 1ns delay between
//     evaluation and output commit, so every transient glitch from
//     asynchronously-arriving inputs is counted as a separate switch.
// ==========================================
enum GateOp { GATE_AND, GATE_OR, GATE_XOR };

struct Gate;
static void register_gate(Gate* g);

SC_MODULE(Gate) {
    sc_in<bool> in1, in2;
    sc_out<bool> out;

    GateOp op;
    sc_event ev_commit;
    bool old_value;
    bool pending_value;
    unsigned int calls;
    unsigned int switches;

    SC_HAS_PROCESS(Gate);

    Gate(sc_module_name name, GateOp gate_op)
        : sc_module(name), op(gate_op), old_value(false), pending_value(false),
          calls(0), switches(0)
    {
        SC_METHOD(eval);
        dont_initialize();
        sensitive << in1 << in2;

        SC_METHOD(commit);
        dont_initialize();
        sensitive << ev_commit;

        register_gate(this);
    }

    void reset_switches() { switches = 0; }

    void eval() {
        calls++;
        bool value_a = in1.read();
        bool value_b = in2.read();
        bool next;
        if (op == GATE_AND)      next = value_a & value_b;
        else if (op == GATE_OR)  next = value_a | value_b;
        else /* GATE_XOR */      next = value_a ^ value_b;

        if (next != old_value) {
            old_value = next;
            switches++;
            pending_value = next;
            ev_commit.notify(1, SC_NS);
        }
    }

    void commit() {
        out.write(pending_value);
    }
};

static std::vector<Gate*> g_all_gates;
static void register_gate(Gate* g) { g_all_gates.push_back(g); }
static void reset_all_gate_switches() {
    for (auto* g : g_all_gates) g->reset_switches();
}
static void clear_gate_registry() { g_all_gates.clear(); }

// ==========================================
// 1. 4-BIT CLA BLOCK MODULE (reusable)
//    Gate count: 38 per block
//    Exposes group-Propagate (PG = p3·p2·p1·p0, already c4a6 internally)
//    and group-Generate (GG = g3|p3·g2|p3·p2·g1|p3·p2·p1·g0, already c4o3
//    internally) so the second-level ClaUnit can drive all inter-block
//    carries simultaneously without any new gates here.
//    Wired as real per-gate dependencies (not batched tiers), so the
//    carry-lookahead tree's genuine internal gate-to-gate depth is
//    reflected in both timing and glitch counting.
// ==========================================
SC_MODULE(ClaBlock4) {
    sc_in<sc_lv<4>>  A, B;
    sc_in<bool>      Cin;
    sc_out<sc_lv<4>> Sum;
    sc_out<bool>     Cout;
    sc_out<bool>     PG;   // group propagate = p3·p2·p1·p0  (= c4a6)
    sc_out<bool>     GG;   // group generate  = g3|p3·g2|…   (= c4o3)

    sc_signal<bool> a_bit[4], b_bit[4];
    sc_signal<bool> p[4], g[4];
    sc_signal<bool> c1a, c1o;
    sc_signal<bool> c2a1, c2a2, c2a3, c2o1, c2o2;
    sc_signal<bool> c3a1, c3a2, c3a3, c3a4, c3a5, c3o1, c3o2, c3o3;
    sc_signal<bool> c4a1, c4a2, c4a3, c4a4, c4a5, c4a6, c4a7;
    sc_signal<bool> c4o1, c4o2, c4o3, c4o4;
    sc_signal<bool> s[4];

    Gate *g_p[4], *g_g[4], *g_s[4];
    Gate *g_c1a, *g_c1o;
    Gate *g_c2a1, *g_c2a2, *g_c2a3, *g_c2o1, *g_c2o2;
    Gate *g_c3a1, *g_c3a2, *g_c3a3, *g_c3a4, *g_c3a5, *g_c3o1, *g_c3o2, *g_c3o3;
    Gate *g_c4a1, *g_c4a2, *g_c4a3, *g_c4a4, *g_c4a5, *g_c4a6, *g_c4a7;
    Gate *g_c4o1, *g_c4o2, *g_c4o3, *g_c4o4;

    SC_CTOR(ClaBlock4) {
        SC_METHOD(unpack_inputs);
        dont_initialize();
        sensitive << A << B;

        SC_METHOD(pack_outputs);
        dont_initialize();
        for (int i = 0; i < 4; i++) sensitive << s[i];
        sensitive << c4o4 << c4a6 << c4o3;

        for (int i = 0; i < 4; i++) {
            std::string pn = "P" + std::to_string(i);
            g_p[i] = new Gate(pn.c_str(), GATE_XOR);
            g_p[i]->in1(a_bit[i]); g_p[i]->in2(b_bit[i]); g_p[i]->out(p[i]);

            std::string gn = "G" + std::to_string(i);
            g_g[i] = new Gate(gn.c_str(), GATE_AND);
            g_g[i]->in1(a_bit[i]); g_g[i]->in2(b_bit[i]); g_g[i]->out(g[i]);
        }

        g_c1a = new Gate("C1A", GATE_AND); g_c1a->in1(p[0]); g_c1a->in2(Cin); g_c1a->out(c1a);
        g_c1o = new Gate("C1O", GATE_OR);  g_c1o->in1(g[0]); g_c1o->in2(c1a); g_c1o->out(c1o);

        g_c2a1 = new Gate("C2A1", GATE_AND); g_c2a1->in1(p[1]);  g_c2a1->in2(g[0]);  g_c2a1->out(c2a1);
        g_c2a2 = new Gate("C2A2", GATE_AND); g_c2a2->in1(p[1]);  g_c2a2->in2(p[0]);  g_c2a2->out(c2a2);
        g_c2a3 = new Gate("C2A3", GATE_AND); g_c2a3->in1(c2a2);  g_c2a3->in2(Cin);   g_c2a3->out(c2a3);
        g_c2o1 = new Gate("C2O1", GATE_OR);  g_c2o1->in1(g[1]);  g_c2o1->in2(c2a1);  g_c2o1->out(c2o1);
        g_c2o2 = new Gate("C2O2", GATE_OR);  g_c2o2->in1(c2o1);  g_c2o2->in2(c2a3);  g_c2o2->out(c2o2);

        g_c3a1 = new Gate("C3A1", GATE_AND); g_c3a1->in1(p[2]);  g_c3a1->in2(g[1]);  g_c3a1->out(c3a1);
        g_c3a2 = new Gate("C3A2", GATE_AND); g_c3a2->in1(p[2]);  g_c3a2->in2(p[1]);  g_c3a2->out(c3a2);
        g_c3a3 = new Gate("C3A3", GATE_AND); g_c3a3->in1(c3a2);  g_c3a3->in2(g[0]);  g_c3a3->out(c3a3);
        g_c3a4 = new Gate("C3A4", GATE_AND); g_c3a4->in1(c3a2);  g_c3a4->in2(p[0]);  g_c3a4->out(c3a4);
        g_c3a5 = new Gate("C3A5", GATE_AND); g_c3a5->in1(c3a4);  g_c3a5->in2(Cin);   g_c3a5->out(c3a5);
        g_c3o1 = new Gate("C3O1", GATE_OR);  g_c3o1->in1(g[2]);  g_c3o1->in2(c3a1);  g_c3o1->out(c3o1);
        g_c3o2 = new Gate("C3O2", GATE_OR);  g_c3o2->in1(c3o1);  g_c3o2->in2(c3a3);  g_c3o2->out(c3o2);
        g_c3o3 = new Gate("C3O3", GATE_OR);  g_c3o3->in1(c3o2);  g_c3o3->in2(c3a5);  g_c3o3->out(c3o3);

        g_c4a1 = new Gate("C4A1", GATE_AND); g_c4a1->in1(p[3]);  g_c4a1->in2(g[2]);  g_c4a1->out(c4a1);
        g_c4a2 = new Gate("C4A2", GATE_AND); g_c4a2->in1(p[3]);  g_c4a2->in2(p[2]);  g_c4a2->out(c4a2);
        g_c4a3 = new Gate("C4A3", GATE_AND); g_c4a3->in1(c4a2);  g_c4a3->in2(g[1]);  g_c4a3->out(c4a3);
        g_c4a4 = new Gate("C4A4", GATE_AND); g_c4a4->in1(c4a2);  g_c4a4->in2(p[1]);  g_c4a4->out(c4a4);
        g_c4a5 = new Gate("C4A5", GATE_AND); g_c4a5->in1(c4a4);  g_c4a5->in2(g[0]);  g_c4a5->out(c4a5);
        // c4a6 = p3·p2·p1·p0  ← this IS the group Propagate (PG) of this block
        g_c4a6 = new Gate("C4A6", GATE_AND); g_c4a6->in1(c4a4);  g_c4a6->in2(p[0]);  g_c4a6->out(c4a6);
        g_c4a7 = new Gate("C4A7", GATE_AND); g_c4a7->in1(c4a6);  g_c4a7->in2(Cin);   g_c4a7->out(c4a7);
        g_c4o1 = new Gate("C4O1", GATE_OR);  g_c4o1->in1(g[3]);  g_c4o1->in2(c4a1);  g_c4o1->out(c4o1);
        g_c4o2 = new Gate("C4O2", GATE_OR);  g_c4o2->in1(c4o1);  g_c4o2->in2(c4a3);  g_c4o2->out(c4o2);
        // c4o3 = g3|p3·g2|p3·p2·g1|p3·p2·p1·g0  ← this IS the group Generate (GG) of this block
        g_c4o3 = new Gate("C4O3", GATE_OR);  g_c4o3->in1(c4o2);  g_c4o3->in2(c4a5);  g_c4o3->out(c4o3);
        g_c4o4 = new Gate("C4O4", GATE_OR);  g_c4o4->in1(c4o3);  g_c4o4->in2(c4a7);  g_c4o4->out(c4o4);

        g_s[0] = new Gate("S0", GATE_XOR); g_s[0]->in1(p[0]); g_s[0]->in2(Cin);  g_s[0]->out(s[0]);
        g_s[1] = new Gate("S1", GATE_XOR); g_s[1]->in1(p[1]); g_s[1]->in2(c1o);  g_s[1]->out(s[1]);
        g_s[2] = new Gate("S2", GATE_XOR); g_s[2]->in1(p[2]); g_s[2]->in2(c2o2); g_s[2]->out(s[2]);
        g_s[3] = new Gate("S3", GATE_XOR); g_s[3]->in1(p[3]); g_s[3]->in2(c3o3); g_s[3]->out(s[3]);
    }

    void unpack_inputs() {
        sc_lv<4> va = A.read(), vb = B.read();
        for (int i = 0; i < 4; i++) {
            a_bit[i].write(va[i].is_01() ? va[i].to_bool() : false);
            b_bit[i].write(vb[i].is_01() ? vb[i].to_bool() : false);
        }
    }

    void pack_outputs() {
        sc_lv<4> out;
        for (int i = 0; i < 4; i++) out[i] = s[i].read();
        Sum.write(out);
        Cout.write(c4o4.read());
        // Expose the group-level signals to the second-level ClaUnit.
        // No new gates: c4a6 and c4o3 are already driven by existing gate
        // instances and their switches are already counted below.
        PG.write(c4a6.read());
        GG.write(c4o3.read());
    }

    unsigned int total_block_switches() const {
        unsigned int t = 0;
        for (int i = 0; i < 4; i++) t += g_p[i]->switches + g_g[i]->switches + g_s[i]->switches;
        t += g_c1a->switches + g_c1o->switches;
        t += g_c2a1->switches + g_c2a2->switches + g_c2a3->switches + g_c2o1->switches + g_c2o2->switches;
        t += g_c3a1->switches + g_c3a2->switches + g_c3a3->switches + g_c3a4->switches + g_c3a5->switches +
             g_c3o1->switches + g_c3o2->switches + g_c3o3->switches;
        t += g_c4a1->switches + g_c4a2->switches + g_c4a3->switches + g_c4a4->switches + g_c4a5->switches +
             g_c4a6->switches + g_c4a7->switches;
        t += g_c4o1->switches + g_c4o2->switches + g_c4o3->switches + g_c4o4->switches;
        return t;
    }

    ~ClaBlock4() {
        for (int i = 0; i < 4; i++) { delete g_p[i]; delete g_g[i]; delete g_s[i]; }
        delete g_c1a; delete g_c1o;
        delete g_c2a1; delete g_c2a2; delete g_c2a3; delete g_c2o1; delete g_c2o2;
        delete g_c3a1; delete g_c3a2; delete g_c3a3; delete g_c3a4; delete g_c3a5; delete g_c3o1; delete g_c3o2; delete g_c3o3;
        delete g_c4a1; delete g_c4a2; delete g_c4a3; delete g_c4a4; delete g_c4a5; delete g_c4a6; delete g_c4a7;
        delete g_c4o1; delete g_c4o2; delete g_c4o3; delete g_c4o4;
    }
};

// ==========================================
// 2. SECOND-LEVEL RECURSIVE CARRY LOOKAHEAD TREE (ClaTree)
//    True recursive, multi-level (Brent-Kung-style) carry-lookahead
//    network: combines N blocks' group-P/G in a balanced binary tree
//    rather than one flat N-way lookahead equation, so the Cin-dependent
//    critical path grows with O(log2 N) instead of O(N).
//
//    - Every internal tree node computes a "combine" (super-PG/super-GG
//      for its whole sub-range) purely from its two child sub-ranges'
//      PG/GG. This does NOT depend on Cin at all, so every combine in
//      the whole tree is computable in parallel, immediately, entirely
//      independent of carry propagation.
//    - Every internal node also computes a "split": the Cin fed to its
//      upper child = (lower child's GG) | (lower child's PG & this
//      node's own Cin). This is the ONLY part of the tree whose depth
//      depends on Cin, and it costs exactly 2 gates (1 AND + 1 OR) per
//      tree level — so the deepest leaf's Cin is ready after
//      2*ceil(log2 N) gate delays, not O(N).
//    - Uneven splits (this file has N=3, not a power of 2) put the
//      larger half as the "lower" child (which inherits Cin for free)
//      and the smaller half as the "upper" child (which pays the 2-gate
//      split cost), minimizing worst-case depth.
//
//    Gate count for N leaves: 5*(N-1) + 2. For N=3 (this file) that is
//    12 gates (down from 15 in the old flat single-level ClaUnit3).
//
//    Implementation note: this module is built once per instance from a
//    constructor parameter (N) via a recursive build_range() helper,
//    using sc_vector<sc_in<bool>>/sc_vector<sc_out<bool>> for its N-wide
//    ports (rather than fixed-size arrays) and a small Wire helper that
//    tags whether an operand is the module's own port or an internally
//    built signal, so leaf-level and internal-node gate construction
//    share the same code path. Every AND/OR is still a full gate-level
//    Gate instance with 1ns commit delay and complete switch counting.
//    Verified against a 5000-trial brute-force reference check (for
//    N=2,3,4,8) before use here.
// ==========================================
struct Wire {
    sc_in<bool>* port = nullptr;
    sc_signal<bool>* sig = nullptr;
    Wire() {}
    Wire(sc_in<bool>& p) : port(&p) {}
    Wire(sc_signal<bool>& s) : sig(&s) {}
};

SC_MODULE(ClaTree) {
    sc_vector<sc_in<bool>>  PG, GG;
    sc_in<bool>             Cin;
    sc_vector<sc_out<bool>> C;      // carries into blocks 1..N-1
    sc_out<bool>            Cout;   // overall carry out

    std::vector<Gate*> gates;
    std::vector<sc_signal<bool>*> sigs;
    std::vector<Wire> leaf_cin;     // leaf_cin[i] = resolved Cin wire for block i (i=0 unused)
    sc_signal<bool>* cout_sig;
    int N;

    SC_HAS_PROCESS(ClaTree);

    ClaTree(sc_module_name name, int n_blocks)
        : sc_module(name), PG("PG", n_blocks), GG("GG", n_blocks),
          C("C", n_blocks > 0 ? n_blocks - 1 : 0), N(n_blocks), leaf_cin(n_blocks)
    {
        Wire root_cin(Cin);
        RangeResult root = build_range(0, N, root_cin);
        sc_signal<bool>* cout_and = gate2("COUT_A", GATE_AND, root.pg, Wire(Cin));
        cout_sig = gate2("COUT_O", GATE_OR, root.gg, Wire(*cout_and));

        SC_METHOD(drive_outputs);
        dont_initialize();
        for (int i = 1; i < N; i++) sensitive << *leaf_cin[i].sig;
        sensitive << *cout_sig;
    }

    sc_signal<bool>* new_sig() { auto* s = new sc_signal<bool>(); sigs.push_back(s); return s; }

    static void bind_wire(sc_in<bool>& dst, const Wire& w) {
        if (w.port) dst(*w.port); else dst(*w.sig);
    }

    sc_signal<bool>* gate2(const char* nm, GateOp op, const Wire& a, const Wire& b) {
        Gate* g = new Gate(nm, op);
        bind_wire(g->in1, a);
        bind_wire(g->in2, b);
        sc_signal<bool>* out = new_sig();
        g->out(*out);
        gates.push_back(g);
        return out;
    }

    struct RangeResult { Wire pg; Wire gg; };

    // Recursively builds the tree over leaf range [lo, hi). Returns this
    // whole range's combined (PG, GG); records each interior leaf's
    // resolved Cin wire into leaf_cin[] as it goes.
    RangeResult build_range(int lo, int hi, Wire cin) {
        int size = hi - lo;
        char nm[64];
        if (size == 1) {
            int idx = lo;
            if (idx >= 1) leaf_cin[idx] = cin;
            return RangeResult{ Wire(PG[idx]), Wire(GG[idx]) };
        }
        int half = (size + 1) / 2;     // lower half gets the larger (or equal) share
        int mid = lo + half;
        RangeResult lo_res = build_range(lo, mid, cin);

        std::snprintf(nm, sizeof(nm), "SPLIT_A_%d_%d", lo, mid);
        sc_signal<bool>* and1 = gate2(nm, GATE_AND, lo_res.pg, cin);
        std::snprintf(nm, sizeof(nm), "SPLIT_O_%d_%d", lo, mid);
        sc_signal<bool>* cin_upper_sig = gate2(nm, GATE_OR, lo_res.gg, Wire(*and1));

        RangeResult hi_res = build_range(mid, hi, Wire(*cin_upper_sig));

        std::snprintf(nm, sizeof(nm), "COMB_PG_%d_%d", lo, hi);
        sc_signal<bool>* pg_out = gate2(nm, GATE_AND, lo_res.pg, hi_res.pg);
        std::snprintf(nm, sizeof(nm), "COMB_A_%d_%d", lo, hi);
        sc_signal<bool>* and2 = gate2(nm, GATE_AND, hi_res.pg, lo_res.gg);
        std::snprintf(nm, sizeof(nm), "COMB_O_%d_%d", lo, hi);
        sc_signal<bool>* gg_out = gate2(nm, GATE_OR, hi_res.gg, Wire(*and2));

        return RangeResult{ Wire(*pg_out), Wire(*gg_out) };
    }

    void drive_outputs() {
        for (int i = 1; i < N; i++) C[i - 1].write(leaf_cin[i].sig->read());
        Cout.write(cout_sig->read());
    }

    unsigned int total_unit_switches() const {
        unsigned int t = 0;
        for (auto* g : gates) t += g->switches;
        return t;
    }

    ~ClaTree() {
        for (auto* g : gates) delete g;
        for (auto* s : sigs) delete s;
    }
};

// ==========================================
// 3. 12-BIT CARRY LOOKAHEAD ADDER (3 x 4-bit blocks + 1 ClaTree)
//    All three 4-bit blocks work in parallel; the ClaTree computes the
//    carries into blocks 1/2 via a recursive binary lookahead tree from
//    group-P/G signals (which depend only on A and B, not on Cin), so no
//    carry ripples between blocks and the Cin-dependent depth is
//    O(log2 N) rather than O(N).
//    Total gates: 3 × 38 (blocks) + 12 (ClaTree, N=3) = 126
// ==========================================
SC_MODULE(CarryLookaheadAdder12) {
    static const int NBLK = 3;

    sc_in<sc_lv<12>>  A, B;
    sc_out<sc_lv<12>> Sum;
    sc_out<bool>      Cout;

    sc_signal<sc_lv<4>> a_slice[NBLK], b_slice[NBLK], s_slice[NBLK];
    sc_signal<bool>     blk_cout[NBLK];
    sc_signal<bool>     blk_pg[NBLK], blk_gg[NBLK];
    sc_signal<bool>     c_mid[NBLK - 1];
    sc_signal<bool>     const_zero;           // tied low for block 0's Cin

    ClaBlock4* blk[NBLK];
    ClaTree*   clu;

    SC_CTOR(CarryLookaheadAdder12) {
        for (int i = 0; i < NBLK; i++) {
            std::string name = "BLK" + std::to_string(i);
            blk[i] = new ClaBlock4(name.c_str());
            blk[i]->A(a_slice[i]); blk[i]->B(b_slice[i]);
            blk[i]->Cin(i == 0 ? const_zero : c_mid[i - 1]);
            blk[i]->Sum(s_slice[i]); blk[i]->Cout(blk_cout[i]);
            blk[i]->PG(blk_pg[i]); blk[i]->GG(blk_gg[i]);
        }

        // ---- Second-level ClaTree: computes c_mid[0..1], Cout from group P/G ----
        clu = new ClaTree("CLU", NBLK);
        for (int i = 0; i < NBLK; i++) { clu->PG[i](blk_pg[i]); clu->GG[i](blk_gg[i]); }
        clu->Cin(const_zero);
        for (int i = 0; i < NBLK - 1; i++) clu->C[i](c_mid[i]);
        clu->Cout(Cout);

        SC_METHOD(split_inputs);    dont_initialize(); sensitive << A << B;
        SC_METHOD(combine_outputs); dont_initialize();
        sensitive << s_slice[0] << s_slice[1] << s_slice[2];
    }

    void split_inputs() {
        sc_lv<12> va=A.read(), vb=B.read();
        for (int blki=0; blki<NBLK; blki++) {
            sc_lv<4> a4, b4;
            for (int i=0; i<4; i++) { a4[i]=va[blki*4+i]; b4[i]=vb[blki*4+i]; }
            a_slice[blki].write(a4); b_slice[blki].write(b4);
        }
    }

    void combine_outputs() {
        sc_lv<12> s;
        for (int blki=0; blki<NBLK; blki++) {
            sc_lv<4> s4=s_slice[blki].read();
            for (int i=0; i<4; i++) s[blki*4+i]=s4[i];
        }
        Sum.write(s);
    }

    unsigned int total_switches() const {
        unsigned int total = 0;
        for (int i = 0; i < NBLK; i++) total += blk[i]->total_block_switches();
        total += clu->total_unit_switches();
        return total;
    }

    ~CarryLookaheadAdder12() {
        for (int i = 0; i < NBLK; i++) delete blk[i];
        delete clu;
    }
};

// ==========================================
// 4. TESTBENCH — Monte Carlo random sampling
//    Draws num_samples uniformly random (a, b) pairs instead of
//    exhaustively covering the whole input space, so the same
//    methodology also works for widths where exhaustive coverage is
//    computationally infeasible (e.g. 32-bit).
// ==========================================

// ============================================================================
// Real OpenSTA (Nangate45) max delay per (startpoint pin, endpoint pin),
// extracted from all_critical_paths.csv for CLA-12 (180 pairs).
// Input pin index: 0..11=A[i], 12..23=B[i]. Output pin index: 0..11=Sum[i], 12=Cout.
// ============================================================================
static const int NUM_IN = 24;
static const int NUM_OUT = 13;
static double path_delay[NUM_IN][NUM_OUT];

static void init_path_delay_table() {
    for (int i = 0; i < NUM_IN; i++)
        for (int o = 0; o < NUM_OUT; o++)
            path_delay[i][o] = 0.0;

    path_delay[0][12] = 1.1;
    path_delay[0][0] = 0.39;
    path_delay[0][10] = 1.35;
    path_delay[0][11] = 1.35;
    path_delay[0][1] = 0.46;
    path_delay[0][2] = 0.73;
    path_delay[0][3] = 0.73;
    path_delay[0][4] = 0.77;
    path_delay[0][5] = 1.07;
    path_delay[0][6] = 1.07;
    path_delay[0][7] = 1.07;
    path_delay[0][8] = 1.06;
    path_delay[0][9] = 1.35;
    path_delay[10][12] = 1.05;
    path_delay[10][10] = 0.37;
    path_delay[10][11] = 1.0;
    path_delay[11][12] = 1.11;
    path_delay[11][11] = 0.36;
    path_delay[1][12] = 1.3;
    path_delay[1][10] = 1.56;
    path_delay[1][11] = 1.56;
    path_delay[1][1] = 0.39;
    path_delay[1][2] = 0.84;
    path_delay[1][3] = 0.94;
    path_delay[1][4] = 0.98;
    path_delay[1][5] = 1.27;
    path_delay[1][6] = 1.27;
    path_delay[1][7] = 1.27;
    path_delay[1][8] = 1.27;
    path_delay[1][9] = 1.56;
    path_delay[2][12] = 1.46;
    path_delay[2][10] = 1.72;
    path_delay[2][11] = 1.72;
    path_delay[2][2] = 0.37;
    path_delay[2][3] = 1.0;
    path_delay[2][4] = 1.14;
    path_delay[2][5] = 1.43;
    path_delay[2][6] = 1.43;
    path_delay[2][7] = 1.43;
    path_delay[2][8] = 1.42;
    path_delay[2][9] = 1.72;
    path_delay[3][12] = 1.52;
    path_delay[3][10] = 1.77;
    path_delay[3][11] = 1.77;
    path_delay[3][3] = 0.36;
    path_delay[3][4] = 1.19;
    path_delay[3][5] = 1.48;
    path_delay[3][6] = 1.48;
    path_delay[3][7] = 1.48;
    path_delay[3][8] = 1.48;
    path_delay[3][9] = 1.77;
    path_delay[4][12] = 1.02;
    path_delay[4][10] = 1.27;
    path_delay[4][11] = 1.27;
    path_delay[4][4] = 0.39;
    path_delay[4][5] = 0.68;
    path_delay[4][6] = 0.77;
    path_delay[4][7] = 0.77;
    path_delay[4][8] = 0.98;
    path_delay[4][9] = 1.27;
    path_delay[5][12] = 1.18;
    path_delay[5][10] = 1.44;
    path_delay[5][11] = 1.44;
    path_delay[5][5] = 0.39;
    path_delay[5][6] = 0.84;
    path_delay[5][7] = 0.94;
    path_delay[5][8] = 1.14;
    path_delay[5][9] = 1.44;
    path_delay[6][12] = 1.34;
    path_delay[6][10] = 1.59;
    path_delay[6][11] = 1.59;
    path_delay[6][6] = 0.37;
    path_delay[6][7] = 1.0;
    path_delay[6][8] = 1.3;
    path_delay[6][9] = 1.59;
    path_delay[7][12] = 1.39;
    path_delay[7][10] = 1.65;
    path_delay[7][11] = 1.65;
    path_delay[7][7] = 0.36;
    path_delay[7][8] = 1.36;
    path_delay[7][9] = 1.65;
    path_delay[8][12] = 0.73;
    path_delay[8][10] = 0.77;
    path_delay[8][11] = 0.77;
    path_delay[8][8] = 0.39;
    path_delay[8][9] = 0.68;
    path_delay[9][12] = 0.9;
    path_delay[9][10] = 0.84;
    path_delay[9][11] = 0.94;
    path_delay[9][9] = 0.39;
    path_delay[12][12] = 1.11;
    path_delay[12][0] = 0.38;
    path_delay[12][10] = 1.36;
    path_delay[12][11] = 1.36;
    path_delay[12][1] = 0.47;
    path_delay[12][2] = 0.74;
    path_delay[12][3] = 0.74;
    path_delay[12][4] = 0.78;
    path_delay[12][5] = 1.08;
    path_delay[12][6] = 1.08;
    path_delay[12][7] = 1.08;
    path_delay[12][8] = 1.07;
    path_delay[12][9] = 1.36;
    path_delay[22][12] = 1.07;
    path_delay[22][10] = 0.38;
    path_delay[22][11] = 1.01;
    path_delay[23][12] = 1.12;
    path_delay[23][11] = 0.38;
    path_delay[13][12] = 1.32;
    path_delay[13][10] = 1.57;
    path_delay[13][11] = 1.57;
    path_delay[13][1] = 0.38;
    path_delay[13][2] = 0.85;
    path_delay[13][3] = 0.95;
    path_delay[13][4] = 0.99;
    path_delay[13][5] = 1.29;
    path_delay[13][6] = 1.29;
    path_delay[13][7] = 1.29;
    path_delay[13][8] = 1.28;
    path_delay[13][9] = 1.57;
    path_delay[14][12] = 1.47;
    path_delay[14][10] = 1.73;
    path_delay[14][11] = 1.73;
    path_delay[14][2] = 0.38;
    path_delay[14][3] = 1.01;
    path_delay[14][4] = 1.15;
    path_delay[14][5] = 1.44;
    path_delay[14][6] = 1.44;
    path_delay[14][7] = 1.44;
    path_delay[14][8] = 1.44;
    path_delay[14][9] = 1.73;
    path_delay[15][12] = 1.53;
    path_delay[15][10] = 1.78;
    path_delay[15][11] = 1.78;
    path_delay[15][3] = 0.38;
    path_delay[15][4] = 1.21;
    path_delay[15][5] = 1.5;
    path_delay[15][6] = 1.5;
    path_delay[15][7] = 1.5;
    path_delay[15][8] = 1.49;
    path_delay[15][9] = 1.78;
    path_delay[16][12] = 1.03;
    path_delay[16][10] = 1.29;
    path_delay[16][11] = 1.29;
    path_delay[16][4] = 0.38;
    path_delay[16][5] = 0.69;
    path_delay[16][6] = 0.79;
    path_delay[16][7] = 0.79;
    path_delay[16][8] = 0.99;
    path_delay[16][9] = 1.29;
    path_delay[17][12] = 1.19;
    path_delay[17][10] = 1.45;
    path_delay[17][11] = 1.45;
    path_delay[17][5] = 0.38;
    path_delay[17][6] = 0.85;
    path_delay[17][7] = 0.95;
    path_delay[17][8] = 1.16;
    path_delay[17][9] = 1.45;
    path_delay[18][12] = 1.35;
    path_delay[18][10] = 1.61;
    path_delay[18][11] = 1.61;
    path_delay[18][6] = 0.38;
    path_delay[18][7] = 1.01;
    path_delay[18][8] = 1.31;
    path_delay[18][9] = 1.61;
    path_delay[19][12] = 1.41;
    path_delay[19][10] = 1.66;
    path_delay[19][11] = 1.66;
    path_delay[19][7] = 0.38;
    path_delay[19][8] = 1.37;
    path_delay[19][9] = 1.66;
    path_delay[20][12] = 0.74;
    path_delay[20][10] = 0.79;
    path_delay[20][11] = 0.79;
    path_delay[20][8] = 0.38;
    path_delay[20][9] = 0.69;
    path_delay[21][12] = 0.91;
    path_delay[21][10] = 0.85;
    path_delay[21][11] = 0.95;
    path_delay[21][9] = 0.38;
}

// ============================================================================
// TESTBENCH -- Monte Carlo, per-sample EDP. Gate switches are reset via
// reset_all_gate_switches() (global registry, width/structure-agnostic --
// works the same whether the design is a standalone block, one ClaBlock4,
// or multiple blocks + a ClaTree).
// ============================================================================
SC_MODULE(Testbench) {
    sc_out<sc_lv<12>> A, B;
    sc_in<sc_lv<12>>  Sum;
    sc_in<bool>      Cout;

    CarryLookaheadAdder12* cla_ptr;

    unsigned int number_of_errors;

    unsigned long long num_samples;
    unsigned long long rng_seed;

    double sum_switches, sum_delay, sum_edp;

    unsigned int prev_a, prev_b, prev_sum;
    bool prev_cout;

    SC_CTOR(Testbench)
        : cla_ptr(nullptr), number_of_errors(0), num_samples(1000000ULL), rng_seed(42ULL),
          sum_switches(0.0), sum_delay(0.0), sum_edp(0.0),
          prev_a(0), prev_b(0), prev_sum(0), prev_cout(false)
    { SC_THREAD(stimulus); }

    void stimulus() {
        std::mt19937_64 rng(rng_seed);
        std::uniform_int_distribution<unsigned long long> dist(0ULL, 4095ULL);

        A.write(make_12bit_vector(0));
        B.write(make_12bit_vector(0));
        wait(50, SC_NS);
        prev_a = 0; prev_b = 0;
        prev_sum = to_unsigned_12bit(Sum.read());
        prev_cout = Cout.read();

        for (unsigned long long i = 0; i < num_samples; i++) {
            unsigned int a_value = static_cast<unsigned int>(dist(rng));
            unsigned int b_value = static_cast<unsigned int>(dist(rng));
            apply_test(a_value, b_value);
        }

        assert(number_of_errors == 0);
        sc_stop();
    }

    void apply_test(unsigned int a_value, unsigned int b_value) {
        reset_all_gate_switches();

        A.write(make_12bit_vector(a_value));
        B.write(make_12bit_vector(b_value));
        wait(50, SC_NS);

        unsigned int expected_result = a_value + b_value;
        unsigned int actual_sum      = to_unsigned_12bit(Sum.read());
        bool actual_cout             = Cout.read();
        unsigned int actual_result   = actual_sum + (actual_cout ? 4096U : 0U);
        if (expected_result != actual_result) number_of_errors++;

        unsigned int switches_i = cla_ptr->total_switches();

        bool in_changed[NUM_IN];
        for (int k = 0; k < 12; k++) in_changed[k] = (((a_value >> k) & 1U) != ((prev_a >> k) & 1U));
        for (int k = 0; k < 12; k++) in_changed[12 + k] = (((b_value >> k) & 1U) != ((prev_b >> k) & 1U));

        bool out_changed[NUM_OUT];
        for (int k = 0; k < 12; k++) out_changed[k] = (((actual_sum >> k) & 1U) != ((prev_sum >> k) & 1U));
        out_changed[12] = (actual_cout != prev_cout);

        double delay_i = 0.0;
        for (int in = 0; in < NUM_IN; in++) {
            if (!in_changed[in]) continue;
            for (int out = 0; out < NUM_OUT; out++) {
                if (!out_changed[out]) continue;
                double d = path_delay[in][out];
                if (d > delay_i) delay_i = d;
            }
        }

        double edp_i = static_cast<double>(switches_i) * delay_i;
        sum_switches += switches_i;
        sum_delay    += delay_i;
        sum_edp      += edp_i;

        prev_a = a_value;
        prev_b = b_value;
        prev_sum = actual_sum;
        prev_cout = actual_cout;
    }
};

// ==========================================
// MAIN -- Monte Carlo, auto-parallel across all CPU cores
// ==========================================
struct WorkerResult {
    double sum_switches;
    double sum_delay;
    double sum_edp;
    unsigned long long errors;
};

static void run_slice(unsigned long long samples_for_this_worker, unsigned long long seed_for_this_worker,
                       double& out_sum_switches, double& out_sum_delay, double& out_sum_edp,
                       unsigned int& out_errors) {
    clear_gate_registry();

    sc_signal<sc_lv<12>> A, B, Sum;
    sc_signal<bool>     Cout;

    CarryLookaheadAdder12 cla("CLA12");
    Testbench tb("TB");
    tb.cla_ptr = &cla;

    cla.A(A); cla.B(B); cla.Sum(Sum); cla.Cout(Cout);
    tb.A(A);  tb.B(B);  tb.Sum(Sum);  tb.Cout(Cout);

    tb.num_samples = samples_for_this_worker;
    tb.rng_seed = seed_for_this_worker;

    sc_start();

    out_sum_switches = tb.sum_switches;
    out_sum_delay = tb.sum_delay;
    out_sum_edp = tb.sum_edp;
    out_errors = tb.number_of_errors;
}

int sc_main(int argc, char* argv[]) {
    init_path_delay_table();

    unsigned long long total_samples = (argc >= 2) ? std::stoull(argv[1]) : 1000000ULL;
    unsigned long long base_seed     = (argc >= 3) ? std::stoull(argv[2]) : 42ULL;

    unsigned int num_workers = std::thread::hardware_concurrency();
    if (num_workers == 0) num_workers = 1;
    num_workers = static_cast<unsigned int>(std::min<unsigned long long>(num_workers, std::max<unsigned long long>(total_samples, 1ULL)));

    std::vector<int> read_fd(num_workers);
    std::vector<pid_t> worker_pid(num_workers);

    unsigned long long base_slice = total_samples / num_workers;
    unsigned long long remainder  = total_samples % num_workers;

    for (unsigned int i = 0; i < num_workers; i++) {
        unsigned long long samples_for_worker = base_slice + (i < remainder ? 1ULL : 0ULL);

        int fds[2];
        if (pipe(fds) != 0) { perror("pipe"); return 1; }

        pid_t child = fork();
        if (child < 0) { perror("fork"); return 1; }

        if (child == 0) {
            close(fds[0]);
            WorkerResult result{};
            unsigned int errors = 0;
            run_slice(samples_for_worker, base_seed + i, result.sum_switches, result.sum_delay, result.sum_edp, errors);
            result.errors = errors;
            ssize_t written = write(fds[1], &result, sizeof(result));
            (void)written;
            close(fds[1]);
            _exit(0);
        }

        close(fds[1]);
        read_fd[i] = fds[0];
        worker_pid[i] = child;
    }

    double total_sum_switches = 0.0, total_sum_delay = 0.0, total_sum_edp = 0.0;
    unsigned long long total_errors = 0;
    for (unsigned int i = 0; i < num_workers; i++) {
        WorkerResult result{};
        ssize_t got = read(read_fd[i], &result, sizeof(result));
        if (got == static_cast<ssize_t>(sizeof(result))) {
            total_sum_switches += result.sum_switches;
            total_sum_delay    += result.sum_delay;
            total_sum_edp      += result.sum_edp;
            total_errors       += result.errors;
        }
        close(read_fd[i]);
        int status = 0;
        waitpid(worker_pid[i], &status, 0);
    }

    assert(total_errors == 0);

    double avg_switches = total_sum_switches / static_cast<double>(total_samples);
    double avg_delay    = total_sum_delay    / static_cast<double>(total_samples);
    double avg_edp_correct = total_sum_edp   / static_cast<double>(total_samples);
    double avg_edp_naive   = avg_switches * avg_delay;

    std::cout << "CLA-12-MC-pathEDP: SAMPLES=" << total_samples << " ERRORS=" << total_errors
              << " AVG_SWITCHES=" << avg_switches
              << " AVG_DELAY_ns=" << avg_delay
              << " AVG_EDP_correct=" << avg_edp_correct
              << " AVG_EDP_naive=" << avg_edp_naive
              << " pct_diff=" << ((avg_edp_naive / avg_edp_correct - 1.0) * 100.0) << "%\n";

    return 0;
}
