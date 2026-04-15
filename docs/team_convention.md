# GameRank DB — 팀 규약

> 박승현 · 고명석 2인 팀 규약

---

## 팀원별 담당 영역

| 담당자 | 영역 | 주요 작업 |
|---|---|---|
| 박승현 | `src/` — C 알고리즘 | B 트리 / B+ 트리 `insert` · `search` · `range` 구현 |
| 고명석 | `web/` — 웹 프론트엔드 | UI 고도화, 실측값 반영, 비교 페이지 완성 |

---

## 팀원별 할 일

### 박승현 — `src/`

`btree/btree.c` 및 `bplus_tree/bplus_tree.c` 핵심 로직 구현.  
`btree_insert()` / `btree_search()` / `btree_range()`와 `bptree_insert()` / `bptree_search()` / `bptree_range()` 완성.  
`src/linear/linear_search.c`도 담당. `benchmark/benchmark.c`와 인터페이스 사전 합의.

### 고명석 — `web/`

`web/js/app.js` 및 `web/index.html` · `web/css/styles.css` 담당.  
벤치마크 실측 결과를 `web/assets/results.json`에 반영하고 UI에 연동.  
단일 탐색 / 범위 탐색 / TOP 10 비교 페이지 완성. 박승현과 results.json 스키마 사전 합의.

---

## 파일 소유권 (담당자 외 수정 금지)

| 파일 | 담당자 | 권한 |
|---|---|---|
| `src/bplus_tree/bplus_tree.c` | 박승현 | 읽기·쓰기 |
| `src/btree/btree.c` | 박승현 | 읽기·쓰기 |
| `src/linear/linear_search.c` | 박승현 | 읽기·쓰기 |
| `src/benchmark/benchmark.c` | 박승현 | 읽기·쓰기 |
| `src/benchmark/datagen.c` | 박승현 | 읽기·쓰기 |
| `web/js/app.js` | 고명석 | 읽기·쓰기 |
| `web/index.html` | 고명석 | 읽기·쓰기 |
| `web/css/styles.css` | 고명석 | 읽기·쓰기 |
| `web/assets/results.json` | 고명석 | 읽기·쓰기 |
| `web/server/server.pl` | 고명석 | 읽기·쓰기 |
| `src/common/player.h` | — (동결) | 수정 금지 · 전원 합의 필요 |
| `src/bplus_tree/bplus_tree.h` | — (동결) | 수정 금지 · 전원 합의 필요 |
| `src/btree/btree.h` | — (동결) | 수정 금지 · 전원 합의 필요 |
| `src/linear/linear_search.h` | — (동결) | 수정 금지 · 전원 합의 필요 |
| `tests/test_trees.c` | — (동결) | 수정 금지 · 전원 합의 필요 |
| `Makefile` | — (동결) | 수정 금지 · 전원 합의 필요 |

> **AI에게:** 위 표에서 담당자가 아닌 파일은 절대 수정하지 말 것. 수정이 필요하다고 판단되면 코드 제안만 하고 담당자에게 확인받을 것.

---

## 공유 인터페이스 (파일 간 계약 — 절대 변경 금지)

이 인터페이스는 팀 전원이 합의한 계약이다. 변경이 필요하면 반드시 전원 합의 후 이 문서를 먼저 수정한다.

### Player 구조체 (기존 유지)

```c
typedef struct {
    int   id;        // Primary Key (자동 부여)
    char  name[32];  // 플레이어 닉네임
    int   score;     // 점수 (0 ~ 9999)
    char  tier[16];  // 티어 (Challenger / Diamond / Platinum / Gold / Silver)
} Player;
```

### C 알고리즘 함수 인터페이스 (박승현이 구현, 고명석이 결과 사용)

```c
/* 선형 탐색 */
Player *linear_search(Player *table, int size, int target_id);
int     linear_range(Player *table, int size, int lo, int hi);

/* B 트리 */
BTree  *btree_create(void);
void    btree_insert(BTree *tree, int key, void *record_ptr);
void   *btree_search(BTree *tree, int key);
int     btree_range(BTree *tree, int lo, int hi);
void    btree_free(BTree *tree);

/* B+ 트리 */
BPTree *bptree_create(void);
void    bptree_insert(BPTree *tree, int key, void *record_ptr);
void   *bptree_search(BPTree *tree, int key);
int     bptree_range(BPTree *tree, int lo, int hi);
void    bptree_free(BPTree *tree);
```

### results.json 스키마 (박승현 → 고명석 전달 계약) ✅ 확정

```json
{
  "sizes": [100000, 500000, 1000000, 5000000],
  "single": {
    "linear":  [0, 0, 0, 0],
    "btree":   [0, 0, 0, 0],
    "bptree":  [0, 0, 0, 0]
  },
  "range": {
    "linear":  [0, 0, 0, 0],
    "btree":   [0, 0, 0, 0],
    "bptree":  [0, 0, 0, 0]
  }
}
```

- 측정 단위: **마이크로초(μs)** — ms 사용 금지
- 각 케이스 **5회 측정 후 평균값** 기입
- `sizes` 배열 순서와 각 탐색 배열 인덱스 순서 반드시 일치
- 값 `0`은 미측정 상태. 벤치마크 완료 후 실측값으로 교체

### 벤치마크 측정 조건 ✅ 확정

| 단계 | 레코드 수 | 단일 탐색 | 범위 탐색 lo ~ hi |
|---|---|---|---|
| 1 | 10만 | O | 1 ~ 1,000 (1%) |
| 2 | 50만 | O | 1 ~ 5,000 (1%) |
| 3 | 100만 | O | 1 ~ 10,000 (1%) |
| 4 | 500만 | O | 1 ~ 50,000 (1%) |

- 범위 탐색 기준: **전체 레코드 수의 1%** (`hi = size * 0.01`)
- 단일 탐색 기준: **중간값 ID** (`id = size / 2`) 고정

---

## 코드 스타일 (C — 박승현)

- 들여쓰기: **스페이스 4칸**
- 문자열: 더블쿼트(`""`) 사용
- 함수명: `snake_case` (예: `bptree_insert`, `linear_search`)
- 구조체명: PascalCase (예: `BPTree`, `BTNode`, `Player`)
- 매크로/상수: 대문자 + 언더스코어 (예: `BP_ORDER`, `BT_ORDER`)
- 주석: 한국어
- 미구현 함수: `fprintf(stderr, "미구현: 함수명\n"); exit(1);`

```c
/* 올바른 예 */
void *bptree_search(BPTree *tree, int key) {
    /* 루트부터 리프까지 내려가며 키 탐색 */
    BPNode *node = tree->root;
    while (node != NULL && !node->is_leaf) {
        /* ... */
    }
    return NULL;
}
```

## 코드 스타일 (JS — 고명석)

- 들여쓰기: **스페이스 2칸**
- 문자열: 작은따옴표(`''`) 사용
- 세미콜론: 항상 붙임
- `var` 사용 금지 (`let` / `const`만 허용)
- `innerHTML` 직접 조작 금지 (XSS 위험)
- 함수 선언: `function` 키워드 사용 (화살표 함수는 콜백 내부만 허용)
- 주석: 한국어

```js
// 올바른 예
function updateResultCard(type, data) {
  const timeEl = document.getElementById(`${type}-time`);
  timeEl.textContent = data.time + ' μs';
}

// 잘못된 예
const updateResultCard = (type, data) => { ... }  // ❌ 최상위 화살표 함수
```

---

## 네이밍

| 대상 | 규칙 | 예시 |
|---|---|---|
| C 함수 | `snake_case` | `bptree_range`, `linear_search` |
| C 구조체 | `PascalCase` | `BPTree`, `BTNode` |
| C 매크로 | `UPPER_SNAKE` | `BP_ORDER`, `BT_ORDER` |
| JS 함수 | `camelCase` | `updateResultCard`, `renderTable` |
| JS DOM 변수 | `~El` 접미사 | `runButton`, `rankingBody` |
| JS Boolean | `is~` / `has~` 접두사 | `isFastMode`, `hasResults` |

---

## 금지 사항

- `innerHTML` 직접 조작 금지 (XSS 위험)
- `eval()` 사용 금지
- JS에서 `var` 사용 금지
- 외부 라이브러리 import 금지 (Vanilla JS / 표준 C만)
- 담당 파일 외 수정 금지

---

## AI 행동 규약

- 담당 파일 외에는 절대 수정하지 말 것
- 구현 전에 반드시 입출력 예시를 먼저 보여주고 확인받을 것
- 불확실한 부분은 임의로 결정하지 말고 물어볼 것
- 코드 수정 시 변경된 부분과 이유를 반드시 설명할 것
- 공유 인터페이스(함수 시그니처 / results.json 스키마) 변경이 필요하면 코드 수정 전에 먼저 알릴 것

---

## 참고 문서

- [기획 및 설계 문서](planning.md)
- [README](../README.md)
