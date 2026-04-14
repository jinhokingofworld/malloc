/*
Explicit Free List Memory Allocator
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

/* 블록 주소를 8바이트 경계에 맞추기 위한 정렬 기준입니다. */
#define ALIGNMENT 8

#define MAX(x, y) ((x) > (y) ? (x) : (y))
#define MIN(x, y) ((x) > (y) ? (y) : (x))

/* 주어진 크기를 가장 가까운 ALIGNMENT 배수로 올림합니다. */
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~0x7)

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
#define FTRP(bp)        ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

/*
 * free block의 payload 앞쪽에는 연결 리스트용 포인터 2개를 저장한다.
 * bp는 payload의 시작 주소이므로, bp에는 prev, bp + 포인터 크기에는 next를 둔다.
 */
#define PREV_FREE(bp)       (*(void **)(bp))
#define NEXT_FREE(bp)       (*(void **)((char *)(bp) + sizeof(void *)))
#define MIN_BLOCK_SIZE      ALIGN(DSIZE + 2 * sizeof(void *))

#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))

static char *heap_listp;
static void *free_listp;

/*********************************************************
 * 학생 안내:
 * 이 구조체에는 팀 정보를 적습니다.
 * 드라이버 프로그램은 이 값을 읽어 제출자 정보를 확인합니다.
 ********************************************************/
team_t team = {
    "Ateam",
    "jinho",
    "jinhokinoftheword@gmail.com",
    "",
    ""};

/* size_t 크기도 정렬 기준에 맞춰 저장하기 위한 상수입니다. */
// #define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

int mm_init(void);
void *mm_malloc(size_t size);
void mm_free(void *ptr);
void *extend_heap(size_t wsize);
void *coalesce(void *bp);
void *first_fit(size_t asize);
void place(void *bp, size_t wsize);
static size_t adjust_block_size(size_t size);
static void insert_free_block(void *bp);
static void remove_free_block(void *bp);


int mm_init(void) {
    /*
        역할: 할당기를 초기화한다.
        - 힙의 시작 부분에 패딩을 두어 이후 payload가 정렬되도록 한다.
        - prologue 블록을 만든다. (할당된 상태의 가짜 블록)
        - epilogue 블록을 만든다. (크기 0, 할당된 상태의 헤더)
        - 초기 가용 블록을 만들기 위해 힙을 CHUNKSIZE만큼 확장한다.
        - 확장에 실패하면 -1, 성공하면 0을 반환한다.
    */

    //padding, prologue, epilogue공간 할당받기
    if ((heap_listp = mem_sbrk(4 * WSIZE)) == (void *)-1)
        return -1;

    //padding
    PUT((char *)heap_listp, 0);
    //prologue - header
    PUT((char *)heap_listp + WSIZE, PACK(DSIZE, 1));
    //prologue - footer
    PUT((char *)heap_listp + 2*WSIZE, PACK(DSIZE, 1));
    //epilogue - header
    PUT((char *)heap_listp + 3*WSIZE, PACK(0, 1));

    heap_listp = (char *)heap_listp + 2 * WSIZE;
    free_listp = NULL;

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

    size_t asize = adjust_block_size(size);

    //탐색을 통해, 메모리를 찾는다.
    void *bp = first_fit(asize);

    //적합한 블록을 못 찾았다면, extend해서 할당
    if (bp == NULL) {
        size_t extend_size = MAX(asize, CHUNKSIZE);
        bp = extend_heap(extend_size / WSIZE);
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
 * mm_free - 블록을 free 상태로 바꾸고, 주변 free block과 합친 뒤
 * explicit free list에 다시 넣습니다.
 */
void mm_free(void *ptr)
{
    if (ptr == NULL) return;

    // 현재 포인터가 가리키는 애 free만들기
    unsigned int size = GET_SIZE(HDRP(ptr));
    PUT(HDRP(ptr), PACK(size, 0));
    PUT(FTRP(ptr), PACK(size, 0));

    ptr = coalesce(ptr);
    insert_free_block(ptr);
    // coalease로 사이즈 키우기
    return;
}

/*
 * mm_realloc - 가능하면 현재 블록을 그대로 쓰거나 다음 free block을 합쳐 확장합니다.
 * 그럴 수 없으면 새 블록을 할당하고 기존 데이터를 복사합니다.
 */
void *mm_realloc(void *bp, size_t size)
{
    if (bp == NULL) return mm_malloc(size);
    if (size == 0) {
        mm_free(bp);
        return NULL;
    }

    size_t asize = adjust_block_size(size);
    
    //일단 뒤 블록의 free인지 확인,
    //free면 사이즈를 확인하고, 현재 값과 더한 값이 size보다 크면, 병합하고, bp리턴
    unsigned int next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t next_size = GET_SIZE(HDRP(NEXT_BLKP(bp)));
    size_t curr_size = GET_SIZE(HDRP(bp));

    if (curr_size >= asize) {
        size_t remain = curr_size - asize;
        if (remain >= MIN_BLOCK_SIZE) {
            PUT(HDRP(bp), PACK(asize, 1));
            PUT(FTRP(bp), PACK(asize, 1));

            void *free_bp = NEXT_BLKP(bp);
            PUT(HDRP(free_bp), PACK(remain, 0));
            PUT(FTRP(free_bp), PACK(remain, 0));
            free_bp = coalesce(free_bp);
            insert_free_block(free_bp);
        }
        return bp;
    }

    if (!next_alloc && asize <= next_size + curr_size) {
        size_t total_size = curr_size + next_size;
        size_t remain = total_size - asize;

        remove_free_block(NEXT_BLKP(bp));

        if (remain >= MIN_BLOCK_SIZE) {
            PUT(HDRP(bp), PACK(asize, 1));
            PUT(FTRP(bp), PACK(asize, 1));

            void *free_bp = NEXT_BLKP(bp);
            PUT(HDRP(free_bp), PACK(remain, 0));
            PUT(FTRP(free_bp), PACK(remain, 0));
            free_bp = coalesce(free_bp);
            insert_free_block(free_bp);
        } else {
            PUT(HDRP(bp), PACK(total_size, 1));
            PUT(FTRP(bp), PACK(total_size, 1));
        }

        return bp;
    }

    //뒤 블록이 free가 아니면, malloc으로 새로운 위치를 받아옴 
    void *new_bp = mm_malloc(size);
    if (new_bp == NULL) return NULL;

    //기존의 내용 복사
    memcpy(new_bp, bp, MIN(size, curr_size - DSIZE));
    mm_free(bp);

    return new_bp;
}

//size_t는 메모리의 크기, 길이, 개수를 표현하는 표준 타입

void *extend_heap(size_t wsize) {
    //mem_sbrk → 공간 확보 → free block 생성 → epilogue 재배치

    size_t size = (wsize % 2) ? (wsize + 1) * WSIZE : wsize * WSIZE;
    size = MAX(size, MIN_BLOCK_SIZE);

    void *bp = mem_sbrk((int)size);
    // void *new_listp = (char *)bp - WSIZE; 
    if (bp == (void *) - 1) return NULL;

    //free Block 생성
    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));

    //Epilogue
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));

    bp = coalesce(bp);
    insert_free_block(bp);
    return bp;
}

// 연결 coalesce: 주변 free block과 합치기만 하고, free list 삽입은 호출자가 한다.
void *coalesce(void *bp) {
    if (bp == NULL) return NULL;

    int prev_alloc = GET_ALLOC(HDRP(PREV_BLKP(bp)));
    int next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));

    if (prev_alloc && next_alloc) {              // case 1
        return bp;
    }

    else if (prev_alloc && !next_alloc) {        // case 2
        void *next_bp = NEXT_BLKP(bp);
        size += GET_SIZE(HDRP(next_bp));
        remove_free_block(next_bp);
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
    }

    else if (!prev_alloc && next_alloc) {        // case 3
        void *prev_bp = PREV_BLKP(bp);
        size += GET_SIZE(HDRP(prev_bp));
        remove_free_block(prev_bp);
        PUT(FTRP(bp), PACK(size, 0));
        PUT(HDRP(prev_bp), PACK(size, 0));
        bp = prev_bp;
    }

    else {                                       // case 4
        void *prev_bp = PREV_BLKP(bp);
        void *next_bp = NEXT_BLKP(bp);
        size += GET_SIZE(HDRP(prev_bp)) +
                GET_SIZE(HDRP(next_bp));
        remove_free_block(prev_bp);
        remove_free_block(next_bp);
        PUT(HDRP(prev_bp), PACK(size, 0));
        PUT(FTRP(next_bp), PACK(size, 0));
        bp = prev_bp;
    }

    return bp;
}

//쓸 수 있는 free block을 explicit free list에서 first-fit으로 찾는다.
void* first_fit(size_t asize) {
    //명시적 free list의 처음부터 순회한다.
    for (void *bp = free_listp; bp != NULL; bp = NEXT_FREE(bp)) {
        if (asize <= GET_SIZE(HDRP(bp))) return bp;
    }
    
    //못 찾았다면, NULL 리턴
    return NULL;
}

//실제 블록 사이즈를 받아와야 함
void place(void *bp, size_t asize) {
    //찾은 블록을 어떻게 사용할지 생각한다.
    size_t ogsize = GET_SIZE(HDRP(bp));
    size_t temp = ogsize - asize;

    remove_free_block(bp);

    //남은 블록이 free list 포인터 2개를 담을 수 없다면 그대로 사용한다.
    if (temp < MIN_BLOCK_SIZE) {
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
    insert_free_block(next_bp);
    return;
}

//free 리스트 LIFO 스타일로 아이템 넣기
static void insert_free_block(void *bp) { 
    PREV_FREE(bp) = NULL;
    NEXT_FREE(bp) = free_listp;
    if (free_listp != NULL) {
        PREV_FREE(free_listp) = bp;
    }

    free_listp = bp;
    return;
}

static void remove_free_block(void *bp) {
    if (bp == NULL) return;

    if (PREV_FREE(bp) != NULL) {
        NEXT_FREE(PREV_FREE(bp)) = NEXT_FREE(bp);
    } else {
        free_listp = NEXT_FREE(bp);
    }

    if (NEXT_FREE(bp) != NULL) {
        PREV_FREE(NEXT_FREE(bp)) = PREV_FREE(bp);
    }

    PREV_FREE(bp) = NULL;
    NEXT_FREE(bp) = NULL;
    return;
}

static size_t adjust_block_size(size_t size) {
    size_t asize = ALIGN(size + DSIZE);
    return MAX(asize, MIN_BLOCK_SIZE);
}
