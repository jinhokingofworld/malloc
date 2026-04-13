# mm.c 초안 리뷰 메모

작성일: 2026-04-13

## 요약

현재 초안은 implicit free list + boundary tag 방식으로 방향은 잘 잡혀 있지만, 아직 드라이버 정합성 검사를 통과하지 못한다.

가장 먼저 고쳐야 할 문제는 `first_fit`의 시작 포인터와 `mm_malloc`의 블록 크기 계산이다. 이 둘 때문에 첫 할당부터 payload 주소가 8바이트 정렬 조건을 깨고, header/footer가 저장하는 block size도 8의 배수가 아닐 수 있다.

## 확인한 실행 결과

빌드:

```sh
make clean
make
```

결과:

- `make clean` 후 `make`는 성공했다.
- 기존 `.o` 파일이 남아 있을 때는 링크 단계에서 `unknown file type` 오류가 났다. 코드 자체 문제라기보다는 이전 빌드 산출물이 현재 실행 환경과 맞지 않았던 것으로 보인다.
- `mdriver.c`에서 warning 2개가 발생했지만, 이번 `mm.c` 초안의 직접 원인은 아니다.

짧은 trace:

```sh
./mdriver -V -f short1-bal.rep
./mdriver -V -f short2-bal.rep
```

두 trace 모두 첫 alloc에서 실패했다.

```text
Payload address (...) not aligned to 8 bytes
```

전체 기본 trace:

```sh
./mdriver -v
```

11개 기본 trace가 모두 line 6, 즉 첫 할당에서 같은 정렬 오류로 실패했다.

## 우선 수정해야 할 문제

### 1. `first_fit` 시작 위치가 잘못됨

위치: `mm.c`의 `first_fit`

현재 코드:

```c
void *bp = (char *)heap_listp + WSIZE;
```

`heap_listp`는 prologue block의 payload 위치를 가리키도록 잡혀 있다. 따라서 첫 실제 free block으로 이동하려면 단순히 `WSIZE`만 더하면 안 되고, prologue block 전체 크기만큼 이동해야 한다.

권장 방향:

```c
void *bp = NEXT_BLKP(heap_listp);
```

현재처럼 `heap_listp + WSIZE`에서 시작하면 포인터가 block payload 기준이 아니라 prologue footer/epilogue 근처로 어긋난다. 이후 `NEXT_BLKP` 계산도 그 잘못된 기준에서 진행되어 첫 반환 payload가 8바이트 경계가 아닌 주소가 된다.

### 2. `mm_malloc`의 크기 보정 조건이 잘못됨

위치: `mm.c`의 `mm_malloc`

현재 코드:

```c
int newsize = ALIGN(size) + DSIZE;
int wsize = newsize / WSIZE;
if (wsize / 2 != 0) {
    wsize += 1;
    newsize += WSIZE;
}
```

`wsize / 2 != 0`은 `wsize`가 2 이상이면 거의 항상 참이다. 즉 대부분의 요청에서 word 수를 1개 더 늘린다.

문제는 header/footer의 size 필드는 하위 3비트를 flag 용도로 쓰기 때문에 block size가 반드시 8의 배수여야 한다는 점이다. 그런데 `wsize`를 1개 늘리면 4바이트만 증가하므로, 예를 들어 2048바이트가 2052바이트가 될 수 있다. 이 값은 8의 배수가 아니고 `GET_SIZE`에서 `& ~0x7`을 거치며 2048로 잘려 metadata 해석이 꼬일 수 있다.

권장 방향:

```c
size_t asize = MAX(2 * DSIZE, ALIGN(size + DSIZE));
```

또는 word 단위를 계속 쓰고 싶다면 `wsize`가 홀수일 때만 1을 더해야 한다.

```c
if (wsize % 2 != 0) {
    wsize += 1;
}
```

다만 실수 여지가 적은 쪽은 allocator 내부에서는 byte size(`asize`)를 중심으로 계산하고, `extend_heap`에 넘길 때만 word 수로 바꾸는 방식이다.

### 3. `place`에서 splinter가 생길 때 전체 블록 크기를 써야 함

위치: `mm.c`의 `place`

현재 코드:

```c
int temp = GET_SIZE(HDRP(bp)) - newsize;

if (temp < 2 * DSIZE) {
    PUT(HDRP(bp), PACK(newsize, 1));
    PUT(FTRP(bp), PACK(newsize, 1));
    return;
}
```

남은 크기가 최소 free block 크기보다 작아서 split하지 않는 경우에는 요청 크기 `newsize`가 아니라 기존 free block 전체 크기 `csize`를 할당 블록으로 표시해야 한다.

현재처럼 `newsize`로 줄여 버리면, split하지 않은 나머지 작은 조각이 어떤 block에도 속하지 않는 빈 공간처럼 남을 수 있다. 그 다음 block 탐색, footer 접근, coalescing에서 heap 구조가 깨질 위험이 있다.

권장 방향:

```c
size_t csize = GET_SIZE(HDRP(bp));
size_t remainder = csize - asize;

if (remainder < 2 * DSIZE) {
    PUT(HDRP(bp), PACK(csize, 1));
    PUT(FTRP(bp), PACK(csize, 1));
    return;
}
```

### 4. `mm_realloc`이 아직 항상 실패함

위치: `mm.c`의 `mm_realloc`

현재는 항상 `NULL`을 반환한다. 기본 trace 목록에는 `realloc-bal.rep`, `realloc2-bal.rep`가 포함되어 있으므로, 정렬 문제를 고친 뒤에도 realloc trace에서 바로 실패한다.

일단 단순 구현으로 충분하다.

필수 동작:

- `ptr == NULL`이면 `mm_malloc(size)`처럼 동작
- `size == 0`이면 `mm_free(ptr)` 후 `NULL` 반환
- 새 블록을 `mm_malloc(size)`로 할당
- 기존 payload 중 `min(old_payload_size, size)`만큼 `memcpy`
- 기존 블록 `mm_free(ptr)`
- 새 포인터 반환

기존 payload 크기는 현재 block 전체 크기에서 header/footer 크기인 `DSIZE`를 뺀 값으로 계산하면 된다.

## 다음으로 체크할 사항

### `mm_init`에서 `mem_init()`을 호출하지 않는 것이 일반적임

위치: `mm.c`의 `mm_init`

현재 `mm_init` 내부에서 `mem_init()`을 호출하고 있다. 하지만 `mdriver.c`는 이미 테스트 시작 전에 `mem_init()`을 호출하고, trace마다 `mem_reset_brk()` 후 `mm_init()`을 호출한다.

`mm_init()`이 다시 `mem_init()`을 호출하면 가상 힙을 반복해서 새로 할당하게 되어 메모리 누수가 생긴다. 특히 성능 측정에서는 `mm_init()`이 반복 호출될 수 있으므로 불필요하게 위험하다.

권장 방향:

- `mm_init`에서는 `mem_sbrk`로 allocator용 prologue/epilogue를 만드는 일만 한다.
- `mem_init` 호출은 driver/test harness의 책임으로 둔다.

### `extend_heap`도 짝수 word 수로 확장해야 함

위치: `mm.c`의 `extend_heap`

초기 `extend_heap(CHUNKSIZE / WSIZE)`는 짝수 word라 괜찮지만, `mm_malloc`에서 필요한 크기만큼 확장할 때 홀수 word가 들어오면 block size가 8바이트 정렬을 깨게 된다.

권장 방향:

```c
size_t size = (wsize % 2) ? (wsize + 1) * WSIZE : wsize * WSIZE;
```

그리고 header/footer/epilogue를 모두 이 `size` 기준으로 쓰는 편이 안전하다.

### `int` 대신 `size_t` 사용 검토

위치: `mm_malloc`, `extend_heap`, `place`

크기 계산에 `int`가 섞여 있다.

```c
int newsize = ...
int wsize = ...
int temp = ...
```

Malloc lab trace에서는 대부분 큰 문제가 되지 않을 수 있지만, allocator 코드에서는 크기와 주소 차이를 다룰 때 `size_t`를 쓰는 편이 안전하다. 특히 `mem_sbrk`에 넘기기 직전에만 overflow 가능성을 확인하고 `int`로 변환하는 흐름이 좋다.

### 상단 설명 주석이 현재 구현과 맞지 않음

파일 상단 설명은 아직 naive allocator 설명에 가깝다.

현재 코드는 이미 header/footer, free block reuse, coalescing, first-fit을 구현하고 있으므로, 나중에 제출 전에는 설명 주석을 실제 구현에 맞게 바꾸는 것이 좋다.

### 팀 정보 확인

위치: `team_t team`

현재 `id1`이 `bovik@cs.cmu.edu`로 남아 있다.

채점 환경에서 팀 정보까지 확인한다면 실제 로그인 ID/이메일 규칙에 맞게 수정해야 한다.

## 추천 수정 순서

1. `mm_malloc`의 adjusted block size 계산을 8바이트 배수로 고친다.
2. `first_fit` 시작 포인터를 `NEXT_BLKP(heap_listp)`로 고친다.
3. `place`에서 split하지 않는 경우 기존 block 전체 크기를 할당 상태로 표시한다.
4. `extend_heap`에서 홀수 word 요청을 짝수 word로 보정한다.
5. `mm_realloc`의 단순 버전을 구현한다.
6. `mm_init`에서 `mem_init()` 호출을 제거하고 driver 흐름에 맞춘다.
7. 아래 순서로 다시 테스트한다.

```sh
make clean
make
./mdriver -V -f short1-bal.rep
./mdriver -V -f short2-bal.rep
./mdriver -V -f traces/realloc-bal.rep
./mdriver -v
```

주의: `-f` 옵션은 현재 디렉터리 기준으로 파일을 읽도록 되어 있으므로, `traces/realloc-bal.rep`처럼 경로를 포함해서 넘기거나 해당 trace를 현재 디렉터리로 복사해서 실행해야 한다.
