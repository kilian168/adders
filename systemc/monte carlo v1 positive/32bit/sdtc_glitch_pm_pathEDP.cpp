// sdtc_glitch.cpp – 32-bit Signed-Digit / Two's-Complement Carry-Save Adder, gate-level switching activity
//
// Ported from SD_add_TC_4bit.vhdl ("DIG_Add" digit cells wired into the
// "main" entity, generalized from 4 digits to 32). SD_a/SD_b together are a
// PM (plus-minus) signed-digit number: digit i = SD_a(i) - SD_b(i) in
// {-1,0,1} ("10"=+1, "01"=-1, "00"/"11"=0), which sums to plain
// SD_a_unsigned - SD_b_unsigned. TC is a two's-complement (signed) 32-bit
// operand. Digit i is a single digit cell over (TC(i), SD_b(i), SD_a(i))
// with NO carry chain between digits (unlike RCA/CLA), so the whole adder
// settles in one cell's gate depth no matter how wide it is. Its
// sum/carry outputs are exposed directly as a redundant (32+1)-bit pair:
//   Sum_a(i)   = sum of digit i         for i = 0..32-1
//   Sum_a(32) = NOT TC(32-1)          (sign-correction top bit)
//   Sum_b(0)   = '0'                    (matches the VHDL's Sum0_b <= '0')
//   Sum_b(i+1) = carry of digit i       for i = 0..32-1
// Each digit contributes 2*carry - sum (not 2*carry + sum) to the total, so
// the testbench checks Sum_b - Sum_a + 2^32*(TC(32-1)?-1:+1) == SD_a - SD_b + TC_signed.
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

static unsigned long long to_unsigned_33bit(const sc_lv<33>& bits) {
    unsigned long long value = 0;
    for (int i = 0; i < 33; i++) {
        if (bits[i].is_01() && bits[i].to_bool()) {
            value += (1ULL << i);
        }
    }
    return value;
}

// TC is a two's-complement N-bit operand; decode its signed integer value.
static long long to_signed_twos_complement_32bit(unsigned int value) {
    long long v = static_cast<long long>(value);
    if (value & (1U << (32 - 1))) {
        v -= (1LL << 32);
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
    unsigned int switches;

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
// 2. GATE-LEVEL DIGIT CELL (7 gates) -- ported from the VHDL
//    "main"/DIG_Add entity's signed-digit + two's-complement digit
//    equations (class kept as "FullAdder" so every call site below is
//    unchanged; it no longer computes a textbook majority carry):
//      s1    = XOR(cin,b)          notB  = NOT(b)
//      sum   = XOR(s1,a)           term1 = AND(cin,notB)
//      notS1 = NOT(s1)             term2 = AND(notS1,a)
//      cout  = OR(term1,term2)
//    Port mapping is unchanged: a=TC(i), b=SD_b(i), cin=SD_a(i).
// ==========================================
SC_MODULE(FullAdder) {
    sc_in<bool> a, b, cin;
    sc_out<bool> sum, cout;

    sc_signal<bool> notB, s1, c1, c2;

    GateNot *g_notB;
    Gate *g_xor1, *g_and1, *g_xor2, *g_and2, *g_or1;

    SC_CTOR(FullAdder) {
        g_notB = new GateNot("NOTB");
        g_notB->in1(b); g_notB->out(notB);

        g_xor1 = new Gate("XOR1", GATE_XOR);
        g_xor1->in1(a); g_xor1->in2(notB); g_xor1->out(s1);

        g_and1 = new Gate("AND1", GATE_AND);
        g_and1->in1(a); g_and1->in2(notB); g_and1->out(c1);

        g_xor2 = new Gate("XOR2", GATE_XOR);
        g_xor2->in1(s1); g_xor2->in2(cin); g_xor2->out(sum);

        g_and2 = new Gate("AND2", GATE_AND);
        g_and2->in1(s1); g_and2->in2(cin); g_and2->out(c2);

        g_or1 = new Gate("OR1", GATE_OR);
        g_or1->in1(c1); g_or1->in2(c2); g_or1->out(cout);
    }

    unsigned int total_switches() const {
        return g_notB->switches + g_xor1->switches + g_and1->switches +
               g_xor2->switches + g_and2->switches + g_or1->switches;
    }

    ~FullAdder() {
        delete g_notB; delete g_xor1; delete g_and1;
        delete g_xor2; delete g_and2; delete g_or1;
    }
};

// ==========================================
// 3. 32-BIT SIGNED-DIGIT / TWO'S-COMPLEMENT ADDER
//    32 independent digit cells (7 gates each), one per digit, with NO
//    carry chain between them. Gate count: 32 x 6 (digit cells) + 1 (NOT) = 193
// ==========================================
SC_MODULE(SdTcAdder32) {
    sc_in<sc_lv<32>> SD_a;
    sc_in<sc_lv<32>> SD_b;
    sc_in<sc_lv<32>> TC;

    sc_out<sc_lv<33>> Sum_a;
    sc_out<sc_lv<33>> Sum_b;

    sc_signal<bool> sda_bit[32];
    sc_signal<bool> sdb_bit[32];
    sc_signal<bool> tc_bit[32];
    sc_signal<bool> sum_bit[32];
    sc_signal<bool> carry_bit[32];
    sc_signal<bool> const_zero;
    sc_signal<bool> top_bit;

    FullAdder* fa[32];
    GateNot* g_not_top;

    SC_CTOR(SdTcAdder32) {
        for (int i = 0; i < 32; i++) {
            std::string name = "FA_" + std::to_string(i);
            fa[i] = new FullAdder(name.c_str());

            fa[i]->a(tc_bit[i]);
            fa[i]->b(sdb_bit[i]);
            fa[i]->cin(sda_bit[i]);
            fa[i]->sum(sum_bit[i]);
            fa[i]->cout(carry_bit[i]);
        }

        g_not_top = new GateNot("NOT_TOP");
        g_not_top->in1(tc_bit[32 - 1]);
        g_not_top->out(top_bit);

        SC_METHOD(split_inputs);
        sensitive << SD_a << SD_b << TC;

        SC_METHOD(combine_outputs);
        for (int i = 0; i < 32; i++) {
            sensitive << sum_bit[i];
            sensitive << carry_bit[i];
        }
        sensitive << top_bit;
    }

    void split_inputs() {
        sc_lv<32> value_sda = SD_a.read();
        sc_lv<32> value_sdb = SD_b.read();
        sc_lv<32> value_tc = TC.read();

        for (int i = 0; i < 32; i++) {
            sda_bit[i].write(value_sda[i].is_01() ? value_sda[i].to_bool() : false);
            sdb_bit[i].write(value_sdb[i].is_01() ? value_sdb[i].to_bool() : false);
            tc_bit[i].write(value_tc[i].is_01() ? value_tc[i].to_bool() : false);
        }
    }

    void combine_outputs() {
        sc_lv<33> value_sum_a;
        sc_lv<33> value_sum_b;

        for (int i = 0; i < 32; i++) {
            value_sum_a[i] = sum_bit[i].read();
            value_sum_b[i + 1] = carry_bit[i].read();
        }
        value_sum_a[32] = top_bit.read();
        value_sum_b[0] = const_zero.read();

        Sum_a.write(value_sum_a);
        Sum_b.write(value_sum_b);
    }

    unsigned int total_switches() const {
        unsigned int total = 0;
        for (int i = 0; i < 32; i++) {
            total += fa[i]->total_switches();
        }
        total += g_not_top->switches;
        return total;
    }

    ~SdTcAdder32() {
        for (int i = 0; i < 32; i++) {
            delete fa[i];
        }
        delete g_not_top;
    }
};

// ==========================================
// 4. TESTBENCH -- Monte Carlo random sampling
//    Draws num_samples uniformly random (SD_a, SD_b, TC) triples instead
//    of exhaustively covering the whole input space: at 32 bits exhaustive
//    coverage (2^32 x 2^32 x 2^32 cases) is computationally infeasible, so
//    Monte Carlo sampling is the only tractable option.
//
//    SD_a/SD_b are the PM signed-digit rails (digit i = SD_a(i)-SD_b(i));
//    TC is two's-complement signed. Correctness is checked against the
//    redundant-representation identity Sum_a + Sum_b - (2^(N+1)-1) ==
//    SD_a - SD_b + TC_signed (see header comment and apply_test below).
// ==========================================

// ============================================================================
// Real OpenSTA (Nangate45) max delay per (startpoint pin, endpoint pin),
// extracted from all_critical_paths.csv for SDTC-PM-32 (193 pairs).
// Input pin index: 0..31=SD_a[i], 32..63=SD_b[i], 64..95=TC[i].
// Output pin index: 0..32=Sum_a[i], 33..65=Sum_b[i].
// ============================================================================
static const int NUM_IN = 96;
static const int NUM_OUT = 66;
static double path_delay[NUM_IN][NUM_OUT];

static void init_path_delay_table() {
    for (int i = 0; i < NUM_IN; i++)
        for (int o = 0; o < NUM_OUT; o++)
            path_delay[i][o] = 0.0;

    path_delay[0][0] = 0.36;
    path_delay[0][34] = 0.47;
    path_delay[10][10] = 0.36;
    path_delay[10][44] = 0.47;
    path_delay[11][11] = 0.36;
    path_delay[11][45] = 0.47;
    path_delay[12][12] = 0.36;
    path_delay[12][46] = 0.47;
    path_delay[13][13] = 0.36;
    path_delay[13][47] = 0.47;
    path_delay[14][14] = 0.36;
    path_delay[14][48] = 0.47;
    path_delay[15][15] = 0.36;
    path_delay[15][49] = 0.47;
    path_delay[16][16] = 0.36;
    path_delay[16][50] = 0.47;
    path_delay[17][17] = 0.36;
    path_delay[17][51] = 0.47;
    path_delay[18][18] = 0.36;
    path_delay[18][52] = 0.47;
    path_delay[19][19] = 0.36;
    path_delay[19][53] = 0.47;
    path_delay[1][1] = 0.36;
    path_delay[1][35] = 0.47;
    path_delay[20][20] = 0.36;
    path_delay[20][54] = 0.47;
    path_delay[21][21] = 0.36;
    path_delay[21][55] = 0.47;
    path_delay[22][22] = 0.36;
    path_delay[22][56] = 0.47;
    path_delay[23][23] = 0.36;
    path_delay[23][57] = 0.47;
    path_delay[24][24] = 0.36;
    path_delay[24][58] = 0.47;
    path_delay[25][25] = 0.36;
    path_delay[25][59] = 0.47;
    path_delay[26][26] = 0.36;
    path_delay[26][60] = 0.47;
    path_delay[27][27] = 0.36;
    path_delay[27][61] = 0.47;
    path_delay[28][28] = 0.36;
    path_delay[28][62] = 0.47;
    path_delay[29][29] = 0.36;
    path_delay[29][63] = 0.47;
    path_delay[2][2] = 0.36;
    path_delay[2][36] = 0.47;
    path_delay[30][30] = 0.36;
    path_delay[30][64] = 0.47;
    path_delay[31][31] = 0.36;
    path_delay[31][65] = 0.47;
    path_delay[3][3] = 0.36;
    path_delay[3][37] = 0.47;
    path_delay[4][4] = 0.36;
    path_delay[4][38] = 0.47;
    path_delay[5][5] = 0.36;
    path_delay[5][39] = 0.47;
    path_delay[6][6] = 0.36;
    path_delay[6][40] = 0.47;
    path_delay[7][7] = 0.36;
    path_delay[7][41] = 0.47;
    path_delay[8][8] = 0.36;
    path_delay[8][42] = 0.47;
    path_delay[9][9] = 0.36;
    path_delay[9][43] = 0.47;
    path_delay[32][0] = 0.37;
    path_delay[32][34] = 0.46;
    path_delay[42][10] = 0.37;
    path_delay[42][44] = 0.46;
    path_delay[43][11] = 0.37;
    path_delay[43][45] = 0.46;
    path_delay[44][12] = 0.37;
    path_delay[44][46] = 0.46;
    path_delay[45][13] = 0.37;
    path_delay[45][47] = 0.46;
    path_delay[46][14] = 0.37;
    path_delay[46][48] = 0.46;
    path_delay[47][15] = 0.37;
    path_delay[47][49] = 0.46;
    path_delay[48][16] = 0.37;
    path_delay[48][50] = 0.46;
    path_delay[49][17] = 0.37;
    path_delay[49][51] = 0.46;
    path_delay[50][18] = 0.37;
    path_delay[50][52] = 0.46;
    path_delay[51][19] = 0.37;
    path_delay[51][53] = 0.46;
    path_delay[33][1] = 0.37;
    path_delay[33][35] = 0.46;
    path_delay[52][20] = 0.37;
    path_delay[52][54] = 0.46;
    path_delay[53][21] = 0.37;
    path_delay[53][55] = 0.46;
    path_delay[54][22] = 0.37;
    path_delay[54][56] = 0.46;
    path_delay[55][23] = 0.37;
    path_delay[55][57] = 0.46;
    path_delay[56][24] = 0.37;
    path_delay[56][58] = 0.46;
    path_delay[57][25] = 0.37;
    path_delay[57][59] = 0.46;
    path_delay[58][26] = 0.37;
    path_delay[58][60] = 0.46;
    path_delay[59][27] = 0.37;
    path_delay[59][61] = 0.46;
    path_delay[60][28] = 0.37;
    path_delay[60][62] = 0.46;
    path_delay[61][29] = 0.37;
    path_delay[61][63] = 0.46;
    path_delay[34][2] = 0.37;
    path_delay[34][36] = 0.46;
    path_delay[62][30] = 0.37;
    path_delay[62][64] = 0.46;
    path_delay[63][31] = 0.37;
    path_delay[63][65] = 0.46;
    path_delay[35][3] = 0.37;
    path_delay[35][37] = 0.46;
    path_delay[36][4] = 0.37;
    path_delay[36][38] = 0.46;
    path_delay[37][5] = 0.37;
    path_delay[37][39] = 0.46;
    path_delay[38][6] = 0.37;
    path_delay[38][40] = 0.46;
    path_delay[39][7] = 0.37;
    path_delay[39][41] = 0.46;
    path_delay[40][8] = 0.37;
    path_delay[40][42] = 0.46;
    path_delay[41][9] = 0.37;
    path_delay[41][43] = 0.46;
    path_delay[64][0] = 0.18;
    path_delay[64][34] = 0.25;
    path_delay[74][10] = 0.18;
    path_delay[74][44] = 0.25;
    path_delay[75][11] = 0.18;
    path_delay[75][45] = 0.25;
    path_delay[76][12] = 0.18;
    path_delay[76][46] = 0.25;
    path_delay[77][13] = 0.18;
    path_delay[77][47] = 0.25;
    path_delay[78][14] = 0.18;
    path_delay[78][48] = 0.25;
    path_delay[79][15] = 0.18;
    path_delay[79][49] = 0.25;
    path_delay[80][16] = 0.18;
    path_delay[80][50] = 0.25;
    path_delay[81][17] = 0.18;
    path_delay[81][51] = 0.25;
    path_delay[82][18] = 0.18;
    path_delay[82][52] = 0.25;
    path_delay[83][19] = 0.18;
    path_delay[83][53] = 0.25;
    path_delay[65][1] = 0.18;
    path_delay[65][35] = 0.25;
    path_delay[84][20] = 0.18;
    path_delay[84][54] = 0.25;
    path_delay[85][21] = 0.18;
    path_delay[85][55] = 0.25;
    path_delay[86][22] = 0.18;
    path_delay[86][56] = 0.25;
    path_delay[87][23] = 0.18;
    path_delay[87][57] = 0.25;
    path_delay[88][24] = 0.18;
    path_delay[88][58] = 0.25;
    path_delay[89][25] = 0.18;
    path_delay[89][59] = 0.25;
    path_delay[90][26] = 0.18;
    path_delay[90][60] = 0.25;
    path_delay[91][27] = 0.18;
    path_delay[91][61] = 0.25;
    path_delay[92][28] = 0.18;
    path_delay[92][62] = 0.25;
    path_delay[93][29] = 0.18;
    path_delay[93][63] = 0.25;
    path_delay[66][2] = 0.18;
    path_delay[66][36] = 0.25;
    path_delay[94][30] = 0.18;
    path_delay[94][64] = 0.25;
    path_delay[95][31] = 0.18;
    path_delay[95][32] = 0.01;
    path_delay[95][65] = 0.25;
    path_delay[67][3] = 0.18;
    path_delay[67][37] = 0.25;
    path_delay[68][4] = 0.18;
    path_delay[68][38] = 0.25;
    path_delay[69][5] = 0.18;
    path_delay[69][39] = 0.25;
    path_delay[70][6] = 0.18;
    path_delay[70][40] = 0.25;
    path_delay[71][7] = 0.18;
    path_delay[71][41] = 0.25;
    path_delay[72][8] = 0.18;
    path_delay[72][42] = 0.25;
    path_delay[73][9] = 0.18;
    path_delay[73][43] = 0.25;
}

// ============================================================================
// TESTBENCH -- Monte Carlo, per-sample EDP. SD_a, SD_b, TC each drawn as
// independent uniform random N-bit values (same as the original Monte
// Carlo methodology -- SD_a/SD_b are not drawn as independent redundant
// digits here, just plain N-bit values, so there's no NAF-style encoding
// question for this adder the way there was for SD).
// ============================================================================
SC_MODULE(Testbench) {
    sc_out<sc_lv<32>> SD_a, SD_b, TC;
    sc_in<sc_lv<33>> Sum_a, Sum_b;

    SdTcAdder32* sdtc_ptr;

    unsigned int number_of_errors;
    unsigned long long num_samples;
    unsigned long long rng_seed;

    double sum_switches, sum_delay, sum_edp;
    unsigned int prev_sda, prev_sdb, prev_tc;
    unsigned long long prev_sum_a, prev_sum_b;

    SC_CTOR(Testbench)
        : sdtc_ptr(nullptr), number_of_errors(0), num_samples(1000000ULL), rng_seed(42ULL),
          sum_switches(0.0), sum_delay(0.0), sum_edp(0.0),
          prev_sda(0), prev_sdb(0), prev_tc(0), prev_sum_a(0), prev_sum_b(0)
    {
        SC_THREAD(stimulus);
    }

    void stimulus() {
        std::mt19937_64 rng(rng_seed);
        std::uniform_int_distribution<unsigned long long> dist(0ULL, 2147483647ULL);

        SD_a.write(make_32bit_vector(0));
        SD_b.write(make_32bit_vector(0));
        TC.write(make_32bit_vector(0));
        wait(200, SC_NS);
        prev_sda = 0; prev_sdb = 0; prev_tc = 0;
        prev_sum_a = to_unsigned_33bit(Sum_a.read());
        prev_sum_b = to_unsigned_33bit(Sum_b.read());

        for (unsigned long long i = 0; i < num_samples; i++) {
            unsigned int sd_pattern = static_cast<unsigned int>(dist(rng));
            unsigned int sda_value = sd_pattern & ((static_cast<unsigned int>(1) << (32-1)) - 1);  // top digit -1 (value = SD_a - SD_b)
            unsigned int sdb_value = sd_pattern & (static_cast<unsigned int>(1) << (32-1));
            unsigned int tc_value  = static_cast<unsigned int>(dist(rng));
            apply_test(sda_value, sdb_value, tc_value);
        }

        assert(number_of_errors == 0);
        sc_stop();
    }

    void apply_test(unsigned int sda_value, unsigned int sdb_value, unsigned int tc_value) {
        reset_all_gate_switches();

        SD_a.write(make_32bit_vector(sda_value));
        SD_b.write(make_32bit_vector(sdb_value));
        TC.write(make_32bit_vector(tc_value));

        wait(200, SC_NS);

        long long expected_result = static_cast<long long>(sda_value)
                                   - static_cast<long long>(sdb_value)
                                   + to_signed_twos_complement_32bit(tc_value);        sc_lv<33> sum_a_val = Sum_a.read();
        sc_lv<33> sum_b_val = Sum_b.read();
        unsigned long long actual_sum_a = to_unsigned_33bit(sum_a_val);
        unsigned long long actual_sum_b = to_unsigned_33bit(sum_b_val);

        long long actual_result = static_cast<long long>(actual_sum_a)

                                 + static_cast<long long>(actual_sum_b)

                                 - ((1LL << (32 + 1)) - 1);

        if (expected_result != actual_result) {
            number_of_errors++;
        }

        unsigned int switches_i = sdtc_ptr->total_switches();

        bool in_changed[NUM_IN];
        for (int k = 0; k < 32; k++) in_changed[k] = (((sda_value >> k) & 1U) != ((prev_sda >> k) & 1U));
        for (int k = 0; k < 32; k++) in_changed[32 + k] = (((sdb_value >> k) & 1U) != ((prev_sdb >> k) & 1U));
        for (int k = 0; k < 32; k++) in_changed[64 + k] = (((tc_value >> k) & 1U) != ((prev_tc >> k) & 1U));

        bool out_changed[NUM_OUT];
        for (int k = 0; k <= 32; k++) out_changed[k] = (((actual_sum_a >> k) & 1ULL) != ((prev_sum_a >> k) & 1ULL));
        for (int k = 0; k <= 32; k++) out_changed[33 + k] = (((actual_sum_b >> k) & 1ULL) != ((prev_sum_b >> k) & 1ULL));

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
    sc_signal<sc_lv<32>> SD_a, SD_b, TC;
    sc_signal<sc_lv<33>> Sum_a, Sum_b;

    SdTcAdder32 sdtc("SDTC32");
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

    std::cout << "SDTC-PM-32-MC-pathEDP: SAMPLES=" << total_samples << " ERRORS=" << total_errors
              << " AVG_SWITCHES=" << avg_switches
              << " AVG_DELAY_ns=" << avg_delay
              << " AVG_EDP_correct=" << avg_edp_correct
              << " AVG_EDP_naive=" << avg_edp_naive
              << " pct_diff=" << ((avg_edp_naive / avg_edp_correct - 1.0) * 100.0) << "%\n";

    return 0;
}
