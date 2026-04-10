/* 
 * 함수 실행 시간을 재는 함수 타입과 선언들입니다.
 */
typedef void (*ftimer_test_funct)(void *); 

/* Unix interval timer로 f(argp)의 실행 시간을 추정하고 n번 평균을 반환합니다. */
double ftimer_itimer(ftimer_test_funct f, void *argp, int n);


/* gettimeofday로 f(argp)의 실행 시간을 추정하고 n번 평균을 반환합니다. */
double ftimer_gettod(ftimer_test_funct f, void *argp, int n);
