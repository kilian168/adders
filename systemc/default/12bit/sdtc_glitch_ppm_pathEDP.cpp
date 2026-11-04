// sdtc_glitch.cpp – 12-bit Signed-Digit / Two's-Complement Carry-Save Adder, gate-level switching activity
//
// Ported from SD_add_TC_4bit.vhdl ("DIG_Add" digit cells wired into the
// "main" entity, generalized from 4 digits to 12). SD_a, SD_b are unsigned
// 12-bit operands and TC is a two's-complement (signed) 12-bit operand.
// Digit i is a single 1-bit full adder over (TC(i), SD_b(i), SD_a(i)) with
// NO carry chain between digits (unlike RCA/CLA), so the whole adder
// settles in one full-adder's gate depth no matter how wide it is. Its
// sum/carry outputs are exposed directly as a redundant (12+1)-bit pair:
//   Sum_a(i)   = sum of digit i         for i = 0..12-1
//   Sum_a(12) = NOT TC(12-1)          (sign-correction top bit)
//   Sum_b(0)   = '0'                    (matches the VHDL's Sum0_b <= '0')
//   Sum_b(i+1) = carry of digit i       for i = 0..12-1
// The testbench checks the redundant-representation identity
// Sum_a + Sum_b - 2^12 == SD_a + SD_b + TC_signed.
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
static sc_lv<12> make_12bit_vector(unsigned int value) {
    sc_lv<12> bits;
    for (int i = 0; i < 12; i++) {
        bits[i] = ((value >> i) & 1U) != 0U;
    }
    return bits;
}

static unsigned int to_unsigned_12bit(const sc_lv<12>& bits) {
    unsigned int value = 0;
    for (int i = 0; i < 12; i++) {
        if (bits[i].is_01() && bits[i].to_bool()) {
            value += (1U << i);
        }
    }
    return value;
}

static unsigned long long to_unsigned_13bit(const sc_lv<13>& bits) {
    unsigned long long value = 0;
    for (int i = 0; i < 13; i++) {
        if (bits[i].is_01() && bits[i].to_bool()) {
            value += (1ULL << i);
        }
    }
    return value;
}

// TC is a two's-complement N-bit operand; decode its signed integer value.
static long long to_signed_twos_complement_12bit(unsigned int value) {
    long long v = static_cast<long long>(value);
    if (value & (1U << (12 - 1))) {
        v -= (1LL << 12);
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
// 3. 12-BIT SIGNED-DIGIT / TWO'S-COMPLEMENT ADDER
//    12 independent full adders, one per digit, with NO carry chain
//    between them. Gate count: 12 x 5 (full adders) + 1 (NOT) = 61
// ==========================================
SC_MODULE(SdTcAdder12) {
    sc_in<sc_lv<12>> SD_a;
    sc_in<sc_lv<12>> SD_b;
    sc_in<sc_lv<12>> TC;

    sc_out<sc_lv<13>> Sum_a;
    sc_out<sc_lv<13>> Sum_b;

    sc_signal<bool> sda_bit[12];
    sc_signal<bool> sdb_bit[12];
    sc_signal<bool> tc_bit[12];
    sc_signal<bool> sum_bit[12];
    sc_signal<bool> carry_bit[12];
    sc_signal<bool> const_zero;
    sc_signal<bool> top_bit;

    FullAdder* fa[12];
    GateNot* g_not_top;

    SC_CTOR(SdTcAdder12) {
        for (int i = 0; i < 12; i++) {
            std::string name = "FA_" + std::to_string(i);
            fa[i] = new FullAdder(name.c_str());

            fa[i]->a(tc_bit[i]);
            fa[i]->b(sdb_bit[i]);
            fa[i]->cin(sda_bit[i]);
            fa[i]->sum(sum_bit[i]);
            fa[i]->cout(carry_bit[i]);
        }

        g_not_top = new GateNot("NOT_TOP");
        g_not_top->in1(tc_bit[12 - 1]);
        g_not_top->out(top_bit);

        SC_METHOD(split_inputs);
        sensitive << SD_a << SD_b << TC;

        SC_METHOD(combine_outputs);
        for (int i = 0; i < 12; i++) {
            sensitive << sum_bit[i];
            sensitive << carry_bit[i];
        }
        sensitive << top_bit;
    }

    void split_inputs() {
        sc_lv<12> value_sda = SD_a.read();
        sc_lv<12> value_sdb = SD_b.read();
        sc_lv<12> value_tc = TC.read();

        for (int i = 0; i < 12; i++) {
            sda_bit[i].write(value_sda[i].is_01() ? value_sda[i].to_bool() : false);
            sdb_bit[i].write(value_sdb[i].is_01() ? value_sdb[i].to_bool() : false);
            tc_bit[i].write(value_tc[i].is_01() ? value_tc[i].to_bool() : false);
        }
    }

    void combine_outputs() {
        sc_lv<13> value_sum_a;
        sc_lv<13> value_sum_b;

        for (int i = 0; i < 12; i++) {
            value_sum_a[i] = sum_bit[i].read();
            value_sum_b[i + 1] = carry_bit[i].read();
        }
        value_sum_a[12] = top_bit.read();
        value_sum_b[0] = const_zero.read();

        Sum_a.write(value_sum_a);
        Sum_b.write(value_sum_b);
    }

    unsigned int total_switches() const {
        unsigned int total = 0;
        for (int i = 0; i < 12; i++) {
            total += fa[i]->total_switches();
        }
        total += g_not_top->switches;
        return total;
    }

    ~SdTcAdder12() {
        for (int i = 0; i < 12; i++) {
            delete fa[i];
        }
        delete g_not_top;
    }
};

// ==========================================
// 4. TESTBENCH -- Monte Carlo random sampling
//    Draws num_samples uniformly random (SD_a, SD_b, TC) triples instead
//    of exhaustively covering the whole input space: at 12 bits exhaustive
//    coverage (2^12 x 2^12 x 2^12 cases) is computationally infeasible, so
//    Monte Carlo sampling is the only tractable option.
//
//    SD_a, SD_b are unsigned; TC is two's-complement signed. Correctness
//    is checked against the redundant-representation identity
//    Sum_a + Sum_b - 2^N == SD_a + SD_b + TC_signed (see header comment).
// ==========================================

// ============================================================================
// Real OpenSTA (Nangate45) max delay per (startpoint pin, endpoint pin),
// extracted from all_critical_paths.csv for SDTC-PPM-12 (73 pairs).
// Input pin index: 0..11=SD_a[i], 12..23=SD_b[i], 24..35=TC[i].
// Output pin index: 0..12=Sum_a[i], 13..25=Sum_b[i].
// ============================================================================
static const int NUM_IN = 36;
static const int NUM_OUT = 26;
static double path_delay[NUM_IN][NUM_OUT];

static void init_path_delay_table() {
    for (int i = 0; i < NUM_IN; i++)
        for (int o = 0; o < NUM_OUT; o++)
            path_delay[i][o] = 0.0;

    path_delay[0][0] = 0.18;
    path_delay[0][14] = 0.25;
    path_delay[10][10] = 0.18;
    path_delay[10][24] = 0.25;
    path_delay[11][11] = 0.18;
    path_delay[11][25] = 0.25;
    path_delay[1][1] = 0.18;
    path_delay[1][15] = 0.25;
    path_delay[2][2] = 0.18;
    path_delay[2][16] = 0.25;
    path_delay[3][3] = 0.18;
    path_delay[3][17] = 0.25;
    path_delay[4][4] = 0.18;
    path_delay[4][18] = 0.25;
    path_delay[5][5] = 0.18;
    path_delay[5][19] = 0.25;
    path_delay[6][6] = 0.18;
    path_delay[6][20] = 0.25;
    path_delay[7][7] = 0.18;
    path_delay[7][21] = 0.25;
    path_delay[8][8] = 0.18;
    path_delay[8][22] = 0.25;
    path_delay[9][9] = 0.18;
    path_delay[9][23] = 0.25;
    path_delay[12][0] = 0.37;
    path_delay[12][14] = 0.46;
    path_delay[22][10] = 0.37;
    path_delay[22][24] = 0.46;
    path_delay[23][11] = 0.37;
    path_delay[23][25] = 0.46;
    path_delay[13][1] = 0.37;
    path_delay[13][15] = 0.46;
    path_delay[14][2] = 0.37;
    path_delay[14][16] = 0.46;
    path_delay[15][3] = 0.37;
    path_delay[15][17] = 0.46;
    path_delay[16][4] = 0.37;
    path_delay[16][18] = 0.46;
    path_delay[17][5] = 0.37;
    path_delay[17][19] = 0.46;
    path_delay[18][6] = 0.37;
    path_delay[18][20] = 0.46;
    path_delay[19][7] = 0.37;
    path_delay[19][21] = 0.46;
    path_delay[20][8] = 0.37;
    path_delay[20][22] = 0.46;
    path_delay[21][9] = 0.37;
    path_delay[21][23] = 0.46;
    path_delay[24][0] = 0.36;
    path_delay[24][14] = 0.45;
    path_delay[34][10] = 0.36;
    path_delay[34][24] = 0.45;
    path_delay[35][11] = 0.36;
    path_delay[35][12] = 0.01;
    path_delay[35][25] = 0.45;
    path_delay[25][1] = 0.36;
    path_delay[25][15] = 0.45;
    path_delay[26][2] = 0.36;
    path_delay[26][16] = 0.45;
    path_delay[27][3] = 0.36;
    path_delay[27][17] = 0.45;
    path_delay[28][4] = 0.36;
    path_delay[28][18] = 0.45;
    path_delay[29][5] = 0.36;
    path_delay[29][19] = 0.45;
    path_delay[30][6] = 0.36;
    path_delay[30][20] = 0.45;
    path_delay[31][7] = 0.36;
    path_delay[31][21] = 0.45;
    path_delay[32][8] = 0.36;
    path_delay[32][22] = 0.45;
    path_delay[33][9] = 0.36;
    path_delay[33][23] = 0.45;
}

// ============================================================================
// TESTBENCH -- Monte Carlo, per-sample EDP. SD_a, SD_b, TC each drawn as
// independent uniform random N-bit values (same as the original Monte
// Carlo methodology -- SD_a/SD_b are not drawn as independent redundant
// digits here, just plain N-bit values, so there's no NAF-style encoding
// question for this adder the way there was for TER).
// ============================================================================

// ============================================================================
// TESTBENCH -- exhaustive 2^N x 2^N sweep (canonical SD encoding: SD_a = value's
// own bits, SD_b = 0 always, matching TER-BIN/the 2/4/8-bit canonical tables).
// Auto-parallel across CPU cores via sd_value partitioning; each worker warms
// up to the TRUE predecessor vector (sd_start-1, 4096-1) before its own counted
// slice (or the all-zero state for worker 0), so per-vector switches/delay
// exactly match what a single continuous process would compute at that point --
// verified to reproduce the single-process AVG_SWITCHES exactly at 8-bit.
// ============================================================================
SC_MODULE(Testbench) {
    sc_out<sc_lv<12>> SD_a, SD_b, TC;
    sc_in<sc_lv<13>> Sum_a, Sum_b;

    SdTcAdder12* sdtc_ptr;

    unsigned int number_of_errors;
    unsigned int sd_start;
    unsigned int sd_end;

    double sum_switches, sum_delay, sum_edp;
    unsigned int prev_sd, prev_tc;
    unsigned int prev_sum_a, prev_sum_b;

    SC_CTOR(Testbench)
        : sdtc_ptr(nullptr), number_of_errors(0), sd_start(0), sd_end(4096U),
          sum_switches(0.0), sum_delay(0.0), sum_edp(0.0),
          prev_sd(0), prev_tc(0), prev_sum_a(0), prev_sum_b(0)
    {
        SC_THREAD(stimulus);
    }

    void stimulus() {
        unsigned int warm_sd = (sd_start == 0) ? 0U : (sd_start - 1);
        unsigned int warm_tc = (sd_start == 0) ? 0U : 4095U;

        SD_a.write(make_12bit_vector(warm_sd));
        SD_b.write(make_12bit_vector(0));
        TC.write(make_12bit_vector(warm_tc));
        wait(200, SC_NS);
        prev_sd = warm_sd; prev_tc = warm_tc;
        prev_sum_a = static_cast<unsigned int>(to_unsigned_13bit(Sum_a.read()));
        prev_sum_b = static_cast<unsigned int>(to_unsigned_13bit(Sum_b.read()));

        for (unsigned int sd_value = sd_start; sd_value < sd_end; sd_value++) {
            for (unsigned int tc_value = 0; tc_value < 4096U; tc_value++) {
                apply_test(sd_value, tc_value);
            }
        }

        assert(number_of_errors == 0);
        sc_stop();
    }

    void apply_test(unsigned int sd_value, unsigned int tc_value) {
        reset_all_gate_switches();

        SD_a.write(make_12bit_vector(sd_value));
        SD_b.write(make_12bit_vector(0));
        TC.write(make_12bit_vector(tc_value));

        wait(200, SC_NS);

        long long expected_result = static_cast<long long>(sd_value)
                                   + to_signed_twos_complement_12bit(tc_value);

        sc_lv<13> sum_a_val = Sum_a.read();
        sc_lv<13> sum_b_val = Sum_b.read();
        unsigned int actual_sum_a = static_cast<unsigned int>(to_unsigned_13bit(sum_a_val));
        unsigned int actual_sum_b = static_cast<unsigned int>(to_unsigned_13bit(sum_b_val));

        long long actual_result = static_cast<long long>(actual_sum_a)
                                 + static_cast<long long>(actual_sum_b)
                                 - (1LL << 12);

        if (expected_result != actual_result) {
            number_of_errors++;
        }

        unsigned int switches_i = sdtc_ptr->total_switches();

        bool in_changed[NUM_IN];
        for (int k = 0; k < 12; k++) in_changed[k] = (((sd_value >> k) & 1U) != ((prev_sd >> k) & 1U));
        for (int k = 0; k < 12; k++) in_changed[12 + k] = false;  // SD_b is always 0, never changes
        for (int k = 0; k < 12; k++) in_changed[24 + k] = (((tc_value >> k) & 1U) != ((prev_tc >> k) & 1U));

        bool out_changed[NUM_OUT];
        for (int k = 0; k <= 12; k++) out_changed[k] = (((actual_sum_a >> k) & 1U) != ((prev_sum_a >> k) & 1U));
        for (int k = 0; k <= 12; k++) out_changed[13 + k] = (((actual_sum_b >> k) & 1U) != ((prev_sum_b >> k) & 1U));

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

static void run_slice(unsigned int sd_start, unsigned int sd_end,
                       double& out_sum_switches, double& out_sum_delay, double& out_sum_edp,
                       unsigned int& out_errors, unsigned long long& out_vectors) {
    sc_signal<sc_lv<12>> SD_a, SD_b, TC;
    sc_signal<sc_lv<13>> Sum_a, Sum_b;

    SdTcAdder12 sdtc("SDTC12");
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
    out_vectors = static_cast<unsigned long long>(sd_end - sd_start) * 4096ULL;
}

struct WorkerResult {
    double sum_switches;
    double sum_delay;
    double sum_edp;
    unsigned long long errors;
    unsigned long long vectors;
};

int sc_main(int argc, char* argv[]) {
    init_path_delay_table();

    unsigned int num_workers = std::thread::hardware_concurrency();
    if (num_workers == 0) num_workers = 1;
    num_workers = static_cast<unsigned int>(std::min<unsigned long long>(num_workers, 4096ULL));

    std::vector<int> read_fd(num_workers);
    std::vector<pid_t> worker_pid(num_workers);
    unsigned int slice = 4096U / num_workers;

    for (unsigned int i = 0; i < num_workers; i++) {
        int fds[2];
        if (pipe(fds) != 0) { perror("pipe"); return 1; }

        pid_t child = fork();
        if (child < 0) { perror("fork"); return 1; }

        if (child == 0) {
            close(fds[0]);
            unsigned int sd_start = i * slice;
            unsigned int sd_end = (i == num_workers - 1) ? 4096U : (i + 1) * slice;

            WorkerResult result{};
            double sw = 0.0, de = 0.0, edp = 0.0;
            unsigned int err = 0;
            unsigned long long vec = 0;
            run_slice(sd_start, sd_end, sw, de, edp, err, vec);
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

    std::cout << "SDTC-PPM-12-EXH-pathEDP: VECTORS=" << total_vectors
              << " AVG_SWITCHES=" << avg_switches
              << " AVG_DELAY_ns=" << avg_delay
              << " AVG_EDP_correct=" << avg_edp_correct
              << " AVG_EDP_naive=" << avg_edp_naive
              << " pct_diff=" << ((avg_edp_naive / avg_edp_correct - 1.0) * 100.0) << "%\n";

    return 0;
}
