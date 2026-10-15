// ter_glitch.cpp -- 4-digit balanced ternary adder
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
// Clean 3-input OR gate primitive, purely combinational.
// ==========================================
SC_MODULE(Gate3Or) {
    sc_in<bool> in1, in2, in3;
    sc_out<bool> out;

    SC_CTOR(Gate3Or) {
        SC_METHOD(logic);
        sensitive << in1 << in2 << in3;
    }

    void logic() {
        out = in1.read() | in2.read() | in3.read();
    }
};

// ==========================================
// GATE-LEVEL BINARY FULL ADDER MODULE (6 gates)
//    xor1 = XOR(a,b)        and1 = AND(a,b)
//    and2 = AND(a,c)        and3 = AND(b,c)
//    sum  = XOR(xor1,c)     carry = OR3(and1,and2,and3)
// ==========================================
SC_MODULE(BsdBinaryFullAdder) {
    sc_in<bool> input_a;
    sc_in<bool> input_b;
    sc_in<bool> input_c;

    sc_out<bool> sum_out;
    sc_out<bool> carry_out;

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

    ~BsdBinaryFullAdder() {
        delete g_xor1; delete g_and1; delete g_and2;
        delete g_and3; delete g_xor2; delete g_or1;
    }
};

// ==========================================
// 4-digit balanced-ternary adder, dual-rail (a/b) encoding on both
// operands and both outputs. Two FA stages per digit: stage1 reduces
// (A_a,B_a,A_b); stage2 combines stage1's sum with B_b and the PREVIOUS
// digit's stage1 carry -- this one-step-only cross-digit dependency is
// what keeps the design's depth constant instead of rippling across N.
// ==========================================
SC_MODULE(BalancedTernaryAdder4) {
    sc_in<sc_uint<4>> A_a, A_b, B_a, B_b;
    sc_out<sc_uint<5>> S_minus, S_plus;

    sc_signal<bool> sa_a[4], sa_b[4], sb_a[4], sb_b[4];
    sc_signal<bool> stage1_sum[4], stage1_carry[4];
    sc_signal<bool> stage2_sum[4], stage2_carry[4];
    sc_signal<bool> const_one;

    BsdBinaryFullAdder* stage1_adders[4];
    BsdBinaryFullAdder* stage2_adders[4];

    SC_CTOR(BalancedTernaryAdder4) {
        const_one = true;

        for (int i = 0; i < 4; i++) {
            stage1_adders[i] = new BsdBinaryFullAdder(("S1_FA" + std::to_string(i + 1)).c_str());
            stage1_adders[i]->input_a(sa_a[i]);
            stage1_adders[i]->input_b(sb_a[i]);
            stage1_adders[i]->input_c(sa_b[i]);
            stage1_adders[i]->sum_out(stage1_sum[i]);
            stage1_adders[i]->carry_out(stage1_carry[i]);

            stage2_adders[i] = new BsdBinaryFullAdder(("S2_FA" + std::to_string(i + 1)).c_str());
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
        sensitive << A_a << A_b << B_a << B_b;

        SC_METHOD(pack_outputs);
        for (int i = 0; i < 4; i++) {
            sensitive << stage2_sum[i] << stage2_carry[i];
        }
        sensitive << stage1_carry[4 - 1];
    }

    void unpack_inputs() {
        sc_uint<4> va = A_a.read(), vb = A_b.read(), vc = B_a.read(), vd = B_b.read();
        for (int i = 0; i < 4; i++) {
            sa_a[i] = va[i];
            sa_b[i] = vb[i];
            sb_a[i] = vc[i];
            sb_b[i] = vd[i];
        }
    }

    void pack_outputs() {
        sc_uint<5> output_minus, output_plus;
        output_minus[0] = false;   // no carry-in below the least significant digit
        for (int i = 0; i < 4; i++) {
            output_plus[i] = stage2_sum[i].read();
            output_minus[i + 1] = stage2_carry[i].read();
        }
        output_plus[4] = stage1_carry[4 - 1].read();
        S_minus = output_minus;
        S_plus = output_plus;
    }

    ~BalancedTernaryAdder4() {
        for (int i = 0; i < 4; i++) {
            delete stage1_adders[i];
            delete stage2_adders[i];
        }
    }
};

// ==========================================
// Wrapper: instantiates the DUT and binds every port to a plain signal.
// ICSC requires this -- the DUT itself is never the top module directly.
// ==========================================
SC_MODULE(Tb) {
    sc_signal<sc_uint<4>> A_a;
    sc_signal<sc_uint<4>> A_b;
    sc_signal<sc_uint<4>> B_a;
    sc_signal<sc_uint<4>> B_b;
    sc_signal<sc_uint<5>> S_minus;
    sc_signal<sc_uint<5>> S_plus;

    BalancedTernaryAdder4 dut_inst{"dut_inst"};

    SC_CTOR(Tb) {
        dut_inst.A_a(A_a);
        dut_inst.A_b(A_b);
        dut_inst.B_a(B_a);
        dut_inst.B_b(B_b);
        dut_inst.S_minus(S_minus);
        dut_inst.S_plus(S_plus);
    }
};

int sc_main(int argc, char** argv) {
    Tb tb("tb");
    sc_start();
    return 0;
}
