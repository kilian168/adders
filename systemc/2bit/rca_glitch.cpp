// rca2_exhaustive.cpp
#include <systemc.h>
#include <iostream>
#include <iomanip>
#include <string>
#include <cassert>

// ==========================================
// Helper functions
// ==========================================
static sc_lv<2> make_two_bit_vector(unsigned int value) {
    sc_lv<2> bits;
    bits[0] = (value & 1U) != 0U;
    bits[1] = (value & 2U) != 0U;
    return bits;
}

static unsigned int to_unsigned_2bit(const sc_lv<2>& bits) {
    unsigned int value = 0;

    if (bits[0].is_01() && bits[0].to_bool()) {
        value += 1U;
    }

    if (bits[1].is_01() && bits[1].to_bool()) {
        value += 2U;
    }

    return value;
}

// ==========================================
// 1. GATE-LEVEL FULL ADDER MODULE
// ==========================================
SC_MODULE(FullAdder) {
    sc_in<bool> a;
    sc_in<bool> b;
    sc_in<bool> cin;

    sc_out<bool> sum;
    sc_out<bool> cout;

    sc_event ev_tier2;
    sc_event ev_tier3;

    bool s1_val;
    bool c1_val;
    bool c2_val;

    bool xor1_old;
    bool xor2_old;
    bool and1_old;
    bool and2_old;
    bool or1_old;

    unsigned int xor1_calls;
    unsigned int xor1_switches;
    unsigned int xor2_calls;
    unsigned int xor2_switches;
    unsigned int and1_calls;
    unsigned int and1_switches;
    unsigned int and2_calls;
    unsigned int and2_switches;
    unsigned int or1_calls;
    unsigned int or1_switches;

    SC_CTOR(FullAdder)
        : s1_val(false),
          c1_val(false),
          c2_val(false),
          xor1_old(false),
          xor2_old(false),
          and1_old(false),
          and2_old(false),
          or1_old(false),
          xor1_calls(0),
          xor1_switches(0),
          xor2_calls(0),
          xor2_switches(0),
          and1_calls(0),
          and1_switches(0),
          and2_calls(0),
          and2_switches(0),
          or1_calls(0),
          or1_switches(0)
    {
        // cin is included here (though unused below) purely so that an
        // upstream carry change re-enters this 1ns-per-tier delay chain,
        // matching the explicit gate-delay model used by the CLA/ternary adders.
        SC_METHOD(process_tier1);
        dont_initialize();
        sensitive << a << b << cin;

        SC_METHOD(process_tier2);
        dont_initialize();
        sensitive << ev_tier2;

        SC_METHOD(process_tier3);
        dont_initialize();
        sensitive << ev_tier3;
    }

    void process_tier1() {
        bool value_a = a.read();
        bool value_b = b.read();

        xor1_calls++;
        bool next_xor1 = value_a ^ value_b;
        if (next_xor1 != xor1_old) {
            xor1_old = next_xor1;
            xor1_switches++;
            s1_val = next_xor1;
        }

        and1_calls++;
        bool next_and1 = value_a & value_b;
        if (next_and1 != and1_old) {
            and1_old = next_and1;
            and1_switches++;
            c1_val = next_and1;
        }

        ev_tier2.notify(1, SC_NS);
    }

    void process_tier2() {
        bool value_cin = cin.read();

        xor2_calls++;
        bool next_xor2 = s1_val ^ value_cin;
        if (next_xor2 != xor2_old) {
            xor2_old = next_xor2;
            xor2_switches++;
            sum.write(next_xor2);
        }

        and2_calls++;
        bool next_and2 = s1_val & value_cin;
        if (next_and2 != and2_old) {
            and2_old = next_and2;
            and2_switches++;
            c2_val = next_and2;
        }

        ev_tier3.notify(1, SC_NS);
    }

    void process_tier3() {
        or1_calls++;
        bool next_or1 = c1_val | c2_val;
        if (next_or1 != or1_old) {
            or1_old = next_or1;
            or1_switches++;
            cout.write(next_or1);
        }
    }
};

// ==========================================
// 2. 2-BIT RIPPLE CARRY ADDER
// ==========================================
SC_MODULE(RippleCarryAdder2) {
    sc_in<sc_lv<2>> A;
    sc_in<sc_lv<2>> B;

    sc_out<sc_lv<2>> Sum;
    sc_out<bool> Cout;

    sc_signal<bool> a[2];
    sc_signal<bool> b[2];
    sc_signal<bool> s[2];
    sc_signal<bool> c[2];
    sc_signal<bool> const_zero;

    FullAdder* fa[2];

    SC_CTOR(RippleCarryAdder2) {
        for (int i = 0; i < 2; i++) {
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
        sensitive << s[0] << s[1] << c[1];
    }

    void split_inputs() {
        sc_lv<2> value_a = A.read();
        sc_lv<2> value_b = B.read();

        for (int i = 0; i < 2; i++) {
            a[i].write(value_a[i].is_01() ? value_a[i].to_bool() : false);
            b[i].write(value_b[i].is_01() ? value_b[i].to_bool() : false);
        }
    }

    void combine_outputs() {
        sc_lv<2> value_sum;

        for (int i = 0; i < 2; i++) {
            value_sum[i] = s[i].read();
        }

        Sum.write(value_sum);
        Cout.write(c[1].read());
    }

    void print_report() {
        unsigned int total_switches = 0;

        for (int i = 0; i < 2; i++) {
            total_switches +=
                fa[i]->xor1_switches +
                fa[i]->xor2_switches +
                fa[i]->and1_switches +
                fa[i]->and2_switches +
                fa[i]->or1_switches;
        }

        std::cout << "RCA-2: GATES=10 SWITCHES=" << total_switches << " DELAY=4ns\n";
    }

    ~RippleCarryAdder2() {
        for (int i = 0; i < 2; i++) {
            delete fa[i];
        }
    }
};

// ==========================================
// 3. TESTBENCH: exhaustive 2-bit test
// ==========================================
SC_MODULE(Testbench) {
    sc_out<sc_lv<2>> A;
    sc_out<sc_lv<2>> B;

    sc_in<sc_lv<2>> Sum;
    sc_in<bool> Cout;

    RippleCarryAdder2* rca_ptr;

    unsigned int number_of_errors;

    SC_CTOR(Testbench)
        : rca_ptr(nullptr),
          number_of_errors(0)
    {
        SC_THREAD(stimulus);
    }

    void stimulus() {
        for (unsigned int a_value = 0; a_value < 4; a_value++) {
            for (unsigned int b_value = 0; b_value < 4; b_value++) {
                apply_test(a_value, b_value);
            }
        }

        assert(number_of_errors == 0);
        rca_ptr->print_report();

        sc_stop();
    }

    void apply_test(unsigned int a_value, unsigned int b_value) {
        A.write(make_two_bit_vector(a_value));
        B.write(make_two_bit_vector(b_value));

        wait(200, SC_NS);

        unsigned int expected_result = a_value + b_value;
        unsigned int actual_sum = to_unsigned_2bit(Sum.read());
        unsigned int actual_result = actual_sum + (Cout.read() ? 4U : 0U);

        if (expected_result != actual_result) {
            number_of_errors++;
        }
    }
};

// ==========================================
// 4. MAIN
// ==========================================
int sc_main(int argc, char* argv[]) {
    sc_signal<sc_lv<2>> A;
    sc_signal<sc_lv<2>> B;

    sc_signal<sc_lv<2>> Sum;
    sc_signal<bool> Cout;

    RippleCarryAdder2 rca("RCA2");
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

    sc_start();

    return 0;
}
