// sdtc_glitch.cpp – 64-bit Signed-Digit / Two's-Complement Carry-Save Adder, gate-level switching activity
//
// Ported from SD_add_TC_4bit.vhdl ("DIG_Add" digit cells wired into the
// "main" entity, generalized from 4 digits to 64). SD_a, SD_b are unsigned
// 64-bit operands and TC is a two's-complement (signed) 64-bit operand.
// Digit i is a single 1-bit full adder over (TC(i), SD_b(i), SD_a(i)) with
// NO carry chain between digits (unlike RCA/CLA), so the whole adder
// settles in one full-adder's gate depth no matter how wide it is. Its
// sum/carry outputs are exposed directly as a redundant (64+1)-bit pair:
//   Sum_a(i)   = sum of digit i         for i = 0..64-1
//   Sum_a(64) = NOT TC(64-1)          (sign-correction top bit)
//   Sum_b(0)   = '0'                    (matches the VHDL's Sum0_b <= '0')
//   Sum_b(i+1) = carry of digit i       for i = 0..64-1
// The testbench checks the redundant-representation identity
// Sum_a + Sum_b - 2^64 == SD_a + SD_b + TC_signed.
//
// At N=64, SD_a/SD_b/TC values and the 65-bit Sum_a/Sum_b readback all
// need more range than a 64-bit integer can hold without overflow, so
// this file uses __int128/__uint128_t for value and correctness-check
// arithmetic instead of the long long/unsigned long long used at
// smaller widths.
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
static sc_lv<64> make_64bit_vector(unsigned long long value) {
    sc_lv<64> bits;
    for (int i = 0; i < 64; i++) {
        bits[i] = ((value >> i) & 1ULL) != 0ULL;
    }
    return bits;
}

static unsigned long long to_unsigned_64bit(const sc_lv<64>& bits) {
    unsigned long long value = 0;
    for (int i = 0; i < 64; i++) {
        if (bits[i].is_01() && bits[i].to_bool()) {
            value += (1ULL << i);
        }
    }
    return value;
}

static __uint128_t to_unsigned_65bit(const sc_lv<65>& bits) {
    __uint128_t value = 0;
    for (int i = 0; i < 65; i++) {
        if (bits[i].is_01() && bits[i].to_bool()) {
            value += (static_cast<__uint128_t>(1) << i);
        }
    }
    return value;
}

// TC is a two's-complement N-bit operand; decode its signed integer value.
static __int128 to_signed_twos_complement_64bit(unsigned long long value) {
    __int128 v = static_cast<__int128>(value);
    if (value & (1ULL << (64 - 1))) {
        v -= (static_cast<__int128>(1) << 64);
    }
    return v;
}

// ==========================================
// 1a. GENERIC GATE-LEVEL PRIMITIVE
//     Each instance is exactly one 2-input AND/OR/XOR gate.
//     It is sensitive to each of its two inputs independently, so
//     inputs arriving at different simulated times each trigger their
//     own evaluation (and are counted as separate switches if the
//     gate's output value changes), instead of being batched into one
//     combined evaluation. Output commit is delayed by 1ns from the
//     evaluation that caused the change, modeling real gate delay.
// ==========================================
enum GateOp { GATE_AND, GATE_OR, GATE_XOR };

struct Gate;
static void register_gate(Gate* g);
struct GateNot;
static void register_gatenot(GateNot* g);

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

// ==========================================
// 1b. GATE-LEVEL INVERTER PRIMITIVE
//     Same event/delay/switch-counting behaviour as Gate, but for the
//     single NOT gate that forms the top digit's sign-correction bit
//     (Sum_a[N] = NOT TC[N-1]). Unlike AND/OR/XOR, NOT(false) != false,
//     so eval() must also run once at t=0 to settle correctly even if
//     its input never changes away from its power-up default -- which
//     is why (unlike Gate above) it does NOT call dont_initialize().
// ==========================================
SC_MODULE(GateNot) {
    sc_in<bool> in1;
    sc_out<bool> out;

    sc_event ev_commit;
    bool old_value;
    bool pending_value;
    unsigned int calls;
    uint64_t switches;

    SC_CTOR(GateNot) : old_value(false), pending_value(false), calls(0), switches(0) {
        SC_METHOD(eval);
        sensitive << in1;

        SC_METHOD(commit);
        dont_initialize();
        sensitive << ev_commit;

        register_gatenot(this);
    }

    void reset_switches() { switches = 0; }

    void eval() {
        calls++;
        bool next = !in1.read();
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
static std::vector<GateNot*> g_all_gatenots;
static void register_gatenot(GateNot* g) { g_all_gatenots.push_back(g); }
static void reset_all_gate_switches() {
    for (auto* g : g_all_gates) g->reset_switches();
    for (auto* g : g_all_gatenots) g->reset_switches();
}

// ==========================================
// 2. GATE-LEVEL FULL ADDER MODULE (5 gates)
//    s1   = XOR(a,b)        c1  = AND(a,b)
//    sum  = XOR(s1,cin)     c2  = AND(s1,cin)
//    cout = OR(c1,c2)
// ==========================================
SC_MODULE(FullAdder) {
    sc_in<bool> a, b, cin;
    sc_out<bool> sum, cout;

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

    ~FullAdder() {
        delete g_xor1; delete g_and1; delete g_xor2; delete g_and2; delete g_or1;
    }
};

// ==========================================
// 3. 64-BIT SIGNED-DIGIT / TWO'S-COMPLEMENT ADDER
//    64 independent full adders, one per digit, with NO carry chain
//    between them. Gate count: 64 x 5 (full adders) + 1 (NOT) = 321
// ==========================================
SC_MODULE(SdTcAdder64) {
    sc_in<sc_lv<64>> SD_a;
    sc_in<sc_lv<64>> SD_b;
    sc_in<sc_lv<64>> TC;

    sc_out<sc_lv<65>> Sum_a;
    sc_out<sc_lv<65>> Sum_b;

    sc_signal<bool> sda_bit[64];
    sc_signal<bool> sdb_bit[64];
    sc_signal<bool> tc_bit[64];
    sc_signal<bool> sum_bit[64];
    sc_signal<bool> carry_bit[64];
    sc_signal<bool> const_zero;
    sc_signal<bool> top_bit;

    FullAdder* fa[64];
    GateNot* g_not_top;

    SC_CTOR(SdTcAdder64) {
        for (int i = 0; i < 64; i++) {
            std::string name = "FA_" + std::to_string(i);
            fa[i] = new FullAdder(name.c_str());

            fa[i]->a(tc_bit[i]);
            fa[i]->b(sdb_bit[i]);
            fa[i]->cin(sda_bit[i]);
            fa[i]->sum(sum_bit[i]);
            fa[i]->cout(carry_bit[i]);
        }

        g_not_top = new GateNot("NOT_TOP");
        g_not_top->in1(tc_bit[64 - 1]);
        g_not_top->out(top_bit);

        SC_METHOD(split_inputs);
        sensitive << SD_a << SD_b << TC;

        SC_METHOD(combine_outputs);
        for (int i = 0; i < 64; i++) {
            sensitive << sum_bit[i];
            sensitive << carry_bit[i];
        }
        sensitive << top_bit;
    }

    void split_inputs() {
        sc_lv<64> value_sda = SD_a.read();
        sc_lv<64> value_sdb = SD_b.read();
        sc_lv<64> value_tc = TC.read();

        for (int i = 0; i < 64; i++) {
            sda_bit[i].write(value_sda[i].is_01() ? value_sda[i].to_bool() : false);
            sdb_bit[i].write(value_sdb[i].is_01() ? value_sdb[i].to_bool() : false);
            tc_bit[i].write(value_tc[i].is_01() ? value_tc[i].to_bool() : false);
        }
    }

    void combine_outputs() {
        sc_lv<65> value_sum_a;
        sc_lv<65> value_sum_b;

        for (int i = 0; i < 64; i++) {
            value_sum_a[i] = sum_bit[i].read();
            value_sum_b[i + 1] = carry_bit[i].read();
        }
        value_sum_a[64] = top_bit.read();
        value_sum_b[0] = const_zero.read();

        Sum_a.write(value_sum_a);
        Sum_b.write(value_sum_b);
    }

    uint64_t total_switches() const {
        uint64_t total = 0;
        for (int i = 0; i < 64; i++) {
            total += fa[i]->total_switches();
        }
        total += g_not_top->switches;
        return total;
    }

    ~SdTcAdder64() {
        for (int i = 0; i < 64; i++) {
            delete fa[i];
        }
        delete g_not_top;
    }
};

// ==========================================
// 4. TESTBENCH -- Monte Carlo random sampling
//    Draws num_samples uniformly random (SD_a, SD_b, TC) triples instead
//    of exhaustively covering the whole input space: at 64 bits exhaustive
//    coverage (2^64 x 2^64 x 2^64 cases) is astronomically infeasible, so
//    Monte Carlo sampling is the only tractable option.
//
//    SD_a, SD_b are unsigned; TC is two's-complement signed. Correctness
//    is checked against the redundant-representation identity
//    Sum_a + Sum_b - 2^N == SD_a + SD_b + TC_signed (see header comment).
// ==========================================

// ============================================================================
// Real OpenSTA (Nangate45) max delay per (startpoint pin, endpoint pin),
// extracted from all_critical_paths.csv for SDTC-PPM-64 (385 pairs).
// Input pin index: 0..63=SD_a[i], 64..127=SD_b[i], 128..191=TC[i].
// Output pin index: 0..64=Sum_a[i], 65..129=Sum_b[i].
// ============================================================================
static const int NUM_IN = 192;
static const int NUM_OUT = 130;
static double path_delay[NUM_IN][NUM_OUT];

static void init_path_delay_table() {
    for (int i = 0; i < NUM_IN; i++)
        for (int o = 0; o < NUM_OUT; o++)
            path_delay[i][o] = 0.0;

    path_delay[0][0] = 0.18;
    path_delay[0][66] = 0.25;
    path_delay[10][10] = 0.18;
    path_delay[10][76] = 0.25;
    path_delay[11][11] = 0.18;
    path_delay[11][77] = 0.25;
    path_delay[12][12] = 0.18;
    path_delay[12][78] = 0.25;
    path_delay[13][13] = 0.18;
    path_delay[13][79] = 0.25;
    path_delay[14][14] = 0.18;
    path_delay[14][80] = 0.25;
    path_delay[15][15] = 0.18;
    path_delay[15][81] = 0.25;
    path_delay[16][16] = 0.18;
    path_delay[16][82] = 0.25;
    path_delay[17][17] = 0.18;
    path_delay[17][83] = 0.25;
    path_delay[18][18] = 0.18;
    path_delay[18][84] = 0.25;
    path_delay[19][19] = 0.18;
    path_delay[19][85] = 0.25;
    path_delay[1][1] = 0.18;
    path_delay[1][67] = 0.25;
    path_delay[20][20] = 0.18;
    path_delay[20][86] = 0.25;
    path_delay[21][21] = 0.18;
    path_delay[21][87] = 0.25;
    path_delay[22][22] = 0.18;
    path_delay[22][88] = 0.25;
    path_delay[23][23] = 0.18;
    path_delay[23][89] = 0.25;
    path_delay[24][24] = 0.18;
    path_delay[24][90] = 0.25;
    path_delay[25][25] = 0.18;
    path_delay[25][91] = 0.25;
    path_delay[26][26] = 0.18;
    path_delay[26][92] = 0.25;
    path_delay[27][27] = 0.18;
    path_delay[27][93] = 0.25;
    path_delay[28][28] = 0.18;
    path_delay[28][94] = 0.25;
    path_delay[29][29] = 0.18;
    path_delay[29][95] = 0.25;
    path_delay[2][2] = 0.18;
    path_delay[2][68] = 0.25;
    path_delay[30][30] = 0.18;
    path_delay[30][96] = 0.25;
    path_delay[31][31] = 0.18;
    path_delay[31][97] = 0.25;
    path_delay[32][32] = 0.18;
    path_delay[32][98] = 0.25;
    path_delay[33][33] = 0.18;
    path_delay[33][99] = 0.25;
    path_delay[34][34] = 0.18;
    path_delay[34][100] = 0.25;
    path_delay[35][35] = 0.18;
    path_delay[35][101] = 0.25;
    path_delay[36][36] = 0.18;
    path_delay[36][102] = 0.25;
    path_delay[37][37] = 0.18;
    path_delay[37][103] = 0.25;
    path_delay[38][38] = 0.18;
    path_delay[38][104] = 0.25;
    path_delay[39][39] = 0.18;
    path_delay[39][105] = 0.25;
    path_delay[3][3] = 0.18;
    path_delay[3][69] = 0.25;
    path_delay[40][40] = 0.18;
    path_delay[40][106] = 0.25;
    path_delay[41][41] = 0.18;
    path_delay[41][107] = 0.25;
    path_delay[42][42] = 0.18;
    path_delay[42][108] = 0.25;
    path_delay[43][43] = 0.18;
    path_delay[43][109] = 0.25;
    path_delay[44][44] = 0.18;
    path_delay[44][110] = 0.25;
    path_delay[45][45] = 0.18;
    path_delay[45][111] = 0.25;
    path_delay[46][46] = 0.18;
    path_delay[46][112] = 0.25;
    path_delay[47][47] = 0.18;
    path_delay[47][113] = 0.25;
    path_delay[48][48] = 0.18;
    path_delay[48][114] = 0.25;
    path_delay[49][49] = 0.18;
    path_delay[49][115] = 0.25;
    path_delay[4][4] = 0.18;
    path_delay[4][70] = 0.25;
    path_delay[50][50] = 0.18;
    path_delay[50][116] = 0.25;
    path_delay[51][51] = 0.18;
    path_delay[51][117] = 0.25;
    path_delay[52][52] = 0.18;
    path_delay[52][118] = 0.25;
    path_delay[53][53] = 0.18;
    path_delay[53][119] = 0.25;
    path_delay[54][54] = 0.18;
    path_delay[54][120] = 0.25;
    path_delay[55][55] = 0.18;
    path_delay[55][121] = 0.25;
    path_delay[56][56] = 0.18;
    path_delay[56][122] = 0.25;
    path_delay[57][57] = 0.18;
    path_delay[57][123] = 0.25;
    path_delay[58][58] = 0.18;
    path_delay[58][124] = 0.25;
    path_delay[59][59] = 0.18;
    path_delay[59][125] = 0.25;
    path_delay[5][5] = 0.18;
    path_delay[5][71] = 0.25;
    path_delay[60][60] = 0.18;
    path_delay[60][126] = 0.25;
    path_delay[61][61] = 0.18;
    path_delay[61][127] = 0.25;
    path_delay[62][62] = 0.18;
    path_delay[62][128] = 0.25;
    path_delay[63][63] = 0.18;
    path_delay[63][129] = 0.25;
    path_delay[6][6] = 0.18;
    path_delay[6][72] = 0.25;
    path_delay[7][7] = 0.18;
    path_delay[7][73] = 0.25;
    path_delay[8][8] = 0.18;
    path_delay[8][74] = 0.25;
    path_delay[9][9] = 0.18;
    path_delay[9][75] = 0.25;
    path_delay[64][0] = 0.37;
    path_delay[64][66] = 0.46;
    path_delay[74][10] = 0.37;
    path_delay[74][76] = 0.46;
    path_delay[75][11] = 0.37;
    path_delay[75][77] = 0.46;
    path_delay[76][12] = 0.37;
    path_delay[76][78] = 0.46;
    path_delay[77][13] = 0.37;
    path_delay[77][79] = 0.46;
    path_delay[78][14] = 0.37;
    path_delay[78][80] = 0.46;
    path_delay[79][15] = 0.37;
    path_delay[79][81] = 0.46;
    path_delay[80][16] = 0.37;
    path_delay[80][82] = 0.46;
    path_delay[81][17] = 0.37;
    path_delay[81][83] = 0.46;
    path_delay[82][18] = 0.37;
    path_delay[82][84] = 0.46;
    path_delay[83][19] = 0.37;
    path_delay[83][85] = 0.46;
    path_delay[65][1] = 0.37;
    path_delay[65][67] = 0.46;
    path_delay[84][20] = 0.37;
    path_delay[84][86] = 0.46;
    path_delay[85][21] = 0.37;
    path_delay[85][87] = 0.46;
    path_delay[86][22] = 0.37;
    path_delay[86][88] = 0.46;
    path_delay[87][23] = 0.37;
    path_delay[87][89] = 0.46;
    path_delay[88][24] = 0.37;
    path_delay[88][90] = 0.46;
    path_delay[89][25] = 0.37;
    path_delay[89][91] = 0.46;
    path_delay[90][26] = 0.37;
    path_delay[90][92] = 0.46;
    path_delay[91][27] = 0.37;
    path_delay[91][93] = 0.46;
    path_delay[92][28] = 0.37;
    path_delay[92][94] = 0.46;
    path_delay[93][29] = 0.37;
    path_delay[93][95] = 0.46;
    path_delay[66][2] = 0.37;
    path_delay[66][68] = 0.46;
    path_delay[94][30] = 0.37;
    path_delay[94][96] = 0.46;
    path_delay[95][31] = 0.37;
    path_delay[95][97] = 0.46;
    path_delay[96][32] = 0.37;
    path_delay[96][98] = 0.46;
    path_delay[97][33] = 0.37;
    path_delay[97][99] = 0.46;
    path_delay[98][34] = 0.37;
    path_delay[98][100] = 0.46;
    path_delay[99][35] = 0.37;
    path_delay[99][101] = 0.46;
    path_delay[100][36] = 0.37;
    path_delay[100][102] = 0.46;
    path_delay[101][37] = 0.37;
    path_delay[101][103] = 0.46;
    path_delay[102][38] = 0.37;
    path_delay[102][104] = 0.46;
    path_delay[103][39] = 0.37;
    path_delay[103][105] = 0.46;
    path_delay[67][3] = 0.37;
    path_delay[67][69] = 0.46;
    path_delay[104][40] = 0.37;
    path_delay[104][106] = 0.46;
    path_delay[105][41] = 0.37;
    path_delay[105][107] = 0.46;
    path_delay[106][42] = 0.37;
    path_delay[106][108] = 0.46;
    path_delay[107][43] = 0.37;
    path_delay[107][109] = 0.46;
    path_delay[108][44] = 0.37;
    path_delay[108][110] = 0.46;
    path_delay[109][45] = 0.37;
    path_delay[109][111] = 0.46;
    path_delay[110][46] = 0.37;
    path_delay[110][112] = 0.46;
    path_delay[111][47] = 0.37;
    path_delay[111][113] = 0.46;
    path_delay[112][48] = 0.37;
    path_delay[112][114] = 0.46;
    path_delay[113][49] = 0.37;
    path_delay[113][115] = 0.46;
    path_delay[68][4] = 0.37;
    path_delay[68][70] = 0.46;
    path_delay[114][50] = 0.37;
    path_delay[114][116] = 0.46;
    path_delay[115][51] = 0.37;
    path_delay[115][117] = 0.46;
    path_delay[116][52] = 0.37;
    path_delay[116][118] = 0.46;
    path_delay[117][53] = 0.37;
    path_delay[117][119] = 0.46;
    path_delay[118][54] = 0.37;
    path_delay[118][120] = 0.46;
    path_delay[119][55] = 0.37;
    path_delay[119][121] = 0.46;
    path_delay[120][56] = 0.37;
    path_delay[120][122] = 0.46;
    path_delay[121][57] = 0.37;
    path_delay[121][123] = 0.46;
    path_delay[122][58] = 0.37;
    path_delay[122][124] = 0.46;
    path_delay[123][59] = 0.37;
    path_delay[123][125] = 0.46;
    path_delay[69][5] = 0.37;
    path_delay[69][71] = 0.46;
    path_delay[124][60] = 0.37;
    path_delay[124][126] = 0.46;
    path_delay[125][61] = 0.37;
    path_delay[125][127] = 0.46;
    path_delay[126][62] = 0.37;
    path_delay[126][128] = 0.46;
    path_delay[127][63] = 0.37;
    path_delay[127][129] = 0.46;
    path_delay[70][6] = 0.37;
    path_delay[70][72] = 0.46;
    path_delay[71][7] = 0.37;
    path_delay[71][73] = 0.46;
    path_delay[72][8] = 0.37;
    path_delay[72][74] = 0.46;
    path_delay[73][9] = 0.37;
    path_delay[73][75] = 0.46;
    path_delay[128][0] = 0.36;
    path_delay[128][66] = 0.45;
    path_delay[138][10] = 0.36;
    path_delay[138][76] = 0.45;
    path_delay[139][11] = 0.36;
    path_delay[139][77] = 0.45;
    path_delay[140][12] = 0.36;
    path_delay[140][78] = 0.45;
    path_delay[141][13] = 0.36;
    path_delay[141][79] = 0.45;
    path_delay[142][14] = 0.36;
    path_delay[142][80] = 0.45;
    path_delay[143][15] = 0.36;
    path_delay[143][81] = 0.45;
    path_delay[144][16] = 0.36;
    path_delay[144][82] = 0.45;
    path_delay[145][17] = 0.36;
    path_delay[145][83] = 0.45;
    path_delay[146][18] = 0.36;
    path_delay[146][84] = 0.45;
    path_delay[147][19] = 0.36;
    path_delay[147][85] = 0.45;
    path_delay[129][1] = 0.36;
    path_delay[129][67] = 0.45;
    path_delay[148][20] = 0.36;
    path_delay[148][86] = 0.45;
    path_delay[149][21] = 0.36;
    path_delay[149][87] = 0.45;
    path_delay[150][22] = 0.36;
    path_delay[150][88] = 0.45;
    path_delay[151][23] = 0.36;
    path_delay[151][89] = 0.45;
    path_delay[152][24] = 0.36;
    path_delay[152][90] = 0.45;
    path_delay[153][25] = 0.36;
    path_delay[153][91] = 0.45;
    path_delay[154][26] = 0.36;
    path_delay[154][92] = 0.45;
    path_delay[155][27] = 0.36;
    path_delay[155][93] = 0.45;
    path_delay[156][28] = 0.36;
    path_delay[156][94] = 0.45;
    path_delay[157][29] = 0.36;
    path_delay[157][95] = 0.45;
    path_delay[130][2] = 0.36;
    path_delay[130][68] = 0.45;
    path_delay[158][30] = 0.36;
    path_delay[158][96] = 0.45;
    path_delay[159][31] = 0.36;
    path_delay[159][97] = 0.45;
    path_delay[160][32] = 0.36;
    path_delay[160][98] = 0.45;
    path_delay[161][33] = 0.36;
    path_delay[161][99] = 0.45;
    path_delay[162][34] = 0.36;
    path_delay[162][100] = 0.45;
    path_delay[163][35] = 0.36;
    path_delay[163][101] = 0.45;
    path_delay[164][36] = 0.36;
    path_delay[164][102] = 0.45;
    path_delay[165][37] = 0.36;
    path_delay[165][103] = 0.45;
    path_delay[166][38] = 0.36;
    path_delay[166][104] = 0.45;
    path_delay[167][39] = 0.36;
    path_delay[167][105] = 0.45;
    path_delay[131][3] = 0.36;
    path_delay[131][69] = 0.45;
    path_delay[168][40] = 0.36;
    path_delay[168][106] = 0.45;
    path_delay[169][41] = 0.36;
    path_delay[169][107] = 0.45;
    path_delay[170][42] = 0.36;
    path_delay[170][108] = 0.45;
    path_delay[171][43] = 0.36;
    path_delay[171][109] = 0.45;
    path_delay[172][44] = 0.36;
    path_delay[172][110] = 0.45;
    path_delay[173][45] = 0.36;
    path_delay[173][111] = 0.45;
    path_delay[174][46] = 0.36;
    path_delay[174][112] = 0.45;
    path_delay[175][47] = 0.36;
    path_delay[175][113] = 0.45;
    path_delay[176][48] = 0.36;
    path_delay[176][114] = 0.45;
    path_delay[177][49] = 0.36;
    path_delay[177][115] = 0.45;
    path_delay[132][4] = 0.36;
    path_delay[132][70] = 0.45;
    path_delay[178][50] = 0.36;
    path_delay[178][116] = 0.45;
    path_delay[179][51] = 0.36;
    path_delay[179][117] = 0.45;
    path_delay[180][52] = 0.36;
    path_delay[180][118] = 0.45;
    path_delay[181][53] = 0.36;
    path_delay[181][119] = 0.45;
    path_delay[182][54] = 0.36;
    path_delay[182][120] = 0.45;
    path_delay[183][55] = 0.36;
    path_delay[183][121] = 0.45;
    path_delay[184][56] = 0.36;
    path_delay[184][122] = 0.45;
    path_delay[185][57] = 0.36;
    path_delay[185][123] = 0.45;
    path_delay[186][58] = 0.36;
    path_delay[186][124] = 0.45;
    path_delay[187][59] = 0.36;
    path_delay[187][125] = 0.45;
    path_delay[133][5] = 0.36;
    path_delay[133][71] = 0.45;
    path_delay[188][60] = 0.36;
    path_delay[188][126] = 0.45;
    path_delay[189][61] = 0.36;
    path_delay[189][127] = 0.45;
    path_delay[190][62] = 0.36;
    path_delay[190][128] = 0.45;
    path_delay[191][63] = 0.36;
    path_delay[191][64] = 0.01;
    path_delay[191][129] = 0.45;
    path_delay[134][6] = 0.36;
    path_delay[134][72] = 0.45;
    path_delay[135][7] = 0.36;
    path_delay[135][73] = 0.45;
    path_delay[136][8] = 0.36;
    path_delay[136][74] = 0.45;
    path_delay[137][9] = 0.36;
    path_delay[137][75] = 0.45;
}

// ============================================================================
// TESTBENCH -- Monte Carlo, per-sample EDP. SD_a, SD_b, TC each drawn as
// independent uniform random N-bit values (same as the original Monte
// Carlo methodology -- SD_a/SD_b are not drawn as independent redundant
// digits here, just plain N-bit values, so there's no NAF-style encoding
// question for this adder the way there was for SD).
// ============================================================================
SC_MODULE(Testbench) {
    sc_out<sc_lv<64>> SD_a, SD_b, TC;
    sc_in<sc_lv<65>> Sum_a, Sum_b;

    SdTcAdder64* sdtc_ptr;

    unsigned int number_of_errors;
    unsigned long long num_samples;
    unsigned long long rng_seed;

    double sum_switches, sum_delay, sum_edp;
    unsigned long long prev_sda, prev_sdb, prev_tc;
    __uint128_t prev_sum_a, prev_sum_b;

    SC_CTOR(Testbench)
        : sdtc_ptr(nullptr), number_of_errors(0), num_samples(1000000ULL), rng_seed(42ULL),
          sum_switches(0.0), sum_delay(0.0), sum_edp(0.0),
          prev_sda(0), prev_sdb(0), prev_tc(0), prev_sum_a(0), prev_sum_b(0)
    {
        SC_THREAD(stimulus);
    }

    void stimulus() {
        std::mt19937_64 rng(rng_seed);
        std::uniform_int_distribution<unsigned long long> dist(0ULL, 9223372036854775807ULL);

        SD_a.write(make_64bit_vector(0));
        SD_b.write(make_64bit_vector(0));
        TC.write(make_64bit_vector(0));
        wait(200, SC_NS);
        prev_sda = 0; prev_sdb = 0; prev_tc = 0;
        prev_sum_a = to_unsigned_65bit(Sum_a.read());
        prev_sum_b = to_unsigned_65bit(Sum_b.read());

        for (unsigned long long i = 0; i < num_samples; i++) {
            unsigned long long sd_pattern = dist(rng);
            unsigned long long sda_value = sd_pattern & ((static_cast<unsigned long long>(1) << (64-1)) - 1);  // digits 0/+1 below the top, read as digit = SD_a + SD_b - 1 (dual-rail, as in the SD adder)
            unsigned long long sdb_value = ((static_cast<unsigned long long>(1) << (64-1)) - 1) | ((sd_pattern & (static_cast<unsigned long long>(1) << (64-1))) ? 0 : (static_cast<unsigned long long>(1) << (64-1)));  // lower digits (a,b)=(bit,1); top digit 0/-1 = (0,1)/(0,0)
            unsigned long long tc_value  = dist(rng);
            apply_test(sda_value, sdb_value, tc_value);
        }

        assert(number_of_errors == 0);
        sc_stop();
    }

    void apply_test(unsigned long long sda_value, unsigned long long sdb_value, unsigned long long tc_value) {
        reset_all_gate_switches();

        SD_a.write(make_64bit_vector(sda_value));
        SD_b.write(make_64bit_vector(sdb_value));
        TC.write(make_64bit_vector(tc_value));

        wait(200, SC_NS);

        __int128 expected_result = static_cast<__int128>(sda_value)
                                  + static_cast<__int128>(sdb_value)
                                  + to_signed_twos_complement_64bit(tc_value);

        sc_lv<65> sum_a_val = Sum_a.read();
        sc_lv<65> sum_b_val = Sum_b.read();
        __uint128_t actual_sum_a = to_unsigned_65bit(sum_a_val);
        __uint128_t actual_sum_b = to_unsigned_65bit(sum_b_val);

        __int128 actual_result = static_cast<__int128>(actual_sum_a)
                                + static_cast<__int128>(actual_sum_b)
                                - (static_cast<__int128>(1) << 64);

        if (expected_result != actual_result) {
            number_of_errors++;
        }

        unsigned int switches_i = sdtc_ptr->total_switches();

        bool in_changed[NUM_IN];
        for (int k = 0; k < 64; k++) in_changed[k] = (((sda_value >> k) & 1ULL) != ((prev_sda >> k) & 1ULL));
        for (int k = 0; k < 64; k++) in_changed[64 + k] = (((sdb_value >> k) & 1ULL) != ((prev_sdb >> k) & 1ULL));
        for (int k = 0; k < 64; k++) in_changed[128 + k] = (((tc_value >> k) & 1ULL) != ((prev_tc >> k) & 1ULL));

        bool out_changed[NUM_OUT];
        for (int k = 0; k <= 64; k++) out_changed[k] = (((actual_sum_a >> k) & 1) != ((prev_sum_a >> k) & 1));
        for (int k = 0; k <= 64; k++) out_changed[65 + k] = (((actual_sum_b >> k) & 1) != ((prev_sum_b >> k) & 1));

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

        prev_sda = sda_value;
        prev_sdb = sdb_value;
        prev_tc = tc_value;
        prev_sum_a = actual_sum_a;
        prev_sum_b = actual_sum_b;
    }
};

// ============================================================================
// MAIN -- Monte Carlo, auto-parallel across all CPU cores
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
    sc_signal<sc_lv<64>> SD_a, SD_b, TC;
    sc_signal<sc_lv<65>> Sum_a, Sum_b;

    SdTcAdder64 sdtc("SDTC64");
    Testbench tb("TB");
    tb.sdtc_ptr = &sdtc;

    sdtc.SD_a(SD_a); sdtc.SD_b(SD_b); sdtc.TC(TC);
    sdtc.Sum_a(Sum_a); sdtc.Sum_b(Sum_b);

    tb.SD_a(SD_a); tb.SD_b(SD_b); tb.TC(TC);
    tb.Sum_a(Sum_a); tb.Sum_b(Sum_b);

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

    std::cout << "SDTC-PPM-64-MC-pathEDP: SAMPLES=" << total_samples
              << " AVG_SWITCHES=" << avg_switches
              << " AVG_DELAY_ns=" << avg_delay
              << " AVG_EDP_correct=" << avg_edp_correct
              << " AVG_EDP_naive=" << avg_edp_naive
              << " pct_diff=" << ((avg_edp_naive / avg_edp_correct - 1.0) * 100.0) << "%\n";

    return 0;
}
