#include <unistd.h>

/* memlib.c가 제공하는 힙 시뮬레이션 함수들입니다. */
void mem_init(void);               
void mem_deinit(void);
void *mem_sbrk(int incr);
void mem_reset_brk(void); 
void *mem_heap_lo(void);
void *mem_heap_hi(void);
size_t mem_heapsize(void);
size_t mem_pagesize(void);
