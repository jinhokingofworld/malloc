#include <unistd.h>

// bp = payload의 시작 주소

/* memlib.c가 제공하는 힙 시뮬레이션 함수들입니다. */
void mem_init(void);          //mem_start_brk에 20MB 할당    
void mem_deinit(void);        //free(mem_start_brk) 
void *mem_sbrk(int incr);     //힙을 incr만큼 늘림. 새구간의 시작 주소 반환
void mem_reset_brk(void);     //brk 포인터를 처음 위치로 되돌려, 빈 힙 상태로 만듦
void *mem_heap_lo(void);      //힙의 첫 바이트 주소 반환
void *mem_heap_hi(void);      //힙의 마지막 유효 바이트 반환
size_t mem_heapsize(void);    //현재 힙 전체 크기를 바이트단위로 반환
size_t mem_pagesize(void);    //시스템 페이지 크기 반환    

#define WSIZE   4
#define DSIZE   8
//힙 확장할 때 한 번에 늘리는 크기
#define CHUNKSIZE (1<<12) // 비트 시프트 연산, 2^12

#define MAX(x, y) ((x) > (y) ? (x) : (y))

#define PACK(size, alloc) ((size) | (alloc))

//GET → p 주소 값 읽기
//PUT → p 주소에 값 쓰기
#define GET(p)          (*(unsigned int*)(p))
#define PUT(p, val)     (*(unsigned int*)(p) = (val))

//블록의 전체 크기 (header + payload + footer)
#define GET_SIZE(p)     (GET(p) & ~0x7)
#define GET_ALLOC(p)     (GET(p) & 0x1)

//header 위치 = header는 1WORD만큼이니까 bp에서 4 줄임
#define HDRP(bp)        ((char *)(bp) - WSIZE)
//payload 시작주소 + 현재 블록 크기 = 다음 블록 payload 시작주소
//payload시작주소 - header - footer => footer 시작주소
#define FTRP(bp)        ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))

#define ALIGNMENT 8
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~0x7)
#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))
