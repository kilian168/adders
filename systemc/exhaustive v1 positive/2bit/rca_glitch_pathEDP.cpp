// rca2_exhaustive.cpp
#include <systemc.h>
#include <iostream>
#include <iomanip>
#include <string>
#include <cassert>

// ---- exhaustive range mapping (v1): index i -> bit pattern of value (OFFSET + i) ----
static const unsigned long long EXH_COUNT = 2ULL;
static const unsigned long long EXH_OFFSET = 0ULL;
static const unsigned long long EXH_MASK = 3ULL;
static const unsigned int EXH_TOP = 2U;
static const unsigned int EXH_LOW = 1U;
static inline unsigned int exh_map(unsigned long long i) { return static_cast<unsigned int>((i + EXH_OFFSET) & EXH_MASK); }

// ==========================================
// Helper functions
// ==========================================
static sc_lv<2> make_2bit_vector(unsigned int value) {
    sc_lv<2> bits;
    bits[0] = (value & 1U) != 0U;
    bits[1] = (value & 2U) != 0U;
    return bits;
}

static unsigned int to_unsigned_2bit(const sc_lv<2>& bits) {
    unsigned int value = 0;

    if (bits[0].is_01() && bits[0].to_bool()) {
        value += 1U;
    }

    if (bits[1].is_01() && bits[1].to_bool()) {
        value += 2U;
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

    sc_signal<bool> s1;
    sc_signal<bool> c1;
    sc_signal<bool> c2;

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
// 3. 2-BIT RIPPLE CARRY ADDER
// ==========================================
SC_MODULE(RippleCarryAdder2) {
    sc_in<sc_lv<2>> A;
    sc_in<sc_lv<2>> B;

    sc_out<sc_lv<2>> Sum;
    sc_out<bool> Cout;

    sc_signal<bool> a[2];
    sc_signal<bool> b[2];
    sc_signal<bool> s[2];
    sc_signal<bool> c[2];
    sc_signal<bool> const_zero;

    FullAdder* fa[2];

    SC_CTOR(RippleCarryAdder2) {
        for (int i = 0; i < 2; i++) {
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
        sensitive << s[0] << s[1] << c[1];
    }

    void split_inputs() {
        sc_lv<2> value_a = A.read();
        sc_lv<2> value_b = B.read();

        for (int i = 0; i < 2; i++) {
            a[i].write(value_a[i].is_01() ? value_a[i].to_bool() : false);
            b[i].write(value_b[i].is_01() ? value_b[i].to_bool() : false);
        }
    }

    void combine_outputs() {
        sc_lv<2> value_sum;

        for (int i = 0; i < 2; i++) {
            value_sum[i] = s[i].read();
        }

        Sum.write(value_sum);
        Cout.write(c[1].read());
    }

    unsigned int total_switches() const {
        unsigned int total = 0;
        for (int i = 0; i < 2; i++) { total += fa[i]->total_switches(); }
        return total;
    }

    void reset_switches() {
        for (int i = 0; i < 2; i++) fa[i]->reset_switches();
    }

    void print_report() {
        unsigned int total_switches = 0;

        for (int i = 0; i < 2; i++) {
            total_switches += fa[i]->total_switches();
        }

        std::cout << "RCA-2: GATES=10 SWITCHES=" << total_switches << " DELAY=4ns\n";
    }

    ~RippleCarryAdder2() {
        for (int i = 0; i < 2; i++) {
            delete fa[i];
        }
    }
};

// ==========================================
// 4. TESTBENCH: exhaustive 2-bit test
// ==========================================

// ============================================================================
// Real OpenSTA (Nangate45) max delay per (startpoint pin, endpoint pin),
// extracted from all_critical_paths.csv for RCA-2 (10 pairs).
// ============================================================================
static const int NUM_IN = 4;
static const int NUM_OUT = 3;
static double path_delay[NUM_IN][NUM_OUT];

static void init_path_delay_table() {
    for (int i = 0; i < NUM_IN; i++)
        for (int o = 0; o < NUM_OUT; o++)
            path_delay[i][o] = 0.0;

    path_delay[0][2] = 0.53;
    path_delay[0][0] = 0.36;
    path_delay[0][1] = 0.45;
    path_delay[1][2] = 0.45;
    path_delay[1][1] = 0.36;
    path_delay[2][2] = 0.54;
    path_delay[2][0] = 0.37;
    path_delay[2][1] = 0.46;
    path_delay[3][2] = 0.46;
    path_delay[3][1] = 0.37;
}

// ============================================================================
// TESTBENCH -- exhaustive 2^N x 2^N sweep, per-sample (per-vector) EDP.
// ============================================================================
SC_MODULE(Testbench) {
    sc_out<sc_lv<2>> A, B;
    sc_in<sc_lv<2>> Sum;
    sc_in<bool> Cout;

    RippleCarryAdder2* rca_ptr;

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
        A.write(make_2bit_vector(exh_map(0)));
        B.write(make_2bit_vector(exh_map(0)));
        wait(200, SC_NS);
        prev_a = exh_map(0); prev_b = exh_map(0);
        prev_sum = to_unsigned_2bit(Sum.read());
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

        A.write(make_2bit_vector(a_value));
        B.write(make_2bit_vector(b_value));
        wait(200, SC_NS);

        unsigned int expected_result = a_value + b_value;
        unsigned int actual_sum = to_unsigned_2bit(Sum.read());
        bool actual_cout = Cout.read();
        unsigned int actual_result = actual_sum + (actual_cout ? 4U : 0U);
        if (expected_result != actual_result) number_of_errors++;

        unsigned int switches_i = rca_ptr->total_switches();

        bool in_changed[NUM_IN];
        for (int k = 0; k < 2; k++) in_changed[k] = (((a_value >> k) & 1U) != ((prev_a >> k) & 1U));
        for (int k = 0; k < 2; k++) in_changed[2 + k] = (((b_value >> k) & 1U) != ((prev_b >> k) & 1U));

        bool out_changed[NUM_OUT];
        for (int k = 0; k < 2; k++) out_changed[k] = (((actual_sum >> k) & 1U) != ((prev_sum >> k) & 1U));
        out_changed[2] = (actual_cout != prev_cout);

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
// MAIN -- auto-parallel across all CPU cores via a_value partitioning
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
    sc_signal<sc_lv<2>> A, B, Sum;
    sc_signal<bool> Cout;

    RippleCarryAdder2 rca("RCA2");
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

    // Single continuous process (no forking): preserves cross-vector
    // correlation across the ENTIRE sweep (including across a-value
    // boundaries), matching the established exhaustive methodology
    // exactly. Forking into independent fresh-started workers would
    // break that continuity at every worker boundary.
    double sum_switches = 0.0, sum_delay = 0.0, sum_edp = 0.0;
    unsigned int errors = 0;
    unsigned long long vectors = 0;
    run_slice(0, EXH_COUNT, sum_switches, sum_delay, sum_edp, errors, vectors);

    assert(errors == 0);

    double avg_switches = sum_switches / static_cast<double>(vectors);
    double avg_delay    = sum_delay    / static_cast<double>(vectors);
    double avg_edp_correct = sum_edp   / static_cast<double>(vectors);
    double avg_edp_naive   = avg_switches * avg_delay;

    std::cout << "RCA-2-EXH-pathEDP: VECTORS=" << vectors
              << " AVG_SWITCHES=" << avg_switches
              << " AVG_DELAY_ns=" << avg_delay
              << " AVG_EDP_correct=" << avg_edp_correct
              << " AVG_EDP_naive=" << avg_edp_naive
              << " pct_diff=" << ((avg_edp_naive / avg_edp_correct - 1.0) * 100.0) << "%\n";

    return 0;
}
