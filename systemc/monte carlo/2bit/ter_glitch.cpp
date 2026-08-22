// bsd_ternary_adder2_montecarlo.cpp
#include <systemc.h>
#include <iostream>
#include <iomanip>
#include <string>
#include <random>
#include <algorithm>
#include <cstdio>
#include <thread>
#include <vector>
#include <sys/wait.h>
#include <unistd.h>

// ============================================================================
// 0. HELPER STRUCTS AND FUNCTIONS
// ============================================================================
struct DualRail2 {
    sc_lv<2> rail_a;
    sc_lv<2> rail_b;
};

static DualRail2 encode_unsigned_to_balanced_ternary_2(unsigned int value) {
    DualRail2 encoded;
    encoded.rail_a = "00";
    encoded.rail_b = "00";

    if (value > 3U) {
        std::cerr << "Fehler: Der 2-Trit-Test erwartet Werte im Bereich 0..3.\n";
        sc_stop();
        return encoded;
    }

    int remaining_value = static_cast<int>(value);

    for (int i = 0; i < 2; i++) {
        int remainder = remaining_value % 3;
        remaining_value /= 3;

        int trit_value;

        if (remainder == 0) {
            trit_value = 0;
        } else if (remainder == 1) {
            trit_value = 1;
        } else {
            trit_value = -1;
            remaining_value += 1;
        }

        if (trit_value == 0) {
            encoded.rail_a[i] = false;
            encoded.rail_b[i] = false;
        } else if (trit_value == 1) {
            encoded.rail_a[i] = true;
            encoded.rail_b[i] = true;
        } else {
            encoded.rail_a[i] = false;
            encoded.rail_b[i] = true;
        }
    }

    return encoded;
}

// ============================================================================
// 1. GENERIC GATE-LEVEL PRIMITIVES
//    Each instance is exactly one logic gate, independently sensitive to
//    each of its inputs (so inputs arriving at different simulated times
//    each trigger their own evaluation and are counted as separate
//    switches), with a 1ns delay between evaluation and output commit.
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
    unsigned int switches;

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
// 2. GATE-LEVEL BINARY FULL ADDER (6 gates)
//    xor1 = XOR(a,b)        and1 = AND(a,b)
//    and2 = AND(a,c)        and3 = AND(b,c)
//    sum  = XOR(xor1,c)     carry = OR3(and1,and2,and3)
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

    unsigned int total_switches() const {
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
// 2. 2-TRIT BALANCED TERNARY / BSD-SUM ADDER
// ============================================================================
SC_MODULE(BalancedTernaryAdder2) {
    sc_in<sc_lv<2>> A_a;
    sc_in<sc_lv<2>> A_b;
    sc_in<sc_lv<2>> B_a;
    sc_in<sc_lv<2>> B_b;

    sc_out<sc_lv<3>> S_minus;
    sc_out<sc_lv<3>> S_plus;

    sc_signal<bool> sa_a[2];
    sc_signal<bool> sa_b[2];
    sc_signal<bool> sb_a[2];
    sc_signal<bool> sb_b[2];

    sc_signal<bool> stage1_sum[2];
    sc_signal<bool> stage1_carry[2];

    sc_signal<bool> stage2_sum[2];
    sc_signal<bool> stage2_carry[2];

    sc_signal<bool> const_one;

    BsdBinaryFullAdder* stage1_adders[2];
    BsdBinaryFullAdder* stage2_adders[2];

    SC_CTOR(BalancedTernaryAdder2) {
        const_one.write(true);

        for (int i = 0; i < 2; i++) {
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

            stage2_adders[i]->input_b(sb_b[i]);
            stage2_adders[i]->input_c(stage1_sum[i]);
            stage2_adders[i]->sum_out(stage2_sum[i]);
            stage2_adders[i]->carry_out(stage2_carry[i]);
        }

        SC_METHOD(unpack_inputs);
        dont_initialize();
        sensitive << A_a << A_b << B_a << B_b;

        SC_METHOD(pack_outputs);
        dont_initialize();
        sensitive << stage2_sum[0] << stage2_carry[0]
                  << stage2_sum[1] << stage2_carry[1]
                  << stage1_carry[1];
    }

    void unpack_inputs() {
        sc_lv<2> value_a_a = A_a.read();
        sc_lv<2> value_a_b = A_b.read();
        sc_lv<2> value_b_a = B_a.read();
        sc_lv<2> value_b_b = B_b.read();

        for (int i = 0; i < 2; i++) {
            sa_a[i].write(value_a_a[i].is_01() ? value_a_a[i].to_bool() : false);
            sa_b[i].write(value_a_b[i].is_01() ? value_a_b[i].to_bool() : false);
            sb_a[i].write(value_b_a[i].is_01() ? value_b_a[i].to_bool() : false);
            sb_b[i].write(value_b_b[i].is_01() ? value_b_b[i].to_bool() : false);
        }
    }

    void pack_outputs() {
        sc_lv<3> output_minus;
        sc_lv<3> output_plus;

        output_minus[0] = stage2_carry[0].read();
        output_plus[0] = stage2_sum[0].read();

        output_minus[1] = stage2_carry[1].read();
        output_plus[1] = stage2_sum[1].read();

        output_minus[2] = stage2_carry[1].read();
        output_plus[2] = stage1_carry[1].read();

        S_minus.write(output_minus);
        S_plus.write(output_plus);
    }

    void reset_counters() {
        for (int i = 0; i < 2; i++) {
            stage1_adders[i]->reset_counters();
            stage2_adders[i]->reset_counters();
        }
    }

    unsigned int total_switches() const {
        unsigned int total_switches = 0;

        for (int i = 0; i < 2; i++) {
            total_switches += stage1_adders[i]->total_switches();
            total_switches += stage2_adders[i]->total_switches();
        }

        return total_switches;
    }

    ~BalancedTernaryAdder2() {
        for (int i = 0; i < 2; i++) {
            delete stage1_adders[i];
            delete stage2_adders[i];
        }
    }
};

// ============================================================================
// 3. TESTBENCH — Monte Carlo random sampling
//    Draws num_samples uniformly random (a, b) pairs instead of
//    exhaustively covering the whole input space, so the same
//    methodology also works for widths where exhaustive coverage is
//    computationally infeasible (e.g. 32-bit). No correctness check is
//    performed here, matching the original exhaustive testbench, which
//    only measured switching activity for this adder.
// ============================================================================
SC_MODULE(Testbench) {
    sc_out<sc_lv<2>> A_a;
    sc_out<sc_lv<2>> A_b;
    sc_out<sc_lv<2>> B_a;
    sc_out<sc_lv<2>> B_b;

    sc_in<sc_lv<3>> S_minus;
    sc_in<sc_lv<3>> S_plus;

    BalancedTernaryAdder2* design_ptr;

    unsigned long long num_samples;
    unsigned long long rng_seed;

    SC_CTOR(Testbench)
        : design_ptr(nullptr),
          num_samples(1000000ULL),
          rng_seed(42ULL)
    {
        SC_THREAD(stimulus);
    }

    void stimulus() {
        DualRail2 zero = encode_unsigned_to_balanced_ternary_2(0);

        A_a.write(zero.rail_a);
        A_b.write(zero.rail_b);
        B_a.write(zero.rail_a);
        B_b.write(zero.rail_b);

        wait(20, SC_NS);

        design_ptr->reset_counters();

        std::mt19937_64 rng(rng_seed);
        std::uniform_int_distribution<unsigned long long> dist(0ULL, 3ULL);

        for (unsigned long long i = 0; i < num_samples; i++) {
            unsigned int a_value = static_cast<unsigned int>(dist(rng));
            unsigned int b_value = static_cast<unsigned int>(dist(rng));
            apply_test(a_value, b_value);
        }

        sc_stop();
    }

    void apply_test(unsigned int a_value, unsigned int b_value) {
        DualRail2 encoded_a = encode_unsigned_to_balanced_ternary_2(a_value);
        DualRail2 encoded_b = encode_unsigned_to_balanced_ternary_2(b_value);

        A_a.write(encoded_a.rail_a);
        A_b.write(encoded_a.rail_b);
        B_a.write(encoded_b.rail_a);
        B_b.write(encoded_b.rail_b);

        wait(20, SC_NS);
    }
};

// ============================================================================
// 4. MAIN — Monte Carlo, auto-parallel across all CPU cores
// ============================================================================

// Elaborates a fresh adder + testbench and simulates exactly
// samples_for_this_worker random test vectors, seeded independently so
// parallel workers never repeat each other's samples.
static unsigned long long run_slice(unsigned long long samples_for_this_worker, unsigned long long seed_for_this_worker) {
    sc_signal<sc_lv<2>> A_a;
    sc_signal<sc_lv<2>> A_b;
    sc_signal<sc_lv<2>> B_a;
    sc_signal<sc_lv<2>> B_b;

    sc_signal<sc_lv<3>> S_minus;
    sc_signal<sc_lv<3>> S_plus;

    BalancedTernaryAdder2 adder("BalancedTernaryAdder2");
    Testbench tb("TB");

    tb.design_ptr = &adder;

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

    tb.num_samples = samples_for_this_worker;
    tb.rng_seed = seed_for_this_worker;

    sc_start();

    return adder.total_switches();
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
    // kernel, and reports its partial switch count back through a pipe.
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
            unsigned long long switches = run_slice(samples_for_worker, base_seed + i);
            ssize_t written = write(fds[1], &switches, sizeof(switches));
            (void)written;
            close(fds[1]);
            _exit(0);
        }

        close(fds[1]);
        read_fd[i] = fds[0];
        worker_pid[i] = child;
    }

    unsigned long long total_switches = 0;
    for (unsigned int i = 0; i < num_workers; i++) {
        unsigned long long switches = 0;
        ssize_t got = read(read_fd[i], &switches, sizeof(switches));
        if (got == static_cast<ssize_t>(sizeof(switches))) {
            total_switches += switches;
        }
        close(read_fd[i]);
        int status = 0;
        waitpid(worker_pid[i], &status, 0);
    }

    double avg_switches = total_samples > 0
        ? (static_cast<double>(total_switches) / static_cast<double>(total_samples))
        : 0.0;
    std::cout << "TER-2-MC: GATES=24 SAMPLES=" << total_samples
              << " SWITCHES=" << total_switches
              << " AVG_SWITCHES=" << avg_switches
              << " DELAY=4ns\n";

    return 0;
}
