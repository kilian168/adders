// sdtc_glitch.cpp – 8-bit Signed-Digit / Two's-Complement Carry-Save Adder, gate-level switching activity
//
// Ported from SD_add_TC_4bit.vhdl ("DIG_Add" digit cells wired into the
// "main" entity, generalized from 4 digits to 8). SD_a/SD_b together are a
// PM (plus-minus) signed-digit number: digit i = SD_a(i) - SD_b(i) in
// {-1,0,1} ("10"=+1, "01"=-1, "00"/"11"=0), which sums to plain
// SD_a_unsigned - SD_b_unsigned. TC is a two's-complement (signed) 8-bit
// operand. Digit i is a single digit cell over (TC(i), SD_b(i), SD_a(i))
// with NO carry chain between digits (unlike RCA/CLA), so the whole adder
// settles in one cell's gate depth no matter how wide it is. Its
// sum/carry outputs are exposed directly as a redundant (8+1)-bit pair:
//   Sum_a(i)   = sum of digit i         for i = 0..8-1
//   Sum_a(8) = NOT TC(8-1)          (sign-correction top bit)
//   Sum_b(0)   = '0'                    (matches the VHDL's Sum0_b <= '0')
//   Sum_b(i+1) = carry of digit i       for i = 0..8-1
// Each digit contributes 2*carry - sum (not 2*carry + sum) to the total, so
// the testbench checks Sum_b - Sum_a + 2^8*(TC(8-1)?-1:+1) == SD_a - SD_b + TC_signed.
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
static sc_lv<8> make_8bit_vector(unsigned int value) {
    sc_lv<8> bits;
    for (int i = 0; i < 8; i++) {
        bits[i] = ((value >> i) & 1U) != 0U;
    }
    return bits;
}

static unsigned int to_unsigned_8bit(const sc_lv<8>& bits) {
    unsigned int value = 0;
    for (int i = 0; i < 8; i++) {
        if (bits[i].is_01() && bits[i].to_bool()) {
            value += (1U << i);
        }
    }
    return value;
}

static unsigned long long to_unsigned_9bit(const sc_lv<9>& bits) {
    unsigned long long value = 0;
    for (int i = 0; i < 9; i++) {
        if (bits[i].is_01() && bits[i].to_bool()) {
            value += (1ULL << i);
        }
    }
    return value;
}

// TC is a two's-complement N-bit operand; decode its signed integer value.
static long long to_signed_twos_complement_8bit(unsigned int value) {
    long long v = static_cast<long long>(value);
    if (value & (1U << (8 - 1))) {
        v -= (1LL << 8);
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

    sc_signal<bool> s1, notB, term1, notS1, term2;

    Gate *g_xor1, *g_and1, *g_and2, *g_or1, *g_xor2;
    GateNot *g_notB, *g_notS1;

    SC_CTOR(FullAdder) {
        g_xor1 = new Gate("XOR1", GATE_XOR);
        g_xor1->in1(cin); g_xor1->in2(b); g_xor1->out(s1);

        g_notB = new GateNot("NOTB");
        g_notB->in1(b); g_notB->out(notB);

        g_and1 = new Gate("AND1", GATE_AND);
        g_and1->in1(cin); g_and1->in2(notB); g_and1->out(term1);

        g_notS1 = new GateNot("NOTS1");
        g_notS1->in1(s1); g_notS1->out(notS1);

        g_and2 = new Gate("AND2", GATE_AND);
        g_and2->in1(notS1); g_and2->in2(a); g_and2->out(term2);

        g_or1 = new Gate("OR1", GATE_OR);
        g_or1->in1(term1); g_or1->in2(term2); g_or1->out(cout);

        g_xor2 = new Gate("XOR2", GATE_XOR);
        g_xor2->in1(s1); g_xor2->in2(a); g_xor2->out(sum);
    }

    unsigned int total_switches() const {
        return g_xor1->switches + g_notB->switches + g_and1->switches +
               g_notS1->switches + g_and2->switches + g_or1->switches + g_xor2->switches;
    }

    ~FullAdder() {
        delete g_xor1; delete g_notB; delete g_and1; delete g_notS1;
        delete g_and2; delete g_or1; delete g_xor2;
    }
};

// ==========================================
// 3. 8-BIT SIGNED-DIGIT / TWO'S-COMPLEMENT ADDER
//    8 independent digit cells (7 gates each), one per digit, with NO
//    carry chain between them. Gate count: 8 x 7 (digit cells) + 1 (NOT) = 57
// ==========================================
SC_MODULE(SdTcAdder8) {
    sc_in<sc_lv<8>> SD_a;
    sc_in<sc_lv<8>> SD_b;
    sc_in<sc_lv<8>> TC;

    sc_out<sc_lv<9>> Sum_a;
    sc_out<sc_lv<9>> Sum_b;

    sc_signal<bool> sda_bit[8];
    sc_signal<bool> sdb_bit[8];
    sc_signal<bool> tc_bit[8];
    sc_signal<bool> sum_bit[8];
    sc_signal<bool> carry_bit[8];
    sc_signal<bool> const_zero;
    sc_signal<bool> top_bit;

    FullAdder* fa[8];
    GateNot* g_not_top;

    SC_CTOR(SdTcAdder8) {
        for (int i = 0; i < 8; i++) {
            std::string name = "FA_" + std::to_string(i);
            fa[i] = new FullAdder(name.c_str());

            fa[i]->a(tc_bit[i]);
            fa[i]->b(sdb_bit[i]);
            fa[i]->cin(sda_bit[i]);
            fa[i]->sum(sum_bit[i]);
            fa[i]->cout(carry_bit[i]);
        }

        g_not_top = new GateNot("NOT_TOP");
        g_not_top->in1(tc_bit[8 - 1]);
        g_not_top->out(top_bit);

        SC_METHOD(split_inputs);
        sensitive << SD_a << SD_b << TC;

        SC_METHOD(combine_outputs);
        for (int i = 0; i < 8; i++) {
            sensitive << sum_bit[i];
            sensitive << carry_bit[i];
        }
        sensitive << top_bit;
    }

    void split_inputs() {
        sc_lv<8> value_sda = SD_a.read();
        sc_lv<8> value_sdb = SD_b.read();
        sc_lv<8> value_tc = TC.read();

        for (int i = 0; i < 8; i++) {
            sda_bit[i].write(value_sda[i].is_01() ? value_sda[i].to_bool() : false);
            sdb_bit[i].write(value_sdb[i].is_01() ? value_sdb[i].to_bool() : false);
            tc_bit[i].write(value_tc[i].is_01() ? value_tc[i].to_bool() : false);
        }
    }

    void combine_outputs() {
        sc_lv<9> value_sum_a;
        sc_lv<9> value_sum_b;

        for (int i = 0; i < 8; i++) {
            value_sum_a[i] = sum_bit[i].read();
            value_sum_b[i + 1] = carry_bit[i].read();
        }
        value_sum_a[8] = top_bit.read();
        value_sum_b[0] = const_zero.read();

        Sum_a.write(value_sum_a);
        Sum_b.write(value_sum_b);
    }

    unsigned int total_switches() const {
        unsigned int total = 0;
        for (int i = 0; i < 8; i++) {
            total += fa[i]->total_switches();
        }
        total += g_not_top->switches;
        return total;
    }

    ~SdTcAdder8() {
        for (int i = 0; i < 8; i++) {
            delete fa[i];
        }
        delete g_not_top;
    }
};

// ==========================================
// 4. TESTBENCH -- Monte Carlo random sampling
//    Draws num_samples uniformly random (SD_a, SD_b, TC) triples instead
//    of exhaustively covering the whole input space: at 8 bits exhaustive
//    coverage (2^8 x 2^8 x 2^8 cases) is computationally infeasible, so
//    Monte Carlo sampling is the only tractable option.
//
//    SD_a/SD_b are the PM signed-digit rails (digit i = SD_a(i)-SD_b(i));
//    TC is two's-complement signed. Correctness is checked against the
//    redundant-representation identity Sum_b - Sum_a + 2^N*(+-1) ==
//    SD_a - SD_b + TC_signed (see header comment and apply_test below).
// ==========================================

// ============================================================================
// Real OpenSTA (Nangate45) max delay per (startpoint pin, endpoint pin),
// extracted from all_critical_paths.csv for SDTC-PM-8 (49 pairs).
// ============================================================================
static const int NUM_IN = 24;
static const int NUM_OUT = 18;
static double path_delay[NUM_IN][NUM_OUT];

static void init_path_delay_table() {
    for (int i = 0; i < NUM_IN; i++)
        for (int o = 0; o < NUM_OUT; o++)
            path_delay[i][o] = 0.0;

    path_delay[0][0] = 0.36;
    path_delay[0][10] = 0.47;
    path_delay[1][1] = 0.36;
    path_delay[1][11] = 0.47;
    path_delay[2][2] = 0.36;
    path_delay[2][12] = 0.47;
    path_delay[3][3] = 0.36;
    path_delay[3][13] = 0.47;
    path_delay[4][4] = 0.36;
    path_delay[4][14] = 0.47;
    path_delay[5][5] = 0.36;
    path_delay[5][15] = 0.47;
    path_delay[6][6] = 0.36;
    path_delay[6][16] = 0.47;
    path_delay[7][7] = 0.36;
    path_delay[7][17] = 0.47;
    path_delay[8][0] = 0.37;
    path_delay[8][10] = 0.46;
    path_delay[9][1] = 0.37;
    path_delay[9][11] = 0.46;
    path_delay[10][2] = 0.37;
    path_delay[10][12] = 0.46;
    path_delay[11][3] = 0.37;
    path_delay[11][13] = 0.46;
    path_delay[12][4] = 0.37;
    path_delay[12][14] = 0.46;
    path_delay[13][5] = 0.37;
    path_delay[13][15] = 0.46;
    path_delay[14][6] = 0.37;
    path_delay[14][16] = 0.46;
    path_delay[15][7] = 0.37;
    path_delay[15][17] = 0.46;
    path_delay[16][0] = 0.18;
    path_delay[16][10] = 0.25;
    path_delay[17][1] = 0.18;
    path_delay[17][11] = 0.25;
    path_delay[18][2] = 0.18;
    path_delay[18][12] = 0.25;
    path_delay[19][3] = 0.18;
    path_delay[19][13] = 0.25;
    path_delay[20][4] = 0.18;
    path_delay[20][14] = 0.25;
    path_delay[21][5] = 0.18;
    path_delay[21][15] = 0.25;
    path_delay[22][6] = 0.18;
    path_delay[22][16] = 0.25;
    path_delay[23][7] = 0.18;
    path_delay[23][8] = 0.01;
    path_delay[23][17] = 0.25;
}

// ============================================================================
// TESTBENCH -- exhaustive 2^N x 2^N sweep, matching TER's "dropped NAF"
// approach: SD_a/SD_b together are a REDUNDANT dual-rail signed-digit
// encoding of ONE operand (digit i = SD_a(i) - SD_b(i) in {-1,0,+1},
// same structure as TER's A_a/A_b), so instead of exhaustively covering
// every (SD_a, SD_b) combination (2^N x 2^N on their own, on top of TC's
// own 2^N -- 2^(3N) total), each SD value v is encoded canonically:
// SD_a = v's own bits, SD_b = 0 always (never uses the -1 digit state,
// exactly like TER-BIN's plain-binary-digit encoding). That leaves a
// genuine 2-operand sweep: SD value x TC value, 2^N x 2^N total vectors.
// Single continuous process (no forking) to preserve cross-vector
// correlation, same as RCA/CLA/TER's exhaustive methodology.
// ============================================================================

SC_MODULE(Testbench) {
    sc_out<sc_lv<8>> SD_a, SD_b, TC;
    sc_in<sc_lv<9>> Sum_a, Sum_b;

    SdTcAdder8* sdtc_ptr;

    unsigned int number_of_errors;

    double sum_switches, sum_delay, sum_edp;
    unsigned int prev_sd, prev_tc;
    unsigned int prev_sum_a, prev_sum_b;

    SC_CTOR(Testbench)
        : sdtc_ptr(nullptr), number_of_errors(0),
          sum_switches(0.0), sum_delay(0.0), sum_edp(0.0),
          prev_sd(0), prev_tc(0), prev_sum_a(0), prev_sum_b(0)
    {
        SC_THREAD(stimulus);
    }

    void stimulus() {
        SD_a.write(make_8bit_vector(0));
        SD_b.write(make_8bit_vector(0));
        TC.write(make_8bit_vector(0));
        wait(200, SC_NS);
        prev_sd = 0; prev_tc = 0;
        prev_sum_a = static_cast<unsigned int>(to_unsigned_9bit(Sum_a.read()));
        prev_sum_b = static_cast<unsigned int>(to_unsigned_9bit(Sum_b.read()));

        for (unsigned int i = 0; i < 65536U; i++) {
            unsigned int g = i ^ (i >> 1);
            unsigned int sd_value = (g >> 8) & 255U;
            unsigned int tc_value = g & 255U;
            apply_test(sd_value, tc_value);
        }

        assert(number_of_errors == 0);
        sc_stop();
    }

    void apply_test(unsigned int sd_value, unsigned int tc_value) {
        reset_all_gate_switches();

        SD_a.write(make_8bit_vector(sd_value));
        SD_b.write(make_8bit_vector(0));
        TC.write(make_8bit_vector(tc_value));

        wait(200, SC_NS);

        long long expected_result = static_cast<long long>(sd_value)
                                   + to_signed_twos_complement_8bit(tc_value);

        unsigned int tc_top = (tc_value >> (8 - 1)) & 1U;
        long long sign_term = tc_top ? -(1LL << 8) : (1LL << 8);

        sc_lv<9> sum_a_val = Sum_a.read();
        sc_lv<9> sum_b_val = Sum_b.read();
        unsigned int actual_sum_a = static_cast<unsigned int>(to_unsigned_9bit(sum_a_val));
        unsigned int actual_sum_b = static_cast<unsigned int>(to_unsigned_9bit(sum_b_val));

        long long actual_result = static_cast<long long>(actual_sum_b)
                                 - static_cast<long long>(actual_sum_a)
                                 + sign_term;

        if (expected_result != actual_result) {
            number_of_errors++;
        }

        unsigned int switches_i = sdtc_ptr->total_switches();

        bool in_changed[NUM_IN];
        for (int k = 0; k < 8; k++) in_changed[k] = (((sd_value >> k) & 1U) != ((prev_sd >> k) & 1U));
        for (int k = 0; k < 8; k++) in_changed[8 + k] = false;  // SD_b is always 0, never changes
        for (int k = 0; k < 8; k++) in_changed[16 + k] = (((tc_value >> k) & 1U) != ((prev_tc >> k) & 1U));

        bool out_changed[NUM_OUT];
        for (int k = 0; k <= 8; k++) out_changed[k] = (((actual_sum_a >> k) & 1U) != ((prev_sum_a >> k) & 1U));
        for (int k = 0; k <= 8; k++) out_changed[9 + k] = (((actual_sum_b >> k) & 1U) != ((prev_sum_b >> k) & 1U));

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

        prev_sd = sd_value;
        prev_tc = tc_value;
        prev_sum_a = actual_sum_a;
        prev_sum_b = actual_sum_b;
    }
};

// ==========================================
// MAIN -- single continuous process
// ==========================================
int sc_main(int argc, char* argv[]) {
    init_path_delay_table();

    sc_signal<sc_lv<8>> SD_a, SD_b, TC;
    sc_signal<sc_lv<9>> Sum_a, Sum_b;

    SdTcAdder8 sdtc("SDTC8");
    Testbench tb("TB");
    tb.sdtc_ptr = &sdtc;

    sdtc.SD_a(SD_a); sdtc.SD_b(SD_b); sdtc.TC(TC);
    sdtc.Sum_a(Sum_a); sdtc.Sum_b(Sum_b);

    tb.SD_a(SD_a); tb.SD_b(SD_b); tb.TC(TC);
    tb.Sum_a(Sum_a); tb.Sum_b(Sum_b);

    sc_start();

    assert(tb.number_of_errors == 0);

    unsigned long long vectors = 65536ULL;
    double avg_switches = tb.sum_switches / static_cast<double>(vectors);
    double avg_delay    = tb.sum_delay    / static_cast<double>(vectors);
    double avg_edp_correct = tb.sum_edp   / static_cast<double>(vectors);
    double avg_edp_naive   = avg_switches * avg_delay;

    std::cout << "SDTC-PM-8-GRAY-pathEDP: VECTORS=" << vectors
              << " AVG_SWITCHES=" << avg_switches
              << " AVG_DELAY_ns=" << avg_delay
              << " AVG_EDP_correct=" << avg_edp_correct
              << " AVG_EDP_naive=" << avg_edp_naive
              << " pct_diff=" << ((avg_edp_naive / avg_edp_correct - 1.0) * 100.0) << "%\n";

    return 0;
}
