/*
 * bplus_tree.c  ─  B+ 트리 (B+Tree) 구현
 *
 * 핵심 개념 요약:
 *  1. 구조: 내부 노드(BPNode) + 리프 노드(BPLeaf) 분리
 *           → 내부 노드는 탐색 경로, 리프 노드만 실제 레코드 보유
 *  2. 리프 연결 리스트: 모든 BPLeaf가 next 포인터로 연결
 *           → 범위 탐색 시 리프 레벨에서 순차 스캔 가능
 *  3. 리프 분할: copy-up (오른쪽 리프의 첫 키를 부모에 복사, 리프에서 제거 안 함)
 *  4. 내부 분할: move-up (중간 키를 부모로 올리고 양쪽에서 제거)
 */

#include "bplus_tree.h"
#include <stdlib.h>
#include <string.h>

/* ====================================================================
 * 내부 헬퍼 함수
 * ==================================================================== */

/*
 * new_leaf: 새 BPLeaf를 heap에 할당하고 초기화한다.
 * calloc 사용으로 모든 필드 0/NULL 초기화.
 */
static BPLeaf *new_leaf(void) {
    BPLeaf *l = calloc(1, sizeof(BPLeaf));
    return l;
}

/*
 * new_node: 새 BPNode를 할당하고 초기화한다.
 *
 * is_leaf == 1이면 BPLeaf도 함께 생성하여 leaf 필드에 연결한다.
 * is_leaf == 0이면 내부 노드로, leaf는 NULL.
 *
 * 이 구조 덕분에 BPNode 트리 구조와 BPLeaf 연결 리스트 구조가 분리된다.
 */
static BPNode *new_node(int is_leaf) {
    BPNode *n = calloc(1, sizeof(BPNode));
    n->is_leaf = is_leaf;
    if (is_leaf) n->leaf = new_leaf();  /* 리프 노드면 BPLeaf 생성 */
    return n;
}

/*
 * BPSplit: 노드 분할 결과를 담는 구조체
 *
 * 분할이 발생하면 부모 노드에게 다음 정보가 필요하다:
 *   key   - 부모로 승격될 키 (내부 노드에 삽입됨)
 *   right - 분할로 새로 생긴 오른쪽 BPNode
 */
typedef struct {
    int     key;    /* 부모로 승격될 키 */
    BPNode *right;  /* 분할로 생긴 오른쪽 BPNode */
} BPSplit;

/*
 * split_leaf: 꽉 찬 리프(BPLeaf)를 둘로 분할한다. (copy-up 방식)
 *
 * copy-up의 의미:
 *   분할된 오른쪽 리프의 첫 번째 키가 부모로 '복사'된다.
 *   리프에는 그 키가 그대로 남아 있다. (B 트리의 move-up과 다름!)
 *
 * 이유: B+ 트리에서 리프는 실제 데이터를 보유하므로
 *       키를 제거하면 데이터 접근이 불가능해진다.
 *
 * 분할 예시 (BP_ORDER=4, mid=2):
 *   분할 전 리프: keys = [10, 20, 30, 40], ptrs = [p0, p1, p2, p3]
 *   분할 후:
 *     왼쪽 리프: keys = [10, 20]            (mid=2개)
 *     오른쪽 리프: keys = [30, 40]           (나머지)
 *     승격 키: 30  ← 오른쪽 리프의 첫 번째 키 (복사, 제거 안 함)
 *   리프 연결: 왼쪽→오른쪽→(기존 다음 리프)
 *
 * 매개변수: left_node - 분할할 리프의 BPNode 래퍼
 * 반환값: BPSplit (승격 키 + 오른쪽 BPNode)
 */
static BPSplit split_leaf(BPNode *left_node) {
    BPLeaf *left  = left_node->leaf;         /* 분할 대상 리프 */
    BPNode *right_node = new_node(1);         /* 새 오른쪽 BPNode (리프 타입) */
    BPLeaf *right      = right_node->leaf;    /* 오른쪽 BPLeaf */

    int mid         = BP_ORDER / 2;           /* 분할 기준점: 16 */
    int right_count = left->num_keys - mid;   /* 오른쪽에 넘길 키 개수 */

    /* mid 이후의 키와 레코드 포인터를 오른쪽 리프로 복사 */
    for (int i = 0; i < right_count; i++) {
        right->keys[i] = left->keys[mid + i];
        right->ptrs[i] = left->ptrs[mid + i];
    }
    right->num_keys = right_count;
    left->num_keys  = mid;  /* 왼쪽은 mid개만 남김 */

    /*
     * 리프 연결 리스트 업데이트:
     *   기존: left → (next)
     *   변경: left → right → (next)
     */
    right->next = left->next;
    left->next  = right;

    /*
     * 승격 키 = 오른쪽 리프의 첫 번째 키.
     * copy-up: 오른쪽 리프에서 제거하지 않음!
     */
    BPSplit s;
    s.key   = right->keys[0];
    s.right = right_node;
    return s;
}

/*
 * split_internal: 꽉 찬 내부 노드를 둘로 분할한다. (move-up 방식)
 *
 * 내부 노드는 실제 레코드가 없으므로 중간 키를 부모로 이동시키고
 * 양쪽 노드에서 제거한다. (리프 분할의 copy-up과 다름!)
 *
 * 분할 예시 (BP_ORDER=4, mid=2):
 *   분할 전: keys = [10, 20, 30, 40], children = [c0,c1,c2,c3,c4]
 *   분할 후:
 *     왼쪽(node): keys = [10, 20], children = [c0,c1,c2]  (mid=2개)
 *     승격 키:    30                                        ← 부모로 이동
 *     오른쪽:     keys = [40], children = [c3,c4]          (나머지)
 *
 * 매개변수: node - 분할할 내부 노드 (왼쪽으로 남음)
 * 반환값: BPSplit (승격 키 + 오른쪽 BPNode)
 */
static BPSplit split_internal(BPNode *node) {
    int mid         = BP_ORDER / 2;            /* 분할 기준: 16 */
    int right_count = node->num_keys - mid - 1; /* 오른쪽 키 개수 */

    BPNode *right = new_node(0);  /* 새 내부 노드 */

    /* mid+1 이후의 키와 자식 포인터를 오른쪽 노드로 복사 */
    for (int i = 0; i < right_count; i++) {
        right->keys[i]     = node->keys[mid + 1 + i];
        right->children[i] = node->children[mid + 1 + i];
    }
    /* 오른쪽 노드의 가장 오른쪽 자식 포인터 */
    right->children[right_count] = node->children[node->num_keys];
    right->num_keys = right_count;

    /* 승격 키 = keys[mid] (move-up: 이 키는 부모로 올라가고 양쪽에서 제거) */
    BPSplit s;
    s.key   = node->keys[mid];
    s.right = right;

    /* 왼쪽 노드는 mid개만 남김 (keys[mid]는 제거됨) */
    node->num_keys = mid;
    return s;
}

/*
 * insert_rec: 재귀 삽입 함수.
 *
 * 동작 흐름:
 *   1. 리프 노드에 도달 시:
 *      - 삽입 위치 탐색 (정렬 유지)
 *      - 중복 키면 포인터만 갱신
 *      - 삽입 후 꽉 차면(num_keys==BP_ORDER) split_leaf() 호출
 *   2. 내부 노드 시:
 *      - 적절한 자식으로 재귀 호출
 *      - 자식에서 분할 발생 시 승격 키를 현재 노드에 삽입
 *      - 현재 노드도 꽉 차면 split_internal() 호출
 *
 * 반환값:
 *   1 → 분할 발생 (split에 결과 저장)
 *   0 → 분할 없음
 */
static int insert_rec(BPNode *node, int key, void *record_ptr, BPSplit *split) {
    if (node->is_leaf) {
        BPLeaf *leaf = node->leaf;

        /* ── 삽입 위치 탐색: key보다 작은 키를 건너뜀 ── */
        int i = 0;
        while (i < leaf->num_keys && leaf->keys[i] < key) i++;

        /* ── 중복 키 처리: 레코드 포인터만 갱신 ── */
        if (i < leaf->num_keys && leaf->keys[i] == key) {
            leaf->ptrs[i] = record_ptr;
            return 0;
        }

        /*
         * ── 키와 포인터 삽입 ──
         * memmove로 i 이후 원소들을 오른쪽으로 한 칸씩 이동시켜 공간 확보.
         * 예: keys=[10,30], i=1, key=20 삽입
         *   memmove 후: keys=[10,?,30]
         *   삽입 후:    keys=[10,20,30]
         */
        memmove(&leaf->keys[i + 1], &leaf->keys[i],
                sizeof(int) * (leaf->num_keys - i));
        memmove(&leaf->ptrs[i + 1], &leaf->ptrs[i],
                sizeof(void *) * (leaf->num_keys - i));
        leaf->keys[i] = key;
        leaf->ptrs[i] = record_ptr;
        leaf->num_keys++;

        /* ── 오버플로우 확인: BP_ORDER개가 되면 리프 분할 ── */
        if (leaf->num_keys == BP_ORDER) {
            *split = split_leaf(node);
            return 1;  /* 분할 발생 */
        }
        return 0;
    }

    /*
     * ── 내부 노드: 적절한 자식으로 재귀 삽입 ──
     *
     * B+ 트리 내부 노드의 키 의미:
     *   keys[i]는 children[i+1] 서브트리의 최솟값.
     *   key >= keys[i] 이면 오른쪽(children[i+1])으로 이동.
     *
     * 예시 (keys=[20,40], key=30):
     *   i=0: 30 >= 20 → i++
     *   i=1: 30 < 40 → 정지, children[1]로 이동
     */
    int i = 0;
    while (i < node->num_keys && key >= node->keys[i]) i++;

    BPSplit child_split;
    int promoted = insert_rec(node->children[i], key, record_ptr, &child_split);

    if (promoted) {
        /*
         * ── 자식 분할 발생: 승격된 키를 현재 노드의 i번 위치에 삽입 ──
         *
         * 승격 키 child_split.key를 keys[i]에 삽입하고,
         * 분할된 오른쪽 자식을 children[i+1]에 연결.
         *
         * 예시 (현재 keys=[20,40], i=1, 승격 키=30):
         *   memmove 후: keys=[20,?,40], children=[c0,c1,?,c2]
         *   삽입 후:    keys=[20,30,40], children=[c0,c1,c1_right,c2]
         */
        memmove(&node->keys[i + 1], &node->keys[i],
                sizeof(int) * (node->num_keys - i));
        memmove(&node->children[i + 2], &node->children[i + 1],
                sizeof(BPNode *) * (node->num_keys - i));
        node->keys[i]         = child_split.key;
        node->children[i + 1] = child_split.right;
        node->num_keys++;

        /* ── 오버플로우 확인: 내부 노드도 꽉 차면 분할 ── */
        if (node->num_keys == BP_ORDER) {
            *split = split_internal(node);
            return 1;
        }
    }
    return 0;
}

/* ====================================================================
 * 공개 API
 * ==================================================================== */

/*
 * bptree_create: 빈 B+ 트리 생성.
 *
 * 처음에는 루트가 리프 노드(BPLeaf 포함) 하나로 시작한다.
 * 키가 삽입되면서 분할이 일어나 내부 노드가 생성되며 트리가 성장한다.
 */
BPTree *bptree_create(void) {
    BPTree *t = calloc(1, sizeof(BPTree));
    t->root   = new_node(1);   /* 초기 루트 = 리프 노드 */
    return t;
}

/*
 * bptree_insert: 트리에 (key, record_ptr) 쌍을 삽입한다.
 *
 * insert_rec()이 1을 반환하면 루트가 분할된 것이므로
 * 새 내부 노드를 루트로 만들고, 기존 루트와 분할 오른쪽을 자식으로 연결.
 *
 *  [분할 전]         [분할 후]
 *   (root/leaf)  →   (new_root / 내부 노드)
 *                    /              \
 *               (old_root)      (s.right)
 *                          승격 키: s.key
 */
void bptree_insert(BPTree *tree, int key, void *record_ptr) {
    BPSplit s;
    int promoted = insert_rec(tree->root, key, record_ptr, &s);
    if (promoted) {
        /* 루트 분할 → 새 내부 루트 생성 */
        BPNode *new_root      = new_node(0);    /* is_leaf=0: 내부 노드 */
        new_root->keys[0]     = s.key;           /* 승격 키 */
        new_root->children[0] = tree->root;      /* 왼쪽 자식 = 기존 루트 */
        new_root->children[1] = s.right;         /* 오른쪽 자식 = 분할 결과 */
        new_root->num_keys    = 1;
        tree->root = new_root;
    }
}

/*
 * bptree_search: key에 해당하는 레코드 포인터를 반환한다.
 *
 * B+ 트리 탐색의 특징:
 *   - 항상 리프 노드까지 내려간다. (내부 노드에 실제 데이터 없음)
 *   - 모든 키가 리프에 있으므로 탐색 깊이가 일정하다.
 *
 * 탐색 흐름:
 *   1. is_leaf가 아닌 동안: keys 배열을 비교하며 올바른 children으로 이동
 *   2. is_leaf에 도달: BPLeaf->keys에서 선형 탐색
 *
 * 예시 (key=30, 내부 노드 keys=[20,40]):
 *   30 >= 20 → i++
 *   30 < 40 → children[1]로 이동 (is_leaf인 리프 노드)
 *   리프 keys=[25,30,35] → keys[1]==30 → ptrs[1] 반환
 */
void *bptree_search(BPTree *tree, int key) {
    BPNode *node = tree->root;

    /* ── 루트부터 리프까지 내려가기 ── */
    while (!node->is_leaf) {
        int i = 0;
        /*
         * key >= keys[i] 이면 오른쪽 자식(children[i+1])으로 이동.
         * 루프 종료 시 i가 가야 할 children 인덱스.
         */
        while (i < node->num_keys && key >= node->keys[i]) i++;
        node = node->children[i];
    }

    /* ── 리프(BPLeaf)에서 키 탐색 ── */
    BPLeaf *leaf = node->leaf;
    for (int i = 0; i < leaf->num_keys; i++) {
        if (leaf->keys[i] == key) return leaf->ptrs[i];   /* 발견! */
        if (leaf->keys[i] > key)  break;  /* 정렬되어 있으므로 이후는 볼 필요 없음 */
    }
    return NULL;
}

/*
 * bptree_range: lo 이상 hi 이하 범위의 레코드 개수를 반환한다.
 *
 * B+ 트리 범위 탐색의 핵심 장점:
 *   1. lo에 해당하는 리프까지 O(log N)으로 내려감
 *   2. 리프 연결 리스트(next)를 따라 O(k)로 스캔 (k = 결과 수)
 *   → 총 시간 복잡도: O(log N + k)
 *   → B 트리의 전체 순회 O(N)보다 훨씬 효율적
 *
 * 동작 예시 (lo=25, hi=45, 리프: [10,20]→[30,40]→[50,60]):
 *   1. lo=25로 탐색 → [10,20] 리프에 도달 (25 >= 20이면 다음 리프로...)
 *      실제로는 내부 노드 탐색으로 올바른 리프로 이동
 *   2. [30,40] 리프: 30>=25 count++, 40<=45 count++ → count=2
 *   3. [50,60] 리프: 50>45 → 즉시 return 2
 */
int bptree_range(BPTree *tree, int lo, int hi) {
    BPNode *node = tree->root;

    /* ── lo에 해당하는 리프까지 내려가기 ── */
    while (!node->is_leaf) {
        int i = 0;
        while (i < node->num_keys && lo >= node->keys[i]) i++;
        node = node->children[i];
    }

    /* ── 리프 연결 리스트를 따라가며 범위 내 키 카운트 ── */
    int count    = 0;
    BPLeaf *leaf = node->leaf;
    while (leaf != NULL) {
        for (int i = 0; i < leaf->num_keys; i++) {
            if (leaf->keys[i] > hi)  return count;  /* hi 초과 → 즉시 종료 */
            if (leaf->keys[i] >= lo) count++;        /* [lo, hi] 범위 내 카운트 */
        }
        leaf = leaf->next;  /* 다음 리프로 이동 */
    }
    return count;
}

/* ====================================================================
 * 메모리 해제
 * ==================================================================== */

/*
 * free_bp_node: 재귀적으로 BPNode와 하위 노드를 모두 해제한다.
 *
 * - is_leaf == 1: BPLeaf도 함께 해제
 * - is_leaf == 0: 자식 BPNode들을 먼저 재귀 해제, 그 다음 자신 해제
 *
 * 후위 순회(post-order) 방식으로 메모리 누수 없이 해제.
 */
static void free_bp_node(BPNode *node) {
    if (node == NULL) return;
    if (node->is_leaf) {
        free(node->leaf);   /* BPLeaf 해제 */
    } else {
        /* 내부 노드: children[0..num_keys] 재귀 해제 */
        for (int i = 0; i <= node->num_keys; i++)
            free_bp_node(node->children[i]);
    }
    free(node);  /* BPNode 자신 해제 */
}

/*
 * bptree_free: B+ 트리 전체 메모리 해제.
 * 루트부터 모든 노드 해제 후 BPTree 구조체도 해제.
 */
void bptree_free(BPTree *tree) {
    free_bp_node(tree->root);
    free(tree);
}
