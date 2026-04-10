/* 
 * clock.c - 여러 플랫폼에서 CPU 사이클 카운터를 다루는 함수들입니다.
 *
 * 가능한 환경에서는 프로세서의 사이클 카운터를 직접 읽고,
 * 지원하지 않는 환경에서는 오류를 안내합니다.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/times.h>
#include "clock.h"


/******************************************************* 
 * 플랫폼별로 달라지는 함수들입니다.
 *
 * 참고:
 * `__i386__`, `__alpha` 같은 상수는
 * GCC가 전처리기를 호출할 때 자동으로 정의합니다.
 *******************************************************/

#if defined(__i386__)  
/*******************************************************
 * x86(Pentium 계열)용 start_counter(), get_counter() 구현입니다.
 *******************************************************/


/* x86 사이클 카운터용 시작 상태를 저장합니다. */
static unsigned cyc_hi = 0;
static unsigned cyc_lo = 0;


/* 현재 사이클 카운터 값을 상위/하위 32비트로 읽어 옵니다.
   x86의 `rdtsc` 어셈블리 명령을 사용합니다. */
void access_counter(unsigned *hi, unsigned *lo)
{
    asm("rdtsc; movl %%edx,%0; movl %%eax,%1"   /* 카운터를 읽고 */
	: "=r" (*hi), "=r" (*lo)                /* 상/하위 값을 출력 변수에 담습니다. */
	: /* 입력 없음 */                      /* 입력 피연산자는 없습니다. */
	: "%edx", "%eax");
}

/* 현재 카운터 값을 시작 시점으로 기록합니다. */
void start_counter()
{
    access_counter(&cyc_hi, &cyc_lo);
}

/* 마지막 start_counter 이후 경과한 사이클 수를 계산합니다. */
double get_counter()
{
    unsigned ncyc_hi, ncyc_lo;
    unsigned hi, lo, borrow;
    double result;

    /* 현재 카운터 값을 다시 읽습니다. */
    access_counter(&ncyc_hi, &ncyc_lo);

    /* 64비트 값처럼 다뤄서 이전 시점과 차이를 계산합니다. */
    lo = ncyc_lo - cyc_lo;
    borrow = lo > ncyc_lo;
    hi = ncyc_hi - cyc_hi - borrow;
    result = (double) hi * (1 << 30) * 4 + lo;
    if (result < 0) {
	fprintf(stderr, "Error: counter returns neg value: %.0f\n", result);
    }
    return result;
}
#elif defined(__alpha)

/****************************************************
 * Alpha용 start_counter(), get_counter() 구현입니다.
 ***************************************************/

/* Alpha에서도 시작 카운터 값을 저장할 변수를 둡니다. */
static unsigned cyc_hi = 0;
static unsigned cyc_lo = 0;


/* Alpha의 사이클 타이머를 이용해 사이클 수를 측정합니다. */

/*
 * counterRoutine 배열에는 Alpha 사이클 카운터를 읽는 기계어 명령이 들어 있습니다.
 * `rpcc` 명령으로 64비트 카운터를 읽는데,
 * 아래 32비트는 현재 프로세스가 사용한 사이클 수,
 * 위 32비트는 벽시계 기준 사이클 수를 뜻합니다.
 *
 * 여기서는 아래 32비트만 unsigned int로 꺼내 사용자 영역 카운터로 사용합니다.
 * 단, 이 카운터는 표현 가능한 범위가 짧아서 오래 측정하기에는 적합하지 않습니다.
 */
static unsigned int counterRoutine[] =
{
    0x601fc000u,
    0x401f0000u,
    0x6bfa8001u
};

/* 위 기계어 배열을 함수 포인터처럼 호출할 수 있게 바꿉니다. */
static unsigned int (*counter)(void)= (void *)counterRoutine;


void start_counter()
{
    /* 현재 카운터 값을 시작 시점으로 저장합니다. */
    cyc_hi = 0;
    cyc_lo = counter();
}

double get_counter()
{
    unsigned ncyc_hi, ncyc_lo;
    unsigned hi, lo, borrow;
    double result;
    ncyc_lo = counter();
    ncyc_hi = 0;
    lo = ncyc_lo - cyc_lo;
    borrow = lo > ncyc_lo;
    hi = ncyc_hi - cyc_hi - borrow;
    result = (double) hi * (1 << 30) * 4 + lo;
    if (result < 0) {
	fprintf(stderr, "Error: Cycle counter returning negative value: %.0f\n", result);
    }
    return result;
}

#else

/****************************************************************
 * 아직 사이클 카운터를 구현하지 않은 다른 플랫폼용 코드입니다.
 * 이런 환경에서는 적절한 측정 방식을 고르라고 안내하고 종료합니다.
 ***************************************************************/

void start_counter()
{
    printf("ERROR: You are trying to use a start_counter routine in clock.c\n");
    printf("that has not been implemented yet on this platform.\n");
    printf("Please choose another timing package in config.h.\n");
    exit(1);
}

double get_counter() 
{
    printf("ERROR: You are trying to use a get_counter routine in clock.c\n");
    printf("that has not been implemented yet on this platform.\n");
    printf("Please choose another timing package in config.h.\n");
    exit(1);
}
#endif




/*******************************
 * 플랫폼과 무관하게 공통으로 쓰는 함수들입니다.
 ******************************/
double ovhd()
{
    /* 캐시 영향을 줄이기 위해 두 번 실행한 뒤 값을 사용합니다. */
    int i;
    double result;

    for (i = 0; i < 2; i++) {
	start_counter();
	result = get_counter();
    }
    return result;
}

/* CPU가 sleeptime초 동안 몇 사이클 진행하는지 재서 MHz를 추정합니다. */
double mhz_full(int verbose, int sleeptime)
{
    double rate;

    /* 수면 전후의 카운터 차이로 평균 클럭을 계산합니다. */
    start_counter();
    sleep(sleeptime);
    rate = get_counter() / (1e6*sleeptime);
    if (verbose) 
	printf("Processor clock rate ~= %.1f MHz\n", rate);
    return rate;
}
/* 기본 대기 시간 2초를 쓰는 간단한 버전입니다. */
double mhz(int verbose)
{
    return mhz_full(verbose, 2);
}

/* 타이머 인터럽트 오버헤드를 보정하는 특수 카운터 관련 변수들입니다. */

static double cyc_per_tick = 0.0;

#define NEVENT 100
#define THRESHOLD 1000
#define RECORDTHRESH 3000

/* 타이머 인터럽트가 몇 사이클 정도를 잡아먹는지 대략 추정합니다. */
static void callibrate(int verbose)
{
    double oldt;
    struct tms t;
    clock_t oldc;
    int e = 0;

    times(&t);
    oldc = t.tms_utime;
    start_counter();
    oldt = get_counter();
    /* 유저 시간 tick과 사이클 증가량을 비교하며 보정값을 찾습니다. */
    while (e <NEVENT) {
	double newt = get_counter();

	if (newt-oldt >= THRESHOLD) {
	    clock_t newc;
	    times(&t);
	    newc = t.tms_utime;
	    if (newc > oldc) {
		double cpt = (newt-oldt)/(newc-oldc);
		if ((cyc_per_tick == 0.0 || cyc_per_tick > cpt) && cpt > RECORDTHRESH)
		    cyc_per_tick = cpt;
		/*
		  디버깅이 필요하면 각 이벤트가 몇 사이클, 몇 tick이었는지 출력할 수 있습니다.
		*/
		e++;
		oldc = newc;
	    }
	    oldt = newt;
	}
    }
    if (verbose)
	printf("Setting cyc_per_tick to %f\n", cyc_per_tick);
}

static clock_t start_tick = 0;

void start_comp_counter() 
{
    struct tms t;

    /* 보정값이 아직 없으면 먼저 추정합니다. */
    if (cyc_per_tick == 0.0)
	callibrate(0);
    /* 보정용 시작 tick과 시작 카운터를 함께 저장합니다. */
    times(&t);
    start_tick = t.tms_utime;
    start_counter();
}

double get_comp_counter() 
{
    /* 원시 카운터 값을 먼저 읽습니다. */
    double time = get_counter();
    double ctime;
    struct tms t;
    clock_t ticks;

    /* 측정 구간 동안 지난 user tick 수를 구합니다. */
    times(&t);
    ticks = t.tms_utime - start_tick;
    /* tick에 해당하는 예상 오버헤드를 빼서 보정된 값을 만듭니다. */
    ctime = time - ticks*cyc_per_tick;
    /*
      디버깅 시에는 원시 사이클 수와 보정 후 값을 함께 출력할 수 있습니다.
    */
    return ctime;
}
