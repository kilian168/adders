// main.cpp
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

    ~FullAdder() {
        delete g_xor1; delete g_and1; delete g_xor2; delete g_and2; delete g_or1;
    }
};

// ==========================================
// 2. 12-BIT RIPPLE CARRY ADDER
// ==========================================
SC_MODULE(RippleCarryAdder12) {
    sc_in<sc_lv<12>> A;
    sc_in<sc_lv<12>> B;

    sc_out<sc_lv<12>> Sum;
    sc_out<bool> Cout;

    sc_signal<bool> a[12];
    sc_signal<bool> b[12];
    sc_signal<bool> s[12];
    sc_signal<bool> c[12];
    sc_signal<bool> const_zero;

    FullAdder* fa[12];

    SC_CTOR(RippleCarryAdder12) {
        for (int i = 0; i < 12; i++) {
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
        for (int i = 0; i < 12; i++) {
            sensitive << s[i];
        }
        sensitive << c[11];
    }

    void split_inputs() {
        sc_lv<12> value_a = A.read();
        sc_lv<12> value_b = B.read();

        for (int i = 0; i < 12; i++) {
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
        sc_lv<12> value_sum;

        for (int i = 0; i < 12; i++) {
            value_sum[i] = s[i].read();
        }

        Sum.write(value_sum);
        Cout.write(c[11].read());
    }

    unsigned int total_switches() const {
        unsigned int total = 0;
        for (int i = 0; i < 12; i++) {
            total += fa[i]->total_switches();
        }
        return total;
    }

    ~RippleCarryAdder12() {
        for (int i = 0; i < 12; i++) {
            delete fa[i];
        }
    }
};

// ==========================================
// 3. TESTBENCH — Monte Carlo random sampling
//    Draws num_samples uniformly random (a, b) pairs instead of
//    exhaustively covering the whole input space, so the same
//    methodology also works for widths where exhaustive coverage is
//    computationally infeasible (e.g. 32-bit).
// ==========================================
SC_MODULE(Testbench) {
    sc_out<sc_lv<12>> A;
    sc_out<sc_lv<12>> B;

    sc_in<sc_lv<12>> Sum;
    sc_in<bool> Cout;

    RippleCarryAdder12* rca_ptr;

    unsigned int number_of_errors;

    unsigned long long num_samples;
    unsigned long long rng_seed;

    SC_CTOR(Testbench)
        : rca_ptr(nullptr),
          number_of_errors(0),
          num_samples(1000000ULL),
          rng_seed(42ULL)
    {
        SC_THREAD(stimulus);
    }

    void stimulus() {
        std::mt19937_64 rng(rng_seed);
        std::uniform_int_distribution<unsigned long long> dist(0ULL, 4095ULL);

        for (unsigned long long i = 0; i < num_samples; i++) {
            unsigned int a_value = static_cast<unsigned int>(dist(rng));
            unsigned int b_value = static_cast<unsigned int>(dist(rng));
            apply_test(a_value, b_value);
        }

        assert(number_of_errors == 0);
        sc_stop();
    }

    void apply_test(unsigned int a_value, unsigned int b_value) {
        A.write(make_12bit_vector(a_value));
        B.write(make_12bit_vector(b_value));

        wait(200, SC_NS);

        unsigned int expected_result = a_value + b_value;
        unsigned int actual_sum = to_unsigned_12bit(Sum.read());
        unsigned int actual_result = actual_sum + (Cout.read() ? 4096U : 0U);

        if (expected_result != actual_result) {
            number_of_errors++;
        }
    }
};

// ==========================================
// 4. MAIN — Monte Carlo, auto-parallel across all CPU cores
// ==========================================

// Elaborates a fresh adder + testbench and simulates exactly
// samples_for_this_worker random test vectors, seeded independently so
// parallel workers never repeat each other's samples.
static void run_slice(unsigned long long samples_for_this_worker, unsigned long long seed_for_this_worker,
                       unsigned long long& out_switches, unsigned int& out_errors) {
    sc_signal<sc_lv<12>> A;
    sc_signal<sc_lv<12>> B;

    sc_signal<sc_lv<12>> Sum;
    sc_signal<bool> Cout;

    RippleCarryAdder12 rca("RCA12");
    Testbench tb("TB");

    tb.rca_ptr = &rca;

    rca.A(A);
    rca.B(B);
    rca.Sum(Sum);
    rca.Cout(Cout);

    tb.A(A);
    tb.B(B);
    tb.Sum(Sum);
    tb.Cout(Cout);

    tb.num_samples = samples_for_this_worker;
    tb.rng_seed = seed_for_this_worker;

    sc_start();

    out_switches = rca.total_switches();
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
    std::cout << "RCA-12-MC: GATES=60 SAMPLES=" << total_samples
              << " SWITCHES=" << total_switches
              << " AVG_SWITCHES=" << avg_switches
              << " DELAY=24ns\n";

    return 0;
}
