// main.cpp
#include <systemc.h>
#include <iostream>
#include <iomanip>
#include <string>
#include <cassert>

// ============================================================================
// 0. HELPER STRUCTS AND FUNCTIONS
// ============================================================================
struct DualRail12 {
    sc_lv<12> rail_a;
    sc_lv<12> rail_b;
};

// Encodes each bit of `value` as a dual-rail digit at binary place value 2^i
// (despite the function's name, the circuit's digits are binary-weighted,
// not base-3 - confirmed against the reference schematic). Coding per digit:
// 0 -> (rail_a=0, rail_b=1), 1 -> (rail_a=1, rail_b=1). rail_b is therefore
// always 1; only rail_a carries the bit value.
static DualRail12 encode_unsigned_to_balanced_ternary_12(unsigned int value) {
    DualRail12 encoded;
    encoded.rail_a = "000000000000";
    encoded.rail_b = "000000000000";

    if (value > 4095U) {
        std::cerr << "Fehler: Der 12-Bit-Test erwartet Werte im Bereich 0..4095.\n";
        sc_stop();
        return encoded;
    }

    for (int i = 0; i < 12; i++) {
        bool bit = ((value >> i) & 1U) != 0U;
        encoded.rail_a[i] = bit;
        encoded.rail_b[i] = true;
    }

    return encoded;
}

// Decodes the 13-digit dual-rail result: each (S_plus[i], S_minus[i]) pair
// is a digit at binary place value 2^i, coded 00=-1, 11=+1, 01/10=0
// (same coding as the operand rails).
static int decode_dual_rail_13(const sc_lv<13>& s_plus, const sc_lv<13>& s_minus) {
    int value = 0;
    for (int i = 0; i < 13; i++) {
        bool p = s_plus[i].is_01() && s_plus[i].to_bool();
        bool m = s_minus[i].is_01() && s_minus[i].to_bool();
        int digit = (p && m) ? 1 : ((!p && !m) ? -1 : 0);
        value += digit * (1 << i);
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
    sc_in<bool> input_a, input_b, input_c;
    sc_out<bool> sum_out, carry_out;

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
// 2. 12-TRIT BALANCED TERNARY ADDER MODULE
// ============================================================================
SC_MODULE(BalancedTernaryAdder12) {
    sc_in<sc_lv<12>> A_a, A_b, B_a, B_b;
    sc_out<sc_lv<13>> S_minus, S_plus;

    sc_signal<bool> sa_a[12];
    sc_signal<bool> sa_b[12];
    sc_signal<bool> sb_a[12];
    sc_signal<bool> sb_b[12];

    sc_signal<bool> stage1_sum[12];
    sc_signal<bool> stage1_carry[12];

    sc_signal<bool> stage2_sum[12];
    sc_signal<bool> stage2_carry[12];

    sc_signal<bool> const_one;

    BsdBinaryFullAdder* stage1_adders[12];
    BsdBinaryFullAdder* stage2_adders[12];

    SC_CTOR(BalancedTernaryAdder12) {
        const_one.write(true);

        for (int i = 0; i < 12; i++) {
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
        for (int i = 0; i < 12; i++) {
            sensitive << stage2_sum[i] << stage2_carry[i];
        }
        sensitive << stage1_carry[11];
    }

    void unpack_inputs() {
        sc_lv<12> value_a_a = A_a.read();
        sc_lv<12> value_a_b = A_b.read();
        sc_lv<12> value_b_a = B_a.read();
        sc_lv<12> value_b_b = B_b.read();

        for (int i = 0; i < 12; i++) {
            sa_a[i].write(value_a_a[i].is_01() ? value_a_a[i].to_bool() : false);
            sa_b[i].write(value_a_b[i].is_01() ? value_a_b[i].to_bool() : false);
            sb_a[i].write(value_b_a[i].is_01() ? value_b_a[i].to_bool() : false);
            sb_b[i].write(value_b_b[i].is_01() ? value_b_b[i].to_bool() : false);
        }
    }

    void pack_outputs() {
        sc_lv<13> output_minus;
        sc_lv<13> output_plus;

        output_minus[0] = false;  // no carry-in below the least significant digit
        for (int i = 0; i < 12; i++) {
            output_minus[i + 1] = stage2_carry[i].read();
            output_plus[i] = stage2_sum[i].read();
        }

        output_plus[12] = stage1_carry[11].read();

        S_minus.write(output_minus);
        S_plus.write(output_plus);
    }

    void reset_counters() {
        for (int i = 0; i < 12; i++) {
            stage1_adders[i]->reset_counters();
            stage2_adders[i]->reset_counters();
        }
    }

    void print_report() {
        unsigned int total_switches = 0;
        for (int i = 0; i < 12; i++) {
            total_switches += stage1_adders[i]->total_switches();
            total_switches += stage2_adders[i]->total_switches();
        }

        std::cout << "TER-12: GATES=144 SWITCHES=" << total_switches << " DELAY=4ns\n";
    }

    ~BalancedTernaryAdder12() {
        for (int i = 0; i < 12; i++) {
            delete stage1_adders[i];
            delete stage2_adders[i];
        }
    }
};

// ============================================================================
// 3. TESTBENCH – exhaustive 12-trit test (4096 x 4096 = 16,777,216 cases)
// ============================================================================
SC_MODULE(Testbench) {
    sc_out<sc_lv<12>> A_a, A_b, B_a, B_b;
    sc_in<sc_lv<13>> S_minus, S_plus;

    BalancedTernaryAdder12* design_ptr;

    unsigned int number_of_errors;

    unsigned int a_start;
    unsigned int a_end;

    SC_CTOR(Testbench)
        : design_ptr(nullptr),
          number_of_errors(0),
          a_start(0),
          a_end(4096)
    {
        SC_THREAD(stimulus);
    }

    void stimulus() {
        DualRail12 zero = encode_unsigned_to_balanced_ternary_12(0);

        A_a.write(zero.rail_a);
        A_b.write(zero.rail_b);
        B_a.write(zero.rail_a);
        B_b.write(zero.rail_b);

        wait(20, SC_NS);

        design_ptr->reset_counters();

        for (unsigned int a_value = a_start; a_value < a_end; a_value++) {
            for (unsigned int b_value = 0; b_value < 4096; b_value++) {
                apply_test(a_value, b_value);
            }
        }

        assert(number_of_errors == 0);
        design_ptr->print_report();

        sc_stop();
    }

    void apply_test(unsigned int a_value, unsigned int b_value) {
        DualRail12 encoded_a = encode_unsigned_to_balanced_ternary_12(a_value);
        DualRail12 encoded_b = encode_unsigned_to_balanced_ternary_12(b_value);

        A_a.write(encoded_a.rail_a);
        A_b.write(encoded_a.rail_b);
        B_a.write(encoded_b.rail_a);
        B_b.write(encoded_b.rail_b);

        wait(20, SC_NS);

        int expected_result = static_cast<int>(a_value + b_value);
        int actual_result = decode_dual_rail_13(S_plus.read(), S_minus.read());

        if (expected_result != actual_result) {
            number_of_errors++;
        }
    }
};

// ============================================================================
// 4. MAIN
// ============================================================================
int sc_main(int argc, char* argv[]) {
    sc_signal<sc_lv<12>> A_a, A_b, B_a, B_b;
    sc_signal<sc_lv<13>> S_minus, S_plus;

    BalancedTernaryAdder12 adder("BalancedTernaryAdder12");
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

    // Optional partitioning: "ter_12bit <partition_index> <num_partitions>"
    // restricts this process to a slice of the a-value range, so the
    // exhaustive sweep can be split across parallel processes.
    if (argc == 3) {
        unsigned int partition_index = static_cast<unsigned int>(std::stoul(argv[1]));
        unsigned int num_partitions = static_cast<unsigned int>(std::stoul(argv[2]));
        unsigned int slice = 4096U / num_partitions;
        tb.a_start = partition_index * slice;
        tb.a_end = (partition_index == num_partitions - 1) ? 4096U : (partition_index + 1) * slice;
    }

    sc_start();

    return 0;
}
