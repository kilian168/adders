// sdtc_glitch.cpp – 2-bit Signed-Digit / Two's-Complement Carry-Save Adder, gate-level switching activity
//
// Ported from SD_add_TC_4bit.vhdl ("DIG_Add" digit cells wired into the
// "main" entity, generalized from 4 digits to 2). SD_a/SD_b together are a
// PM (plus-minus) signed-digit number: digit i = SD_a(i) - SD_b(i) in
// {-1,0,1} ("10"=+1, "01"=-1, "00"/"11"=0), which sums to plain
// SD_a_unsigned - SD_b_unsigned. TC is a two's-complement (signed) 2-bit
// operand. Digit i is a single digit cell over (TC(i), SD_b(i), SD_a(i))
// with NO carry chain between digits (unlike RCA/CLA), so the whole adder
// settles in one cell's gate depth no matter how wide it is. Its
// sum/carry outputs are exposed directly as a redundant (2+1)-bit pair:
//   Sum_a(i)   = sum of digit i         for i = 0..2-1
//   Sum_a(2) = NOT TC(2-1)          (sign-correction top bit)
//   Sum_b(0)   = '0'                    (matches the VHDL's Sum0_b <= '0')
//   Sum_b(i+1) = carry of digit i       for i = 0..2-1
// Each digit contributes 2*carry - sum (not 2*carry + sum) to the total, so
// the testbench checks Sum_b - Sum_a + 2^2*(TC(2-1)?-1:+1) == SD_a - SD_b + TC_signed.
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
// 3. 2-BIT SIGNED-DIGIT / TWO'S-COMPLEMENT ADDER
//    2 independent digit cells (7 gates each), one per digit, with NO
//    carry chain between them. Gate count: 2 x 7 (digit cells) + 1 (NOT) = 15
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
//    SD_a/SD_b are the PM signed-digit rails (digit i = SD_a(i)-SD_b(i));
//    TC is two's-complement signed. Correctness is checked against the
//    redundant-representation identity Sum_b - Sum_a + 2^N*(+-1) ==
//    SD_a - SD_b + TC_signed (see header comment and apply_test below).
// ==========================================

// ============================================================================
// Real OpenSTA (Nangate45) max delay per (startpoint pin, endpoint pin),
// extracted from all_critical_paths.csv for SDTC-PM-2 (13 pairs).
// ============================================================================
static const int NUM_IN = 6;
static const int NUM_OUT = 6;
static double path_delay[NUM_IN][NUM_OUT];

static void init_path_delay_table() {
    for (int i = 0; i < NUM_IN; i++)
        for (int o = 0; o < NUM_OUT; o++)
            path_delay[i][o] = 0.0;

    path_delay[0][0] = 0.36;
    path_delay[0][4] = 0.47;
    path_delay[1][1] = 0.36;
    path_delay[1][5] = 0.47;
    path_delay[2][0] = 0.37;
    path_delay[2][4] = 0.46;
    path_delay[3][1] = 0.37;
    path_delay[3][5] = 0.46;
    path_delay[4][0] = 0.18;
    path_delay[4][4] = 0.25;
    path_delay[5][1] = 0.18;
    path_delay[5][2] = 0.01;
    path_delay[5][5] = 0.25;
}

// ============================================================================
// TESTBENCH -- exhaustive 2^N x 2^N sweep, matching SD's "dropped NAF"
// approach: SD_a/SD_b together are a REDUNDANT dual-rail signed-digit
// encoding of ONE operand (digit i = SD_a(i) - SD_b(i) in {-1,0,+1},
// same structure as SD's A_a/A_b), so instead of exhaustively covering
// every (SD_a, SD_b) combination (2^N x 2^N on their own, on top of TC's
// own 2^N -- 2^(3N) total), each SD value v is encoded canonically:
// SD_a = v's own bits, SD_b = 0 always (never uses the -1 digit state,
// exactly like SD-BIN's plain-binary-digit encoding). That leaves a
// genuine 2-operand sweep: SD value x TC value, 2^N x 2^N total vectors.
// Single continuous process (no forking) to preserve cross-vector
// correlation, same as RCA/CLA/SD's exhaustive methodology.
// ============================================================================
// True NAF (Non-Adjacent Form) / Canonical Signed Digit recoding of the SD
// operand -- real -1 digits used (unlike v1's dropped-NAF). Fits in exactly
// N digit positions for the signed N-bit range, verified same as standalone
// SD's v2. TC stays untouched (already signed two's-complement as in v1).
static void naf_digits_signed(long long v, int digits[], int n) {
    for (int i = 0; i < n; i++) digits[i] = 0;
    long long rem = v;
    int i = 0;
    while (rem != 0) {
        int d = static_cast<int>(2LL - (rem & 3LL));
        if (rem & 1LL) {
            digits[i] = d;
            rem -= d;
        }
        rem >>= 1;
        i++;
    }
}

SC_MODULE(Testbench) {
    sc_out<sc_lv<2>> SD_a, SD_b, TC;
    sc_in<sc_lv<3>> Sum_a, Sum_b;

    SdTcAdder2* sdtc_ptr;

    unsigned int number_of_errors;
    long long sd_start;
    long long sd_end;

    double sum_switches, sum_delay, sum_edp;
    unsigned int prev_sda_bits, prev_sdb_bits, prev_tc;
    unsigned int prev_sum_a, prev_sum_b;

    SC_CTOR(Testbench)
        : sdtc_ptr(nullptr), number_of_errors(0), sd_start(-2LL), sd_end(2LL),
          sum_switches(0.0), sum_delay(0.0), sum_edp(0.0),
          prev_sda_bits(0), prev_sdb_bits(0), prev_tc(0), prev_sum_a(0), prev_sum_b(0)
    {
        SC_THREAD(stimulus);
    }

    void stimulus() {
        bool is_first_worker = (sd_start == -2LL);
        long long warm_sd_signed = is_first_worker ? sd_start : (sd_start - 1);
        unsigned int warm_tc = is_first_worker ? 0U : 3U;

        int warm_digits[2];
        naf_digits_signed(warm_sd_signed, warm_digits, 2);
        unsigned int warm_sda = 0, warm_sdb = 0;
        for (int i = 0; i < 2; i++) {
            if (warm_digits[i] == 1) warm_sda |= (1U << i);
            if (warm_digits[i] == -1) warm_sdb |= (1U << i);
        }

        SD_a.write(make_2bit_vector(warm_sda));
        SD_b.write(make_2bit_vector(warm_sdb));
        TC.write(make_2bit_vector(warm_tc));
        wait(200, SC_NS);
        prev_sda_bits = warm_sda; prev_sdb_bits = warm_sdb; prev_tc = warm_tc;
        prev_sum_a = static_cast<unsigned int>(to_unsigned_3bit(Sum_a.read()));
        prev_sum_b = static_cast<unsigned int>(to_unsigned_3bit(Sum_b.read()));

        for (long long sd_value = sd_start; sd_value < sd_end; sd_value++) {
            for (unsigned int tc_value = 0; tc_value < 4U; tc_value++) {
                apply_test(sd_value, tc_value);
            }
        }

        assert(number_of_errors == 0);
        sc_stop();
    }

    void apply_test(long long sd_value, unsigned int tc_value) {
        reset_all_gate_switches();

        int sd_digits[2];
        naf_digits_signed(sd_value, sd_digits, 2);
        unsigned int sda_bits = 0, sdb_bits = 0;
        for (int i = 0; i < 2; i++) {
            if (sd_digits[i] == 1) sda_bits |= (1U << i);
            if (sd_digits[i] == -1) sdb_bits |= (1U << i);
        }

        SD_a.write(make_2bit_vector(sda_bits));
        SD_b.write(make_2bit_vector(sdb_bits));
        TC.write(make_2bit_vector(tc_value));

        wait(200, SC_NS);

        long long expected_result = sd_value + to_signed_twos_complement_2bit(tc_value);

        unsigned int tc_top = (tc_value >> (2 - 1)) & 1U;
        long long sign_term = tc_top ? -(1LL << 2) : (1LL << 2);

        sc_lv<3> sum_a_val = Sum_a.read();
        sc_lv<3> sum_b_val = Sum_b.read();
        unsigned int actual_sum_a = static_cast<unsigned int>(to_unsigned_3bit(sum_a_val));
        unsigned int actual_sum_b = static_cast<unsigned int>(to_unsigned_3bit(sum_b_val));

        long long actual_result = static_cast<long long>(actual_sum_b)
                                 - static_cast<long long>(actual_sum_a)
                                 + sign_term;

        if (expected_result != actual_result) {
            number_of_errors++;
        }

        unsigned int switches_i = sdtc_ptr->total_switches();

        bool in_changed[NUM_IN];
        for (int k = 0; k < 2; k++) in_changed[k] = (((sda_bits >> k) & 1U) != ((prev_sda_bits >> k) & 1U));
        for (int k = 0; k < 2; k++) in_changed[2 + k] = (((sdb_bits >> k) & 1U) != ((prev_sdb_bits >> k) & 1U));
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

        prev_sda_bits = sda_bits;
        prev_sdb_bits = sdb_bits;
        prev_tc = tc_value;
        prev_sum_a = actual_sum_a;
        prev_sum_b = actual_sum_b;
    }
};

// ==========================================
// MAIN -- fork-based partitioning over the SIGNED sd range.
// ==========================================
struct WorkerResult {
    double sum_switches;
    double sum_delay;
    double sum_edp;
    unsigned long long errors;
};

static void run_slice(long long sd_start, long long sd_end,
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

    tb.sd_start = sd_start;
    tb.sd_end = sd_end;

    sc_start();

    out_sum_switches = tb.sum_switches;
    out_sum_delay = tb.sum_delay;
    out_sum_edp = tb.sum_edp;
    out_errors = tb.number_of_errors;
}

int sc_main(int argc, char* argv[]) {
    init_path_delay_table();

    unsigned int num_workers = std::thread::hardware_concurrency();
    if (num_workers == 0) num_workers = 1;
    num_workers = static_cast<unsigned int>(std::min<unsigned long long>(num_workers, 4ULL));

    std::vector<int> read_fd(num_workers);
    std::vector<pid_t> worker_pid(num_workers);
    long long slice = 4LL / static_cast<long long>(num_workers);

    for (unsigned int i = 0; i < num_workers; i++) {
        int fds[2];
        if (pipe(fds) != 0) { perror("pipe"); return 1; }

        pid_t child = fork();
        if (child < 0) { perror("fork"); return 1; }

        if (child == 0) {
            close(fds[0]);
            long long sd_start = -2LL + static_cast<long long>(i) * slice;
            long long sd_end = (i == num_workers - 1) ? 2LL : (-2LL + static_cast<long long>(i + 1) * slice);

            WorkerResult result{};
            double sw = 0.0, de = 0.0, edp = 0.0;
            unsigned int err = 0;
            run_slice(sd_start, sd_end, sw, de, edp, err);
            result.sum_switches = sw; result.sum_delay = de; result.sum_edp = edp;
            result.errors = err;

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

    unsigned long long vectors = 4ULL * 4ULL;
    double avg_switches = total_sum_switches / static_cast<double>(vectors);
    double avg_delay    = total_sum_delay    / static_cast<double>(vectors);
    double avg_edp_correct = total_sum_edp   / static_cast<double>(vectors);
    double avg_edp_naive   = avg_switches * avg_delay;

    std::cout << "SDTC-PM-2-EXHV2-pathEDP: VECTORS=" << vectors
              << " AVG_SWITCHES=" << avg_switches
              << " AVG_DELAY_ns=" << avg_delay
              << " AVG_EDP_correct=" << avg_edp_correct
              << " AVG_EDP_naive=" << avg_edp_naive
              << " pct_diff=" << ((avg_edp_naive / avg_edp_correct - 1.0) * 100.0) << "%\n";

    return 0;
}
