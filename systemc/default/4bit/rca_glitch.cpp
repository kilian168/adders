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
// 1. GENERIC GATE-LEVEL PRIMITIVE
//    Each instance is exactly one 2-input AND/OR/XOR gate.
//    It is sensitive to each of its two inputs independently, so
//    inputs arriving at different simulated times each trigger their
//    own evaluation (and are counted as separate switches if the
//    gate's output value changes), instead of being batched into one
//    combined evaluation. Output commit is delayed by 1ns from the
//    evaluation that caused the change, modeling real gate delay.
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
// 2. GATE-LEVEL FULL ADDER MODULE (5 gates)
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

    unsigned int total_switches() const {
        return g_xor1->switches + g_and1->switches + g_xor2->switches + g_and2->switches + g_or1->switches;
    }

    ~FullAdder() {
        delete g_xor1; delete g_and1; delete g_xor2; delete g_and2; delete g_or1;
    }
};

// ==========================================
// 3. 4-BIT RIPPLE CARRY ADDER
// ==========================================
SC_MODULE(RippleCarryAdder4) {
    sc_in<sc_lv<4>> A, B;
    sc_out<sc_lv<4>> Sum;
    sc_out<bool> Cout;

    sc_signal<bool> a[4], b[4], s[4], c[4];
    sc_signal<bool> const_zero;
    FullAdder* fa[4];

    SC_CTOR(RippleCarryAdder4) {
        for (int i = 0; i < 4; i++) {
            std::string name = "FA_" + std::to_string(i);
            fa[i] = new FullAdder(name.c_str());
            fa[i]->a(a[i]);
            fa[i]->b(b[i]);
            fa[i]->sum(s[i]);
            fa[i]->cout(c[i]);

            if (i == 0) {
                fa[i]->cin(const_zero);
            } else {
                fa[i]->cin(c[i-1]);
            }
        }

        SC_METHOD(split_inputs);
        sensitive << A << B;

        SC_METHOD(combine_outputs);
        sensitive << s[0] << s[1] << s[2] << s[3] << c[3];
    }

    void split_inputs() {
        sc_lv<4> val_A = A.read();
        sc_lv<4> val_B = B.read();

        for (int i = 0; i < 4; i++) {
            if (val_A[i].is_01() && val_B[i].is_01()) {
                a[i].write(val_A[i].to_bool());
                b[i].write(val_B[i].to_bool());
            } else {
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
            total_sw += fa[i]->total_switches();
        }
        std::cout << "RCA-4: GATES=20 SWITCHES=" << total_sw << " DELAY=8ns\n";
    }

    ~RippleCarryAdder4() {
        for (int i = 0; i < 4; i++) delete fa[i];
    }
};

// ==========================================
// 4. TESTBENCH
// ==========================================
SC_MODULE(Testbench) {
    sc_out<sc_lv<4>> A, B;
    sc_in<sc_lv<4>> Sum;
    sc_in<bool> Cout;

    RippleCarryAdder4* rca_ptr;

    unsigned int number_of_errors;

    unsigned int a_start;
    unsigned int a_end;

    SC_CTOR(Testbench)
        : rca_ptr(nullptr),
          number_of_errors(0),
          a_start(0),
          a_end(16)
    {
        SC_THREAD(stimulus);
    }

    void stimulus() {
        for (unsigned int a_value = a_start; a_value < a_end; a_value++) {
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
// 5. MAIN ENTRY
// ==========================================
int sc_main(int argc, char* argv[]) {
    sc_signal<sc_lv<4>> A, B, Sum;
    sc_signal<bool> Cout;

    RippleCarryAdder4 rca("RCA4");
    Testbench tb("TB");
    tb.rca_ptr = &rca;

    rca.A(A); rca.B(B); rca.Sum(Sum); rca.Cout(Cout);
    tb.A(A); tb.B(B); tb.Sum(Sum); tb.Cout(Cout);

    // Optional partitioning: "rca_4bit <partition_index> <num_partitions>"
    // restricts this process to a slice of the a-value range, so the
    // exhaustive sweep can be split across parallel processes.
    if (argc == 3) {
        unsigned int partition_index = static_cast<unsigned int>(std::stoul(argv[1]));
        unsigned int num_partitions = static_cast<unsigned int>(std::stoul(argv[2]));
        unsigned int slice = 16U / num_partitions;
        tb.a_start = partition_index * slice;
        tb.a_end = (partition_index == num_partitions - 1) ? 16U : (partition_index + 1) * slice;
    }

    sc_start();
    return 0;
}
