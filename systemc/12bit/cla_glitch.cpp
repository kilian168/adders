// cla_glitch.cpp – 12-bit Carry Lookahead Adder, gate-level switching activity
// Monte Carlo: 1,000,000 random additions (exhaustive = 16,777,216 tests, impractical)
#include <systemc.h>
#include <iostream>
#include <iomanip>
#include <string>
#include <random>

// ==========================================
// Helper functions
// ==========================================
static sc_lv<12> make_12bit_vector(unsigned int value) {
    sc_lv<12> bits;
    for (int i = 0; i < 12; i++) bits[i] = ((value >> i) & 1U) != 0U;
    return bits;
}

static unsigned int to_unsigned_12bit(const sc_lv<12>& bits) {
    unsigned int value = 0;
    for (int i = 0; i < 12; i++)
        if (bits[i].is_01() && bits[i].to_bool()) value += (1U << i);
    return value;
}

// ==========================================
// 1. 4-BIT CLA BLOCK MODULE (reusable)
//    Gate count: 38 per block
// ==========================================
SC_MODULE(ClaBlock4) {
    sc_in<sc_lv<4>>  A, B;
    sc_in<bool>      Cin;
    sc_out<sc_lv<4>> Sum;
    sc_out<bool>     Cout;

    sc_event ev_carry, ev_sum;

    bool p[4], g[4], p_old[4], g_old[4];
    unsigned int p_sw[4], g_sw[4];

    bool c1a, c1o, c1a_old, c1o_old;
    unsigned int c1a_sw, c1o_sw;

    bool c2a1, c2a2, c2a3, c2o1, c2o2;
    bool c2a1_old, c2a2_old, c2a3_old, c2o1_old, c2o2_old;
    unsigned int c2a1_sw, c2a2_sw, c2a3_sw, c2o1_sw, c2o2_sw;

    bool c3a1, c3a2, c3a3, c3a4, c3a5, c3o1, c3o2, c3o3;
    bool c3a1_old, c3a2_old, c3a3_old, c3a4_old, c3a5_old, c3o1_old, c3o2_old, c3o3_old;
    unsigned int c3a1_sw, c3a2_sw, c3a3_sw, c3a4_sw, c3a5_sw, c3o1_sw, c3o2_sw, c3o3_sw;

    bool c4a1, c4a2, c4a3, c4a4, c4a5, c4a6, c4a7;
    bool c4o1, c4o2, c4o3, c4o4;
    bool c4a1_old, c4a2_old, c4a3_old, c4a4_old, c4a5_old, c4a6_old, c4a7_old;
    bool c4o1_old, c4o2_old, c4o3_old, c4o4_old;
    unsigned int c4a1_sw, c4a2_sw, c4a3_sw, c4a4_sw, c4a5_sw, c4a6_sw, c4a7_sw;
    unsigned int c4o1_sw, c4o2_sw, c4o3_sw, c4o4_sw;

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
        sc_lv<4> va=A.read(), vb=B.read();
        for (int i=0; i<4; i++) {
            bool ai=va[i].is_01()?va[i].to_bool():false;
            bool bi=vb[i].is_01()?vb[i].to_bool():false;
            bool np=ai^bi; if(np!=p_old[i]){p_old[i]=np;p[i]=np;p_sw[i]++;}
            bool ng=ai&bi; if(ng!=g_old[i]){g_old[i]=ng;g[i]=ng;g_sw[i]++;}
        }
        ev_carry.notify(1,SC_NS);
    }

    void tier2_carry() {
        bool cin=Cin.read();
        bool nc1a=p[0]&cin;   if(nc1a!=c1a_old){c1a_old=nc1a;c1a=nc1a;c1a_sw++;}
        bool nc1o=g[0]|c1a;   if(nc1o!=c1o_old){c1o_old=nc1o;c1o=nc1o;c1o_sw++;}
        bool nc2a1=p[1]&g[0]; if(nc2a1!=c2a1_old){c2a1_old=nc2a1;c2a1=nc2a1;c2a1_sw++;}
        bool nc2a2=p[1]&p[0]; if(nc2a2!=c2a2_old){c2a2_old=nc2a2;c2a2=nc2a2;c2a2_sw++;}
        bool nc2a3=c2a2&cin;  if(nc2a3!=c2a3_old){c2a3_old=nc2a3;c2a3=nc2a3;c2a3_sw++;}
        bool nc2o1=g[1]|c2a1; if(nc2o1!=c2o1_old){c2o1_old=nc2o1;c2o1=nc2o1;c2o1_sw++;}
        bool nc2o2=c2o1|c2a3; if(nc2o2!=c2o2_old){c2o2_old=nc2o2;c2o2=nc2o2;c2o2_sw++;}
        bool nc3a1=p[2]&g[1]; if(nc3a1!=c3a1_old){c3a1_old=nc3a1;c3a1=nc3a1;c3a1_sw++;}
        bool nc3a2=p[2]&p[1]; if(nc3a2!=c3a2_old){c3a2_old=nc3a2;c3a2=nc3a2;c3a2_sw++;}
        bool nc3a3=c3a2&g[0]; if(nc3a3!=c3a3_old){c3a3_old=nc3a3;c3a3=nc3a3;c3a3_sw++;}
        bool nc3a4=c3a2&p[0]; if(nc3a4!=c3a4_old){c3a4_old=nc3a4;c3a4=nc3a4;c3a4_sw++;}
        bool nc3a5=c3a4&cin;  if(nc3a5!=c3a5_old){c3a5_old=nc3a5;c3a5=nc3a5;c3a5_sw++;}
        bool nc3o1=g[2]|c3a1; if(nc3o1!=c3o1_old){c3o1_old=nc3o1;c3o1=nc3o1;c3o1_sw++;}
        bool nc3o2=c3o1|c3a3; if(nc3o2!=c3o2_old){c3o2_old=nc3o2;c3o2=nc3o2;c3o2_sw++;}
        bool nc3o3=c3o2|c3a5; if(nc3o3!=c3o3_old){c3o3_old=nc3o3;c3o3=nc3o3;c3o3_sw++;}
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
        ev_sum.notify(1,SC_NS);
    }

    void tier3_sum() {
        bool cin=Cin.read();
        bool ns0=p[0]^cin;  if(ns0!=s_old[0]){s_old[0]=ns0;s[0]=ns0;s_sw[0]++;}
        bool ns1=p[1]^c1o;  if(ns1!=s_old[1]){s_old[1]=ns1;s[1]=ns1;s_sw[1]++;}
        bool ns2=p[2]^c2o2; if(ns2!=s_old[2]){s_old[2]=ns2;s[2]=ns2;s_sw[2]++;}
        bool ns3=p[3]^c3o3; if(ns3!=s_old[3]){s_old[3]=ns3;s[3]=ns3;s_sw[3]++;}
        sc_lv<4> out; for(int i=0;i<4;i++) out[i]=s[i];
        Sum.write(out); Cout.write(c4o4);
    }

    unsigned int total_block_switches() {
        unsigned int t=0;
        for(int i=0;i<4;i++) t+=p_sw[i]+g_sw[i]+s_sw[i];
        t+=c1a_sw+c1o_sw;
        t+=c2a1_sw+c2a2_sw+c2a3_sw+c2o1_sw+c2o2_sw;
        t+=c3a1_sw+c3a2_sw+c3a3_sw+c3a4_sw+c3a5_sw+c3o1_sw+c3o2_sw+c3o3_sw;
        t+=c4a1_sw+c4a2_sw+c4a3_sw+c4a4_sw+c4a5_sw+c4a6_sw+c4a7_sw;
        t+=c4o1_sw+c4o2_sw+c4o3_sw+c4o4_sw;
        return t;
    }
};

// ==========================================
// 2. 12-BIT CARRY LOOKAHEAD ADDER (3 x 4-bit blocks)
//    Total gates: 3 x 38 = 114
// ==========================================
SC_MODULE(CarryLookaheadAdder12) {
    sc_in<sc_lv<12>>  A, B;
    sc_out<sc_lv<12>> Sum;
    sc_out<bool>      Cout;

    sc_signal<sc_lv<4>> a_slice[3], b_slice[3], s_slice[3];
    sc_signal<bool>     c_between[2];
    sc_signal<bool>     const_zero;

    ClaBlock4* blk[3];

    SC_CTOR(CarryLookaheadAdder12) {
        blk[0] = new ClaBlock4("BLK0");
        blk[0]->A(a_slice[0]); blk[0]->B(b_slice[0]);
        blk[0]->Cin(const_zero); blk[0]->Sum(s_slice[0]); blk[0]->Cout(c_between[0]);

        blk[1] = new ClaBlock4("BLK1");
        blk[1]->A(a_slice[1]); blk[1]->B(b_slice[1]);
        blk[1]->Cin(c_between[0]); blk[1]->Sum(s_slice[1]); blk[1]->Cout(c_between[1]);

        blk[2] = new ClaBlock4("BLK2");
        blk[2]->A(a_slice[2]); blk[2]->B(b_slice[2]);
        blk[2]->Cin(c_between[1]); blk[2]->Sum(s_slice[2]); blk[2]->Cout(Cout);

        SC_METHOD(split_inputs);    dont_initialize(); sensitive << A << B;
        SC_METHOD(combine_outputs); dont_initialize();
        sensitive << s_slice[0] << s_slice[1] << s_slice[2];
    }

    void split_inputs() {
        sc_lv<12> va=A.read(), vb=B.read();
        for (int blki=0; blki<3; blki++) {
            sc_lv<4> a4, b4;
            for (int i=0; i<4; i++) { a4[i]=va[blki*4+i]; b4[i]=vb[blki*4+i]; }
            a_slice[blki].write(a4); b_slice[blki].write(b4);
        }
    }

    void combine_outputs() {
        sc_lv<12> s;
        for (int blki=0; blki<3; blki++) {
            sc_lv<4> s4=s_slice[blki].read();
            for (int i=0; i<4; i++) s[blki*4+i]=s4[i];
        }
        Sum.write(s);
    }

    void print_report() {
        unsigned int total=0;
        std::cout << "\n=======================================================\n";
        std::cout << "      12-BIT CARRY LOOKAHEAD ADDER SWITCH REPORT      \n";
        std::cout << "=======================================================\n";
        for(int i=0;i<3;i++){
            unsigned int bsw=blk[i]->total_block_switches();
            std::cout << " BLK" << i << " (bits " << (i*4) << "-" << (i*4+3)
                      << ") | switches: " << bsw << "\n";
            total+=bsw;
        }
        std::cout << "-------------------------------------------------------\n";
        std::cout << " GATES: 114   TOTAL SWITCHES: " << total << "\n";
        std::cout << "=======================================================\n";
    }

    ~CarryLookaheadAdder12() { for(int i=0;i<3;i++) delete blk[i]; }
};

// ==========================================
// 3. TESTBENCH – Monte Carlo 12-bit test
// ==========================================
SC_MODULE(Testbench) {
    sc_out<sc_lv<12>> A, B;
    sc_in<sc_lv<12>>  Sum;
    sc_in<bool>       Cout;

    CarryLookaheadAdder12* cla_ptr;

    unsigned int number_of_additions;
    unsigned long long accumulated_expected_results;
    unsigned long long accumulated_actual_results;
    unsigned int number_of_errors;

    SC_CTOR(Testbench)
        : cla_ptr(nullptr), number_of_additions(0),
          accumulated_expected_results(0), accumulated_actual_results(0), number_of_errors(0)
    { SC_THREAD(stimulus); }

    void stimulus() {
        std::cout << "\n=======================================================\n";
        std::cout << "    MONTE CARLO SIMULATION - 12-BIT CLA ADDER         \n";
        std::cout << "=======================================================\n";

        std::mt19937 rng(12345);
        std::uniform_int_distribution<unsigned int> dist(0U, 4095U);

        const unsigned int number_of_random_tests = 1000000;
        for (unsigned int i = 0; i < number_of_random_tests; i++) {
            apply_test(dist(rng), dist(rng));
        }

        std::cout << "-------------------------------------------------------\n";
        std::cout << " Anzahl getesteter 12-Bit-Additionen: " << number_of_additions << "\n";
        std::cout << " Summe aller erwarteten Ergebniswerte: " << accumulated_expected_results << "\n";
        std::cout << " Summe aller tatsaechlichen Ergebniswerte: " << accumulated_actual_results << "\n";
        std::cout << " Anzahl Fehler: " << number_of_errors << "\n";
        std::cout << "=======================================================\n";

        cla_ptr->print_report();
        sc_stop();
    }

    void apply_test(unsigned int a_value, unsigned int b_value) {
        A.write(make_12bit_vector(a_value));
        B.write(make_12bit_vector(b_value));
        wait(10, SC_NS);

        unsigned int expected_result = a_value + b_value;
        unsigned int actual_sum      = to_unsigned_12bit(Sum.read());
        unsigned int actual_result   = actual_sum + (Cout.read() ? 4096U : 0U);
        bool is_correct = expected_result == actual_result;

        number_of_additions++;
        accumulated_expected_results += expected_result;
        accumulated_actual_results   += actual_result;
        if (!is_correct) number_of_errors++;
    }
};

// ==========================================
// 4. MAIN
// ==========================================
int sc_main(int argc, char* argv[]) {
    sc_signal<sc_lv<12>> A, B, Sum;
    sc_signal<bool>      Cout;

    CarryLookaheadAdder12 cla("CLA12");
    Testbench tb("TB");
    tb.cla_ptr = &cla;

    cla.A(A); cla.B(B); cla.Sum(Sum); cla.Cout(Cout);
    tb.A(A);  tb.B(B);  tb.Sum(Sum);  tb.Cout(Cout);

    sc_start();
    return 0;
}
