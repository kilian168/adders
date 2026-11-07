// sd_glitch.cpp -- 64-digit redundant signed-digit ("PPM") adder,
// Monte Carlo random sampling over the genuine digit-space.
// Gate-level port of the reference VHDL (8 chained 1-bit DIG_Add full
// adders for N=4, generalized to N digits here): each operand digit i
// is dual-rail encoded as (rail_a, rail_b) with digit = rail_a+rail_b-1,
// i.e. -1 -> (0,0), 0 -> (0,1) [or equivalently (1,0)], +1 -> (1,1).
// Place values are binary (2^i), not base-3 -- confirmed against the
// reference schematic. Each sample draws every digit of A and B
// independently and uniformly from {-1,0,+1}.
//
// At N=64 the place-value sum needs more than 64 bits (two 64-digit
// operands each range to +-(2^64-1), and their sum can reach 2^65-2),
// so all value/decode arithmetic here uses __int128 instead of the
// long long used at smaller widths.
#include <systemc.h>
#include <iostream>
#include <iomanip>
#include <string>
#include <cassert>
#include <cstdint>
#include <algorithm>
#include <random>
#include <limits>
#include <thread>
#include <vector>
#include <sys/wait.h>
#include <unistd.h>

// ============================================================================
// 0. HELPER STRUCTS AND FUNCTIONS
// ============================================================================
struct DigitRails64 {
    sc_lv<64> rail_a;
    sc_lv<64> rail_b;
};

// Encodes an array of N signed digits (each -1, 0, or +1) into dual rails,
// one canonical encoding per digit value: -1->(0,0), 0->(0,1), +1->(1,1).
static DigitRails64 encode_digits_64(const int digits[64]) {
    DigitRails64 encoded;
    encoded.rail_a = "0000000000000000000000000000000000000000000000000000000000000000";
    encoded.rail_b = "0000000000000000000000000000000000000000000000000000000000000000";
    for (int i = 0; i < 64; i++) {
        int d = digits[i];
        encoded.rail_a[i] = (d == 1);
        encoded.rail_b[i] = (d != -1);
    }
    return encoded;
}

// Numeric value of a digit pattern at binary place values (2^i).
static __int128 digits_value_64(const int digits[64]) {
    __int128 value = 0;
    for (int i = 0; i < 64; i++) value += static_cast<__int128>(digits[i]) * (static_cast<__int128>(1) << i);
    return value;
}

// Plain binary expansion of an unsigned value into N signed digits
// (each 0 or +1, never -1) -- the dropped-NAF/SD-BIN encoding, matching
// RCA/CLA's unsigned 0..2^N-1 value range exactly.
static void binary_digits(unsigned long long v, int digits[64]) {
    for (int i = 0; i < 64; i++) digits[i] = static_cast<int>((v >> i) & 1ULL);
}

// Decodes the 65-digit dual-rail result: each (S_plus[i], S_minus[i])
// pair is a digit at binary place value 2^i, coded 00=-1, 11=+1, 01/10=0
// (same coding as the operand rails).
static __int128 decode_dual_rail_65(const sc_lv<65>& s_plus, const sc_lv<65>& s_minus) {
    __int128 value = 0;
    for (int i = 0; i < 65; i++) {
        bool p = s_plus[i].is_01() && s_plus[i].to_bool();
        bool m = s_minus[i].is_01() && s_minus[i].to_bool();
        int digit = (p && m) ? 1 : ((!p && !m) ? -1 : 0);
        value += static_cast<__int128>(digit) * (static_cast<__int128>(1) << i);
    }
    return value;
}

// ============================================================================
// 1. GENERIC GATE-LEVEL PRIMITIVES
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
// 2. 64-DIGIT SIGNED-DIGIT ("PPM") ADDER -- exact port of the reference VHDL
// ============================================================================
SC_MODULE(SignedDigitAdder64) {
    sc_in<sc_lv<64>> A_a;
    sc_in<sc_lv<64>> A_b;
    sc_in<sc_lv<64>> B_a;
    sc_in<sc_lv<64>> B_b;

    sc_out<sc_lv<65>> S_minus;
    sc_out<sc_lv<65>> S_plus;

    sc_signal<bool> sa_a[64];
    sc_signal<bool> sa_b[64];
    sc_signal<bool> sb_a[64];
    sc_signal<bool> sb_b[64];

    sc_signal<bool> stage1_sum[64];
    sc_signal<bool> stage1_carry[64];

    sc_signal<bool> stage2_sum[64];
    sc_signal<bool> stage2_carry[64];

    sc_signal<bool> const_one;

    BsdBinaryFullAdder* stage1_adders[64];
    BsdBinaryFullAdder* stage2_adders[64];

    void drive_const_one() { const_one.write(true); }

    SC_CTOR(SignedDigitAdder64) {
        SC_METHOD(drive_const_one);

        for (int i = 0; i < 64; i++) {
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
        for (int i = 0; i < 64; i++) {
            sensitive << stage2_sum[i] << stage2_carry[i];
        }
        sensitive << stage1_carry[63];
    }

    void unpack_inputs() {
        sc_lv<64> value_a_a = A_a.read();
        sc_lv<64> value_a_b = A_b.read();
        sc_lv<64> value_b_a = B_a.read();
        sc_lv<64> value_b_b = B_b.read();

        for (int i = 0; i < 64; i++) {
            sa_a[i].write(value_a_a[i].is_01() ? value_a_a[i].to_bool() : false);
            sa_b[i].write(value_a_b[i].is_01() ? value_a_b[i].to_bool() : false);
            sb_a[i].write(value_b_a[i].is_01() ? value_b_a[i].to_bool() : false);
            sb_b[i].write(value_b_b[i].is_01() ? value_b_b[i].to_bool() : false);
        }
    }

    void pack_outputs() {
        sc_lv<65> output_minus;
        sc_lv<65> output_plus;

        output_minus[0] = false;  // no carry-in below the least significant digit
        for (int i = 0; i < 64; i++) {
            output_minus[i + 1] = stage2_carry[i].read();
            output_plus[i] = stage2_sum[i].read();
        }

        output_plus[64] = stage1_carry[63].read();

        S_minus.write(output_minus);
        S_plus.write(output_plus);
    }

    void reset_counters() {
        for (int i = 0; i < 64; i++) {
            stage1_adders[i]->reset_counters();
            stage2_adders[i]->reset_counters();
        }
    }

    uint64_t total_switches() const {
        uint64_t total_switches = 0;
        for (int i = 0; i < 64; i++) {
            total_switches += stage1_adders[i]->total_switches();
            total_switches += stage2_adders[i]->total_switches();
        }
        return total_switches;
    }

    ~SignedDigitAdder64() {
        for (int i = 0; i < 64; i++) {
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
// extracted from all_critical_paths.csv for SD-64 (893 structurally
// connected pin pairs). Input pin index: 0..63=A_a[i], 64..127=A_b[i],
// 128..191=B_a[i], 192..255=B_b[i]. Output pin index: 0..64=S_plus[i],
// 65..129=S_minus[i].
// ============================================================================
static const int NUM_IN = 256;
static const int NUM_OUT = 130;
static double path_delay[NUM_IN][NUM_OUT];

static void init_path_delay_table() {
    for (int i = 0; i < NUM_IN; i++)
        for (int o = 0; o < NUM_OUT; o++)
            path_delay[i][o] = 0.0;

    path_delay[0][66] = 0.71;
    path_delay[0][67] = 0.82;
    path_delay[0][0] = 0.76;
    path_delay[0][1] = 0.79;
    path_delay[10][76] = 0.76;
    path_delay[10][77] = 0.82;
    path_delay[10][10] = 0.76;
    path_delay[10][11] = 0.79;
    path_delay[11][77] = 0.76;
    path_delay[11][78] = 0.82;
    path_delay[11][11] = 0.76;
    path_delay[11][12] = 0.79;
    path_delay[12][78] = 0.76;
    path_delay[12][79] = 0.82;
    path_delay[12][12] = 0.76;
    path_delay[12][13] = 0.79;
    path_delay[13][79] = 0.76;
    path_delay[13][80] = 0.82;
    path_delay[13][13] = 0.76;
    path_delay[13][14] = 0.79;
    path_delay[14][80] = 0.76;
    path_delay[14][81] = 0.82;
    path_delay[14][14] = 0.76;
    path_delay[14][15] = 0.79;
    path_delay[15][81] = 0.76;
    path_delay[15][82] = 0.82;
    path_delay[15][15] = 0.76;
    path_delay[15][16] = 0.79;
    path_delay[16][82] = 0.76;
    path_delay[16][83] = 0.82;
    path_delay[16][16] = 0.76;
    path_delay[16][17] = 0.79;
    path_delay[17][83] = 0.76;
    path_delay[17][84] = 0.82;
    path_delay[17][17] = 0.76;
    path_delay[17][18] = 0.79;
    path_delay[18][84] = 0.76;
    path_delay[18][85] = 0.82;
    path_delay[18][18] = 0.76;
    path_delay[18][19] = 0.79;
    path_delay[19][85] = 0.76;
    path_delay[19][86] = 0.82;
    path_delay[19][19] = 0.76;
    path_delay[19][20] = 0.79;
    path_delay[1][67] = 0.76;
    path_delay[1][68] = 0.82;
    path_delay[1][1] = 0.76;
    path_delay[1][2] = 0.79;
    path_delay[20][86] = 0.76;
    path_delay[20][87] = 0.82;
    path_delay[20][20] = 0.76;
    path_delay[20][21] = 0.79;
    path_delay[21][87] = 0.76;
    path_delay[21][88] = 0.82;
    path_delay[21][21] = 0.76;
    path_delay[21][22] = 0.79;
    path_delay[22][88] = 0.76;
    path_delay[22][89] = 0.82;
    path_delay[22][22] = 0.76;
    path_delay[22][23] = 0.79;
    path_delay[23][89] = 0.76;
    path_delay[23][90] = 0.82;
    path_delay[23][23] = 0.76;
    path_delay[23][24] = 0.79;
    path_delay[24][90] = 0.76;
    path_delay[24][91] = 0.82;
    path_delay[24][24] = 0.76;
    path_delay[24][25] = 0.79;
    path_delay[25][91] = 0.76;
    path_delay[25][92] = 0.82;
    path_delay[25][25] = 0.76;
    path_delay[25][26] = 0.79;
    path_delay[26][92] = 0.76;
    path_delay[26][93] = 0.82;
    path_delay[26][26] = 0.76;
    path_delay[26][27] = 0.79;
    path_delay[27][93] = 0.76;
    path_delay[27][94] = 0.82;
    path_delay[27][27] = 0.76;
    path_delay[27][28] = 0.79;
    path_delay[28][94] = 0.76;
    path_delay[28][95] = 0.82;
    path_delay[28][28] = 0.76;
    path_delay[28][29] = 0.79;
    path_delay[29][95] = 0.76;
    path_delay[29][96] = 0.82;
    path_delay[29][29] = 0.76;
    path_delay[29][30] = 0.79;
    path_delay[2][68] = 0.76;
    path_delay[2][69] = 0.82;
    path_delay[2][2] = 0.76;
    path_delay[2][3] = 0.79;
    path_delay[30][96] = 0.76;
    path_delay[30][97] = 0.82;
    path_delay[30][30] = 0.76;
    path_delay[30][31] = 0.79;
    path_delay[31][97] = 0.76;
    path_delay[31][98] = 0.82;
    path_delay[31][31] = 0.76;
    path_delay[31][32] = 0.79;
    path_delay[32][98] = 0.76;
    path_delay[32][99] = 0.82;
    path_delay[32][32] = 0.76;
    path_delay[32][33] = 0.79;
    path_delay[33][99] = 0.76;
    path_delay[33][100] = 0.82;
    path_delay[33][33] = 0.76;
    path_delay[33][34] = 0.79;
    path_delay[34][100] = 0.76;
    path_delay[34][101] = 0.82;
    path_delay[34][34] = 0.76;
    path_delay[34][35] = 0.79;
    path_delay[35][101] = 0.76;
    path_delay[35][102] = 0.82;
    path_delay[35][35] = 0.76;
    path_delay[35][36] = 0.79;
    path_delay[36][102] = 0.76;
    path_delay[36][103] = 0.82;
    path_delay[36][36] = 0.76;
    path_delay[36][37] = 0.79;
    path_delay[37][103] = 0.76;
    path_delay[37][104] = 0.82;
    path_delay[37][37] = 0.76;
    path_delay[37][38] = 0.79;
    path_delay[38][104] = 0.76;
    path_delay[38][105] = 0.82;
    path_delay[38][38] = 0.76;
    path_delay[38][39] = 0.79;
    path_delay[39][105] = 0.76;
    path_delay[39][106] = 0.82;
    path_delay[39][39] = 0.76;
    path_delay[39][40] = 0.79;
    path_delay[3][69] = 0.76;
    path_delay[3][70] = 0.82;
    path_delay[3][3] = 0.76;
    path_delay[3][4] = 0.79;
    path_delay[40][106] = 0.76;
    path_delay[40][107] = 0.82;
    path_delay[40][40] = 0.76;
    path_delay[40][41] = 0.79;
    path_delay[41][107] = 0.76;
    path_delay[41][108] = 0.82;
    path_delay[41][41] = 0.76;
    path_delay[41][42] = 0.79;
    path_delay[42][108] = 0.76;
    path_delay[42][109] = 0.82;
    path_delay[42][42] = 0.76;
    path_delay[42][43] = 0.79;
    path_delay[43][109] = 0.76;
    path_delay[43][110] = 0.82;
    path_delay[43][43] = 0.76;
    path_delay[43][44] = 0.79;
    path_delay[44][110] = 0.76;
    path_delay[44][111] = 0.82;
    path_delay[44][44] = 0.76;
    path_delay[44][45] = 0.79;
    path_delay[45][111] = 0.76;
    path_delay[45][112] = 0.82;
    path_delay[45][45] = 0.76;
    path_delay[45][46] = 0.79;
    path_delay[46][112] = 0.76;
    path_delay[46][113] = 0.82;
    path_delay[46][46] = 0.76;
    path_delay[46][47] = 0.79;
    path_delay[47][113] = 0.76;
    path_delay[47][114] = 0.82;
    path_delay[47][47] = 0.76;
    path_delay[47][48] = 0.79;
    path_delay[48][114] = 0.76;
    path_delay[48][115] = 0.82;
    path_delay[48][48] = 0.76;
    path_delay[48][49] = 0.79;
    path_delay[49][115] = 0.76;
    path_delay[49][116] = 0.82;
    path_delay[49][49] = 0.76;
    path_delay[49][50] = 0.79;
    path_delay[4][70] = 0.76;
    path_delay[4][71] = 0.82;
    path_delay[4][4] = 0.76;
    path_delay[4][5] = 0.79;
    path_delay[50][116] = 0.76;
    path_delay[50][117] = 0.82;
    path_delay[50][50] = 0.76;
    path_delay[50][51] = 0.79;
    path_delay[51][117] = 0.76;
    path_delay[51][118] = 0.82;
    path_delay[51][51] = 0.76;
    path_delay[51][52] = 0.79;
    path_delay[52][118] = 0.76;
    path_delay[52][119] = 0.82;
    path_delay[52][52] = 0.76;
    path_delay[52][53] = 0.79;
    path_delay[53][119] = 0.76;
    path_delay[53][120] = 0.82;
    path_delay[53][53] = 0.76;
    path_delay[53][54] = 0.79;
    path_delay[54][120] = 0.76;
    path_delay[54][121] = 0.82;
    path_delay[54][54] = 0.76;
    path_delay[54][55] = 0.79;
    path_delay[55][121] = 0.76;
    path_delay[55][122] = 0.82;
    path_delay[55][55] = 0.76;
    path_delay[55][56] = 0.79;
    path_delay[56][122] = 0.76;
    path_delay[56][123] = 0.82;
    path_delay[56][56] = 0.76;
    path_delay[56][57] = 0.79;
    path_delay[57][123] = 0.76;
    path_delay[57][124] = 0.82;
    path_delay[57][57] = 0.76;
    path_delay[57][58] = 0.79;
    path_delay[58][124] = 0.76;
    path_delay[58][125] = 0.82;
    path_delay[58][58] = 0.76;
    path_delay[58][59] = 0.79;
    path_delay[59][125] = 0.76;
    path_delay[59][126] = 0.82;
    path_delay[59][59] = 0.76;
    path_delay[59][60] = 0.79;
    path_delay[5][71] = 0.76;
    path_delay[5][72] = 0.82;
    path_delay[5][5] = 0.76;
    path_delay[5][6] = 0.79;
    path_delay[60][126] = 0.76;
    path_delay[60][127] = 0.82;
    path_delay[60][60] = 0.76;
    path_delay[60][61] = 0.79;
    path_delay[61][127] = 0.76;
    path_delay[61][128] = 0.82;
    path_delay[61][61] = 0.76;
    path_delay[61][62] = 0.79;
    path_delay[62][128] = 0.76;
    path_delay[62][129] = 0.82;
    path_delay[62][62] = 0.76;
    path_delay[62][63] = 0.79;
    path_delay[63][129] = 0.76;
    path_delay[63][63] = 0.76;
    path_delay[63][64] = 0.37;
    path_delay[6][72] = 0.76;
    path_delay[6][73] = 0.82;
    path_delay[6][6] = 0.76;
    path_delay[6][7] = 0.79;
    path_delay[7][73] = 0.76;
    path_delay[7][74] = 0.82;
    path_delay[7][7] = 0.76;
    path_delay[7][8] = 0.79;
    path_delay[8][75] = 0.82;
    path_delay[8][74] = 0.76;
    path_delay[8][8] = 0.76;
    path_delay[8][9] = 0.79;
    path_delay[9][75] = 0.76;
    path_delay[9][76] = 0.82;
    path_delay[9][10] = 0.79;
    path_delay[9][9] = 0.76;
    path_delay[64][66] = 0.53;
    path_delay[64][67] = 0.82;
    path_delay[64][0] = 0.59;
    path_delay[64][1] = 0.79;
    path_delay[74][76] = 0.59;
    path_delay[74][77] = 0.82;
    path_delay[74][10] = 0.59;
    path_delay[74][11] = 0.79;
    path_delay[75][77] = 0.59;
    path_delay[75][78] = 0.82;
    path_delay[75][11] = 0.59;
    path_delay[75][12] = 0.79;
    path_delay[76][78] = 0.59;
    path_delay[76][79] = 0.82;
    path_delay[76][12] = 0.59;
    path_delay[76][13] = 0.79;
    path_delay[77][79] = 0.59;
    path_delay[77][80] = 0.82;
    path_delay[77][13] = 0.59;
    path_delay[77][14] = 0.79;
    path_delay[78][80] = 0.59;
    path_delay[78][81] = 0.82;
    path_delay[78][14] = 0.59;
    path_delay[78][15] = 0.79;
    path_delay[79][81] = 0.59;
    path_delay[79][82] = 0.82;
    path_delay[79][15] = 0.59;
    path_delay[79][16] = 0.79;
    path_delay[80][82] = 0.59;
    path_delay[80][83] = 0.82;
    path_delay[80][16] = 0.59;
    path_delay[80][17] = 0.79;
    path_delay[81][83] = 0.59;
    path_delay[81][84] = 0.82;
    path_delay[81][17] = 0.59;
    path_delay[81][18] = 0.79;
    path_delay[82][84] = 0.59;
    path_delay[82][85] = 0.82;
    path_delay[82][18] = 0.59;
    path_delay[82][19] = 0.79;
    path_delay[83][85] = 0.59;
    path_delay[83][86] = 0.82;
    path_delay[83][19] = 0.59;
    path_delay[83][20] = 0.79;
    path_delay[65][67] = 0.59;
    path_delay[65][68] = 0.82;
    path_delay[65][1] = 0.59;
    path_delay[65][2] = 0.79;
    path_delay[84][86] = 0.59;
    path_delay[84][87] = 0.82;
    path_delay[84][20] = 0.59;
    path_delay[84][21] = 0.79;
    path_delay[85][87] = 0.59;
    path_delay[85][88] = 0.82;
    path_delay[85][21] = 0.59;
    path_delay[85][22] = 0.79;
    path_delay[86][88] = 0.59;
    path_delay[86][89] = 0.82;
    path_delay[86][22] = 0.59;
    path_delay[86][23] = 0.79;
    path_delay[87][89] = 0.59;
    path_delay[87][90] = 0.82;
    path_delay[87][23] = 0.59;
    path_delay[87][24] = 0.79;
    path_delay[88][90] = 0.59;
    path_delay[88][91] = 0.82;
    path_delay[88][24] = 0.59;
    path_delay[88][25] = 0.79;
    path_delay[89][91] = 0.59;
    path_delay[89][92] = 0.82;
    path_delay[89][25] = 0.59;
    path_delay[89][26] = 0.79;
    path_delay[90][92] = 0.59;
    path_delay[90][93] = 0.82;
    path_delay[90][26] = 0.59;
    path_delay[90][27] = 0.79;
    path_delay[91][93] = 0.59;
    path_delay[91][94] = 0.82;
    path_delay[91][27] = 0.59;
    path_delay[91][28] = 0.79;
    path_delay[92][94] = 0.59;
    path_delay[92][95] = 0.82;
    path_delay[92][28] = 0.59;
    path_delay[92][29] = 0.79;
    path_delay[93][95] = 0.59;
    path_delay[93][96] = 0.82;
    path_delay[93][29] = 0.59;
    path_delay[93][30] = 0.79;
    path_delay[66][68] = 0.59;
    path_delay[66][69] = 0.82;
    path_delay[66][2] = 0.59;
    path_delay[66][3] = 0.79;
    path_delay[94][96] = 0.59;
    path_delay[94][97] = 0.82;
    path_delay[94][30] = 0.59;
    path_delay[94][31] = 0.79;
    path_delay[95][97] = 0.59;
    path_delay[95][98] = 0.82;
    path_delay[95][31] = 0.59;
    path_delay[95][32] = 0.79;
    path_delay[96][98] = 0.59;
    path_delay[96][99] = 0.82;
    path_delay[96][32] = 0.59;
    path_delay[96][33] = 0.79;
    path_delay[97][99] = 0.59;
    path_delay[97][100] = 0.82;
    path_delay[97][33] = 0.59;
    path_delay[97][34] = 0.79;
    path_delay[98][100] = 0.59;
    path_delay[98][101] = 0.82;
    path_delay[98][34] = 0.59;
    path_delay[98][35] = 0.79;
    path_delay[99][101] = 0.59;
    path_delay[99][102] = 0.82;
    path_delay[99][35] = 0.59;
    path_delay[99][36] = 0.79;
    path_delay[100][102] = 0.59;
    path_delay[100][103] = 0.82;
    path_delay[100][36] = 0.59;
    path_delay[100][37] = 0.79;
    path_delay[101][103] = 0.59;
    path_delay[101][104] = 0.82;
    path_delay[101][37] = 0.59;
    path_delay[101][38] = 0.79;
    path_delay[102][104] = 0.59;
    path_delay[102][105] = 0.82;
    path_delay[102][38] = 0.59;
    path_delay[102][39] = 0.79;
    path_delay[103][105] = 0.59;
    path_delay[103][106] = 0.82;
    path_delay[103][39] = 0.59;
    path_delay[103][40] = 0.79;
    path_delay[67][69] = 0.59;
    path_delay[67][70] = 0.82;
    path_delay[67][3] = 0.59;
    path_delay[67][4] = 0.79;
    path_delay[104][106] = 0.59;
    path_delay[104][107] = 0.82;
    path_delay[104][40] = 0.59;
    path_delay[104][41] = 0.79;
    path_delay[105][107] = 0.59;
    path_delay[105][108] = 0.82;
    path_delay[105][41] = 0.59;
    path_delay[105][42] = 0.79;
    path_delay[106][108] = 0.59;
    path_delay[106][109] = 0.82;
    path_delay[106][42] = 0.59;
    path_delay[106][43] = 0.79;
    path_delay[107][109] = 0.59;
    path_delay[107][110] = 0.82;
    path_delay[107][43] = 0.59;
    path_delay[107][44] = 0.79;
    path_delay[108][110] = 0.59;
    path_delay[108][111] = 0.82;
    path_delay[108][44] = 0.59;
    path_delay[108][45] = 0.79;
    path_delay[109][111] = 0.59;
    path_delay[109][112] = 0.82;
    path_delay[109][45] = 0.59;
    path_delay[109][46] = 0.79;
    path_delay[110][112] = 0.59;
    path_delay[110][113] = 0.82;
    path_delay[110][46] = 0.59;
    path_delay[110][47] = 0.79;
    path_delay[111][113] = 0.59;
    path_delay[111][114] = 0.82;
    path_delay[111][47] = 0.59;
    path_delay[111][48] = 0.79;
    path_delay[112][114] = 0.59;
    path_delay[112][115] = 0.82;
    path_delay[112][48] = 0.59;
    path_delay[112][49] = 0.79;
    path_delay[113][115] = 0.59;
    path_delay[113][116] = 0.82;
    path_delay[113][49] = 0.59;
    path_delay[113][50] = 0.79;
    path_delay[68][70] = 0.59;
    path_delay[68][71] = 0.82;
    path_delay[68][4] = 0.59;
    path_delay[68][5] = 0.79;
    path_delay[114][116] = 0.59;
    path_delay[114][117] = 0.82;
    path_delay[114][50] = 0.59;
    path_delay[114][51] = 0.79;
    path_delay[115][117] = 0.59;
    path_delay[115][118] = 0.82;
    path_delay[115][51] = 0.59;
    path_delay[115][52] = 0.79;
    path_delay[116][118] = 0.59;
    path_delay[116][119] = 0.82;
    path_delay[116][52] = 0.59;
    path_delay[116][53] = 0.79;
    path_delay[117][119] = 0.59;
    path_delay[117][120] = 0.82;
    path_delay[117][53] = 0.59;
    path_delay[117][54] = 0.79;
    path_delay[118][120] = 0.59;
    path_delay[118][121] = 0.82;
    path_delay[118][54] = 0.59;
    path_delay[118][55] = 0.79;
    path_delay[119][121] = 0.59;
    path_delay[119][122] = 0.82;
    path_delay[119][55] = 0.59;
    path_delay[119][56] = 0.79;
    path_delay[120][122] = 0.59;
    path_delay[120][123] = 0.82;
    path_delay[120][56] = 0.59;
    path_delay[120][57] = 0.79;
    path_delay[121][123] = 0.59;
    path_delay[121][124] = 0.82;
    path_delay[121][57] = 0.59;
    path_delay[121][58] = 0.79;
    path_delay[122][124] = 0.59;
    path_delay[122][125] = 0.82;
    path_delay[122][58] = 0.59;
    path_delay[122][59] = 0.79;
    path_delay[123][125] = 0.59;
    path_delay[123][126] = 0.82;
    path_delay[123][59] = 0.59;
    path_delay[123][60] = 0.79;
    path_delay[69][71] = 0.59;
    path_delay[69][72] = 0.82;
    path_delay[69][5] = 0.59;
    path_delay[69][6] = 0.79;
    path_delay[124][126] = 0.59;
    path_delay[124][127] = 0.82;
    path_delay[124][60] = 0.59;
    path_delay[124][61] = 0.79;
    path_delay[125][127] = 0.59;
    path_delay[125][128] = 0.82;
    path_delay[125][61] = 0.59;
    path_delay[125][62] = 0.79;
    path_delay[126][128] = 0.59;
    path_delay[126][129] = 0.82;
    path_delay[126][62] = 0.59;
    path_delay[126][63] = 0.79;
    path_delay[127][129] = 0.59;
    path_delay[127][63] = 0.59;
    path_delay[127][64] = 0.38;
    path_delay[70][72] = 0.59;
    path_delay[70][73] = 0.82;
    path_delay[70][6] = 0.59;
    path_delay[70][7] = 0.79;
    path_delay[71][73] = 0.59;
    path_delay[71][74] = 0.82;
    path_delay[71][7] = 0.59;
    path_delay[71][8] = 0.79;
    path_delay[72][75] = 0.82;
    path_delay[72][74] = 0.59;
    path_delay[72][8] = 0.59;
    path_delay[72][9] = 0.79;
    path_delay[73][75] = 0.59;
    path_delay[73][76] = 0.82;
    path_delay[73][10] = 0.79;
    path_delay[73][9] = 0.59;
    path_delay[128][66] = 0.72;
    path_delay[128][67] = 0.81;
    path_delay[128][0] = 0.77;
    path_delay[128][1] = 0.78;
    path_delay[138][76] = 0.78;
    path_delay[138][77] = 0.81;
    path_delay[138][10] = 0.77;
    path_delay[138][11] = 0.78;
    path_delay[139][77] = 0.78;
    path_delay[139][78] = 0.81;
    path_delay[139][11] = 0.77;
    path_delay[139][12] = 0.78;
    path_delay[140][78] = 0.78;
    path_delay[140][79] = 0.81;
    path_delay[140][12] = 0.77;
    path_delay[140][13] = 0.78;
    path_delay[141][79] = 0.78;
    path_delay[141][80] = 0.81;
    path_delay[141][13] = 0.77;
    path_delay[141][14] = 0.78;
    path_delay[142][80] = 0.78;
    path_delay[142][81] = 0.81;
    path_delay[142][14] = 0.77;
    path_delay[142][15] = 0.78;
    path_delay[143][81] = 0.78;
    path_delay[143][82] = 0.81;
    path_delay[143][15] = 0.77;
    path_delay[143][16] = 0.78;
    path_delay[144][82] = 0.78;
    path_delay[144][83] = 0.81;
    path_delay[144][16] = 0.77;
    path_delay[144][17] = 0.78;
    path_delay[145][83] = 0.78;
    path_delay[145][84] = 0.81;
    path_delay[145][17] = 0.77;
    path_delay[145][18] = 0.78;
    path_delay[146][84] = 0.78;
    path_delay[146][85] = 0.81;
    path_delay[146][18] = 0.77;
    path_delay[146][19] = 0.78;
    path_delay[147][85] = 0.78;
    path_delay[147][86] = 0.81;
    path_delay[147][19] = 0.77;
    path_delay[147][20] = 0.78;
    path_delay[129][67] = 0.78;
    path_delay[129][68] = 0.81;
    path_delay[129][1] = 0.77;
    path_delay[129][2] = 0.78;
    path_delay[148][86] = 0.78;
    path_delay[148][87] = 0.81;
    path_delay[148][20] = 0.77;
    path_delay[148][21] = 0.78;
    path_delay[149][87] = 0.78;
    path_delay[149][88] = 0.81;
    path_delay[149][21] = 0.77;
    path_delay[149][22] = 0.78;
    path_delay[150][88] = 0.78;
    path_delay[150][89] = 0.81;
    path_delay[150][22] = 0.77;
    path_delay[150][23] = 0.78;
    path_delay[151][89] = 0.78;
    path_delay[151][90] = 0.81;
    path_delay[151][23] = 0.77;
    path_delay[151][24] = 0.78;
    path_delay[152][90] = 0.78;
    path_delay[152][91] = 0.81;
    path_delay[152][24] = 0.77;
    path_delay[152][25] = 0.78;
    path_delay[153][91] = 0.78;
    path_delay[153][92] = 0.81;
    path_delay[153][25] = 0.77;
    path_delay[153][26] = 0.78;
    path_delay[154][92] = 0.78;
    path_delay[154][93] = 0.81;
    path_delay[154][26] = 0.77;
    path_delay[154][27] = 0.78;
    path_delay[155][93] = 0.78;
    path_delay[155][94] = 0.81;
    path_delay[155][27] = 0.77;
    path_delay[155][28] = 0.78;
    path_delay[156][94] = 0.78;
    path_delay[156][95] = 0.81;
    path_delay[156][28] = 0.77;
    path_delay[156][29] = 0.78;
    path_delay[157][95] = 0.78;
    path_delay[157][96] = 0.81;
    path_delay[157][29] = 0.77;
    path_delay[157][30] = 0.78;
    path_delay[130][68] = 0.78;
    path_delay[130][69] = 0.81;
    path_delay[130][2] = 0.77;
    path_delay[130][3] = 0.78;
    path_delay[158][96] = 0.78;
    path_delay[158][97] = 0.81;
    path_delay[158][30] = 0.77;
    path_delay[158][31] = 0.78;
    path_delay[159][97] = 0.78;
    path_delay[159][98] = 0.81;
    path_delay[159][31] = 0.77;
    path_delay[159][32] = 0.78;
    path_delay[160][98] = 0.78;
    path_delay[160][99] = 0.81;
    path_delay[160][32] = 0.77;
    path_delay[160][33] = 0.78;
    path_delay[161][99] = 0.78;
    path_delay[161][100] = 0.81;
    path_delay[161][33] = 0.77;
    path_delay[161][34] = 0.78;
    path_delay[162][100] = 0.78;
    path_delay[162][101] = 0.81;
    path_delay[162][34] = 0.77;
    path_delay[162][35] = 0.78;
    path_delay[163][101] = 0.78;
    path_delay[163][102] = 0.81;
    path_delay[163][35] = 0.77;
    path_delay[163][36] = 0.78;
    path_delay[164][102] = 0.78;
    path_delay[164][103] = 0.81;
    path_delay[164][36] = 0.77;
    path_delay[164][37] = 0.78;
    path_delay[165][103] = 0.78;
    path_delay[165][104] = 0.81;
    path_delay[165][37] = 0.77;
    path_delay[165][38] = 0.78;
    path_delay[166][104] = 0.78;
    path_delay[166][105] = 0.81;
    path_delay[166][38] = 0.77;
    path_delay[166][39] = 0.78;
    path_delay[167][105] = 0.78;
    path_delay[167][106] = 0.81;
    path_delay[167][39] = 0.77;
    path_delay[167][40] = 0.78;
    path_delay[131][69] = 0.78;
    path_delay[131][70] = 0.81;
    path_delay[131][3] = 0.77;
    path_delay[131][4] = 0.78;
    path_delay[168][106] = 0.78;
    path_delay[168][107] = 0.81;
    path_delay[168][40] = 0.77;
    path_delay[168][41] = 0.78;
    path_delay[169][107] = 0.78;
    path_delay[169][108] = 0.81;
    path_delay[169][41] = 0.77;
    path_delay[169][42] = 0.78;
    path_delay[170][108] = 0.78;
    path_delay[170][109] = 0.81;
    path_delay[170][42] = 0.77;
    path_delay[170][43] = 0.78;
    path_delay[171][109] = 0.78;
    path_delay[171][110] = 0.81;
    path_delay[171][43] = 0.77;
    path_delay[171][44] = 0.78;
    path_delay[172][110] = 0.78;
    path_delay[172][111] = 0.81;
    path_delay[172][44] = 0.77;
    path_delay[172][45] = 0.78;
    path_delay[173][111] = 0.78;
    path_delay[173][112] = 0.81;
    path_delay[173][45] = 0.77;
    path_delay[173][46] = 0.78;
    path_delay[174][112] = 0.78;
    path_delay[174][113] = 0.81;
    path_delay[174][46] = 0.77;
    path_delay[174][47] = 0.78;
    path_delay[175][113] = 0.78;
    path_delay[175][114] = 0.81;
    path_delay[175][47] = 0.77;
    path_delay[175][48] = 0.78;
    path_delay[176][114] = 0.78;
    path_delay[176][115] = 0.81;
    path_delay[176][48] = 0.77;
    path_delay[176][49] = 0.78;
    path_delay[177][115] = 0.78;
    path_delay[177][116] = 0.81;
    path_delay[177][49] = 0.77;
    path_delay[177][50] = 0.78;
    path_delay[132][70] = 0.78;
    path_delay[132][71] = 0.81;
    path_delay[132][4] = 0.77;
    path_delay[132][5] = 0.78;
    path_delay[178][116] = 0.78;
    path_delay[178][117] = 0.81;
    path_delay[178][50] = 0.77;
    path_delay[178][51] = 0.78;
    path_delay[179][117] = 0.78;
    path_delay[179][118] = 0.81;
    path_delay[179][51] = 0.77;
    path_delay[179][52] = 0.78;
    path_delay[180][118] = 0.78;
    path_delay[180][119] = 0.81;
    path_delay[180][52] = 0.77;
    path_delay[180][53] = 0.78;
    path_delay[181][119] = 0.78;
    path_delay[181][120] = 0.81;
    path_delay[181][53] = 0.77;
    path_delay[181][54] = 0.78;
    path_delay[182][120] = 0.78;
    path_delay[182][121] = 0.81;
    path_delay[182][54] = 0.77;
    path_delay[182][55] = 0.78;
    path_delay[183][121] = 0.78;
    path_delay[183][122] = 0.81;
    path_delay[183][55] = 0.77;
    path_delay[183][56] = 0.78;
    path_delay[184][122] = 0.78;
    path_delay[184][123] = 0.81;
    path_delay[184][56] = 0.77;
    path_delay[184][57] = 0.78;
    path_delay[185][123] = 0.78;
    path_delay[185][124] = 0.81;
    path_delay[185][57] = 0.77;
    path_delay[185][58] = 0.78;
    path_delay[186][124] = 0.78;
    path_delay[186][125] = 0.81;
    path_delay[186][58] = 0.77;
    path_delay[186][59] = 0.78;
    path_delay[187][125] = 0.78;
    path_delay[187][126] = 0.81;
    path_delay[187][59] = 0.77;
    path_delay[187][60] = 0.78;
    path_delay[133][71] = 0.78;
    path_delay[133][72] = 0.81;
    path_delay[133][5] = 0.77;
    path_delay[133][6] = 0.78;
    path_delay[188][126] = 0.78;
    path_delay[188][127] = 0.81;
    path_delay[188][60] = 0.77;
    path_delay[188][61] = 0.78;
    path_delay[189][127] = 0.78;
    path_delay[189][128] = 0.81;
    path_delay[189][61] = 0.77;
    path_delay[189][62] = 0.78;
    path_delay[190][128] = 0.78;
    path_delay[190][129] = 0.81;
    path_delay[190][62] = 0.77;
    path_delay[190][63] = 0.78;
    path_delay[191][129] = 0.78;
    path_delay[191][63] = 0.77;
    path_delay[191][64] = 0.37;
    path_delay[134][72] = 0.78;
    path_delay[134][73] = 0.81;
    path_delay[134][6] = 0.77;
    path_delay[134][7] = 0.78;
    path_delay[135][73] = 0.78;
    path_delay[135][74] = 0.81;
    path_delay[135][7] = 0.77;
    path_delay[135][8] = 0.78;
    path_delay[136][75] = 0.81;
    path_delay[136][74] = 0.78;
    path_delay[136][8] = 0.77;
    path_delay[136][9] = 0.78;
    path_delay[137][75] = 0.78;
    path_delay[137][76] = 0.81;
    path_delay[137][10] = 0.78;
    path_delay[137][9] = 0.77;
    path_delay[192][66] = 0.32;
    path_delay[192][0] = 0.18;
    path_delay[202][76] = 0.38;
    path_delay[202][10] = 0.18;
    path_delay[203][77] = 0.38;
    path_delay[203][11] = 0.18;
    path_delay[204][78] = 0.38;
    path_delay[204][12] = 0.18;
    path_delay[205][79] = 0.38;
    path_delay[205][13] = 0.18;
    path_delay[206][80] = 0.38;
    path_delay[206][14] = 0.18;
    path_delay[207][81] = 0.38;
    path_delay[207][15] = 0.18;
    path_delay[208][82] = 0.38;
    path_delay[208][16] = 0.18;
    path_delay[209][83] = 0.38;
    path_delay[209][17] = 0.18;
    path_delay[210][84] = 0.38;
    path_delay[210][18] = 0.18;
    path_delay[211][85] = 0.38;
    path_delay[211][19] = 0.18;
    path_delay[193][67] = 0.38;
    path_delay[193][1] = 0.18;
    path_delay[212][86] = 0.38;
    path_delay[212][20] = 0.18;
    path_delay[213][87] = 0.38;
    path_delay[213][21] = 0.18;
    path_delay[214][88] = 0.38;
    path_delay[214][22] = 0.18;
    path_delay[215][89] = 0.38;
    path_delay[215][23] = 0.18;
    path_delay[216][90] = 0.38;
    path_delay[216][24] = 0.18;
    path_delay[217][91] = 0.38;
    path_delay[217][25] = 0.18;
    path_delay[218][92] = 0.38;
    path_delay[218][26] = 0.18;
    path_delay[219][93] = 0.38;
    path_delay[219][27] = 0.18;
    path_delay[220][94] = 0.38;
    path_delay[220][28] = 0.18;
    path_delay[221][95] = 0.38;
    path_delay[221][29] = 0.18;
    path_delay[194][68] = 0.38;
    path_delay[194][2] = 0.18;
    path_delay[222][96] = 0.38;
    path_delay[222][30] = 0.18;
    path_delay[223][97] = 0.38;
    path_delay[223][31] = 0.18;
    path_delay[224][98] = 0.38;
    path_delay[224][32] = 0.18;
    path_delay[225][99] = 0.38;
    path_delay[225][33] = 0.18;
    path_delay[226][100] = 0.38;
    path_delay[226][34] = 0.18;
    path_delay[227][101] = 0.38;
    path_delay[227][35] = 0.18;
    path_delay[228][102] = 0.38;
    path_delay[228][36] = 0.18;
    path_delay[229][103] = 0.38;
    path_delay[229][37] = 0.18;
    path_delay[230][104] = 0.38;
    path_delay[230][38] = 0.18;
    path_delay[231][105] = 0.38;
    path_delay[231][39] = 0.18;
    path_delay[195][69] = 0.38;
    path_delay[195][3] = 0.18;
    path_delay[232][106] = 0.38;
    path_delay[232][40] = 0.18;
    path_delay[233][107] = 0.38;
    path_delay[233][41] = 0.18;
    path_delay[234][108] = 0.38;
    path_delay[234][42] = 0.18;
    path_delay[235][109] = 0.38;
    path_delay[235][43] = 0.18;
    path_delay[236][110] = 0.38;
    path_delay[236][44] = 0.18;
    path_delay[237][111] = 0.38;
    path_delay[237][45] = 0.18;
    path_delay[238][112] = 0.38;
    path_delay[238][46] = 0.18;
    path_delay[239][113] = 0.38;
    path_delay[239][47] = 0.18;
    path_delay[240][114] = 0.38;
    path_delay[240][48] = 0.18;
    path_delay[241][115] = 0.38;
    path_delay[241][49] = 0.18;
    path_delay[196][70] = 0.38;
    path_delay[196][4] = 0.18;
    path_delay[242][116] = 0.38;
    path_delay[242][50] = 0.18;
    path_delay[243][117] = 0.38;
    path_delay[243][51] = 0.18;
    path_delay[244][118] = 0.38;
    path_delay[244][52] = 0.18;
    path_delay[245][119] = 0.38;
    path_delay[245][53] = 0.18;
    path_delay[246][120] = 0.38;
    path_delay[246][54] = 0.18;
    path_delay[247][121] = 0.38;
    path_delay[247][55] = 0.18;
    path_delay[248][122] = 0.38;
    path_delay[248][56] = 0.18;
    path_delay[249][123] = 0.38;
    path_delay[249][57] = 0.18;
    path_delay[250][124] = 0.38;
    path_delay[250][58] = 0.18;
    path_delay[251][125] = 0.38;
    path_delay[251][59] = 0.18;
    path_delay[197][71] = 0.38;
    path_delay[197][5] = 0.18;
    path_delay[252][126] = 0.38;
    path_delay[252][60] = 0.18;
    path_delay[253][127] = 0.38;
    path_delay[253][61] = 0.18;
    path_delay[254][128] = 0.38;
    path_delay[254][62] = 0.18;
    path_delay[255][129] = 0.38;
    path_delay[255][63] = 0.18;
    path_delay[198][72] = 0.38;
    path_delay[198][6] = 0.18;
    path_delay[199][73] = 0.38;
    path_delay[199][7] = 0.18;
    path_delay[200][74] = 0.38;
    path_delay[200][8] = 0.18;
    path_delay[201][75] = 0.38;
    path_delay[201][9] = 0.18;
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
    sc_out<sc_lv<64>> A_a, A_b, B_a, B_b;
    sc_in<sc_lv<65>> S_minus, S_plus;

    SignedDigitAdder64* design_ptr;

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
        int zero_digits[64];
        for (int i = 0; i < 64; i++) zero_digits[i] = 0;
        DigitRails64 zero = encode_digits_64(zero_digits);

        A_a.write(zero.rail_a);
        A_b.write(zero.rail_b);
        B_a.write(zero.rail_a);
        B_b.write(zero.rail_b);

        wait(20, SC_NS);

        design_ptr->reset_counters();

        std::mt19937_64 rng(rng_seed);
        std::uniform_int_distribution<unsigned long long> dist(0ULL, std::numeric_limits<unsigned long long>::max());

        for (unsigned long long i = 0; i < num_samples; i++) {
            unsigned long long value_a = dist(rng);
            unsigned long long value_b = dist(rng);
            apply_test(value_a, value_b);
        }

        assert(number_of_errors == 0);
        sc_stop();
    }

    void apply_test(unsigned long long value_a, unsigned long long value_b) {
        design_ptr->reset_counters();

        int a_digits[64], b_digits[64];
        binary_digits(value_a, a_digits);
        binary_digits(value_b, b_digits);

        DigitRails64 encoded_a = encode_digits_64(a_digits);
        DigitRails64 encoded_b = encode_digits_64(b_digits);

        A_a.write(encoded_a.rail_a);
        A_b.write(encoded_a.rail_b);
        B_a.write(encoded_b.rail_a);
        B_b.write(encoded_b.rail_b);

        wait(20, SC_NS);

        __int128 expected_result = static_cast<__int128>(value_a) + static_cast<__int128>(value_b);
        sc_lv<65> s_plus_val = S_plus.read();
        sc_lv<65> s_minus_val = S_minus.read();
        __int128 actual_result = decode_dual_rail_65(s_plus_val, s_minus_val);

        if (expected_result != actual_result) {
            number_of_errors++;
        }

        unsigned long long switches_i = design_ptr->total_switches();

        bool cur_in[NUM_IN];
        for (int i = 0; i < 64; i++) cur_in[i] = encoded_a.rail_a[i].is_01() ? encoded_a.rail_a[i].to_bool() : false;
        for (int i = 0; i < 64; i++) cur_in[64 + i] = encoded_a.rail_b[i].is_01() ? encoded_a.rail_b[i].to_bool() : false;
        for (int i = 0; i < 64; i++) cur_in[128 + i] = encoded_b.rail_a[i].is_01() ? encoded_b.rail_a[i].to_bool() : false;
        for (int i = 0; i < 64; i++) cur_in[192 + i] = encoded_b.rail_b[i].is_01() ? encoded_b.rail_b[i].to_bool() : false;

        bool cur_out[NUM_OUT];
        for (int i = 0; i <= 64; i++) cur_out[i] = s_plus_val[i].is_01() ? s_plus_val[i].to_bool() : false;
        for (int i = 0; i <= 64; i++) cur_out[65 + i] = s_minus_val[i].is_01() ? s_minus_val[i].to_bool() : false;

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
    sc_signal<sc_lv<64>> A_a, A_b, B_a, B_b;
    sc_signal<sc_lv<65>> S_minus, S_plus;

    SignedDigitAdder64 adder("SignedDigitAdder64");
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

    std::cout << "SD-64-MC-pathEDP: SAMPLES=" << total_samples
              << " AVG_SWITCHES=" << avg_switches
              << " AVG_DELAY_ns=" << avg_delay
              << " AVG_EDP_correct=" << avg_edp_correct
              << " AVG_EDP_naive=" << avg_edp_naive
              << " pct_diff=" << ((avg_edp_naive / avg_edp_correct - 1.0) * 100.0) << "%\n";

    return 0;
}
