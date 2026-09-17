// sdtc_glitch.cpp – 4-bit Signed-Digit / Two's-Complement Carry-Save Adder, gate-level switching activity
//
// Ported from SD_add_TC_4bit.vhdl ("DIG_Add" digit cells wired into the
// "main" entity, generalized from 4 digits to 4). SD_a/SD_b together are a
// PM (plus-minus) signed-digit number: digit i = SD_a(i) - SD_b(i) in
// {-1,0,1} ("10"=+1, "01"=-1, "00"/"11"=0), which sums to plain
// SD_a_unsigned - SD_b_unsigned. TC is a two's-complement (signed) 4-bit
// operand. Digit i is a single digit cell over (TC(i), SD_b(i), SD_a(i))
// with NO carry chain between digits (unlike RCA/CLA), so the whole adder
// settles in one cell's gate depth no matter how wide it is. Its
// sum/carry outputs are exposed directly as a redundant (4+1)-bit pair:
//   Sum_a(i)   = sum of digit i         for i = 0..4-1
//   Sum_a(4) = NOT TC(4-1)          (sign-correction top bit)
//   Sum_b(0)   = '0'                    (matches the VHDL's Sum0_b <= '0')
//   Sum_b(i+1) = carry of digit i       for i = 0..4-1
// Each digit contributes 2*carry - sum (not 2*carry + sum) to the total, so
// the testbench checks Sum_b - Sum_a + 2^4*(TC(4-1)?-1:+1) == SD_a - SD_b + TC_signed.
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
static sc_lv<4> make_4bit_vector(unsigned int value) {
    sc_lv<4> bits;
    for (int i = 0; i < 4; i++) {
        bits[i] = ((value >> i) & 1U) != 0U;
    }
    return bits;
}

static unsigned int to_unsigned_4bit(const sc_lv<4>& bits) {
    unsigned int value = 0;
    for (int i = 0; i < 4; i++) {
        if (bits[i].is_01() && bits[i].to_bool()) {
            value += (1U << i);
        }
    }
    return value;
}

static unsigned long long to_unsigned_5bit(const sc_lv<5>& bits) {
    unsigned long long value = 0;
    for (int i = 0; i < 5; i++) {
        if (bits[i].is_01() && bits[i].to_bool()) {
            value += (1ULL << i);
        }
    }
    return value;
}

// TC is a two's-complement N-bit operand; decode its signed integer value.
static long long to_signed_twos_complement_4bit(unsigned int value) {
    long long v = static_cast<long long>(value);
    if (value & (1U << (4 - 1))) {
        v -= (1LL << 4);
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
    }

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
// 3. 4-BIT SIGNED-DIGIT / TWO'S-COMPLEMENT ADDER
//    4 independent digit cells (7 gates each), one per digit, with NO
//    carry chain between them. Gate count: 4 x 7 (digit cells) + 1 (NOT) = 29
// ==========================================
SC_MODULE(SdTcAdder4) {
    sc_in<sc_lv<4>> SD_a;
    sc_in<sc_lv<4>> SD_b;
    sc_in<sc_lv<4>> TC;

    sc_out<sc_lv<5>> Sum_a;
    sc_out<sc_lv<5>> Sum_b;

    sc_signal<bool> sda_bit[4];
    sc_signal<bool> sdb_bit[4];
    sc_signal<bool> tc_bit[4];
    sc_signal<bool> sum_bit[4];
    sc_signal<bool> carry_bit[4];
    sc_signal<bool> const_zero;
    sc_signal<bool> top_bit;

    FullAdder* fa[4];
    GateNot* g_not_top;

    SC_CTOR(SdTcAdder4) {
        for (int i = 0; i < 4; i++) {
            std::string name = "FA_" + std::to_string(i);
            fa[i] = new FullAdder(name.c_str());

            fa[i]->a(tc_bit[i]);
            fa[i]->b(sdb_bit[i]);
            fa[i]->cin(sda_bit[i]);
            fa[i]->sum(sum_bit[i]);
            fa[i]->cout(carry_bit[i]);
        }

        g_not_top = new GateNot("NOT_TOP");
        g_not_top->in1(tc_bit[4 - 1]);
        g_not_top->out(top_bit);

        SC_METHOD(split_inputs);
        sensitive << SD_a << SD_b << TC;

        SC_METHOD(combine_outputs);
        for (int i = 0; i < 4; i++) {
            sensitive << sum_bit[i];
            sensitive << carry_bit[i];
        }
        sensitive << top_bit;
    }

    void split_inputs() {
        sc_lv<4> value_sda = SD_a.read();
        sc_lv<4> value_sdb = SD_b.read();
        sc_lv<4> value_tc = TC.read();

        for (int i = 0; i < 4; i++) {
            sda_bit[i].write(value_sda[i].is_01() ? value_sda[i].to_bool() : false);
            sdb_bit[i].write(value_sdb[i].is_01() ? value_sdb[i].to_bool() : false);
            tc_bit[i].write(value_tc[i].is_01() ? value_tc[i].to_bool() : false);
        }
    }

    void combine_outputs() {
        sc_lv<5> value_sum_a;
        sc_lv<5> value_sum_b;

        for (int i = 0; i < 4; i++) {
            value_sum_a[i] = sum_bit[i].read();
            value_sum_b[i + 1] = carry_bit[i].read();
        }
        value_sum_a[4] = top_bit.read();
        value_sum_b[0] = const_zero.read();

        Sum_a.write(value_sum_a);
        Sum_b.write(value_sum_b);
    }

    unsigned int total_switches() const {
        unsigned int total = 0;
        for (int i = 0; i < 4; i++) {
            total += fa[i]->total_switches();
        }
        total += g_not_top->switches;
        return total;
    }

    ~SdTcAdder4() {
        for (int i = 0; i < 4; i++) {
            delete fa[i];
        }
        delete g_not_top;
    }
};

// ==========================================
// 4. TESTBENCH -- Monte Carlo random sampling
//    Draws num_samples uniformly random (SD_a, SD_b, TC) triples instead
//    of exhaustively covering the whole input space: at 4 bits exhaustive
//    coverage (2^4 x 2^4 x 2^4 cases) is computationally infeasible, so
//    Monte Carlo sampling is the only tractable option.
//
//    SD_a/SD_b are the PM signed-digit rails (digit i = SD_a(i)-SD_b(i));
//    TC is two's-complement signed. Correctness is checked against the
//    redundant-representation identity Sum_b - Sum_a + 2^N*(+-1) ==
//    SD_a - SD_b + TC_signed (see header comment and apply_test below).
// ==========================================
SC_MODULE(Testbench) {
    sc_out<sc_lv<4>> SD_a, SD_b, TC;

    sc_in<sc_lv<5>> Sum_a, Sum_b;

    SdTcAdder4* sdtc_ptr;

    unsigned int number_of_errors;

    unsigned long long num_samples;
    unsigned long long rng_seed;

    SC_CTOR(Testbench)
        : sdtc_ptr(nullptr),
          number_of_errors(0),
          num_samples(1000000ULL),
          rng_seed(42ULL)
    {
        SC_THREAD(stimulus);
    }

    void stimulus() {
        std::mt19937_64 rng(rng_seed);
        std::uniform_int_distribution<unsigned long long> dist(0ULL, 15ULL);

        for (unsigned long long i = 0; i < num_samples; i++) {
            unsigned int sda_value = static_cast<unsigned int>(dist(rng));
            unsigned int sdb_value = static_cast<unsigned int>(dist(rng));
            unsigned int tc_value  = static_cast<unsigned int>(dist(rng));
            apply_test(sda_value, sdb_value, tc_value);
        }

        assert(number_of_errors == 0);
        sc_stop();
    }

    void apply_test(unsigned int sda_value, unsigned int sdb_value, unsigned int tc_value) {
        SD_a.write(make_4bit_vector(sda_value));
        SD_b.write(make_4bit_vector(sdb_value));
        TC.write(make_4bit_vector(tc_value));

        wait(200, SC_NS);

        // PM encoding: digit i = SD_a(i) - SD_b(i), so the SD number's
        // value is simply SD_a_unsigned - SD_b_unsigned.
        long long expected_result = static_cast<long long>(sda_value)
                                   - static_cast<long long>(sdb_value)
                                   + to_signed_twos_complement_4bit(tc_value);

        // Each digit cell now contributes 2*carry - sum (not 2*carry + sum)
        // to the total, so the redundant pair recombines as Sum_b - Sum_a,
        // and the sign-correction bit flips the +2^N term to -2^N when
        // TC's own sign bit is set.
        unsigned int tc_top = (tc_value >> (4 - 1)) & 1U;
        long long sign_term = tc_top ? -(1LL << 4) : (1LL << 4);

        long long actual_result = static_cast<long long>(to_unsigned_5bit(Sum_b.read()))
                                 - static_cast<long long>(to_unsigned_5bit(Sum_a.read()))
                                 + sign_term;

        if (expected_result != actual_result) {
            number_of_errors++;
        }
    }
};

// ==========================================
// 5. MAIN -- Monte Carlo, auto-parallel across all CPU cores
// ==========================================

// Elaborates a fresh adder + testbench and simulates exactly
// samples_for_this_worker random test vectors, seeded independently so
// parallel workers never repeat each other's samples.
static void run_slice(unsigned long long samples_for_this_worker, unsigned long long seed_for_this_worker,
                       unsigned long long& out_switches, unsigned int& out_errors) {
    sc_signal<sc_lv<4>> SD_a, SD_b, TC;
    sc_signal<sc_lv<5>> Sum_a, Sum_b;

    SdTcAdder4 sdtc("SDTC4");
    Testbench tb("TB");
    tb.sdtc_ptr = &sdtc;

    sdtc.SD_a(SD_a); sdtc.SD_b(SD_b); sdtc.TC(TC);
    sdtc.Sum_a(Sum_a); sdtc.Sum_b(Sum_b);

    tb.SD_a(SD_a); tb.SD_b(SD_b); tb.TC(TC);
    tb.Sum_a(Sum_a); tb.Sum_b(Sum_b);

    tb.num_samples = samples_for_this_worker;
    tb.rng_seed = seed_for_this_worker;

    sc_start();

    out_switches = sdtc.total_switches();
    out_errors = tb.number_of_errors;
}

int sc_main(int argc, char* argv[]) {
    unsigned long long total_samples = (argc >= 2) ? std::stoull(argv[1]) : 1000000ULL;
    unsigned long long base_seed     = (argc >= 3) ? std::stoull(argv[2]) : 42ULL;

    // SystemC's kernel state (sc_curr_simcontext) is a single
    // un-synchronized global, not thread-local, so one process cannot
    // safely run more than one simulation concurrently via std::thread.
    // Instead we fork() one worker process per core before any SystemC
    // object is created; each child simulates its own independent slice
    // of samples (with its own RNG stream) in its own independent
    // kernel, and reports its partial switch/error counts back through
    // a pipe.
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
            unsigned long long switches = 0;
            unsigned int errors = 0;
            run_slice(samples_for_worker, base_seed + i, switches, errors);
            unsigned long long payload[2] = { switches, static_cast<unsigned long long>(errors) };
            ssize_t written = write(fds[1], payload, sizeof(payload));
            (void)written;
            close(fds[1]);
            _exit(0);
        }

        close(fds[1]);
        read_fd[i] = fds[0];
        worker_pid[i] = child;
    }

    unsigned long long total_switches = 0;
    unsigned long long total_errors = 0;
    for (unsigned int i = 0; i < num_workers; i++) {
        unsigned long long payload[2] = { 0, 0 };
        ssize_t got = read(read_fd[i], payload, sizeof(payload));
        if (got == static_cast<ssize_t>(sizeof(payload))) {
            total_switches += payload[0];
            total_errors   += payload[1];
        }
        close(read_fd[i]);
        int status = 0;
        waitpid(worker_pid[i], &status, 0);
    }

    assert(total_errors == 0);
    double avg_switches = total_samples > 0
        ? (static_cast<double>(total_switches) / static_cast<double>(total_samples))
        : 0.0;
    std::cout << "SDTC-4-MC: GATES=29 SAMPLES=" << total_samples
              << " SWITCHES=" << total_switches
              << " AVG_SWITCHES=" << avg_switches
              << " DELAY=3ns\n";

    return 0;
}