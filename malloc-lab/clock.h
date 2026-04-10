/* CPU 사이클 카운터를 다루는 함수 선언들입니다. */

/* 카운터 측정을 시작합니다. */
void start_counter();

/* 시작 이후 지난 CPU 사이클 수를 구합니다. */
double get_counter();

/* 카운터 자체를 읽는 데 드는 오버헤드를 측정합니다. */
double ovhd();

/* 기본 대기 시간을 사용해 CPU 클럭 속도(MHz)를 추정합니다. */
double mhz(int verbose);

/* 대기 시간을 직접 지정해 CPU 클럭 속도를 더 세밀하게 추정합니다. */
double mhz_full(int verbose, int sleeptime);

/* 타이머 인터럽트 오버헤드를 보정하는 특수 카운터입니다. */

void start_comp_counter();

double get_comp_counter();
