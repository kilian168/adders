// main.cpp
#include <systemc.h>
#include <iostream>
#include <iomanip>
#include <string>

// ==========================================
// Helper functions
// ==========================================
static sc_lv<8> make_8bit_vector(unsigned int value) {
    sc_lv<8> bits;
    for (int i = 0; i < 8; i++) {
        bits[i] = ((value >> i) & 1U) != 0U;
    }
    return bits;
}

static unsigned int to_unsigned_8bit(const sc_lv<8>& bits) {
    unsigned int value = 0;
    for (int i = 0; i < 8; i++) {
        if (bits[i].is_01() && bits[i].to_bool()) {
            value += (1U << i);
        }
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

    sc_signal<bool> s1;
    sc_signal<bool> c1;
    sc_signal<bool> c2;

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
        : xor1_old(false),
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
        SC_METHOD(process_tier1);
        dont_initialize();
        sensitive << a << b;

        SC_METHOD(process_tier2);
        dont_initialize();
        sensitive << s1 << cin;

        SC_METHOD(process_tier3);
        dont_initialize();
        sensitive << c1 << c2;
    }

    void process_tier1() {
        bool value_a = a.read();
        bool value_b = b.read();

        xor1_calls++;
        bool next_xor1 = value_a ^ value_b;
        if (next_xor1 != xor1_old) {
            xor1_old = next_xor1;
            xor1_switches++;
            s1.write(next_xor1);
        }

        and1_calls++;
        bool next_and1 = value_a & value_b;
        if (next_and1 != and1_old) {
            and1_old = next_and1;
            and1_switches++;
            c1.write(next_and1);
        }
    }

    void process_tier2() {
        bool value_s1 = s1.read();
        bool value_cin = cin.read();

        xor2_calls++;
        bool next_xor2 = value_s1 ^ value_cin;
        if (next_xor2 != xor2_old) {
            xor2_old = next_xor2;
            xor2_switches++;
            sum.write(next_xor2);
        }

        and2_calls++;
        bool next_and2 = value_s1 & value_cin;
        if (next_and2 != and2_old) {
            and2_old = next_and2;
            and2_switches++;
            c2.write(next_and2);
        }
    }

    void process_tier3() {
        bool value_c1 = c1.read();
        bool value_c2 = c2.read();

        or1_calls++;
        bool next_or1 = value_c1 | value_c2;
        if (next_or1 != or1_old) {
            or1_old = next_or1;
            or1_switches++;
            cout.write(next_or1);
        }
    }
};

// ==========================================
// 2. 8-BIT RIPPLE CARRY ADDER
// ==========================================
SC_MODULE(RippleCarryAdder8) {
    sc_in<sc_lv<8>> A;
    sc_in<sc_lv<8>> B;

    sc_out<sc_lv<8>> Sum;
    sc_out<bool> Cout;

    sc_signal<bool> a[8];
    sc_signal<bool> b[8];
    sc_signal<bool> s[8];
    sc_signal<bool> c[8];
    sc_signal<bool> const_zero;

    FullAdder* fa[8];

    SC_CTOR(RippleCarryAdder8) {
        for (int i = 0; i < 8; i++) {
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
        sensitive << s[0] << s[1] << s[2] << s[3]
                  << s[4] << s[5] << s[6] << s[7]
                  << c[7];
    }

    void split_inputs() {
        sc_lv<8> value_a = A.read();
        sc_lv<8> value_b = B.read();

        for (int i = 0; i < 8; i++) {
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
        sc_lv<8> value_sum;

        for (int i = 0; i < 8; i++) {
            value_sum[i] = s[i].read();
        }

        Sum.write(value_sum);
        Cout.write(c[7].read());
    }

    void print_report() {
        unsigned int total_switches = 0;

        std::cout << "\n=======================================================\n";
        std::cout << "          RIPPLE CARRY ADDER 8-BIT SWITCH REPORT       \n";
        std::cout << "=======================================================\n";

        for (int i = 0; i < 8; i++) {
            unsigned int block_switches =
                fa[i]->xor1_switches +
                fa[i]->xor2_switches +
                fa[i]->and1_switches +
                fa[i]->and2_switches +
                fa[i]->or1_switches;

            std::string name = "FA" + std::to_string(i + 1);

            std::cout << " " << name
                      << " | XOR1 SW: " << std::setw(3) << fa[i]->xor1_switches
                      << " | XOR2 SW: " << std::setw(3) << fa[i]->xor2_switches
                      << " | AND1 SW: " << std::setw(3) << fa[i]->and1_switches
                      << " | AND2 SW: " << std::setw(3) << fa[i]->and2_switches
                      << " | OR1 SW: " << std::setw(3) << fa[i]->or1_switches
                      << " | TOTAL: " << std::setw(3) << block_switches
                      << "\n";

            total_switches += block_switches;
        }

        std::cout << "-------------------------------------------------------\n";
        std::cout << " GESAMTSUMME ALLER SWITCHES IM RIPPLE CARRY ADDER: "
                  << total_switches << "\n";
        std::cout << "=======================================================\n";
    }

    ~RippleCarryAdder8() {
        for (int i = 0; i < 8; i++) {
            delete fa[i];
        }
    }
};

// ==========================================
// 3. TESTBENCH
// ==========================================
SC_MODULE(Testbench) {
    sc_out<sc_lv<8>> A;
    sc_out<sc_lv<8>> B;

    sc_in<sc_lv<8>> Sum;
    sc_in<bool> Cout;

    RippleCarryAdder8* rca_ptr;

    unsigned int number_of_additions;
    unsigned int accumulated_expected_results;
    unsigned int accumulated_actual_results;
    unsigned int number_of_errors;

    SC_CTOR(Testbench)
        : rca_ptr(nullptr),
          number_of_additions(0),
          accumulated_expected_results(0),
          accumulated_actual_results(0),
          number_of_errors(0)
    {
        SC_THREAD(stimulus);
    }

    void stimulus() {
        std::cout << "\n=======================================================\n";
        std::cout << "          EXHAUSTIVE 8-BIT RIPPLE CARRY TEST           \n";
        std::cout << "=======================================================\n";

        for (unsigned int a_value = 0; a_value < 256; a_value++) {
            for (unsigned int b_value = 0; b_value < 256; b_value++) {
                apply_test(a_value, b_value);
            }
        }

        std::cout << "-------------------------------------------------------\n";
        std::cout << " Anzahl getesteter 8-Bit-Additionen: "
                  << number_of_additions << "\n";
        std::cout << " Summe aller erwarteten Ergebniswerte: "
                  << accumulated_expected_results << "\n";
        std::cout << " Summe aller tatsächlichen Ergebniswerte: "
                  << accumulated_actual_results << "\n";
        std::cout << " Anzahl Fehler: "
                  << number_of_errors << "\n";
        std::cout << "=======================================================\n";

        rca_ptr->print_report();

        sc_stop();
    }

    void apply_test(unsigned int a_value, unsigned int b_value) {
        A.write(make_8bit_vector(a_value));
        B.write(make_8bit_vector(b_value));

        wait(10, SC_NS);

        unsigned int expected_result = a_value + b_value;
        unsigned int actual_sum = to_unsigned_8bit(Sum.read());
        unsigned int actual_result = actual_sum + (Cout.read() ? 256U : 0U);

        bool is_correct = expected_result == actual_result;

        number_of_additions++;
        accumulated_expected_results += expected_result;
        accumulated_actual_results += actual_result;

        if (!is_correct) {
            number_of_errors++;
        }
    }
};

// ==========================================
// 4. MAIN ENTRY POINT
// ==========================================
int sc_main(int argc, char* argv[]) {
    sc_signal<sc_lv<8>> A;
    sc_signal<sc_lv<8>> B;

    sc_signal<sc_lv<8>> Sum;
    sc_signal<bool> Cout;

    RippleCarryAdder8 rca("RCA8");
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