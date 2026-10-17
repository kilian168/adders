// rca_glitch.cpp -- 16-bit ripple-carry adder
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
// GATE-LEVEL FULL ADDER MODULE (5 gates)
//    s1   = XOR(a,b)        c1  = AND(a,b)
//    sum  = XOR(s1,cin)     c2  = AND(s1,cin)
//    cout = OR(c1,c2)
// ==========================================
SC_MODULE(FullAdder) {
    sc_in<bool> a, b, cin;
    sc_out<bool> sum, cout;

    sc_signal<bool> s1, c1, c2;

    Gate *g_xor1, *g_and1, *g_xor2, *g_and2, *g_or1;

    SC_CTOR(FullAdder) {
        g_xor1 = new Gate("XOR1", GATE_XOR);
        g_xor1->in1(a); g_xor1->in2(b); g_xor1->out(s1);

        g_and1 = new Gate("AND1", GATE_AND);
        g_and1->in1(a); g_and1->in2(b); g_and1->out(c1);

        g_xor2 = new Gate("XOR2", GATE_XOR);
        g_xor2->in1(s1); g_xor2->in2(cin); g_xor2->out(sum);

        g_and2 = new Gate("AND2", GATE_AND);
        g_and2->in1(s1); g_and2->in2(cin); g_and2->out(c2);

        g_or1 = new Gate("OR1", GATE_OR);
        g_or1->in1(c1); g_or1->in2(c2); g_or1->out(cout);
    }

    ~FullAdder() {
        delete g_xor1; delete g_and1; delete g_xor2; delete g_and2; delete g_or1;
    }
};

// ==========================================
// 16-bit ripple-carry adder: 16 full-adder cells chained through carry.
// ==========================================
SC_MODULE(RippleCarryAdder16) {
    sc_in<sc_uint<16>> A, B;
    sc_out<sc_uint<16>> Sum;
    sc_out<bool> Cout;

    sc_signal<bool> a_bit[16], b_bit[16], s_bit[16], c_bit[16];
    sc_signal<bool> const_zero;
    void drive_const_zero() { const_zero.write(false); }  // real tie-low driver (SystemC's implicit zero-init doesn't synthesize)
    FullAdder* fa[16];

    SC_CTOR(RippleCarryAdder16) {
        SC_METHOD(drive_const_zero);
        for (int i = 0; i < 16; i++) {
            fa[i] = new FullAdder(("FA_" + std::to_string(i)).c_str());
            fa[i]->a(a_bit[i]);
            fa[i]->b(b_bit[i]);
            fa[i]->cin(i == 0 ? const_zero : c_bit[i - 1]);
            fa[i]->sum(s_bit[i]);
            fa[i]->cout(c_bit[i]);
        }

        SC_METHOD(split_inputs);
        sensitive << A << B;

        SC_METHOD(combine_outputs);
        for (int i = 0; i < 16; i++) sensitive << s_bit[i];
        sensitive << c_bit[16 - 1];
    }

    void split_inputs() {
        sc_uint<16> a = A.read(), b = B.read();
        for (int i = 0; i < 16; i++) {
            a_bit[i] = a[i];
            b_bit[i] = b[i];
        }
    }

    void combine_outputs() {
        sc_uint<16> s;
        for (int i = 0; i < 16; i++) s[i] = s_bit[i].read();
        Sum = s;
        Cout = c_bit[16 - 1].read();
    }

    ~RippleCarryAdder16() {
        for (int i = 0; i < 16; i++) delete fa[i];
    }
};

// ==========================================
// Wrapper: instantiates the DUT and binds every port to a plain signal.
// ICSC requires this -- the DUT itself is never the top module directly.
// ==========================================
SC_MODULE(Tb) {
    sc_signal<sc_uint<16>> A;
    sc_signal<sc_uint<16>> B;
    sc_signal<sc_uint<16>> Sum;
    sc_signal<bool> Cout;

    RippleCarryAdder16 dut_inst{"dut_inst"};

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
