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
            total_sw += blocks[i]->total_switches();
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