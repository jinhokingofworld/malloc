/*
 * memlib.c - 메모리 시스템을 흉내 내는 시뮬레이션 모듈입니다.
 *
 * 학생이 만든 malloc과 시스템의 libc malloc을 같은 프로그램 안에서
 * 번갈아 시험할 수 있도록, 가짜 힙 공간을 만들어 관리합니다.
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <sys/mman.h>
#include <string.h>
#include <errno.h>

#include "memlib.h"
#include "config.h"

/* 이 파일 내부에서만 사용하는 힙 상태 변수들입니다. */
static char *mem_start_brk;  /* 힙의 첫 번째 바이트 주소입니다. */
static char *mem_brk;        /* 현재 힙의 끝, 즉 다음 할당이 시작될 위치입니다. */
static char *mem_max_addr;   /* 힙이 커질 수 있는 최대 합법 주소입니다. */

/* 
 * mem_init - 시뮬레이션용 메모리 시스템을 초기화합니다.
 */
void mem_init(void)
{
    /* 가상의 힙으로 사용할 큰 메모리 덩어리를 한 번에 확보합니다. */
    if ((mem_start_brk = (char *)malloc(MAX_HEAP)) == NULL) {
	fprintf(stderr, "mem_init_vm: malloc error\n");
	exit(1);
    }

    /* 확보한 메모리의 끝 주소를 최대 경계로 저장합니다. */
    mem_max_addr = mem_start_brk + MAX_HEAP;
    /* 처음에는 비어 있는 힙이므로 brk는 시작점과 같습니다. */
    mem_brk = mem_start_brk;
}

/* 
 * mem_deinit - 시뮬레이션용 힙 메모리를 반납합니다.
 */
void mem_deinit(void)
{
    free(mem_start_brk);
}

/*
 * mem_reset_brk - brk 포인터를 처음 위치로 되돌려 빈 힙 상태로 만듭니다.
 */
void mem_reset_brk()
{
    mem_brk = mem_start_brk;
}

/* 
 * mem_sbrk - sbrk를 단순하게 흉내 낸 함수입니다.
 *
 * 힙을 incr 바이트만큼 늘리고, 새로 확보된 구간의 시작 주소를 반환합니다.
 * 이 모형에서는 힙을 줄이는 동작은 지원하지 않습니다.
 */
void *mem_sbrk(int incr) 
{
    /* 확장 전 힙 끝 주소를 저장해 두었다가 반환값으로 사용합니다. */
    char *old_brk = mem_brk;

    /* 음수 요청이거나 최대 힙 경계를 넘는 요청은 실패입니다. */
    if ( (incr < 0) || ((mem_brk + incr) > mem_max_addr)) {
	errno = ENOMEM;
	fprintf(stderr, "ERROR: mem_sbrk failed. Ran out of memory...\n");
	return (void *)-1;
    }
    /* 힙 끝을 incr만큼 앞으로 옮겨 실제 확장을 반영합니다. */
    mem_brk += incr;
    /* old_brk부터 새 공간을 사용할 수 있습니다. */
    return (void *)old_brk;
}

/*
 * mem_heap_lo - 힙의 첫 바이트 주소를 반환합니다.
 */
void *mem_heap_lo()
{
    return (void *)mem_start_brk;
}

/* 
 * mem_heap_hi - 현재 힙에서 마지막으로 유효한 바이트 주소를 반환합니다.
 */
void *mem_heap_hi()
{
    return (void *)(mem_brk - 1);
}

/*
 * mem_heapsize() - 현재 힙 전체 크기를 바이트 단위로 반환합니다.
 */
size_t mem_heapsize() 
{
    return (size_t)(mem_brk - mem_start_brk);
}

/*
 * mem_pagesize() - 시스템 페이지 크기를 반환합니다.
 */
size_t mem_pagesize()
{
    return (size_t)getpagesize();
}
