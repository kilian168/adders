// main.cpp
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
    for (int i = 0; i < 32; i++) {
        bits[i] = ((value >> i) & 1U) != 0U;
    }
    return bits;
}

static unsigned int to_unsigned_32bit(const sc_lv<32>& bits) {
    unsigned int value = 0;
    for (int i = 0; i < 32; i++) {
        if (bits[i].is_01() && bits[i].to_bool()) {
            value += (1U << i);
        }
    }
    return value;
}

// ==========================================
// 1. GENERIC GATE-LEVEL PRIMITIVE
//    Each instance is exactly one 2-input AND/OR/XOR gate.
//    It is sensitive to each of its two inputs independently, so
//    inputs arriving at different simulated times each trigger their
//    own evaluation (and are counted as separate switches if the
//    gate's output value changes), instead of being batched into one
//    combined evaluation. Output commit is delayed by 1ns from the
//    evaluation that caused the change, modeling real gate delay.
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

    void reset_switches() { switches = 0; }
};

// ==========================================
// 2. GATE-LEVEL FULL ADDER MODULE (5 gates)
//    s1   = XOR(a,b)        c1  = AND(a,b)
//    sum  = XOR(s1,cin)     c2  = AND(s1,cin)
//    cout = OR(c1,c2)
// ==========================================
SC_MODULE(FullAdder) {
    sc_in<bool> a;
    sc_in<bool> b;
    sc_in<bool> cin;

    sc_out<bool> sum;
    sc_out<bool> cout;

    sc_signal<bool> s1, c1, c2;

    Gate *g_xor1, *g_and1, *g_xor2, *g_and2, *g_or1;

    SC_CTOR(FullAdder) {
        g_xor1 = new Gate("XOR1", GATE_XOR);
        g_xor1->in1(a); g_xor1->in2(b); g_xor1->out(s1);

        g_and1 = new Gate("AND1", GATE_AND);
        g_and1->in1(a); g_and1->in2(b); g_and1->out(c1);

        g_xor2 = new Gate("XOR2", GATE_XOR);
        g_xor2->in1(s1); g_xor2->in2(cin); g_xor2->out(sum);

        g_and2 = new Gate("AND2", GATE_AND);
        g_and2->in1(s1); g_and2->in2(cin); g_and2->out(c2);

        g_or1 = new Gate("OR1", GATE_OR);
        g_or1->in1(c1); g_or1->in2(c2); g_or1->out(cout);
    }

    unsigned int total_switches() const {
        return g_xor1->switches + g_and1->switches + g_xor2->switches + g_and2->switches + g_or1->switches;
    }

    void reset_switches() {
        g_xor1->reset_switches(); g_and1->reset_switches(); g_xor2->reset_switches();
        g_and2->reset_switches(); g_or1->reset_switches();
    }

    ~FullAdder() {
        delete g_xor1; delete g_and1; delete g_xor2; delete g_and2; delete g_or1;
    }
};

// ==========================================
// 2. 32-BIT RIPPLE CARRY ADDER
// ==========================================
SC_MODULE(RippleCarryAdder32) {
    sc_in<sc_lv<32>> A;
    sc_in<sc_lv<32>> B;

    sc_out<sc_lv<32>> Sum;
    sc_out<bool> Cout;

    sc_signal<bool> a[32];
    sc_signal<bool> b[32];
    sc_signal<bool> s[32];
    sc_signal<bool> c[32];
    sc_signal<bool> const_zero;

    FullAdder* fa[32];

    SC_CTOR(RippleCarryAdder32) {
        for (int i = 0; i < 32; i++) {
            std::string name = "FA_" + std::to_string(i);
            fa[i] = new FullAdder(name.c_str());

            fa[i]->a(a[i]);
            fa[i]->b(b[i]);
            fa[i]->sum(s[i]);
            fa[i]->cout(c[i]);

            if (i == 0) {
                fa[i]->cin(const_zero);
            } else {
                fa[i]->cin(c[i - 1]);
            }
        }

        SC_METHOD(split_inputs);
        sensitive << A << B;

        SC_METHOD(combine_outputs);
        for (int i = 0; i < 32; i++) {
            sensitive << s[i];
        }
        sensitive << c[31];
    }

    void split_inputs() {
        sc_lv<32> value_a = A.read();
        sc_lv<32> value_b = B.read();

        for (int i = 0; i < 32; i++) {
            if (value_a[i].is_01()) {
                a[i].write(value_a[i].to_bool());
            } else {
                a[i].write(false);
            }

            if (value_b[i].is_01()) {
                b[i].write(value_b[i].to_bool());
            } else {
                b[i].write(false);
            }
        }
    }

    void combine_outputs() {
        sc_lv<32> value_sum;

        for (int i = 0; i < 32; i++) {
            value_sum[i] = s[i].read();
        }

        Sum.write(value_sum);
        Cout.write(c[31].read());
    }

    unsigned int total_switches() const {
        unsigned int total = 0;
        for (int i = 0; i < 32; i++) {
            total += fa[i]->total_switches();
        }
        return total;
    }

    void reset_switches() {
        for (int i = 0; i < 32; i++) fa[i]->reset_switches();
    }

    ~RippleCarryAdder32() {
        for (int i = 0; i < 32; i++) {
            delete fa[i];
        }
    }
};

// ==========================================
// 3. TESTBENCH — Monte Carlo random sampling
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
// extracted from all_critical_paths.csv for RCA-32 (1120 pairs).
// Input pin index: 0..31=A[i], 32..63=B[i]. Output pin index: 0..31=Sum[i], 32=Cout.
// ============================================================================
static const int NUM_IN = 64;
static const int NUM_OUT = 33;
static double path_delay[NUM_IN][NUM_OUT];

static void init_path_delay_table() {
    for (int i = 0; i < NUM_IN; i++)
        for (int o = 0; o < NUM_OUT; o++)
            path_delay[i][o] = 0.0;

    path_delay[0][32] = 9.51;
    path_delay[0][0] = 0.36;
    path_delay[0][10] = 3.15;
    path_delay[0][11] = 3.45;
    path_delay[0][12] = 3.74;
    path_delay[0][13] = 4.04;
    path_delay[0][14] = 4.34;
    path_delay[0][15] = 4.64;
    path_delay[0][16] = 4.94;
    path_delay[0][17] = 5.24;
    path_delay[0][18] = 5.54;
    path_delay[0][19] = 5.84;
    path_delay[0][1] = 0.45;
    path_delay[0][20] = 6.14;
    path_delay[0][21] = 6.44;
    path_delay[0][22] = 6.74;
    path_delay[0][23] = 7.04;
    path_delay[0][24] = 7.34;
    path_delay[0][25] = 7.64;
    path_delay[0][26] = 7.94;
    path_delay[0][27] = 8.24;
    path_delay[0][28] = 8.54;
    path_delay[0][29] = 8.84;
    path_delay[0][2] = 0.75;
    path_delay[0][30] = 9.14;
    path_delay[0][31] = 9.44;
    path_delay[0][3] = 1.05;
    path_delay[0][4] = 1.35;
    path_delay[0][5] = 1.65;
    path_delay[0][6] = 1.95;
    path_delay[0][7] = 2.25;
    path_delay[0][8] = 2.55;
    path_delay[0][9] = 2.85;
    path_delay[10][32] = 6.74;
    path_delay[10][10] = 0.36;
    path_delay[10][11] = 0.67;
    path_delay[10][12] = 0.97;
    path_delay[10][13] = 1.27;
    path_delay[10][14] = 1.57;
    path_delay[10][15] = 1.87;
    path_delay[10][16] = 2.17;
    path_delay[10][17] = 2.47;
    path_delay[10][18] = 2.77;
    path_delay[10][19] = 3.07;
    path_delay[10][20] = 3.36;
    path_delay[10][21] = 3.66;
    path_delay[10][22] = 3.96;
    path_delay[10][23] = 4.26;
    path_delay[10][24] = 4.56;
    path_delay[10][25] = 4.86;
    path_delay[10][26] = 5.16;
    path_delay[10][27] = 5.46;
    path_delay[10][28] = 5.76;
    path_delay[10][29] = 6.06;
    path_delay[10][30] = 6.36;
    path_delay[10][31] = 6.66;
    path_delay[11][32] = 6.44;
    path_delay[11][11] = 0.36;
    path_delay[11][12] = 0.67;
    path_delay[11][13] = 0.97;
    path_delay[11][14] = 1.27;
    path_delay[11][15] = 1.57;
    path_delay[11][16] = 1.87;
    path_delay[11][17] = 2.17;
    path_delay[11][18] = 2.47;
    path_delay[11][19] = 2.77;
    path_delay[11][20] = 3.07;
    path_delay[11][21] = 3.36;
    path_delay[11][22] = 3.66;
    path_delay[11][23] = 3.96;
    path_delay[11][24] = 4.26;
    path_delay[11][25] = 4.56;
    path_delay[11][26] = 4.86;
    path_delay[11][27] = 5.16;
    path_delay[11][28] = 5.46;
    path_delay[11][29] = 5.76;
    path_delay[11][30] = 6.06;
    path_delay[11][31] = 6.36;
    path_delay[12][32] = 6.14;
    path_delay[12][12] = 0.36;
    path_delay[12][13] = 0.67;
    path_delay[12][14] = 0.97;
    path_delay[12][15] = 1.27;
    path_delay[12][16] = 1.57;
    path_delay[12][17] = 1.87;
    path_delay[12][18] = 2.17;
    path_delay[12][19] = 2.47;
    path_delay[12][20] = 2.77;
    path_delay[12][21] = 3.07;
    path_delay[12][22] = 3.36;
    path_delay[12][23] = 3.66;
    path_delay[12][24] = 3.96;
    path_delay[12][25] = 4.26;
    path_delay[12][26] = 4.56;
    path_delay[12][27] = 4.86;
    path_delay[12][28] = 5.16;
    path_delay[12][29] = 5.46;
    path_delay[12][30] = 5.76;
    path_delay[12][31] = 6.06;
    path_delay[13][32] = 5.84;
    path_delay[13][13] = 0.36;
    path_delay[13][14] = 0.67;
    path_delay[13][15] = 0.97;
    path_delay[13][16] = 1.27;
    path_delay[13][17] = 1.57;
    path_delay[13][18] = 1.87;
    path_delay[13][19] = 2.17;
    path_delay[13][20] = 2.47;
    path_delay[13][21] = 2.77;
    path_delay[13][22] = 3.07;
    path_delay[13][23] = 3.36;
    path_delay[13][24] = 3.66;
    path_delay[13][25] = 3.96;
    path_delay[13][26] = 4.26;
    path_delay[13][27] = 4.56;
    path_delay[13][28] = 4.86;
    path_delay[13][29] = 5.16;
    path_delay[13][30] = 5.46;
    path_delay[13][31] = 5.76;
    path_delay[14][32] = 5.54;
    path_delay[14][14] = 0.36;
    path_delay[14][15] = 0.67;
    path_delay[14][16] = 0.97;
    path_delay[14][17] = 1.27;
    path_delay[14][18] = 1.57;
    path_delay[14][19] = 1.87;
    path_delay[14][20] = 2.17;
    path_delay[14][21] = 2.47;
    path_delay[14][22] = 2.77;
    path_delay[14][23] = 3.07;
    path_delay[14][24] = 3.36;
    path_delay[14][25] = 3.66;
    path_delay[14][26] = 3.96;
    path_delay[14][27] = 4.26;
    path_delay[14][28] = 4.56;
    path_delay[14][29] = 4.86;
    path_delay[14][30] = 5.16;
    path_delay[14][31] = 5.46;
    path_delay[15][32] = 5.24;
    path_delay[15][15] = 0.36;
    path_delay[15][16] = 0.67;
    path_delay[15][17] = 0.97;
    path_delay[15][18] = 1.27;
    path_delay[15][19] = 1.57;
    path_delay[15][20] = 1.87;
    path_delay[15][21] = 2.17;
    path_delay[15][22] = 2.47;
    path_delay[15][23] = 2.77;
    path_delay[15][24] = 3.07;
    path_delay[15][25] = 3.36;
    path_delay[15][26] = 3.66;
    path_delay[15][27] = 3.96;
    path_delay[15][28] = 4.26;
    path_delay[15][29] = 4.56;
    path_delay[15][30] = 4.86;
    path_delay[15][31] = 5.16;
    path_delay[16][32] = 4.94;
    path_delay[16][16] = 0.36;
    path_delay[16][17] = 0.67;
    path_delay[16][18] = 0.97;
    path_delay[16][19] = 1.27;
    path_delay[16][20] = 1.57;
    path_delay[16][21] = 1.87;
    path_delay[16][22] = 2.17;
    path_delay[16][23] = 2.47;
    path_delay[16][24] = 2.77;
    path_delay[16][25] = 3.07;
    path_delay[16][26] = 3.36;
    path_delay[16][27] = 3.66;
    path_delay[16][28] = 3.96;
    path_delay[16][29] = 4.26;
    path_delay[16][30] = 4.56;
    path_delay[16][31] = 4.86;
    path_delay[17][32] = 4.64;
    path_delay[17][17] = 0.36;
    path_delay[17][18] = 0.67;
    path_delay[17][19] = 0.97;
    path_delay[17][20] = 1.27;
    path_delay[17][21] = 1.57;
    path_delay[17][22] = 1.87;
    path_delay[17][23] = 2.17;
    path_delay[17][24] = 2.47;
    path_delay[17][25] = 2.77;
    path_delay[17][26] = 3.07;
    path_delay[17][27] = 3.36;
    path_delay[17][28] = 3.66;
    path_delay[17][29] = 3.96;
    path_delay[17][30] = 4.26;
    path_delay[17][31] = 4.56;
    path_delay[18][32] = 4.34;
    path_delay[18][18] = 0.36;
    path_delay[18][19] = 0.67;
    path_delay[18][20] = 0.97;
    path_delay[18][21] = 1.27;
    path_delay[18][22] = 1.57;
    path_delay[18][23] = 1.87;
    path_delay[18][24] = 2.17;
    path_delay[18][25] = 2.47;
    path_delay[18][26] = 2.77;
    path_delay[18][27] = 3.07;
    path_delay[18][28] = 3.36;
    path_delay[18][29] = 3.66;
    path_delay[18][30] = 3.96;
    path_delay[18][31] = 4.26;
    path_delay[19][32] = 4.04;
    path_delay[19][19] = 0.36;
    path_delay[19][20] = 0.67;
    path_delay[19][21] = 0.97;
    path_delay[19][22] = 1.27;
    path_delay[19][23] = 1.57;
    path_delay[19][24] = 1.87;
    path_delay[19][25] = 2.17;
    path_delay[19][26] = 2.47;
    path_delay[19][27] = 2.77;
    path_delay[19][28] = 3.07;
    path_delay[19][29] = 3.36;
    path_delay[19][30] = 3.66;
    path_delay[19][31] = 3.96;
    path_delay[1][32] = 9.43;
    path_delay[1][10] = 3.07;
    path_delay[1][11] = 3.36;
    path_delay[1][12] = 3.66;
    path_delay[1][13] = 3.96;
    path_delay[1][14] = 4.26;
    path_delay[1][15] = 4.56;
    path_delay[1][16] = 4.86;
    path_delay[1][17] = 5.16;
    path_delay[1][18] = 5.46;
    path_delay[1][19] = 5.76;
    path_delay[1][1] = 0.36;
    path_delay[1][20] = 6.06;
    path_delay[1][21] = 6.36;
    path_delay[1][22] = 6.66;
    path_delay[1][23] = 6.96;
    path_delay[1][24] = 7.26;
    path_delay[1][25] = 7.56;
    path_delay[1][26] = 7.86;
    path_delay[1][27] = 8.16;
    path_delay[1][28] = 8.46;
    path_delay[1][29] = 8.76;
    path_delay[1][2] = 0.67;
    path_delay[1][30] = 9.06;
    path_delay[1][31] = 9.36;
    path_delay[1][3] = 0.97;
    path_delay[1][4] = 1.27;
    path_delay[1][5] = 1.57;
    path_delay[1][6] = 1.87;
    path_delay[1][7] = 2.17;
    path_delay[1][8] = 2.47;
    path_delay[1][9] = 2.77;
    path_delay[20][32] = 3.74;
    path_delay[20][20] = 0.36;
    path_delay[20][21] = 0.67;
    path_delay[20][22] = 0.97;
    path_delay[20][23] = 1.27;
    path_delay[20][24] = 1.57;
    path_delay[20][25] = 1.87;
    path_delay[20][26] = 2.17;
    path_delay[20][27] = 2.47;
    path_delay[20][28] = 2.77;
    path_delay[20][29] = 3.07;
    path_delay[20][30] = 3.36;
    path_delay[20][31] = 3.66;
    path_delay[21][32] = 3.44;
    path_delay[21][21] = 0.36;
    path_delay[21][22] = 0.67;
    path_delay[21][23] = 0.97;
    path_delay[21][24] = 1.27;
    path_delay[21][25] = 1.57;
    path_delay[21][26] = 1.87;
    path_delay[21][27] = 2.17;
    path_delay[21][28] = 2.47;
    path_delay[21][29] = 2.77;
    path_delay[21][30] = 3.07;
    path_delay[21][31] = 3.36;
    path_delay[22][32] = 3.14;
    path_delay[22][22] = 0.36;
    path_delay[22][23] = 0.67;
    path_delay[22][24] = 0.97;
    path_delay[22][25] = 1.27;
    path_delay[22][26] = 1.57;
    path_delay[22][27] = 1.87;
    path_delay[22][28] = 2.17;
    path_delay[22][29] = 2.47;
    path_delay[22][30] = 2.77;
    path_delay[22][31] = 3.07;
    path_delay[23][32] = 2.84;
    path_delay[23][23] = 0.36;
    path_delay[23][24] = 0.67;
    path_delay[23][25] = 0.97;
    path_delay[23][26] = 1.27;
    path_delay[23][27] = 1.57;
    path_delay[23][28] = 1.87;
    path_delay[23][29] = 2.17;
    path_delay[23][30] = 2.47;
    path_delay[23][31] = 2.77;
    path_delay[24][32] = 2.54;
    path_delay[24][24] = 0.36;
    path_delay[24][25] = 0.67;
    path_delay[24][26] = 0.97;
    path_delay[24][27] = 1.27;
    path_delay[24][28] = 1.57;
    path_delay[24][29] = 1.87;
    path_delay[24][30] = 2.17;
    path_delay[24][31] = 2.47;
    path_delay[25][32] = 2.24;
    path_delay[25][25] = 0.36;
    path_delay[25][26] = 0.67;
    path_delay[25][27] = 0.97;
    path_delay[25][28] = 1.27;
    path_delay[25][29] = 1.57;
    path_delay[25][30] = 1.87;
    path_delay[25][31] = 2.17;
    path_delay[26][32] = 1.94;
    path_delay[26][26] = 0.36;
    path_delay[26][27] = 0.67;
    path_delay[26][28] = 0.97;
    path_delay[26][29] = 1.27;
    path_delay[26][30] = 1.57;
    path_delay[26][31] = 1.87;
    path_delay[27][32] = 1.64;
    path_delay[27][27] = 0.36;
    path_delay[27][28] = 0.67;
    path_delay[27][29] = 0.97;
    path_delay[27][30] = 1.27;
    path_delay[27][31] = 1.57;
    path_delay[28][32] = 1.35;
    path_delay[28][28] = 0.36;
    path_delay[28][29] = 0.67;
    path_delay[28][30] = 0.97;
    path_delay[28][31] = 1.27;
    path_delay[29][32] = 1.05;
    path_delay[29][29] = 0.36;
    path_delay[29][30] = 0.67;
    path_delay[29][31] = 0.97;
    path_delay[2][32] = 9.13;
    path_delay[2][10] = 2.77;
    path_delay[2][11] = 3.07;
    path_delay[2][12] = 3.36;
    path_delay[2][13] = 3.66;
    path_delay[2][14] = 3.96;
    path_delay[2][15] = 4.26;
    path_delay[2][16] = 4.56;
    path_delay[2][17] = 4.86;
    path_delay[2][18] = 5.16;
    path_delay[2][19] = 5.46;
    path_delay[2][20] = 5.76;
    path_delay[2][21] = 6.06;
    path_delay[2][22] = 6.36;
    path_delay[2][23] = 6.66;
    path_delay[2][24] = 6.96;
    path_delay[2][25] = 7.26;
    path_delay[2][26] = 7.56;
    path_delay[2][27] = 7.86;
    path_delay[2][28] = 8.16;
    path_delay[2][29] = 8.46;
    path_delay[2][2] = 0.36;
    path_delay[2][30] = 8.76;
    path_delay[2][31] = 9.06;
    path_delay[2][3] = 0.67;
    path_delay[2][4] = 0.97;
    path_delay[2][5] = 1.27;
    path_delay[2][6] = 1.57;
    path_delay[2][7] = 1.87;
    path_delay[2][8] = 2.17;
    path_delay[2][9] = 2.47;
    path_delay[30][32] = 0.75;
    path_delay[30][30] = 0.36;
    path_delay[30][31] = 0.67;
    path_delay[31][32] = 0.45;
    path_delay[31][31] = 0.36;
    path_delay[3][32] = 8.83;
    path_delay[3][10] = 2.47;
    path_delay[3][11] = 2.77;
    path_delay[3][12] = 3.07;
    path_delay[3][13] = 3.36;
    path_delay[3][14] = 3.66;
    path_delay[3][15] = 3.96;
    path_delay[3][16] = 4.26;
    path_delay[3][17] = 4.56;
    path_delay[3][18] = 4.86;
    path_delay[3][19] = 5.16;
    path_delay[3][20] = 5.46;
    path_delay[3][21] = 5.76;
    path_delay[3][22] = 6.06;
    path_delay[3][23] = 6.36;
    path_delay[3][24] = 6.66;
    path_delay[3][25] = 6.96;
    path_delay[3][26] = 7.26;
    path_delay[3][27] = 7.56;
    path_delay[3][28] = 7.86;
    path_delay[3][29] = 8.16;
    path_delay[3][30] = 8.46;
    path_delay[3][31] = 8.76;
    path_delay[3][3] = 0.36;
    path_delay[3][4] = 0.67;
    path_delay[3][5] = 0.97;
    path_delay[3][6] = 1.27;
    path_delay[3][7] = 1.57;
    path_delay[3][8] = 1.87;
    path_delay[3][9] = 2.17;
    path_delay[4][32] = 8.53;
    path_delay[4][10] = 2.17;
    path_delay[4][11] = 2.47;
    path_delay[4][12] = 2.77;
    path_delay[4][13] = 3.07;
    path_delay[4][14] = 3.36;
    path_delay[4][15] = 3.66;
    path_delay[4][16] = 3.96;
    path_delay[4][17] = 4.26;
    path_delay[4][18] = 4.56;
    path_delay[4][19] = 4.86;
    path_delay[4][20] = 5.16;
    path_delay[4][21] = 5.46;
    path_delay[4][22] = 5.76;
    path_delay[4][23] = 6.06;
    path_delay[4][24] = 6.36;
    path_delay[4][25] = 6.66;
    path_delay[4][26] = 6.96;
    path_delay[4][27] = 7.26;
    path_delay[4][28] = 7.56;
    path_delay[4][29] = 7.86;
    path_delay[4][30] = 8.16;
    path_delay[4][31] = 8.46;
    path_delay[4][4] = 0.36;
    path_delay[4][5] = 0.67;
    path_delay[4][6] = 0.97;
    path_delay[4][7] = 1.27;
    path_delay[4][8] = 1.57;
    path_delay[4][9] = 1.87;
    path_delay[5][32] = 8.24;
    path_delay[5][10] = 1.87;
    path_delay[5][11] = 2.17;
    path_delay[5][12] = 2.47;
    path_delay[5][13] = 2.77;
    path_delay[5][14] = 3.07;
    path_delay[5][15] = 3.36;
    path_delay[5][16] = 3.66;
    path_delay[5][17] = 3.96;
    path_delay[5][18] = 4.26;
    path_delay[5][19] = 4.56;
    path_delay[5][20] = 4.86;
    path_delay[5][21] = 5.16;
    path_delay[5][22] = 5.46;
    path_delay[5][23] = 5.76;
    path_delay[5][24] = 6.06;
    path_delay[5][25] = 6.36;
    path_delay[5][26] = 6.66;
    path_delay[5][27] = 6.96;
    path_delay[5][28] = 7.26;
    path_delay[5][29] = 7.56;
    path_delay[5][30] = 7.86;
    path_delay[5][31] = 8.16;
    path_delay[5][5] = 0.36;
    path_delay[5][6] = 0.67;
    path_delay[5][7] = 0.97;
    path_delay[5][8] = 1.27;
    path_delay[5][9] = 1.57;
    path_delay[6][32] = 7.94;
    path_delay[6][10] = 1.57;
    path_delay[6][11] = 1.87;
    path_delay[6][12] = 2.17;
    path_delay[6][13] = 2.47;
    path_delay[6][14] = 2.77;
    path_delay[6][15] = 3.07;
    path_delay[6][16] = 3.36;
    path_delay[6][17] = 3.66;
    path_delay[6][18] = 3.96;
    path_delay[6][19] = 4.26;
    path_delay[6][20] = 4.56;
    path_delay[6][21] = 4.86;
    path_delay[6][22] = 5.16;
    path_delay[6][23] = 5.46;
    path_delay[6][24] = 5.76;
    path_delay[6][25] = 6.06;
    path_delay[6][26] = 6.36;
    path_delay[6][27] = 6.66;
    path_delay[6][28] = 6.96;
    path_delay[6][29] = 7.26;
    path_delay[6][30] = 7.56;
    path_delay[6][31] = 7.86;
    path_delay[6][6] = 0.36;
    path_delay[6][7] = 0.67;
    path_delay[6][8] = 0.97;
    path_delay[6][9] = 1.27;
    path_delay[7][32] = 7.64;
    path_delay[7][10] = 1.27;
    path_delay[7][11] = 1.57;
    path_delay[7][12] = 1.87;
    path_delay[7][13] = 2.17;
    path_delay[7][14] = 2.47;
    path_delay[7][15] = 2.77;
    path_delay[7][16] = 3.07;
    path_delay[7][17] = 3.36;
    path_delay[7][18] = 3.66;
    path_delay[7][19] = 3.96;
    path_delay[7][20] = 4.26;
    path_delay[7][21] = 4.56;
    path_delay[7][22] = 4.86;
    path_delay[7][23] = 5.16;
    path_delay[7][24] = 5.46;
    path_delay[7][25] = 5.76;
    path_delay[7][26] = 6.06;
    path_delay[7][27] = 6.36;
    path_delay[7][28] = 6.66;
    path_delay[7][29] = 6.96;
    path_delay[7][30] = 7.26;
    path_delay[7][31] = 7.56;
    path_delay[7][7] = 0.36;
    path_delay[7][8] = 0.67;
    path_delay[7][9] = 0.97;
    path_delay[8][32] = 7.34;
    path_delay[8][10] = 0.97;
    path_delay[8][11] = 1.27;
    path_delay[8][12] = 1.57;
    path_delay[8][13] = 1.87;
    path_delay[8][14] = 2.17;
    path_delay[8][15] = 2.47;
    path_delay[8][16] = 2.77;
    path_delay[8][17] = 3.07;
    path_delay[8][18] = 3.36;
    path_delay[8][19] = 3.66;
    path_delay[8][20] = 3.96;
    path_delay[8][21] = 4.26;
    path_delay[8][22] = 4.56;
    path_delay[8][23] = 4.86;
    path_delay[8][24] = 5.16;
    path_delay[8][25] = 5.46;
    path_delay[8][26] = 5.76;
    path_delay[8][27] = 6.06;
    path_delay[8][28] = 6.36;
    path_delay[8][29] = 6.66;
    path_delay[8][30] = 6.96;
    path_delay[8][31] = 7.26;
    path_delay[8][8] = 0.36;
    path_delay[8][9] = 0.67;
    path_delay[9][32] = 7.04;
    path_delay[9][10] = 0.67;
    path_delay[9][11] = 0.97;
    path_delay[9][12] = 1.27;
    path_delay[9][13] = 1.57;
    path_delay[9][14] = 1.87;
    path_delay[9][15] = 2.17;
    path_delay[9][16] = 2.47;
    path_delay[9][17] = 2.77;
    path_delay[9][18] = 3.07;
    path_delay[9][19] = 3.36;
    path_delay[9][20] = 3.66;
    path_delay[9][21] = 3.96;
    path_delay[9][22] = 4.26;
    path_delay[9][23] = 4.56;
    path_delay[9][24] = 4.86;
    path_delay[9][25] = 5.16;
    path_delay[9][26] = 5.46;
    path_delay[9][27] = 5.76;
    path_delay[9][28] = 6.06;
    path_delay[9][29] = 6.36;
    path_delay[9][30] = 6.66;
    path_delay[9][31] = 6.96;
    path_delay[9][9] = 0.36;
    path_delay[32][32] = 9.52;
    path_delay[32][0] = 0.37;
    path_delay[32][10] = 3.15;
    path_delay[32][11] = 3.45;
    path_delay[32][12] = 3.75;
    path_delay[32][13] = 4.05;
    path_delay[32][14] = 4.35;
    path_delay[32][15] = 4.65;
    path_delay[32][16] = 4.95;
    path_delay[32][17] = 5.25;
    path_delay[32][18] = 5.55;
    path_delay[32][19] = 5.85;
    path_delay[32][1] = 0.46;
    path_delay[32][20] = 6.15;
    path_delay[32][21] = 6.45;
    path_delay[32][22] = 6.75;
    path_delay[32][23] = 7.05;
    path_delay[32][24] = 7.35;
    path_delay[32][25] = 7.65;
    path_delay[32][26] = 7.95;
    path_delay[32][27] = 8.25;
    path_delay[32][28] = 8.55;
    path_delay[32][29] = 8.85;
    path_delay[32][2] = 0.76;
    path_delay[32][30] = 9.14;
    path_delay[32][31] = 9.44;
    path_delay[32][3] = 1.06;
    path_delay[32][4] = 1.36;
    path_delay[32][5] = 1.66;
    path_delay[32][6] = 1.96;
    path_delay[32][7] = 2.26;
    path_delay[32][8] = 2.55;
    path_delay[32][9] = 2.85;
    path_delay[42][32] = 6.75;
    path_delay[42][10] = 0.37;
    path_delay[42][11] = 0.68;
    path_delay[42][12] = 0.98;
    path_delay[42][13] = 1.28;
    path_delay[42][14] = 1.58;
    path_delay[42][15] = 1.88;
    path_delay[42][16] = 2.18;
    path_delay[42][17] = 2.48;
    path_delay[42][18] = 2.78;
    path_delay[42][19] = 3.08;
    path_delay[42][20] = 3.38;
    path_delay[42][21] = 3.68;
    path_delay[42][22] = 3.98;
    path_delay[42][23] = 4.28;
    path_delay[42][24] = 4.58;
    path_delay[42][25] = 4.88;
    path_delay[42][26] = 5.18;
    path_delay[42][27] = 5.48;
    path_delay[42][28] = 5.77;
    path_delay[42][29] = 6.07;
    path_delay[42][30] = 6.37;
    path_delay[42][31] = 6.67;
    path_delay[43][32] = 6.45;
    path_delay[43][11] = 0.37;
    path_delay[43][12] = 0.68;
    path_delay[43][13] = 0.98;
    path_delay[43][14] = 1.28;
    path_delay[43][15] = 1.58;
    path_delay[43][16] = 1.88;
    path_delay[43][17] = 2.18;
    path_delay[43][18] = 2.48;
    path_delay[43][19] = 2.78;
    path_delay[43][20] = 3.08;
    path_delay[43][21] = 3.38;
    path_delay[43][22] = 3.68;
    path_delay[43][23] = 3.98;
    path_delay[43][24] = 4.28;
    path_delay[43][25] = 4.58;
    path_delay[43][26] = 4.88;
    path_delay[43][27] = 5.18;
    path_delay[43][28] = 5.48;
    path_delay[43][29] = 5.77;
    path_delay[43][30] = 6.07;
    path_delay[43][31] = 6.37;
    path_delay[44][32] = 6.15;
    path_delay[44][12] = 0.37;
    path_delay[44][13] = 0.68;
    path_delay[44][14] = 0.98;
    path_delay[44][15] = 1.28;
    path_delay[44][16] = 1.58;
    path_delay[44][17] = 1.88;
    path_delay[44][18] = 2.18;
    path_delay[44][19] = 2.48;
    path_delay[44][20] = 2.78;
    path_delay[44][21] = 3.08;
    path_delay[44][22] = 3.38;
    path_delay[44][23] = 3.68;
    path_delay[44][24] = 3.98;
    path_delay[44][25] = 4.28;
    path_delay[44][26] = 4.58;
    path_delay[44][27] = 4.88;
    path_delay[44][28] = 5.18;
    path_delay[44][29] = 5.48;
    path_delay[44][30] = 5.77;
    path_delay[44][31] = 6.07;
    path_delay[45][32] = 5.85;
    path_delay[45][13] = 0.37;
    path_delay[45][14] = 0.68;
    path_delay[45][15] = 0.98;
    path_delay[45][16] = 1.28;
    path_delay[45][17] = 1.58;
    path_delay[45][18] = 1.88;
    path_delay[45][19] = 2.18;
    path_delay[45][20] = 2.48;
    path_delay[45][21] = 2.78;
    path_delay[45][22] = 3.08;
    path_delay[45][23] = 3.38;
    path_delay[45][24] = 3.68;
    path_delay[45][25] = 3.98;
    path_delay[45][26] = 4.28;
    path_delay[45][27] = 4.58;
    path_delay[45][28] = 4.88;
    path_delay[45][29] = 5.18;
    path_delay[45][30] = 5.48;
    path_delay[45][31] = 5.77;
    path_delay[46][32] = 5.55;
    path_delay[46][14] = 0.37;
    path_delay[46][15] = 0.68;
    path_delay[46][16] = 0.98;
    path_delay[46][17] = 1.28;
    path_delay[46][18] = 1.58;
    path_delay[46][19] = 1.88;
    path_delay[46][20] = 2.18;
    path_delay[46][21] = 2.48;
    path_delay[46][22] = 2.78;
    path_delay[46][23] = 3.08;
    path_delay[46][24] = 3.38;
    path_delay[46][25] = 3.68;
    path_delay[46][26] = 3.98;
    path_delay[46][27] = 4.28;
    path_delay[46][28] = 4.58;
    path_delay[46][29] = 4.88;
    path_delay[46][30] = 5.18;
    path_delay[46][31] = 5.48;
    path_delay[47][32] = 5.25;
    path_delay[47][15] = 0.37;
    path_delay[47][16] = 0.68;
    path_delay[47][17] = 0.98;
    path_delay[47][18] = 1.28;
    path_delay[47][19] = 1.58;
    path_delay[47][20] = 1.88;
    path_delay[47][21] = 2.18;
    path_delay[47][22] = 2.48;
    path_delay[47][23] = 2.78;
    path_delay[47][24] = 3.08;
    path_delay[47][25] = 3.38;
    path_delay[47][26] = 3.68;
    path_delay[47][27] = 3.98;
    path_delay[47][28] = 4.28;
    path_delay[47][29] = 4.58;
    path_delay[47][30] = 4.88;
    path_delay[47][31] = 5.18;
    path_delay[48][32] = 4.95;
    path_delay[48][16] = 0.37;
    path_delay[48][17] = 0.68;
    path_delay[48][18] = 0.98;
    path_delay[48][19] = 1.28;
    path_delay[48][20] = 1.58;
    path_delay[48][21] = 1.88;
    path_delay[48][22] = 2.18;
    path_delay[48][23] = 2.48;
    path_delay[48][24] = 2.78;
    path_delay[48][25] = 3.08;
    path_delay[48][26] = 3.38;
    path_delay[48][27] = 3.68;
    path_delay[48][28] = 3.98;
    path_delay[48][29] = 4.28;
    path_delay[48][30] = 4.58;
    path_delay[48][31] = 4.88;
    path_delay[49][32] = 4.65;
    path_delay[49][17] = 0.37;
    path_delay[49][18] = 0.68;
    path_delay[49][19] = 0.98;
    path_delay[49][20] = 1.28;
    path_delay[49][21] = 1.58;
    path_delay[49][22] = 1.88;
    path_delay[49][23] = 2.18;
    path_delay[49][24] = 2.48;
    path_delay[49][25] = 2.78;
    path_delay[49][26] = 3.08;
    path_delay[49][27] = 3.38;
    path_delay[49][28] = 3.68;
    path_delay[49][29] = 3.98;
    path_delay[49][30] = 4.28;
    path_delay[49][31] = 4.58;
    path_delay[50][32] = 4.35;
    path_delay[50][18] = 0.37;
    path_delay[50][19] = 0.68;
    path_delay[50][20] = 0.98;
    path_delay[50][21] = 1.28;
    path_delay[50][22] = 1.58;
    path_delay[50][23] = 1.88;
    path_delay[50][24] = 2.18;
    path_delay[50][25] = 2.48;
    path_delay[50][26] = 2.78;
    path_delay[50][27] = 3.08;
    path_delay[50][28] = 3.38;
    path_delay[50][29] = 3.68;
    path_delay[50][30] = 3.98;
    path_delay[50][31] = 4.28;
    path_delay[51][32] = 4.05;
    path_delay[51][19] = 0.37;
    path_delay[51][20] = 0.68;
    path_delay[51][21] = 0.98;
    path_delay[51][22] = 1.28;
    path_delay[51][23] = 1.58;
    path_delay[51][24] = 1.88;
    path_delay[51][25] = 2.18;
    path_delay[51][26] = 2.48;
    path_delay[51][27] = 2.78;
    path_delay[51][28] = 3.08;
    path_delay[51][29] = 3.38;
    path_delay[51][30] = 3.68;
    path_delay[51][31] = 3.98;
    path_delay[33][32] = 9.45;
    path_delay[33][10] = 3.08;
    path_delay[33][11] = 3.38;
    path_delay[33][12] = 3.68;
    path_delay[33][13] = 3.98;
    path_delay[33][14] = 4.28;
    path_delay[33][15] = 4.58;
    path_delay[33][16] = 4.88;
    path_delay[33][17] = 5.18;
    path_delay[33][18] = 5.48;
    path_delay[33][19] = 5.77;
    path_delay[33][1] = 0.37;
    path_delay[33][20] = 6.07;
    path_delay[33][21] = 6.37;
    path_delay[33][22] = 6.67;
    path_delay[33][23] = 6.97;
    path_delay[33][24] = 7.27;
    path_delay[33][25] = 7.57;
    path_delay[33][26] = 7.87;
    path_delay[33][27] = 8.17;
    path_delay[33][28] = 8.47;
    path_delay[33][29] = 8.77;
    path_delay[33][2] = 0.68;
    path_delay[33][30] = 9.07;
    path_delay[33][31] = 9.37;
    path_delay[33][3] = 0.98;
    path_delay[33][4] = 1.28;
    path_delay[33][5] = 1.58;
    path_delay[33][6] = 1.88;
    path_delay[33][7] = 2.18;
    path_delay[33][8] = 2.48;
    path_delay[33][9] = 2.78;
    path_delay[52][32] = 3.76;
    path_delay[52][20] = 0.37;
    path_delay[52][21] = 0.68;
    path_delay[52][22] = 0.98;
    path_delay[52][23] = 1.28;
    path_delay[52][24] = 1.58;
    path_delay[52][25] = 1.88;
    path_delay[52][26] = 2.18;
    path_delay[52][27] = 2.48;
    path_delay[52][28] = 2.78;
    path_delay[52][29] = 3.08;
    path_delay[52][30] = 3.38;
    path_delay[52][31] = 3.68;
    path_delay[53][32] = 3.46;
    path_delay[53][21] = 0.37;
    path_delay[53][22] = 0.68;
    path_delay[53][23] = 0.98;
    path_delay[53][24] = 1.28;
    path_delay[53][25] = 1.58;
    path_delay[53][26] = 1.88;
    path_delay[53][27] = 2.18;
    path_delay[53][28] = 2.48;
    path_delay[53][29] = 2.78;
    path_delay[53][30] = 3.08;
    path_delay[53][31] = 3.38;
    path_delay[54][32] = 3.16;
    path_delay[54][22] = 0.37;
    path_delay[54][23] = 0.68;
    path_delay[54][24] = 0.98;
    path_delay[54][25] = 1.28;
    path_delay[54][26] = 1.58;
    path_delay[54][27] = 1.88;
    path_delay[54][28] = 2.18;
    path_delay[54][29] = 2.48;
    path_delay[54][30] = 2.78;
    path_delay[54][31] = 3.08;
    path_delay[55][32] = 2.86;
    path_delay[55][23] = 0.37;
    path_delay[55][24] = 0.68;
    path_delay[55][25] = 0.98;
    path_delay[55][26] = 1.28;
    path_delay[55][27] = 1.58;
    path_delay[55][28] = 1.88;
    path_delay[55][29] = 2.18;
    path_delay[55][30] = 2.48;
    path_delay[55][31] = 2.78;
    path_delay[56][32] = 2.56;
    path_delay[56][24] = 0.37;
    path_delay[56][25] = 0.68;
    path_delay[56][26] = 0.98;
    path_delay[56][27] = 1.28;
    path_delay[56][28] = 1.58;
    path_delay[56][29] = 1.88;
    path_delay[56][30] = 2.18;
    path_delay[56][31] = 2.48;
    path_delay[57][32] = 2.26;
    path_delay[57][25] = 0.37;
    path_delay[57][26] = 0.68;
    path_delay[57][27] = 0.98;
    path_delay[57][28] = 1.28;
    path_delay[57][29] = 1.58;
    path_delay[57][30] = 1.88;
    path_delay[57][31] = 2.18;
    path_delay[58][32] = 1.96;
    path_delay[58][26] = 0.37;
    path_delay[58][27] = 0.68;
    path_delay[58][28] = 0.98;
    path_delay[58][29] = 1.28;
    path_delay[58][30] = 1.58;
    path_delay[58][31] = 1.88;
    path_delay[59][32] = 1.66;
    path_delay[59][27] = 0.37;
    path_delay[59][28] = 0.68;
    path_delay[59][29] = 0.98;
    path_delay[59][30] = 1.28;
    path_delay[59][31] = 1.58;
    path_delay[60][32] = 1.36;
    path_delay[60][28] = 0.37;
    path_delay[60][29] = 0.68;
    path_delay[60][30] = 0.98;
    path_delay[60][31] = 1.28;
    path_delay[61][32] = 1.06;
    path_delay[61][29] = 0.37;
    path_delay[61][30] = 0.68;
    path_delay[61][31] = 0.98;
    path_delay[34][32] = 9.15;
    path_delay[34][10] = 2.78;
    path_delay[34][11] = 3.08;
    path_delay[34][12] = 3.38;
    path_delay[34][13] = 3.68;
    path_delay[34][14] = 3.98;
    path_delay[34][15] = 4.28;
    path_delay[34][16] = 4.58;
    path_delay[34][17] = 4.88;
    path_delay[34][18] = 5.18;
    path_delay[34][19] = 5.48;
    path_delay[34][20] = 5.77;
    path_delay[34][21] = 6.07;
    path_delay[34][22] = 6.37;
    path_delay[34][23] = 6.67;
    path_delay[34][24] = 6.97;
    path_delay[34][25] = 7.27;
    path_delay[34][26] = 7.57;
    path_delay[34][27] = 7.87;
    path_delay[34][28] = 8.17;
    path_delay[34][29] = 8.47;
    path_delay[34][2] = 0.37;
    path_delay[34][30] = 8.77;
    path_delay[34][31] = 9.07;
    path_delay[34][3] = 0.68;
    path_delay[34][4] = 0.98;
    path_delay[34][5] = 1.28;
    path_delay[34][6] = 1.58;
    path_delay[34][7] = 1.88;
    path_delay[34][8] = 2.18;
    path_delay[34][9] = 2.48;
    path_delay[62][32] = 0.76;
    path_delay[62][30] = 0.37;
    path_delay[62][31] = 0.68;
    path_delay[63][32] = 0.46;
    path_delay[63][31] = 0.37;
    path_delay[35][32] = 8.85;
    path_delay[35][10] = 2.48;
    path_delay[35][11] = 2.78;
    path_delay[35][12] = 3.08;
    path_delay[35][13] = 3.38;
    path_delay[35][14] = 3.68;
    path_delay[35][15] = 3.98;
    path_delay[35][16] = 4.28;
    path_delay[35][17] = 4.58;
    path_delay[35][18] = 4.88;
    path_delay[35][19] = 5.18;
    path_delay[35][20] = 5.48;
    path_delay[35][21] = 5.77;
    path_delay[35][22] = 6.07;
    path_delay[35][23] = 6.37;
    path_delay[35][24] = 6.67;
    path_delay[35][25] = 6.97;
    path_delay[35][26] = 7.27;
    path_delay[35][27] = 7.57;
    path_delay[35][28] = 7.87;
    path_delay[35][29] = 8.17;
    path_delay[35][30] = 8.47;
    path_delay[35][31] = 8.77;
    path_delay[35][3] = 0.37;
    path_delay[35][4] = 0.68;
    path_delay[35][5] = 0.98;
    path_delay[35][6] = 1.28;
    path_delay[35][7] = 1.58;
    path_delay[35][8] = 1.88;
    path_delay[35][9] = 2.18;
    path_delay[36][32] = 8.55;
    path_delay[36][10] = 2.18;
    path_delay[36][11] = 2.48;
    path_delay[36][12] = 2.78;
    path_delay[36][13] = 3.08;
    path_delay[36][14] = 3.38;
    path_delay[36][15] = 3.68;
    path_delay[36][16] = 3.98;
    path_delay[36][17] = 4.28;
    path_delay[36][18] = 4.58;
    path_delay[36][19] = 4.88;
    path_delay[36][20] = 5.18;
    path_delay[36][21] = 5.48;
    path_delay[36][22] = 5.77;
    path_delay[36][23] = 6.07;
    path_delay[36][24] = 6.37;
    path_delay[36][25] = 6.67;
    path_delay[36][26] = 6.97;
    path_delay[36][27] = 7.27;
    path_delay[36][28] = 7.57;
    path_delay[36][29] = 7.87;
    path_delay[36][30] = 8.17;
    path_delay[36][31] = 8.47;
    path_delay[36][4] = 0.37;
    path_delay[36][5] = 0.68;
    path_delay[36][6] = 0.98;
    path_delay[36][7] = 1.28;
    path_delay[36][8] = 1.58;
    path_delay[36][9] = 1.88;
    path_delay[37][32] = 8.25;
    path_delay[37][10] = 1.88;
    path_delay[37][11] = 2.18;
    path_delay[37][12] = 2.48;
    path_delay[37][13] = 2.78;
    path_delay[37][14] = 3.08;
    path_delay[37][15] = 3.38;
    path_delay[37][16] = 3.68;
    path_delay[37][17] = 3.98;
    path_delay[37][18] = 4.28;
    path_delay[37][19] = 4.58;
    path_delay[37][20] = 4.88;
    path_delay[37][21] = 5.18;
    path_delay[37][22] = 5.48;
    path_delay[37][23] = 5.77;
    path_delay[37][24] = 6.07;
    path_delay[37][25] = 6.37;
    path_delay[37][26] = 6.67;
    path_delay[37][27] = 6.97;
    path_delay[37][28] = 7.27;
    path_delay[37][29] = 7.57;
    path_delay[37][30] = 7.87;
    path_delay[37][31] = 8.17;
    path_delay[37][5] = 0.37;
    path_delay[37][6] = 0.68;
    path_delay[37][7] = 0.98;
    path_delay[37][8] = 1.28;
    path_delay[37][9] = 1.58;
    path_delay[38][32] = 7.95;
    path_delay[38][10] = 1.58;
    path_delay[38][11] = 1.88;
    path_delay[38][12] = 2.18;
    path_delay[38][13] = 2.48;
    path_delay[38][14] = 2.78;
    path_delay[38][15] = 3.08;
    path_delay[38][16] = 3.38;
    path_delay[38][17] = 3.68;
    path_delay[38][18] = 3.98;
    path_delay[38][19] = 4.28;
    path_delay[38][20] = 4.58;
    path_delay[38][21] = 4.88;
    path_delay[38][22] = 5.18;
    path_delay[38][23] = 5.48;
    path_delay[38][24] = 5.77;
    path_delay[38][25] = 6.07;
    path_delay[38][26] = 6.37;
    path_delay[38][27] = 6.67;
    path_delay[38][28] = 6.97;
    path_delay[38][29] = 7.27;
    path_delay[38][30] = 7.57;
    path_delay[38][31] = 7.87;
    path_delay[38][6] = 0.37;
    path_delay[38][7] = 0.68;
    path_delay[38][8] = 0.98;
    path_delay[38][9] = 1.28;
    path_delay[39][32] = 7.65;
    path_delay[39][10] = 1.28;
    path_delay[39][11] = 1.58;
    path_delay[39][12] = 1.88;
    path_delay[39][13] = 2.18;
    path_delay[39][14] = 2.48;
    path_delay[39][15] = 2.78;
    path_delay[39][16] = 3.08;
    path_delay[39][17] = 3.38;
    path_delay[39][18] = 3.68;
    path_delay[39][19] = 3.98;
    path_delay[39][20] = 4.28;
    path_delay[39][21] = 4.58;
    path_delay[39][22] = 4.88;
    path_delay[39][23] = 5.18;
    path_delay[39][24] = 5.48;
    path_delay[39][25] = 5.77;
    path_delay[39][26] = 6.07;
    path_delay[39][27] = 6.37;
    path_delay[39][28] = 6.67;
    path_delay[39][29] = 6.97;
    path_delay[39][30] = 7.27;
    path_delay[39][31] = 7.57;
    path_delay[39][7] = 0.37;
    path_delay[39][8] = 0.68;
    path_delay[39][9] = 0.98;
    path_delay[40][32] = 7.35;
    path_delay[40][10] = 0.98;
    path_delay[40][11] = 1.28;
    path_delay[40][12] = 1.58;
    path_delay[40][13] = 1.88;
    path_delay[40][14] = 2.18;
    path_delay[40][15] = 2.48;
    path_delay[40][16] = 2.78;
    path_delay[40][17] = 3.08;
    path_delay[40][18] = 3.38;
    path_delay[40][19] = 3.68;
    path_delay[40][20] = 3.98;
    path_delay[40][21] = 4.28;
    path_delay[40][22] = 4.58;
    path_delay[40][23] = 4.88;
    path_delay[40][24] = 5.18;
    path_delay[40][25] = 5.48;
    path_delay[40][26] = 5.77;
    path_delay[40][27] = 6.07;
    path_delay[40][28] = 6.37;
    path_delay[40][29] = 6.67;
    path_delay[40][30] = 6.97;
    path_delay[40][31] = 7.27;
    path_delay[40][8] = 0.37;
    path_delay[40][9] = 0.68;
    path_delay[41][32] = 7.05;
    path_delay[41][10] = 0.68;
    path_delay[41][11] = 0.98;
    path_delay[41][12] = 1.28;
    path_delay[41][13] = 1.58;
    path_delay[41][14] = 1.88;
    path_delay[41][15] = 2.18;
    path_delay[41][16] = 2.48;
    path_delay[41][17] = 2.78;
    path_delay[41][18] = 3.08;
    path_delay[41][19] = 3.38;
    path_delay[41][20] = 3.68;
    path_delay[41][21] = 3.98;
    path_delay[41][22] = 4.28;
    path_delay[41][23] = 4.58;
    path_delay[41][24] = 4.88;
    path_delay[41][25] = 5.18;
    path_delay[41][26] = 5.48;
    path_delay[41][27] = 5.77;
    path_delay[41][28] = 6.07;
    path_delay[41][29] = 6.37;
    path_delay[41][30] = 6.67;
    path_delay[41][31] = 6.97;
    path_delay[41][9] = 0.37;
}

// ============================================================================
// 4. TESTBENCH -- Monte Carlo, per-sample EDP
// ============================================================================
SC_MODULE(Testbench) {
    sc_out<sc_lv<32>> A, B;
    sc_in<sc_lv<32>> Sum;
    sc_in<bool> Cout;

    RippleCarryAdder32* rca_ptr;

    unsigned int number_of_errors;

    unsigned long long num_samples;
    unsigned long long rng_seed;

    double sum_switches, sum_delay, sum_edp;

    unsigned int prev_a, prev_b, prev_sum;
    bool prev_cout;

    SC_CTOR(Testbench)
        : rca_ptr(nullptr),
          number_of_errors(0),
          num_samples(1000000ULL),
          rng_seed(42ULL),
          sum_switches(0.0), sum_delay(0.0), sum_edp(0.0),
          prev_a(0), prev_b(0), prev_sum(0), prev_cout(false)
    {
        SC_THREAD(stimulus);
    }

    void stimulus() {
        std::mt19937_64 rng(rng_seed);
        std::uniform_int_distribution<unsigned long long> dist(0ULL, 4294967295ULL);

        A.write(make_32bit_vector(0));
        B.write(make_32bit_vector(0));
        wait(200, SC_NS);
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
        rca_ptr->reset_switches();

        A.write(make_32bit_vector(a_value));
        B.write(make_32bit_vector(b_value));

        wait(200, SC_NS);

        unsigned int expected_result = a_value + b_value;
        unsigned int actual_sum = to_unsigned_32bit(Sum.read());
        bool actual_cout = Cout.read();
        unsigned int actual_result = actual_sum + (actual_cout ? 4294967296U : 0U);

        if (expected_result != actual_result) {
            number_of_errors++;
        }

        unsigned int switches_i = rca_ptr->total_switches();

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

// ============================================================================
// 5. MAIN -- Monte Carlo, auto-parallel across all CPU cores
// ============================================================================
struct WorkerResult {
    double sum_switches;
    double sum_delay;
    double sum_edp;
    unsigned long long errors;
};

static void run_slice(unsigned long long samples_for_this_worker, unsigned long long seed_for_this_worker,
                       double& out_sum_switches, double& out_sum_delay, double& out_sum_edp,
                       unsigned int& out_errors) {
    sc_signal<sc_lv<32>> A, B, Sum;
    sc_signal<bool> Cout;

    RippleCarryAdder32 rca("RCA32");
    Testbench tb("TB");
    tb.rca_ptr = &rca;

    rca.A(A); rca.B(B); rca.Sum(Sum); rca.Cout(Cout);
    tb.A(A); tb.B(B); tb.Sum(Sum); tb.Cout(Cout);

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

    std::cout << "RCA-32-MC-pathEDP: SAMPLES=" << total_samples << " ERRORS=" << total_errors
              << " AVG_SWITCHES=" << avg_switches
              << " AVG_DELAY_ns=" << avg_delay
              << " AVG_EDP_correct=" << avg_edp_correct
              << " AVG_EDP_naive=" << avg_edp_naive
              << " pct_diff=" << ((avg_edp_naive / avg_edp_correct - 1.0) * 100.0) << "%\n";

    return 0;
}
