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
#define CHUNKSIZE (1<<12) // 비트 시프트 연산, 2^12

#define MAX(x, y) ((x) > (y) ? (x) : (y))

#define PACK(size, alloc) ((size) | (alloc))

#define GET(p)          (*(unsigned int*)(p))
#define PUT(p, val)     (*(unsigned int*)(p) = (val))

#define GET_SIZE(p)     (GET(p) & ~0x7)
#define PUT(p, val)     (GET(p) & 0x1)

#define HDRP(bp)        ((char *)(bp) - WSIZE)
#define FTRP(bp)        ((char *)(bp) - GET_SIZE(HDRP(bp)) - DSIZE)

#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))

/*********************************************************
 * 학생 안내:
 * 이 구조체에는 팀 정보를 적습니다.
 * 드라이버 프로그램은 이 값을 읽어 제출자 정보를 확인합니다.
 ********************************************************/
team_t team = {
    /* 팀 이름 */
    "ateam",
    /* 첫 번째 팀원의 실명 */
    "Harry Bovik",
    /* 첫 번째 팀원의 이메일 주소 */
    "bovik@cs.cmu.edu",
    /* 두 번째 팀원의 실명(없으면 빈 문자열) */
    "",
    /* 두 번째 팀원의 이메일 주소(없으면 빈 문자열) */
    ""};

/* 블록 주소를 8바이트 경계에 맞추기 위한 정렬 기준입니다. */
#define ALIGNMENT 8

/* 주어진 크기를 가장 가까운 ALIGNMENT 배수로 올림합니다. */
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~0x7)

/* size_t 크기도 정렬 기준에 맞춰 저장하기 위한 상수입니다. */
#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

/*
 * mm_init - malloc 패키지를 초기화합니다.
 *
 * 이 단순 예제는 별도 가용 리스트나 힙 메타데이터를 만들지 않으므로
 * 실제 초기화 작업은 필요하지 않습니다.
 */
int mm_init(void)
{
    
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
    /* 요청 크기와 메타데이터 크기를 더한 뒤 정렬 기준에 맞춥니다. */
    int newsize = ALIGN(size + SIZE_T_SIZE);
    /* 시뮬레이션된 힙을 newsize 바이트만큼 확장합니다. */
    void *p = mem_sbrk(newsize);

    /* 힙 확장에 실패하면 할당 실패를 의미하는 NULL을 반환합니다. */
    if (p == (void *)-1)
        return NULL;
    else
    {
        /* 블록의 맨 앞에 원래 요청한 payload 크기를 저장합니다. */
        *(size_t *)p = size;
        /* 메타데이터 바로 뒤 주소가 사용자 payload의 시작점입니다. */
        return (void *)((char *)p + SIZE_T_SIZE);
    }
}

/*
 * mm_free - 이 예제에서는 free가 아무 동작도 하지 않습니다.
 *
 * 즉, 한 번 확보한 힙 공간은 다시 재사용되지 않습니다.
 */
void mm_free(void *ptr)
{
    /* 단순 구현이므로 free 요청을 무시합니다. */
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
    /* 기존 블록 주소를 따로 보관합니다. */
    void *oldptr = ptr;
    /* 새 블록 주소를 저장할 변수입니다. */
    void *newptr;
    /* 실제로 복사할 바이트 수를 저장합니다. */
    size_t copySize;

    /* 먼저 새 크기에 맞는 블록을 새로 확보합니다. */
    newptr = mm_malloc(size);

    /* 새 블록을 못 만들면 realloc도 실패입니다. */
    if (newptr == NULL)
        return NULL;
    /* 기존 블록 앞 메타데이터에서 원래 크기를 읽어 옵니다. */
    copySize = *(size_t *)((char *)oldptr - SIZE_T_SIZE);
    /* 새 블록이 더 작다면, 넘치는 부분은 복사하지 않도록 크기를 줄입니다. */
    if (size < copySize)
        copySize = size;
    /* 안전한 범위만큼만 이전 내용을 새 블록으로 복사합니다. */
    memcpy(newptr, oldptr, copySize);
    /* 인터페이스상 이전 블록을 해제합니다. */
    mm_free(oldptr);
    /* 새 블록 주소를 반환합니다. */
    return newptr;
}
