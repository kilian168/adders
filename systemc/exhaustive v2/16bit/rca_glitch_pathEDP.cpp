// main.cpp
#include <systemc.h>
#include <iostream>
#include <iomanip>
#include <string>
#include <cassert>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <vector>
#include <sys/wait.h>
#include <unistd.h>

// ---- exhaustive range mapping (v2): index i -> bit pattern of value (OFFSET + i) ----
static const unsigned long long EXH_COUNT = 65536ULL;
static const unsigned long long EXH_OFFSET = 32768ULL;
static const unsigned long long EXH_MASK = 65535ULL;
static const unsigned int EXH_TOP = 32768U;
static const unsigned int EXH_LOW = 32767U;
static inline unsigned int exh_map(unsigned long long i) { return static_cast<unsigned int>((i + EXH_OFFSET) & EXH_MASK); }

// ==========================================
// Helper functions
// ==========================================
static sc_lv<16> make_16bit_vector(unsigned int value) {
    sc_lv<16> bits;
    for (int i = 0; i < 16; i++) {
        bits[i] = ((value >> i) & 1U) != 0U;
    }
    return bits;
}

static unsigned int to_unsigned_16bit(const sc_lv<16>& bits) {
    unsigned int value = 0;
    for (int i = 0; i < 16; i++) {
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
    uint64_t switches;

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

    uint64_t total_switches() const {
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
// 2. 16-BIT RIPPLE CARRY ADDER
// ==========================================
SC_MODULE(RippleCarryAdder16) {
    sc_in<sc_lv<16>> A;
    sc_in<sc_lv<16>> B;

    sc_out<sc_lv<16>> Sum;
    sc_out<bool> Cout;

    sc_signal<bool> a[16];
    sc_signal<bool> b[16];
    sc_signal<bool> s[16];
    sc_signal<bool> c[16];
    sc_signal<bool> const_zero;

    FullAdder* fa[16];

    SC_CTOR(RippleCarryAdder16) {
        for (int i = 0; i < 16; i++) {
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
        for (int i = 0; i < 16; i++) {
            sensitive << s[i];
        }
        sensitive << c[15];
    }

    void split_inputs() {
        sc_lv<16> value_a = A.read();
        sc_lv<16> value_b = B.read();

        for (int i = 0; i < 16; i++) {
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
        sc_lv<16> value_sum;

        for (int i = 0; i < 16; i++) {
            value_sum[i] = s[i].read();
        }

        Sum.write(value_sum);
        Cout.write(c[15].read());
    }

    uint64_t total_switches() const {
        uint64_t total = 0;
        for (int i = 0; i < 16; i++) {
            total += fa[i]->total_switches();
        }
        return total;
    }

    void reset_switches() {
        for (int i = 0; i < 16; i++) fa[i]->reset_switches();
    }

    void print_report() {
        std::cout << "RCA-16: GATES=80 SWITCHES=" << total_switches() << " DELAY=32ns\n";
    }

    ~RippleCarryAdder16() {
        for (int i = 0; i < 16; i++) {
            delete fa[i];
        }
    }
};

// ==========================================
// 3. TESTBENCH
// ==========================================

// ============================================================================
// Real OpenSTA (Nangate45) max delay per (startpoint pin, endpoint pin),
// extracted from all_critical_paths.csv for RCA-16 (304 pairs).
// ============================================================================
static const int NUM_IN = 32;
static const int NUM_OUT = 17;
static double path_delay[NUM_IN][NUM_OUT];

static void init_path_delay_table() {
    for (int i = 0; i < NUM_IN; i++)
        for (int o = 0; o < NUM_OUT; o++)
            path_delay[i][o] = 0.0;

    path_delay[0][16] = 4.72;
    path_delay[0][0] = 0.36;
    path_delay[0][10] = 3.15;
    path_delay[0][11] = 3.45;
    path_delay[0][12] = 3.74;
    path_delay[0][13] = 4.04;
    path_delay[0][14] = 4.34;
    path_delay[0][15] = 4.64;
    path_delay[0][1] = 0.45;
    path_delay[0][2] = 0.75;
    path_delay[0][3] = 1.05;
    path_delay[0][4] = 1.35;
    path_delay[0][5] = 1.65;
    path_delay[0][6] = 1.95;
    path_delay[0][7] = 2.25;
    path_delay[0][8] = 2.55;
    path_delay[0][9] = 2.85;
    path_delay[10][16] = 1.94;
    path_delay[10][10] = 0.36;
    path_delay[10][11] = 0.67;
    path_delay[10][12] = 0.97;
    path_delay[10][13] = 1.27;
    path_delay[10][14] = 1.57;
    path_delay[10][15] = 1.87;
    path_delay[11][16] = 1.64;
    path_delay[11][11] = 0.36;
    path_delay[11][12] = 0.67;
    path_delay[11][13] = 0.97;
    path_delay[11][14] = 1.27;
    path_delay[11][15] = 1.57;
    path_delay[12][16] = 1.35;
    path_delay[12][12] = 0.36;
    path_delay[12][13] = 0.67;
    path_delay[12][14] = 0.97;
    path_delay[12][15] = 1.27;
    path_delay[13][16] = 1.05;
    path_delay[13][13] = 0.36;
    path_delay[13][14] = 0.67;
    path_delay[13][15] = 0.97;
    path_delay[14][16] = 0.75;
    path_delay[14][14] = 0.36;
    path_delay[14][15] = 0.67;
    path_delay[15][16] = 0.45;
    path_delay[15][15] = 0.36;
    path_delay[1][16] = 4.64;
    path_delay[1][10] = 3.07;
    path_delay[1][11] = 3.36;
    path_delay[1][12] = 3.66;
    path_delay[1][13] = 3.96;
    path_delay[1][14] = 4.26;
    path_delay[1][15] = 4.56;
    path_delay[1][1] = 0.36;
    path_delay[1][2] = 0.67;
    path_delay[1][3] = 0.97;
    path_delay[1][4] = 1.27;
    path_delay[1][5] = 1.57;
    path_delay[1][6] = 1.87;
    path_delay[1][7] = 2.17;
    path_delay[1][8] = 2.47;
    path_delay[1][9] = 2.77;
    path_delay[2][16] = 4.34;
    path_delay[2][10] = 2.77;
    path_delay[2][11] = 3.07;
    path_delay[2][12] = 3.36;
    path_delay[2][13] = 3.66;
    path_delay[2][14] = 3.96;
    path_delay[2][15] = 4.26;
    path_delay[2][2] = 0.36;
    path_delay[2][3] = 0.67;
    path_delay[2][4] = 0.97;
    path_delay[2][5] = 1.27;
    path_delay[2][6] = 1.57;
    path_delay[2][7] = 1.87;
    path_delay[2][8] = 2.17;
    path_delay[2][9] = 2.47;
    path_delay[3][16] = 4.04;
    path_delay[3][10] = 2.47;
    path_delay[3][11] = 2.77;
    path_delay[3][12] = 3.07;
    path_delay[3][13] = 3.36;
    path_delay[3][14] = 3.66;
    path_delay[3][15] = 3.96;
    path_delay[3][3] = 0.36;
    path_delay[3][4] = 0.67;
    path_delay[3][5] = 0.97;
    path_delay[3][6] = 1.27;
    path_delay[3][7] = 1.57;
    path_delay[3][8] = 1.87;
    path_delay[3][9] = 2.17;
    path_delay[4][16] = 3.74;
    path_delay[4][10] = 2.17;
    path_delay[4][11] = 2.47;
    path_delay[4][12] = 2.77;
    path_delay[4][13] = 3.07;
    path_delay[4][14] = 3.36;
    path_delay[4][15] = 3.66;
    path_delay[4][4] = 0.36;
    path_delay[4][5] = 0.67;
    path_delay[4][6] = 0.97;
    path_delay[4][7] = 1.27;
    path_delay[4][8] = 1.57;
    path_delay[4][9] = 1.87;
    path_delay[5][16] = 3.44;
    path_delay[5][10] = 1.87;
    path_delay[5][11] = 2.17;
    path_delay[5][12] = 2.47;
    path_delay[5][13] = 2.77;
    path_delay[5][14] = 3.07;
    path_delay[5][15] = 3.36;
    path_delay[5][5] = 0.36;
    path_delay[5][6] = 0.67;
    path_delay[5][7] = 0.97;
    path_delay[5][8] = 1.27;
    path_delay[5][9] = 1.57;
    path_delay[6][16] = 3.14;
    path_delay[6][10] = 1.57;
    path_delay[6][11] = 1.87;
    path_delay[6][12] = 2.17;
    path_delay[6][13] = 2.47;
    path_delay[6][14] = 2.77;
    path_delay[6][15] = 3.07;
    path_delay[6][6] = 0.36;
    path_delay[6][7] = 0.67;
    path_delay[6][8] = 0.97;
    path_delay[6][9] = 1.27;
    path_delay[7][16] = 2.84;
    path_delay[7][10] = 1.27;
    path_delay[7][11] = 1.57;
    path_delay[7][12] = 1.87;
    path_delay[7][13] = 2.17;
    path_delay[7][14] = 2.47;
    path_delay[7][15] = 2.77;
    path_delay[7][7] = 0.36;
    path_delay[7][8] = 0.67;
    path_delay[7][9] = 0.97;
    path_delay[8][16] = 2.54;
    path_delay[8][10] = 0.97;
    path_delay[8][11] = 1.27;
    path_delay[8][12] = 1.57;
    path_delay[8][13] = 1.87;
    path_delay[8][14] = 2.17;
    path_delay[8][15] = 2.47;
    path_delay[8][8] = 0.36;
    path_delay[8][9] = 0.67;
    path_delay[9][16] = 2.24;
    path_delay[9][10] = 0.67;
    path_delay[9][11] = 0.97;
    path_delay[9][12] = 1.27;
    path_delay[9][13] = 1.57;
    path_delay[9][14] = 1.87;
    path_delay[9][15] = 2.17;
    path_delay[9][9] = 0.36;
    path_delay[16][16] = 4.73;
    path_delay[16][0] = 0.37;
    path_delay[16][10] = 3.15;
    path_delay[16][11] = 3.45;
    path_delay[16][12] = 3.75;
    path_delay[16][13] = 4.05;
    path_delay[16][14] = 4.35;
    path_delay[16][15] = 4.65;
    path_delay[16][1] = 0.46;
    path_delay[16][2] = 0.76;
    path_delay[16][3] = 1.06;
    path_delay[16][4] = 1.36;
    path_delay[16][5] = 1.66;
    path_delay[16][6] = 1.96;
    path_delay[16][7] = 2.26;
    path_delay[16][8] = 2.55;
    path_delay[16][9] = 2.85;
    path_delay[26][16] = 1.96;
    path_delay[26][10] = 0.37;
    path_delay[26][11] = 0.68;
    path_delay[26][12] = 0.98;
    path_delay[26][13] = 1.28;
    path_delay[26][14] = 1.58;
    path_delay[26][15] = 1.88;
    path_delay[27][16] = 1.66;
    path_delay[27][11] = 0.37;
    path_delay[27][12] = 0.68;
    path_delay[27][13] = 0.98;
    path_delay[27][14] = 1.28;
    path_delay[27][15] = 1.58;
    path_delay[28][16] = 1.36;
    path_delay[28][12] = 0.37;
    path_delay[28][13] = 0.68;
    path_delay[28][14] = 0.98;
    path_delay[28][15] = 1.28;
    path_delay[29][16] = 1.06;
    path_delay[29][13] = 0.37;
    path_delay[29][14] = 0.68;
    path_delay[29][15] = 0.98;
    path_delay[30][16] = 0.76;
    path_delay[30][14] = 0.37;
    path_delay[30][15] = 0.68;
    path_delay[31][16] = 0.46;
    path_delay[31][15] = 0.37;
    path_delay[17][16] = 4.65;
    path_delay[17][10] = 3.08;
    path_delay[17][11] = 3.38;
    path_delay[17][12] = 3.68;
    path_delay[17][13] = 3.98;
    path_delay[17][14] = 4.28;
    path_delay[17][15] = 4.58;
    path_delay[17][1] = 0.37;
    path_delay[17][2] = 0.68;
    path_delay[17][3] = 0.98;
    path_delay[17][4] = 1.28;
    path_delay[17][5] = 1.58;
    path_delay[17][6] = 1.88;
    path_delay[17][7] = 2.18;
    path_delay[17][8] = 2.48;
    path_delay[17][9] = 2.78;
    path_delay[18][16] = 4.35;
    path_delay[18][10] = 2.78;
    path_delay[18][11] = 3.08;
    path_delay[18][12] = 3.38;
    path_delay[18][13] = 3.68;
    path_delay[18][14] = 3.98;
    path_delay[18][15] = 4.28;
    path_delay[18][2] = 0.37;
    path_delay[18][3] = 0.68;
    path_delay[18][4] = 0.98;
    path_delay[18][5] = 1.28;
    path_delay[18][6] = 1.58;
    path_delay[18][7] = 1.88;
    path_delay[18][8] = 2.18;
    path_delay[18][9] = 2.48;
    path_delay[19][16] = 4.05;
    path_delay[19][10] = 2.48;
    path_delay[19][11] = 2.78;
    path_delay[19][12] = 3.08;
    path_delay[19][13] = 3.38;
    path_delay[19][14] = 3.68;
    path_delay[19][15] = 3.98;
    path_delay[19][3] = 0.37;
    path_delay[19][4] = 0.68;
    path_delay[19][5] = 0.98;
    path_delay[19][6] = 1.28;
    path_delay[19][7] = 1.58;
    path_delay[19][8] = 1.88;
    path_delay[19][9] = 2.18;
    path_delay[20][16] = 3.76;
    path_delay[20][10] = 2.18;
    path_delay[20][11] = 2.48;
    path_delay[20][12] = 2.78;
    path_delay[20][13] = 3.08;
    path_delay[20][14] = 3.38;
    path_delay[20][15] = 3.68;
    path_delay[20][4] = 0.37;
    path_delay[20][5] = 0.68;
    path_delay[20][6] = 0.98;
    path_delay[20][7] = 1.28;
    path_delay[20][8] = 1.58;
    path_delay[20][9] = 1.88;
    path_delay[21][16] = 3.46;
    path_delay[21][10] = 1.88;
    path_delay[21][11] = 2.18;
    path_delay[21][12] = 2.48;
    path_delay[21][13] = 2.78;
    path_delay[21][14] = 3.08;
    path_delay[21][15] = 3.38;
    path_delay[21][5] = 0.37;
    path_delay[21][6] = 0.68;
    path_delay[21][7] = 0.98;
    path_delay[21][8] = 1.28;
    path_delay[21][9] = 1.58;
    path_delay[22][16] = 3.16;
    path_delay[22][10] = 1.58;
    path_delay[22][11] = 1.88;
    path_delay[22][12] = 2.18;
    path_delay[22][13] = 2.48;
    path_delay[22][14] = 2.78;
    path_delay[22][15] = 3.08;
    path_delay[22][6] = 0.37;
    path_delay[22][7] = 0.68;
    path_delay[22][8] = 0.98;
    path_delay[22][9] = 1.28;
    path_delay[23][16] = 2.86;
    path_delay[23][10] = 1.28;
    path_delay[23][11] = 1.58;
    path_delay[23][12] = 1.88;
    path_delay[23][13] = 2.18;
    path_delay[23][14] = 2.48;
    path_delay[23][15] = 2.78;
    path_delay[23][7] = 0.37;
    path_delay[23][8] = 0.68;
    path_delay[23][9] = 0.98;
    path_delay[24][16] = 2.56;
    path_delay[24][10] = 0.98;
    path_delay[24][11] = 1.28;
    path_delay[24][12] = 1.58;
    path_delay[24][13] = 1.88;
    path_delay[24][14] = 2.18;
    path_delay[24][15] = 2.48;
    path_delay[24][8] = 0.37;
    path_delay[24][9] = 0.68;
    path_delay[25][16] = 2.26;
    path_delay[25][10] = 0.68;
    path_delay[25][11] = 0.98;
    path_delay[25][12] = 1.28;
    path_delay[25][13] = 1.58;
    path_delay[25][14] = 1.88;
    path_delay[25][15] = 2.18;
    path_delay[25][9] = 0.37;
}

// ============================================================================
// TESTBENCH -- exhaustive 2^N x 2^N sweep, per-vector EDP. Single
// continuous process (preserves cross-vector correlation, matching the
// established exhaustive methodology).
// ============================================================================
SC_MODULE(Testbench) {
    sc_out<sc_lv<16>> A, B;
    sc_in<sc_lv<16>> Sum;
    sc_in<bool> Cout;

    RippleCarryAdder16* rca_ptr;

    unsigned int number_of_errors;
    unsigned int a_start;
    unsigned int a_end;

    double sum_switches, sum_delay, sum_edp;
    unsigned int prev_a, prev_b, prev_sum;
    bool prev_cout;

    SC_CTOR(Testbench)
        : rca_ptr(nullptr), number_of_errors(0), a_start(0), a_end(EXH_COUNT),
          sum_switches(0.0), sum_delay(0.0), sum_edp(0.0),
          prev_a(0), prev_b(0), prev_sum(0), prev_cout(false)
    {
        SC_THREAD(stimulus);
    }

    void stimulus() {
        unsigned int warm_a = (a_start == 0) ? exh_map(0) : exh_map(a_start - 1);
        unsigned int warm_b = (a_start == 0) ? exh_map(0) : exh_map(EXH_COUNT - 1);
        A.write(make_16bit_vector(warm_a));
        B.write(make_16bit_vector(warm_b));
        wait(200, SC_NS);
        prev_a = warm_a; prev_b = warm_b;
        prev_sum = to_unsigned_16bit(Sum.read());
        prev_cout = Cout.read();

        for (unsigned int ia = a_start; ia < a_end; ia++) {
            unsigned int a_value = exh_map(ia);
            for (unsigned int ib = 0; ib < static_cast<unsigned int>(EXH_COUNT); ib++) {
                unsigned int b_value = exh_map(ib);
                apply_test(a_value, b_value);
            }
        }

        assert(number_of_errors == 0);
        sc_stop();
    }

    void apply_test(unsigned int a_value, unsigned int b_value) {
        rca_ptr->reset_switches();

        A.write(make_16bit_vector(a_value));
        B.write(make_16bit_vector(b_value));
        wait(200, SC_NS);

        unsigned int expected_result = a_value + b_value;
        unsigned int actual_sum = to_unsigned_16bit(Sum.read());
        bool actual_cout = Cout.read();
        unsigned int actual_result = actual_sum + (actual_cout ? 65536U : 0U);
        if (expected_result != actual_result) number_of_errors++;

        uint64_t switches_i = rca_ptr->total_switches();

        bool in_changed[NUM_IN];
        for (int k = 0; k < 16; k++) in_changed[k] = (((a_value >> k) & 1U) != ((prev_a >> k) & 1U));
        for (int k = 0; k < 16; k++) in_changed[16 + k] = (((b_value >> k) & 1U) != ((prev_b >> k) & 1U));

        bool out_changed[NUM_OUT];
        for (int k = 0; k < 16; k++) out_changed[k] = (((actual_sum >> k) & 1U) != ((prev_sum >> k) & 1U));
        out_changed[16] = (actual_cout != prev_cout);

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
        sum_switches += static_cast<double>(switches_i);
        sum_delay    += delay_i;
        sum_edp      += edp_i;

        prev_a = a_value;
        prev_b = b_value;
        prev_sum = actual_sum;
        prev_cout = actual_cout;
    }
};

// ==========================================
// MAIN -- single continuous process
// ==========================================
struct WorkerResult {
    double sum_switches;
    double sum_delay;
    double sum_edp;
    unsigned long long errors;
    unsigned long long vectors;
};

static void run_slice(unsigned int a_start, unsigned int a_end,
                       double& out_sum_switches, double& out_sum_delay, double& out_sum_edp,
                       unsigned int& out_errors, unsigned long long& out_vectors) {
    sc_signal<sc_lv<16>> A, B, Sum;
    sc_signal<bool> Cout;

    RippleCarryAdder16 rca("RCA16");
    Testbench tb("TB");
    tb.rca_ptr = &rca;

    rca.A(A); rca.B(B); rca.Sum(Sum); rca.Cout(Cout);
    tb.A(A); tb.B(B); tb.Sum(Sum); tb.Cout(Cout);

    tb.a_start = a_start;
    tb.a_end = a_end;

    sc_start();

    out_sum_switches = tb.sum_switches;
    out_sum_delay = tb.sum_delay;
    out_sum_edp = tb.sum_edp;
    out_errors = tb.number_of_errors;
    out_vectors = static_cast<unsigned long long>(a_end - a_start) * EXH_COUNT;
}

int sc_main(int argc, char* argv[]) {
    init_path_delay_table();

    unsigned int num_workers = std::thread::hardware_concurrency();
    if (num_workers == 0) num_workers = 1;
    num_workers = static_cast<unsigned int>(std::min<unsigned long long>(num_workers, EXH_COUNT));

    std::vector<int> read_fd(num_workers);
    std::vector<pid_t> worker_pid(num_workers);
    unsigned int slice = static_cast<unsigned int>(EXH_COUNT) / num_workers;

    for (unsigned int i = 0; i < num_workers; i++) {
        int fds[2];
        if (pipe(fds) != 0) { perror("pipe"); return 1; }

        pid_t child = fork();
        if (child < 0) { perror("fork"); return 1; }

        if (child == 0) {
            close(fds[0]);
            unsigned int a_start = i * slice;
            unsigned int a_end = (i == num_workers - 1) ? static_cast<unsigned int>(EXH_COUNT) : (i + 1) * slice;

            WorkerResult result{};
            double sw = 0.0, de = 0.0, edp = 0.0;
            unsigned int err = 0;
            unsigned long long vec = 0;
            run_slice(a_start, a_end, sw, de, edp, err, vec);
            result.sum_switches = sw; result.sum_delay = de; result.sum_edp = edp;
            result.errors = err; result.vectors = vec;

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
    unsigned long long total_errors = 0, total_vectors = 0;
    for (unsigned int i = 0; i < num_workers; i++) {
        WorkerResult result{};
        ssize_t got = read(read_fd[i], &result, sizeof(result));
        if (got == static_cast<ssize_t>(sizeof(result))) {
            total_sum_switches += result.sum_switches;
            total_sum_delay    += result.sum_delay;
            total_sum_edp      += result.sum_edp;
            total_errors       += result.errors;
            total_vectors      += result.vectors;
        }
        close(read_fd[i]);
        int status = 0;
        waitpid(worker_pid[i], &status, 0);
    }

    assert(total_errors == 0);

    double avg_switches = total_sum_switches / static_cast<double>(total_vectors);
    double avg_delay    = total_sum_delay    / static_cast<double>(total_vectors);
    double avg_edp_correct = total_sum_edp   / static_cast<double>(total_vectors);
    double avg_edp_naive   = avg_switches * avg_delay;

    std::cout << "RCA-16-EXH-pathEDP: VECTORS=" << total_vectors
              << " AVG_SWITCHES=" << avg_switches
              << " AVG_DELAY_ns=" << avg_delay
              << " AVG_EDP_correct=" << avg_edp_correct
              << " AVG_EDP_naive=" << avg_edp_naive
              << " pct_diff=" << ((avg_edp_naive / avg_edp_correct - 1.0) * 100.0) << "%\n";

    return 0;
}
