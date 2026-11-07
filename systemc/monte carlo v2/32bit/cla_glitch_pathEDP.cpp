// cla_glitch.cpp – 32-bit Carry Lookahead Adder (2-level CLA tree), gate-level switching activity
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
static sc_lv<32> make_32bit_vector(unsigned int value) {
    sc_lv<32> bits;
    for (int i = 0; i < 32; i++) bits[i] = ((value >> i) & 1U) != 0U;
    return bits;
}

static unsigned int to_unsigned_32bit(const sc_lv<32>& bits) {
    unsigned int value = 0;
    for (int i = 0; i < 32; i++)
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
//    internally) so the second-level ClaUnit8 can drive all inter-block
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
        // Expose the group-level signals to the second-level ClaUnit8.
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
//    - For N=8 (a power of two) the split is perfectly even at every
//      level, giving every block's Cin after exactly 2*log2(8) = 6 gate
//      delays, instead of the old flat ClaUnit8's much deeper per-level
//      AND chains (level k=8 alone needed a 7-AND-deep chain before its
//      final OR).
//
//    Gate count for N leaves: 5*(N-1) + 2. For N=8 (this file) that is
//    37 gates (down from 100 in the old flat single-level ClaUnit8).
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
// 3. 32-BIT CARRY LOOKAHEAD ADDER (8 x 4-bit blocks + 1 ClaTree)
//    All eight 4-bit blocks work in parallel; the ClaTree computes the
//    carries into blocks 1..7 plus the overall carry out via a recursive
//    binary lookahead tree from group-P/G signals (which depend only on
//    A and B, not on Cin), so no carry ripples between blocks and the
//    Cin-dependent depth is O(log2 N) rather than O(N).
//    Total gates: 8 × 38 (blocks) + 37 (ClaTree, N=8) = 341
// ==========================================
SC_MODULE(CarryLookaheadAdder32) {
    static const int NBLK = 8;

    sc_in<sc_lv<32>>  A, B;
    sc_out<sc_lv<32>> Sum;
    sc_out<bool>      Cout;

    sc_signal<sc_lv<4>> a_slice[NBLK], b_slice[NBLK], s_slice[NBLK];
    sc_signal<bool>     blk_cout[NBLK];
    sc_signal<bool>     blk_pg[NBLK], blk_gg[NBLK];
    sc_signal<bool>     c_mid[NBLK - 1];       // carries into blocks 1..7 (from ClaTree)
    sc_signal<bool>     const_zero;            // tied low for block 0's Cin

    ClaBlock4* blk[NBLK];
    ClaTree*   clu;

    SC_CTOR(CarryLookaheadAdder32) {
        for (int blki = 0; blki < NBLK; blki++) {
            std::string name = "BLK" + std::to_string(blki);
            blk[blki] = new ClaBlock4(name.c_str());
            blk[blki]->A(a_slice[blki]);
            blk[blki]->B(b_slice[blki]);

            if (blki == 0) blk[blki]->Cin(const_zero);
            else            blk[blki]->Cin(c_mid[blki - 1]);

            blk[blki]->Sum(s_slice[blki]);
            blk[blki]->Cout(blk_cout[blki]);
            blk[blki]->PG(blk_pg[blki]);
            blk[blki]->GG(blk_gg[blki]);
        }

        // ---- Second-level ClaTree: computes c_mid[0..6], Cout from group P/G ----
        clu = new ClaTree("CLU", NBLK);
        for (int i = 0; i < NBLK; i++) {
            clu->PG[i](blk_pg[i]);
            clu->GG[i](blk_gg[i]);
        }
        clu->Cin(const_zero);
        for (int i = 0; i < NBLK - 1; i++) clu->C[i](c_mid[i]);
        clu->Cout(Cout);

        SC_METHOD(split_inputs);    dont_initialize(); sensitive << A << B;
        SC_METHOD(combine_outputs); dont_initialize();
        for (int blki = 0; blki < NBLK; blki++) sensitive << s_slice[blki];
    }

    void split_inputs() {
        sc_lv<32> va=A.read(), vb=B.read();
        for (int blki=0; blki<8; blki++) {
            sc_lv<4> a4, b4;
            for (int i=0; i<4; i++) { a4[i]=va[blki*4+i]; b4[i]=vb[blki*4+i]; }
            a_slice[blki].write(a4); b_slice[blki].write(b4);
        }
    }

    void combine_outputs() {
        sc_lv<32> s;
        for (int blki=0; blki<8; blki++) {
            sc_lv<4> s4=s_slice[blki].read();
            for (int i=0; i<4; i++) s[blki*4+i]=s4[i];
        }
        Sum.write(s);
    }

    unsigned int total_switches() const {
        unsigned int total = 0;
        for (int i = 0; i < 8; i++) total += blk[i]->total_block_switches();
        total += clu->total_unit_switches();
        return total;
    }

    ~CarryLookaheadAdder32() {
        for (int i = 0; i < 8; i++) delete blk[i];
        delete clu;
    }
};

// ==========================================
// 4. TESTBENCH — Monte Carlo random sampling
//    Draws num_samples uniformly random (a, b) pairs instead of
//    exhaustively covering the whole input space: at 32 bits exhaustive
//    coverage (2^32 x 2^32 cases) is computationally infeasible, so
//    Monte Carlo sampling is the only tractable option.
//
//    Note: a 32-bit adder's result needs 33 bits (32-bit sum + carry),
//    so the correctness check below widens to unsigned long long before
//    adding — doing this arithmetic in plain 32-bit unsigned int would
//    silently overflow/wrap and produce false error reports.
// ==========================================

// ============================================================================
// Real OpenSTA (Nangate45) max delay per (startpoint pin, endpoint pin),
// extracted from all_critical_paths.csv for CLA-32 (1120 pairs).
// Input pin index: 0..31=A[i], 32..63=B[i]. Output pin index: 0..31=Sum[i], 32=Cout.
// ============================================================================
static const int NUM_IN = 64;
static const int NUM_OUT = 33;
static double path_delay[NUM_IN][NUM_OUT];

static void init_path_delay_table() {
    for (int i = 0; i < NUM_IN; i++)
        for (int o = 0; o < NUM_OUT; o++)
            path_delay[i][o] = 0.0;

    path_delay[0][32] = 1.38;
    path_delay[0][0] = 0.39;
    path_delay[0][10] = 1.36;
    path_delay[0][11] = 1.36;
    path_delay[0][12] = 1.38;
    path_delay[0][13] = 1.68;
    path_delay[0][14] = 1.68;
    path_delay[0][15] = 1.68;
    path_delay[0][16] = 1.36;
    path_delay[0][17] = 1.65;
    path_delay[0][18] = 1.65;
    path_delay[0][19] = 1.65;
    path_delay[0][1] = 0.46;
    path_delay[0][20] = 1.67;
    path_delay[0][21] = 1.97;
    path_delay[0][22] = 1.97;
    path_delay[0][23] = 1.97;
    path_delay[0][24] = 1.68;
    path_delay[0][25] = 1.97;
    path_delay[0][26] = 1.97;
    path_delay[0][27] = 1.97;
    path_delay[0][28] = 2.0;
    path_delay[0][29] = 2.29;
    path_delay[0][2] = 0.73;
    path_delay[0][30] = 2.29;
    path_delay[0][31] = 2.29;
    path_delay[0][3] = 0.73;
    path_delay[0][4] = 0.77;
    path_delay[0][5] = 1.07;
    path_delay[0][6] = 1.07;
    path_delay[0][7] = 1.07;
    path_delay[0][8] = 1.07;
    path_delay[0][9] = 1.36;
    path_delay[10][32] = 1.62;
    path_delay[10][10] = 0.37;
    path_delay[10][11] = 1.0;
    path_delay[10][12] = 1.14;
    path_delay[10][13] = 1.43;
    path_delay[10][14] = 1.43;
    path_delay[10][15] = 1.43;
    path_delay[10][16] = 1.6;
    path_delay[10][17] = 1.89;
    path_delay[10][18] = 1.89;
    path_delay[10][19] = 1.89;
    path_delay[10][20] = 1.91;
    path_delay[10][21] = 2.21;
    path_delay[10][22] = 2.21;
    path_delay[10][23] = 2.21;
    path_delay[10][24] = 1.92;
    path_delay[10][25] = 2.21;
    path_delay[10][26] = 2.21;
    path_delay[10][27] = 2.21;
    path_delay[10][28] = 2.24;
    path_delay[10][29] = 2.53;
    path_delay[10][30] = 2.53;
    path_delay[10][31] = 2.53;
    path_delay[11][32] = 1.68;
    path_delay[11][11] = 0.36;
    path_delay[11][12] = 1.19;
    path_delay[11][13] = 1.48;
    path_delay[11][14] = 1.48;
    path_delay[11][15] = 1.48;
    path_delay[11][16] = 1.65;
    path_delay[11][17] = 1.94;
    path_delay[11][18] = 1.94;
    path_delay[11][19] = 1.94;
    path_delay[11][20] = 1.97;
    path_delay[11][21] = 2.26;
    path_delay[11][22] = 2.26;
    path_delay[11][23] = 2.26;
    path_delay[11][24] = 1.97;
    path_delay[11][25] = 2.27;
    path_delay[11][26] = 2.27;
    path_delay[11][27] = 2.27;
    path_delay[11][28] = 2.29;
    path_delay[11][29] = 2.58;
    path_delay[11][30] = 2.58;
    path_delay[11][31] = 2.58;
    path_delay[12][32] = 1.18;
    path_delay[12][12] = 0.39;
    path_delay[12][13] = 0.68;
    path_delay[12][14] = 0.77;
    path_delay[12][15] = 0.77;
    path_delay[12][16] = 1.15;
    path_delay[12][17] = 1.44;
    path_delay[12][18] = 1.44;
    path_delay[12][19] = 1.44;
    path_delay[12][20] = 1.47;
    path_delay[12][21] = 1.76;
    path_delay[12][22] = 1.76;
    path_delay[12][23] = 1.76;
    path_delay[12][24] = 1.47;
    path_delay[12][25] = 1.77;
    path_delay[12][26] = 1.77;
    path_delay[12][27] = 1.77;
    path_delay[12][28] = 1.79;
    path_delay[12][29] = 2.08;
    path_delay[12][30] = 2.08;
    path_delay[12][31] = 2.08;
    path_delay[13][32] = 1.34;
    path_delay[13][13] = 0.39;
    path_delay[13][14] = 0.84;
    path_delay[13][15] = 0.94;
    path_delay[13][16] = 1.32;
    path_delay[13][17] = 1.61;
    path_delay[13][18] = 1.61;
    path_delay[13][19] = 1.61;
    path_delay[13][20] = 1.63;
    path_delay[13][21] = 1.93;
    path_delay[13][22] = 1.93;
    path_delay[13][23] = 1.93;
    path_delay[13][24] = 1.64;
    path_delay[13][25] = 1.93;
    path_delay[13][26] = 1.93;
    path_delay[13][27] = 1.93;
    path_delay[13][28] = 1.96;
    path_delay[13][29] = 2.25;
    path_delay[13][30] = 2.25;
    path_delay[13][31] = 2.25;
    path_delay[14][32] = 1.5;
    path_delay[14][14] = 0.37;
    path_delay[14][15] = 1.0;
    path_delay[14][16] = 1.47;
    path_delay[14][17] = 1.77;
    path_delay[14][18] = 1.77;
    path_delay[14][19] = 1.77;
    path_delay[14][20] = 1.79;
    path_delay[14][21] = 2.08;
    path_delay[14][22] = 2.08;
    path_delay[14][23] = 2.08;
    path_delay[14][24] = 1.8;
    path_delay[14][25] = 2.09;
    path_delay[14][26] = 2.09;
    path_delay[14][27] = 2.09;
    path_delay[14][28] = 2.11;
    path_delay[14][29] = 2.41;
    path_delay[14][30] = 2.41;
    path_delay[14][31] = 2.41;
    path_delay[15][32] = 1.55;
    path_delay[15][15] = 0.36;
    path_delay[15][16] = 1.53;
    path_delay[15][17] = 1.82;
    path_delay[15][18] = 1.82;
    path_delay[15][19] = 1.82;
    path_delay[15][20] = 1.85;
    path_delay[15][21] = 2.14;
    path_delay[15][22] = 2.14;
    path_delay[15][23] = 2.14;
    path_delay[15][24] = 1.85;
    path_delay[15][25] = 2.14;
    path_delay[15][26] = 2.14;
    path_delay[15][27] = 2.14;
    path_delay[15][28] = 2.17;
    path_delay[15][29] = 2.46;
    path_delay[15][30] = 2.46;
    path_delay[15][31] = 2.46;
    path_delay[16][32] = 1.26;
    path_delay[16][16] = 0.39;
    path_delay[16][17] = 0.68;
    path_delay[16][18] = 0.77;
    path_delay[16][19] = 0.77;
    path_delay[16][20] = 0.81;
    path_delay[16][21] = 1.1;
    path_delay[16][22] = 1.1;
    path_delay[16][23] = 1.1;
    path_delay[16][24] = 1.07;
    path_delay[16][25] = 1.36;
    path_delay[16][26] = 1.36;
    path_delay[16][27] = 1.36;
    path_delay[16][28] = 1.38;
    path_delay[16][29] = 1.68;
    path_delay[16][30] = 1.68;
    path_delay[16][31] = 1.68;
    path_delay[17][32] = 1.47;
    path_delay[17][17] = 0.39;
    path_delay[17][18] = 0.84;
    path_delay[17][19] = 0.94;
    path_delay[17][20] = 0.98;
    path_delay[17][21] = 1.27;
    path_delay[17][22] = 1.27;
    path_delay[17][23] = 1.27;
    path_delay[17][24] = 1.27;
    path_delay[17][25] = 1.56;
    path_delay[17][26] = 1.56;
    path_delay[17][27] = 1.56;
    path_delay[17][28] = 1.59;
    path_delay[17][29] = 1.88;
    path_delay[17][30] = 1.88;
    path_delay[17][31] = 1.88;
    path_delay[18][32] = 1.62;
    path_delay[18][18] = 0.37;
    path_delay[18][19] = 1.0;
    path_delay[18][20] = 1.14;
    path_delay[18][21] = 1.43;
    path_delay[18][22] = 1.43;
    path_delay[18][23] = 1.43;
    path_delay[18][24] = 1.43;
    path_delay[18][25] = 1.72;
    path_delay[18][26] = 1.72;
    path_delay[18][27] = 1.72;
    path_delay[18][28] = 1.75;
    path_delay[18][29] = 2.04;
    path_delay[18][30] = 2.04;
    path_delay[18][31] = 2.04;
    path_delay[19][32] = 1.68;
    path_delay[19][19] = 0.36;
    path_delay[19][20] = 1.19;
    path_delay[19][21] = 1.48;
    path_delay[19][22] = 1.48;
    path_delay[19][23] = 1.48;
    path_delay[19][24] = 1.48;
    path_delay[19][25] = 1.78;
    path_delay[19][26] = 1.78;
    path_delay[19][27] = 1.78;
    path_delay[19][28] = 1.8;
    path_delay[19][29] = 2.09;
    path_delay[19][30] = 2.09;
    path_delay[19][31] = 2.09;
    path_delay[1][32] = 1.59;
    path_delay[1][10] = 1.56;
    path_delay[1][11] = 1.56;
    path_delay[1][12] = 1.59;
    path_delay[1][13] = 1.88;
    path_delay[1][14] = 1.88;
    path_delay[1][15] = 1.88;
    path_delay[1][16] = 1.56;
    path_delay[1][17] = 1.86;
    path_delay[1][18] = 1.86;
    path_delay[1][19] = 1.86;
    path_delay[1][1] = 0.39;
    path_delay[1][20] = 1.88;
    path_delay[1][21] = 2.17;
    path_delay[1][22] = 2.17;
    path_delay[1][23] = 2.17;
    path_delay[1][24] = 1.89;
    path_delay[1][25] = 2.18;
    path_delay[1][26] = 2.18;
    path_delay[1][27] = 2.18;
    path_delay[1][28] = 2.2;
    path_delay[1][29] = 2.5;
    path_delay[1][2] = 0.84;
    path_delay[1][30] = 2.5;
    path_delay[1][31] = 2.5;
    path_delay[1][3] = 0.94;
    path_delay[1][4] = 0.98;
    path_delay[1][5] = 1.27;
    path_delay[1][6] = 1.27;
    path_delay[1][7] = 1.27;
    path_delay[1][8] = 1.27;
    path_delay[1][9] = 1.56;
    path_delay[20][32] = 1.18;
    path_delay[20][20] = 0.39;
    path_delay[20][21] = 0.68;
    path_delay[20][22] = 0.77;
    path_delay[20][23] = 0.77;
    path_delay[20][24] = 0.98;
    path_delay[20][25] = 1.28;
    path_delay[20][26] = 1.28;
    path_delay[20][27] = 1.28;
    path_delay[20][28] = 1.3;
    path_delay[20][29] = 1.59;
    path_delay[20][30] = 1.59;
    path_delay[20][31] = 1.59;
    path_delay[21][32] = 1.34;
    path_delay[21][21] = 0.39;
    path_delay[21][22] = 0.84;
    path_delay[21][23] = 0.94;
    path_delay[21][24] = 1.15;
    path_delay[21][25] = 1.44;
    path_delay[21][26] = 1.44;
    path_delay[21][27] = 1.44;
    path_delay[21][28] = 1.47;
    path_delay[21][29] = 1.76;
    path_delay[21][30] = 1.76;
    path_delay[21][31] = 1.76;
    path_delay[22][32] = 1.5;
    path_delay[22][22] = 0.37;
    path_delay[22][23] = 1.0;
    path_delay[22][24] = 1.31;
    path_delay[22][25] = 1.6;
    path_delay[22][26] = 1.6;
    path_delay[22][27] = 1.6;
    path_delay[22][28] = 1.62;
    path_delay[22][29] = 1.92;
    path_delay[22][30] = 1.92;
    path_delay[22][31] = 1.92;
    path_delay[23][32] = 1.55;
    path_delay[23][23] = 0.36;
    path_delay[23][24] = 1.36;
    path_delay[23][25] = 1.65;
    path_delay[23][26] = 1.65;
    path_delay[23][27] = 1.65;
    path_delay[23][28] = 1.68;
    path_delay[23][29] = 1.97;
    path_delay[23][30] = 1.97;
    path_delay[23][31] = 1.97;
    path_delay[24][32] = 1.14;
    path_delay[24][24] = 0.39;
    path_delay[24][25] = 0.68;
    path_delay[24][26] = 0.77;
    path_delay[24][27] = 0.77;
    path_delay[24][28] = 0.81;
    path_delay[24][29] = 1.1;
    path_delay[24][30] = 1.1;
    path_delay[24][31] = 1.1;
    path_delay[25][32] = 1.34;
    path_delay[25][25] = 0.39;
    path_delay[25][26] = 0.84;
    path_delay[25][27] = 0.94;
    path_delay[25][28] = 0.98;
    path_delay[25][29] = 1.27;
    path_delay[25][30] = 1.27;
    path_delay[25][31] = 1.27;
    path_delay[26][32] = 1.5;
    path_delay[26][26] = 0.37;
    path_delay[26][27] = 1.0;
    path_delay[26][28] = 1.14;
    path_delay[26][29] = 1.43;
    path_delay[26][30] = 1.43;
    path_delay[26][31] = 1.43;
    path_delay[27][32] = 1.55;
    path_delay[27][27] = 0.36;
    path_delay[27][28] = 1.19;
    path_delay[27][29] = 1.48;
    path_delay[27][30] = 1.48;
    path_delay[27][31] = 1.48;
    path_delay[28][32] = 1.05;
    path_delay[28][28] = 0.39;
    path_delay[28][29] = 0.68;
    path_delay[28][30] = 0.77;
    path_delay[28][31] = 0.77;
    path_delay[29][32] = 1.22;
    path_delay[29][29] = 0.39;
    path_delay[29][30] = 0.84;
    path_delay[29][31] = 0.94;
    path_delay[2][32] = 1.75;
    path_delay[2][10] = 1.72;
    path_delay[2][11] = 1.72;
    path_delay[2][12] = 1.75;
    path_delay[2][13] = 2.04;
    path_delay[2][14] = 2.04;
    path_delay[2][15] = 2.04;
    path_delay[2][16] = 1.72;
    path_delay[2][17] = 2.01;
    path_delay[2][18] = 2.01;
    path_delay[2][19] = 2.01;
    path_delay[2][20] = 2.04;
    path_delay[2][21] = 2.33;
    path_delay[2][22] = 2.33;
    path_delay[2][23] = 2.33;
    path_delay[2][24] = 2.04;
    path_delay[2][25] = 2.34;
    path_delay[2][26] = 2.34;
    path_delay[2][27] = 2.34;
    path_delay[2][28] = 2.36;
    path_delay[2][29] = 2.65;
    path_delay[2][2] = 0.37;
    path_delay[2][30] = 2.65;
    path_delay[2][31] = 2.65;
    path_delay[2][3] = 1.0;
    path_delay[2][4] = 1.14;
    path_delay[2][5] = 1.43;
    path_delay[2][6] = 1.43;
    path_delay[2][7] = 1.43;
    path_delay[2][8] = 1.43;
    path_delay[2][9] = 1.72;
    path_delay[30][32] = 1.38;
    path_delay[30][30] = 0.37;
    path_delay[30][31] = 1.0;
    path_delay[31][32] = 1.43;
    path_delay[31][31] = 0.36;
    path_delay[3][32] = 1.8;
    path_delay[3][10] = 1.78;
    path_delay[3][11] = 1.78;
    path_delay[3][12] = 1.8;
    path_delay[3][13] = 2.09;
    path_delay[3][14] = 2.09;
    path_delay[3][15] = 2.09;
    path_delay[3][16] = 1.77;
    path_delay[3][17] = 2.07;
    path_delay[3][18] = 2.07;
    path_delay[3][19] = 2.07;
    path_delay[3][20] = 2.09;
    path_delay[3][21] = 2.38;
    path_delay[3][22] = 2.38;
    path_delay[3][23] = 2.38;
    path_delay[3][24] = 2.1;
    path_delay[3][25] = 2.39;
    path_delay[3][26] = 2.39;
    path_delay[3][27] = 2.39;
    path_delay[3][28] = 2.41;
    path_delay[3][29] = 2.71;
    path_delay[3][30] = 2.71;
    path_delay[3][31] = 2.71;
    path_delay[3][3] = 0.36;
    path_delay[3][4] = 1.19;
    path_delay[3][5] = 1.48;
    path_delay[3][6] = 1.48;
    path_delay[3][7] = 1.48;
    path_delay[3][8] = 1.48;
    path_delay[3][9] = 1.78;
    path_delay[4][32] = 1.3;
    path_delay[4][10] = 1.28;
    path_delay[4][11] = 1.28;
    path_delay[4][12] = 1.3;
    path_delay[4][13] = 1.59;
    path_delay[4][14] = 1.59;
    path_delay[4][15] = 1.59;
    path_delay[4][16] = 1.28;
    path_delay[4][17] = 1.57;
    path_delay[4][18] = 1.57;
    path_delay[4][19] = 1.57;
    path_delay[4][20] = 1.59;
    path_delay[4][21] = 1.89;
    path_delay[4][22] = 1.89;
    path_delay[4][23] = 1.89;
    path_delay[4][24] = 1.6;
    path_delay[4][25] = 1.89;
    path_delay[4][26] = 1.89;
    path_delay[4][27] = 1.89;
    path_delay[4][28] = 1.92;
    path_delay[4][29] = 2.21;
    path_delay[4][30] = 2.21;
    path_delay[4][31] = 2.21;
    path_delay[4][4] = 0.39;
    path_delay[4][5] = 0.68;
    path_delay[4][6] = 0.77;
    path_delay[4][7] = 0.77;
    path_delay[4][8] = 0.98;
    path_delay[4][9] = 1.28;
    path_delay[5][32] = 1.47;
    path_delay[5][10] = 1.44;
    path_delay[5][11] = 1.44;
    path_delay[5][12] = 1.47;
    path_delay[5][13] = 1.76;
    path_delay[5][14] = 1.76;
    path_delay[5][15] = 1.76;
    path_delay[5][16] = 1.44;
    path_delay[5][17] = 1.73;
    path_delay[5][18] = 1.73;
    path_delay[5][19] = 1.73;
    path_delay[5][20] = 1.76;
    path_delay[5][21] = 2.05;
    path_delay[5][22] = 2.05;
    path_delay[5][23] = 2.05;
    path_delay[5][24] = 1.76;
    path_delay[5][25] = 2.06;
    path_delay[5][26] = 2.06;
    path_delay[5][27] = 2.06;
    path_delay[5][28] = 2.08;
    path_delay[5][29] = 2.37;
    path_delay[5][30] = 2.37;
    path_delay[5][31] = 2.37;
    path_delay[5][5] = 0.39;
    path_delay[5][6] = 0.84;
    path_delay[5][7] = 0.94;
    path_delay[5][8] = 1.15;
    path_delay[5][9] = 1.44;
    path_delay[6][32] = 1.62;
    path_delay[6][10] = 1.6;
    path_delay[6][11] = 1.6;
    path_delay[6][12] = 1.62;
    path_delay[6][13] = 1.92;
    path_delay[6][14] = 1.92;
    path_delay[6][15] = 1.92;
    path_delay[6][16] = 1.6;
    path_delay[6][17] = 1.89;
    path_delay[6][18] = 1.89;
    path_delay[6][19] = 1.89;
    path_delay[6][20] = 1.91;
    path_delay[6][21] = 2.21;
    path_delay[6][22] = 2.21;
    path_delay[6][23] = 2.21;
    path_delay[6][24] = 1.92;
    path_delay[6][25] = 2.21;
    path_delay[6][26] = 2.21;
    path_delay[6][27] = 2.21;
    path_delay[6][28] = 2.24;
    path_delay[6][29] = 2.53;
    path_delay[6][30] = 2.53;
    path_delay[6][31] = 2.53;
    path_delay[6][6] = 0.37;
    path_delay[6][7] = 1.0;
    path_delay[6][8] = 1.31;
    path_delay[6][9] = 1.6;
    path_delay[7][32] = 1.68;
    path_delay[7][10] = 1.65;
    path_delay[7][11] = 1.65;
    path_delay[7][12] = 1.68;
    path_delay[7][13] = 1.97;
    path_delay[7][14] = 1.97;
    path_delay[7][15] = 1.97;
    path_delay[7][16] = 1.65;
    path_delay[7][17] = 1.94;
    path_delay[7][18] = 1.94;
    path_delay[7][19] = 1.94;
    path_delay[7][20] = 1.97;
    path_delay[7][21] = 2.26;
    path_delay[7][22] = 2.26;
    path_delay[7][23] = 2.26;
    path_delay[7][24] = 1.97;
    path_delay[7][25] = 2.27;
    path_delay[7][26] = 2.27;
    path_delay[7][27] = 2.27;
    path_delay[7][28] = 2.29;
    path_delay[7][29] = 2.58;
    path_delay[7][30] = 2.58;
    path_delay[7][31] = 2.58;
    path_delay[7][7] = 0.36;
    path_delay[7][8] = 1.36;
    path_delay[7][9] = 1.65;
    path_delay[8][32] = 1.26;
    path_delay[8][10] = 0.77;
    path_delay[8][11] = 0.77;
    path_delay[8][12] = 0.81;
    path_delay[8][13] = 1.1;
    path_delay[8][14] = 1.1;
    path_delay[8][15] = 1.1;
    path_delay[8][16] = 1.23;
    path_delay[8][17] = 1.53;
    path_delay[8][18] = 1.53;
    path_delay[8][19] = 1.53;
    path_delay[8][20] = 1.55;
    path_delay[8][21] = 1.84;
    path_delay[8][22] = 1.84;
    path_delay[8][23] = 1.84;
    path_delay[8][24] = 1.56;
    path_delay[8][25] = 1.85;
    path_delay[8][26] = 1.85;
    path_delay[8][27] = 1.85;
    path_delay[8][28] = 1.87;
    path_delay[8][29] = 2.17;
    path_delay[8][30] = 2.17;
    path_delay[8][31] = 2.17;
    path_delay[8][8] = 0.39;
    path_delay[8][9] = 0.68;
    path_delay[9][32] = 1.47;
    path_delay[9][10] = 0.84;
    path_delay[9][11] = 0.94;
    path_delay[9][12] = 0.98;
    path_delay[9][13] = 1.27;
    path_delay[9][14] = 1.27;
    path_delay[9][15] = 1.27;
    path_delay[9][16] = 1.44;
    path_delay[9][17] = 1.73;
    path_delay[9][18] = 1.73;
    path_delay[9][19] = 1.73;
    path_delay[9][20] = 1.76;
    path_delay[9][21] = 2.05;
    path_delay[9][22] = 2.05;
    path_delay[9][23] = 2.05;
    path_delay[9][24] = 1.76;
    path_delay[9][25] = 2.05;
    path_delay[9][26] = 2.05;
    path_delay[9][27] = 2.05;
    path_delay[9][28] = 2.08;
    path_delay[9][29] = 2.37;
    path_delay[9][30] = 2.37;
    path_delay[9][31] = 2.37;
    path_delay[9][9] = 0.39;
    path_delay[32][32] = 1.39;
    path_delay[32][0] = 0.38;
    path_delay[32][10] = 1.37;
    path_delay[32][11] = 1.37;
    path_delay[32][12] = 1.39;
    path_delay[32][13] = 1.68;
    path_delay[32][14] = 1.68;
    path_delay[32][15] = 1.68;
    path_delay[32][16] = 1.37;
    path_delay[32][17] = 1.66;
    path_delay[32][18] = 1.66;
    path_delay[32][19] = 1.66;
    path_delay[32][1] = 0.47;
    path_delay[32][20] = 1.68;
    path_delay[32][21] = 1.98;
    path_delay[32][22] = 1.98;
    path_delay[32][23] = 1.98;
    path_delay[32][24] = 1.69;
    path_delay[32][25] = 1.98;
    path_delay[32][26] = 1.98;
    path_delay[32][27] = 1.98;
    path_delay[32][28] = 2.0;
    path_delay[32][29] = 2.3;
    path_delay[32][2] = 0.74;
    path_delay[32][30] = 2.3;
    path_delay[32][31] = 2.3;
    path_delay[32][3] = 0.74;
    path_delay[32][4] = 0.78;
    path_delay[32][5] = 1.08;
    path_delay[32][6] = 1.08;
    path_delay[32][7] = 1.08;
    path_delay[32][8] = 1.07;
    path_delay[32][9] = 1.37;
    path_delay[42][32] = 1.64;
    path_delay[42][10] = 0.38;
    path_delay[42][11] = 1.01;
    path_delay[42][12] = 1.15;
    path_delay[42][13] = 1.44;
    path_delay[42][14] = 1.44;
    path_delay[42][15] = 1.44;
    path_delay[42][16] = 1.61;
    path_delay[42][17] = 1.9;
    path_delay[42][18] = 1.9;
    path_delay[42][19] = 1.9;
    path_delay[42][20] = 1.93;
    path_delay[42][21] = 2.22;
    path_delay[42][22] = 2.22;
    path_delay[42][23] = 2.22;
    path_delay[42][24] = 1.93;
    path_delay[42][25] = 2.22;
    path_delay[42][26] = 2.22;
    path_delay[42][27] = 2.22;
    path_delay[42][28] = 2.25;
    path_delay[42][29] = 2.54;
    path_delay[42][30] = 2.54;
    path_delay[42][31] = 2.54;
    path_delay[43][32] = 1.69;
    path_delay[43][11] = 0.38;
    path_delay[43][12] = 1.21;
    path_delay[43][13] = 1.5;
    path_delay[43][14] = 1.5;
    path_delay[43][15] = 1.5;
    path_delay[43][16] = 1.66;
    path_delay[43][17] = 1.96;
    path_delay[43][18] = 1.96;
    path_delay[43][19] = 1.96;
    path_delay[43][20] = 1.98;
    path_delay[43][21] = 2.27;
    path_delay[43][22] = 2.27;
    path_delay[43][23] = 2.27;
    path_delay[43][24] = 1.99;
    path_delay[43][25] = 2.28;
    path_delay[43][26] = 2.28;
    path_delay[43][27] = 2.28;
    path_delay[43][28] = 2.3;
    path_delay[43][29] = 2.6;
    path_delay[43][30] = 2.6;
    path_delay[43][31] = 2.6;
    path_delay[44][32] = 1.19;
    path_delay[44][12] = 0.38;
    path_delay[44][13] = 0.69;
    path_delay[44][14] = 0.79;
    path_delay[44][15] = 0.79;
    path_delay[44][16] = 1.17;
    path_delay[44][17] = 1.46;
    path_delay[44][18] = 1.46;
    path_delay[44][19] = 1.46;
    path_delay[44][20] = 1.48;
    path_delay[44][21] = 1.78;
    path_delay[44][22] = 1.78;
    path_delay[44][23] = 1.78;
    path_delay[44][24] = 1.49;
    path_delay[44][25] = 1.78;
    path_delay[44][26] = 1.78;
    path_delay[44][27] = 1.78;
    path_delay[44][28] = 1.8;
    path_delay[44][29] = 2.1;
    path_delay[44][30] = 2.1;
    path_delay[44][31] = 2.1;
    path_delay[45][32] = 1.36;
    path_delay[45][13] = 0.38;
    path_delay[45][14] = 0.85;
    path_delay[45][15] = 0.95;
    path_delay[45][16] = 1.33;
    path_delay[45][17] = 1.62;
    path_delay[45][18] = 1.62;
    path_delay[45][19] = 1.62;
    path_delay[45][20] = 1.65;
    path_delay[45][21] = 1.94;
    path_delay[45][22] = 1.94;
    path_delay[45][23] = 1.94;
    path_delay[45][24] = 1.65;
    path_delay[45][25] = 1.95;
    path_delay[45][26] = 1.95;
    path_delay[45][27] = 1.95;
    path_delay[45][28] = 1.97;
    path_delay[45][29] = 2.26;
    path_delay[45][30] = 2.26;
    path_delay[45][31] = 2.26;
    path_delay[46][32] = 1.51;
    path_delay[46][14] = 0.38;
    path_delay[46][15] = 1.01;
    path_delay[46][16] = 1.49;
    path_delay[46][17] = 1.78;
    path_delay[46][18] = 1.78;
    path_delay[46][19] = 1.78;
    path_delay[46][20] = 1.8;
    path_delay[46][21] = 2.1;
    path_delay[46][22] = 2.1;
    path_delay[46][23] = 2.1;
    path_delay[46][24] = 1.81;
    path_delay[46][25] = 2.1;
    path_delay[46][26] = 2.1;
    path_delay[46][27] = 2.1;
    path_delay[46][28] = 2.13;
    path_delay[46][29] = 2.42;
    path_delay[46][30] = 2.42;
    path_delay[46][31] = 2.42;
    path_delay[47][32] = 1.57;
    path_delay[47][15] = 0.38;
    path_delay[47][16] = 1.54;
    path_delay[47][17] = 1.83;
    path_delay[47][18] = 1.83;
    path_delay[47][19] = 1.83;
    path_delay[47][20] = 1.86;
    path_delay[47][21] = 2.15;
    path_delay[47][22] = 2.15;
    path_delay[47][23] = 2.15;
    path_delay[47][24] = 1.86;
    path_delay[47][25] = 2.16;
    path_delay[47][26] = 2.16;
    path_delay[47][27] = 2.16;
    path_delay[47][28] = 2.18;
    path_delay[47][29] = 2.47;
    path_delay[47][30] = 2.47;
    path_delay[47][31] = 2.47;
    path_delay[48][32] = 1.27;
    path_delay[48][16] = 0.38;
    path_delay[48][17] = 0.69;
    path_delay[48][18] = 0.79;
    path_delay[48][19] = 0.79;
    path_delay[48][20] = 0.82;
    path_delay[48][21] = 1.11;
    path_delay[48][22] = 1.11;
    path_delay[48][23] = 1.11;
    path_delay[48][24] = 1.07;
    path_delay[48][25] = 1.37;
    path_delay[48][26] = 1.37;
    path_delay[48][27] = 1.37;
    path_delay[48][28] = 1.39;
    path_delay[48][29] = 1.68;
    path_delay[48][30] = 1.68;
    path_delay[48][31] = 1.68;
    path_delay[49][32] = 1.48;
    path_delay[49][17] = 0.38;
    path_delay[49][18] = 0.85;
    path_delay[49][19] = 0.95;
    path_delay[49][20] = 0.99;
    path_delay[49][21] = 1.29;
    path_delay[49][22] = 1.29;
    path_delay[49][23] = 1.29;
    path_delay[49][24] = 1.29;
    path_delay[49][25] = 1.58;
    path_delay[49][26] = 1.58;
    path_delay[49][27] = 1.58;
    path_delay[49][28] = 1.6;
    path_delay[49][29] = 1.9;
    path_delay[49][30] = 1.9;
    path_delay[49][31] = 1.9;
    path_delay[50][32] = 1.64;
    path_delay[50][18] = 0.38;
    path_delay[50][19] = 1.01;
    path_delay[50][20] = 1.15;
    path_delay[50][21] = 1.44;
    path_delay[50][22] = 1.44;
    path_delay[50][23] = 1.44;
    path_delay[50][24] = 1.44;
    path_delay[50][25] = 1.74;
    path_delay[50][26] = 1.74;
    path_delay[50][27] = 1.74;
    path_delay[50][28] = 1.76;
    path_delay[50][29] = 2.05;
    path_delay[50][30] = 2.05;
    path_delay[50][31] = 2.05;
    path_delay[51][32] = 1.69;
    path_delay[51][19] = 0.38;
    path_delay[51][20] = 1.21;
    path_delay[51][21] = 1.5;
    path_delay[51][22] = 1.5;
    path_delay[51][23] = 1.5;
    path_delay[51][24] = 1.5;
    path_delay[51][25] = 1.79;
    path_delay[51][26] = 1.79;
    path_delay[51][27] = 1.79;
    path_delay[51][28] = 1.81;
    path_delay[51][29] = 2.11;
    path_delay[51][30] = 2.11;
    path_delay[51][31] = 2.11;
    path_delay[33][32] = 1.6;
    path_delay[33][10] = 1.58;
    path_delay[33][11] = 1.58;
    path_delay[33][12] = 1.6;
    path_delay[33][13] = 1.9;
    path_delay[33][14] = 1.9;
    path_delay[33][15] = 1.9;
    path_delay[33][16] = 1.58;
    path_delay[33][17] = 1.87;
    path_delay[33][18] = 1.87;
    path_delay[33][19] = 1.87;
    path_delay[33][1] = 0.38;
    path_delay[33][20] = 1.89;
    path_delay[33][21] = 2.19;
    path_delay[33][22] = 2.19;
    path_delay[33][23] = 2.19;
    path_delay[33][24] = 1.9;
    path_delay[33][25] = 2.19;
    path_delay[33][26] = 2.19;
    path_delay[33][27] = 2.19;
    path_delay[33][28] = 2.22;
    path_delay[33][29] = 2.51;
    path_delay[33][2] = 0.85;
    path_delay[33][30] = 2.51;
    path_delay[33][31] = 2.51;
    path_delay[33][3] = 0.95;
    path_delay[33][4] = 0.99;
    path_delay[33][5] = 1.29;
    path_delay[33][6] = 1.29;
    path_delay[33][7] = 1.29;
    path_delay[33][8] = 1.29;
    path_delay[33][9] = 1.58;
    path_delay[52][32] = 1.19;
    path_delay[52][20] = 0.38;
    path_delay[52][21] = 0.69;
    path_delay[52][22] = 0.79;
    path_delay[52][23] = 0.79;
    path_delay[52][24] = 1.0;
    path_delay[52][25] = 1.29;
    path_delay[52][26] = 1.29;
    path_delay[52][27] = 1.29;
    path_delay[52][28] = 1.32;
    path_delay[52][29] = 1.61;
    path_delay[52][30] = 1.61;
    path_delay[52][31] = 1.61;
    path_delay[53][32] = 1.36;
    path_delay[53][21] = 0.38;
    path_delay[53][22] = 0.85;
    path_delay[53][23] = 0.95;
    path_delay[53][24] = 1.16;
    path_delay[53][25] = 1.46;
    path_delay[53][26] = 1.46;
    path_delay[53][27] = 1.46;
    path_delay[53][28] = 1.48;
    path_delay[53][29] = 1.77;
    path_delay[53][30] = 1.77;
    path_delay[53][31] = 1.77;
    path_delay[54][32] = 1.51;
    path_delay[54][22] = 0.38;
    path_delay[54][23] = 1.01;
    path_delay[54][24] = 1.32;
    path_delay[54][25] = 1.61;
    path_delay[54][26] = 1.61;
    path_delay[54][27] = 1.61;
    path_delay[54][28] = 1.64;
    path_delay[54][29] = 1.93;
    path_delay[54][30] = 1.93;
    path_delay[54][31] = 1.93;
    path_delay[55][32] = 1.57;
    path_delay[55][23] = 0.38;
    path_delay[55][24] = 1.37;
    path_delay[55][25] = 1.67;
    path_delay[55][26] = 1.67;
    path_delay[55][27] = 1.67;
    path_delay[55][28] = 1.69;
    path_delay[55][29] = 1.98;
    path_delay[55][30] = 1.98;
    path_delay[55][31] = 1.98;
    path_delay[56][32] = 1.14;
    path_delay[56][24] = 0.38;
    path_delay[56][25] = 0.69;
    path_delay[56][26] = 0.79;
    path_delay[56][27] = 0.79;
    path_delay[56][28] = 0.82;
    path_delay[56][29] = 1.11;
    path_delay[56][30] = 1.11;
    path_delay[56][31] = 1.11;
    path_delay[57][32] = 1.36;
    path_delay[57][25] = 0.38;
    path_delay[57][26] = 0.85;
    path_delay[57][27] = 0.95;
    path_delay[57][28] = 0.99;
    path_delay[57][29] = 1.29;
    path_delay[57][30] = 1.29;
    path_delay[57][31] = 1.29;
    path_delay[58][32] = 1.51;
    path_delay[58][26] = 0.38;
    path_delay[58][27] = 1.01;
    path_delay[58][28] = 1.15;
    path_delay[58][29] = 1.44;
    path_delay[58][30] = 1.44;
    path_delay[58][31] = 1.44;
    path_delay[59][32] = 1.57;
    path_delay[59][27] = 0.38;
    path_delay[59][28] = 1.21;
    path_delay[59][29] = 1.5;
    path_delay[59][30] = 1.5;
    path_delay[59][31] = 1.5;
    path_delay[60][32] = 1.07;
    path_delay[60][28] = 0.38;
    path_delay[60][29] = 0.69;
    path_delay[60][30] = 0.79;
    path_delay[60][31] = 0.79;
    path_delay[61][32] = 1.23;
    path_delay[61][29] = 0.38;
    path_delay[61][30] = 0.85;
    path_delay[61][31] = 0.95;
    path_delay[34][32] = 1.76;
    path_delay[34][10] = 1.74;
    path_delay[34][11] = 1.74;
    path_delay[34][12] = 1.76;
    path_delay[34][13] = 2.05;
    path_delay[34][14] = 2.05;
    path_delay[34][15] = 2.05;
    path_delay[34][16] = 1.73;
    path_delay[34][17] = 2.03;
    path_delay[34][18] = 2.03;
    path_delay[34][19] = 2.03;
    path_delay[34][20] = 2.05;
    path_delay[34][21] = 2.34;
    path_delay[34][22] = 2.34;
    path_delay[34][23] = 2.34;
    path_delay[34][24] = 2.06;
    path_delay[34][25] = 2.35;
    path_delay[34][26] = 2.35;
    path_delay[34][27] = 2.35;
    path_delay[34][28] = 2.37;
    path_delay[34][29] = 2.67;
    path_delay[34][2] = 0.38;
    path_delay[34][30] = 2.67;
    path_delay[34][31] = 2.67;
    path_delay[34][3] = 1.01;
    path_delay[34][4] = 1.15;
    path_delay[34][5] = 1.44;
    path_delay[34][6] = 1.44;
    path_delay[34][7] = 1.44;
    path_delay[34][8] = 1.44;
    path_delay[34][9] = 1.74;
    path_delay[62][32] = 1.39;
    path_delay[62][30] = 0.38;
    path_delay[62][31] = 1.01;
    path_delay[63][32] = 1.44;
    path_delay[63][31] = 0.38;
    path_delay[35][32] = 1.82;
    path_delay[35][10] = 1.79;
    path_delay[35][11] = 1.79;
    path_delay[35][12] = 1.81;
    path_delay[35][13] = 2.11;
    path_delay[35][14] = 2.11;
    path_delay[35][15] = 2.11;
    path_delay[35][16] = 1.79;
    path_delay[35][17] = 2.08;
    path_delay[35][18] = 2.08;
    path_delay[35][19] = 2.08;
    path_delay[35][20] = 2.11;
    path_delay[35][21] = 2.4;
    path_delay[35][22] = 2.4;
    path_delay[35][23] = 2.4;
    path_delay[35][24] = 2.11;
    path_delay[35][25] = 2.4;
    path_delay[35][26] = 2.4;
    path_delay[35][27] = 2.4;
    path_delay[35][28] = 2.43;
    path_delay[35][29] = 2.72;
    path_delay[35][30] = 2.72;
    path_delay[35][31] = 2.72;
    path_delay[35][3] = 0.38;
    path_delay[35][4] = 1.21;
    path_delay[35][5] = 1.5;
    path_delay[35][6] = 1.5;
    path_delay[35][7] = 1.5;
    path_delay[35][8] = 1.5;
    path_delay[35][9] = 1.79;
    path_delay[36][32] = 1.32;
    path_delay[36][10] = 1.29;
    path_delay[36][11] = 1.29;
    path_delay[36][12] = 1.32;
    path_delay[36][13] = 1.61;
    path_delay[36][14] = 1.61;
    path_delay[36][15] = 1.61;
    path_delay[36][16] = 1.29;
    path_delay[36][17] = 1.58;
    path_delay[36][18] = 1.58;
    path_delay[36][19] = 1.58;
    path_delay[36][20] = 1.61;
    path_delay[36][21] = 1.9;
    path_delay[36][22] = 1.9;
    path_delay[36][23] = 1.9;
    path_delay[36][24] = 1.61;
    path_delay[36][25] = 1.9;
    path_delay[36][26] = 1.9;
    path_delay[36][27] = 1.9;
    path_delay[36][28] = 1.93;
    path_delay[36][29] = 2.22;
    path_delay[36][30] = 2.22;
    path_delay[36][31] = 2.22;
    path_delay[36][4] = 0.38;
    path_delay[36][5] = 0.69;
    path_delay[36][6] = 0.79;
    path_delay[36][7] = 0.79;
    path_delay[36][8] = 1.0;
    path_delay[36][9] = 1.29;
    path_delay[37][32] = 1.48;
    path_delay[37][10] = 1.46;
    path_delay[37][11] = 1.46;
    path_delay[37][12] = 1.48;
    path_delay[37][13] = 1.77;
    path_delay[37][14] = 1.77;
    path_delay[37][15] = 1.77;
    path_delay[37][16] = 1.45;
    path_delay[37][17] = 1.75;
    path_delay[37][18] = 1.75;
    path_delay[37][19] = 1.75;
    path_delay[37][20] = 1.77;
    path_delay[37][21] = 2.06;
    path_delay[37][22] = 2.06;
    path_delay[37][23] = 2.06;
    path_delay[37][24] = 1.78;
    path_delay[37][25] = 2.07;
    path_delay[37][26] = 2.07;
    path_delay[37][27] = 2.07;
    path_delay[37][28] = 2.09;
    path_delay[37][29] = 2.39;
    path_delay[37][30] = 2.39;
    path_delay[37][31] = 2.39;
    path_delay[37][5] = 0.38;
    path_delay[37][6] = 0.85;
    path_delay[37][7] = 0.95;
    path_delay[37][8] = 1.16;
    path_delay[37][9] = 1.46;
    path_delay[38][32] = 1.64;
    path_delay[38][10] = 1.61;
    path_delay[38][11] = 1.61;
    path_delay[38][12] = 1.64;
    path_delay[38][13] = 1.93;
    path_delay[38][14] = 1.93;
    path_delay[38][15] = 1.93;
    path_delay[38][16] = 1.61;
    path_delay[38][17] = 1.9;
    path_delay[38][18] = 1.9;
    path_delay[38][19] = 1.9;
    path_delay[38][20] = 1.93;
    path_delay[38][21] = 2.22;
    path_delay[38][22] = 2.22;
    path_delay[38][23] = 2.22;
    path_delay[38][24] = 1.93;
    path_delay[38][25] = 2.23;
    path_delay[38][26] = 2.23;
    path_delay[38][27] = 2.23;
    path_delay[38][28] = 2.25;
    path_delay[38][29] = 2.54;
    path_delay[38][30] = 2.54;
    path_delay[38][31] = 2.54;
    path_delay[38][6] = 0.38;
    path_delay[38][7] = 1.01;
    path_delay[38][8] = 1.32;
    path_delay[38][9] = 1.61;
    path_delay[39][32] = 1.69;
    path_delay[39][10] = 1.67;
    path_delay[39][11] = 1.67;
    path_delay[39][12] = 1.69;
    path_delay[39][13] = 1.98;
    path_delay[39][14] = 1.98;
    path_delay[39][15] = 1.98;
    path_delay[39][16] = 1.67;
    path_delay[39][17] = 1.96;
    path_delay[39][18] = 1.96;
    path_delay[39][19] = 1.96;
    path_delay[39][20] = 1.98;
    path_delay[39][21] = 2.28;
    path_delay[39][22] = 2.28;
    path_delay[39][23] = 2.28;
    path_delay[39][24] = 1.99;
    path_delay[39][25] = 2.28;
    path_delay[39][26] = 2.28;
    path_delay[39][27] = 2.28;
    path_delay[39][28] = 2.3;
    path_delay[39][29] = 2.6;
    path_delay[39][30] = 2.6;
    path_delay[39][31] = 2.6;
    path_delay[39][7] = 0.38;
    path_delay[39][8] = 1.37;
    path_delay[39][9] = 1.67;
    path_delay[40][32] = 1.27;
    path_delay[40][10] = 0.79;
    path_delay[40][11] = 0.79;
    path_delay[40][12] = 0.82;
    path_delay[40][13] = 1.11;
    path_delay[40][14] = 1.11;
    path_delay[40][15] = 1.11;
    path_delay[40][16] = 1.24;
    path_delay[40][17] = 1.53;
    path_delay[40][18] = 1.53;
    path_delay[40][19] = 1.53;
    path_delay[40][20] = 1.56;
    path_delay[40][21] = 1.85;
    path_delay[40][22] = 1.85;
    path_delay[40][23] = 1.85;
    path_delay[40][24] = 1.56;
    path_delay[40][25] = 1.86;
    path_delay[40][26] = 1.86;
    path_delay[40][27] = 1.86;
    path_delay[40][28] = 1.88;
    path_delay[40][29] = 2.17;
    path_delay[40][30] = 2.17;
    path_delay[40][31] = 2.17;
    path_delay[40][8] = 0.38;
    path_delay[40][9] = 0.69;
    path_delay[41][32] = 1.48;
    path_delay[41][10] = 0.85;
    path_delay[41][11] = 0.95;
    path_delay[41][12] = 0.99;
    path_delay[41][13] = 1.29;
    path_delay[41][14] = 1.29;
    path_delay[41][15] = 1.29;
    path_delay[41][16] = 1.45;
    path_delay[41][17] = 1.75;
    path_delay[41][18] = 1.75;
    path_delay[41][19] = 1.75;
    path_delay[41][20] = 1.77;
    path_delay[41][21] = 2.06;
    path_delay[41][22] = 2.06;
    path_delay[41][23] = 2.06;
    path_delay[41][24] = 1.77;
    path_delay[41][25] = 2.07;
    path_delay[41][26] = 2.07;
    path_delay[41][27] = 2.07;
    path_delay[41][28] = 2.09;
    path_delay[41][29] = 2.38;
    path_delay[41][30] = 2.38;
    path_delay[41][31] = 2.38;
    path_delay[41][9] = 0.38;
}

// ============================================================================
// TESTBENCH -- Monte Carlo, per-sample EDP. Gate switches are reset via
// reset_all_gate_switches() (global registry, width/structure-agnostic --
// works the same whether the design is a standalone block, one ClaBlock4,
// or multiple blocks + a ClaTree).
// ============================================================================
SC_MODULE(Testbench) {
    sc_out<sc_lv<32>> A, B;
    sc_in<sc_lv<32>>  Sum;
    sc_in<bool>      Cout;

    CarryLookaheadAdder32* cla_ptr;

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
        std::uniform_int_distribution<unsigned long long> dist(0ULL, 4294967295ULL);

        A.write(make_32bit_vector(0));
        B.write(make_32bit_vector(0));
        wait(50, SC_NS);
        prev_a = 0; prev_b = 0;
        prev_sum = to_unsigned_32bit(Sum.read());
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

        A.write(make_32bit_vector(a_value));
        B.write(make_32bit_vector(b_value));
        wait(50, SC_NS);

        unsigned int expected_result = a_value + b_value;
        unsigned int actual_sum      = to_unsigned_32bit(Sum.read());
        bool actual_cout             = Cout.read();
        unsigned int actual_result   = actual_sum + (actual_cout ? 4294967296U : 0U);
        if (expected_result != actual_result) number_of_errors++;

        unsigned int switches_i = cla_ptr->total_switches();

        bool in_changed[NUM_IN];
        for (int k = 0; k < 32; k++) in_changed[k] = (((a_value >> k) & 1U) != ((prev_a >> k) & 1U));
        for (int k = 0; k < 32; k++) in_changed[32 + k] = (((b_value >> k) & 1U) != ((prev_b >> k) & 1U));

        bool out_changed[NUM_OUT];
        for (int k = 0; k < 32; k++) out_changed[k] = (((actual_sum >> k) & 1U) != ((prev_sum >> k) & 1U));
        out_changed[32] = (actual_cout != prev_cout);

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

    sc_signal<sc_lv<32>> A, B, Sum;
    sc_signal<bool>     Cout;

    CarryLookaheadAdder32 cla("CLA32");
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

    std::cout << "CLA-32-MC-pathEDP: SAMPLES=" << total_samples
              << " AVG_SWITCHES=" << avg_switches
              << " AVG_DELAY_ns=" << avg_delay
              << " AVG_EDP_correct=" << avg_edp_correct
              << " AVG_EDP_naive=" << avg_edp_naive
              << " pct_diff=" << ((avg_edp_naive / avg_edp_correct - 1.0) * 100.0) << "%\n";

    return 0;
}
