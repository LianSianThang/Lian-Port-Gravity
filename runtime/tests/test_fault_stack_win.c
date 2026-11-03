#include <assert.h>
#include <stdio.h>
#include "../src/fault_stack_win.h"

int main(void) {
    FaultStackCandidate found[8];
    /* A page-sized allocation makes the bounded read independent of the
     * compiler's placement of this test's own stack locals. */
    uintptr_t *words=VirtualAlloc(NULL,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    assert(words);
    words[2]=(uintptr_t)&main;
    words[5]=(uintptr_t)words; /* readable data is not evidence of a code pointer */
    words[9]=(uintptr_t)&VirtualQuery;
    const size_t count=fault_stack_candidates((uintptr_t)words,found,8);
    assert(count==2 && found[0].address==words[2] && found[0].offset==2*sizeof(uintptr_t));
    assert(found[1].address==words[9] && found[1].offset==9*sizeof(uintptr_t));
    assert(fault_stack_candidates((uintptr_t)words,found,1)==1);
    assert(fault_stack_candidates((uintptr_t)words,found,0)==0);
    assert(fault_stack_candidates(0,found,8)==0);
    assert(fault_stack_candidates(1,found,8)==0);
    assert(fault_stack_candidates((uintptr_t)words,NULL,8)==0);
    assert(VirtualFree(words,0,MEM_RELEASE));
    puts("PASS: bounded fault evidence finds executable module addresses, excludes data and handles unreadable stacks");
}
