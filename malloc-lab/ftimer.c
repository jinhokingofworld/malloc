/*
 * ftimer.c - 함수 실행 시간을 초 단위로 추정하는 도구입니다.
 *
 * 같은 함수를 여러 번 실행해 평균 시간을 구함으로써,
 * 한 번 측정했을 때 생길 수 있는 우연한 오차를 줄입니다.
 *
 * 제공하는 방식은 두 가지입니다.
 * 1. `ftimer_itimer`: Unix interval timer 사용
 * 2. `ftimer_gettod`: `gettimeofday` 사용
 */
#include <stdio.h>
#include <sys/time.h>
#include "ftimer.h"

/* 이 파일 내부에서만 쓰는 보조 함수 선언입니다. */
static void init_etime(void);
static double get_etime(void);

/* 
 * ftimer_itimer - interval timer를 이용해 f(argp)의 실행 시간을 추정합니다.
 * n번 반복 실행한 평균 시간을 반환합니다.
 */
double ftimer_itimer(ftimer_test_funct f, void *argp, int n)
{
    double start, tmeas;
    int i;

    /* 기준 타이머 상태를 초기화합니다. */
    init_etime();
    /* 측정 시작 시점을 기록합니다. */
    start = get_etime();
    /* 같은 함수를 n번 실행해 평균 시간을 낼 준비를 합니다. */
    for (i = 0; i < n; i++) 
	f(argp);
    /* 전체 경과 시간을 구합니다. */
    tmeas = get_etime() - start;
    /* 총 시간을 실행 횟수로 나눠 1회 평균 시간을 반환합니다. */
    return tmeas / n;
}

/* 
 * ftimer_gettod - gettimeofday를 이용해 f(argp)의 실행 시간을 추정합니다.
 * 역시 n번 평균을 반환합니다.
 */
double ftimer_gettod(ftimer_test_funct f, void *argp, int n)
{
    int i;
    struct timeval stv, etv;
    double diff;

    /* 실제 시계 시간을 기준으로 시작 시각을 기록합니다. */
    gettimeofday(&stv, NULL);
    /* 측정 대상 함수를 여러 번 실행합니다. */
    for (i = 0; i < n; i++) 
	f(argp);
    /* 종료 시각을 다시 읽습니다. */
    gettimeofday(&etv,NULL);
    /* 초와 마이크로초 차이를 합쳐 전체 경과 시간을 계산합니다. */
    diff = 1E3*(etv.tv_sec - stv.tv_sec) + 1E-3*(etv.tv_usec-stv.tv_usec);
    /* 평균 시간을 구한 뒤 초 단위 값으로 바꿉니다. */
    diff /= n;
    return (1E-3*diff);
}


/*
 * 아래는 Unix interval timer를 다루는 보조 함수들입니다.
 */

/* 타이머를 충분히 크게 잡기 위한 초기 최대 시간입니다. */
#define MAX_ETIME 86400   

/* 타이머 초기 상태를 저장하는 정적 변수들입니다. */
static struct itimerval first_u; /* 사용자 CPU 시간용 타이머 */
static struct itimerval first_r; /* 실제 경과 시간용 타이머 */
static struct itimerval first_p; /* 프로파일링용 타이머 */

/* 세 종류의 interval timer를 모두 초기화합니다. */
static void init_etime(void)
{
    /* 사용자 CPU 시간 기준 타이머를 MAX_ETIME으로 맞춥니다. */
    first_u.it_interval.tv_sec = 0;
    first_u.it_interval.tv_usec = 0;
    first_u.it_value.tv_sec = MAX_ETIME;
    first_u.it_value.tv_usec = 0;
    setitimer(ITIMER_VIRTUAL, &first_u, NULL);

    /* 실제 시간 기준 타이머도 같은 방식으로 설정합니다. */
    first_r.it_interval.tv_sec = 0;
    first_r.it_interval.tv_usec = 0;
    first_r.it_value.tv_sec = MAX_ETIME;
    first_r.it_value.tv_usec = 0;
    setitimer(ITIMER_REAL, &first_r, NULL);
   
    /* 프로파일링 타이머도 함께 초기화합니다. */
    first_p.it_interval.tv_sec = 0;
    first_p.it_interval.tv_usec = 0;
    first_p.it_value.tv_sec = MAX_ETIME;
    first_p.it_value.tv_usec = 0;
    setitimer(ITIMER_PROF, &first_p, NULL);
}

/* init_etime 호출 이후 경과한 시간을 초 단위로 계산합니다. */
static double get_etime(void) {
    struct itimerval v_curr;
    struct itimerval r_curr;
    struct itimerval p_curr;

    /* 현재 타이머 값들을 읽어 옵니다. */
    getitimer(ITIMER_VIRTUAL, &v_curr);
    getitimer(ITIMER_REAL,&r_curr);
    getitimer(ITIMER_PROF,&p_curr);

    /* 초기값에서 현재값을 빼 실제 경과 시간을 계산합니다. */
    return (double) ((first_p.it_value.tv_sec - r_curr.it_value.tv_sec) +
		     (first_p.it_value.tv_usec - r_curr.it_value.tv_usec)*1e-6);
}



