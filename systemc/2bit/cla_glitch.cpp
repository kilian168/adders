// cla_glitch.cpp – 2-bit Carry Lookahead Adder, gate-level switching activity
#include <systemc.h>
#include <iostream>
#include <iomanip>
#include <string>

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
// 1. 2-BIT CARRY LOOKAHEAD ADDER MODULE
//    Gate count: 13
//    PG tier : 2 XOR (P) + 2 AND (G)          =  4 gates
//    C1 logic: 1 AND (P0&Cin) + 1 OR           =  2 gates
//    C2 logic: 3 AND + 2 OR (Cout)             =  5 gates
//    Sum tier: 2 XOR (S0=P0^Cin, S1=P1^C1)    =  2 gates
//    Cin is hardwired 0 for top-level adder.
// ==========================================
SC_MODULE(CarryLookaheadAdder2) {
    sc_in<sc_lv<2>>  A, B;
    sc_out<sc_lv<2>> Sum;
    sc_out<bool>     Cout;

    sc_event ev_carry, ev_sum;

    // Tier 1: PG gates
    bool p[2], g[2], p_old[2], g_old[2];
    unsigned int p_sw[2], g_sw[2];

    // Tier 2: Carry logic
    // C1 = G[0] | (P[0] & Cin),  Cin=0 => c1a always 0
    bool c1a, c1o, c1a_old, c1o_old;
    unsigned int c1a_sw, c1o_sw;
    // C2 = G[1] | (P[1]&G[0]) | (P[1]&P[0]&Cin),  Cin=0 => c2a3 always 0
    bool c2a1, c2a2, c2a3, c2o1, c2o2;
    bool c2a1_old, c2a2_old, c2a3_old, c2o1_old, c2o2_old;
    unsigned int c2a1_sw, c2a2_sw, c2a3_sw, c2o1_sw, c2o2_sw;

    // Tier 3: Sum gates
    bool s[2], s_old[2];
    unsigned int s_sw[2];

    SC_CTOR(CarryLookaheadAdder2) {
        for (int i = 0; i < 2; i++) {
            p[i] = g[i] = p_old[i] = g_old[i] = s[i] = s_old[i] = false;
            p_sw[i] = g_sw[i] = s_sw[i] = 0;
        }
        c1a = c1o = c1a_old = c1o_old = false;
        c1a_sw = c1o_sw = 0;
        c2a1 = c2a2 = c2a3 = c2o1 = c2o2 = false;
        c2a1_old = c2a2_old = c2a3_old = c2o1_old = c2o2_old = false;
        c2a1_sw = c2a2_sw = c2a3_sw = c2o1_sw = c2o2_sw = 0;

        SC_METHOD(tier1_pg);    dont_initialize(); sensitive << A << B;
        SC_METHOD(tier2_carry); dont_initialize(); sensitive << ev_carry;
        SC_METHOD(tier3_sum);   dont_initialize(); sensitive << ev_sum;
    }

    void tier1_pg() {
        sc_lv<2> va = A.read(), vb = B.read();
        for (int i = 0; i < 2; i++) {
            bool ai = va[i].is_01() ? va[i].to_bool() : false;
            bool bi = vb[i].is_01() ? vb[i].to_bool() : false;
            bool np = ai ^ bi;
            if (np != p_old[i]) { p_old[i] = np; p[i] = np; p_sw[i]++; }
            bool ng = ai & bi;
            if (ng != g_old[i]) { g_old[i] = ng; g[i] = ng; g_sw[i]++; }
        }
        ev_carry.notify(1, SC_NS);
    }

    void tier2_carry() {
        const bool cin = false;
        // C1
        bool nc1a = p[0] & cin;
        if (nc1a != c1a_old) { c1a_old = nc1a; c1a = nc1a; c1a_sw++; }
        bool nc1o = g[0] | c1a;
        if (nc1o != c1o_old) { c1o_old = nc1o; c1o = nc1o; c1o_sw++; }
        // C2 (Cout)
        bool nc2a1 = p[1] & g[0];
        if (nc2a1 != c2a1_old) { c2a1_old = nc2a1; c2a1 = nc2a1; c2a1_sw++; }
        bool nc2a2 = p[1] & p[0];
        if (nc2a2 != c2a2_old) { c2a2_old = nc2a2; c2a2 = nc2a2; c2a2_sw++; }
        bool nc2a3 = c2a2 & cin;
        if (nc2a3 != c2a3_old) { c2a3_old = nc2a3; c2a3 = nc2a3; c2a3_sw++; }
        bool nc2o1 = g[1] | c2a1;
        if (nc2o1 != c2o1_old) { c2o1_old = nc2o1; c2o1 = nc2o1; c2o1_sw++; }
        bool nc2o2 = c2o1 | c2a3;
        if (nc2o2 != c2o2_old) { c2o2_old = nc2o2; c2o2 = nc2o2; c2o2_sw++; }
        ev_sum.notify(1, SC_NS);
    }

    void tier3_sum() {
        const bool cin = false;
        bool ns0 = p[0] ^ cin;
        if (ns0 != s_old[0]) { s_old[0] = ns0; s[0] = ns0; s_sw[0]++; }
        bool ns1 = p[1] ^ c1o;
        if (ns1 != s_old[1]) { s_old[1] = ns1; s[1] = ns1; s_sw[1]++; }
        sc_lv<2> out; out[0] = s[0]; out[1] = s[1];
        Sum.write(out);
        Cout.write(c2o2);
    }

    unsigned int total_switches() {
        unsigned int t = 0;
        for (int i = 0; i < 2; i++) t += p_sw[i] + g_sw[i] + s_sw[i];
        t += c1a_sw + c1o_sw + c2a1_sw + c2a2_sw + c2a3_sw + c2o1_sw + c2o2_sw;
        return t;
    }

    void print_report() {
        std::cout << "\n=======================================================\n";
        std::cout << "       2-BIT CARRY LOOKAHEAD ADDER SWITCH REPORT      \n";
        std::cout << "=======================================================\n";
        std::cout << " PG tier  | P0_XOR:" << std::setw(4) << p_sw[0]
                  << "  G0_AND:" << std::setw(4) << g_sw[0]
                  << "  P1_XOR:" << std::setw(4) << p_sw[1]
                  << "  G1_AND:" << std::setw(4) << g_sw[1] << "\n";
        std::cout << " C1 logic | AND:"   << std::setw(4) << c1a_sw
                  << "  OR:"              << std::setw(4) << c1o_sw << "\n";
        std::cout << " C2 logic | AND1:"  << std::setw(4) << c2a1_sw
                  << "  AND2:"            << std::setw(4) << c2a2_sw
                  << "  AND3:"            << std::setw(4) << c2a3_sw
                  << "  OR1:"             << std::setw(4) << c2o1_sw
                  << "  OR2:"             << std::setw(4) << c2o2_sw << "\n";
        std::cout << " Sum tier | S0_XOR:" << std::setw(4) << s_sw[0]
                  << "  S1_XOR:"          << std::setw(4) << s_sw[1] << "\n";
        std::cout << "-------------------------------------------------------\n";
        std::cout << " GATES: 13   TOTAL SWITCHES: " << total_switches() << "\n";
        std::cout << "=======================================================\n";
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

    unsigned int number_of_additions;
    unsigned int accumulated_expected_results;
    unsigned int accumulated_actual_results;
    unsigned int number_of_errors;

    SC_CTOR(Testbench)
        : cla_ptr(nullptr), number_of_additions(0),
          accumulated_expected_results(0), accumulated_actual_results(0), number_of_errors(0)
    { SC_THREAD(stimulus); }

    void stimulus() {
        std::cout << "\n=======================================================\n";
        std::cout << "         EXHAUSTIVE 2-BIT CARRY LOOKAHEAD TEST        \n";
        std::cout << "=======================================================\n";
        std::cout << " Nr |  A | A_dec |  B | B_dec | Expected | Sum | Cout | Actual | Status\n";
        std::cout << "----+----+-------+----+-------+----------+-----+------+--------+--------\n";

        for (unsigned int a_value = 0; a_value < 4; a_value++) {
            for (unsigned int b_value = 0; b_value < 4; b_value++) {
                apply_test(a_value, b_value);
            }
        }

        std::cout << "-------------------------------------------------------\n";
        std::cout << " Anzahl getesteter 2-Bit-Additionen: " << number_of_additions << "\n";
        std::cout << " Summe aller erwarteten Ergebniswerte: " << accumulated_expected_results << "\n";
        std::cout << " Summe aller tatsaechlichen Ergebniswerte: " << accumulated_actual_results << "\n";
        std::cout << " Anzahl Fehler: " << number_of_errors << "\n";
        std::cout << "=======================================================\n";

        cla_ptr->print_report();
        sc_stop();
    }

    void apply_test(unsigned int a_value, unsigned int b_value) {
        A.write(make_2bit_vector(a_value));
        B.write(make_2bit_vector(b_value));
        wait(10, SC_NS);

        unsigned int expected_result = a_value + b_value;
        unsigned int actual_sum      = to_unsigned_2bit(Sum.read());
        unsigned int actual_result   = actual_sum + (Cout.read() ? 4U : 0U);
        bool is_correct = expected_result == actual_result;

        number_of_additions++;
        accumulated_expected_results += expected_result;
        accumulated_actual_results   += actual_result;
        if (!is_correct) number_of_errors++;

        std::cout << std::setw(3) << number_of_additions << " | "
                  << A.read() << " | " << std::setw(5) << a_value << " | "
                  << B.read() << " | " << std::setw(5) << b_value << " | "
                  << std::setw(8) << expected_result << " | "
                  << Sum.read() << " | " << std::setw(4) << Cout.read() << " | "
                  << std::setw(6) << actual_result << " | "
                  << (is_correct ? "OK" : "FEHLER") << "\n";
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
