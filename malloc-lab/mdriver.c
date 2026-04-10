/*
 * mdriver.c - CS:APP Malloc Lab 채점/테스트 드라이버입니다.
 *
 * 여러 trace 파일에 들어 있는 메모리 요청 시나리오를 순서대로 실행해 보면서,
 * `mm.c`에 작성한 `malloc`, `free`, `realloc` 구현이
 * 올바르게 동작하는지와 얼마나 효율적인지를 검사합니다.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <float.h>
#include <time.h>

extern char *optarg; // getopt가 옵션 인자를 전달할 때 사용하는 전역 포인터입니다.

#include "mm.h"
#include "memlib.h"
#include "fsecs.h"
#include "config.h"

/**********************
 * 상수와 매크로
 **********************/

/* 자주 쓰는 보조 상수들입니다. */
#define MAXLINE 1024	   /* 한 줄 문자열이 가질 최대 길이입니다. */
#define HDRLINES 4		   /* trace 파일 맨 앞의 헤더 줄 수입니다. */
#define LINENUM(i) (i + 5) /* trace의 i번째 요청을 실제 파일 줄 번호로 바꿉니다. */

/* 포인터 p가 ALIGNMENT 바이트 경계에 맞게 정렬되어 있으면 참입니다. */
#define IS_ALIGNED(p) ((((unsigned int)(p)) % ALIGNMENT) == 0)

/******************************
 * 핵심 구조체 자료형
 *****************************/

/* 각 할당 블록 payload의 주소 범위를 기록하는 연결 리스트 노드입니다. */
typedef struct range_t
{
	char *lo;			  /* payload 시작 주소 */
	char *hi;			  /* payload 끝 주소 */
	struct range_t *next; /* 다음 노드 */
} range_t;

/* trace 안의 단일 메모리 요청 하나를 표현합니다. */
typedef struct
{
	enum
	{
		ALLOC,
		FREE,
		REALLOC
	} type;	   /* 요청 종류: alloc, free, realloc */
	int index; /* 나중에 free/realloc이 참조할 블록 번호 */
	int size;  /* alloc/realloc 요청 크기(바이트) */
} traceop_t;

/* trace 파일 하나를 메모리 안에 읽어 둔 결과입니다. */
typedef struct
{
	int sugg_heapsize;	 /* 권장 힙 크기(현재는 사용하지 않음) */
	int num_ids;		 /* alloc/realloc이 사용할 ID 개수 */
	int num_ops;		 /* 전체 요청 개수 */
	int weight;			 /* trace 가중치(현재는 사용하지 않음) */
	traceop_t *ops;		 /* 요청 배열 */
	char **blocks;		 /* 각 ID가 현재 가리키는 블록 주소 배열 */
	size_t *block_sizes; /* 각 ID 블록의 payload 크기 배열 */
} trace_t;

/*
 * 속도 측정 함수(xxx_speed)에 넘길 인자를 묶어 둔 구조체입니다.
 * fcyc는 인자를 포인터 하나만 받을 수 있어서 이렇게 포장합니다.
 */
typedef struct
{
	trace_t *trace;
	range_t *ranges;
} speed_t;

/* 한 trace에서 어떤 malloc 구현이 낸 핵심 결과를 요약합니다. */
typedef struct
{
	/* libc와 학생 구현(mm.c) 모두에 대해 기록되는 값입니다. */
	double ops;	 /* 이 trace에 포함된 전체 연산 수 */
	int valid;	 /* allocator가 이 trace를 올바르게 처리했는지 여부 */
	double secs; /* 이 trace를 처리하는 데 걸린 시간(초) */

	/* 학생 구현(mm.c)에 대해서만 의미 있는 값입니다. */
	double util; /* 공간 활용도(libc 측정에서는 항상 0) */

	/* 참고: valid가 참일 때만 secs와 util이 의미 있습니다. */
} stats_t;

/********************
 * 전역 변수
 *******************/
int verbose = 0;	   /* 상세 출력 수준을 나타내는 전역 플래그입니다. */
static int errors = 0; /* 학생 malloc 실행 중 발견한 오류 개수입니다. */
char msg[MAXLINE];	   /* 에러 메시지를 임시로 조합할 버퍼입니다. */

/* 기본 trace 파일들을 찾을 디렉터리입니다. */
static char tracedir[MAXLINE] = TRACEDIR;

/* 기본으로 사용할 trace 파일 이름 목록입니다. */
static char *default_tracefiles[] = {
	DEFAULT_TRACEFILES, NULL};

/*********************
 * 함수 선언
 *********************/

/* range 리스트를 다루는 함수들입니다. */
static int add_range(range_t **ranges, char *lo, int size,
					 int tracenum, int opnum);
static void remove_range(range_t **ranges, char *lo);
static void clear_ranges(range_t **ranges);

/* trace 파일을 읽고 해제하는 함수들입니다. */
static trace_t *read_trace(char *tracedir, char *filename);
static void free_trace(trace_t *trace);

/* libc malloc의 정합성과 속도를 평가하는 함수들입니다. */
static int eval_libc_valid(trace_t *trace, int tracenum);
static void eval_libc_speed(void *ptr);

/* 학생 구현(mm.c)의 정합성, 공간 활용도, 속도를 평가하는 함수들입니다. */
static int eval_mm_valid(trace_t *trace, int tracenum, range_t **ranges);
static double eval_mm_util(trace_t *trace, int tracenum, range_t **ranges);
static void eval_mm_speed(void *ptr);

/* 기타 보조 함수들입니다. */
static void printresults(int n, stats_t *stats);
static void usage(void);
static void unix_error(char *msg);
static void malloc_error(int tracenum, int opnum, char *msg);
static void app_error(char *msg);

/**************
 * 메인 루틴
 **************/
int main(int argc, char **argv)
{
	int i;
	int c;
	char **tracefiles = NULL;	/* NULL로 끝나는 trace 파일 이름 배열입니다. */
	int num_tracefiles = 0;		/* 위 배열에 들어 있는 trace 개수입니다. */
	trace_t *trace = NULL;		/* 현재 읽고 있는 trace 하나를 담습니다. */
	range_t *ranges = NULL;		/* 현재 trace에서 할당 블록 범위를 추적합니다. */
	stats_t *libc_stats = NULL; /* 각 trace에 대한 libc 측정 결과입니다. */
	stats_t *mm_stats = NULL;	/* 각 trace에 대한 학생 구현 측정 결과입니다. */
	speed_t speed_params;		/* 속도 측정 함수에 넘길 입력 묶음입니다. */

	int team_check = 1; /* 1이면 팀 정보가 비어 있는지 검사합니다. */
	int run_libc = 0;	/* 1이면 libc malloc도 함께 측정합니다. */
	int autograder = 0; /* 1이면 autograder용 요약 출력도 만듭니다. */

	/* 최종 성능 지수를 계산할 때 사용할 임시 변수들입니다. */
	double secs, ops, util, avg_mm_util, avg_mm_throughput, p1, p2, perfindex;
	int numcorrect;

	/*
	 * 명령행 옵션을 읽어서 실행 방식을 결정합니다.
	 */
	while ((c = getopt(argc, argv, "f:t:hvVgal")) != EOF)
	{
		/* 현재 읽은 옵션 문자를 그대로 출력하는 디버깅용 코드입니다. */
		printf("getopt returned: %d\n", c); // 디버깅용 출력 추가

		switch (c)
		{
		case 'g': /* autograder가 읽을 간단 요약도 함께 출력합니다. */
			autograder = 1;
			break;
		case 'f': /* 현재 디렉터리 기준 특정 trace 하나만 사용합니다. */
			num_tracefiles = 1;
			if ((tracefiles = realloc(tracefiles, 2 * sizeof(char *))) == NULL)
				unix_error("ERROR: realloc failed in main");
			/* 단일 파일 지정 시에는 현재 디렉터리 기준으로 읽습니다. */
			strcpy(tracedir, "./");
			tracefiles[0] = strdup(optarg);
			tracefiles[1] = NULL;
			break;
		case 't':					 /* 기본 trace 디렉터리를 바꿉니다. */
			if (num_tracefiles == 1) /* 이미 -f가 있었다면 단일 파일 설정을 우선합니다. */
				break;
			strcpy(tracedir, optarg);
			if (tracedir[strlen(tracedir) - 1] != '/')
				strcat(tracedir, "/"); /* 뒤에 /를 붙여 경로 결합을 쉽게 만듭니다. */
			break;
		case 'a': /* 팀 정보 검사를 생략합니다. */
			team_check = 0;
			break;
		case 'l': /* 학생 구현 외에 libc malloc도 함께 실행합니다. */
			run_libc = 1;
			break;
		case 'v': /* trace별 상세 결과를 출력합니다. */
			verbose = 1;
			break;
		case 'V': /* -v보다 더 자세한 디버그 정보를 출력합니다. */
			verbose = 2;
			break;
		case 'h': /* 사용법을 출력하고 종료합니다. */
			usage();
			exit(0);
		default:
			usage();
			exit(1);
		}
	}

	/*
	 * 팀 정보가 채워졌는지 확인하고 화면에 출력합니다.
	 */
	if (team_check)
	{
		/* 기본 템플릿 그대로 비어 있으면 제출 정보가 없다고 판단합니다. */
		if (!strcmp(team.teamname, ""))
		{
			printf("ERROR: Please provide the information about your team in mm.c.\n");
			exit(1);
		}
		else
			printf("Team Name:%s\n", team.teamname);
		if ((*team.name1 == '\0') || (*team.id1 == '\0'))
		{
			printf("ERROR.  You must fill in all team member 1 fields!\n");
			exit(1);
		}
		else
			printf("Member 1 :%s:%s\n", team.name1, team.id1);

		/* 2번 팀원 정보는 이름과 ID가 둘 다 있거나 둘 다 없어야 합니다. */
		if (((*team.name2 != '\0') && (*team.id2 == '\0')) ||
			((*team.name2 == '\0') && (*team.id2 != '\0')))
		{
			printf("ERROR.  You must fill in all or none of the team member 2 ID fields!\n");
			exit(1);
		}
		else if (*team.name2 != '\0')
			printf("Member 2 :%s:%s\n", team.name2, team.id2);
	}

	/*
	 * -f 옵션이 없으면 미리 정해 둔 기본 trace 묶음을 전부 사용합니다.
	 */
	if (tracefiles == NULL)
	{
		tracefiles = default_tracefiles;
		num_tracefiles = sizeof(default_tracefiles) / sizeof(char *) - 1;
		printf("Using default tracefiles in %s\n", tracedir);
	}

	/* 시간 측정 모듈을 초기화합니다. */
	init_fsecs();

	/*
	 * 옵션에 따라 libc malloc도 먼저 실행하고 평가합니다.
	 */
	if (run_libc)
	{
		if (verbose > 1)
			printf("\nTesting libc malloc\n");

		/* trace마다 결과를 저장할 stats_t 배열을 준비합니다. */
		libc_stats = (stats_t *)calloc(num_tracefiles, sizeof(stats_t));
		if (libc_stats == NULL)
			unix_error("libc_stats calloc in main failed");

		/* 각 trace에 대해 정합성과 속도를 차례대로 측정합니다. */
		for (i = 0; i < num_tracefiles; i++)
		{
			/* trace 파일 하나를 읽어 메모리 안 구조체로 바꿉니다. */
			trace = read_trace(tracedir, tracefiles[i]);
			libc_stats[i].ops = trace->num_ops;
			if (verbose > 1)
				printf("Checking libc malloc for correctness, ");
			/* 먼저 이 trace를 끝까지 문제없이 처리하는지 확인합니다. */
			libc_stats[i].valid = eval_libc_valid(trace, i);
			if (libc_stats[i].valid)
			{
				/* 올바르게 동작했을 때만 속도를 재는 것이 의미가 있습니다. */
				speed_params.trace = trace;
				if (verbose > 1)
					printf("and performance.\n");
				libc_stats[i].secs = fsecs(eval_libc_speed, &speed_params);
			}
			/* 다음 trace를 위해 읽어 둔 데이터를 해제합니다. */
			free_trace(trace);
		}

		/* 요청 시 libc 결과를 표 형태로 출력합니다. */
		if (verbose)
		{
			printf("\nResults for libc malloc:\n");
			printresults(num_tracefiles, libc_stats);
		}
	}

	/*
	 * 학생이 작성한 mm 패키지는 항상 실행하고 평가합니다.
	 */
	if (verbose > 1)
		printf("\nTesting mm malloc\n");

	/* trace별 학생 구현 결과를 저장할 배열을 준비합니다. */
	mm_stats = (stats_t *)calloc(num_tracefiles, sizeof(stats_t));
	if (mm_stats == NULL)
		unix_error("mm_stats calloc in main failed");

	/* memlib.c의 가상 힙 시스템을 초기화합니다. */
	mem_init();

	/* 각 trace에 대해 정합성, 공간 활용도, 속도를 차례대로 검사합니다. */
	for (i = 0; i < num_tracefiles; i++)
	{
		trace = read_trace(tracedir, tracefiles[i]);
		mm_stats[i].ops = trace->num_ops;
		if (verbose > 1)
			printf("Checking mm_malloc for correctness, ");
		/* 먼저 동작이 맞는지 검사합니다. */
		mm_stats[i].valid = eval_mm_valid(trace, i, &ranges);
		if (mm_stats[i].valid)
		{
			if (verbose > 1)
				printf("efficiency, ");
			/* 올바른 구현에 대해서만 공간 활용도를 계산합니다. */
			mm_stats[i].util = eval_mm_util(trace, i, &ranges);
			speed_params.trace = trace;
			speed_params.ranges = ranges;
			if (verbose > 1)
				printf("and performance.\n");
			/* 마지막으로 처리 시간을 재어 throughput 계산에 사용합니다. */
			mm_stats[i].secs = fsecs(eval_mm_speed, &speed_params);
		}
		free_trace(trace);
	}

	/* 요청 시 학생 구현 결과도 표로 출력합니다. */
	if (verbose)
	{
		printf("\nResults for mm malloc:\n");
		printresults(num_tracefiles, mm_stats);
		printf("\n");
	}

	/*
	 * 학생 구현의 전체 평균 통계를 누적합니다.
	 */
	secs = 0;
	ops = 0;
	util = 0;
	numcorrect = 0;
	for (i = 0; i < num_tracefiles; i++)
	{
		/* trace별 결과를 모두 더해 전체 평균과 합계를 만들 준비를 합니다. */
		secs += mm_stats[i].secs;
		ops += mm_stats[i].ops;
		util += mm_stats[i].util;
		if (mm_stats[i].valid)
			numcorrect++;
	}
	avg_mm_util = util / num_tracefiles;

	/*
	 * 최종 성능 지수를 계산해 출력합니다.
	 */
	if (errors == 0)
	{
		/* 총 연산 수를 총 시간으로 나누어 평균 처리량을 구합니다. */
		avg_mm_throughput = ops / secs;

		/* p1은 공간 활용도 점수 부분입니다. */
		p1 = UTIL_WEIGHT * avg_mm_util;
		if (avg_mm_throughput > AVG_LIBC_THRUPUT)
		{
			/* 기준 처리량을 넘으면 throughput 점수는 최대치로 제한됩니다. */
			p2 = (double)(1.0 - UTIL_WEIGHT);
		}
		else
		{
			/* 기준보다 느리면 비율만큼만 throughput 점수를 받습니다. */
			p2 = ((double)(1.0 - UTIL_WEIGHT)) *
				 (avg_mm_throughput / AVG_LIBC_THRUPUT);
		}

		/* 두 점수를 더해 100점 만점 기준 성능 지수를 만듭니다. */
		perfindex = (p1 + p2) * 100.0;
		printf("Perf index = %.0f (util) + %.0f (thru) = %.0f/100\n",
			   p1 * 100,
			   p2 * 100,
			   perfindex);
	}
	else
	{ /* 오류가 하나라도 있으면 성능 지수는 0으로 처리합니다. */
		perfindex = 0.0;
		printf("Terminated with %d errors\n", errors);
	}

	if (autograder)
	{
		/* 자동 채점기가 읽기 쉬운 형식으로 핵심 결과만 따로 출력합니다. */
		printf("correct:%d\n", numcorrect);
		printf("perfidx:%.0f\n", perfindex);
	}

	exit(0);
}

/*****************************************************************
 * 아래 함수들은 range 리스트를 다룹니다.
 * range 리스트는 현재 할당된 각 블록 payload의 주소 범위를 기록합니다.
 * 이 정보를 이용하면 새 블록이 기존 블록과 겹치는지 검사할 수 있습니다.
 ****************************************************************/

/*
 * add_range - 새로 할당된 블록의 주소 범위를 검사한 뒤 range 리스트에 추가합니다.
 *
 * trace tracenum의 opnum번째 요청에 따라,
 * 학생 allocator가 lo 주소에 size 바이트 블록을 내어 주었다고 가정합니다.
 * 이 블록이 정렬 조건과 힙 범위를 만족하고 다른 블록과 겹치지 않으면
 * range 노드를 만들어 리스트 앞에 연결합니다.
 */
static int add_range(range_t **ranges, char *lo, int size,
					 int tracenum, int opnum)
{
	char *hi = lo + size - 1;
	range_t *p;
	char msg[MAXLINE];

	assert(size > 0);

	/* payload 시작 주소는 반드시 정렬 조건을 만족해야 합니다. */
	if (!IS_ALIGNED(lo))
	{
		sprintf(msg, "Payload address (%p) not aligned to %d bytes",
				lo, ALIGNMENT);
		malloc_error(tracenum, opnum, msg);
		return 0;
	}

	/* payload 시작과 끝이 모두 현재 힙 내부에 있어야 합니다. */
	if ((lo < (char *)mem_heap_lo()) || (lo > (char *)mem_heap_hi()) ||
		(hi < (char *)mem_heap_lo()) || (hi > (char *)mem_heap_hi()))
	{
		sprintf(msg, "Payload (%p:%p) lies outside heap (%p:%p)",
				lo, hi, mem_heap_lo(), mem_heap_hi());
		malloc_error(tracenum, opnum, msg);
		return 0;
	}

	/* 새 payload가 이미 기록된 다른 payload 구간과 겹치면 안 됩니다. */
	for (p = *ranges; p != NULL; p = p->next)
	{
		if ((lo >= p->lo && lo <= p->hi) ||
			(hi >= p->lo && hi <= p->hi))
		{
			sprintf(msg, "Payload (%p:%p) overlaps another payload (%p:%p)\n",
					lo, hi, p->lo, p->hi);
			malloc_error(tracenum, opnum, msg);
			return 0;
		}
	}

	/*
	 * 여기까지 통과했다면 블록이 올바르다고 보고,
	 * 범위 정보를 새 노드로 만들어 리스트에 저장합니다.
	 */
	if ((p = (range_t *)malloc(sizeof(range_t))) == NULL)
		unix_error("malloc error in add_range");
	p->next = *ranges;
	p->lo = lo;
	p->hi = hi;
	*ranges = p;
	return 1;
}

/*
 * remove_range - payload 시작 주소가 lo인 range 기록을 리스트에서 제거합니다.
 */
static void remove_range(range_t **ranges, char *lo)
{
	range_t *p;
	range_t **prevpp = ranges;
	int size;

	/* 연결 리스트를 순회하며 시작 주소가 같은 노드를 찾습니다. */
	for (p = *ranges; p != NULL; p = p->next)
	{
		if (p->lo == lo)
		{
			/* 이전 노드의 next를 건너뛰게 바꿔 현재 노드를 리스트에서 뺍니다. */
			*prevpp = p->next;
			size = p->hi - p->lo + 1;
			free(p);
			break;
		}
		prevpp = &(p->next);
	}
}

/*
 * clear_ranges - 현재 trace에 대해 기록된 모든 range 노드를 해제합니다.
 */
static void clear_ranges(range_t **ranges)
{
	range_t *p;
	range_t *pnext;

	/* 리스트를 끝까지 순회하면서 노드를 하나씩 free합니다. */
	for (p = *ranges; p != NULL; p = pnext)
	{
		pnext = p->next;
		free(p);
	}
	*ranges = NULL;
}

/**********************************************
 * 아래 함수들은 trace 파일을 읽고 해제하는 역할을 합니다.
 *********************************************/

/*
 * read_trace - trace 파일 하나를 읽어 메모리 안 구조체로 바꿉니다.
 */
static trace_t *read_trace(char *tracedir, char *filename)
{
	FILE *tracefile;
	trace_t *trace;
	char type[MAXLINE];
	char path[MAXLINE];
	unsigned index, size;
	unsigned max_index = 0;
	unsigned op_index;

	if (verbose > 1)
		printf("Reading tracefile: %s\n", filename);

	/* trace 전체 정보를 담을 구조체를 먼저 할당합니다. */
	if ((trace = (trace_t *)malloc(sizeof(trace_t))) == NULL)
		unix_error("malloc 1 failed in read_trance");

	/* 디렉터리와 파일 이름을 합쳐 실제 경로를 만들고 헤더를 읽습니다. */
	strcpy(path, tracedir);
	strcat(path, filename);
	if ((tracefile = fopen(path, "r")) == NULL)
	{
		sprintf(msg, "Could not open %s in read_trace", path);
		unix_error(msg);
	}
	fscanf(tracefile, "%d", &(trace->sugg_heapsize)); /* 현재는 사용하지 않는 값입니다. */
	fscanf(tracefile, "%d", &(trace->num_ids));
	fscanf(tracefile, "%d", &(trace->num_ops));
	fscanf(tracefile, "%d", &(trace->weight)); /* 현재는 사용하지 않는 값입니다. */

	/* 각 요청을 저장할 배열을 num_ops 크기로 만듭니다. */
	if ((trace->ops =
			 (traceop_t *)malloc(trace->num_ops * sizeof(traceop_t))) == NULL)
		unix_error("malloc 2 failed in read_trace");

	/* 각 ID가 현재 어떤 블록을 가리키는지 저장할 포인터 배열입니다. */
	if ((trace->blocks =
			 (char **)malloc(trace->num_ids * sizeof(char *))) == NULL)
		unix_error("malloc 3 failed in read_trace");

	/* 각 ID 블록의 현재 payload 크기도 별도 배열에 함께 저장합니다. */
	if ((trace->block_sizes =
			 (size_t *)malloc(trace->num_ids * sizeof(size_t))) == NULL)
		unix_error("malloc 4 failed in read_trace");

	/* 이제 trace 본문을 한 줄씩 읽어 요청 배열에 채워 넣습니다. */
	index = 0;
	op_index = 0;
	while (fscanf(tracefile, "%s", type) != EOF)
	{
		switch (type[0])
		{
		case 'a':
			/* a index size: 새 블록 할당 요청입니다. */
			fscanf(tracefile, "%u %u", &index, &size);
			trace->ops[op_index].type = ALLOC;
			trace->ops[op_index].index = index;
			trace->ops[op_index].size = size;
			max_index = (index > max_index) ? index : max_index;
			break;
		case 'r':
			/* r index size: 기존 ID 블록을 재할당하는 요청입니다. */
			fscanf(tracefile, "%u %u", &index, &size);
			trace->ops[op_index].type = REALLOC;
			trace->ops[op_index].index = index;
			trace->ops[op_index].size = size;
			max_index = (index > max_index) ? index : max_index;
			break;
		case 'f':
			/* f index: 해당 ID 블록을 해제하는 요청입니다. */
			fscanf(tracefile, "%ud", &index);
			trace->ops[op_index].type = FREE;
			trace->ops[op_index].index = index;
			break;
		default:
			printf("Bogus type character (%c) in tracefile %s\n",
				   type[0], path);
			exit(1);
		}
		op_index++;
	}
	fclose(tracefile);
	/* trace에 등장한 최대 ID와 요청 수가 헤더 정보와 맞는지 확인합니다. */
	assert(max_index == trace->num_ids - 1);
	assert(trace->num_ops == op_index);

	return trace;
}

/*
 * free_trace - read_trace에서 만든 trace 구조체와 내부 배열들을 모두 해제합니다.
 */
void free_trace(trace_t *trace)
{
	free(trace->ops); /* 요청 배열 해제 */
	free(trace->blocks);
	free(trace->block_sizes);
	free(trace); /* trace 본체도 해제 */
}

/**********************************************************************
 * 아래 함수들은 libc malloc과 학생 malloc의
 * 정합성, 공간 활용도, 처리 속도를 평가합니다.
 **********************************************************************/

/*
 * eval_mm_valid - 학생 malloc 구현이 올바르게 동작하는지 검사합니다.
 */
static int eval_mm_valid(trace_t *trace, int tracenum, range_t **ranges)
{
	int i, j;
	int index;
	int size;
	int oldsize;
	char *newp;
	char *oldp;
	char *p;

	/* 새 trace 평가를 위해 가상 힙과 range 기록을 모두 초기 상태로 되돌립니다. */
	mem_reset_brk();
	clear_ranges(ranges);

	/* 학생 allocator의 초기화 함수가 정상 동작하는지 먼저 확인합니다. */
	if (mm_init() < 0)
	{
		malloc_error(tracenum, 0, "mm_init failed.");
		return 0;
	}

	/* trace에 적힌 요청을 처음부터 끝까지 순서대로 실행합니다. */
	for (i = 0; i < trace->num_ops; i++)
	{
		/* 현재 요청이 참조하는 ID와 크기를 꺼내 둡니다. */
		index = trace->ops[i].index;
		size = trace->ops[i].size;

		switch (trace->ops[i].type)
		{

		case ALLOC: /* mm_malloc */

			/* 학생이 만든 mm_malloc로 새 블록을 요청합니다. */
			if ((p = mm_malloc(size)) == NULL)
			{
				malloc_error(tracenum, i, "mm_malloc failed.");
				return 0;
			}

			/*
			 * 새 블록이 정렬 조건을 만족하는지,
			 * 힙 범위 안에 있는지,
			 * 이미 할당된 다른 블록과 겹치지 않는지 검사합니다.
			 * 모두 통과하면 range 리스트에 등록합니다.
			 */
			if (add_range(ranges, p, size, tracenum, i) == 0)
				return 0;

			/* 추가 검사 설명:
			 * 블록 전체를 index의 하위 1바이트 값으로 채워 둡니다.
			 * 나중에 realloc 후에도 예전 데이터가 잘 복사됐는지 검사할 때 사용합니다.
			 */
			memset(p, index & 0xFF, size);

			/* 이 ID가 지금 어떤 블록과 크기를 가리키는지 기록합니다. */
			trace->blocks[index] = p;
			trace->block_sizes[index] = size;
			break;

		case REALLOC: /* mm_realloc */

			/* 기존 블록을 새 크기로 재할당합니다. */
			oldp = trace->blocks[index];
			if ((newp = mm_realloc(oldp, size)) == NULL)
			{
				malloc_error(tracenum, i, "mm_realloc failed.");
				return 0;
			}

			/* 예전 주소 범위 기록은 더 이상 유효하지 않으므로 제거합니다. */
			remove_range(ranges, oldp);

			/* 새 블록 주소도 정렬/겹침 여부를 검사한 뒤 range에 다시 넣습니다. */
			if (add_range(ranges, newp, size, tracenum, i) == 0)
				return 0;

			/* 추가 검사 설명:
			 * 새 블록 앞부분에 예전 데이터가 그대로 남아 있는지 확인합니다.
			 * 검사 후에는 다시 index 값 패턴으로 채워 다음 realloc 검사에 대비합니다.
			 */
			oldsize = trace->block_sizes[index];
			/* 축소 realloc이면 겹치는 부분까지만 검사해야 합니다. */
			if (size < oldsize)
				oldsize = size;
			/* 복사되어야 할 구간의 모든 바이트를 직접 검증합니다. */
			for (j = 0; j < oldsize; j++)
			{
				if (newp[j] != (index & 0xFF))
				{
					malloc_error(tracenum, i, "mm_realloc did not preserve the "
											  "data from old block");
					return 0;
				}
			}
			memset(newp, index & 0xFF, size);

			/* 이 ID가 이제 새 블록을 가리키도록 기록을 갱신합니다. */
			trace->blocks[index] = newp;
			trace->block_sizes[index] = size;
			break;

		case FREE: /* mm_free */

			/* 추적 리스트에서 제거한 뒤 학생의 free를 호출합니다. */
			p = trace->blocks[index];
			remove_range(ranges, p);
			mm_free(p);
			break;

		default:
			app_error("Nonexistent request type in eval_mm_valid");
		}
	}

	/* 여기까지 오류 없이 왔다면 이 trace에서는 올바르게 동작한 것입니다. */
	return 1;
}

/*
 * eval_mm_util - 학생 allocator의 공간 활용도를 계산합니다.
 *
 * 핵심 아이디어는 "이상적인 allocator였다면 실제로 필요한 payload 총합이
 * 최대 얼마였는가"를 `max_total_size`로 기억하는 것입니다.
 * 그리고 실제 힙 크기(`mem_heapsize`)와 비교해
 * `max_total_size / heapsize` 비율을 공간 활용도로 봅니다.
 *
 * 여기서 mem_sbrk는 힙을 줄이는 기능이 없기 때문에,
 * 최종 brk 위치는 곧 힙이 한 번이라도 커졌던 최대 높이와 같습니다.
 */
static double eval_mm_util(trace_t *trace, int tracenum, range_t **ranges)
{
	int i;
	int index;
	int size, newsize, oldsize;
	int max_total_size = 0;
	int total_size = 0;
	char *p;
	char *newp, *oldp;

	/* 깨끗한 상태에서 다시 측정하기 위해 힙과 allocator를 초기화합니다. */
	mem_reset_brk();
	if (mm_init() < 0)
		app_error("mm_init failed in eval_mm_util");

	/* trace를 다시 실행하면서 현재 payload 총합과 최대치를 추적합니다. */
	for (i = 0; i < trace->num_ops; i++)
	{
		switch (trace->ops[i].type)
		{

		case ALLOC: /* mm_alloc */
			index = trace->ops[i].index;
			size = trace->ops[i].size;

			/* 실제 할당이 가능한지 확인하면서 블록을 얻습니다. */
			if ((p = mm_malloc(size)) == NULL)
				app_error("mm_malloc failed in eval_mm_util");

			/* 이후 free/realloc이 이 ID를 참조할 수 있게 기록합니다. */
			trace->blocks[index] = p;
			trace->block_sizes[index] = size;

			/* 현재 살아 있는 모든 블록 payload 크기 총합을 추적합니다. */
			total_size += size;

			/* 지금까지 본 payload 총합 중 최대값을 갱신합니다. */
			max_total_size = (total_size > max_total_size) ? total_size : max_total_size;
			break;

		case REALLOC: /* mm_realloc */
			index = trace->ops[i].index;
			newsize = trace->ops[i].size;
			oldsize = trace->block_sizes[index];

			/* realloc 전후 payload 총합 차이만큼 현재 사용량을 조정합니다. */
			oldp = trace->blocks[index];
			if ((newp = mm_realloc(oldp, newsize)) == NULL)
				app_error("mm_realloc failed in eval_mm_util");

			/* ID가 가리키는 블록 주소와 크기를 새 값으로 바꿉니다. */
			trace->blocks[index] = newp;
			trace->block_sizes[index] = newsize;

			/* 현재 살아 있는 모든 블록 payload 크기 총합을 추적합니다. */
			total_size += (newsize - oldsize);

			/* 최대 payload 총합도 필요하면 갱신합니다. */
			max_total_size = (total_size > max_total_size) ? total_size : max_total_size;
			break;

		case FREE: /* mm_free */
			index = trace->ops[i].index;
			size = trace->block_sizes[index];
			p = trace->blocks[index];

			/* free는 현재 총 payload 사용량을 줄이는 역할만 합니다. */
			mm_free(p);

			/* 현재 살아 있는 모든 블록 payload 크기 총합을 추적합니다. */
			total_size -= size;

			break;

		default:
			app_error("Nonexistent request type in eval_mm_util");
		}
	}

	/* 이상적 최대 사용량을 실제 힙 크기로 나눈 값을 활용도로 반환합니다. */
	return ((double)max_total_size / (double)mem_heapsize());
}

/*
 * eval_mm_speed - 학생 allocator의 실행 시간을 재기 위해 반복 호출되는 함수입니다.
 */
static void eval_mm_speed(void *ptr)
{
	int i, index, size, newsize;
	char *p, *newp, *oldp, *block;
	trace_t *trace = ((speed_t *)ptr)->trace;

	/* 매 측정마다 같은 시작 조건을 만들기 위해 힙과 allocator를 초기화합니다. */
	mem_reset_brk();
	if (mm_init() < 0)
		app_error("mm_init failed in eval_mm_speed");

	/* trace 요청을 순서대로 다시 실행해 전체 처리 시간을 잽니다. */
	for (i = 0; i < trace->num_ops; i++)
		switch (trace->ops[i].type)
		{

		case ALLOC: /* mm_malloc */
			index = trace->ops[i].index;
			size = trace->ops[i].size;
			/* 포인터만 기록해 두면 이후 realloc/free에서 재사용할 수 있습니다. */
			if ((p = mm_malloc(size)) == NULL)
				app_error("mm_malloc error in eval_mm_speed");
			trace->blocks[index] = p;
			break;

		case REALLOC: /* mm_realloc */
			index = trace->ops[i].index;
			newsize = trace->ops[i].size;
			oldp = trace->blocks[index];
			/* 현재 ID가 가리키는 블록을 새 크기로 교체합니다. */
			if ((newp = mm_realloc(oldp, newsize)) == NULL)
				app_error("mm_realloc error in eval_mm_speed");
			trace->blocks[index] = newp;
			break;

		case FREE: /* mm_free */
			index = trace->ops[i].index;
			block = trace->blocks[index];
			/* 저장해 둔 포인터를 이용해 해제 요청을 수행합니다. */
			mm_free(block);
			break;

		default:
			app_error("Nonexistent request type in eval_mm_valid");
		}
}

/*
 * eval_libc_valid - 같은 trace를 libc malloc에도 적용해
 * trace 자체에 문제가 없는지 확인합니다.
 *
 * libc 쪽에서라도 실패가 나면 시스템 차원의 예외 상황으로 보고 종료합니다.
 */
static int eval_libc_valid(trace_t *trace, int tracenum)
{
	int i, newsize;
	char *p, *newp, *oldp;

	/* libc의 malloc/realloc/free를 동일한 순서로 실행합니다. */
	for (i = 0; i < trace->num_ops; i++)
	{
		switch (trace->ops[i].type)
		{

		case ALLOC: /* malloc */
			/* 시스템 malloc이 실패하면 trace 수행 자체가 불가능한 상황입니다. */
			if ((p = malloc(trace->ops[i].size)) == NULL)
			{
				malloc_error(tracenum, i, "libc malloc failed");
				unix_error("System message");
			}
			trace->blocks[trace->ops[i].index] = p;
			break;

		case REALLOC: /* realloc */
			/* 기존 포인터를 libc realloc으로 교체합니다. */
			newsize = trace->ops[i].size;
			oldp = trace->blocks[trace->ops[i].index];
			if ((newp = realloc(oldp, newsize)) == NULL)
			{
				malloc_error(tracenum, i, "libc realloc failed");
				unix_error("System message");
			}
			trace->blocks[trace->ops[i].index] = newp;
			break;

		case FREE: /* free */
			/* 현재 ID가 들고 있던 블록을 반납합니다. */
			free(trace->blocks[trace->ops[i].index]);
			break;

		default:
			app_error("invalid operation type  in eval_libc_valid");
		}
	}

	return 1;
}

/*
 * eval_libc_speed - libc malloc 패키지의 처리 시간을 재는 함수입니다.
 */
static void eval_libc_speed(void *ptr)
{
	int i;
	int index, size, newsize;
	char *p, *newp, *oldp, *block;
	trace_t *trace = ((speed_t *)ptr)->trace;

	/* 학생 구현과 같은 trace를 libc에도 그대로 적용해 비교 기준을 만듭니다. */
	for (i = 0; i < trace->num_ops; i++)
	{
		switch (trace->ops[i].type)
		{
		case ALLOC: /* malloc */
			index = trace->ops[i].index;
			size = trace->ops[i].size;
			if ((p = malloc(size)) == NULL)
				unix_error("malloc failed in eval_libc_speed");
			trace->blocks[index] = p;
			break;

		case REALLOC: /* realloc */
			index = trace->ops[i].index;
			newsize = trace->ops[i].size;
			oldp = trace->blocks[index];
			if ((newp = realloc(oldp, newsize)) == NULL)
				unix_error("realloc failed in eval_libc_speed\n");

			trace->blocks[index] = newp;
			break;

		case FREE: /* free */
			index = trace->ops[i].index;
			block = trace->blocks[index];
			free(block);
			break;
		}
	}
}

/*************************************
 * 기타 보조 함수들
 ************************************/

/*
 * printresults - 한 malloc 구현의 측정 결과를 표로 출력합니다.
 */
static void printresults(int n, stats_t *stats)
{
	int i;
	double secs = 0;
	double ops = 0;
	double util = 0;

	/* 먼저 trace별 개별 결과를 한 줄씩 출력합니다. */
	printf("%5s%7s %5s%8s%10s%6s\n",
		   "trace", " valid", "util", "ops", "secs", "Kops");
	for (i = 0; i < n; i++)
	{
		/* valid가 참인 trace만 util/sec/Kops 값이 의미 있습니다. */
		if (stats[i].valid)
		{
			printf("%2d%10s%5.0f%%%8.0f%10.6f%6.0f\n",
				   i,
				   "yes",
				   stats[i].util * 100.0,
				   stats[i].ops,
				   stats[i].secs,
				   (stats[i].ops / 1e3) / stats[i].secs);
			secs += stats[i].secs;
			ops += stats[i].ops;
			util += stats[i].util;
		}
		else
		{
			printf("%2d%10s%6s%8s%10s%6s\n",
				   i,
				   "no",
				   "-",
				   "-",
				   "-",
				   "-");
		}
	}

	/* 마지막에는 전체 trace를 합친 요약 결과를 출력합니다. */
	if (errors == 0)
	{
		printf("%12s%5.0f%%%8.0f%10.6f%6.0f\n",
			   "Total       ",
			   (util / n) * 100.0,
			   ops,
			   secs,
			   (ops / 1e3) / secs);
	}
	else
	{
		printf("%12s%6s%8s%10s%6s\n",
			   "Total       ",
			   "-",
			   "-",
			   "-",
			   "-");
	}
}

/*
 * app_error - 프로그램 내부 논리 오류를 보고하고 종료합니다.
 */
void app_error(char *msg)
{
	printf("%s\n", msg);
	exit(1);
}

/*
 * unix_error - errno를 함께 보여 주는 시스템 오류를 출력하고 종료합니다.
 */
void unix_error(char *msg)
{
	printf("%s: %s\n", msg, strerror(errno));
	exit(1);
}

/*
 * malloc_error - 학생 malloc 패키지에서 발견한 오류를 기록하고 출력합니다.
 */
void malloc_error(int tracenum, int opnum, char *msg)
{
	/* 오류 개수를 누적해 최종 성능 지수 계산에 반영합니다. */
	errors++;
	printf("ERROR [trace %d, line %d]: %s\n", tracenum, LINENUM(opnum), msg);
}

/*
 * usage - 지원하는 명령행 옵션을 설명합니다.
 */
static void usage(void)
{
	fprintf(stderr, "Usage: mdriver [-hvVal] [-f <file>] [-t <dir>]\n");
	fprintf(stderr, "Options\n");
	fprintf(stderr, "\t-a         Don't check the team structure.\n");
	fprintf(stderr, "\t-f <file>  Use <file> as the trace file.\n");
	fprintf(stderr, "\t-g         Generate summary info for autograder.\n");
	fprintf(stderr, "\t-h         Print this message.\n");
	fprintf(stderr, "\t-l         Run libc malloc as well.\n");
	fprintf(stderr, "\t-t <dir>   Directory to find default traces.\n");
	fprintf(stderr, "\t-v         Print per-trace performance breakdowns.\n");
	fprintf(stderr, "\t-V         Print additional debug info.\n");
}
