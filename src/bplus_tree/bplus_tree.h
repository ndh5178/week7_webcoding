/*
 * bplus_tree.h  ─  B+ 트리 (B+Tree) 인터페이스
 *
 * ┌─────────────────────────────────────────────────────────────────┐
 * │  B+ 트리란?                                                      │
 * │  - 실제 레코드 포인터는 오직 리프 노드(BPLeaf)에만 저장한다.         │
 * │  - 내부 노드(BPNode)는 탐색 경로만 안내하는 '인덱스' 역할.           │
 * │  - 모든 리프 노드는 연결 리스트로 이어져 범위 탐색이 O(k)로 빠름.    │
 * │  - 삽입 시 리프 분할은 copy-up (오른쪽 첫 키를 부모에 복사).         │
 * │    내부 노드 분할은 move-up (중간 키가 부모로 이동).                 │
 * │                                                                  │
 * │  B 트리와의 비교:                                                  │
 * │    탐색: B+트리는 항상 리프까지 내려감 (일정한 탐색 깊이)            │
 * │    범위: B+트리의 연결 리스트 >> B트리의 전체 순회                   │
 * └─────────────────────────────────────────────────────────────────┘
 */

#ifndef BPLUS_TREE_H
#define BPLUS_TREE_H

/*
 * BP_ORDER: 노드(리프/내부) 하나에 저장할 수 있는 최대 키 개수.
 *
 * 32로 설정 시:
 *   - 리프 노드: 최대 32개의 (키, 레코드 포인터) 쌍 저장
 *   - 내부 노드: 최대 32개 키, 33개 자식 포인터
 *   - 100만 건 기준 트리 높이 ≈ log_16(1,000,000) ≈ 5 레벨
 */
#define BP_ORDER 32

/*
 * BPLeaf: B+ 트리 리프 노드 구조체
 *
 * B+ 트리에서 실제 데이터(레코드 포인터)는 오직 리프에만 존재한다.
 * 모든 리프는 next 포인터로 연결 리스트를 이루어 범위 탐색에 사용된다.
 *
 *  리프 연결 리스트 예시:
 *    [10,20] → [30,40] → [50,60] → NULL
 *     ↑ lo=25 범위 탐색 시 이 리프로 이동 후 next를 따라가며 hi 이하 카운트
 */
typedef struct BPLeaf {
    int            keys[BP_ORDER];   /* 정렬된 키 배열 */
    void          *ptrs[BP_ORDER];   /* 각 키에 대응하는 레코드 포인터 */
    int            num_keys;         /* 현재 키 개수 */
    struct BPLeaf *next;             /* 다음 리프 노드 (범위 탐색용 연결 리스트) */
} BPLeaf;

/*
 * BPNode: B+ 트리 내부(및 리프 래퍼) 노드 구조체
 *
 * B+ 트리의 내부 노드는 탐색 경로만 제공하고 실제 레코드는 없다.
 * is_leaf == 1이면 leaf 포인터를 통해 실제 BPLeaf에 접근한다.
 *
 * 구조 예시 (is_leaf=0, 키가 2개인 내부 노드):
 *
 *   children[0]   keys[0]   children[1]   keys[1]   children[2]
 *    (< 20)         20        (20~40)        40         (>= 40)
 *
 * 주의: 내부 노드의 keys[i]는 children[i+1] 서브트리의 최솟값
 */
typedef struct BPNode {
    int            keys[BP_ORDER];           /* 탐색용 키 배열 (내부 노드에는 레코드 없음) */
    struct BPNode *children[BP_ORDER + 1];   /* 자식 노드 포인터 배열 */
    int            num_keys;                 /* 현재 키 개수 */
    int            is_leaf;                  /* 1 = 리프 래퍼 노드, 0 = 내부 노드 */
    BPLeaf        *leaf;                     /* is_leaf==1일 때 실제 리프 데이터 접근용 */
} BPNode;

/*
 * BPTree: B+ 트리 최상위 구조체
 * 루트 BPNode 포인터 하나만 갖는다.
 */
typedef struct {
    BPNode *root;
} BPTree;

/* ──────────────────── 공개 API ──────────────────── */

/*
 * bptree_create: 빈 B+ 트리를 생성해 반환한다.
 * 최초 루트는 리프 노드(BPLeaf 포함)로 시작한다.
 */
BPTree *bptree_create(void);

/*
 * bptree_insert: key와 record_ptr를 트리에 삽입한다.
 * - 중복 키면 record_ptr만 갱신
 * - 리프가 꽉 차면 copy-up 분할, 내부 노드가 꽉 차면 move-up 분할
 */
void    bptree_insert(BPTree *tree, int key, void *record_ptr);

/*
 * bptree_search: key에 해당하는 레코드 포인터를 반환한다.
 * - 항상 리프 노드까지 내려간 뒤 BPLeaf에서 검색
 * - 없으면 NULL 반환
 */
void   *bptree_search(BPTree *tree, int key);

/*
 * bptree_range: lo 이상 hi 이하 범위의 레코드 개수를 반환한다.
 * - lo에 해당하는 리프까지 내려간 뒤 next 연결 리스트를 따라가며 카운트
 * - 이 방식이 B+ 트리의 핵심 장점: O(log N + k), k = 결과 수
 */
int     bptree_range(BPTree *tree, int lo, int hi);

/*
 * bptree_free: 트리가 차지하는 모든 메모리를 해제한다.
 */
void    bptree_free(BPTree *tree);

#endif /* BPLUS_TREE_H */
