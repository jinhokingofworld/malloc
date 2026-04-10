#ifndef __CONFIG_H_
#define __CONFIG_H_

/*
 * config.h - malloc lab 설정 파일입니다.
 *
 * 드라이버가 어떤 trace를 읽을지,
 * 성능 점수를 어떤 기준으로 계산할지,
 * 어떤 시간 측정 방식을 사용할지를 여기서 정합니다.
 */

/*
 * 드라이버가 기본 trace 파일을 찾을 디렉터리 경로입니다.
 * 실행 시 `-t` 옵션으로 다른 경로를 줄 수도 있습니다.
 */
#define TRACEDIR "./traces/"

/*
 * 드라이버가 기본으로 사용할 trace 파일 목록입니다.
 * 테스트 세트에 trace를 추가하거나 삭제하고 싶다면 이 목록을 바꾸면 됩니다.
 * 예를 들어 realloc 구현을 요구하지 않으려면 마지막 realloc trace들을 뺄 수 있습니다.
 */
#define DEFAULT_TRACEFILES \
  "amptjp-bal.rep",\
  "cccp-bal.rep",\
  "cp-decl-bal.rep",\
  "expr-bal.rep",\
  "coalescing-bal.rep",\
  "random-bal.rep",\
  "random2-bal.rep",\
  "binary-bal.rep",\
  "binary2-bal.rep",\
  "realloc-bal.rep",\
  "realloc2-bal.rep"

/*
 * 기준 시스템에서 libc malloc이 내는 대략적인 처리량입니다.
 * 이 값은 throughput 점수의 상한선을 정하는 데 사용됩니다.
 * 즉, 이 기준을 넘는다고 해서 점수가 계속 올라가지는 않습니다.
 * 이렇게 해야 속도만 빠르고 allocator로서 품질이 낮은 구현이 과도하게 유리해지지 않습니다.
 */
#define AVG_LIBC_THRUPUT      600E3  /* 600 Kops/sec */

 /* 
  * 최종 성능 지수에서 공간 활용도와 처리량이 차지하는 비중을 정합니다.
  * `UTIL_WEIGHT`는 공간 활용도 비중,
  * `1 - UTIL_WEIGHT`는 처리량 비중입니다.
  */
#define UTIL_WEIGHT .60

/* 
 * 블록 정렬 단위입니다. 바이트 기준이며 보통 4 또는 8입니다.
 */
#define ALIGNMENT 8  

/* 
 * 시뮬레이션 힙의 최대 크기입니다.
 */
#define MAX_HEAP (20*(1<<20))  /* 20 MB */

/*****************************************************************************
 * 아래 USE_xxx 상수 중 정확히 하나만 1로 두어 시간 측정 방식을 선택합니다.
 *****************************************************************************/
#define USE_FCYC   0   /* 사이클 카운터 + K-best 방식(x86, Alpha 전용) */
#define USE_ITIMER 0   /* interval timer 사용(대부분의 Unix 계열) */
#define USE_GETTOD 1   /* gettimeofday 사용(대부분의 Unix 계열) */

#endif /* __CONFIG_H */
