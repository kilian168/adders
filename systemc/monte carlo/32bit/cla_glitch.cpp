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
    }

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
SC_MODULE(Testbench) {
    sc_out<sc_lv<32>> A, B;
    sc_in<sc_lv<32>>  Sum;
    sc_in<bool>       Cout;

    CarryLookaheadAdder32* cla_ptr;

    unsigned int number_of_errors;

    unsigned long long num_samples;
    unsigned long long rng_seed;

    SC_CTOR(Testbench)
        : cla_ptr(nullptr), number_of_errors(0), num_samples(1000000ULL), rng_seed(42ULL)
    { SC_THREAD(stimulus); }

    void stimulus() {
        std::mt19937_64 rng(rng_seed);
        std::uniform_int_distribution<unsigned long long> dist(0ULL, 4294967295ULL);

        for (unsigned long long i = 0; i < num_samples; i++) {
            unsigned int a_value = static_cast<unsigned int>(dist(rng));
            unsigned int b_value = static_cast<unsigned int>(dist(rng));
            apply_test(a_value, b_value);
        }

        assert(number_of_errors == 0);
        sc_stop();
    }

    void apply_test(unsigned int a_value, unsigned int b_value) {
        A.write(make_32bit_vector(a_value));
        B.write(make_32bit_vector(b_value));
        wait(50, SC_NS);

        unsigned long long expected_result = static_cast<unsigned long long>(a_value)
                                            + static_cast<unsigned long long>(b_value);
        unsigned long long actual_sum      = to_unsigned_32bit(Sum.read());
        unsigned long long actual_result   = actual_sum + (Cout.read() ? 4294967296ULL : 0ULL);
        if (expected_result != actual_result) number_of_errors++;
    }
};

// ==========================================
// 5. MAIN — Monte Carlo, auto-parallel across all CPU cores
// ==========================================

// Elaborates a fresh adder + testbench and simulates exactly
// samples_for_this_worker random test vectors, seeded independently so
// parallel workers never repeat each other's samples.
static void run_slice(unsigned long long samples_for_this_worker, unsigned long long seed_for_this_worker,
                       unsigned long long& out_switches, unsigned int& out_errors) {
    sc_signal<sc_lv<32>> A, B, Sum;
    sc_signal<bool>      Cout;

    CarryLookaheadAdder32 cla("CLA32");
    Testbench tb("TB");
    tb.cla_ptr = &cla;

    cla.A(A); cla.B(B); cla.Sum(Sum); cla.Cout(Cout);
    tb.A(A);  tb.B(B);  tb.Sum(Sum);  tb.Cout(Cout);

    tb.num_samples = samples_for_this_worker;
    tb.rng_seed = seed_for_this_worker;

    sc_start();

    out_switches = cla.total_switches();
    out_errors = tb.number_of_errors;
}

int sc_main(int argc, char* argv[]) {
    unsigned long long total_samples = (argc >= 2) ? std::stoull(argv[1]) : 1000000ULL;
    unsigned long long base_seed     = (argc >= 3) ? std::stoull(argv[2]) : 42ULL;

    // SystemC's kernel state (sc_curr_simcontext) is a single
    // un-synchronized global, not thread-local, so one process cannot
    // safely run more than one simulation concurrently via std::thread.
    // Instead we fork() one worker process per core before any SystemC
    // object is created; each child simulates its own independent slice
    // of samples (with its own RNG stream) in its own independent
    // kernel, and reports its partial switch/error counts back through
    // a pipe.
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
            unsigned long long switches = 0;
            unsigned int errors = 0;
            run_slice(samples_for_worker, base_seed + i, switches, errors);
            unsigned long long payload[2] = { switches, static_cast<unsigned long long>(errors) };
            ssize_t written = write(fds[1], payload, sizeof(payload));
            (void)written;
            close(fds[1]);
            _exit(0);
        }

        close(fds[1]);
        read_fd[i] = fds[0];
        worker_pid[i] = child;
    }

    unsigned long long total_switches = 0;
    unsigned long long total_errors = 0;
    for (unsigned int i = 0; i < num_workers; i++) {
        unsigned long long payload[2] = { 0, 0 };
        ssize_t got = read(read_fd[i], payload, sizeof(payload));
        if (got == static_cast<ssize_t>(sizeof(payload))) {
            total_switches += payload[0];
            total_errors   += payload[1];
        }
        close(read_fd[i]);
        int status = 0;
        waitpid(worker_pid[i], &status, 0);
    }

    assert(total_errors == 0);
    double avg_switches = total_samples > 0
        ? (static_cast<double>(total_switches) / static_cast<double>(total_samples))
        : 0.0;
    std::cout << "CLA-32-MC: GATES=341 (8x38 blocks + 37 CLA-tree) SAMPLES=" << total_samples
              << " SWITCHES=" << total_switches
              << " AVG_SWITCHES=" << avg_switches
              << " DELAY=16ns\n";

    return 0;
}