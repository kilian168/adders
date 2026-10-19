// ter_glitch.cpp -- 4-digit redundant signed-digit ("PMM") adder.
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
struct DigitRails4 {
    sc_lv<4> rail_a;
    sc_lv<4> rail_b;
};

// Encodes an array of N signed digits (each -1, 0, or +1) into dual rails,
// one canonical encoding per digit value: -1->(0,0), 0->(0,1), +1->(1,1).
static DigitRails4 encode_digits_4(const int digits[4]) {
    DigitRails4 encoded;
    encoded.rail_a = "0000";
    encoded.rail_b = "0000";
    for (int i = 0; i < 4; i++) {
        int d = digits[i];
        encoded.rail_a[i] = (d == 1);
        encoded.rail_b[i] = (d != -1);
    }
    return encoded;
}

// Decodes a base-3 index (0 .. 3^N - 1) into N signed digits via base-3
// digit extraction, each trit mapped {0,1,2} -> {-1,0,+1}.
static void digits_from_index_4(unsigned long long idx, int digits[4]) {
    for (int i = 0; i < 4; i++) {
        digits[i] = static_cast<int>(idx % 3ULL) - 1;
        idx /= 3ULL;
    }
}

static const unsigned long long POW3_4 = 81ULL;

// Numeric value of a digit pattern at binary place values (2^i).
static long long digits_value_4(const int digits[4]) {
    long long value = 0;
    for (int i = 0; i < 4; i++) value += static_cast<long long>(digits[i]) * (1LL << i);
    return value;
}

// Decodes the (N+1)-digit dual-rail result: each (S_plus[i], S_minus[i])
// pair is a digit at binary place value 2^i, coded 00=-1, 11=+1, 01/10=0
// (same coding as the operand rails).
static long long decode_dual_rail_5(const sc_lv<5>& s_plus, const sc_lv<5>& s_minus) {
    long long value = 0;
    for (int i = 0; i < 5; i++) {
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
// 2. 4-DIGIT SIGNED-DIGIT ("PMM") ADDER -- exact port of the reference VHDL
// ============================================================================
SC_MODULE(SignedDigitAdder4) {
    sc_in<sc_lv<4>> A_a;
    sc_in<sc_lv<4>> A_b;
    sc_in<sc_lv<4>> B_a;
    sc_in<sc_lv<4>> B_b;

    sc_out<sc_lv<5>> S_minus;
    sc_out<sc_lv<5>> S_plus;

    sc_signal<bool> sa_a[4];
    sc_signal<bool> sa_b[4];
    sc_signal<bool> sb_a[4];
    sc_signal<bool> sb_b[4];

    sc_signal<bool> stage1_sum[4];
    sc_signal<bool> stage1_carry[4];

    sc_signal<bool> stage2_sum[4];
    sc_signal<bool> stage2_carry[4];

    sc_signal<bool> const_one;

    BsdBinaryFullAdder* stage1_adders[4];
    BsdBinaryFullAdder* stage2_adders[4];

    void drive_const_one() { const_one.write(true); }

    SC_CTOR(SignedDigitAdder4) {
        SC_METHOD(drive_const_one);

        for (int i = 0; i < 4; i++) {
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
        for (int i = 0; i < 4; i++) {
            sensitive << stage2_sum[i] << stage2_carry[i];
        }
        sensitive << stage1_carry[3];
    }

    void unpack_inputs() {
        sc_lv<4> value_a_a = A_a.read();
        sc_lv<4> value_a_b = A_b.read();
        sc_lv<4> value_b_a = B_a.read();
        sc_lv<4> value_b_b = B_b.read();

        for (int i = 0; i < 4; i++) {
            sa_a[i].write(value_a_a[i].is_01() ? value_a_a[i].to_bool() : false);
            sa_b[i].write(value_a_b[i].is_01() ? value_a_b[i].to_bool() : false);
            sb_a[i].write(value_b_a[i].is_01() ? value_b_a[i].to_bool() : false);
            sb_b[i].write(value_b_b[i].is_01() ? value_b_b[i].to_bool() : false);
        }
    }

    void pack_outputs() {
        sc_lv<5> output_minus;
        sc_lv<5> output_plus;

        output_minus[0] = false;  // no carry-in below the least significant digit
        for (int i = 0; i < 4; i++) {
            output_minus[i + 1] = stage2_carry[i].read();
            output_plus[i] = stage2_sum[i].read();
        }

        output_plus[4] = stage1_carry[3].read();

        S_minus.write(output_minus);
        S_plus.write(output_plus);
    }

    void reset_counters() {
        for (int i = 0; i < 4; i++) {
            stage1_adders[i]->reset_counters();
            stage2_adders[i]->reset_counters();
        }
    }

    uint64_t total_switches() const {
        uint64_t total_switches = 0;
        for (int i = 0; i < 4; i++) {
            total_switches += stage1_adders[i]->total_switches();
            total_switches += stage2_adders[i]->total_switches();
        }
        return total_switches;
    }

    void print_report() {
        std::cout << "TER-4: GATES=48 SWITCHES=" << total_switches() << " DELAY=4ns\n";
    }

    ~SignedDigitAdder4() {
        for (int i = 0; i < 4; i++) {
            delete stage1_adders[i];
            delete stage2_adders[i];
        }
    }
};

// ============================================================================
// 3. FUNCTIONAL TESTBENCH -- a few hand-picked, human-checkable numbers
//    (not exhaustive/random), to directly confirm: (a) no "-15/-16"
//    correction is needed on the raw per-digit decode, and (b) redundant
//    encodings of the same value (e.g. two different digit patterns
//    both worth 2) still add correctly.
// ============================================================================
struct Case {
    const char* label;
    int a_digits[4];
    int b_digits[4];
};

SC_MODULE(Testbench) {
    sc_out<sc_lv<4>> A_a, A_b, B_a, B_b;
    sc_in<sc_lv<5>> S_minus, S_plus;

    SignedDigitAdder4* design_ptr;
    int failures;

    SC_CTOR(Testbench) : design_ptr(nullptr), failures(0) {
        SC_THREAD(stimulus);
    }

    void run_case(const Case& c) {
        DigitRails4 ea = encode_digits_4(c.a_digits);
        DigitRails4 eb = encode_digits_4(c.b_digits);

        A_a.write(ea.rail_a);
        A_b.write(ea.rail_b);
        B_a.write(eb.rail_a);
        B_b.write(eb.rail_b);

        wait(20, SC_NS);

        long long a_val = digits_value_4(c.a_digits);
        long long b_val = digits_value_4(c.b_digits);
        long long expected = a_val + b_val;
        long long actual = decode_dual_rail_5(S_plus.read(), S_minus.read());

        bool pass = (expected == actual);
        if (!pass) failures++;

        auto fmt_digits = [](const int d[4]) {
            std::string s = "(";
            for (int i = 3; i >= 0; i--) { s += std::to_string(d[i]); if (i) s += ","; }
            s += ")";
            return s;
        };

        std::cout << std::left << std::setw(28) << c.label
                  << " A=" << std::setw(4) << a_val << fmt_digits(c.a_digits)
                  << " B=" << std::setw(4) << b_val << fmt_digits(c.b_digits)
                  << " expected=" << std::setw(5) << expected
                  << " actual=" << std::setw(5) << actual
                  << (pass ? "  PASS" : "  *** FAIL ***")
                  << "\n";
    }

    void stimulus() {
        int zero[4] = {0, 0, 0, 0};
        DigitRails4 z = encode_digits_4(zero);
        A_a.write(z.rail_a); A_b.write(z.rail_b);
        B_a.write(z.rail_a); B_b.write(z.rail_b);
        wait(20, SC_NS);
        design_ptr->reset_counters();

        std::cout << "\n=== 4-digit signed-digit (\"PMM\") adder -- functional check ===\n";
        std::cout << "digit tuples printed MSB..LSB, place values (8,4,2,1)\n\n";

        // digit[] index 0 = LSB (place value 1), index 3 = MSB (place value 8)
        run_case({"0 + 0",            {0,0,0,0},   {0,0,0,0}});
        run_case({"7 + 8 = 15",       {1,1,1,0},   {0,0,0,1}});
        run_case({"15 + 15 = 30",     {1,1,1,1},   {1,1,1,1}});
        run_case({"-15 + -15 = -30",  {-1,-1,-1,-1}, {-1,-1,-1,-1}});
        run_case({"5 + -3 = 2",       {1,0,1,0},   {1,0,-1,0}});
        run_case({"1 + -1 = 0",       {1,0,0,0},   {-1,0,0,0}});
        run_case({"-8 + -7 = -15",    {0,0,0,-1},  {-1,-1,-1,0}});
        run_case({"2 (redundant enc)+0", {0,-1,1,0}, {0,0,0,0}});   // 2 via digit1=-1,digit2=+1 instead of digit1=+1
        run_case({"9 + 6 = 15",       {1,0,0,1},   {0,1,1,0}});
        run_case({"-1 + -1 = -2",     {-1,0,0,0},  {-1,0,0,0}});

        std::cout << "\n";
        if (failures == 0) {
            std::cout << "ALL FUNCTIONAL CASES PASSED (" << "no correction/offset needed on the decode)\n";
        } else {
            std::cout << failures << " CASE(S) FAILED\n";
        }

        assert(failures == 0);
        sc_stop();
    }
};

// ============================================================================
// 4. MAIN
// ============================================================================
int sc_main(int argc, char* argv[]) {
    sc_signal<sc_lv<4>> A_a, A_b, B_a, B_b;
    sc_signal<sc_lv<5>> S_minus, S_plus;

    SignedDigitAdder4 adder("SignedDigitAdder4");
    Testbench tb("TB");

    tb.design_ptr = &adder;

    adder.A_a(A_a); adder.A_b(A_b); adder.B_a(B_a); adder.B_b(B_b);
    adder.S_minus(S_minus); adder.S_plus(S_plus);

    tb.A_a(A_a); tb.A_b(A_b); tb.B_a(B_a); tb.B_b(B_b);
    tb.S_minus(S_minus); tb.S_plus(S_plus);

    sc_start();

    return 0;
}
