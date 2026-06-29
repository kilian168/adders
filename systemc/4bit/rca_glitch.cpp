#include <systemc.h>
#include <iostream>
#include <iomanip>
#include <string>
#include <cassert>

// ==========================================
// Helper functions
// ==========================================
static sc_lv<4> make_4bit_vector(unsigned int value) {
    sc_lv<4> bits;
    for (int i = 0; i < 4; i++) {
        bits[i] = ((value >> i) & 1U) != 0U;
    }
    return bits;
}

static unsigned int to_unsigned_4bit(const sc_lv<4>& bits) {
    unsigned int value = 0;
    for (int i = 0; i < 4; i++) {
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
    sc_in<bool> a, b, cin;
    sc_out<bool> sum, cout;

    sc_event ev_tier2, ev_tier3;

    bool s1_val, c1_val, c2_val;

    // Persistent state copies to detect changes
    bool xor1_old, xor2_old, and1_old, and2_old, or1_old;

    // Per-gate counters
    unsigned int xor1_calls, xor1_switches;
    unsigned int xor2_calls, xor2_switches;
    unsigned int and1_calls, and1_switches;
    unsigned int and2_calls, and2_switches;
    unsigned int or1_calls,  or1_switches;

    SC_CTOR(FullAdder) :
        s1_val(false), c1_val(false), c2_val(false),
        xor1_old(false), xor2_old(false), and1_old(false), and2_old(false), or1_old(false),
        xor1_calls(0), xor1_switches(0), xor2_calls(0), xor2_switches(0),
        and1_calls(0), and1_switches(0), and2_calls(0), and2_switches(0),
        or1_calls(0),  or1_switches(0)
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

    // Tier 1: Reacts to raw inputs (XOR1 and AND1)
    void process_tier1() {
        xor1_calls++;
        bool next_xor1 = a.read() ^ b.read();
        if (next_xor1 != xor1_old) {
            xor1_old = next_xor1;
            xor1_switches++;
            s1_val = next_xor1;
        }

        and1_calls++;
        bool next_and1 = a.read() & b.read();
        if (next_and1 != and1_old) {
            and1_old = next_and1;
            and1_switches++;
            c1_val = next_and1;
        }

        ev_tier2.notify(1, SC_NS);
    }

    // Tier 2: Reacts to Tier 1 outputs and Carry In (XOR2 and AND2)
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

    // Tier 3: Final Output Gate (OR1)
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
// 2. 4-BIT RIPPLE CARRY ADDER
// ==========================================
SC_MODULE(RippleCarryAdder4) {
    sc_in<sc_lv<4>> A, B;
    sc_out<sc_lv<4>> Sum;
    sc_out<bool> Cout;

    sc_signal<bool> a[4], b[4], s[4], c[4];
    sc_signal<bool> const_zero;
    FullAdder* fa[4]; // Array of pointers to track individual instances

    SC_CTOR(RippleCarryAdder4) {
        for (int i = 0; i < 4; i++) {
            std::string name = "FA_" + std::to_string(i);
            fa[i] = new FullAdder(name.c_str());
            fa[i]->a(a[i]);
            fa[i]->b(b[i]);
            fa[i]->sum(s[i]);
            fa[i]->cout(c[i]);

            // Explicitly separate port binding to avoid compiler confusion
            if (i == 0) {
                fa[i]->cin(const_zero); // Bind the first adder's carry-in to a fixed 0
            } else {
                fa[i]->cin(c[i-1]); // Bind subsequent adders to the previous internal carry signal
            }
        }

        SC_METHOD(split_inputs);
        sensitive << A << B;

        SC_METHOD(combine_outputs);
        sensitive << s[0] << s[1] << s[2] << s[3] << c[3];
    }

    void reset_counters() {
    	for (int i = 0; i < 4; i++) {
            fa[i]->xor1_calls = 0; fa[i]->xor1_switches = 0;
            fa[i]->xor2_calls = 0; fa[i]->xor2_switches = 0;
            fa[i]->and1_calls = 0; fa[i]->and1_switches = 0;
            fa[i]->and2_calls = 0; fa[i]->and2_switches = 0;
            fa[i]->or1_calls  = 0; fa[i]->or1_switches  = 0;
        }
    }


    void split_inputs() {
        sc_lv<4> val_A = A.read();
        sc_lv<4> val_B = B.read();

        // Loop through each bit
        for (int i = 0; i < 4; i++) {
            // Only read if the signal has a valid logic 0 or 1 value
            if (val_A[i].is_01() && val_B[i].is_01()) {
                a[i].write(val_A[i].to_bool());
                b[i].write(val_B[i].to_bool());
            } else {
                // Default fallback initialization to prevent 'X' conversion crash
                a[i].write(false);
                b[i].write(false);
            }
        }
    }


    void combine_outputs() {
        sc_lv<4> val_Sum;
        for (int i = 0; i < 4; i++) { val_Sum[i] = s[i].read(); }
        Sum.write(val_Sum);
        Cout.write(c[3].read());
    }

    void print_report() {
        unsigned int total_sw = 0;
        for (int i = 0; i < 4; i++) {
            total_sw += (fa[i]->xor1_switches + fa[i]->xor2_switches + fa[i]->and1_switches + fa[i]->and2_switches + fa[i]->or1_switches);
        }
        std::cout << "RCA-4: GATES=20 SWITCHES=" << total_sw << " DELAY=8ns\n";
    }


    ~RippleCarryAdder4() {
        for (int i = 0; i < 4; i++) delete fa[i];
    }
};

// ==========================================
// 3. TESTBENCH
// ==========================================
SC_MODULE(Testbench) {
    sc_out<sc_lv<4>> A, B;
    sc_in<sc_lv<4>> Sum;
    sc_in<bool> Cout;

    RippleCarryAdder4* rca_ptr; // Pointer to access printing function

    unsigned int number_of_errors;

    SC_CTOR(Testbench)
        : rca_ptr(nullptr),
          number_of_errors(0)
    {
        SC_THREAD(stimulus);
    }

    void stimulus() {
        for (unsigned int a_value = 0; a_value < 16; a_value++) {
            for (unsigned int b_value = 0; b_value < 16; b_value++) {
                apply_test(a_value, b_value);
            }
        }

        assert(number_of_errors == 0);
        rca_ptr->print_report();
        sc_stop();
    }


    void apply_test(unsigned int a_value, unsigned int b_value) {
        A.write(make_4bit_vector(a_value));
        B.write(make_4bit_vector(b_value));

        wait(200, SC_NS);

        unsigned int expected_result = a_value + b_value;
        unsigned int actual_sum = to_unsigned_4bit(Sum.read());
        unsigned int actual_result = actual_sum + (Cout.read() ? 16U : 0U);

        if (expected_result != actual_result) {
            number_of_errors++;
        }
    }
};

// ==========================================
// 4. MAIN ENTRY
// ==========================================
int sc_main(int argc, char* argv[]) {
    sc_signal<sc_lv<4>> A, B, Sum;
    sc_signal<bool> Cout;

    RippleCarryAdder4 rca("RCA4");
    Testbench tb("TB");
    tb.rca_ptr = &rca; // Pass pointer to testbench for reporting

    rca.A(A); rca.B(B); rca.Sum(Sum); rca.Cout(Cout);
    tb.A(A); tb.B(B); tb.Sum(Sum); tb.Cout(Cout);

    sc_start();
    return 0;
}
