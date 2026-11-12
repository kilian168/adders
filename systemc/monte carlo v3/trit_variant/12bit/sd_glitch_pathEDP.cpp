// sd_glitch.cpp -- 12-digit redundant signed-digit ("PPM") adder,
// Monte Carlo random sampling over the genuine digit-space.
// Gate-level port of the reference VHDL (8 chained 1-bit DIG_Add full
// adders for N=4, generalized to N digits here): each operand digit i
// is dual-rail encoded as (rail_a, rail_b) with digit = rail_a+rail_b-1,
// i.e. -1 -> (0,0), 0 -> (0,1) [or equivalently (1,0)], +1 -> (1,1).
// Place values are binary (2^i), not base-3 -- confirmed against the
// reference schematic. Each sample draws every digit of A and B
// independently and uniformly from {-1,0,+1}, so exhaustive-infeasible
// widths (12, 16, ...) still get genuine coverage of the redundant
// input space the circuit actually sees.
#include <systemc.h>
#include <iostream>
#include <iomanip>
#include <string>
#include <cassert>
#include <cstdint>
#include <algorithm>
#include <random>
#include <thread>
#include <vector>
#include <sys/wait.h>
#include <unistd.h>

// ============================================================================
// 0. HELPER STRUCTS AND FUNCTIONS
// ============================================================================
struct DigitRails12 {
    sc_lv<12> rail_a;
    sc_lv<12> rail_b;
};

// Encodes an array of N signed digits (each -1, 0, or +1) into dual rails,
// one canonical encoding per digit value: -1->(0,0), 0->(0,1), +1->(1,1).
static DigitRails12 encode_digits_12(const int digits[12]) {
    DigitRails12 encoded;
    encoded.rail_a = "000000000000";
    encoded.rail_b = "000000000000";
    for (int i = 0; i < 12; i++) {
        int d = digits[i];
        encoded.rail_a[i] = (d == 1);
        encoded.rail_b[i] = (d != -1);
    }
    return encoded;
}

// Numeric value of a digit pattern at binary place values (2^i).
static long long digits_value_12(const int digits[12]) {
    long long value = 0;
    for (int i = 0; i < 12; i++) value += static_cast<long long>(digits[i]) * (1LL << i);
    return value;
}

// Plain binary expansion of an unsigned value into N signed digits
// (each 0 or +1, never -1) -- the dropped-NAF/SD-BIN encoding, matching
// RCA/CLA's unsigned 0..2^N-1 value range exactly.
static void binary_digits(unsigned long long v, int digits[12]) {
    for (int i = 0; i < 12; i++) digits[i] = static_cast<int>((v >> i) & 1ULL);
}

// Decodes the (N+1)-digit dual-rail result: each (S_plus[i], S_minus[i])
// pair is a digit at binary place value 2^i, coded 00=-1, 11=+1, 01/10=0
// (same coding as the operand rails).
static long long decode_dual_rail_13(const sc_lv<13>& s_plus, const sc_lv<13>& s_minus) {
    long long value = 0;
    for (int i = 0; i < 13; i++) {
        bool p = s_plus[i].is_01() && s_plus[i].to_bool();
        bool m = s_minus[i].is_01() && s_minus[i].to_bool();
        int digit = (p && m) ? 1 : ((!p && !m) ? -1 : 0);
        value += static_cast<long long>(digit) * (1LL << i);
    }
    return value;
}

// ============================================================================
// 1. GENERIC GATE-LEVEL PRIMITIVES
//    Each instance is exactly one logic gate, independently sensitive to
//    each of its inputs (so inputs arriving at different simulated times
//    each trigger their own evaluation and are counted as separate
//    switches), with a 1ns delay between evaluation and output commit.
// ============================================================================
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

    void reset_counters() {
        calls = 0;
        switches = 0;
    }
};

SC_MODULE(Gate3Or) {
    sc_in<bool> in1, in2, in3;
    sc_out<bool> out;

    sc_event ev_commit;
    bool old_value;
    bool pending_value;
    unsigned int calls;
    uint64_t switches;

    SC_CTOR(Gate3Or) : old_value(false), pending_value(false), calls(0), switches(0) {
        SC_METHOD(eval);
        dont_initialize();
        sensitive << in1 << in2 << in3;

        SC_METHOD(commit);
        dont_initialize();
        sensitive << ev_commit;
    }

    void eval() {
        calls++;
        bool next = in1.read() | in2.read() | in3.read();
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

    void reset_counters() {
        calls = 0;
        switches = 0;
    }
};

// ============================================================================
// 2. GATE-LEVEL BINARY FULL ADDER (6 gates) -- one instance == one DIG_Add
//    xor1 = XOR(a,b)        and1 = AND(a,b)
//    and2 = AND(a,c)        and3 = AND(b,c)
//    sum  = XOR(xor1,c)     carry = OR3(and1,and2,and3)
// ============================================================================
SC_MODULE(BsdBinaryFullAdder) {
    sc_in<bool> input_a;
    sc_in<bool> input_b;
    sc_in<bool> input_c;

    sc_out<bool> sum_out;
    sc_out<bool> carry_out;

    sc_signal<bool> xor1_sig, and1_sig, and2_sig, and3_sig;

    Gate *g_xor1, *g_and1, *g_and2, *g_and3, *g_xor2;
    Gate3Or *g_or1;

    SC_CTOR(BsdBinaryFullAdder) {
        g_xor1 = new Gate("XOR1", GATE_XOR);
        g_xor1->in1(input_a); g_xor1->in2(input_b); g_xor1->out(xor1_sig);

        g_and1 = new Gate("AND1", GATE_AND);
        g_and1->in1(input_a); g_and1->in2(input_b); g_and1->out(and1_sig);

        g_and2 = new Gate("AND2", GATE_AND);
        g_and2->in1(input_a); g_and2->in2(input_c); g_and2->out(and2_sig);

        g_and3 = new Gate("AND3", GATE_AND);
        g_and3->in1(input_b); g_and3->in2(input_c); g_and3->out(and3_sig);

        g_xor2 = new Gate("XOR2", GATE_XOR);
        g_xor2->in1(xor1_sig); g_xor2->in2(input_c); g_xor2->out(sum_out);

        g_or1 = new Gate3Or("OR1");
        g_or1->in1(and1_sig); g_or1->in2(and2_sig); g_or1->in3(and3_sig); g_or1->out(carry_out);
    }

    uint64_t total_switches() const {
        return g_xor1->switches + g_and1->switches + g_and2->switches +
               g_and3->switches + g_xor2->switches + g_or1->switches;
    }

    void reset_counters() {
        g_xor1->reset_counters();
        g_and1->reset_counters();
        g_and2->reset_counters();
        g_and3->reset_counters();
        g_xor2->reset_counters();
        g_or1->reset_counters();
    }

    ~BsdBinaryFullAdder() {
        delete g_xor1; delete g_and1; delete g_and2;
        delete g_and3; delete g_xor2; delete g_or1;
    }
};

// ============================================================================
// 2. 12-DIGIT SIGNED-DIGIT ("PPM") ADDER -- exact port of the reference VHDL
// ============================================================================
SC_MODULE(SignedDigitAdder12) {
    sc_in<sc_lv<12>> A_a;
    sc_in<sc_lv<12>> A_b;
    sc_in<sc_lv<12>> B_a;
    sc_in<sc_lv<12>> B_b;

    sc_out<sc_lv<13>> S_minus;
    sc_out<sc_lv<13>> S_plus;

    sc_signal<bool> sa_a[12];
    sc_signal<bool> sa_b[12];
    sc_signal<bool> sb_a[12];
    sc_signal<bool> sb_b[12];

    sc_signal<bool> stage1_sum[12];
    sc_signal<bool> stage1_carry[12];

    sc_signal<bool> stage2_sum[12];
    sc_signal<bool> stage2_carry[12];

    sc_signal<bool> const_one;

    BsdBinaryFullAdder* stage1_adders[12];
    BsdBinaryFullAdder* stage2_adders[12];

    void drive_const_one() { const_one.write(true); }

    SC_CTOR(SignedDigitAdder12) {
        SC_METHOD(drive_const_one);

        for (int i = 0; i < 12; i++) {
            std::string stage1_name = "S1_FA" + std::to_string(i + 1);
            stage1_adders[i] = new BsdBinaryFullAdder(stage1_name.c_str());

            // Stage 1 FA(i): a=A_a[i], b=B_a[i], c=A_b[i] -- matches gate0..gate3
            // of the reference VHDL (DIG_Add port map a=>Aia, b=>Bia, c_i=>Aib).
            stage1_adders[i]->input_a(sa_a[i]);
            stage1_adders[i]->input_b(sb_a[i]);
            stage1_adders[i]->input_c(sa_b[i]);
            stage1_adders[i]->sum_out(stage1_sum[i]);
            stage1_adders[i]->carry_out(stage1_carry[i]);

            std::string stage2_name = "S2_FA" + std::to_string(i + 1);
            stage2_adders[i] = new BsdBinaryFullAdder(stage2_name.c_str());

            // Stage 2 FA(i): a=(i==0 ? 1 : stage1_carry[i-1]), b=stage1_sum[i],
            // c=B_b[i] -- matches gate4..gate7 (a=>s_prev/n1, b=>s_even, c_i=>Bib).
            if (i == 0) {
                stage2_adders[i]->input_a(const_one);
            } else {
                stage2_adders[i]->input_a(stage1_carry[i - 1]);
            }

            stage2_adders[i]->input_b(stage1_sum[i]);
            stage2_adders[i]->input_c(sb_b[i]);
            stage2_adders[i]->sum_out(stage2_sum[i]);
            stage2_adders[i]->carry_out(stage2_carry[i]);
        }

        SC_METHOD(unpack_inputs);
        dont_initialize();
        sensitive << A_a << A_b << B_a << B_b;

        SC_METHOD(pack_outputs);
        dont_initialize();
        for (int i = 0; i < 12; i++) {
            sensitive << stage2_sum[i] << stage2_carry[i];
        }
        sensitive << stage1_carry[11];
    }

    void unpack_inputs() {
        sc_lv<12> value_a_a = A_a.read();
        sc_lv<12> value_a_b = A_b.read();
        sc_lv<12> value_b_a = B_a.read();
        sc_lv<12> value_b_b = B_b.read();

        for (int i = 0; i < 12; i++) {
            sa_a[i].write(value_a_a[i].is_01() ? value_a_a[i].to_bool() : false);
            sa_b[i].write(value_a_b[i].is_01() ? value_a_b[i].to_bool() : false);
            sb_a[i].write(value_b_a[i].is_01() ? value_b_a[i].to_bool() : false);
            sb_b[i].write(value_b_b[i].is_01() ? value_b_b[i].to_bool() : false);
        }
    }

    void pack_outputs() {
        sc_lv<13> output_minus;
        sc_lv<13> output_plus;

        output_minus[0] = false;  // no carry-in below the least significant digit
        for (int i = 0; i < 12; i++) {
            output_minus[i + 1] = stage2_carry[i].read();
            output_plus[i] = stage2_sum[i].read();
        }

        output_plus[12] = stage1_carry[11].read();

        S_minus.write(output_minus);
        S_plus.write(output_plus);
    }

    void reset_counters() {
        for (int i = 0; i < 12; i++) {
            stage1_adders[i]->reset_counters();
            stage2_adders[i]->reset_counters();
        }
    }

    uint64_t total_switches() const {
        uint64_t total_switches = 0;
        for (int i = 0; i < 12; i++) {
            total_switches += stage1_adders[i]->total_switches();
            total_switches += stage2_adders[i]->total_switches();
        }
        return total_switches;
    }

    ~SignedDigitAdder12() {
        for (int i = 0; i < 12; i++) {
            delete stage1_adders[i];
            delete stage2_adders[i];
        }
    }
};

// ============================================================================
// 3. TESTBENCH -- Monte Carlo random sampling over the genuine
//    redundant digit-space: each sample draws every digit of A and B
//    independently and uniformly from {-1,0,+1}.
// ============================================================================

// ============================================================================
// Real OpenSTA (Nangate45) max delay per (startpoint pin, endpoint pin),
// extracted from all_critical_paths.csv for SD-12 (165 structurally
// connected pin pairs). Input pin index: 0..11=A_a[i], 12..23=A_b[i],
// 24..35=B_a[i], 36..47=B_b[i]. Output pin index: 0..12=S_plus[i],
// 13..25=S_minus[i].
// ============================================================================
static const int NUM_IN = 48;
static const int NUM_OUT = 26;
static double path_delay[NUM_IN][NUM_OUT];

static void init_path_delay_table() {
    for (int i = 0; i < NUM_IN; i++)
        for (int o = 0; o < NUM_OUT; o++)
            path_delay[i][o] = 0.0;

    path_delay[0][14] = 0.71;
    path_delay[0][15] = 0.82;
    path_delay[0][0] = 0.76;
    path_delay[0][1] = 0.79;
    path_delay[10][24] = 0.76;
    path_delay[10][25] = 0.82;
    path_delay[10][10] = 0.76;
    path_delay[10][11] = 0.79;
    path_delay[11][25] = 0.76;
    path_delay[11][11] = 0.76;
    path_delay[11][12] = 0.37;
    path_delay[1][15] = 0.76;
    path_delay[1][16] = 0.82;
    path_delay[1][1] = 0.76;
    path_delay[1][2] = 0.79;
    path_delay[2][16] = 0.76;
    path_delay[2][17] = 0.82;
    path_delay[2][2] = 0.76;
    path_delay[2][3] = 0.79;
    path_delay[3][17] = 0.76;
    path_delay[3][18] = 0.82;
    path_delay[3][3] = 0.76;
    path_delay[3][4] = 0.79;
    path_delay[4][18] = 0.76;
    path_delay[4][19] = 0.82;
    path_delay[4][4] = 0.76;
    path_delay[4][5] = 0.79;
    path_delay[5][19] = 0.76;
    path_delay[5][20] = 0.82;
    path_delay[5][5] = 0.76;
    path_delay[5][6] = 0.79;
    path_delay[6][20] = 0.76;
    path_delay[6][21] = 0.82;
    path_delay[6][6] = 0.76;
    path_delay[6][7] = 0.79;
    path_delay[7][21] = 0.76;
    path_delay[7][22] = 0.82;
    path_delay[7][7] = 0.76;
    path_delay[7][8] = 0.79;
    path_delay[8][23] = 0.82;
    path_delay[8][22] = 0.76;
    path_delay[8][8] = 0.76;
    path_delay[8][9] = 0.79;
    path_delay[9][23] = 0.76;
    path_delay[9][24] = 0.82;
    path_delay[9][10] = 0.79;
    path_delay[9][9] = 0.76;
    path_delay[12][14] = 0.53;
    path_delay[12][15] = 0.82;
    path_delay[12][0] = 0.59;
    path_delay[12][1] = 0.79;
    path_delay[22][24] = 0.59;
    path_delay[22][25] = 0.82;
    path_delay[22][10] = 0.59;
    path_delay[22][11] = 0.79;
    path_delay[23][25] = 0.59;
    path_delay[23][11] = 0.59;
    path_delay[23][12] = 0.38;
    path_delay[13][15] = 0.59;
    path_delay[13][16] = 0.82;
    path_delay[13][1] = 0.59;
    path_delay[13][2] = 0.79;
    path_delay[14][16] = 0.59;
    path_delay[14][17] = 0.82;
    path_delay[14][2] = 0.59;
    path_delay[14][3] = 0.79;
    path_delay[15][17] = 0.59;
    path_delay[15][18] = 0.82;
    path_delay[15][3] = 0.59;
    path_delay[15][4] = 0.79;
    path_delay[16][18] = 0.59;
    path_delay[16][19] = 0.82;
    path_delay[16][4] = 0.59;
    path_delay[16][5] = 0.79;
    path_delay[17][19] = 0.59;
    path_delay[17][20] = 0.82;
    path_delay[17][5] = 0.59;
    path_delay[17][6] = 0.79;
    path_delay[18][20] = 0.59;
    path_delay[18][21] = 0.82;
    path_delay[18][6] = 0.59;
    path_delay[18][7] = 0.79;
    path_delay[19][21] = 0.59;
    path_delay[19][22] = 0.82;
    path_delay[19][7] = 0.59;
    path_delay[19][8] = 0.79;
    path_delay[20][23] = 0.82;
    path_delay[20][22] = 0.59;
    path_delay[20][8] = 0.59;
    path_delay[20][9] = 0.79;
    path_delay[21][23] = 0.59;
    path_delay[21][24] = 0.82;
    path_delay[21][10] = 0.79;
    path_delay[21][9] = 0.59;
    path_delay[24][14] = 0.72;
    path_delay[24][15] = 0.81;
    path_delay[24][0] = 0.77;
    path_delay[24][1] = 0.78;
    path_delay[34][24] = 0.78;
    path_delay[34][25] = 0.81;
    path_delay[34][10] = 0.77;
    path_delay[34][11] = 0.78;
    path_delay[35][25] = 0.78;
    path_delay[35][11] = 0.77;
    path_delay[35][12] = 0.37;
    path_delay[25][15] = 0.78;
    path_delay[25][16] = 0.81;
    path_delay[25][1] = 0.77;
    path_delay[25][2] = 0.78;
    path_delay[26][16] = 0.78;
    path_delay[26][17] = 0.81;
    path_delay[26][2] = 0.77;
    path_delay[26][3] = 0.78;
    path_delay[27][17] = 0.78;
    path_delay[27][18] = 0.81;
    path_delay[27][3] = 0.77;
    path_delay[27][4] = 0.78;
    path_delay[28][18] = 0.78;
    path_delay[28][19] = 0.81;
    path_delay[28][4] = 0.77;
    path_delay[28][5] = 0.78;
    path_delay[29][19] = 0.78;
    path_delay[29][20] = 0.81;
    path_delay[29][5] = 0.77;
    path_delay[29][6] = 0.78;
    path_delay[30][20] = 0.78;
    path_delay[30][21] = 0.81;
    path_delay[30][6] = 0.77;
    path_delay[30][7] = 0.78;
    path_delay[31][21] = 0.78;
    path_delay[31][22] = 0.81;
    path_delay[31][7] = 0.77;
    path_delay[31][8] = 0.78;
    path_delay[32][23] = 0.81;
    path_delay[32][22] = 0.78;
    path_delay[32][8] = 0.77;
    path_delay[32][9] = 0.78;
    path_delay[33][23] = 0.78;
    path_delay[33][24] = 0.81;
    path_delay[33][10] = 0.78;
    path_delay[33][9] = 0.77;
    path_delay[36][14] = 0.32;
    path_delay[36][0] = 0.18;
    path_delay[46][24] = 0.38;
    path_delay[46][10] = 0.18;
    path_delay[47][25] = 0.38;
    path_delay[47][11] = 0.18;
    path_delay[37][15] = 0.38;
    path_delay[37][1] = 0.18;
    path_delay[38][16] = 0.38;
    path_delay[38][2] = 0.18;
    path_delay[39][17] = 0.38;
    path_delay[39][3] = 0.18;
    path_delay[40][18] = 0.38;
    path_delay[40][4] = 0.18;
    path_delay[41][19] = 0.38;
    path_delay[41][5] = 0.18;
    path_delay[42][20] = 0.38;
    path_delay[42][6] = 0.18;
    path_delay[43][21] = 0.38;
    path_delay[43][7] = 0.18;
    path_delay[44][22] = 0.38;
    path_delay[44][8] = 0.18;
    path_delay[45][23] = 0.38;
    path_delay[45][9] = 0.18;
}

// ============================================================================
// 3. TESTBENCH -- Monte Carlo, per-sample EDP. Draws every digit of A
//    and B independently and uniformly from {-1,0,+1} (genuine
//    redundant digit-space), same as the original Monte Carlo SD.
//    design_ptr->reset_counters() already exists and already propagates
//    correctly through every gate -- call it before EVERY sample instead
//    of just once before the loop, so total_switches() right after one
//    apply_test() is that sample's own switch count.
// ============================================================================
SC_MODULE(Testbench) {
    sc_out<sc_lv<12>> A_a, A_b, B_a, B_b;
    sc_in<sc_lv<13>> S_minus, S_plus;

    SignedDigitAdder12* design_ptr;

    unsigned int number_of_errors;

    unsigned long long num_samples;
    unsigned long long rng_seed;

    double sum_switches, sum_delay, sum_edp;

    bool prev_in[NUM_IN];
    bool prev_out[NUM_OUT];

    SC_CTOR(Testbench)
        : design_ptr(nullptr),
          number_of_errors(0),
          num_samples(1000000ULL),
          rng_seed(42ULL),
          sum_switches(0.0), sum_delay(0.0), sum_edp(0.0)
    {
        for (int i = 0; i < NUM_IN; i++) prev_in[i] = false;
        for (int i = 0; i < NUM_OUT; i++) prev_out[i] = false;
        SC_THREAD(stimulus);
    }

    void stimulus() {
        int zero_digits[12];
        for (int i = 0; i < 12; i++) zero_digits[i] = 0;
        DigitRails12 zero = encode_digits_12(zero_digits);

        A_a.write(zero.rail_a);
        A_b.write(zero.rail_b);
        B_a.write(zero.rail_a);
        B_b.write(zero.rail_b);

        wait(20, SC_NS);

        design_ptr->reset_counters();

        std::mt19937_64 rng(rng_seed);
        std::uniform_int_distribution<int> trit_dist(-1, 1);

        for (unsigned long long i = 0; i < num_samples; i++) {
            int a_digits[12], b_digits[12];
            for (int d = 0; d < 12; d++) a_digits[d] = trit_dist(rng);
            for (int d = 0; d < 12; d++) b_digits[d] = trit_dist(rng);
            apply_test(a_digits, b_digits);
        }

        assert(number_of_errors == 0);
        sc_stop();
    }

    void apply_test(const int a_digits[12], const int b_digits[12]) {
        design_ptr->reset_counters();

        DigitRails12 encoded_a = encode_digits_12(a_digits);
        DigitRails12 encoded_b = encode_digits_12(b_digits);

        A_a.write(encoded_a.rail_a);
        A_b.write(encoded_a.rail_b);
        B_a.write(encoded_b.rail_a);
        B_b.write(encoded_b.rail_b);

        wait(20, SC_NS);

        long long expected_result = digits_value_12(a_digits) + digits_value_12(b_digits);
        sc_lv<13> s_plus_val = S_plus.read();
        sc_lv<13> s_minus_val = S_minus.read();
        long long actual_result = decode_dual_rail_13(s_plus_val, s_minus_val);

        if (expected_result != actual_result) {
            number_of_errors++;
        }

        unsigned long long switches_i = design_ptr->total_switches();

        bool cur_in[NUM_IN];
        for (int i = 0; i < 12; i++) cur_in[i] = encoded_a.rail_a[i].is_01() ? encoded_a.rail_a[i].to_bool() : false;
        for (int i = 0; i < 12; i++) cur_in[12 + i] = encoded_a.rail_b[i].is_01() ? encoded_a.rail_b[i].to_bool() : false;
        for (int i = 0; i < 12; i++) cur_in[24 + i] = encoded_b.rail_a[i].is_01() ? encoded_b.rail_a[i].to_bool() : false;
        for (int i = 0; i < 12; i++) cur_in[36 + i] = encoded_b.rail_b[i].is_01() ? encoded_b.rail_b[i].to_bool() : false;

        bool cur_out[NUM_OUT];
        for (int i = 0; i <= 12; i++) cur_out[i] = s_plus_val[i].is_01() ? s_plus_val[i].to_bool() : false;
        for (int i = 0; i <= 12; i++) cur_out[13 + i] = s_minus_val[i].is_01() ? s_minus_val[i].to_bool() : false;

        double delay_i = 0.0;
        for (int in = 0; in < NUM_IN; in++) {
            if (cur_in[in] == prev_in[in]) continue;
            for (int out = 0; out < NUM_OUT; out++) {
                if (cur_out[out] == prev_out[out]) continue;
                double d = path_delay[in][out];
                if (d > delay_i) delay_i = d;
            }
        }

        double edp_i = static_cast<double>(switches_i) * delay_i;
        sum_switches += static_cast<double>(switches_i);
        sum_delay += delay_i;
        sum_edp += edp_i;

        for (int i = 0; i < NUM_IN; i++) prev_in[i] = cur_in[i];
        for (int i = 0; i < NUM_OUT; i++) prev_out[i] = cur_out[i];
    }
};

// ============================================================================
// 4. MAIN -- Monte Carlo, auto-parallel across all CPU cores
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
    sc_signal<sc_lv<12>> A_a, A_b, B_a, B_b;
    sc_signal<sc_lv<13>> S_minus, S_plus;

    SignedDigitAdder12 adder("SignedDigitAdder12");
    Testbench tb("TB");

    tb.design_ptr = &adder;

    adder.A_a(A_a);
    adder.A_b(A_b);
    adder.B_a(B_a);
    adder.B_b(B_b);
    adder.S_minus(S_minus);
    adder.S_plus(S_plus);

    tb.A_a(A_a);
    tb.A_b(A_b);
    tb.B_a(B_a);
    tb.B_b(B_b);
    tb.S_minus(S_minus);
    tb.S_plus(S_plus);

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

    std::cout << "SD-12-MC-pathEDP: SAMPLES=" << total_samples
              << " AVG_SWITCHES=" << avg_switches
              << " AVG_DELAY_ns=" << avg_delay
              << " AVG_EDP_correct=" << avg_edp_correct
              << " AVG_EDP_naive=" << avg_edp_naive
              << " pct_diff=" << ((avg_edp_naive / avg_edp_correct - 1.0) * 100.0) << "%\n";

    return 0;
}
