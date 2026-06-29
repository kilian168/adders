// main.cpp
#include <systemc.h>
#include <iostream>
#include <iomanip>
#include <string>

// ============================================================================
// 0. HELPER STRUCTS AND FUNCTIONS
// ============================================================================
struct DualRail4 {
    sc_lv<4> rail_a;
    sc_lv<4> rail_b;
};

static DualRail4 encode_unsigned_to_balanced_ternary_4(unsigned int value) {
    DualRail4 encoded;
    encoded.rail_a = "0000";
    encoded.rail_b = "0000";

    if (value > 15U) {
        std::cerr << "Fehler: Der 4-Trit-Test erwartet Werte im Bereich 0..15.\n";
        sc_stop();
        return encoded;
    }

    int remaining_value = static_cast<int>(value);

    for (int i = 0; i < 4; i++) {
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
        bool va = input_a.read();
        bool vb = input_b.read();
        bool vc = input_c.read();

        xor1_calls++;
        bool next_xor1 = va ^ vb;
        if (next_xor1 != xor1_old) {
            xor1_old = next_xor1;
            xor1_switches++;
            xor1_val = next_xor1;
        }

        and1_calls++;
        bool next_and1 = va & vb;
        if (next_and1 != and1_old) {
            and1_old = next_and1;
            and1_switches++;
            and1_val = next_and1;
        }

        and2_calls++;
        bool next_and2 = va & vc;
        if (next_and2 != and2_old) {
            and2_old = next_and2;
            and2_switches++;
            and2_val = next_and2;
        }

        and3_calls++;
        bool next_and3 = vb & vc;
        if (next_and3 != and3_old) {
            and3_old = next_and3;
            and3_switches++;
            and3_val = next_and3;
        }

        ev_tier2.notify(1, SC_NS);
    }

    void process_tier2() {
        bool vc = input_c.read();

        xor2_calls++;
        bool next_xor2 = xor1_val ^ vc;
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
// 2. BALANCED TERNARY ADDER MODULE
// ============================================================================
SC_MODULE(BalancedTernaryAdder) {
    sc_in<sc_lv<4>> A_a, A_b, B_a, B_b;
    sc_out<sc_lv<5>> S_minus, S_plus;

    sc_signal<bool> sa_a[4];
    sc_signal<bool> sa_b[4];
    sc_signal<bool> sb_a[4];
    sc_signal<bool> sb_b[4];

    sc_signal<bool> s_minus_out[5];
    sc_signal<bool> s_plus_out[5];

    sc_signal<bool> fa1_sum, fa1_carry;
    sc_signal<bool> fa2_sum, fa2_carry;
    sc_signal<bool> fa4_sum, fa4_carry;
    sc_signal<bool> fa7_sum, fa7_carry;
    sc_signal<bool> fa8_sum, fa8_carry;

    sc_signal<bool> const_one;

    BsdBinaryFullAdder *fa1, *fa2, *fa3, *fa4, *fa5, *fa6, *fa7, *fa8;

    SC_CTOR(BalancedTernaryAdder) {
        const_one.write(true);

        // --- SPALTE 1 ---
        fa1 = new BsdBinaryFullAdder("FA1");
        fa1->input_a(sa_a[0]);
        fa1->input_b(sb_a[0]);
        fa1->input_c(sa_b[0]);
        fa1->sum_out(fa1_sum);
        fa1->carry_out(fa1_carry);

        fa2 = new BsdBinaryFullAdder("FA2");
        fa2->input_a(sa_a[1]);
        fa2->input_b(sb_a[1]);
        fa2->input_c(sa_b[1]);
        fa2->sum_out(fa2_sum);
        fa2->carry_out(fa2_carry);

        fa4 = new BsdBinaryFullAdder("FA4");
        fa4->input_a(sa_a[2]);
        fa4->input_b(sb_a[2]);
        fa4->input_c(sa_b[2]);
        fa4->sum_out(fa4_sum);
        fa4->carry_out(fa4_carry);

        fa7 = new BsdBinaryFullAdder("FA7");
        fa7->input_a(sa_a[3]);
        fa7->input_b(sb_a[3]);
        fa7->input_c(sa_b[3]);
        fa7->sum_out(fa7_sum);
        fa7->carry_out(fa7_carry);

        // --- SPALTE 2 ---
        // Eingangsreihenfolge bleibt wie in deinem ursprünglichen Code,
        // damit die Switch-Zahlen mit deiner alten Messung vergleichbar bleiben.
        fa5 = new BsdBinaryFullAdder("FA5");
        fa5->input_a(const_one);
        fa5->input_b(sb_b[0]);
        fa5->input_c(fa1_sum);
        fa5->sum_out(s_plus_out[0]);
        fa5->carry_out(s_minus_out[0]);

        fa3 = new BsdBinaryFullAdder("FA3");
        fa3->input_a(fa1_carry);
        fa3->input_b(sb_b[1]);
        fa3->input_c(fa2_sum);
        fa3->sum_out(s_plus_out[1]);
        fa3->carry_out(s_minus_out[1]);

        fa6 = new BsdBinaryFullAdder("FA6");
        fa6->input_a(fa2_carry);
        fa6->input_b(sb_b[2]);
        fa6->input_c(fa4_sum);
        fa6->sum_out(s_plus_out[2]);
        fa6->carry_out(s_minus_out[2]);

        fa8 = new BsdBinaryFullAdder("FA8");
        fa8->input_a(fa4_carry);
        fa8->input_b(sb_b[3]);
        fa8->input_c(fa7_sum);
        fa8->sum_out(fa8_sum);
        fa8->carry_out(fa8_carry);

        SC_METHOD(assign_fa8_outputs);
        dont_initialize();
        sensitive << fa8_sum << fa8_carry << fa7_carry;

        SC_METHOD(unpack_inputs);
        dont_initialize();
        sensitive << A_a << A_b << B_a << B_b;

        SC_METHOD(pack_outputs);
        dont_initialize();
        for (int i = 0; i < 5; i++) {
            sensitive << s_minus_out[i] << s_plus_out[i];
        }
    }

    void assign_fa8_outputs() {
        s_plus_out[3].write(fa8_sum.read());
        s_minus_out[3].write(fa8_carry.read());

        s_minus_out[4].write(fa8_carry.read());
        s_plus_out[4].write(fa7_carry.read());
    }

    void unpack_inputs() {
        sc_lv<4> va_a = A_a.read();
        sc_lv<4> va_b = A_b.read();
        sc_lv<4> vb_a = B_a.read();
        sc_lv<4> vb_b = B_b.read();

        for (int i = 0; i < 4; i++) {
            sa_a[i].write(va_a[i].is_01() ? va_a[i].to_bool() : false);
            sa_b[i].write(va_b[i].is_01() ? va_b[i].to_bool() : false);
            sb_a[i].write(vb_a[i].is_01() ? vb_a[i].to_bool() : false);
            sb_b[i].write(vb_b[i].is_01() ? vb_b[i].to_bool() : false);
        }
    }

    void pack_outputs() {
        sc_lv<5> out_m;
        sc_lv<5> out_p;

        for (int i = 0; i < 5; i++) {
            out_m[i] = s_minus_out[i].read();
            out_p[i] = s_plus_out[i].read();
        }

        S_minus.write(out_m);
        S_plus.write(out_p);
    }

    void reset_counters() {
        fa1->reset_counters();
        fa2->reset_counters();
        fa3->reset_counters();
        fa4->reset_counters();
        fa5->reset_counters();
        fa6->reset_counters();
        fa7->reset_counters();
        fa8->reset_counters();
    }

    void print_report() {
        unsigned int total_sw = 0;
        BsdBinaryFullAdder* blocks[8] = {fa1, fa2, fa3, fa4, fa5, fa6, fa7, fa8};

        for (int i = 0; i < 8; i++) {
            total_sw += (
                blocks[i]->xor1_switches +
                blocks[i]->xor2_switches +
                blocks[i]->and1_switches +
                blocks[i]->and2_switches +
                blocks[i]->and3_switches +
                blocks[i]->or1_switches
            );
        }

        std::cout << "TER-4: GATES=48 SWITCHES=" << total_sw << " DELAY=4ns\n";
    }

    ~BalancedTernaryAdder() {
        delete fa1;
        delete fa2;
        delete fa3;
        delete fa4;
        delete fa5;
        delete fa6;
        delete fa7;
        delete fa8;
    }
};

// ============================================================================
// 3. TESTBENCH
// ============================================================================
SC_MODULE(Testbench) {
    sc_out<sc_lv<4>> A_a, A_b, B_a, B_b;
    sc_in<sc_lv<5>> S_minus, S_plus;

    BalancedTernaryAdder* design_ptr;

    SC_CTOR(Testbench)
        : design_ptr(nullptr)
    {
        SC_THREAD(stimulus);
    }

    void stimulus() {
        DualRail4 zero = encode_unsigned_to_balanced_ternary_4(0);

        A_a.write(zero.rail_a);
        A_b.write(zero.rail_b);
        B_a.write(zero.rail_a);
        B_b.write(zero.rail_b);

        wait(20, SC_NS);

        design_ptr->reset_counters();

        for (unsigned int a_value = 0; a_value < 16; a_value++) {
            for (unsigned int b_value = 0; b_value < 16; b_value++) {
                apply_test(a_value, b_value);
            }
        }

        design_ptr->print_report();

        sc_stop();
    }

    void apply_test(unsigned int a_value, unsigned int b_value) {
        DualRail4 encoded_a = encode_unsigned_to_balanced_ternary_4(a_value);
        DualRail4 encoded_b = encode_unsigned_to_balanced_ternary_4(b_value);

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
    sc_signal<sc_lv<4>> A_a, A_b, B_a, B_b;
    sc_signal<sc_lv<5>> S_minus, S_plus;

    BalancedTernaryAdder adder("BalancedTernaryAdder");
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