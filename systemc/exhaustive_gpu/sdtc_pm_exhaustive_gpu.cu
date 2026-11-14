// sdtc_pm_exhaustive_gpu.cu -- GPU port of the SD/TC-PM exhaustive v2
// path-aware EDP sweep (adders/systemc/"exhaustive v2"/<N>bit/sdtc_glitch_pm_pathEDP.cpp).
//
// STATUS: written, NOT YET BUILT OR RUN. nvcc is not installed in this
// environment (the `cuda` package is available via pacman but needs sudo).
// Install it first:
//     sudo pacman -S cuda
// then build with build.sh.
//
// BEFORE TRUSTING ANY OUTPUT: run this at widths 8 and 12 (./build/pm_exh_gpu 8 ...,
// ./build/pm_exh_gpu 12 ...) and diff AvgSwitches / AvgDelay_ns / AvgEDP_Correct
// against the already-verified CPU rows in
// "../exhaustive v2/exhaustive_path_aware_edp_results_v2.csv". They must match
// to the printed precision. Only trust 16/32-bit GPU output after that passes.
// (2^64 total vectors at width 64 is astronomically infeasible either way --
// this is realistically useful up to ~width 24-28, not a full 64-bit sweep.)
//
// Mirrors the CPU reference bit-for-bit:
//   - naf_digits_signed_pm: genuine Non-Adjacent-Form signed-digit decode of
//     the exhaustively-swept SD value, identical recurrence (same integer
//     division, not a shift -- see comment at naf_digits below).
//   - Per-digit cell: the literal 5-gate full adder (shared with PPM) fed
//     (a=TC(i), b'=NOT(SD_b(i)), cin=SD_a(i)), output sum inverted again
//     before becoming Sum_a(i); cout passed straight through to Sum_b(i+1).
//     This is the "two-inverter" PM cell verified earlier this session.
//   - Switch counting: every one of the 7 real gate instances per digit
//     (notB, s1, c1, rawsum, c2, cout, sum) plus the single top-bit NOT
//     gate, compared bit-for-bit against the immediately preceding vector
//     in exhaustive (ascending signed) order -- exactly what
//     Gate::eval()/GateNot::eval() count on the CPU via total_switches().
//   - Path-aware delay: for the current vector, the max over every
//     (input pin, output pin) pair that BOTH changed this vector of the
//     pre-extracted Nangate45 critical-path delay table -- identical to
//     apply_test()'s in_changed/out_changed double loop on the CPU.
//   - Cross-chunk warm-up: each GPU thread, like each CPU fork worker,
//     evaluates one "warm-up" vector (the true predecessor of its chunk's
//     first real vector) purely to seed prev-state correctly, exactly
//     mirroring stimulus()'s warm_sd/warm_tc logic.
//
// Output line format matches the CPU binaries' own stdout exactly, so the
// existing splice_pm_rows.py parsing (same regex) can consume it unchanged:
//   SDTC-PM-<N>-EXH-pathEDP: VECTORS=<v> AVG_SWITCHES=<s> AVG_DELAY_ns=<d>
//     AVG_EDP_correct=<c> AVG_EDP_naive=<nv> pct_diff=<p>%

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <cuda_runtime.h>

#define MAX_WIDTH 32  // raise with care -- see note in main() about feasibility

struct DigitGates {
    unsigned char notB, s1, c1, rawsum, c2, cout, sum;
};

struct VectorState {
    uint64_t sda, sdb, tc;
    uint64_t sum_a, sum_b;
    DigitGates digit[MAX_WIDTH];
    unsigned char top;
};

// Bit-identical to naf_digits_signed_pm on the CPU: (v - d) is always exactly
// divisible by 2 by construction, so plain integer division ("/2", truncating
// toward zero) gives the exact same result as the CPU's "/2" -- no shift-vs-
// division subtlety to worry about.
__device__ __forceinline__ void naf_digits(int64_t v, int8_t* digits, int n) {
    for (int i = 0; i < n; i++) {
        if (v & 1LL) {
            int d = (int)(2LL - (v & 3LL));
            digits[i] = (int8_t)d;
            v = (v - d) / 2;
        } else {
            digits[i] = 0;
            v = v / 2;
        }
    }
}

__device__ __forceinline__ void eval_vector(int64_t sd_signed, uint64_t tc_value, int width, VectorState &vs) {
    int8_t digits[MAX_WIDTH];
    naf_digits(sd_signed, digits, width);
    uint64_t sda = 0, sdb = 0;
    for (int i = 0; i < width; i++) {
        if (digits[i] == 1) sda |= (1ULL << i);
        else if (digits[i] == -1) sdb |= (1ULL << i);
    }
    vs.sda = sda; vs.sdb = sdb; vs.tc = tc_value;

    uint64_t sum_a = 0, sum_b = 0;
    for (int i = 0; i < width; i++) {
        int a = (int)((tc_value >> i) & 1ULL);
        int b = (int)((sdb >> i) & 1ULL);
        int cin = (int)((sda >> i) & 1ULL);
        DigitGates &g = vs.digit[i];
        g.notB   = 1 - b;
        g.s1     = a ^ g.notB;
        g.c1     = a & g.notB;
        g.rawsum = g.s1 ^ cin;
        g.c2     = g.s1 & cin;
        g.cout   = g.c1 | g.c2;
        g.sum    = 1 - g.rawsum;
        sum_a |= ((uint64_t)g.sum) << i;
        sum_b |= ((uint64_t)g.cout) << (i + 1);
    }
    int tc_top = (int)((tc_value >> (width - 1)) & 1ULL);
    vs.top = (unsigned char)(1 - tc_top);
    sum_a |= ((uint64_t)vs.top) << width;
    vs.sum_a = sum_a;
    vs.sum_b = sum_b;
}

__device__ __forceinline__ int count_switches(const VectorState &cur, const VectorState &prev, int width) {
    int sw = 0;
    for (int i = 0; i < width; i++) {
        const DigitGates &a = cur.digit[i], &b = prev.digit[i];
        sw += (a.notB != b.notB) + (a.s1 != b.s1) + (a.c1 != b.c1) +
              (a.rawsum != b.rawsum) + (a.c2 != b.c2) + (a.cout != b.cout) + (a.sum != b.sum);
    }
    sw += (cur.top != prev.top);
    return sw;
}

// Literal port of apply_test's in_changed/out_changed double loop -- not the
// "smart" sparsity-aware version, deliberately, so it is obviously correct
// against the CPU reference. Optimize only after the width-8/12 cross-check
// in the file header passes.
__device__ double path_delay_lookup(const VectorState &cur, const VectorState &prev,
                                     int width, const double* path_delay, int num_out) {
    int num_in = 3 * width;
    double max_delay = 0.0;
    for (int in = 0; in < num_in; in++) {
        bool changed;
        if (in < width)            changed = ((cur.sda >> in) & 1ULL) != ((prev.sda >> in) & 1ULL);
        else if (in < 2 * width)   changed = ((cur.sdb >> (in - width)) & 1ULL) != ((prev.sdb >> (in - width)) & 1ULL);
        else                       changed = ((cur.tc >> (in - 2 * width)) & 1ULL) != ((prev.tc >> (in - 2 * width)) & 1ULL);
        if (!changed) continue;
        for (int out = 0; out < num_out; out++) {
            bool out_changed;
            if (out <= width) out_changed = ((cur.sum_a >> out) & 1ULL) != ((prev.sum_a >> out) & 1ULL);
            else              out_changed = ((cur.sum_b >> (out - (width + 1))) & 1ULL) != ((prev.sum_b >> (out - (width + 1))) & 1ULL);
            if (!out_changed) continue;
            double d = path_delay[in * num_out + out];
            if (d > max_delay) max_delay = d;
        }
    }
    return max_delay;
}

// exh_map/exh_signed: identical remapping to the CPU's exh_map/exh_signed,
// turning a 0-based loop index into the bit pattern of the value that makes
// the whole sweep visit every width-bit signed value in ascending order.
__device__ __forceinline__ uint64_t exh_map(uint64_t i, uint64_t offset, uint64_t mask) {
    return (i + offset) & mask;
}
__device__ __forceinline__ int64_t exh_signed(uint64_t p, uint64_t top, uint64_t mask) {
    return (p & top) ? (int64_t)p - (int64_t)mask - 1 : (int64_t)p;
}

struct ThreadResult {
    double sum_switches, sum_delay, sum_edp;
    unsigned long long errors, vectors;
};

__global__ void exhaustive_kernel(int width, uint64_t count, uint64_t offset, uint64_t mask, uint64_t top,
                                   const double* path_delay, int num_out,
                                   uint64_t isd_start, uint64_t isd_end, uint64_t chunks_per_thread,
                                   ThreadResult* results) {
    uint64_t tid = blockIdx.x * (uint64_t)blockDim.x + threadIdx.x;
    uint64_t my_start = isd_start + tid * chunks_per_thread;
    uint64_t my_end = my_start + chunks_per_thread;
    if (my_end > isd_end) my_end = isd_end;
    if (my_start >= my_end) { results[tid] = {0, 0, 0, 0, 0}; return; }

    // Warm-up: seed prev-state with the true predecessor vector, exactly
    // like stimulus()'s warm_sd/warm_tc (sd_start==0 case uses the SAME
    // first vector as its own warm-up, matching the CPU's single-process
    // cold-start convention bit for bit).
    uint64_t warm_isd = (my_start == 0) ? 0 : (my_start - 1);
    uint64_t warm_itc = (my_start == 0) ? 0 : (count - 1);
    uint64_t warm_sd_p = exh_map(warm_isd, offset, mask);
    uint64_t warm_tc_p = exh_map(warm_itc, offset, mask);
    VectorState prev;
    eval_vector(exh_signed(warm_sd_p, top, mask), warm_tc_p, width, prev);

    double sum_switches = 0, sum_delay = 0, sum_edp = 0;
    unsigned long long errors = 0, vectors = 0;

    for (uint64_t isd = my_start; isd < my_end; isd++) {
        uint64_t sd_p = exh_map(isd, offset, mask);
        int64_t sd_signed = exh_signed(sd_p, top, mask);
        for (uint64_t itc = 0; itc < count; itc++) {
            uint64_t tc_p = exh_map(itc, offset, mask);

            VectorState cur;
            eval_vector(sd_signed, tc_p, width, cur);

            // correctness check
            int64_t sign_term = ((tc_p >> (width - 1)) & 1ULL) ? -((int64_t)1 << width) : ((int64_t)1 << width);
            int64_t actual = (int64_t)cur.sum_b - (int64_t)cur.sum_a + sign_term;
            int64_t tc_signed = ((tc_p >> (width - 1)) & 1ULL) ? (int64_t)tc_p - ((int64_t)1 << width) : (int64_t)tc_p;
            int64_t expected = sd_signed + tc_signed;
            if (expected != actual) errors++;

            int sw = count_switches(cur, prev, width);
            double d = path_delay_lookup(cur, prev, width, path_delay, num_out);
            sum_switches += sw;
            sum_delay += d;
            sum_edp += (double)sw * d;
            vectors++;

            prev = cur;
        }
    }
    results[tid] = {sum_switches, sum_delay, sum_edp, errors, vectors};
}

// ---- host side ----

static void load_path_delay(const char* path, int width, std::vector<double>& table, int& num_in, int& num_out) {
    num_in = 3 * width;
    num_out = 2 * (width + 1);
    table.assign((size_t)num_in * num_out, 0.0);
    std::ifstream f(path);
    if (!f) { fprintf(stderr, "cannot open %s\n", path); exit(1); }
    std::string line;
    while (std::getline(f, line)) {
        int i, o; double v;
        if (sscanf(line.c_str(), " path_delay[%d][%d] = %lf", &i, &o, &v) == 3) {
            if (i >= 0 && i < num_in && o >= 0 && o < num_out) table[(size_t)i * num_out + o] = v;
        }
    }
}

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s <width> <path_delay_file> [threads_per_block] [blocks]\n", argv[0]);
        return 1;
    }
    int width = atoi(argv[1]);
    const char* delay_file = argv[2];
    int tpb = argc > 3 ? atoi(argv[3]) : 256;
    int blocks = argc > 4 ? atoi(argv[4]) : 4096;
    if (width > MAX_WIDTH) {
        fprintf(stderr, "width %d exceeds MAX_WIDTH %d (raise the #define and recompile)\n", width, MAX_WIDTH);
        return 1;
    }
    if (width >= 64) {
        // 1ULL << width is undefined behaviour at width==64 (shift by the
        // full type width); the count/mask/offset/sign_term arithmetic
        // throughout this file assumes width < 64. Width 64 would also mean
        // 2^128 total vectors, not something exhaustive coverage can reach
        // regardless -- use the existing Monte Carlo sweep for width 64.
        fprintf(stderr, "width must be < 64 (1ULL<<width is UB at width==64); use Monte Carlo for width 64\n");
        return 1;
    }
    if (width < 2) {
        fprintf(stderr, "width must be >= 2\n");
        return 1;
    }

    uint64_t count = (uint64_t)1 << width;
    uint64_t mask = count - 1;
    uint64_t offset = (uint64_t)1 << (width - 1);
    uint64_t top = (uint64_t)1 << (width - 1);

    std::vector<double> h_table;
    int num_in, num_out;
    load_path_delay(delay_file, width, h_table, num_in, num_out);
    double* d_table;
    cudaMalloc(&d_table, h_table.size() * sizeof(double));
    cudaMemcpy(d_table, h_table.data(), h_table.size() * sizeof(double), cudaMemcpyHostToDevice);

    uint64_t total_threads = (uint64_t)tpb * blocks;
    uint64_t chunks_per_thread = (count + total_threads - 1) / total_threads;

    ThreadResult* d_results;
    cudaMalloc(&d_results, total_threads * sizeof(ThreadResult));

    exhaustive_kernel<<<blocks, tpb>>>(width, count, offset, mask, top, d_table, num_out,
                                        0, count, chunks_per_thread, d_results);
    cudaError_t err = cudaDeviceSynchronize();
    if (err != cudaSuccess) {
        fprintf(stderr, "CUDA error: %s\n", cudaGetErrorString(err));
        return 1;
    }

    std::vector<ThreadResult> h_results(total_threads);
    cudaMemcpy(h_results.data(), d_results, total_threads * sizeof(ThreadResult), cudaMemcpyDeviceToHost);

    double sum_switches = 0, sum_delay = 0, sum_edp = 0;
    unsigned long long errors = 0, vectors = 0;
    for (auto& r : h_results) {
        sum_switches += r.sum_switches;
        sum_delay += r.sum_delay;
        sum_edp += r.sum_edp;
        errors += r.errors;
        vectors += r.vectors;
    }

    if (errors != 0) {
        fprintf(stderr, "FATAL: %llu / %llu vectors mismatched expected arithmetic result\n", errors, vectors);
        return 2;
    }

    double avg_switches = sum_switches / (double)vectors;
    double avg_delay = sum_delay / (double)vectors;
    double avg_edp_correct = sum_edp / (double)vectors;
    double avg_edp_naive = avg_switches * avg_delay;
    double pct_diff = (avg_edp_naive - avg_edp_correct) / avg_edp_correct * 100.0;

    printf("SDTC-PM-%d-EXH-pathEDP: VECTORS=%llu AVG_SWITCHES=%g AVG_DELAY_ns=%g "
           "AVG_EDP_correct=%g AVG_EDP_naive=%g pct_diff=%g%%\n",
           width, vectors, avg_switches, avg_delay, avg_edp_correct, avg_edp_naive, pct_diff);

    cudaFree(d_table);
    cudaFree(d_results);
    return 0;
}
