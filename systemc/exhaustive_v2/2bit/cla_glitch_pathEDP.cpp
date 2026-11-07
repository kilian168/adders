// cla_glitch.cpp – 2-bit Carry Lookahead Adder, gate-level switching activity
#include <systemc.h>
#include <iostream>
#include <iomanip>
#include <string>
#include <cassert>
#include <thread>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>
#include <vector>

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
    if (bits[0].is_01() && bits[0].to_bool()) value += 1U;
    if (bits[1].is_01() && bits[1].to_bool()) value += 2U;
    return value;
}

// ==========================================
// 0b. GENERIC GATE-LEVEL PRIMITIVE
//     Independently sensitive to each input, with a 1ns delay between
//     evaluation and output commit, so every transient glitch from
//     asynchronously-arriving inputs is counted as a separate switch.
// ==========================================
enum GateOp { GATE_AND, GATE_OR, GATE_XOR };

struct Gate;
static void register_gate(Gate* g);

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

static std::vector<Gate*> g_all_gates;
static void register_gate(Gate* g) { g_all_gates.push_back(g); }
static void reset_all_gate_switches() {
    for (auto* g : g_all_gates) g->reset_switches();
}

// ==========================================
// 1. 2-BIT CARRY LOOKAHEAD ADDER MODULE
//    Gate count: 13
//    PG tier : 2 XOR (P) + 2 AND (G)          =  4 gates
//    C1 logic: 1 AND (P0&Cin) + 1 OR           =  2 gates
//    C2 logic: 3 AND + 2 OR (Cout)             =  5 gates
//    Sum tier: 2 XOR (S0=P0^Cin, S1=P1^C1)    =  2 gates
//    Cin is hardwired 0 for top-level adder.
//    Wired as real per-gate dependencies (not batched tiers).
// ==========================================
SC_MODULE(CarryLookaheadAdder2) {
    sc_in<sc_lv<2>>  A, B;
    sc_out<sc_lv<2>> Sum;
    sc_out<bool>     Cout;

    sc_signal<bool> a_bit[2], b_bit[2];
    sc_signal<bool> p[2], g[2];
    sc_signal<bool> const_zero;
    sc_signal<bool> c1a, c1o;
    sc_signal<bool> c2a1, c2a2, c2a3, c2o1, c2o2;
    sc_signal<bool> s[2];

    Gate *g_p[2], *g_g[2], *g_s[2];
    Gate *g_c1a, *g_c1o;
    Gate *g_c2a1, *g_c2a2, *g_c2a3, *g_c2o1, *g_c2o2;

    SC_CTOR(CarryLookaheadAdder2) {
        SC_METHOD(unpack_inputs);
        dont_initialize();
        sensitive << A << B;

        SC_METHOD(pack_outputs);
        dont_initialize();
        sensitive << s[0] << s[1] << c2o2;

        for (int i = 0; i < 2; i++) {
            std::string pn = "P" + std::to_string(i);
            g_p[i] = new Gate(pn.c_str(), GATE_XOR);
            g_p[i]->in1(a_bit[i]); g_p[i]->in2(b_bit[i]); g_p[i]->out(p[i]);

            std::string gn = "G" + std::to_string(i);
            g_g[i] = new Gate(gn.c_str(), GATE_AND);
            g_g[i]->in1(a_bit[i]); g_g[i]->in2(b_bit[i]); g_g[i]->out(g[i]);
        }

        g_c1a = new Gate("C1A", GATE_AND); g_c1a->in1(p[0]); g_c1a->in2(const_zero); g_c1a->out(c1a);
        g_c1o = new Gate("C1O", GATE_OR);  g_c1o->in1(g[0]); g_c1o->in2(c1a);        g_c1o->out(c1o);

        g_c2a1 = new Gate("C2A1", GATE_AND); g_c2a1->in1(p[1]);  g_c2a1->in2(g[0]);        g_c2a1->out(c2a1);
        g_c2a2 = new Gate("C2A2", GATE_AND); g_c2a2->in1(p[1]);  g_c2a2->in2(p[0]);        g_c2a2->out(c2a2);
        g_c2a3 = new Gate("C2A3", GATE_AND); g_c2a3->in1(c2a2);  g_c2a3->in2(const_zero);  g_c2a3->out(c2a3);
        g_c2o1 = new Gate("C2O1", GATE_OR);  g_c2o1->in1(g[1]);  g_c2o1->in2(c2a1);        g_c2o1->out(c2o1);
        g_c2o2 = new Gate("C2O2", GATE_OR);  g_c2o2->in1(c2o1);  g_c2o2->in2(c2a3);        g_c2o2->out(c2o2);

        g_s[0] = new Gate("S0", GATE_XOR); g_s[0]->in1(p[0]); g_s[0]->in2(const_zero); g_s[0]->out(s[0]);
        g_s[1] = new Gate("S1", GATE_XOR); g_s[1]->in1(p[1]); g_s[1]->in2(c1o);        g_s[1]->out(s[1]);
    }

    void unpack_inputs() {
        sc_lv<2> va = A.read(), vb = B.read();
        for (int i = 0; i < 2; i++) {
            a_bit[i].write(va[i].is_01() ? va[i].to_bool() : false);
            b_bit[i].write(vb[i].is_01() ? vb[i].to_bool() : false);
        }
    }

    void pack_outputs() {
        sc_lv<2> out;
        out[0] = s[0].read();
        out[1] = s[1].read();
        Sum.write(out);
        Cout.write(c2o2.read());
    }

    unsigned int total_switches() const {
        unsigned int t = 0;
        for (int i = 0; i < 2; i++) t += g_p[i]->switches + g_g[i]->switches + g_s[i]->switches;
        t += g_c1a->switches + g_c1o->switches;
        t += g_c2a1->switches + g_c2a2->switches + g_c2a3->switches + g_c2o1->switches + g_c2o2->switches;
        return t;
    }

    void print_report() {
        std::cout << "CLA-2: GATES=13 SWITCHES=" << total_switches() << " DELAY=2ns\n";
    }

    ~CarryLookaheadAdder2() {
        for (int i = 0; i < 2; i++) { delete g_p[i]; delete g_g[i]; delete g_s[i]; }
        delete g_c1a; delete g_c1o;
        delete g_c2a1; delete g_c2a2; delete g_c2a3; delete g_c2o1; delete g_c2o2;
    }
};

// ==========================================
// 2. TESTBENCH – exhaustive 2-bit test
// ==========================================

// ============================================================================
// Real OpenSTA (Nangate45) max delay per (startpoint pin, endpoint pin),
// extracted from all_critical_paths.csv for CLA-2 (10 pairs).
// ============================================================================
static const int NUM_IN = 4;
static const int NUM_OUT = 3;
static double path_delay[NUM_IN][NUM_OUT];

static void init_path_delay_table() {
    for (int i = 0; i < NUM_IN; i++)
        for (int o = 0; o < NUM_OUT; o++)
            path_delay[i][o] = 0.0;

    path_delay[0][2] = 0.51;
    path_delay[0][0] = 0.36;
    path_delay[0][1] = 0.45;
    path_delay[1][2] = 0.61;
    path_delay[1][1] = 0.36;
    path_delay[2][2] = 0.52;
    path_delay[2][0] = 0.38;
    path_delay[2][1] = 0.46;
    path_delay[3][2] = 0.63;
    path_delay[3][1] = 0.38;
}

// ============================================================================
// TESTBENCH -- exhaustive 2^N x 2^N sweep, per-vector EDP. Single
// continuous process (see rca_glitch_pathEDP.cpp for why forking would
// break cross-vector correlation).
// ============================================================================
static long long to_signed_2bit(unsigned int bits) {
    long long mask = 4LL;
    long long half = 2LL;
    long long v = static_cast<long long>(bits & 3U);
    if (v >= half) v -= mask;
    return v;
}

static long long wrap_signed_2bit(long long v) {
    long long mask = 4LL;
    long long half = 2LL;
    long long w = v & (3LL);
    if (w >= half) w -= mask;
    return w;
}

static unsigned int idx_to_bits_2(unsigned int idx) {
    return (2U + idx) & 3U;
}

SC_MODULE(Testbench) {
    sc_out<sc_lv<2>> A, B;
    sc_in<sc_lv<2>>  Sum;
    sc_in<bool>      Cout;

    CarryLookaheadAdder2* cla_ptr;

    unsigned int number_of_errors;
    unsigned int a_start;
    unsigned int a_end;

    double sum_switches, sum_delay, sum_edp;
    unsigned int prev_a, prev_b, prev_sum;
    bool prev_cout;

    SC_CTOR(Testbench)
        : cla_ptr(nullptr), number_of_errors(0), a_start(0), a_end(4),
          sum_switches(0.0), sum_delay(0.0), sum_edp(0.0),
          prev_a(0), prev_b(0), prev_sum(0), prev_cout(false)
    { SC_THREAD(stimulus); }

    void stimulus() {
        bool is_first_worker = (a_start == 0);
        unsigned int warm_a = is_first_worker ? idx_to_bits_2(0) : idx_to_bits_2(a_start - 1);
        unsigned int warm_b = is_first_worker ? idx_to_bits_2(0) : idx_to_bits_2(4U - 1);
        A.write(make_2bit_vector(warm_a));
        B.write(make_2bit_vector(warm_b));
        wait(50, SC_NS);
        prev_a = warm_a; prev_b = warm_b;
        prev_sum = to_unsigned_2bit(Sum.read());
        prev_cout = Cout.read();

        for (unsigned int ia = a_start; ia < a_end; ia++) {
            unsigned int a_value = idx_to_bits_2(ia);
            for (unsigned int ib = 0; ib < 4U; ib++) {
                unsigned int b_value = idx_to_bits_2(ib);
                apply_test(a_value, b_value);
            }
        }

        assert(number_of_errors == 0);
        sc_stop();
    }

    void apply_test(unsigned int a_value, unsigned int b_value) {
        reset_all_gate_switches();

        A.write(make_2bit_vector(a_value));
        B.write(make_2bit_vector(b_value));
        wait(50, SC_NS);

        long long expected_signed = to_signed_2bit(a_value) + to_signed_2bit(b_value);
        unsigned int actual_sum = to_unsigned_2bit(Sum.read());
        bool actual_cout = Cout.read();
        (void)actual_cout;
        long long actual_signed = to_signed_2bit(actual_sum);
        long long wrapped_expected = wrap_signed_2bit(expected_signed);
        if (wrapped_expected != actual_signed) number_of_errors++;

        unsigned int switches_i = cla_ptr->total_switches();

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
    sc_signal<sc_lv<2>> A, B, Sum;
    sc_signal<bool> Cout;

    CarryLookaheadAdder2 cla("CLA2");
    Testbench tb("TB");
    tb.cla_ptr = &cla;

    cla.A(A); cla.B(B); cla.Sum(Sum); cla.Cout(Cout);
    tb.A(A); tb.B(B); tb.Sum(Sum); tb.Cout(Cout);

    tb.a_start = a_start;
    tb.a_end = a_end;

    sc_start();

    out_sum_switches = tb.sum_switches;
    out_sum_delay = tb.sum_delay;
    out_sum_edp = tb.sum_edp;
    out_errors = tb.number_of_errors;
    out_vectors = static_cast<unsigned long long>(a_end - a_start) * 4ULL;
}

int sc_main(int argc, char* argv[]) {
    init_path_delay_table();

    unsigned int num_workers = std::thread::hardware_concurrency();
    if (num_workers == 0) num_workers = 1;
    num_workers = static_cast<unsigned int>(std::min<unsigned long long>(num_workers, 4ULL));

    std::vector<int> read_fd(num_workers);
    std::vector<pid_t> worker_pid(num_workers);
    unsigned int slice = 4U / num_workers;

    for (unsigned int i = 0; i < num_workers; i++) {
        int fds[2];
        if (pipe(fds) != 0) { perror("pipe"); return 1; }

        pid_t child = fork();
        if (child < 0) { perror("fork"); return 1; }

        if (child == 0) {
            close(fds[0]);
            unsigned int a_start = i * slice;
            unsigned int a_end = (i == num_workers - 1) ? 4U : (i + 1) * slice;

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

    std::cout << "CLA-2-EXHV2-pathEDP: VECTORS=" << total_vectors
              << " AVG_SWITCHES=" << avg_switches
              << " AVG_DELAY_ns=" << avg_delay
              << " AVG_EDP_correct=" << avg_edp_correct
              << " AVG_EDP_naive=" << avg_edp_naive
              << " pct_diff=" << ((avg_edp_naive / avg_edp_correct - 1.0) * 100.0) << "%\n";

    return 0;
}
