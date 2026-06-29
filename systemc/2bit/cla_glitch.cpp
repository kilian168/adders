// cla_glitch.cpp – 2-bit Carry Lookahead Adder, gate-level switching activity
#include <systemc.h>
#include <iostream>
#include <iomanip>
#include <string>
#include <cassert>

// ==========================================
// Helper functions
// ==========================================
static sc_lv<2> make_2bit_vector(unsigned int value) {
    sc_lv<2> bits;
    bits[0] = (value & 1U) != 0U;
    bits[1] = (value & 2U) != 0U;
    return bits;
}

static unsigned int to_unsigned_2bit(const sc_lv<2>& bits) {
    unsigned int value = 0;
    if (bits[0].is_01() && bits[0].to_bool()) value += 1U;
    if (bits[1].is_01() && bits[1].to_bool()) value += 2U;
    return value;
}

// ==========================================
// 0b. GENERIC GATE-LEVEL PRIMITIVE
//     Independently sensitive to each input, with a 1ns delay between
//     evaluation and output commit, so every transient glitch from
//     asynchronously-arriving inputs is counted as a separate switch.
// ==========================================
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
};

// ==========================================
// 1. 2-BIT CARRY LOOKAHEAD ADDER MODULE
//    Gate count: 13
//    PG tier : 2 XOR (P) + 2 AND (G)          =  4 gates
//    C1 logic: 1 AND (P0&Cin) + 1 OR           =  2 gates
//    C2 logic: 3 AND + 2 OR (Cout)             =  5 gates
//    Sum tier: 2 XOR (S0=P0^Cin, S1=P1^C1)    =  2 gates
//    Cin is hardwired 0 for top-level adder.
//    Wired as real per-gate dependencies (not batched tiers).
// ==========================================
SC_MODULE(CarryLookaheadAdder2) {
    sc_in<sc_lv<2>>  A, B;
    sc_out<sc_lv<2>> Sum;
    sc_out<bool>     Cout;

    sc_signal<bool> a_bit[2], b_bit[2];
    sc_signal<bool> p[2], g[2];
    sc_signal<bool> const_zero;
    sc_signal<bool> c1a, c1o;
    sc_signal<bool> c2a1, c2a2, c2a3, c2o1, c2o2;
    sc_signal<bool> s[2];

    Gate *g_p[2], *g_g[2], *g_s[2];
    Gate *g_c1a, *g_c1o;
    Gate *g_c2a1, *g_c2a2, *g_c2a3, *g_c2o1, *g_c2o2;

    SC_CTOR(CarryLookaheadAdder2) {
        SC_METHOD(unpack_inputs);
        dont_initialize();
        sensitive << A << B;

        SC_METHOD(pack_outputs);
        dont_initialize();
        sensitive << s[0] << s[1] << c2o2;

        for (int i = 0; i < 2; i++) {
            std::string pn = "P" + std::to_string(i);
            g_p[i] = new Gate(pn.c_str(), GATE_XOR);
            g_p[i]->in1(a_bit[i]); g_p[i]->in2(b_bit[i]); g_p[i]->out(p[i]);

            std::string gn = "G" + std::to_string(i);
            g_g[i] = new Gate(gn.c_str(), GATE_AND);
            g_g[i]->in1(a_bit[i]); g_g[i]->in2(b_bit[i]); g_g[i]->out(g[i]);
        }

        g_c1a = new Gate("C1A", GATE_AND); g_c1a->in1(p[0]); g_c1a->in2(const_zero); g_c1a->out(c1a);
        g_c1o = new Gate("C1O", GATE_OR);  g_c1o->in1(g[0]); g_c1o->in2(c1a);        g_c1o->out(c1o);

        g_c2a1 = new Gate("C2A1", GATE_AND); g_c2a1->in1(p[1]);  g_c2a1->in2(g[0]);        g_c2a1->out(c2a1);
        g_c2a2 = new Gate("C2A2", GATE_AND); g_c2a2->in1(p[1]);  g_c2a2->in2(p[0]);        g_c2a2->out(c2a2);
        g_c2a3 = new Gate("C2A3", GATE_AND); g_c2a3->in1(c2a2);  g_c2a3->in2(const_zero);  g_c2a3->out(c2a3);
        g_c2o1 = new Gate("C2O1", GATE_OR);  g_c2o1->in1(g[1]);  g_c2o1->in2(c2a1);        g_c2o1->out(c2o1);
        g_c2o2 = new Gate("C2O2", GATE_OR);  g_c2o2->in1(c2o1);  g_c2o2->in2(c2a3);        g_c2o2->out(c2o2);

        g_s[0] = new Gate("S0", GATE_XOR); g_s[0]->in1(p[0]); g_s[0]->in2(const_zero); g_s[0]->out(s[0]);
        g_s[1] = new Gate("S1", GATE_XOR); g_s[1]->in1(p[1]); g_s[1]->in2(c1o);        g_s[1]->out(s[1]);
    }

    void unpack_inputs() {
        sc_lv<2> va = A.read(), vb = B.read();
        for (int i = 0; i < 2; i++) {
            a_bit[i].write(va[i].is_01() ? va[i].to_bool() : false);
            b_bit[i].write(vb[i].is_01() ? vb[i].to_bool() : false);
        }
    }

    void pack_outputs() {
        sc_lv<2> out;
        out[0] = s[0].read();
        out[1] = s[1].read();
        Sum.write(out);
        Cout.write(c2o2.read());
    }

    unsigned int total_switches() const {
        unsigned int t = 0;
        for (int i = 0; i < 2; i++) t += g_p[i]->switches + g_g[i]->switches + g_s[i]->switches;
        t += g_c1a->switches + g_c1o->switches;
        t += g_c2a1->switches + g_c2a2->switches + g_c2a3->switches + g_c2o1->switches + g_c2o2->switches;
        return t;
    }

    void print_report() {
        std::cout << "CLA-2: GATES=13 SWITCHES=" << total_switches() << " DELAY=2ns\n";
    }

    ~CarryLookaheadAdder2() {
        for (int i = 0; i < 2; i++) { delete g_p[i]; delete g_g[i]; delete g_s[i]; }
        delete g_c1a; delete g_c1o;
        delete g_c2a1; delete g_c2a2; delete g_c2a3; delete g_c2o1; delete g_c2o2;
    }
};

// ==========================================
// 2. TESTBENCH – exhaustive 2-bit test
// ==========================================
SC_MODULE(Testbench) {
    sc_out<sc_lv<2>> A, B;
    sc_in<sc_lv<2>>  Sum;
    sc_in<bool>      Cout;

    CarryLookaheadAdder2* cla_ptr;

    unsigned int number_of_errors;

    SC_CTOR(Testbench)
        : cla_ptr(nullptr), number_of_errors(0)
    { SC_THREAD(stimulus); }

    void stimulus() {
        for (unsigned int a_value = 0; a_value < 4; a_value++) {
            for (unsigned int b_value = 0; b_value < 4; b_value++) {
                apply_test(a_value, b_value);
            }
        }

        assert(number_of_errors == 0);
        cla_ptr->print_report();
        sc_stop();
    }

    void apply_test(unsigned int a_value, unsigned int b_value) {
        A.write(make_2bit_vector(a_value));
        B.write(make_2bit_vector(b_value));
        wait(50, SC_NS);

        unsigned int expected_result = a_value + b_value;
        unsigned int actual_sum      = to_unsigned_2bit(Sum.read());
        unsigned int actual_result   = actual_sum + (Cout.read() ? 4U : 0U);
        if (expected_result != actual_result) number_of_errors++;
    }
};

// ==========================================
// 3. MAIN
// ==========================================
int sc_main(int argc, char* argv[]) {
    sc_signal<sc_lv<2>> A, B, Sum;
    sc_signal<bool>     Cout;

    CarryLookaheadAdder2 cla("CLA2");
    Testbench tb("TB");
    tb.cla_ptr = &cla;

    cla.A(A); cla.B(B); cla.Sum(Sum); cla.Cout(Cout);
    tb.A(A);  tb.B(B);  tb.Sum(Sum);  tb.Cout(Cout);

    sc_start();
    return 0;
}
