// cla_glitch.cpp – 4-bit Carry Lookahead Adder, gate-level switching activity
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
    for (int i = 0; i < 4; i++) bits[i] = ((value >> i) & 1U) != 0U;
    return bits;
}

static unsigned int to_unsigned_4bit(const sc_lv<4>& bits) {
    unsigned int value = 0;
    for (int i = 0; i < 4; i++)
        if (bits[i].is_01() && bits[i].to_bool()) value += (1U << i);
    return value;
}

// ==========================================
// 1. 4-BIT CLA BLOCK MODULE (single lookahead block)
//    Gate count: 38
//    PG tier : 4 XOR + 4 AND              =  8 gates
//    Carry   : C1(2) + C2(5) + C3(8) + C4(11) = 26 gates
//    Sum tier: 4 XOR                      =  4 gates
// ==========================================
SC_MODULE(ClaBlock4) {
    sc_in<sc_lv<4>>  A, B;
    sc_in<bool>      Cin;
    sc_out<sc_lv<4>> Sum;
    sc_out<bool>     Cout;

    sc_event ev_carry, ev_sum;

    // Tier 1: PG
    bool p[4], g[4], p_old[4], g_old[4];
    unsigned int p_sw[4], g_sw[4];

    // Tier 2: Carry lookahead (26 gates)
    // C1 = G[0]|(P[0]&Cin)
    bool c1a, c1o, c1a_old, c1o_old;
    unsigned int c1a_sw, c1o_sw;
    // C2 = G[1]|(P[1]&G[0])|(P[1]&P[0]&Cin)
    bool c2a1, c2a2, c2a3, c2o1, c2o2;
    bool c2a1_old, c2a2_old, c2a3_old, c2o1_old, c2o2_old;
    unsigned int c2a1_sw, c2a2_sw, c2a3_sw, c2o1_sw, c2o2_sw;
    // C3 = G[2]|(P[2]&G[1])|(P[2]&P[1]&G[0])|(P[2]&P[1]&P[0]&Cin)
    bool c3a1, c3a2, c3a3, c3a4, c3a5, c3o1, c3o2, c3o3;
    bool c3a1_old, c3a2_old, c3a3_old, c3a4_old, c3a5_old, c3o1_old, c3o2_old, c3o3_old;
    unsigned int c3a1_sw, c3a2_sw, c3a3_sw, c3a4_sw, c3a5_sw, c3o1_sw, c3o2_sw, c3o3_sw;
    // C4(Cout) = G[3]|(P[3]&G[2])|(P[3]&P[2]&G[1])|(P[3]&P[2]&P[1]&G[0])|(P[3]&P[2]&P[1]&P[0]&Cin)
    bool c4a1, c4a2, c4a3, c4a4, c4a5, c4a6, c4a7;
    bool c4o1, c4o2, c4o3, c4o4;
    bool c4a1_old, c4a2_old, c4a3_old, c4a4_old, c4a5_old, c4a6_old, c4a7_old;
    bool c4o1_old, c4o2_old, c4o3_old, c4o4_old;
    unsigned int c4a1_sw, c4a2_sw, c4a3_sw, c4a4_sw, c4a5_sw, c4a6_sw, c4a7_sw;
    unsigned int c4o1_sw, c4o2_sw, c4o3_sw, c4o4_sw;

    // Tier 3: Sum (S[0]=P[0]^Cin, S[1]=P[1]^C1, S[2]=P[2]^C2, S[3]=P[3]^C3)
    bool s[4], s_old[4];
    unsigned int s_sw[4];

    SC_CTOR(ClaBlock4) {
        for (int i = 0; i < 4; i++) {
            p[i]=g[i]=p_old[i]=g_old[i]=s[i]=s_old[i]=false;
            p_sw[i]=g_sw[i]=s_sw[i]=0;
        }
        c1a=c1o=c1a_old=c1o_old=false; c1a_sw=c1o_sw=0;
        c2a1=c2a2=c2a3=c2o1=c2o2=false;
        c2a1_old=c2a2_old=c2a3_old=c2o1_old=c2o2_old=false;
        c2a1_sw=c2a2_sw=c2a3_sw=c2o1_sw=c2o2_sw=0;
        c3a1=c3a2=c3a3=c3a4=c3a5=c3o1=c3o2=c3o3=false;
        c3a1_old=c3a2_old=c3a3_old=c3a4_old=c3a5_old=c3o1_old=c3o2_old=c3o3_old=false;
        c3a1_sw=c3a2_sw=c3a3_sw=c3a4_sw=c3a5_sw=c3o1_sw=c3o2_sw=c3o3_sw=0;
        c4a1=c4a2=c4a3=c4a4=c4a5=c4a6=c4a7=false;
        c4o1=c4o2=c4o3=c4o4=false;
        c4a1_old=c4a2_old=c4a3_old=c4a4_old=c4a5_old=c4a6_old=c4a7_old=false;
        c4o1_old=c4o2_old=c4o3_old=c4o4_old=false;
        c4a1_sw=c4a2_sw=c4a3_sw=c4a4_sw=c4a5_sw=c4a6_sw=c4a7_sw=0;
        c4o1_sw=c4o2_sw=c4o3_sw=c4o4_sw=0;

        SC_METHOD(tier1_pg);    dont_initialize(); sensitive << A << B << Cin;
        SC_METHOD(tier2_carry); dont_initialize(); sensitive << ev_carry;
        SC_METHOD(tier3_sum);   dont_initialize(); sensitive << ev_sum;
    }

    void tier1_pg() {
        sc_lv<4> va = A.read(), vb = B.read();
        for (int i = 0; i < 4; i++) {
            bool ai = va[i].is_01() ? va[i].to_bool() : false;
            bool bi = vb[i].is_01() ? vb[i].to_bool() : false;
            bool np = ai ^ bi;
            if (np != p_old[i]) { p_old[i]=np; p[i]=np; p_sw[i]++; }
            bool ng = ai & bi;
            if (ng != g_old[i]) { g_old[i]=ng; g[i]=ng; g_sw[i]++; }
        }
        ev_carry.notify(1, SC_NS);
    }

    void tier2_carry() {
        bool cin = Cin.read();
        // C1
        bool nc1a=p[0]&cin;   if(nc1a!=c1a_old){c1a_old=nc1a;c1a=nc1a;c1a_sw++;}
        bool nc1o=g[0]|c1a;   if(nc1o!=c1o_old){c1o_old=nc1o;c1o=nc1o;c1o_sw++;}
        // C2
        bool nc2a1=p[1]&g[0]; if(nc2a1!=c2a1_old){c2a1_old=nc2a1;c2a1=nc2a1;c2a1_sw++;}
        bool nc2a2=p[1]&p[0]; if(nc2a2!=c2a2_old){c2a2_old=nc2a2;c2a2=nc2a2;c2a2_sw++;}
        bool nc2a3=c2a2&cin;  if(nc2a3!=c2a3_old){c2a3_old=nc2a3;c2a3=nc2a3;c2a3_sw++;}
        bool nc2o1=g[1]|c2a1; if(nc2o1!=c2o1_old){c2o1_old=nc2o1;c2o1=nc2o1;c2o1_sw++;}
        bool nc2o2=c2o1|c2a3; if(nc2o2!=c2o2_old){c2o2_old=nc2o2;c2o2=nc2o2;c2o2_sw++;}
        // C3
        bool nc3a1=p[2]&g[1]; if(nc3a1!=c3a1_old){c3a1_old=nc3a1;c3a1=nc3a1;c3a1_sw++;}
        bool nc3a2=p[2]&p[1]; if(nc3a2!=c3a2_old){c3a2_old=nc3a2;c3a2=nc3a2;c3a2_sw++;}
        bool nc3a3=c3a2&g[0]; if(nc3a3!=c3a3_old){c3a3_old=nc3a3;c3a3=nc3a3;c3a3_sw++;}
        bool nc3a4=c3a2&p[0]; if(nc3a4!=c3a4_old){c3a4_old=nc3a4;c3a4=nc3a4;c3a4_sw++;}
        bool nc3a5=c3a4&cin;  if(nc3a5!=c3a5_old){c3a5_old=nc3a5;c3a5=nc3a5;c3a5_sw++;}
        bool nc3o1=g[2]|c3a1; if(nc3o1!=c3o1_old){c3o1_old=nc3o1;c3o1=nc3o1;c3o1_sw++;}
        bool nc3o2=c3o1|c3a3; if(nc3o2!=c3o2_old){c3o2_old=nc3o2;c3o2=nc3o2;c3o2_sw++;}
        bool nc3o3=c3o2|c3a5; if(nc3o3!=c3o3_old){c3o3_old=nc3o3;c3o3=nc3o3;c3o3_sw++;}
        // C4 (Cout)
        bool nc4a1=p[3]&g[2]; if(nc4a1!=c4a1_old){c4a1_old=nc4a1;c4a1=nc4a1;c4a1_sw++;}
        bool nc4a2=p[3]&p[2]; if(nc4a2!=c4a2_old){c4a2_old=nc4a2;c4a2=nc4a2;c4a2_sw++;}
        bool nc4a3=c4a2&g[1]; if(nc4a3!=c4a3_old){c4a3_old=nc4a3;c4a3=nc4a3;c4a3_sw++;}
        bool nc4a4=c4a2&p[1]; if(nc4a4!=c4a4_old){c4a4_old=nc4a4;c4a4=nc4a4;c4a4_sw++;}
        bool nc4a5=c4a4&g[0]; if(nc4a5!=c4a5_old){c4a5_old=nc4a5;c4a5=nc4a5;c4a5_sw++;}
        bool nc4a6=c4a4&p[0]; if(nc4a6!=c4a6_old){c4a6_old=nc4a6;c4a6=nc4a6;c4a6_sw++;}
        bool nc4a7=c4a6&cin;  if(nc4a7!=c4a7_old){c4a7_old=nc4a7;c4a7=nc4a7;c4a7_sw++;}
        bool nc4o1=g[3]|c4a1; if(nc4o1!=c4o1_old){c4o1_old=nc4o1;c4o1=nc4o1;c4o1_sw++;}
        bool nc4o2=c4o1|c4a3; if(nc4o2!=c4o2_old){c4o2_old=nc4o2;c4o2=nc4o2;c4o2_sw++;}
        bool nc4o3=c4o2|c4a5; if(nc4o3!=c4o3_old){c4o3_old=nc4o3;c4o3=nc4o3;c4o3_sw++;}
        bool nc4o4=c4o3|c4a7; if(nc4o4!=c4o4_old){c4o4_old=nc4o4;c4o4=nc4o4;c4o4_sw++;}
        ev_sum.notify(1, SC_NS);
    }

    void tier3_sum() {
        bool cin = Cin.read();
        bool ns0=p[0]^cin;  if(ns0!=s_old[0]){s_old[0]=ns0;s[0]=ns0;s_sw[0]++;}
        bool ns1=p[1]^c1o;  if(ns1!=s_old[1]){s_old[1]=ns1;s[1]=ns1;s_sw[1]++;}
        bool ns2=p[2]^c2o2; if(ns2!=s_old[2]){s_old[2]=ns2;s[2]=ns2;s_sw[2]++;}
        bool ns3=p[3]^c3o3; if(ns3!=s_old[3]){s_old[3]=ns3;s[3]=ns3;s_sw[3]++;}
        sc_lv<4> out; for(int i=0;i<4;i++) out[i]=s[i];
        Sum.write(out);
        Cout.write(c4o4);
    }

    unsigned int total_block_switches() {
        unsigned int t = 0;
        for (int i = 0; i < 4; i++) t += p_sw[i] + g_sw[i] + s_sw[i];
        t += c1a_sw + c1o_sw;
        t += c2a1_sw + c2a2_sw + c2a3_sw + c2o1_sw + c2o2_sw;
        t += c3a1_sw + c3a2_sw + c3a3_sw + c3a4_sw + c3a5_sw + c3o1_sw + c3o2_sw + c3o3_sw;
        t += c4a1_sw + c4a2_sw + c4a3_sw + c4a4_sw + c4a5_sw + c4a6_sw + c4a7_sw;
        t += c4o1_sw + c4o2_sw + c4o3_sw + c4o4_sw;
        return t;
    }
};

// ==========================================
// 2. 4-BIT CARRY LOOKAHEAD ADDER (1 block)
// ==========================================
SC_MODULE(CarryLookaheadAdder4) {
    sc_in<sc_lv<4>>  A, B;
    sc_out<sc_lv<4>> Sum;
    sc_out<bool>     Cout;

    sc_signal<bool> const_zero;
    ClaBlock4* blk;

    SC_CTOR(CarryLookaheadAdder4) {
        blk = new ClaBlock4("BLK0");
        blk->A(A); blk->B(B); blk->Cin(const_zero); blk->Sum(Sum); blk->Cout(Cout);
    }

    void print_report() {
        unsigned int total = blk->total_block_switches();
        std::cout << "CLA-4: GATES=38 SWITCHES=" << total << " DELAY=2ns\n";
    }

    ~CarryLookaheadAdder4() { delete blk; }
};

// ==========================================
// 3. TESTBENCH – exhaustive 4-bit test
// ==========================================
SC_MODULE(Testbench) {
    sc_out<sc_lv<4>> A, B;
    sc_in<sc_lv<4>>  Sum;
    sc_in<bool>      Cout;

    CarryLookaheadAdder4* cla_ptr;

    unsigned int number_of_errors;

    SC_CTOR(Testbench)
        : cla_ptr(nullptr), number_of_errors(0)
    { SC_THREAD(stimulus); }

    void stimulus() {
        for (unsigned int a_value = 0; a_value < 16; a_value++) {
            for (unsigned int b_value = 0; b_value < 16; b_value++) {
                apply_test(a_value, b_value);
            }
        }

        assert(number_of_errors == 0);
        cla_ptr->print_report();
        sc_stop();
    }

    void apply_test(unsigned int a_value, unsigned int b_value) {
        A.write(make_4bit_vector(a_value));
        B.write(make_4bit_vector(b_value));
        wait(10, SC_NS);

        unsigned int expected_result = a_value + b_value;
        unsigned int actual_sum      = to_unsigned_4bit(Sum.read());
        unsigned int actual_result   = actual_sum + (Cout.read() ? 16U : 0U);
        if (expected_result != actual_result) number_of_errors++;
    }
};

// ==========================================
// 4. MAIN
// ==========================================
int sc_main(int argc, char* argv[]) {
    sc_signal<sc_lv<4>> A, B, Sum;
    sc_signal<bool>     Cout;

    CarryLookaheadAdder4 cla("CLA4");
    Testbench tb("TB");
    tb.cla_ptr = &cla;

    cla.A(A); cla.B(B); cla.Sum(Sum); cla.Cout(Cout);
    tb.A(A);  tb.B(B);  tb.Sum(Sum);  tb.Cout(Cout);

    sc_start();
    return 0;
}
