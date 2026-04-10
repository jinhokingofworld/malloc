/****************************
 * 상위 수준 시간 측정 래퍼 함수들
 ****************************/
#include <stdio.h>
#include "fsecs.h"
#include "fcyc.h"
#include "clock.h"
#include "ftimer.h"
#include "config.h"

static double Mhz;  /* 추정한 CPU 클럭 주파수(MHz)입니다. */

extern int verbose; /* mdriver.c의 -v 옵션으로 켜지는 상세 출력 단계입니다. */

/*
 * init_fsecs - 시간 측정 패키지를 초기화합니다.
 */
void init_fsecs(void)
{
    /* 경고를 피하기 위해 먼저 기본값을 넣어 둡니다. */
    Mhz = 0;

#if USE_FCYC
    if (verbose)
	printf("Measuring performance with a cycle counter.\n");

    /* fcyc가 사용할 핵심 측정 파라미터를 설정합니다. */
    set_fcyc_maxsamples(20); 
    set_fcyc_clear_cache(1);
    set_fcyc_compensate(1);
    set_fcyc_epsilon(0.01);
    set_fcyc_k(3);
    Mhz = mhz(verbose > 0);
#elif USE_ITIMER
    if (verbose)
	printf("Measuring performance with the interval timer.\n");
#elif USE_GETTOD
    if (verbose)
	printf("Measuring performance with gettimeofday().\n");
#endif
}

/*
 * fsecs - 함수 f의 실행 시간을 초 단위로 반환합니다.
 */
double fsecs(fsecs_test_funct f, void *argp) 
{
#if USE_FCYC
    /* 사이클 수를 초로 바꾸기 위해 MHz 값을 사용합니다. */
    double cycles = fcyc(f, argp);
    return cycles/(Mhz*1e6);
#elif USE_ITIMER
    return ftimer_itimer(f, argp, 10);
#elif USE_GETTOD
    return ftimer_gettod(f, argp, 10);
#endif 
}

