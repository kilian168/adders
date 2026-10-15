// sdtc_glitch_pm.cpp -- 4-bit SD/TC adder (PM)
// Architecture-only, synthesizable-subset SystemC for the Intel
// SystemC Compiler (ICSC, https://github.com/intel/systemc-compiler).
// Every individual AND/OR/XOR/NOT gate instance from the original
// switching-activity version is instantiated here too, by the same
// name and with the same wiring -- just as a plain combinational
// SC_METHOD (no sc_event, which ICSC's synthesizable subset forbids)
// and with no switch counting (not needed here). sc_uint<N> is used
// instead of sc_lv<N>, which ICSC also does not support.
#include <systemc.h>
#include <string>

// ==========================================
// Clean 2-input gate primitive (AND/OR/XOR), purely combinational.
// ==========================================
enum GateOp { GATE_AND, GATE_OR, GATE_XOR };

SC_MODULE(Gate) {
    sc_in<bool> in1, in2;
    sc_out<bool> out;
    GateOp op;

    SC_HAS_PROCESS(Gate);

    Gate(sc_module_name name, GateOp gate_op) : sc_module(name), op(gate_op) {
        SC_METHOD(logic);
        sensitive << in1 << in2;
    }

    void logic() {
        bool a = in1.read(), b = in2.read();
        if (op == GATE_AND)      out = a & b;
        else if (op == GATE_OR)  out = a | b;
        else /* GATE_XOR */      out = a ^ b;
    }
};

// ==========================================
// Clean 1-input NOT gate primitive, purely combinational.
// ==========================================
SC_MODULE(GateNot) {
    sc_in<bool> in1;
    sc_out<bool> out;

    SC_CTOR(GateNot) {
        SC_METHOD(logic);
        sensitive << in1;
    }

    void logic() {
        out = !in1.read();
    }
};

// ==========================================
// GATE-LEVEL PM DIGIT CELL (7 gates) -- digit value = SD_a(i) - SD_b(i)
// (plus-minus rails), combined with TC(i):
//    s1    = XOR(cin,b)          notB  = NOT(b)
//    sum   = XOR(s1,a)           term1 = AND(cin,notB)
//    notS1 = NOT(s1)             term2 = AND(notS1,a)
//    cout  = OR(term1,term2)
// Port mapping: a=TC(i), b=SD_b(i), cin=SD_a(i).
// ==========================================
SC_MODULE(FullAdder) {
    sc_in<bool> a, b, cin;
    sc_out<bool> sum, cout;

    sc_signal<bool> s1, notB, term1, notS1, term2;

    Gate *g_xor1, *g_and1, *g_and2, *g_or1, *g_xor2;
    GateNot *g_notB, *g_notS1;

    SC_CTOR(FullAdder) {
        g_xor1 = new Gate("XOR1", GATE_XOR);
        g_xor1->in1(cin); g_xor1->in2(b); g_xor1->out(s1);

        g_notB = new GateNot("NOTB");
        g_notB->in1(b); g_notB->out(notB);

        g_and1 = new Gate("AND1", GATE_AND);
        g_and1->in1(cin); g_and1->in2(notB); g_and1->out(term1);

        g_notS1 = new GateNot("NOTS1");
        g_notS1->in1(s1); g_notS1->out(notS1);

        g_and2 = new Gate("AND2", GATE_AND);
        g_and2->in1(notS1); g_and2->in2(a); g_and2->out(term2);

        g_or1 = new Gate("OR1", GATE_OR);
        g_or1->in1(term1); g_or1->in2(term2); g_or1->out(cout);

        g_xor2 = new Gate("XOR2", GATE_XOR);
        g_xor2->in1(s1); g_xor2->in2(a); g_xor2->out(sum);
    }

    ~FullAdder() {
        delete g_xor1; delete g_notB; delete g_and1; delete g_notS1;
        delete g_and2; delete g_or1; delete g_xor2;
    }
};

// ==========================================
// 4-bit signed-digit / two's-complement adder (PM).
// 4 independent digit cells, NO carry chain between digits (each cell
// only ever reads its own digit's TC/SD_b/SD_a bits). Sum_a(N) is the
// sign-correction top bit = NOT(TC(N-1)), via its own gate instance
// (NOT_TOP); Sum_b(0) is tied to 0.
// ==========================================
SC_MODULE(SdTcAdderPM4) {
    sc_in<sc_uint<4>> SD_a, SD_b, TC;
    sc_out<sc_uint<5>> Sum_a, Sum_b;

    sc_signal<bool> sda_bit[4], sdb_bit[4], tc_bit[4], sum_bit[4], carry_bit[4];
    sc_signal<bool> const_zero;
    sc_signal<bool> top_bit;
    FullAdder* fa[4];
    GateNot* g_not_top;

    SC_CTOR(SdTcAdderPM4) {
        for (int i = 0; i < 4; i++) {
            fa[i] = new FullAdder(("FA_" + std::to_string(i)).c_str());
            fa[i]->a(tc_bit[i]);
            fa[i]->b(sdb_bit[i]);
            fa[i]->cin(sda_bit[i]);
            fa[i]->sum(sum_bit[i]);
            fa[i]->cout(carry_bit[i]);
        }

        g_not_top = new GateNot("NOT_TOP");
        g_not_top->in1(tc_bit[4 - 1]);
        g_not_top->out(top_bit);

        SC_METHOD(split_inputs);
        sensitive << SD_a << SD_b << TC;

        SC_METHOD(combine_outputs);
        for (int i = 0; i < 4; i++) {
            sensitive << sum_bit[i] << carry_bit[i];
        }
        sensitive << top_bit;
    }

    void split_inputs() {
        sc_uint<4> va = SD_a.read(), vb = SD_b.read(), vt = TC.read();
        for (int i = 0; i < 4; i++) {
            sda_bit[i] = va[i];
            sdb_bit[i] = vb[i];
            tc_bit[i] = vt[i];
        }
    }

    void combine_outputs() {
        sc_uint<5> out_a, out_b;
        for (int i = 0; i < 4; i++) {
            out_a[i] = sum_bit[i].read();
            out_b[i + 1] = carry_bit[i].read();
        }
        out_a[4] = top_bit.read();
        out_b[0] = const_zero.read();
        Sum_a = out_a;
        Sum_b = out_b;
    }

    ~SdTcAdderPM4() {
        for (int i = 0; i < 4; i++) delete fa[i];
        delete g_not_top;
    }
};

// ==========================================
// Wrapper: instantiates the DUT and binds every port to a plain signal.
// ICSC requires this -- the DUT itself is never the top module directly.
// ==========================================
SC_MODULE(Tb) {
    sc_signal<sc_uint<4>> SD_a;
    sc_signal<sc_uint<4>> SD_b;
    sc_signal<sc_uint<4>> TC;
    sc_signal<sc_uint<5>> Sum_a;
    sc_signal<sc_uint<5>> Sum_b;

    SdTcAdderPM4 dut_inst{"dut_inst"};

    SC_CTOR(Tb) {
        dut_inst.SD_a(SD_a);
        dut_inst.SD_b(SD_b);
        dut_inst.TC(TC);
        dut_inst.Sum_a(Sum_a);
        dut_inst.Sum_b(Sum_b);
    }
};

int sc_main(int argc, char** argv) {
    Tb tb("tb");
    sc_start();
    return 0;
}
