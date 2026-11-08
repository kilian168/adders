// sdtc_glitch.cpp – 2-bit Signed-Digit / Two's-Complement Carry-Save Adder, gate-level switching activity
//
// Ported from SD_add_TC_4bit.vhdl ("DIG_Add" digit cells wired into the
// "main" entity, generalized from 4 digits to 2). SD_a, SD_b are unsigned
// 2-bit operands and TC is a two's-complement (signed) 2-bit operand.
// Digit i is a single 1-bit full adder over (TC(i), SD_b(i), SD_a(i)) with
// NO carry chain between digits (unlike RCA/CLA), so the whole adder
// settles in one full-adder's gate depth no matter how wide it is. Its
// sum/carry outputs are exposed directly as a redundant (2+1)-bit pair:
//   Sum_a(i)   = sum of digit i         for i = 0..2-1
//   Sum_a(2) = NOT TC(2-1)          (sign-correction top bit)
//   Sum_b(0)   = '0'                    (matches the VHDL's Sum0_b <= '0')
//   Sum_b(i+1) = carry of digit i       for i = 0..2-1
// The testbench checks the redundant-representation identity
// Sum_a + Sum_b - 2^2 == SD_a + SD_b + TC_signed.
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
static sc_lv<2> make_2bit_vector(unsigned int value) {
    sc_lv<2> bits;
    for (int i = 0; i < 2; i++) {
        bits[i] = ((value >> i) & 1U) != 0U;
    }
    return bits;
}

static unsigned int to_unsigned_2bit(const sc_lv<2>& bits) {
    unsigned int value = 0;
    for (int i = 0; i < 2; i++) {
        if (bits[i].is_01() && bits[i].to_bool()) {
            value += (1U << i);
        }
    }
    return value;
}

static unsigned long long to_unsigned_3bit(const sc_lv<3>& bits) {
    unsigned long long value = 0;
    for (int i = 0; i < 3; i++) {
        if (bits[i].is_01() && bits[i].to_bool()) {
            value += (1ULL << i);
        }
    }
    return value;
}

// TC is a two's-complement N-bit operand; decode its signed integer value.
static long long to_signed_twos_complement_2bit(unsigned int value) {
    long long v = static_cast<long long>(value);
    if (value & (1U << (2 - 1))) {
        v -= (1LL << 2);
    }
    return v;
}
// Genuine NAF fed through PPM's UNCHANGED hardware via SD's own dual-rail
// code (rail_a=(d==1), rail_b=(d!=-1) -- a real -1 is (0,0), reachable at
// any digit position). Decode constant becomes 2^(N+1)-1 instead of 2^N.
static void naf_digits_signed_ppm(long long v, int digits[2]) {
    for (int i = 0; i < 2; i++) {
        if (v & 1) {
            int d = 2 - static_cast<int>(v & 3);
            digits[i] = d;
            v = (v - d) / 2;
        } else {
            digits[i] = 0;
            v = v / 2;
        }
    }
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

    unsigned int total_switches() const {
        return g_xor1->switches + g_and1->switches + g_xor2->switches + g_and2->switches + g_or1->switches;
    }

    ~FullAdder() {
        delete g_xor1; delete g_and1; delete g_xor2; delete g_and2; delete g_or1;
    }
};

// ==========================================
// 3. 2-BIT SIGNED-DIGIT / TWO'S-COMPLEMENT ADDER
//    2 independent full adders, one per digit, with NO carry chain
//    between them. Gate count: 2 x 5 (full adders) + 1 (NOT) = 11
// ==========================================
SC_MODULE(SdTcAdder2) {
    sc_in<sc_lv<2>> SD_a;
    sc_in<sc_lv<2>> SD_b;
    sc_in<sc_lv<2>> TC;

    sc_out<sc_lv<3>> Sum_a;
    sc_out<sc_lv<3>> Sum_b;

    sc_signal<bool> sda_bit[2];
    sc_signal<bool> sdb_bit[2];
    sc_signal<bool> tc_bit[2];
    sc_signal<bool> sum_bit[2];
    sc_signal<bool> carry_bit[2];
    sc_signal<bool> const_zero;
    sc_signal<bool> top_bit;

    FullAdder* fa[2];
    GateNot* g_not_top;

    SC_CTOR(SdTcAdder2) {
        for (int i = 0; i < 2; i++) {
            std::string name = "FA_" + std::to_string(i);
            fa[i] = new FullAdder(name.c_str());

            fa[i]->a(tc_bit[i]);
            fa[i]->b(sdb_bit[i]);
            fa[i]->cin(sda_bit[i]);
            fa[i]->sum(sum_bit[i]);
            fa[i]->cout(carry_bit[i]);
        }

        g_not_top = new GateNot("NOT_TOP");
        g_not_top->in1(tc_bit[2 - 1]);
        g_not_top->out(top_bit);

        SC_METHOD(split_inputs);
        sensitive << SD_a << SD_b << TC;

        SC_METHOD(combine_outputs);
        for (int i = 0; i < 2; i++) {
            sensitive << sum_bit[i];
            sensitive << carry_bit[i];
        }
        sensitive << top_bit;
    }

    void split_inputs() {
        sc_lv<2> value_sda = SD_a.read();
        sc_lv<2> value_sdb = SD_b.read();
        sc_lv<2> value_tc = TC.read();

        for (int i = 0; i < 2; i++) {
            sda_bit[i].write(value_sda[i].is_01() ? value_sda[i].to_bool() : false);
            sdb_bit[i].write(value_sdb[i].is_01() ? value_sdb[i].to_bool() : false);
            tc_bit[i].write(value_tc[i].is_01() ? value_tc[i].to_bool() : false);
        }
    }

    void combine_outputs() {
        sc_lv<3> value_sum_a;
        sc_lv<3> value_sum_b;

        for (int i = 0; i < 2; i++) {
            value_sum_a[i] = sum_bit[i].read();
            value_sum_b[i + 1] = carry_bit[i].read();
        }
        value_sum_a[2] = top_bit.read();
        value_sum_b[0] = const_zero.read();

        Sum_a.write(value_sum_a);
        Sum_b.write(value_sum_b);
    }

    unsigned int total_switches() const {
        unsigned int total = 0;
        for (int i = 0; i < 2; i++) {
            total += fa[i]->total_switches();
        }
        total += g_not_top->switches;
        return total;
    }

    ~SdTcAdder2() {
        for (int i = 0; i < 2; i++) {
            delete fa[i];
        }
        delete g_not_top;
    }
};

// ==========================================
// 4. TESTBENCH -- Monte Carlo random sampling
//    Draws num_samples uniformly random (SD_a, SD_b, TC) triples instead
//    of exhaustively covering the whole input space: at 2 bits exhaustive
//    coverage (2^2 x 2^2 x 2^2 cases) is computationally infeasible, so
//    Monte Carlo sampling is the only tractable option.
//
//    SD_a, SD_b are unsigned; TC is two's-complement signed. Correctness
//    is checked against the redundant-representation identity
//    Sum_a + Sum_b - 2^N == SD_a + SD_b + TC_signed (see header comment).
// ==========================================

// ============================================================================
// Real OpenSTA (Nangate45) max delay per (startpoint pin, endpoint pin),
// extracted from all_critical_paths.csv for SDTC-PPM-2 (13 pairs).
// Input pin index: 0..1=SD_a[i], 2..3=SD_b[i], 4..5=TC[i].
// Output pin index: 0..2=Sum_a[i], 3..5=Sum_b[i].
// ============================================================================
static const int NUM_IN = 6;
static const int NUM_OUT = 6;
static double path_delay[NUM_IN][NUM_OUT];

static void init_path_delay_table() {
    for (int i = 0; i < NUM_IN; i++)
        for (int o = 0; o < NUM_OUT; o++)
            path_delay[i][o] = 0.0;

    path_delay[0][0] = 0.18;
    path_delay[0][4] = 0.25;
    path_delay[1][1] = 0.18;
    path_delay[1][5] = 0.25;
    path_delay[2][0] = 0.37;
    path_delay[2][4] = 0.46;
    path_delay[3][1] = 0.37;
    path_delay[3][5] = 0.46;
    path_delay[4][0] = 0.36;
    path_delay[4][4] = 0.45;
    path_delay[5][1] = 0.36;
    path_delay[5][2] = 0.01;
    path_delay[5][5] = 0.45;
}

// ============================================================================
// TESTBENCH -- Monte Carlo, per-sample EDP. SD_a, SD_b, TC each drawn as
// independent uniform random N-bit values (same as the original Monte
// Carlo methodology -- SD_a/SD_b are not drawn as independent redundant
// digits here, just plain N-bit values, so there's no NAF-style encoding
// question for this adder the way there was for SD).
// ============================================================================
SC_MODULE(Testbench) {
    sc_out<sc_lv<2>> SD_a, SD_b, TC;
    sc_in<sc_lv<3>> Sum_a, Sum_b;

    SdTcAdder2* sdtc_ptr;

    unsigned int number_of_errors;
    unsigned long long num_samples;
    unsigned long long rng_seed;

    double sum_switches, sum_delay, sum_edp;
    unsigned int prev_sda, prev_sdb, prev_tc;
    unsigned int prev_sum_a, prev_sum_b;

    SC_CTOR(Testbench)
        : sdtc_ptr(nullptr), number_of_errors(0), num_samples(1000000ULL), rng_seed(42ULL),
          sum_switches(0.0), sum_delay(0.0), sum_edp(0.0),
          prev_sda(0), prev_sdb(0), prev_tc(0), prev_sum_a(0), prev_sum_b(0)
    {
        SC_THREAD(stimulus);
    }

    void stimulus() {
        std::mt19937_64 rng(rng_seed);
        std::uniform_int_distribution<unsigned long long> dist(0ULL, 3ULL);

        SD_a.write(make_2bit_vector(0));
        SD_b.write(make_2bit_vector(0));
        TC.write(make_2bit_vector(0));
        wait(200, SC_NS);
        prev_sda = 0; prev_sdb = 0; prev_tc = 0;
        prev_sum_a = static_cast<unsigned int>(to_unsigned_3bit(Sum_a.read()));
        prev_sum_b = static_cast<unsigned int>(to_unsigned_3bit(Sum_b.read()));

        std::uniform_int_distribution<long long> sd_dist(-2LL, 1LL);
        for (unsigned long long i = 0; i < num_samples; i++) {
            long long sd_value = sd_dist(rng);
            int digits[2];
            naf_digits_signed_ppm(sd_value, digits);
            unsigned int sda_value = 0, sdb_value = 0;
            for (int k = 0; k < 2; k++) {
                if (digits[k] == 1) sda_value |= (1U << k);
                if (digits[k] != -1) sdb_value |= (1U << k);
            }
            unsigned int tc_value  = static_cast<unsigned int>(dist(rng));
            apply_test(sda_value, sdb_value, tc_value);
        }

        assert(number_of_errors == 0);
        sc_stop();
    }

    void apply_test(unsigned int sda_value, unsigned int sdb_value, unsigned int tc_value) {
        reset_all_gate_switches();

        SD_a.write(make_2bit_vector(sda_value));
        SD_b.write(make_2bit_vector(sdb_value));
        TC.write(make_2bit_vector(tc_value));

        wait(200, SC_NS);

        long long expected_result = static_cast<long long>(sda_value)
                                   + static_cast<long long>(sdb_value)
                                   - ((1LL << 2) - 1)
                                   + to_signed_twos_complement_2bit(tc_value);

        sc_lv<3> sum_a_val = Sum_a.read();
        sc_lv<3> sum_b_val = Sum_b.read();
        unsigned int actual_sum_a = static_cast<unsigned int>(to_unsigned_3bit(sum_a_val));
        unsigned int actual_sum_b = static_cast<unsigned int>(to_unsigned_3bit(sum_b_val));

        long long actual_result = static_cast<long long>(actual_sum_a)
                                 + static_cast<long long>(actual_sum_b)
                                 - ((1LL << 3) - 1);

        if (expected_result != actual_result) {
            number_of_errors++;
        }

        unsigned int switches_i = sdtc_ptr->total_switches();

        bool in_changed[NUM_IN];
        for (int k = 0; k < 2; k++) in_changed[k] = (((sda_value >> k) & 1U) != ((prev_sda >> k) & 1U));
        for (int k = 0; k < 2; k++) in_changed[2 + k] = (((sdb_value >> k) & 1U) != ((prev_sdb >> k) & 1U));
        for (int k = 0; k < 2; k++) in_changed[4 + k] = (((tc_value >> k) & 1U) != ((prev_tc >> k) & 1U));

        bool out_changed[NUM_OUT];
        for (int k = 0; k <= 2; k++) out_changed[k] = (((actual_sum_a >> k) & 1U) != ((prev_sum_a >> k) & 1U));
        for (int k = 0; k <= 2; k++) out_changed[3 + k] = (((actual_sum_b >> k) & 1U) != ((prev_sum_b >> k) & 1U));

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
    sc_signal<sc_lv<2>> SD_a, SD_b, TC;
    sc_signal<sc_lv<3>> Sum_a, Sum_b;

    SdTcAdder2 sdtc("SDTC2");
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

    std::cout << "SDTC-PPM-2-MC-pathEDP: SAMPLES=" << total_samples
              << " AVG_SWITCHES=" << avg_switches
              << " AVG_DELAY_ns=" << avg_delay
              << " AVG_EDP_correct=" << avg_edp_correct
              << " AVG_EDP_naive=" << avg_edp_naive
              << " pct_diff=" << ((avg_edp_naive / avg_edp_correct - 1.0) * 100.0) << "%\n";

    return 0;
}
