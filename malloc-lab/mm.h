/* malloc lab에서 구현해야 할 함수와 팀 정보 구조체 선언입니다. */
#include <stdio.h>

extern int mm_init (void);
extern void *mm_malloc (size_t size);
extern void mm_free (void *ptr);
extern void *mm_realloc(void *ptr, size_t size);


/* 
 * 학생들은 1인 또는 2인 팀으로 작업합니다.
 * 이 구조체에는 팀 이름과 팀원 정보를 적어 둡니다.
 * 드라이버는 이 값을 읽어 제출자 정보를 확인합니다.
 */
typedef struct {
    char *teamname; /* 두 사람 팀이면 ID1+ID2, 한 사람 팀이면 ID1 */
    char *name1;    /* 첫 번째 팀원의 실명 */
    char *id1;      /* 첫 번째 팀원의 로그인 ID */
    char *name2;    /* 두 번째 팀원의 실명(있다면) */
    char *id2;      /* 두 번째 팀원의 로그인 ID */
} team_t;

extern team_t team;
