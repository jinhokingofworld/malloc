/*
 * fcyc.h - 테스트 함수가 사용한 CPU 사이클 수를 추정하는
 * fcyc.c의 함수 선언 모음입니다.
 */

/* 측정 대상 함수는 일반 포인터 하나를 인자로 받습니다. */
typedef void (*test_funct)(void *);

/* 함수 f가 사용한 사이클 수를 추정합니다. */
double fcyc(test_funct f, void* argp);

/*********************************************************
 * 측정 루틴이 사용할 설정값을 조정하는 함수들입니다.
 *********************************************************/

/* 
 * set_fcyc_clear_cache - 1로 설정하면 매 측정 전에 캐시를 비우는 코드를 실행합니다.
 * 기본값은 0입니다.
 */
void set_fcyc_clear_cache(int clear);

/* 
 * set_fcyc_cache_size - 캐시 비우기 실험에 사용할 버퍼 크기를 정합니다.
 * 기본값은 1<<19(512KB)입니다.
 */
void set_fcyc_cache_size(int bytes);

/* 
 * set_fcyc_cache_block - 캐시를 훑을 때 사용할 블록 단위를 정합니다.
 * 기본값은 32입니다.
 */
void set_fcyc_cache_block(int bytes);

/* 
 * set_fcyc_compensate - 1로 설정하면 타이머 인터럽트 오버헤드를 보정하려고 시도합니다.
 * 기본값은 0입니다.
 */
void set_fcyc_compensate(int compensate_arg);

/* 
 * set_fcyc_k - K-best 측정 기법에서 사용할 K 값을 정합니다.
 * 기본값은 3입니다.
 */
void set_fcyc_k(int k);

/* 
 * set_fcyc_maxsamples - 허용 오차 안에서 K-best를 찾기 위해 시도할 최대 샘플 수입니다.
 * 이 한도를 넘으면 지금까지 가장 좋은 샘플을 반환합니다.
 * 기본값은 20입니다.
 */
void set_fcyc_maxsamples(int maxsamples_arg);

/* 
 * set_fcyc_epsilon - K-best 샘플들이 서로 충분히 가깝다고 볼 허용 오차입니다.
 * 기본값은 0.01입니다.
 */
void set_fcyc_epsilon(double epsilon_arg);



