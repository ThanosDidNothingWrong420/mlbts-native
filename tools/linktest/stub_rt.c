// Link smoke test only (tools/linktest/build.sh): stub of the 7 runtime symbols TotK-generated code needs. Not a runtime.
#include <stdio.h>
#include <stdlib.h>
#include "cpu.h"
#include "layout.h"
volatile int g_fnprof;
void fnprof_hit(uint64_t a, uint64_t lr, uint64_t x0) { (void)a; (void)lr; (void)x0; }
void rt_dispatch(Cpu *c) { (void)c; abort(); }
void rt_svc(Cpu *c, uint32_t imm, uint64_t pc) { (void)c; (void)imm; (void)pc; abort(); }
void rt_brk(Cpu *c, uint32_t imm, uint64_t pc) { (void)c; (void)imm; (void)pc; abort(); }
void rt_udf(Cpu *c, uint64_t pc) { (void)c; (void)pc; abort(); }
uint64_t rt_ticks(void) { return 0; }
extern const uint64_t main_fn_count, sdk_fn_count;
int main(void) {
    printf("linked: main %llu fns, sdk %llu fns\n", (unsigned long long)main_fn_count, (unsigned long long)sdk_fn_count);
    return 0;
}
