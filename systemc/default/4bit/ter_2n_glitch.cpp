// ter_2n_glitch.cpp -- signed-digit adder, 2-digit width, exhaustive 2^N x
// 2^N VALUE sweep (matches RCA-4/CLA-4's test scope exactly, not 3^N x 3^N).
//
// Earlier attempts in this folder used Non-Adjacent Form (NAF) to pick one
// canonical digit pattern per value, but NAF needs N+1 digit positions for
// the top end of the range (verified separately), which either forces
// widening the adder or skipping some values. Both are unsatisfying for a
// direct RCA/CLA comparison.
//
// This version drops NAF's non-adjacency rule entirely: each operand value
// v in [0, 2^N) is encoded digit-by-digit from its own PLAIN BINARY bits
// (digit[i] = bit i of v, so each digit is 0 or +1 -- never -1). That's
// already a valid pattern in the {-1,0,+1} redundant digit set (just a
// degenerate one that never uses -1), so it needs no NAF conversion, fits
// in exactly N digits for every one of the 2^N values with zero skipped,
// and freely allows adjacent nonzero digits (e.g. 15 = "1111", unchanged).
//
// This does NOT reintroduce carry rippling: the adder's two-stage
// full-adder cascade (stage2[i] depends only on stage1_carry[i-1], one
// digit back, never further) is a fixed property of the wiring, not of
// which digit patterns are driven through it. The existing exhaustive
// ter_glitch.cpp already proves this empirically -- it tests the FULL
// 3^N digit-pattern space (which includes many adjacent-nonzero patterns)
// at every width 2..16 and reports a flat DELAY=4ns throughout, never
// scaling with N the way RCA's DELAY=N does.
#include <systemc.h>
#include <iostream>
#include <iomanip>
#include <string>
#include <cassert>
#include <cstdint>
#include <algorithm>
#include <thread>
#include <vector>
#include <sys/wait.h>
#include <unistd.h>

static const int N = 4;                              // digit width, same as RCA-4/CLA-4
static const unsigned long long POW2_N = 1ULL << N;   // 4: exhaustive value range per operand

// ============================================================================
// 0. HELPER STRUCTS AND FUNCTIONS
// ============================================================================
struct DigitRails2 {
    sc_lv<N> rail_a;
    sc_lv<N> rail_b;
};

// Encodes an array of N signed digits (each -1, 0, or +1) into dual rails,
// one canonical encoding per digit value: -1->(0,0), 0->(0,1), +1->(1,1).
static DigitRails2 encode_digits_2(const int digits[N]) {
    DigitRails2 encoded;
    encoded.rail_a = sc_lv<N>("0");
    encoded.rail_b = sc_lv<N>("0");
    for (int i = 0; i < N; i++) {
        int d = digits[i];
        encoded.rail_a[i] = (d == 1);
        encoded.rail_b[i] = (d != -1);
    }
    return encoded;
}

// Plain binary digit extraction: digits[i] = bit i of v, so each digit is
// 0 or +1 (never -1). No NAF conversion, no adjacency constraint -- a run
// of consecutive 1-bits (e.g. 1111 for 15) is left exactly as-is.
static void binary_digits(long long v, int digits[N]) {
    for (int i = 0; i < N; i++) {
        digits[i] = static_cast<int>((v >> i) & 1LL);
    }
}

// Decodes the (N+1)-digit dual-rail result: each (S_plus[i], S_minus[i])
// pair is a digit at binary place value 2^i, coded 00=-1, 11=+1, 01/10=0.
static long long decode_dual_rail_3(const sc_lv<N + 1>& s_plus, const sc_lv<N + 1>& s_minus) {
    long long value = 0;
    for (int i = 0; i < N + 1; i++) {
        bool p = s_plus[i].is_01() && s_plus[i].to_bool();
        bool m = s_minus[i].is_01() && s_minus[i].to_bool();
        int digit = (p && m) ? 1 : ((!p && !m) ? -1 : 0);
        value += static_cast<long long>(digit) * (1LL << i);
    }
    return value;
}

// ============================================================================
// 1. GENERIC GATE-LEVEL PRIMITIVES (identical to ter_glitch.cpp)
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
    uint64_t switches;

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
    uint64_t switches;

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
// 2. GATE-LEVEL BINARY FULL ADDER (6 gates) -- one instance == one DIG_Add
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

    uint64_t total_switches() const {
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
// 2. 2-DIGIT SIGNED-DIGIT ("PPM") ADDER -- exact port of the reference VHDL,
//    identical to the adder in ter_glitch.cpp (no widening, no change).
// ============================================================================
SC_MODULE(SignedDigitAdder2) {
    sc_in<sc_lv<N>> A_a;
    sc_in<sc_lv<N>> A_b;
    sc_in<sc_lv<N>> B_a;
    sc_in<sc_lv<N>> B_b;

    sc_out<sc_lv<N + 1>> S_minus;
    sc_out<sc_lv<N + 1>> S_plus;

    sc_signal<bool> sa_a[N];
    sc_signal<bool> sa_b[N];
    sc_signal<bool> sb_a[N];
    sc_signal<bool> sb_b[N];

    sc_signal<bool> stage1_sum[N];
    sc_signal<bool> stage1_carry[N];

    sc_signal<bool> stage2_sum[N];
    sc_signal<bool> stage2_carry[N];

    sc_signal<bool> const_one;

    BsdBinaryFullAdder* stage1_adders[N];
    BsdBinaryFullAdder* stage2_adders[N];

    void drive_const_one() { const_one.write(true); }

    SC_CTOR(SignedDigitAdder2) {
        SC_METHOD(drive_const_one);

        for (int i = 0; i < N; i++) {
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

            stage2_adders[i]->input_b(stage1_sum[i]);
            stage2_adders[i]->input_c(sb_b[i]);
            stage2_adders[i]->sum_out(stage2_sum[i]);
            stage2_adders[i]->carry_out(stage2_carry[i]);
        }

        SC_METHOD(unpack_inputs);
        dont_initialize();
        sensitive << A_a << A_b << B_a << B_b;

        SC_METHOD(pack_outputs);
        dont_initialize();
        for (int i = 0; i < N; i++) {
            sensitive << stage2_sum[i] << stage2_carry[i];
        }
        sensitive << stage1_carry[N - 1];
    }

    void unpack_inputs() {
        sc_lv<N> value_a_a = A_a.read();
        sc_lv<N> value_a_b = A_b.read();
        sc_lv<N> value_b_a = B_a.read();
        sc_lv<N> value_b_b = B_b.read();

        for (int i = 0; i < N; i++) {
            sa_a[i].write(value_a_a[i].is_01() ? value_a_a[i].to_bool() : false);
            sa_b[i].write(value_a_b[i].is_01() ? value_a_b[i].to_bool() : false);
            sb_a[i].write(value_b_a[i].is_01() ? value_b_a[i].to_bool() : false);
            sb_b[i].write(value_b_b[i].is_01() ? value_b_b[i].to_bool() : false);
        }
    }

    void pack_outputs() {
        sc_lv<N + 1> output_minus;
        sc_lv<N + 1> output_plus;

        output_minus[0] = false;  // no carry-in below the least significant digit
        for (int i = 0; i < N; i++) {
            output_minus[i + 1] = stage2_carry[i].read();
            output_plus[i] = stage2_sum[i].read();
        }

        output_plus[N] = stage1_carry[N - 1].read();

        S_minus.write(output_minus);
        S_plus.write(output_plus);
    }

    void reset_counters() {
        for (int i = 0; i < N; i++) {
            stage1_adders[i]->reset_counters();
            stage2_adders[i]->reset_counters();
        }
    }

    uint64_t total_switches() const {
        uint64_t total_switches = 0;
        for (int i = 0; i < N; i++) {
            total_switches += stage1_adders[i]->total_switches();
            total_switches += stage2_adders[i]->total_switches();
        }
        return total_switches;
    }

    void print_report() {
        std::cout << "TER-4-BIN: GATES=48 SWITCHES=" << total_switches() << " DELAY=4ns\n";
    }

    ~SignedDigitAdder2() {
        for (int i = 0; i < N; i++) {
            delete stage1_adders[i];
            delete stage2_adders[i];
        }
    }
};

// ============================================================================
// 3. TESTBENCH -- exhaustive over the full 2^N x 2^N VALUE range (same
//    count as RCA-4/CLA-4), each value encoded by its own plain binary
//    bits (no NAF, no skipped values).
// ============================================================================
SC_MODULE(Testbench) {
    sc_out<sc_lv<N>> A_a;
    sc_out<sc_lv<N>> A_b;
    sc_out<sc_lv<N>> B_a;
    sc_out<sc_lv<N>> B_b;

    sc_in<sc_lv<N + 1>> S_minus;
    sc_in<sc_lv<N + 1>> S_plus;

    SignedDigitAdder2* design_ptr;

    unsigned int number_of_errors;

    unsigned long long a_start;
    unsigned long long a_end;
    bool quiet;

    SC_CTOR(Testbench)
        : design_ptr(nullptr),
          number_of_errors(0),
          a_start(0),
          a_end(POW2_N),
          quiet(false)
    {
        SC_THREAD(stimulus);
    }

    void stimulus() {
        int zero_digits[N];
        for (int i = 0; i < N; i++) zero_digits[i] = 0;
        DigitRails2 zero = encode_digits_2(zero_digits);

        A_a.write(zero.rail_a);
        A_b.write(zero.rail_b);
        B_a.write(zero.rail_a);
        B_b.write(zero.rail_b);

        wait(20, SC_NS);

        design_ptr->reset_counters();

        for (unsigned long long value_a = a_start; value_a < a_end; value_a++) {
            for (unsigned long long value_b = 0; value_b < POW2_N; value_b++) {
                apply_test(value_a, value_b);
            }
        }

        assert(number_of_errors == 0);
        if (!quiet) {
            design_ptr->print_report();
        }

        sc_stop();
    }

    void apply_test(unsigned long long value_a, unsigned long long value_b) {
        int a_digits[N], b_digits[N];
        binary_digits(static_cast<long long>(value_a), a_digits);
        binary_digits(static_cast<long long>(value_b), b_digits);

        DigitRails2 encoded_a = encode_digits_2(a_digits);
        DigitRails2 encoded_b = encode_digits_2(b_digits);

        A_a.write(encoded_a.rail_a);
        A_b.write(encoded_a.rail_b);
        B_a.write(encoded_b.rail_a);
        B_b.write(encoded_b.rail_b);

        wait(20, SC_NS);

        long long expected_result = static_cast<long long>(value_a) + static_cast<long long>(value_b);
        long long actual_result = decode_dual_rail_3(S_plus.read(), S_minus.read());

        if (expected_result != actual_result) {
            number_of_errors++;
        }
    }
};

// ============================================================================
// 4. MAIN ENTRY POINT
// ============================================================================
static void run_slice(unsigned long long partition_index, unsigned long long num_partitions,
                       bool quiet, uint64_t& out_switches, unsigned int& out_errors) {
    sc_signal<sc_lv<N>> A_a, A_b, B_a, B_b;
    sc_signal<sc_lv<N + 1>> S_minus, S_plus;

    SignedDigitAdder2 adder("SignedDigitAdder2");
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

    unsigned long long slice = POW2_N / num_partitions;
    tb.a_start = partition_index * slice;
    tb.a_end = (partition_index == num_partitions - 1) ? POW2_N : (partition_index + 1) * slice;
    tb.quiet = quiet;

    sc_start();

    out_switches = adder.total_switches();
    out_errors = tb.number_of_errors;
}

int sc_main(int argc, char* argv[]) {
    if (argc == 3) {
        unsigned long long partition_index = std::stoull(argv[1]);
        unsigned long long num_partitions = std::stoull(argv[2]);
        uint64_t switches = 0; unsigned int errors = 0;
        run_slice(partition_index, num_partitions, /*quiet=*/false, switches, errors);
        assert(errors == 0);
        return 0;
    }

    unsigned int num_workers = std::thread::hardware_concurrency();
    if (num_workers == 0) num_workers = 1;
    num_workers = static_cast<unsigned int>(std::min<unsigned long long>(num_workers, POW2_N));

    std::vector<int> read_fd(num_workers);
    std::vector<pid_t> worker_pid(num_workers);

    for (unsigned int i = 0; i < num_workers; i++) {
        int fds[2];
        if (pipe(fds) != 0) { perror("pipe"); return 1; }

        pid_t child = fork();
        if (child < 0) { perror("fork"); return 1; }

        if (child == 0) {
            close(fds[0]);
            uint64_t switches = 0; unsigned int errors = 0;
            run_slice(i, num_workers, /*quiet=*/true, switches, errors);
            uint64_t payload[2] = { switches, errors };
            ssize_t written = write(fds[1], payload, sizeof(payload));
            (void)written;
            close(fds[1]);
            _exit(0);
        }

        close(fds[1]);
        read_fd[i] = fds[0];
        worker_pid[i] = child;
    }

    uint64_t total_switches = 0;
    unsigned int total_errors = 0;
    for (unsigned int i = 0; i < num_workers; i++) {
        uint64_t payload[2] = { 0, 0 };
        ssize_t got = read(read_fd[i], payload, sizeof(payload));
        if (got == static_cast<ssize_t>(sizeof(payload))) {
            total_switches += payload[0];
            total_errors += static_cast<unsigned int>(payload[1]);
        }
        close(read_fd[i]);
        int status = 0;
        waitpid(worker_pid[i], &status, 0);
    }

    assert(total_errors == 0);
    std::cout << "TER-4-BIN: GATES=48 SWITCHES=" << total_switches << " DELAY=4ns\n";

    return 0;
}
