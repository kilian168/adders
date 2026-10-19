// ter_glitch.cpp -- 8-digit redundant signed-digit ("PPM") adder.
// Gate-level port of the reference VHDL (8 chained 1-bit DIG_Add full
// adders for N=4, generalized to N digits here): each operand digit i
// is dual-rail encoded as (rail_a, rail_b) with digit = rail_a+rail_b-1,
// i.e. -1 -> (0,0), 0 -> (0,1) [or equivalently (1,0) -- both decode to
// 0, this file just picks one canonical encoding per digit value for
// generating test vectors], +1 -> (1,1). Place values are binary (2^i),
// not base-3 -- confirmed against the reference schematic.
//
// Per digit i: stage1 FA(i) = FullAdder(A_a[i], B_a[i], A_b[i]) ->
// (stage1_sum[i], stage1_carry[i]); stage2 FA(i) = FullAdder(
// i==0 ? 1 : stage1_carry[i-1], stage1_sum[i], B_b[i]) ->
// (S_plus[i], S_minus[i+1]). The top digit's stage1 carry feeds
// S_plus[N] directly (no stage2 FA for the overflow digit); S_minus[0]
// is tied to the constant "no carry-in below the LSB".
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

// ============================================================================
// 0. HELPER STRUCTS AND FUNCTIONS
// ============================================================================
struct DigitRails8 {
    sc_lv<8> rail_a;
    sc_lv<8> rail_b;
};

// Encodes an array of N signed digits (each -1, 0, or +1) into dual rails,
// one canonical encoding per digit value: -1->(0,0), 0->(0,1), +1->(1,1).
static DigitRails8 encode_digits_8(const int digits[8]) {
    DigitRails8 encoded;
    encoded.rail_a = "00000000";
    encoded.rail_b = "00000000";
    for (int i = 0; i < 8; i++) {
        int d = digits[i];
        encoded.rail_a[i] = (d == 1);
        encoded.rail_b[i] = (d != -1);
    }
    return encoded;
}

// Decodes a base-3 index (0 .. 3^N - 1) into N signed digits via base-3
// digit extraction, each trit mapped {0,1,2} -> {-1,0,+1}.
static void digits_from_index_8(unsigned long long idx, int digits[8]) {
    for (int i = 0; i < 8; i++) {
        digits[i] = static_cast<int>(idx % 3ULL) - 1;
        idx /= 3ULL;
    }
}

static const unsigned long long POW3_8 = 6561ULL;

// Numeric value of a digit pattern at binary place values (2^i).
static long long digits_value_8(const int digits[8]) {
    long long value = 0;
    for (int i = 0; i < 8; i++) value += static_cast<long long>(digits[i]) * (1LL << i);
    return value;
}

// Decodes the (N+1)-digit dual-rail result: each (S_plus[i], S_minus[i])
// pair is a digit at binary place value 2^i, coded 00=-1, 11=+1, 01/10=0
// (same coding as the operand rails).
static long long decode_dual_rail_9(const sc_lv<9>& s_plus, const sc_lv<9>& s_minus) {
    long long value = 0;
    for (int i = 0; i < 9; i++) {
        bool p = s_plus[i].is_01() && s_plus[i].to_bool();
        bool m = s_minus[i].is_01() && s_minus[i].to_bool();
        int digit = (p && m) ? 1 : ((!p && !m) ? -1 : 0);
        value += static_cast<long long>(digit) * (1LL << i);
    }
    return value;
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
// 2. 8-DIGIT SIGNED-DIGIT ("PPM") ADDER -- exact port of the reference VHDL
// ============================================================================
SC_MODULE(SignedDigitAdder8) {
    sc_in<sc_lv<8>> A_a;
    sc_in<sc_lv<8>> A_b;
    sc_in<sc_lv<8>> B_a;
    sc_in<sc_lv<8>> B_b;

    sc_out<sc_lv<9>> S_minus;
    sc_out<sc_lv<9>> S_plus;

    sc_signal<bool> sa_a[8];
    sc_signal<bool> sa_b[8];
    sc_signal<bool> sb_a[8];
    sc_signal<bool> sb_b[8];

    sc_signal<bool> stage1_sum[8];
    sc_signal<bool> stage1_carry[8];

    sc_signal<bool> stage2_sum[8];
    sc_signal<bool> stage2_carry[8];

    sc_signal<bool> const_one;

    BsdBinaryFullAdder* stage1_adders[8];
    BsdBinaryFullAdder* stage2_adders[8];

    void drive_const_one() { const_one.write(true); }

    SC_CTOR(SignedDigitAdder8) {
        SC_METHOD(drive_const_one);

        for (int i = 0; i < 8; i++) {
            std::string stage1_name = "S1_FA" + std::to_string(i + 1);
            stage1_adders[i] = new BsdBinaryFullAdder(stage1_name.c_str());

            // Stage 1 FA(i): a=A_a[i], b=B_a[i], c=A_b[i] -- matches gate0..gate3
            // of the reference VHDL (DIG_Add port map a=>Aia, b=>Bia, c_i=>Aib).
            stage1_adders[i]->input_a(sa_a[i]);
            stage1_adders[i]->input_b(sb_a[i]);
            stage1_adders[i]->input_c(sa_b[i]);
            stage1_adders[i]->sum_out(stage1_sum[i]);
            stage1_adders[i]->carry_out(stage1_carry[i]);

            std::string stage2_name = "S2_FA" + std::to_string(i + 1);
            stage2_adders[i] = new BsdBinaryFullAdder(stage2_name.c_str());

            // Stage 2 FA(i): a=(i==0 ? 1 : stage1_carry[i-1]), b=stage1_sum[i],
            // c=B_b[i] -- matches gate4..gate7 (a=>s_prev/n1, b=>s_even, c_i=>Bib).
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
        for (int i = 0; i < 8; i++) {
            sensitive << stage2_sum[i] << stage2_carry[i];
        }
        sensitive << stage1_carry[7];
    }

    void unpack_inputs() {
        sc_lv<8> value_a_a = A_a.read();
        sc_lv<8> value_a_b = A_b.read();
        sc_lv<8> value_b_a = B_a.read();
        sc_lv<8> value_b_b = B_b.read();

        for (int i = 0; i < 8; i++) {
            sa_a[i].write(value_a_a[i].is_01() ? value_a_a[i].to_bool() : false);
            sa_b[i].write(value_a_b[i].is_01() ? value_a_b[i].to_bool() : false);
            sb_a[i].write(value_b_a[i].is_01() ? value_b_a[i].to_bool() : false);
            sb_b[i].write(value_b_b[i].is_01() ? value_b_b[i].to_bool() : false);
        }
    }

    void pack_outputs() {
        sc_lv<9> output_minus;
        sc_lv<9> output_plus;

        output_minus[0] = false;  // no carry-in below the least significant digit
        for (int i = 0; i < 8; i++) {
            output_minus[i + 1] = stage2_carry[i].read();
            output_plus[i] = stage2_sum[i].read();
        }

        output_plus[8] = stage1_carry[7].read();

        S_minus.write(output_minus);
        S_plus.write(output_plus);
    }

    void reset_counters() {
        for (int i = 0; i < 8; i++) {
            stage1_adders[i]->reset_counters();
            stage2_adders[i]->reset_counters();
        }
    }

    uint64_t total_switches() const {
        uint64_t total_switches = 0;
        for (int i = 0; i < 8; i++) {
            total_switches += stage1_adders[i]->total_switches();
            total_switches += stage2_adders[i]->total_switches();
        }
        return total_switches;
    }

    void print_report() {
        std::cout << "TER-8: GATES=96 SWITCHES=" << total_switches() << " DELAY=4ns\n";
    }

    ~SignedDigitAdder8() {
        for (int i = 0; i < 8; i++) {
            delete stage1_adders[i];
            delete stage2_adders[i];
        }
    }
};

// ============================================================================
// 3. TESTBENCH -- exhaustive over the full redundant digit-space: every one
//    of the 3^8 digit patterns (each digit in {-1,0,+1}) for A, crossed
//    with every one of the 3^8 patterns for B.
// ============================================================================
SC_MODULE(Testbench) {
    sc_out<sc_lv<8>> A_a;
    sc_out<sc_lv<8>> A_b;
    sc_out<sc_lv<8>> B_a;
    sc_out<sc_lv<8>> B_b;

    sc_in<sc_lv<9>> S_minus;
    sc_in<sc_lv<9>> S_plus;

    SignedDigitAdder8* design_ptr;

    unsigned int number_of_errors;

    unsigned long long a_start;
    unsigned long long a_end;
    bool quiet;

    SC_CTOR(Testbench)
        : design_ptr(nullptr),
          number_of_errors(0),
          a_start(0),
          a_end(POW3_8),
          quiet(false)
    {
        SC_THREAD(stimulus);
    }

    void stimulus() {
        int zero_digits[8];
        for (int i = 0; i < 8; i++) zero_digits[i] = 0;
        DigitRails8 zero = encode_digits_8(zero_digits);

        A_a.write(zero.rail_a);
        A_b.write(zero.rail_b);
        B_a.write(zero.rail_a);
        B_b.write(zero.rail_b);

        wait(20, SC_NS);

        design_ptr->reset_counters();

        for (unsigned long long a_idx = a_start; a_idx < a_end; a_idx++) {
            for (unsigned long long b_idx = 0; b_idx < POW3_8; b_idx++) {
                apply_test(a_idx, b_idx);
            }
        }

        assert(number_of_errors == 0);
        if (!quiet) {
            design_ptr->print_report();
        }

        sc_stop();
    }

    void apply_test(unsigned long long a_idx, unsigned long long b_idx) {
        int a_digits[8], b_digits[8];
        digits_from_index_8(a_idx, a_digits);
        digits_from_index_8(b_idx, b_digits);

        DigitRails8 encoded_a = encode_digits_8(a_digits);
        DigitRails8 encoded_b = encode_digits_8(b_digits);

        A_a.write(encoded_a.rail_a);
        A_b.write(encoded_a.rail_b);
        B_a.write(encoded_b.rail_a);
        B_b.write(encoded_b.rail_b);

        wait(20, SC_NS);

        long long expected_result = digits_value_8(a_digits) + digits_value_8(b_digits);
        long long actual_result = decode_dual_rail_9(S_plus.read(), S_minus.read());

        if (expected_result != actual_result) {
            number_of_errors++;
        }
    }
};

// ============================================================================
// 4. MAIN ENTRY POINT
// ============================================================================

// Elaborates a fresh adder + testbench and simulates exactly one a-index
// slice [a_start, a_end) to completion in the calling process. Used both
// for manual external partitioning (one process, prints its own report)
// and as the body of each forked worker in the auto-parallel default path.
static void run_slice(unsigned long long partition_index, unsigned long long num_partitions,
                       bool quiet, uint64_t& out_switches, unsigned int& out_errors) {
    sc_signal<sc_lv<8>> A_a, A_b, B_a, B_b;
    sc_signal<sc_lv<9>> S_minus, S_plus;

    SignedDigitAdder8 adder("SignedDigitAdder8");
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

    unsigned long long slice = POW3_8 / num_partitions;
    tb.a_start = partition_index * slice;
    tb.a_end = (partition_index == num_partitions - 1) ? POW3_8 : (partition_index + 1) * slice;
    tb.quiet = quiet;

    sc_start();

    out_switches = adder.total_switches();
    out_errors = tb.number_of_errors;
}

int sc_main(int argc, char* argv[]) {
    // Manual external partitioning: "ter_8bit <partition_index> <num_partitions>"
    // runs exactly that slice of the 3^8 a-digit-pattern space in this
    // single process and prints its own partial report.
    if (argc == 3) {
        unsigned long long partition_index = std::stoull(argv[1]);
        unsigned long long num_partitions = std::stoull(argv[2]);
        uint64_t switches = 0; unsigned int errors = 0;
        run_slice(partition_index, num_partitions, /*quiet=*/false, switches, errors);
        assert(errors == 0);
        return 0;
    }

    // Default (no args): fan the exhaustive sweep out across every
    // available CPU core, the same way as rca_glitch.cpp / ter's own
    // 16-bit binary-weighted predecessor.
    unsigned int num_workers = std::thread::hardware_concurrency();
    if (num_workers == 0) num_workers = 1;
    num_workers = static_cast<unsigned int>(std::min<unsigned long long>(num_workers, POW3_8));

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
    std::cout << "TER-8: GATES=96 SWITCHES=" << total_switches << " DELAY=4ns\n";

    return 0;
}
