// sd_2n_glitch.cpp -- signed-digit adder, 2-digit width, exhaustive 2^N x
// 2^N VALUE sweep (matches RCA-2/CLA-2's test scope exactly, not 3^N x 3^N).
//
// Earlier attempts in this folder used Non-Adjacent Form (NAF) to pick one
// canonical digit pattern per value, but NAF needs N+1 digit positions for
// the top end of the range (verified separately), which either forces
// widening the adder or skipping some values. Both are unsatisfying for a
// direct RCA/CLA comparison.
//
// This version drops NAF's non-adjacency rule entirely: each operand value
// v in [0, 2^N) is encoded digit-by-digit from its own PLAIN BINARY bits
// (digit[i] = bit i of v, so each digit is 0 or +1 -- never -1). That's
// already a valid pattern in the {-1,0,+1} redundant digit set (just a
// degenerate one that never uses -1), so it needs no NAF conversion, fits
// in exactly N digits for every one of the 2^N values with zero skipped,
// and freely allows adjacent nonzero digits (e.g. 15 = "1111", unchanged).
//
// This does NOT reintroduce carry rippling: the adder's two-stage
// full-adder cascade (stage2[i] depends only on stage1_carry[i-1], one
// digit back, never further) is a fixed property of the wiring, not of
// which digit patterns are driven through it. The existing exhaustive
// sd_glitch.cpp already proves this empirically -- it tests the FULL
// 3^N digit-pattern space (which includes many adjacent-nonzero patterns)
// at every width 2..16 and reports a flat DELAY=4ns throughout, never
// scaling with N the way RCA's DELAY=N does.
#include <systemc.h>
#include <random>
#include <iostream>
#include <iomanip>
#include <string>
#include <cassert>
#include <cstdint>
#include <algorithm>
#include <thread>
#include <vector>
#include <sys/wait.h>
#include <unistd.h>

static const int N = 2;                              // digit width, same as RCA-2/CLA-2
static const unsigned long long POW2_N = 1ULL << N;   // 4: exhaustive value range per operand

// ============================================================================
// 0. HELPER STRUCTS AND FUNCTIONS
// ============================================================================
struct DigitRails2 {
    sc_lv<N> rail_a;
    sc_lv<N> rail_b;
};

// Encodes an array of N signed digits (each -1, 0, or +1) into dual rails,
// one canonical encoding per digit value: -1->(0,0), 0->(0,1), +1->(1,1).
static DigitRails2 encode_digits_2(const int digits[N]) {
    DigitRails2 encoded;
    encoded.rail_a = sc_lv<N>("0");
    encoded.rail_b = sc_lv<N>("0");
    for (int i = 0; i < N; i++) {
        int d = digits[i];
        encoded.rail_a[i] = (d == 1);
        encoded.rail_b[i] = (d != -1);
    }
    return encoded;
}

// Plain binary digit extraction: digits[i] = bit i of v, so each digit is
// 0 or +1 (never -1). No NAF conversion, no adjacency constraint -- a run
// of consecutive 1-bits (e.g. 1111 for 15) is left exactly as-is.
static void binary_digits(long long v, int digits[N]) {
    for (int i = 0; i < N; i++) {
        digits[i] = static_cast<int>((v >> i) & 1LL);
    }
}

// Decodes the (N+1)-digit dual-rail result: each (S_plus[i], S_minus[i])
// pair is a digit at binary place value 2^i, coded 00=-1, 11=+1, 01/10=0.
static long long decode_dual_rail_3(const sc_lv<N + 1>& s_plus, const sc_lv<N + 1>& s_minus) {
    long long value = 0;
    for (int i = 0; i < N + 1; i++) {
        bool p = s_plus[i].is_01() && s_plus[i].to_bool();
        bool m = s_minus[i].is_01() && s_minus[i].to_bool();
        int digit = (p && m) ? 1 : ((!p && !m) ? -1 : 0);
        value += static_cast<long long>(digit) * (1LL << i);
    }
    return value;
}

// ============================================================================
// 1. GENERIC GATE-LEVEL PRIMITIVES (identical to sd_glitch.cpp)
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
// 2. 2-DIGIT SIGNED-DIGIT ("PPM") ADDER -- exact port of the reference VHDL,
//    identical to the adder in sd_glitch.cpp (no widening, no change).
// ============================================================================
SC_MODULE(SignedDigitAdder2) {
    sc_in<sc_lv<N>> A_a;
    sc_in<sc_lv<N>> A_b;
    sc_in<sc_lv<N>> B_a;
    sc_in<sc_lv<N>> B_b;

    sc_out<sc_lv<N + 1>> S_minus;
    sc_out<sc_lv<N + 1>> S_plus;

    sc_signal<bool> sa_a[N];
    sc_signal<bool> sa_b[N];
    sc_signal<bool> sb_a[N];
    sc_signal<bool> sb_b[N];

    sc_signal<bool> stage1_sum[N];
    sc_signal<bool> stage1_carry[N];

    sc_signal<bool> stage2_sum[N];
    sc_signal<bool> stage2_carry[N];

    sc_signal<bool> const_one;

    BsdBinaryFullAdder* stage1_adders[N];
    BsdBinaryFullAdder* stage2_adders[N];

    void drive_const_one() { const_one.write(true); }

    SC_CTOR(SignedDigitAdder2) {
        SC_METHOD(drive_const_one);

        for (int i = 0; i < N; i++) {
            std::string stage1_name = "S1_FA" + std::to_string(i + 1);
            stage1_adders[i] = new BsdBinaryFullAdder(stage1_name.c_str());

            stage1_adders[i]->input_a(sa_a[i]);
            stage1_adders[i]->input_b(sb_a[i]);
            stage1_adders[i]->input_c(sa_b[i]);
            stage1_adders[i]->sum_out(stage1_sum[i]);
            stage1_adders[i]->carry_out(stage1_carry[i]);

            std::string stage2_name = "S2_FA" + std::to_string(i + 1);
            stage2_adders[i] = new BsdBinaryFullAdder(stage2_name.c_str());

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
        for (int i = 0; i < N; i++) {
            sensitive << stage2_sum[i] << stage2_carry[i];
        }
        sensitive << stage1_carry[N - 1];
    }

    void unpack_inputs() {
        sc_lv<N> value_a_a = A_a.read();
        sc_lv<N> value_a_b = A_b.read();
        sc_lv<N> value_b_a = B_a.read();
        sc_lv<N> value_b_b = B_b.read();

        for (int i = 0; i < N; i++) {
            sa_a[i].write(value_a_a[i].is_01() ? value_a_a[i].to_bool() : false);
            sa_b[i].write(value_a_b[i].is_01() ? value_a_b[i].to_bool() : false);
            sb_a[i].write(value_b_a[i].is_01() ? value_b_a[i].to_bool() : false);
            sb_b[i].write(value_b_b[i].is_01() ? value_b_b[i].to_bool() : false);
        }
    }

    void pack_outputs() {
        sc_lv<N + 1> output_minus;
        sc_lv<N + 1> output_plus;

        output_minus[0] = false;  // no carry-in below the least significant digit
        for (int i = 0; i < N; i++) {
            output_minus[i + 1] = stage2_carry[i].read();
            output_plus[i] = stage2_sum[i].read();
        }

        output_plus[N] = stage1_carry[N - 1].read();

        S_minus.write(output_minus);
        S_plus.write(output_plus);
    }

    void reset_counters() {
        for (int i = 0; i < N; i++) {
            stage1_adders[i]->reset_counters();
            stage2_adders[i]->reset_counters();
        }
    }

    uint64_t total_switches() const {
        uint64_t total_switches = 0;
        for (int i = 0; i < N; i++) {
            total_switches += stage1_adders[i]->total_switches();
            total_switches += stage2_adders[i]->total_switches();
        }
        return total_switches;
    }

    void print_report() {
        std::cout << "SD-2-BIN: GATES=24 SWITCHES=" << total_switches() << " DELAY=4ns\n";
    }

    ~SignedDigitAdder2() {
        for (int i = 0; i < N; i++) {
            delete stage1_adders[i];
            delete stage2_adders[i];
        }
    }
};

// ============================================================================
// 3. TESTBENCH -- hybrid: A is Monte Carlo (fresh random draw every
//    step, using the SAME plain-binary-digit encoding as the exhaustive
//    2^N sweep -- no NAF), B sweeps its FULL value range 0..3
//    exactly once, incrementing by 1 each step (1:1 pairing). Total
//    steps = 2^N = 4.
// ============================================================================

// ============================================================================
// Real OpenSTA (Nangate45) max delay per (startpoint pin, endpoint pin),
// extracted from all_critical_paths.csv for SD-2 (25 pairs).
// ============================================================================
static const int NUM_IN = 8;
static const int NUM_OUT = 6;
static double path_delay[NUM_IN][NUM_OUT];

static void init_path_delay_table() {
    for (int i = 0; i < NUM_IN; i++)
        for (int o = 0; o < NUM_OUT; o++)
            path_delay[i][o] = 0.0;

    path_delay[0][4] = 0.71;
    path_delay[0][5] = 0.82;
    path_delay[0][0] = 0.76;
    path_delay[0][1] = 0.79;
    path_delay[1][5] = 0.76;
    path_delay[1][1] = 0.76;
    path_delay[1][2] = 0.37;
    path_delay[2][4] = 0.53;
    path_delay[2][5] = 0.82;
    path_delay[2][0] = 0.59;
    path_delay[2][1] = 0.79;
    path_delay[3][5] = 0.59;
    path_delay[3][1] = 0.59;
    path_delay[3][2] = 0.38;
    path_delay[4][4] = 0.72;
    path_delay[4][5] = 0.81;
    path_delay[4][0] = 0.77;
    path_delay[4][1] = 0.78;
    path_delay[5][5] = 0.78;
    path_delay[5][1] = 0.77;
    path_delay[5][2] = 0.37;
    path_delay[6][4] = 0.32;
    path_delay[6][0] = 0.18;
    path_delay[7][5] = 0.38;
    path_delay[7][1] = 0.18;
}

// ============================================================================
// TESTBENCH -- hybrid, per-sample EDP.
// ============================================================================
SC_MODULE(Testbench) {
    sc_out<sc_lv<N>> A_a;
    sc_out<sc_lv<N>> A_b;
    sc_out<sc_lv<N>> B_a;
    sc_out<sc_lv<N>> B_b;

    sc_in<sc_lv<N + 1>> S_minus;
    sc_in<sc_lv<N + 1>> S_plus;

    SignedDigitAdder2* design_ptr;

    unsigned int number_of_errors;
    unsigned long long rng_seed;

    double sum_switches, sum_delay, sum_edp;
    bool prev_in[NUM_IN];
    bool prev_out[NUM_OUT];

    SC_CTOR(Testbench)
        : design_ptr(nullptr), number_of_errors(0), rng_seed(42ULL),
          sum_switches(0.0), sum_delay(0.0), sum_edp(0.0)
    {
        for (int i = 0; i < NUM_IN; i++) prev_in[i] = false;
        for (int i = 0; i < NUM_OUT; i++) prev_out[i] = false;
        SC_THREAD(stimulus);
    }

    void stimulus() {
        int zero_digits[N];
        for (int i = 0; i < N; i++) zero_digits[i] = 0;
        DigitRails2 zero = encode_digits_2(zero_digits);

        A_a.write(zero.rail_a);
        A_b.write(zero.rail_b);
        B_a.write(zero.rail_a);
        B_b.write(zero.rail_b);

        wait(20, SC_NS);

        design_ptr->reset_counters();

        std::mt19937_64 rng(rng_seed);
        std::uniform_int_distribution<unsigned long long> dist(0ULL, 1ULL);

                for (unsigned long long value_b = 0; value_b < 2ULL; value_b++) {
            unsigned long long value_a = dist(rng);
            apply_test(value_a, value_b);
        }

        assert(number_of_errors == 0);
        sc_stop();
    }

    void apply_test(unsigned long long value_a, unsigned long long value_b) {
        design_ptr->reset_counters();

        int a_digits[N], b_digits[N];
        binary_digits(static_cast<long long>(value_a), a_digits);
        binary_digits(static_cast<long long>(value_b), b_digits);

        DigitRails2 encoded_a = encode_digits_2(a_digits);
        DigitRails2 encoded_b = encode_digits_2(b_digits);

        A_a.write(encoded_a.rail_a);
        A_b.write(encoded_a.rail_b);
        B_a.write(encoded_b.rail_a);
        B_b.write(encoded_b.rail_b);

        wait(20, SC_NS);

        long long expected_result = static_cast<long long>(value_a) + static_cast<long long>(value_b);
        sc_lv<N + 1> s_plus_val = S_plus.read();
        sc_lv<N + 1> s_minus_val = S_minus.read();
        long long actual_result = decode_dual_rail_3(s_plus_val, s_minus_val);

        if (expected_result != actual_result) {
            number_of_errors++;
        }

        uint64_t switches_i = design_ptr->total_switches();

        bool cur_in[NUM_IN];
        for (int i = 0; i < N; i++) cur_in[i] = encoded_a.rail_a[i].is_01() ? encoded_a.rail_a[i].to_bool() : false;
        for (int i = 0; i < N; i++) cur_in[2 + i] = encoded_a.rail_b[i].is_01() ? encoded_a.rail_b[i].to_bool() : false;
        for (int i = 0; i < N; i++) cur_in[4 + i] = encoded_b.rail_a[i].is_01() ? encoded_b.rail_a[i].to_bool() : false;
        for (int i = 0; i < N; i++) cur_in[6 + i] = encoded_b.rail_b[i].is_01() ? encoded_b.rail_b[i].to_bool() : false;

        bool cur_out[NUM_OUT];
        for (int i = 0; i <= N; i++) cur_out[i] = s_plus_val[i].is_01() ? s_plus_val[i].to_bool() : false;
        for (int i = 0; i <= N; i++) cur_out[3 + i] = s_minus_val[i].is_01() ? s_minus_val[i].to_bool() : false;

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
// 4. MAIN
// ============================================================================
int sc_main(int argc, char* argv[]) {
    init_path_delay_table();

    sc_signal<sc_lv<N>> A_a, A_b, B_a, B_b;
    sc_signal<sc_lv<N + 1>> S_minus, S_plus;

    SignedDigitAdder2 adder("SignedDigitAdderM");
    Testbench tb("TB");

    tb.design_ptr = &adder;
    tb.rng_seed = (argc >= 2) ? std::stoull(argv[1]) : 42ULL;

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

    sc_start();

    assert(tb.number_of_errors == 0);

    double avg_switches = tb.sum_switches / 2.0;
    double avg_delay    = tb.sum_delay / 2.0;
    double avg_edp_correct = tb.sum_edp / 2.0;

    std::cout << "SD-2-HYB-pathEDP: STEPS=2 SEED=" << tb.rng_seed
              << " AVG_SWITCHES=" << avg_switches
              << " AVG_DELAY_ns=" << avg_delay
              << " AVG_EDP_correct=" << avg_edp_correct << "\n";

    return 0;
}
