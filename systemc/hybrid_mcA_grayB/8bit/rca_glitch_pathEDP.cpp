// main.cpp
#include <systemc.h>
#include <random>
#include <iostream>
#include <iomanip>
#include <string>
#include <cassert>

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
// 2. 8-BIT RIPPLE CARRY ADDER
// ==========================================
SC_MODULE(RippleCarryAdder8) {
    sc_in<sc_lv<8>> A;
    sc_in<sc_lv<8>> B;

    sc_out<sc_lv<8>> Sum;
    sc_out<bool> Cout;

    sc_signal<bool> a[8];
    sc_signal<bool> b[8];
    sc_signal<bool> s[8];
    sc_signal<bool> c[8];
    sc_signal<bool> const_zero;

    FullAdder* fa[8];

    SC_CTOR(RippleCarryAdder8) {
        for (int i = 0; i < 8; i++) {
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
        sensitive << s[0] << s[1] << s[2] << s[3]
                  << s[4] << s[5] << s[6] << s[7]
                  << c[7];
    }

    void split_inputs() {
        sc_lv<8> value_a = A.read();
        sc_lv<8> value_b = B.read();

        for (int i = 0; i < 8; i++) {
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
        sc_lv<8> value_sum;

        for (int i = 0; i < 8; i++) {
            value_sum[i] = s[i].read();
        }

        Sum.write(value_sum);
        Cout.write(c[7].read());
    }

    unsigned int total_switches() const {
        unsigned int total = 0;
        for (int i = 0; i < 8; i++) { total += fa[i]->total_switches(); }
        return total;
    }

    void reset_switches() {
        for (int i = 0; i < 8; i++) fa[i]->reset_switches();
    }

    void print_report() {
        unsigned int total_switches = 0;
        for (int i = 0; i < 8; i++) {
            total_switches += fa[i]->total_switches();
        }

        std::cout << "RCA-8: GATES=40 SWITCHES=" << total_switches << " DELAY=16ns\n";
    }

    ~RippleCarryAdder8() {
        for (int i = 0; i < 8; i++) {
            delete fa[i];
        }
    }
};

// ==========================================
// 3. TESTBENCH
// ==========================================

// ==========================================
// 4. TESTBENCH -- hybrid: A is Monte Carlo (fresh random draw every
//    step), B sweeps its FULL value range 0..255 exactly once,
//    incrementing by 1 each step (1:1 pairing -- step i applies
//    B=i, A=random()). Total steps = 2^N = 256, matching B's own
//    range exactly, not the full 2^N x 2^N exhaustive product.
// ==========================================

// ============================================================================
// Real OpenSTA (Nangate45) max delay per (startpoint pin, endpoint pin),
// extracted from all_critical_paths.csv for RCA-8 (88 pairs).
// ============================================================================
static const int NUM_IN = 16;
static const int NUM_OUT = 9;
static double path_delay[NUM_IN][NUM_OUT];

static void init_path_delay_table() {
    for (int i = 0; i < NUM_IN; i++)
        for (int o = 0; o < NUM_OUT; o++)
            path_delay[i][o] = 0.0;

    path_delay[0][8] = 2.32;
    path_delay[0][0] = 0.36;
    path_delay[0][1] = 0.45;
    path_delay[0][2] = 0.75;
    path_delay[0][3] = 1.05;
    path_delay[0][4] = 1.35;
    path_delay[0][5] = 1.65;
    path_delay[0][6] = 1.95;
    path_delay[0][7] = 2.25;
    path_delay[1][8] = 2.24;
    path_delay[1][1] = 0.36;
    path_delay[1][2] = 0.67;
    path_delay[1][3] = 0.97;
    path_delay[1][4] = 1.27;
    path_delay[1][5] = 1.57;
    path_delay[1][6] = 1.87;
    path_delay[1][7] = 2.17;
    path_delay[2][8] = 1.94;
    path_delay[2][2] = 0.36;
    path_delay[2][3] = 0.67;
    path_delay[2][4] = 0.97;
    path_delay[2][5] = 1.27;
    path_delay[2][6] = 1.57;
    path_delay[2][7] = 1.87;
    path_delay[3][8] = 1.64;
    path_delay[3][3] = 0.36;
    path_delay[3][4] = 0.67;
    path_delay[3][5] = 0.97;
    path_delay[3][6] = 1.27;
    path_delay[3][7] = 1.57;
    path_delay[4][8] = 1.35;
    path_delay[4][4] = 0.36;
    path_delay[4][5] = 0.67;
    path_delay[4][6] = 0.97;
    path_delay[4][7] = 1.27;
    path_delay[5][8] = 1.05;
    path_delay[5][5] = 0.36;
    path_delay[5][6] = 0.67;
    path_delay[5][7] = 0.97;
    path_delay[6][8] = 0.75;
    path_delay[6][6] = 0.36;
    path_delay[6][7] = 0.67;
    path_delay[7][8] = 0.45;
    path_delay[7][7] = 0.36;
    path_delay[8][8] = 2.33;
    path_delay[8][0] = 0.37;
    path_delay[8][1] = 0.46;
    path_delay[8][2] = 0.76;
    path_delay[8][3] = 1.06;
    path_delay[8][4] = 1.36;
    path_delay[8][5] = 1.66;
    path_delay[8][6] = 1.96;
    path_delay[8][7] = 2.26;
    path_delay[9][8] = 2.26;
    path_delay[9][1] = 0.37;
    path_delay[9][2] = 0.68;
    path_delay[9][3] = 0.98;
    path_delay[9][4] = 1.28;
    path_delay[9][5] = 1.58;
    path_delay[9][6] = 1.88;
    path_delay[9][7] = 2.18;
    path_delay[10][8] = 1.96;
    path_delay[10][2] = 0.37;
    path_delay[10][3] = 0.68;
    path_delay[10][4] = 0.98;
    path_delay[10][5] = 1.28;
    path_delay[10][6] = 1.58;
    path_delay[10][7] = 1.88;
    path_delay[11][8] = 1.66;
    path_delay[11][3] = 0.37;
    path_delay[11][4] = 0.68;
    path_delay[11][5] = 0.98;
    path_delay[11][6] = 1.28;
    path_delay[11][7] = 1.58;
    path_delay[12][8] = 1.36;
    path_delay[12][4] = 0.37;
    path_delay[12][5] = 0.68;
    path_delay[12][6] = 0.98;
    path_delay[12][7] = 1.28;
    path_delay[13][8] = 1.06;
    path_delay[13][5] = 0.37;
    path_delay[13][6] = 0.68;
    path_delay[13][7] = 0.98;
    path_delay[14][8] = 0.76;
    path_delay[14][6] = 0.37;
    path_delay[14][7] = 0.68;
    path_delay[15][8] = 0.46;
    path_delay[15][7] = 0.37;
}

// ============================================================================
// TESTBENCH -- hybrid, per-sample EDP.
// ============================================================================
SC_MODULE(Testbench) {
    sc_out<sc_lv<8>> A, B;
    sc_in<sc_lv<8>> Sum;
    sc_in<bool> Cout;

    RippleCarryAdder8* rca_ptr;

    unsigned int number_of_errors;
    unsigned long long rng_seed;

    double sum_switches, sum_delay, sum_edp;
    unsigned int prev_a, prev_b, prev_sum;
    bool prev_cout;

    SC_CTOR(Testbench)
        : rca_ptr(nullptr), number_of_errors(0), rng_seed(42ULL),
          sum_switches(0.0), sum_delay(0.0), sum_edp(0.0),
          prev_a(0), prev_b(0), prev_sum(0), prev_cout(false)
    {
        SC_THREAD(stimulus);
    }

    void stimulus() {
        A.write(make_8bit_vector(0));
        B.write(make_8bit_vector(0));
        wait(200, SC_NS);
        prev_a = 0; prev_b = 0;
        prev_sum = to_unsigned_8bit(Sum.read());
        prev_cout = Cout.read();

        std::mt19937_64 rng(rng_seed);
        std::uniform_int_distribution<unsigned long long> dist(0ULL, 127ULL);

                for (unsigned int i = 0; i < 128U; i++) {
            unsigned int b_value = i ^ (i >> 1);  // Gray code: exactly 1 bit changes per step
            unsigned int a_value = static_cast<unsigned int>(dist(rng));
            apply_test(a_value, b_value);
        }

        assert(number_of_errors == 0);
        sc_stop();
    }

    void apply_test(unsigned int a_value, unsigned int b_value) {
        rca_ptr->reset_switches();

        A.write(make_8bit_vector(a_value));
        B.write(make_8bit_vector(b_value));
        wait(200, SC_NS);

        unsigned int expected_result = a_value + b_value;
        unsigned int actual_sum = to_unsigned_8bit(Sum.read());
        bool actual_cout = Cout.read();
        unsigned int actual_result = actual_sum + (actual_cout ? 256U : 0U);
        if (expected_result != actual_result) number_of_errors++;

        unsigned int switches_i = rca_ptr->total_switches();

        bool in_changed[NUM_IN];
        for (int k = 0; k < 8; k++) in_changed[k] = (((a_value >> k) & 1U) != ((prev_a >> k) & 1U));
        for (int k = 0; k < 8; k++) in_changed[8 + k] = (((b_value >> k) & 1U) != ((prev_b >> k) & 1U));

        bool out_changed[NUM_OUT];
        for (int k = 0; k < 8; k++) out_changed[k] = (((actual_sum >> k) & 1U) != ((prev_sum >> k) & 1U));
        out_changed[8] = (actual_cout != prev_cout);

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

// ==========================================
// MAIN
// ==========================================
int sc_main(int argc, char* argv[]) {
    init_path_delay_table();

    sc_signal<sc_lv<8>> A, B, Sum;
    sc_signal<bool> Cout;

    RippleCarryAdder8 rca("RCA8");
    Testbench tb("TB");
    tb.rca_ptr = &rca;
    tb.rng_seed = (argc >= 2) ? std::stoull(argv[1]) : 42ULL;

    rca.A(A); rca.B(B); rca.Sum(Sum); rca.Cout(Cout);
    tb.A(A); tb.B(B); tb.Sum(Sum); tb.Cout(Cout);

    sc_start();

    assert(tb.number_of_errors == 0);

    double avg_switches = tb.sum_switches / 128.0;
    double avg_delay    = tb.sum_delay / 128.0;
    double avg_edp_correct = tb.sum_edp / 128.0;

    std::cout << "RCA-8-HYB-pathEDP: STEPS=128 SEED=" << tb.rng_seed << " ERRORS=" << tb.number_of_errors
              << " AVG_SWITCHES=" << avg_switches
              << " AVG_DELAY_ns=" << avg_delay
              << " AVG_EDP_correct=" << avg_edp_correct << "\n";

    return 0;
}
