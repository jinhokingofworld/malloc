# Explicit Free List `mm.c` 설명 노트

작성일: 2026-04-14

## 1. 한 줄 요약

이번 `mm.c`는 비어 있는 블록만 따로 연결 리스트로 관리하는 malloc 구현이다.

힙 전체를 매번 처음부터 끝까지 뒤지는 방식이 아니라, "현재 비어 있는 블록 목록"만 따라가면서 쓸 수 있는 공간을 찾는다. 이 방식이 explicit free list다.

비유하면 힙은 긴 사물함 줄이고, free list는 그중 빈 사물함 번호만 적어 둔 별도 목록이다.

## 2. 블록 모양

모든 블록은 앞뒤에 크기와 사용 여부를 적어 둔다.

```text
allocated block

[ header ][ payload ... ][ footer ]
```

free block은 payload 자리에 연결 리스트 포인터 2개를 추가로 넣는다.

```text
free block

[ header ][ prev free ptr ][ next free ptr ][ 남는 공간 ... ][ footer ]
```

- `header`: 이 블록 전체 크기와 allocated/free 상태를 저장한다.
- `footer`: header와 같은 정보를 블록 뒤쪽에 저장한다.
- `prev free ptr`: free list에서 이전 free block을 가리킨다.
- `next free ptr`: free list에서 다음 free block을 가리킨다.

64비트 환경에서는 포인터 하나가 8바이트다. 따라서 free block은 최소한 아래 크기만큼 필요하다.

```text
header 4 + footer 4 + prev pointer 8 + next pointer 8 = 24 bytes
```

그래서 코드에는 `MIN_BLOCK_SIZE`가 있다. 너무 작은 free block을 만들면 `prev`와 `next` 포인터를 넣을 공간이 없어서 연결 리스트가 깨진다.

## 3. 중요한 매크로

```c
#define HDRP(bp) ((char *)(bp) - WSIZE)
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)
```

`bp`는 payload 시작 주소다. header는 payload 바로 앞에 있으므로 `bp - WSIZE`로 찾는다. footer는 블록 전체 크기를 이용해서 블록 끝쪽으로 이동해 찾는다.

```c
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))
```

`NEXT_BLKP`는 현재 블록 크기만큼 앞으로 이동해서 다음 블록을 찾는다. `PREV_BLKP`는 현재 블록 앞쪽에 있는 이전 블록의 footer를 읽어서 이전 블록 크기를 알아낸 뒤 뒤로 이동한다.

```c
#define PREV_FREE(bp) (*(void **)(bp))
#define NEXT_FREE(bp) (*(void **)((char *)(bp) + sizeof(void *)))
```

free block의 payload 첫 칸에는 이전 free block 주소를 넣고, 그다음 칸에는 다음 free block 주소를 넣는다.

## 4. `mm_init`

`mm_init`은 malloc 관리자가 시작할 때 필요한 기본 구조를 만든다.

순서는 다음과 같다.

1. padding을 만든다.
2. prologue block을 만든다.
3. epilogue header를 만든다.
4. `heap_listp`를 prologue block 위치로 맞춘다.
5. `free_listp`를 `NULL`로 초기화한다.
6. `extend_heap`으로 처음 free block을 만든다.

prologue와 epilogue는 실제 사용자가 쓰는 블록이 아니라 경계 처리를 쉽게 하기 위한 가짜 블록이다. 덕분에 첫 블록이나 마지막 블록에서도 예외 처리를 덜 할 수 있다.

## 5. `mm_malloc`

`mm_malloc(size)`는 사용자가 요청한 크기를 실제 블록 크기로 바꾼 뒤, free list에서 들어갈 수 있는 블록을 찾는다.

흐름은 다음과 같다.

1. `size == 0`이면 `NULL`을 반환한다.
2. `adjust_block_size`로 header, footer, 정렬, 최소 블록 크기를 반영한 `asize`를 만든다.
3. `first_fit(asize)`로 free list를 앞에서부터 찾는다.
4. 적당한 free block이 없으면 `extend_heap`으로 힙을 늘린다.
5. `place(bp, asize)`로 실제 할당을 진행한다.

`first_fit`은 "처음 발견한 충분히 큰 free block"을 쓰는 전략이다.

## 6. `place`

`place`는 찾은 free block을 allocated block으로 바꾼다.

예를 들어 100바이트짜리 free block에서 40바이트만 쓰면 60바이트가 남는다. 이때 남은 공간이 `MIN_BLOCK_SIZE` 이상이면 블록을 둘로 나눈다.

```text
before

[ free 100 ]

after

[ allocated 40 ][ free 60 ]
```

하지만 남은 공간이 너무 작으면 나누지 않는다.

```text
before

[ free 40 ]

after

[ allocated 40 ]
```

작은 찌꺼기 블록을 만들지 않는 이유는, free block이 되려면 header, footer, prev pointer, next pointer를 모두 담을 수 있어야 하기 때문이다.

중요한 규칙은 할당에 사용할 free block을 free list에서 먼저 빼야 한다는 점이다. 그래서 `place`는 `remove_free_block(bp)`를 먼저 호출한다.

## 7. `mm_free`와 `coalesce`

`mm_free(ptr)`는 블록을 free 상태로 표시한 뒤 `coalesce(ptr)`를 호출한다.

`coalesce`는 바로 옆 블록도 free이면 하나로 합친다.

현재 코드에서는 역할을 더 분명하게 나누었다.

```c
ptr = coalesce(ptr);
insert_free_block(ptr);
```

즉, `coalesce`는 주변 free block과 합치기만 한다. free list에 넣는 일은 `insert_free_block`이 한다. 이렇게 나누면 free block이 언제 list에 들어가는지 코드에서 바로 보인다.

합치는 경우는 4가지다.

```text
case 1: 앞 allocated, 뒤 allocated

[ allocated ][ free ][ allocated ]
```

합칠 이웃이 없으므로 현재 블록 주소를 그대로 반환한다. 그 다음 호출부가 이 블록을 free list에 넣는다.

```text
case 2: 앞 allocated, 뒤 free

[ allocated ][ free ][ free ]
```

뒤 free block을 free list에서 빼고, 현재 블록과 뒤 블록을 합친 뒤, 합쳐진 블록 주소를 반환한다. 그 다음 호출부가 합쳐진 블록을 free list에 넣는다.

```text
case 3: 앞 free, 뒤 allocated

[ free ][ free ][ allocated ]
```

앞 free block을 free list에서 빼고, 앞 블록과 현재 블록을 합친 뒤, 합쳐진 블록 주소를 반환한다. 그 다음 호출부가 합쳐진 블록을 free list에 넣는다.

```text
case 4: 앞 free, 뒤 free

[ free ][ free ][ free ]
```

앞 free block과 뒤 free block을 free list에서 둘 다 빼고, 세 블록을 하나로 합친 뒤, 합쳐진 블록 주소를 반환한다. 그 다음 호출부가 합쳐진 블록을 free list에 넣는다.

여기서 가장 중요한 원칙은 "합치기 전의 이웃 free block을 list에서 제거하고, 합친 최종 블록만 list에 한 번 넣기"다. 그래야 free list에 같은 공간을 가리키는 오래된 주소가 남지 않는다.

## 7-1. free block은 정확히 언제 free list에 들어가나?

free block이 생기는 경로는 여러 개다. 이번 리팩토링 후에는 각 경로에서 free list 삽입이 코드에 직접 보인다.

첫 번째는 `mm_free`다. 사용자가 쓰던 allocated block을 반납하면 header/footer를 free로 바꾸고, 주변 free block과 합친 뒤, 최종 블록을 free list에 넣는다.

```c
PUT(HDRP(ptr), PACK(size, 0));
PUT(FTRP(ptr), PACK(size, 0));

ptr = coalesce(ptr);
insert_free_block(ptr);
```

두 번째는 `extend_heap`이다. 힙을 새로 늘리면 새 공간은 free block이 된다. 이 블록도 앞 블록이 free이면 합쳐질 수 있으므로, 먼저 `coalesce`를 하고 최종 블록을 free list에 넣는다.

```c
PUT(HDRP(bp), PACK(size, 0));
PUT(FTRP(bp), PACK(size, 0));
PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));

bp = coalesce(bp);
insert_free_block(bp);
```

세 번째는 `place`에서 split이 일어나는 경우다. 큰 free block에서 앞부분만 할당하고 뒤쪽이 남으면, 뒤쪽 남은 조각이 새 free block이 된다. 이때는 앞 블록이 방금 allocated block이므로 보통 바로 `insert_free_block(next_bp)`를 해도 된다.

```c
PUT(HDRP(next_bp), PACK(temp, 0));
PUT(FTRP(next_bp), PACK(temp, 0));
insert_free_block(next_bp);
```

네 번째는 `realloc`에서 블록을 줄이거나, 다음 free block을 합쳐 확장한 뒤 뒤쪽이 남는 경우다. 새로 생긴 뒤쪽 free block이 그 다음 블록과 붙어 있을 수 있으므로 `coalesce` 후 free list에 넣는다.

```c
free_bp = coalesce(free_bp);
insert_free_block(free_bp);
```

정리하면 `coalesce`는 "합치기 담당", `insert_free_block`은 "free list에 등록 담당"이다. 이 둘을 분리하면 free block이 만들어지고 등록되는 흐름을 눈으로 추적하기 쉬워진다.

## 8. free list 삽입과 삭제

이 코드는 LIFO 방식으로 free list에 블록을 넣는다. LIFO는 나중에 들어온 블록을 list 맨 앞에 넣는 방식이다.

```text
새 free block -> 기존 첫 free block -> ...
```

`insert_free_block(bp)`는 다음 순서로 움직인다.

1. 새 블록의 `prev`를 `NULL`로 만든다.
2. 새 블록의 `next`를 기존 `free_listp`로 만든다.
3. 기존 첫 블록이 있으면 그 블록의 `prev`를 새 블록으로 바꾼다.
4. `free_listp`를 새 블록으로 바꾼다.

`remove_free_block(bp)`는 해당 블록의 앞뒤를 서로 이어 준 뒤, 자기 자신의 `prev`, `next`를 `NULL`로 비운다.

## 9. `mm_realloc`

`realloc`은 기존 블록 크기를 바꾸는 함수다.

이번 구현은 세 가지 방법을 순서대로 시도한다.

1. 현재 블록이 이미 충분히 크면 그대로 쓴다. 많이 남으면 뒤쪽을 free block으로 나눈다.
2. 현재 블록 바로 뒤가 free이고, 둘을 합치면 충분히 크면 그 자리에서 확장한다.
3. 위 방법이 안 되면 새 블록을 `mm_malloc`으로 만들고, 기존 데이터를 `memcpy`로 복사한 뒤, 기존 블록을 `mm_free`한다.

이렇게 하면 가능한 경우에는 주소를 바꾸지 않고 확장할 수 있다.

## 10. 꼭 기억할 규칙

1. free list에는 free block만 들어간다.
2. allocated block은 free list에 있으면 안 된다.
3. free block을 allocated block으로 바꿀 때는 free list에서 먼저 제거한다.
4. coalesce할 때는 이웃 free block을 free list에서 먼저 제거한다.
5. 합쳐진 최종 free block만 free list에 한 번 넣는다.
6. block size는 항상 8의 배수여야 한다.
7. free block은 최소한 `MIN_BLOCK_SIZE` 이상이어야 한다.

## 11. 테스트 명령어

```sh
make clean
make
./mdriver -V -f short1-bal.rep
./mdriver -V -f short2-bal.rep
./mdriver -V -f traces/realloc-bal.rep
./mdriver -v
```

이번 수정 후 전체 기본 trace에서 `valid`가 모두 `yes`로 나왔다.
