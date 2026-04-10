/*
 * fcyc.c - 함수 f가 사용한 시간을 CPU 사이클 단위로 추정합니다.
 *
 * 한 번만 재지 않고 여러 번 측정한 뒤,
 * 그중 가장 작은 값들(K-best)이 서로 충분히 비슷해질 때까지 반복합니다.
 * 이렇게 하면 우연한 방해 요소 때문에 값이 튀는 문제를 줄일 수 있습니다.
 */
#include <stdlib.h>
#include <sys/times.h>
#include <stdio.h>

#include "fcyc.h"
#include "clock.h"

/* 기본 측정 설정값들입니다. */
#define K 3                  /* K-best 기법에서 유지할 최소값 개수 */
#define MAXSAMPLES 20        /* 이 횟수 안에 수렴하지 않으면 측정을 종료 */
#define EPSILON 0.01         /* K개의 최소값이 이 정도로 가까우면 수렴으로 판단 */
#define COMPENSATE 0         /* 1이면 타이머 인터럽트 오버헤드 보정을 시도 */
#define CLEAR_CACHE 0        /* 1이면 매 측정 전 캐시를 비우는 코드 실행 */
#define CACHE_BYTES (1<<19)  /* 캐시 비우기에 사용할 최대 버퍼 크기 */
#define CACHE_BLOCK 32       /* 캐시를 건너뛰며 읽을 때 사용할 블록 크기 */

static int kbest = K;
static int maxsamples = MAXSAMPLES;
static double epsilon = EPSILON;
static int compensate = COMPENSATE;
static int clear_cache = CLEAR_CACHE;
static int cache_bytes = CACHE_BYTES;
static int cache_block = CACHE_BLOCK;

static int *cache_buf = NULL;

static double *values = NULL;
static int samplecount = 0;

/* 디버깅용 옵션입니다. */
#define KEEP_VALS 0
#define KEEP_SAMPLES 0

#if KEEP_SAMPLES
static double *samples = NULL;
#endif

/* 
 * init_sampler - 새 측정 묶음을 시작할 때 샘플 저장소를 초기화합니다.
 */
static void init_sampler()
{
    /* 이전 측정값이 남아 있으면 해제하고 새 배열을 만듭니다. */
    if (values)
	free(values);
    values = calloc(kbest, sizeof(double));
#if KEEP_SAMPLES
    if (samples)
	free(samples);
    /* wraparound 분석을 위해 여유 공간까지 포함해 저장합니다. */
    samples = calloc(maxsamples+kbest, sizeof(double));
#endif
    /* 이번 측정 묶음에서는 아직 샘플이 하나도 없습니다. */
    samplecount = 0;
}

/* 
 * add_sample - 새 측정값 하나를 K-best 후보 집합에 반영합니다.
 */
static void add_sample(double val)
{
    int pos = 0;
    /* 아직 K개를 다 못 채웠다면 뒤에 그냥 추가합니다. */
    if (samplecount < kbest) {
	pos = samplecount;
	values[pos] = val;
    /* 이미 K개가 차 있으면, 현재 최악값보다 더 좋은 경우에만 교체합니다. */
    } else if (val < values[kbest-1]) {
	pos = kbest-1;
	values[pos] = val;
    }
#if KEEP_SAMPLES
    samples[samplecount] = val;
#endif
    samplecount++;
    /* 새 값이 제자리를 찾도록 간단한 삽입 정렬을 수행합니다. */
    while (pos > 0 && values[pos-1] > values[pos]) {
	double temp = values[pos-1];
	values[pos-1] = values[pos];
	values[pos] = temp;
	pos--;
    }
}

/* 
 * has_converged - K개의 최소 측정값이 epsilon 안으로 충분히 모였는지 검사합니다.
 */
static int has_converged()
{
    return
	(samplecount >= kbest) &&
	((1 + epsilon)*values[0] >= values[kbest-1]);
}

/* 
 * clear - 측정 전에 캐시 영향을 줄이기 위해 큰 버퍼를 훑습니다.
 */
static volatile int sink = 0;

static void clear()
{
    /* 결과가 최적화로 사라지지 않게 누적용 변수를 사용합니다. */
    int x = sink;
    int *cptr, *cend;
    int incr = cache_block/sizeof(int);
    /* 캐시 비우기용 버퍼가 없으면 한 번만 할당합니다. */
    if (!cache_buf) {
	cache_buf = malloc(cache_bytes);
	if (!cache_buf) {
	    fprintf(stderr, "Fatal error.  Malloc returned null when trying to clear cache\n");
	    exit(1);
	}
    }
    cptr = (int *) cache_buf;
    cend = cptr + cache_bytes/sizeof(int);
    /* 일정 간격으로 버퍼를 읽으며 캐시를 오염시킵니다. */
    while (cptr < cend) {
	x += *cptr;
	cptr += incr;
    }
    sink = x;
}

/*
 * fcyc - K-best 기법으로 함수 f의 실행 사이클 수를 추정합니다.
 */
double fcyc(test_funct f, void *argp)
{
    double result;
    /* 새 측정을 시작하기 전에 샘플 저장 공간을 준비합니다. */
    init_sampler();
    /* 보정 모드면 인터럽트 오버헤드를 고려한 카운터를 사용합니다. */
    if (compensate) {
	do {
	    double cyc;
	    /* 옵션에 따라 매 측정 전에 캐시를 비웁니다. */
	    if (clear_cache)
		clear();
	    /* 보정용 카운터로 측정을 시작합니다. */
	    start_comp_counter();
	    f(argp);
	    cyc = get_comp_counter();
	    /* 이번 측정 결과를 K-best 후보에 반영합니다. */
	    add_sample(cyc);
	} while (!has_converged() && samplecount < maxsamples);
    } else {
	do {
	    double cyc;
	    /* 캐시 영향을 줄이고 싶다면 먼저 캐시 비우기 코드를 실행합니다. */
	    if (clear_cache)
		clear();
	    /* 일반 사이클 카운터로 측정을 시작합니다. */
	    start_counter();
	    f(argp);
	    cyc = get_counter();
	    /* 측정값을 저장하고 수렴 여부를 다음 반복에서 판단합니다. */
	    add_sample(cyc);
	} while (!has_converged() && samplecount < maxsamples);
    }
#ifdef DEBUG
    {
	int i;
	printf(" %d smallest values: [", kbest);
	for (i = 0; i < kbest; i++)
	    printf("%.0f%s", values[i], i==kbest-1 ? "]\n" : ", ");
    }
#endif
    /* 정렬된 값 중 가장 작은 값이 최종 추정치가 됩니다. */
    result = values[0];
#if !KEEP_VALS
    /* 측정이 끝났으면 임시 저장 배열을 해제합니다. */
    free(values); 
    values = NULL;
#endif
    return result;  
}


/*************************************************************
 * 아래 함수들은 측정 루틴의 설정값을 바꾸는 역할을 합니다.
 ************************************************************/

/* 
 * set_fcyc_clear_cache - 1이면 매 측정 전에 캐시 비우기 코드를 실행합니다.
 */
void set_fcyc_clear_cache(int clear)
{
    clear_cache = clear;
}

/* 
 * set_fcyc_cache_size - 캐시 비우기용 버퍼 크기를 설정합니다.
 */
void set_fcyc_cache_size(int bytes)
{
    /* 크기가 달라질 때만 갱신하고, 기존 버퍼는 다시 만들 수 있게 비웁니다. */
    if (bytes != cache_bytes) {
	cache_bytes = bytes;
	if (cache_buf) {
	    free(cache_buf);
	    cache_buf = NULL;
	}
    }
}

/* 
 * set_fcyc_cache_block - 캐시 버퍼를 훑을 때 건너뛸 블록 크기를 설정합니다.
 */
void set_fcyc_cache_block(int bytes) {
    cache_block = bytes;
}


/* 
 * set_fcyc_compensate - 인터럽트 오버헤드 보정 사용 여부를 설정합니다.
 */
void set_fcyc_compensate(int compensate_arg)
{
    compensate = compensate_arg;
}

/* 
 * set_fcyc_k - K-best 기법에서 유지할 최소값 개수를 설정합니다.
 */
void set_fcyc_k(int k)
{
    kbest = k;
}

/* 
 * set_fcyc_maxsamples - 수렴을 기다리며 시도할 최대 샘플 수를 설정합니다.
 */
void set_fcyc_maxsamples(int maxsamples_arg)
{
    maxsamples = maxsamples_arg;
}

/* 
 * set_fcyc_epsilon - K-best 값들이 얼마나 가까워야 수렴으로 볼지 정합니다.
 */
void set_fcyc_epsilon(double epsilon_arg)
{
    epsilon = epsilon_arg;
}




