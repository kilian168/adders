// main.cpp
#include <systemc.h>
#include <iostream>
#include <iomanip>
#include <string>

// ============================================================================
// 0. HELPER STRUCTS AND FUNCTIONS
// ============================================================================
struct DualRail8 {
    sc_lv<8> rail_a;
    sc_lv<8> rail_b;
};

static DualRail8 encode_unsigned_to_balanced_ternary_8(unsigned int value) {
    DualRail8 encoded;
    encoded.rail_a = "00000000";
    encoded.rail_b = "00000000";

    if (value > 255U) {
        std::cerr << "Fehler: Der 8-Trit-Test erwartet Werte im Bereich 0..255.\n";
        sc_stop();
        return encoded;
    }

    int remaining_value = static_cast<int>(value);

    for (int i = 0; i < 8; i++) {
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
// 1. GATE-LEVEL BINARY FULL ADDER WITH DELAY & COUNTERS
// ============================================================================
SC_MODULE(BsdBinaryFullAdder) {
    sc_in<bool> input_a, input_b, input_c;
    sc_out<bool> sum_out, carry_out;

    bool xor1_val, and1_val, and2_val, and3_val;
    bool sum_val, carry_val;
    bool xor1_old, xor2_old, and1_old, and2_old, and3_old, or_old;

    sc_event ev_tier2, ev_tier3;

    unsigned int xor1_calls, xor1_switches;
    unsigned int xor2_calls, xor2_switches;
    unsigned int and1_calls, and1_switches;
    unsigned int and2_calls, and2_switches;
    unsigned int and3_calls, and3_switches;
    unsigned int or1_calls, or1_switches;

    SC_CTOR(BsdBinaryFullAdder)
        : xor1_val(false),
          and1_val(false),
          and2_val(false),
          and3_val(false),
          sum_val(false),
          carry_val(false),
          xor1_old(false),
          xor2_old(false),
          and1_old(false),
          and2_old(false),
          and3_old(false),
          or_old(false),
          xor1_calls(0),
          xor1_switches(0),
          xor2_calls(0),
          xor2_switches(0),
          and1_calls(0),
          and1_switches(0),
          and2_calls(0),
          and2_switches(0),
          and3_calls(0),
          and3_switches(0),
          or1_calls(0),
          or1_switches(0)
    {
        SC_METHOD(process_tier1);
        dont_initialize();
        sensitive << input_a << input_b << input_c;

        SC_METHOD(process_tier2);
        dont_initialize();
        sensitive << ev_tier2;

        SC_METHOD(process_tier3);
        dont_initialize();
        sensitive << ev_tier3;
    }

    void process_tier1() {
        bool value_a = input_a.read();
        bool value_b = input_b.read();
        bool value_c = input_c.read();

        xor1_calls++;
        bool next_xor1 = value_a ^ value_b;
        if (next_xor1 != xor1_old) {
            xor1_old = next_xor1;
            xor1_switches++;
            xor1_val = next_xor1;
        }

        and1_calls++;
        bool next_and1 = value_a & value_b;
        if (next_and1 != and1_old) {
            and1_old = next_and1;
            and1_switches++;
            and1_val = next_and1;
        }

        and2_calls++;
        bool next_and2 = value_a & value_c;
        if (next_and2 != and2_old) {
            and2_old = next_and2;
            and2_switches++;
            and2_val = next_and2;
        }

        and3_calls++;
        bool next_and3 = value_b & value_c;
        if (next_and3 != and3_old) {
            and3_old = next_and3;
            and3_switches++;
            and3_val = next_and3;
        }

        ev_tier2.notify(1, SC_NS);
    }

    void process_tier2() {
        bool value_c = input_c.read();

        xor2_calls++;
        bool next_xor2 = xor1_val ^ value_c;
        if (next_xor2 != xor2_old) {
            xor2_old = next_xor2;
            xor2_switches++;
            sum_val = next_xor2;
        }

        or1_calls++;
        bool next_or = and1_val | and2_val | and3_val;
        if (next_or != or_old) {
            or_old = next_or;
            or1_switches++;
            carry_val = next_or;
        }

        ev_tier3.notify(1, SC_NS);
    }

    void process_tier3() {
        sum_out.write(sum_val);
        carry_out.write(carry_val);
    }

    void reset_counters() {
        xor1_calls = 0; xor1_switches = 0;
        xor2_calls = 0; xor2_switches = 0;
        and1_calls = 0; and1_switches = 0;
        and2_calls = 0; and2_switches = 0;
        and3_calls = 0; and3_switches = 0;
        or1_calls  = 0; or1_switches  = 0;
    }
};

// ============================================================================
// 2. 8-TRIT BALANCED TERNARY ADDER MODULE
// ============================================================================
SC_MODULE(BalancedTernaryAdder8) {
    sc_in<sc_lv<8>> A_a, A_b, B_a, B_b;
    sc_out<sc_lv<9>> S_minus, S_plus;

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

    SC_CTOR(BalancedTernaryAdder8) {
        const_one.write(true);

        for (int i = 0; i < 8; i++) {
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

        for (int i = 0; i < 8; i++) {
            output_minus[i] = stage2_carry[i].read();
            output_plus[i] = stage2_sum[i].read();
        }

        output_minus[8] = stage2_carry[7].read();
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

    void print_report() {
        unsigned int total_switches = 0;
        for (int i = 0; i < 8; i++) {
            total_switches +=
                stage1_adders[i]->xor1_switches +
                stage1_adders[i]->xor2_switches +
                stage1_adders[i]->and1_switches +
                stage1_adders[i]->and2_switches +
                stage1_adders[i]->and3_switches +
                stage1_adders[i]->or1_switches;

            total_switches +=
                stage2_adders[i]->xor1_switches +
                stage2_adders[i]->xor2_switches +
                stage2_adders[i]->and1_switches +
                stage2_adders[i]->and2_switches +
                stage2_adders[i]->and3_switches +
                stage2_adders[i]->or1_switches;
        }

        std::cout << "TER-8: GATES=96 SWITCHES=" << total_switches << " DELAY=4ns\n";
    }

    ~BalancedTernaryAdder8() {
        for (int i = 0; i < 8; i++) {
            delete stage1_adders[i];
            delete stage2_adders[i];
        }
    }
};

// ============================================================================
// 3. TESTBENCH
// ============================================================================
SC_MODULE(Testbench) {
    sc_out<sc_lv<8>> A_a, A_b, B_a, B_b;
    sc_in<sc_lv<9>> S_minus, S_plus;

    BalancedTernaryAdder8* design_ptr;

    SC_CTOR(Testbench)
        : design_ptr(nullptr)
    {
        SC_THREAD(stimulus);
    }

    void stimulus() {
        DualRail8 zero = encode_unsigned_to_balanced_ternary_8(0);

        A_a.write(zero.rail_a);
        A_b.write(zero.rail_b);
        B_a.write(zero.rail_a);
        B_b.write(zero.rail_b);

        wait(20, SC_NS);

        design_ptr->reset_counters();

        for (unsigned int a_value = 0; a_value < 256; a_value++) {
            for (unsigned int b_value = 0; b_value < 256; b_value++) {
                apply_test(a_value, b_value);
            }
        }

        design_ptr->print_report();

        sc_stop();
    }

    void apply_test(unsigned int a_value, unsigned int b_value) {
        DualRail8 encoded_a = encode_unsigned_to_balanced_ternary_8(a_value);
        DualRail8 encoded_b = encode_unsigned_to_balanced_ternary_8(b_value);

        A_a.write(encoded_a.rail_a);
        A_b.write(encoded_a.rail_b);
        B_a.write(encoded_b.rail_a);
        B_b.write(encoded_b.rail_b);

        wait(20, SC_NS);
    }
};

// ============================================================================
// 4. MAIN
// ============================================================================
int sc_main(int argc, char* argv[]) {
    sc_signal<sc_lv<8>> A_a, A_b, B_a, B_b;
    sc_signal<sc_lv<9>> S_minus, S_plus;

    BalancedTernaryAdder8 adder("BalancedTernaryAdder8");
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

    sc_start();

    return 0;
}