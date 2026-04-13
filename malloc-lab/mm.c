/*
 * mm-naive.c - 매우 단순하게 만든 malloc 구현 예제입니다.
 *
 * 이 구현은 새 블록이 필요할 때마다 힙의 끝(brk 포인터)을 앞으로 늘려서
 * 메모리를 내어 줍니다. 이미 사용이 끝난 블록을 다시 활용하지 않기 때문에
 * 구현은 쉽지만 메모리 효율은 좋지 않습니다.
 *
 * 블록 맨 앞에는 요청 크기를 저장하는 작은 메타데이터가 있고,
 * 그 뒤에 사용자가 실제로 쓰는 데이터 영역(payload)이 옵니다.
 * 일반적인 allocator가 갖는 빈 블록 재사용, 병합(coalescing),
 * 복잡한 헤더/푸터 관리 같은 기능은 없습니다.
 *
 * realloc도 별도 최적화 없이,
 * 새 블록을 할당하고 기존 내용을 복사한 뒤 이전 블록을 해제하는 방식입니다.
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

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

static char *heap_listp;

/*********************************************************
 * 학생 안내:
 * 이 구조체에는 팀 정보를 적습니다.
 * 드라이버 프로그램은 이 값을 읽어 제출자 정보를 확인합니다.
 ********************************************************/
team_t team = {
    "ateam",
    "jinho",
    "bovik@cs.cmu.edu",
    "",
    ""};

/* 블록 주소를 8바이트 경계에 맞추기 위한 정렬 기준입니다. */
#define ALIGNMENT 8

/* 주어진 크기를 가장 가까운 ALIGNMENT 배수로 올림합니다. */
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~0x7)

/* size_t 크기도 정렬 기준에 맞춰 저장하기 위한 상수입니다. */
// #define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

int mm_init(void);
void *mm_malloc(size_t size);
void mm_free(void *ptr);
void *extend_heap(size_t wsize);
void *coalesce(void *bp);
void* first_fit(size_t wsize);
void place(void *bp, size_t wsize);
/*
 * mm_init - malloc 패키지를 초기화합니다.
 *
 * 이 단순 예제는 별도 가용 리스트나 힙 메타데이터를 만들지 않으므로
 * 실제 초기화 작업은 필요하지 않습니다.
 */
int mm_init(void) {
    /*
        역할: 할당기를 초기화한다.
        - 힙의 시작 부분에 패딩을 두어 이후 payload가 정렬되도록 한다.
        - prologue 블록을 만든다. (할당된 상태의 가짜 블록)
        - epilogue 블록을 만든다. (크기 0, 할당된 상태의 헤더)
        - 초기 가용 블록을 만들기 위해 힙을 CHUNKSIZE만큼 확장한다.
        - 확장에 실패하면 -1, 성공하면 0을 반환한다.
    */

    void *heap_startp;
    //padding, prologue, epilogue공간 할당받기
    if ((heap_startp = mem_sbrk(4 * WSIZE)) == (void *)-1)
        return -1;

    // void *curr = mem_heap_lo();
    heap_listp = ((char *)heap_startp + DSIZE);

    //기존에 썼던 코드도 맞음
    // /* 여기는 padding */
    // PUT(curr, 0);

    // curr = (char *)curr + WSIZE;
    // /* prologue header */
    // PUT(curr, PACK(DSIZE, 1));

    // curr = (char *)curr + WSIZE;
    // /* prologue footer */
    // PUT(curr, PACK(DSIZE, 1));

    // curr = (char *)curr + WSIZE;
    // /* epilogue header */
    // PUT(curr, PACK(0, 1));

    //padding
    PUT((char *)heap_startp, 0);
    //prologue - header
    PUT((char *)heap_startp + WSIZE, PACK(DSIZE, 1));
    //prologue - footer
    PUT((char *)heap_startp + 2*WSIZE, PACK(DSIZE, 1));
    //epilogue - header
    PUT((char *)heap_startp + 3*WSIZE, PACK(0, 1));
    
    void *bp = extend_heap(CHUNKSIZE / WSIZE);
    if (bp == NULL) return -1;

    return 0;
}

/*
 * mm_malloc - 힙의 끝을 늘려 새 블록을 할당합니다.
 *
 * 블록 앞부분에는 요청 크기를 기록해 두고,
 * 사용자에게는 그 뒤의 payload 시작 주소를 돌려줍니다.
 */
void *mm_malloc(size_t size)
{
    /*
        heap을 한번 탐색한다.
        size보다 큰 가용 메모리가 없을 때,
            1. coalesce를 해서 큰 가용 메모리를 만든다.
            2. 그래도 없을 때 sbrk으로 추가해서 사용
        가용 메모리 공간이 있을 때,
            1. 어떤 메모리 공간을 사용할 지 정한다(FF, NF, BF)
            2. 메모리 공간을 전부 사용할지, 나눌지 정한다.
    */
    if (size == 0) return NULL;

    size_t asize = ALIGN(size) + DSIZE;
    int words = (int)asize / WSIZE;

    //탐색을 통해, 메모리를 찾는다.
    void *bp = first_fit(asize);

    //적합한 블록을 못 찾았다면, extend해서 할당
    if (bp == NULL) {
        bp = extend_heap(words);
        //확장 실패
        if (bp == NULL) {
            return NULL;
        }
    }

    //있다면, 사이즈 조정해서 사용
    place(bp, asize);
    return bp;
}

/*
 * mm_free - 이 예제에서는 free가 아무 동작도 하지 않습니다.
 *
 * 즉, 한 번 확보한 힙 공간은 다시 재사용되지 않습니다.
 */
void mm_free(void *ptr)
{
    if (ptr == NULL) return;

    // 현재 포인터가 가리키는 애 free만들기
    unsigned int size = GET_SIZE(HDRP(ptr));
    PUT(HDRP(ptr), PACK(size, 0));
    PUT(FTRP(ptr), PACK(size, 0));

    coalesce(ptr);
    // coalease로 사이즈 키우기
    return;
}

/*
 * mm_realloc - mm_malloc과 mm_free를 이용해 단순하게 구현합니다.
 *
 * 새 블록을 하나 만든 뒤,
 * 기존 데이터 중 복사 가능한 만큼만 옮기고,
 * 이전 블록을 해제합니다.
 */
void *mm_realloc(void *ptr, size_t size)
{
    //일단 뒤 블록의 free인지 확인,
    //free면 사이즈를 확인하고, 현재 값과 더한 값이 size보다 크면, 병합하고, bp리턴

    //뒤 블록이 free가 아니면, ff로 크기가 맞는 새로운 위치를 찾음 
    //기존의 내용 복사

    return NULL;
}

//size_t는 메모리의 크기, 길이, 개수를 표현하는 표준 타입
//여기에서는 word의 갯수를 받음
void *extend_heap(size_t wsize) {
    //mem_sbrk → 공간 확보 → free block 생성 → epilogue 재배치

    void *bp = mem_sbrk((int) wsize * WSIZE);
    void *new_headp = (char *)bp - WSIZE; 
    if (bp == (void *) - 1)
        return NULL;

    //free Block 생성
    PUT((char *)new_headp, PACK(wsize * WSIZE, 0));
    PUT((char *)new_headp + (wsize * WSIZE) - WSIZE, PACK(wsize * WSIZE, 0));
    //Epilogue
    PUT((char *)new_headp + (wsize * WSIZE), PACK(0, 1));

    return coalesce(bp);
}

// 연결 coalesce
void *coalesce(void *bp) {
    if (bp == NULL) return NULL;

    unsigned int prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
    unsigned int next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    unsigned int size = GET_SIZE(HDRP(bp));

    if (prev_alloc && next_alloc) {              // case 1
        return bp;
    }

    else if (prev_alloc && !next_alloc) {        // case 2
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
    }

    else if (!prev_alloc && next_alloc) {        // case 3
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        PUT(FTRP(bp), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }

    else {                                       // case 4
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) +
                GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }

    return bp;
    // //앞을 확인한다.
    // //2word 앞으로 footer == free인지 확인 가능
    // void *prev_bp = PREV_BLKP(bp);
    // void *prev_hp = HDRP(prev_bp);
    // if (GET_ALLOC(prev_hp) == 0) {
    //     //앞의 블록 사이즈 얻기
    //     unsigned int prev_size = GET_SIZE(prev_hp);

    //     //더한 값으로, 앞 블록 헤더 갱신
    //     PUT(prev_hp, PACK(prev_size + current_size, 0));

    //     //더한 값으로, 뒤 블록 푸터 갱신
    //     PUT(FTRP(bp), PACK(prev_size + current_size, 0));
    // }

    // //뒤를 확인한다.
    // void *next_bp = NEXT_BLKP(bp);
    // void *next_fp = FTRP(next_bp);
    // if (GET_ALLOC(HDRP(next_bp)) == 0) {
    //     //합치기
    //     //앞의 블록 사이즈 얻기
    //     unsigned int next_size = GET_SIZE(next_fp);

    //     //더한 값으로, 앞 블록 헤더 갱신
    //     PUT(HDRP(bp), PACK(current_size + next_size, 0));
    //     //더한 값으로, 뒤 블록 푸터 갱신
    //     PUT(next_fp, PACK(current_size + next_size, 0));
    // }
}

//쓸 수 있는 블록 찾기, 워드 사이즈 받았음
void* first_fit(size_t asize) {
    //리스트의 처음부터 순회를 한다.
    void *bp = NEXT_BLKP(heap_listp);
    // 사이즈가 0이 아닐 때까지 순회를 한다.
    while (GET_SIZE(HDRP(bp)) != 0) {
        if (asize <= GET_SIZE(HDRP(bp)) && GET_ALLOC(HDRP(bp)) == 0) return bp;

        bp = NEXT_BLKP(bp);
    }
    
    //못 찾았다면, NULL 리턴
    return NULL;
}

//실제 블록 사이즈를 받아와야 함
void place(void *bp, size_t asize) {
    //찾은 블록을 어떻게 사용할지 생각한다.
    size_t ogsize = GET_SIZE(HDRP(bp));
    size_t temp = ogsize - asize;

    //남은 블록의 크기가 최소 8바이트이상이 되지 않는다면, 그대로 사용한다.
    if (temp < 2 * DSIZE) {
        PUT(HDRP(bp), PACK(ogsize, 1));
        PUT(FTRP(bp), PACK(ogsize, 1)); 
        return;
    }

    //아니면 나눠서 사용한다.
    PUT(HDRP(bp), PACK(asize, 1));
    PUT(FTRP(bp), PACK(asize, 1));
    
    void *next_bp = NEXT_BLKP(bp);  
    PUT(HDRP(next_bp), PACK(temp, 0));
    PUT(FTRP(next_bp), PACK(temp, 0));
    return;
}
