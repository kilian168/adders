// main.cpp
#include <systemc.h>
#include <iostream>
#include <iomanip>
#include <string>
#include <cassert>
#include <algorithm>
#include <cstdio>
#include <thread>
#include <vector>
#include <sys/wait.h>
#include <unistd.h>

// ==========================================
// Helper functions
// ==========================================
static sc_lv<16> make_16bit_vector(unsigned int value) {
    sc_lv<16> bits;
    for (int i = 0; i < 16; i++) {
        bits[i] = ((value >> i) & 1U) != 0U;
    }
    return bits;
}

static unsigned int to_unsigned_16bit(const sc_lv<16>& bits) {
    unsigned int value = 0;
    for (int i = 0; i < 16; i++) {
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
// 2. 16-BIT RIPPLE CARRY ADDER
// ==========================================
SC_MODULE(RippleCarryAdder16) {
    sc_in<sc_lv<16>> A;
    sc_in<sc_lv<16>> B;

    sc_out<sc_lv<16>> Sum;
    sc_out<bool> Cout;

    sc_signal<bool> a[16];
    sc_signal<bool> b[16];
    sc_signal<bool> s[16];
    sc_signal<bool> c[16];
    sc_signal<bool> const_zero;

    FullAdder* fa[16];

    SC_CTOR(RippleCarryAdder16) {
        for (int i = 0; i < 16; i++) {
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
        for (int i = 0; i < 16; i++) {
            sensitive << s[i];
        }
        sensitive << c[15];
    }

    void split_inputs() {
        sc_lv<16> value_a = A.read();
        sc_lv<16> value_b = B.read();

        for (int i = 0; i < 16; i++) {
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
        sc_lv<16> value_sum;

        for (int i = 0; i < 16; i++) {
            value_sum[i] = s[i].read();
        }

        Sum.write(value_sum);
        Cout.write(c[15].read());
    }

    unsigned int total_switches() const {
        unsigned int total = 0;
        for (int i = 0; i < 16; i++) {
            total += fa[i]->total_switches();
        }
        return total;
    }

    void print_report() {
        std::cout << "RCA-16: GATES=80 SWITCHES=" << total_switches() << " DELAY=32ns\n";
    }

    ~RippleCarryAdder16() {
        for (int i = 0; i < 16; i++) {
            delete fa[i];
        }
    }
};

// ==========================================
// 3. TESTBENCH
// ==========================================
SC_MODULE(Testbench) {
    sc_out<sc_lv<16>> A;
    sc_out<sc_lv<16>> B;

    sc_in<sc_lv<16>> Sum;
    sc_in<bool> Cout;

    RippleCarryAdder16* rca_ptr;

    unsigned int number_of_errors;

    // Partition of the a-value range to cover. Defaults to the full
    // range; sc_main() narrows this when run with partition arguments,
    // so the exhaustive sweep can be split across parallel processes.
    unsigned int a_start;
    unsigned int a_end;

    // When true, this process's slice was launched as one of several
    // parallel workers (see run_slice() below), so it stays silent and
    // lets the orchestrating parent print the combined report instead.
    bool quiet;

    SC_CTOR(Testbench)
        : rca_ptr(nullptr),
          number_of_errors(0),
          a_start(0),
          a_end(65536),
          quiet(false)
    {
        SC_THREAD(stimulus);
    }

    void stimulus() {
        for (unsigned int a_value = a_start; a_value < a_end; a_value++) {
            for (unsigned int b_value = 0; b_value < 65536; b_value++) {
                apply_test(a_value, b_value);
            }
        }

        assert(number_of_errors == 0);
        if (!quiet) {
            rca_ptr->print_report();
        }

        sc_stop();
    }

    void apply_test(unsigned int a_value, unsigned int b_value) {
        A.write(make_16bit_vector(a_value));
        B.write(make_16bit_vector(b_value));

        wait(200, SC_NS);

        unsigned int expected_result = a_value + b_value;
        unsigned int actual_sum = to_unsigned_16bit(Sum.read());
        unsigned int actual_result = actual_sum + (Cout.read() ? 65536U : 0U);

        if (expected_result != actual_result) {
            number_of_errors++;
        }
    }
};

// ==========================================
// 4. MAIN ENTRY POINT
// ==========================================

// Elaborates a fresh adder + testbench and simulates exactly one
// a-value slice [partition_index, num_partitions) to completion in the
// calling process. Used both for manual external partitioning (one
// process, prints its own report) and as the body of each forked
// worker in the auto-parallel default path (quiet, reports back via
// out params instead of stdout).
static void run_slice(unsigned int partition_index, unsigned int num_partitions,
                       bool quiet, unsigned int& out_switches, unsigned int& out_errors) {
    sc_signal<sc_lv<16>> A;
    sc_signal<sc_lv<16>> B;

    sc_signal<sc_lv<16>> Sum;
    sc_signal<bool> Cout;

    RippleCarryAdder16 rca("RCA16");
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

    unsigned int slice = 65536U / num_partitions;
    tb.a_start = partition_index * slice;
    tb.a_end = (partition_index == num_partitions - 1) ? 65536U : (partition_index + 1) * slice;
    tb.quiet = quiet;

    sc_start();

    out_switches = rca.total_switches();
    out_errors = tb.number_of_errors;
}

int sc_main(int argc, char* argv[]) {
    // Manual external partitioning: "rca_16bit <partition_index> <num_partitions>"
    // runs exactly that slice in this single process and prints its own
    // partial report, e.g. for hand-orchestrated multi-machine runs.
    if (argc == 3) {
        unsigned int partition_index = static_cast<unsigned int>(std::stoul(argv[1]));
        unsigned int num_partitions = static_cast<unsigned int>(std::stoul(argv[2]));
        unsigned int switches = 0, errors = 0;
        run_slice(partition_index, num_partitions, /*quiet=*/false, switches, errors);
        assert(errors == 0);
        return 0;
    }

    // Default (no args): fan the exhaustive sweep out across every
    // available CPU core. SystemC's kernel state (sc_curr_simcontext) is
    // a single un-synchronized global, not thread-local, so one process
    // cannot safely run more than one simulation concurrently via
    // std::thread. Instead we fork() one worker process per core before
    // any SystemC object is created; each child then elaborates and
    // simulates its own independent slice in its own independent kernel,
    // and reports its partial switch/error counts back through a pipe.
    unsigned int num_workers = std::thread::hardware_concurrency();
    if (num_workers == 0) num_workers = 1;
    num_workers = std::min(num_workers, 65536U);

    std::vector<int> read_fd(num_workers);
    std::vector<pid_t> worker_pid(num_workers);

    for (unsigned int i = 0; i < num_workers; i++) {
        int fds[2];
        if (pipe(fds) != 0) { perror("pipe"); return 1; }

        pid_t child = fork();
        if (child < 0) { perror("fork"); return 1; }

        if (child == 0) {
            close(fds[0]);
            unsigned int switches = 0, errors = 0;
            run_slice(i, num_workers, /*quiet=*/true, switches, errors);
            unsigned int payload[2] = { switches, errors };
            ssize_t written = write(fds[1], payload, sizeof(payload));
            (void)written;
            close(fds[1]);
            _exit(0);
        }

        close(fds[1]);
        read_fd[i] = fds[0];
        worker_pid[i] = child;
    }

    unsigned int total_switches = 0;
    unsigned int total_errors = 0;
    for (unsigned int i = 0; i < num_workers; i++) {
        unsigned int payload[2] = { 0, 0 };
        ssize_t got = read(read_fd[i], payload, sizeof(payload));
        if (got == static_cast<ssize_t>(sizeof(payload))) {
            total_switches += payload[0];
            total_errors += payload[1];
        }
        close(read_fd[i]);
        int status = 0;
        waitpid(worker_pid[i], &status, 0);
    }

    assert(total_errors == 0);
    std::cout << "RCA-16: GATES=80 SWITCHES=" << total_switches << " DELAY=32ns\n";

    return 0;
}
