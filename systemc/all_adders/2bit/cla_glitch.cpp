// cla_glitch.cpp -- 2-bit carry-lookahead adder (small direct lookahead)
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
#include <cstdio>

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
// 2-bit carry-lookahead adder: small enough that the original used a
// single direct lookahead stage (no block/tree hierarchy). Exact
// gate-for-gate port of the original CarryLookaheadAdder2.
// ==========================================
SC_MODULE(CarryLookaheadAdder2) {
    sc_in<sc_uint<2>>  A, B;
    sc_out<sc_uint<2>> Sum;
    sc_out<bool>       Cout;

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
        sensitive << A << B;

        SC_METHOD(pack_outputs);
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
        sc_uint<2> a = A.read(), b = B.read();
        for (int i = 0; i < 2; i++) { a_bit[i] = a[i]; b_bit[i] = b[i]; }
    }

    void pack_outputs() {
        sc_uint<2> out;
        out[0] = s[0].read();
        out[1] = s[1].read();
        Sum = out;
        Cout = c2o2.read();
    }

    ~CarryLookaheadAdder2() {
        for (int i = 0; i < 2; i++) { delete g_p[i]; delete g_g[i]; delete g_s[i]; }
        delete g_c1a; delete g_c1o;
        delete g_c2a1; delete g_c2a2; delete g_c2a3; delete g_c2o1; delete g_c2o2;
    }
};

// ==========================================
// Wrapper: instantiates the DUT and binds every port to a plain signal.
// ICSC requires this -- the DUT itself is never the top module directly.
// ==========================================
SC_MODULE(Tb) {
    sc_signal<sc_uint<2>> A;
    sc_signal<sc_uint<2>> B;
    sc_signal<sc_uint<2>> Sum;
    sc_signal<bool> Cout;

    CarryLookaheadAdder2 dut_inst{"dut_inst"};

    SC_CTOR(Tb) {
        dut_inst.A(A);
        dut_inst.B(B);
        dut_inst.Sum(Sum);
        dut_inst.Cout(Cout);
    }
};

int sc_main(int argc, char** argv) {
    Tb tb("tb");
    sc_start();
    return 0;
}
