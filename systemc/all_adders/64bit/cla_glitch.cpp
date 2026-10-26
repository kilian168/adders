// cla_glitch.cpp -- 64-bit carry-lookahead adder (16 x 4-bit blocks + lookahead tree)
// Architecture-only, synthesizable-subset SystemC for the Intel
// SystemC Compiler (ICSC, https://github.com/intel/systemc-compiler).
// Every individual AND/OR/XOR/NOT gate instance from the original
// switching-activity version is instantiated here too, by the same
// name and with the same wiring -- just as a plain combinational
// SC_METHOD (no sc_event, which ICSC's synthesizable subset forbids)
// and with no switch counting (not needed here). sc_uint<N> is used
// instead of sc_lv<N>, which ICSC also does not support.
#include <systemc.h>
#include <string>
#include <cstdio>

// ==========================================
// Clean 2-input gate primitive (AND/OR/XOR), purely combinational.
// ==========================================
enum GateOp { GATE_AND, GATE_OR, GATE_XOR };

SC_MODULE(Gate) {
    sc_in<bool> in1, in2;
    sc_out<bool> out;
    GateOp op;

    SC_HAS_PROCESS(Gate);

    Gate(sc_module_name name, GateOp gate_op) : sc_module(name), op(gate_op) {
        SC_METHOD(logic);
        sensitive << in1 << in2;
    }

    void logic() {
        bool a = in1.read(), b = in2.read();
        if (op == GATE_AND)      out = a & b;
        else if (op == GATE_OR)  out = a | b;
        else /* GATE_XOR */      out = a ^ b;
    }
};

// ==========================================
// 4-bit carry-lookahead block (38 gates): full lookahead within the
// block, plus group propagate/generate (PG/GG) outputs so multiple
// blocks can be combined by a higher-level lookahead tree without
// rippling Cin through each block in turn. Exact gate-for-gate port of
// the original ClaBlock4.
// ==========================================
SC_MODULE(ClaBlock4) {
    sc_in<sc_uint<4>>  A, B;
    sc_in<bool>      Cin;
    sc_out<sc_uint<4>> Sum;
    sc_out<bool>     Cout;
    sc_out<bool>     PG;   // group propagate = p3*p2*p1*p0  (= c4a6)
    sc_out<bool>     GG;   // group generate  = g3|p3*g2|... (= c4o3)

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
        sensitive << A << B;

        SC_METHOD(pack_outputs);
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
        g_c4a6 = new Gate("C4A6", GATE_AND); g_c4a6->in1(c4a4);  g_c4a6->in2(p[0]);  g_c4a6->out(c4a6);
        g_c4a7 = new Gate("C4A7", GATE_AND); g_c4a7->in1(c4a6);  g_c4a7->in2(Cin);   g_c4a7->out(c4a7);
        g_c4o1 = new Gate("C4O1", GATE_OR);  g_c4o1->in1(g[3]);  g_c4o1->in2(c4a1);  g_c4o1->out(c4o1);
        g_c4o2 = new Gate("C4O2", GATE_OR);  g_c4o2->in1(c4o1);  g_c4o2->in2(c4a3);  g_c4o2->out(c4o2);
        g_c4o3 = new Gate("C4O3", GATE_OR);  g_c4o3->in1(c4o2);  g_c4o3->in2(c4a5);  g_c4o3->out(c4o3);
        g_c4o4 = new Gate("C4O4", GATE_OR);  g_c4o4->in1(c4o3);  g_c4o4->in2(c4a7);  g_c4o4->out(c4o4);

        g_s[0] = new Gate("S0", GATE_XOR); g_s[0]->in1(p[0]); g_s[0]->in2(Cin);  g_s[0]->out(s[0]);
        g_s[1] = new Gate("S1", GATE_XOR); g_s[1]->in1(p[1]); g_s[1]->in2(c1o);  g_s[1]->out(s[1]);
        g_s[2] = new Gate("S2", GATE_XOR); g_s[2]->in1(p[2]); g_s[2]->in2(c2o2); g_s[2]->out(s[2]);
        g_s[3] = new Gate("S3", GATE_XOR); g_s[3]->in1(p[3]); g_s[3]->in2(c3o3); g_s[3]->out(s[3]);
    }

    void unpack_inputs() {
        sc_uint<4> va = A.read(), vb = B.read();
        for (int i = 0; i < 4; i++) {
            a_bit[i] = va[i];
            b_bit[i] = vb[i];
        }
    }

    void pack_outputs() {
        sc_uint<4> out;
        for (int i = 0; i < 4; i++) out[i] = s[i].read();
        Sum = out;
        Cout = c4o4.read();
        PG = c4a6.read();
        GG = c4o3.read();
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
// Second-level recursive carry-lookahead tree: combines 16 blocks'
// group-P/G in a balanced binary tree (Brent-Kung style). Exact port of
// the original ClaTree -- same recursive build_range() algorithm, same
// gate naming (SPLIT_A/SPLIT_O/COMB_PG/COMB_A/COMB_O/COUT_A/COUT_O) --
// only the dynamic std::vector storage became fixed-size arrays
// (ICSC's documented subset doesn't mention std::vector support) and
// the gate primitive lost its sc_event-based delay/switch counting.
// Gate count for N leaves: 5*(N-1)+2 = 77 for N=16.
// ==========================================
struct Wire {
    sc_in<bool>* port = nullptr;
    sc_signal<bool>* sig = nullptr;
    Wire() {}
    Wire(sc_in<bool>& p) : port(&p) {}
    Wire(sc_signal<bool>& s) : sig(&s) {}
};

SC_MODULE(ClaTree) {
    static const int NLEAF = 16;
    static const int NGATE = 77;

    sc_vector<sc_in<bool>>  PG, GG;
    sc_in<bool>             Cin;
    sc_vector<sc_out<bool>> C;      // carries into blocks 1..NLEAF-1
    sc_out<bool>            Cout;   // overall carry out

    Gate* gates[NGATE];
    sc_signal<bool>* sigs[NGATE];
    int gate_count;
    Wire leaf_cin[NLEAF];           // leaf_cin[i] = resolved Cin wire for block i (i=0 unused)
    sc_signal<bool>* cout_sig;
    sc_in<bool> leaf_cin_port[NLEAF];  // ports for drive_outputs to read leaf_cin through (ICSC-synthesizable: no raw pointer reads in process code)
    sc_in<bool> cout_in_port;

    SC_HAS_PROCESS(ClaTree);

    ClaTree(sc_module_name name)
        : sc_module(name), PG("PG", NLEAF), GG("GG", NLEAF), C("C", NLEAF - 1), gate_count(0)
    {
        Wire root_cin(Cin);
        RangeResult root = build_range(0, NLEAF, root_cin);
        sc_signal<bool>* cout_and = gate2("COUT_A", GATE_AND, root.pg, Wire(Cin));
        cout_sig = gate2("COUT_O", GATE_OR, root.gg, Wire(*cout_and));

        leaf_cin_port[0](Cin);  // index 0 unused but SystemC requires every port bound
        for (int i = 1; i < NLEAF; i++) leaf_cin_port[i](*leaf_cin[i].sig);
        cout_in_port(*cout_sig);

        SC_METHOD(drive_outputs);
        for (int i = 1; i < NLEAF; i++) sensitive << leaf_cin_port[i];
        sensitive << cout_in_port;
    }

    void drive_outputs() {
        for (int i = 1; i < NLEAF; i++) C[i - 1].write(leaf_cin_port[i].read());
        Cout.write(cout_in_port.read());
    }

    static void bind_wire(sc_in<bool>& dst, const Wire& w) {
        if (w.port) dst(*w.port); else dst(*w.sig);
    }

    sc_signal<bool>* gate2(const char* nm, GateOp op, const Wire& a, const Wire& b) {
        Gate* g = new Gate(nm, op);
        bind_wire(g->in1, a);
        bind_wire(g->in2, b);
        sc_signal<bool>* out = new sc_signal<bool>();
        g->out(*out);
        gates[gate_count] = g;
        sigs[gate_count] = out;
        gate_count++;
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

    ~ClaTree() {
        for (int i = 0; i < gate_count; i++) { delete gates[i]; delete sigs[i]; }
    }
};

// ==========================================
// 64-bit carry-lookahead adder: 16 x 4-bit ClaBlock4 blocks, combined
// by one ClaTree. All blocks compute their group-PG/GG in parallel
// (independent of Cin); the tree resolves every block's carry-in and
// the overall Cout from those group signals in O(log2 16) gate delays.
// Exact gate-for-gate port of the original CarryLookaheadAdder64.
// ==========================================
SC_MODULE(CarryLookaheadAdder64) {
    static const int NBLK = 16;

    sc_in<sc_uint<64>>  A, B;
    sc_out<sc_uint<64>> Sum;
    sc_out<bool>         Cout;

    sc_signal<sc_uint<4>> a_slice[NBLK], b_slice[NBLK], s_slice[NBLK];
    sc_signal<bool>       blk_cout[NBLK];
    sc_signal<bool>       blk_pg[NBLK], blk_gg[NBLK];
    sc_signal<bool>       c_mid[NBLK - 1];
    sc_signal<bool>       const_zero;      // tied low for block 0's Cin
    void drive_const_zero() { const_zero.write(false); }  // real tie-low driver (SystemC's implicit zero-init doesn't synthesize)

    ClaBlock4* blk[NBLK];
    ClaTree*   tree;

    SC_CTOR(CarryLookaheadAdder64) {
        SC_METHOD(drive_const_zero);
        for (int i = 0; i < NBLK; i++) {
            blk[i] = new ClaBlock4(("BLK" + std::to_string(i)).c_str());
            blk[i]->A(a_slice[i]);
            blk[i]->B(b_slice[i]);
            blk[i]->Cin(i == 0 ? const_zero : c_mid[i - 1]);
            blk[i]->Sum(s_slice[i]);
            blk[i]->Cout(blk_cout[i]);
            blk[i]->PG(blk_pg[i]);
            blk[i]->GG(blk_gg[i]);
        }

        tree = new ClaTree("TREE");
        for (int i = 0; i < NBLK; i++) { tree->PG[i](blk_pg[i]); tree->GG[i](blk_gg[i]); }
        tree->Cin(const_zero);
        for (int i = 0; i < NBLK - 1; i++) tree->C[i](c_mid[i]);
        tree->Cout(Cout);

        SC_METHOD(split_inputs);
        sensitive << A << B;

        SC_METHOD(combine_outputs);
        for (int i = 0; i < NBLK; i++) sensitive << s_slice[i];
    }

    void split_inputs() {
        sc_uint<64> a = A.read(), b = B.read();
        for (int blki = 0; blki < NBLK; blki++) {
            sc_uint<4> a4, b4;
            for (int i = 0; i < 4; i++) { a4[i] = a[blki * 4 + i]; b4[i] = b[blki * 4 + i]; }
            a_slice[blki] = a4;
            b_slice[blki] = b4;
        }
    }

    void combine_outputs() {
        sc_uint<64> s;
        for (int blki = 0; blki < NBLK; blki++) {
            sc_uint<4> s4 = s_slice[blki].read();
            for (int i = 0; i < 4; i++) s[blki * 4 + i] = s4[i];
        }
        Sum = s;
    }

    ~CarryLookaheadAdder64() {
        for (int i = 0; i < NBLK; i++) delete blk[i];
        delete tree;
    }
};

// ==========================================
// Wrapper: instantiates the DUT and binds every port to a plain signal.
// ICSC requires this -- the DUT itself is never the top module directly.
// ==========================================
SC_MODULE(Tb) {
    sc_signal<sc_uint<64>> A;
    sc_signal<sc_uint<64>> B;
    sc_signal<sc_uint<64>> Sum;
    sc_signal<bool> Cout;

    CarryLookaheadAdder64 dut_inst{"dut_inst"};

    SC_CTOR(Tb) {
        dut_inst.A(A);
        dut_inst.B(B);
        dut_inst.Sum(Sum);
        dut_inst.Cout(Cout);
    }
};

int sc_main(int argc, char** argv) {
    Tb tb("tb");
    sc_start();
    return 0;
}
