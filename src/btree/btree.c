/*
 * btree.c  ─  B 트리 (B-Tree) 구현
 *
 * 핵심 개념 요약:
 *  1. 삽입: 재귀적으로 적절한 리프까지 내려가 삽입. 꽉 차면 split_node()로 분할.
 *  2. 탐색: 루트부터 내려가며 모든 노드(내부 + 리프)에서 키를 비교.
 *  3. 범위 탐색: 재귀로 전체 트리를 순회하며 범위 내 키를 카운트.
 *  4. 메모리 해제: 후위 순회(post-order)로 모든 노드를 free.
 */

#include "btree.h"
#include <stdlib.h>
#include <string.h>

/* ====================================================================
 * 내부 헬퍼 함수 (파일 밖에서는 접근 불가 - static)
 * ==================================================================== */

/*
 * new_node: 새 BTNode를 heap에 할당하고 초기화한다.
 *
 * calloc을 사용하므로 모든 필드가 0/NULL로 초기화된다.
 * is_leaf 매개변수로 리프 여부를 지정한다.
 */
static BTNode *new_node(int is_leaf) {
    BTNode *n = calloc(1, sizeof(BTNode));
    n->is_leaf = is_leaf;
    return n;
}

/*
 * find_pos: node 내에서 key가 삽입되거나 탐색될 위치 인덱스를 반환한다.
 *
 * 반환값 i의 의미:
 *   - keys[0..i-1] < key 이고 keys[i] >= key
 *   - 즉, key를 삽입한다면 i번째 자리에 들어가야 함
 *
 * 예시 (keys = [10, 20, 30], key = 15):
 *   i=0: 10 < 15 → i++
 *   i=1: 20 >= 15 → 정지, return 1
 *   → children[1]로 이동하거나, keys[1] 앞에 삽입
 */
static int find_pos(BTNode *node, int key) {
    int i = 0;
    while (i < node->num_keys && node->keys[i] < key)
        i++;
    return i;
}

/*
 * Split: 노드 분할 결과를 담는 구조체
 *
 * B 트리의 분할(move-up) 방식:
 *   꽉 찬 노드 [k0, k1, ..., k_mid, ..., k_n]
 *         ↓ 분할
 *   왼쪽: [k0, ..., k_mid-1]   (mid개)
 *   승격:  k_mid               ← 부모로 올라감, 양쪽 노드에서 제거
 *   오른쪽: [k_mid+1, ..., k_n] (나머지)
 *
 * B+ 트리와의 차이: B+ 트리는 copy-up(리프의 첫 키를 복사)을 사용하지만,
 * B 트리는 move-up(중간 키가 부모로 이동, 양쪽 노드에서 사라짐)을 사용.
 */
typedef struct {
    int     key;    /* 부모로 올라갈 키 */
    void   *ptr;    /* 해당 키의 레코드 포인터 */
    BTNode *right;  /* 새로 생긴 오른쪽 노드 */
} Split;

/*
 * split_node: 꽉 찬 노드(num_keys == BT_ORDER)를 둘로 분할한다.
 *
 * 매개변수:
 *   node - 분할할 노드 (왼쪽으로 남음)
 *   out  - 분할 결과를 저장할 Split 구조체
 *
 * 분할 예시 (BT_ORDER=4, mid=2):
 *   분할 전: keys = [10, 20, 30, 40]
 *   분할 후:
 *     왼쪽(node): keys = [10, 20]        (mid=2개)
 *     승격 키:    key  = 30, ptr = ptrs[2]
 *     오른쪽:     keys = [40]             (BT_ORDER-mid-1=1개)
 */
static void split_node(BTNode *node, Split *out) {
    int mid         = BT_ORDER / 2;   /* 분할 기준점: 16 */
    int right_count = node->num_keys - mid - 1;  /* 오른쪽 노드의 키 개수 */

    /* 오른쪽 노드 생성 (왼쪽과 같은 is_leaf 값) */
    BTNode *right = new_node(node->is_leaf);

    /* mid+1 이후의 키와 레코드 포인터를 오른쪽 노드로 복사 */
    for (int i = 0; i < right_count; i++) {
        right->keys[i] = node->keys[mid + 1 + i];
        right->ptrs[i] = node->ptrs[mid + 1 + i];
    }

    /* 내부 노드일 경우: 자식 포인터도 함께 이전 */
    if (!node->is_leaf) {
        for (int i = 0; i <= right_count; i++)
            right->children[i] = node->children[mid + 1 + i];
    }
    right->num_keys = right_count;

    /* 중간 키(keys[mid])가 부모로 승격됨 */
    out->key   = node->keys[mid];
    out->ptr   = node->ptrs[mid];
    out->right = right;

    /* 왼쪽 노드(node)는 mid개의 키만 남김 */
    node->num_keys = mid;
}

/*
 * insert_rec: 재귀 삽입 함수.
 *
 * 동작 흐름:
 *   1. find_pos()로 삽입 위치 pos 계산
 *   2. 키 중복이면 포인터만 갱신하고 종료
 *   3-a. 리프 노드: 해당 위치에 직접 삽입
 *   3-b. 내부 노드: children[pos]에 재귀 호출
 *   4. 삽입 후 num_keys == BT_ORDER면 split_node() 호출 → 분할 결과 반환
 *
 * 반환값:
 *   1 → 분할 발생 (s에 결과 저장됨)
 *   0 → 분할 없음
 */
static int insert_rec(BTNode *node, int key, void *record_ptr, Split *s) {
    /* 현재 노드에서 삽입 위치 계산 */
    int pos = find_pos(node, key);

    /* ── 중복 키 처리: 포인터(레코드)만 갱신 ── */
    if (pos < node->num_keys && node->keys[pos] == key) {
        node->ptrs[pos] = record_ptr;
        return 0;  /* 분할 없음 */
    }

    if (node->is_leaf) {
        /*
         * ── 리프 노드: 해당 위치에 키와 포인터 삽입 ──
         *
         * memmove를 사용해 pos 이후 원소들을 한 칸씩 오른쪽으로 이동.
         * 예: keys = [10, 30], pos=1, key=20 삽입 시
         *   memmove 후: keys = [10, ?, 30]
         *   삽입 후:    keys = [10, 20, 30]
         */
        memmove(&node->keys[pos + 1], &node->keys[pos],
                sizeof(int) * (node->num_keys - pos));
        memmove(&node->ptrs[pos + 1], &node->ptrs[pos],
                sizeof(void *) * (node->num_keys - pos));
        node->keys[pos] = key;
        node->ptrs[pos] = record_ptr;
        node->num_keys++;
    } else {
        /*
         * ── 내부 노드: children[pos]에 재귀 삽입 ──
         *
         * find_pos()가 반환한 pos는 key가 들어갈 자식의 인덱스.
         *   children[0] < keys[0] <= children[1] < keys[1] ...
         *   key < keys[pos] 이므로 children[pos]가 올바른 서브트리.
         */
        Split child_split;
        int promoted = insert_rec(node->children[pos], key, record_ptr, &child_split);

        if (promoted) {
            /*
             * 자식에서 분할 발생 → 승격된 키를 현재 노드의 pos 위치에 삽입.
             *
             * 키, 포인터, 자식 포인터 모두 한 칸씩 오른쪽으로 밀어야 한다.
             * children[pos+1]이 분할로 생긴 오른쪽 자식.
             */
            memmove(&node->keys[pos + 1], &node->keys[pos],
                    sizeof(int) * (node->num_keys - pos));
            memmove(&node->ptrs[pos + 1], &node->ptrs[pos],
                    sizeof(void *) * (node->num_keys - pos));
            memmove(&node->children[pos + 2], &node->children[pos + 1],
                    sizeof(BTNode *) * (node->num_keys - pos));
            node->keys[pos]         = child_split.key;
            node->ptrs[pos]         = child_split.ptr;
            node->children[pos + 1] = child_split.right;
            node->num_keys++;
        }
    }

    /* ── 오버플로우 확인: BT_ORDER개가 되면 분할 필요 ── */
    if (node->num_keys == BT_ORDER) {
        split_node(node, s);
        return 1;  /* 분할 발생 알림 */
    }
    return 0;
}

/* ====================================================================
 * 공개 API
 * ==================================================================== */

/*
 * btree_create: 빈 B 트리 생성.
 *
 * 처음에는 루트가 리프 노드 하나로 시작한다.
 * 데이터가 삽입되면서 분할이 일어나 트리가 위로 성장한다.
 */
BTree *btree_create(void) {
    BTree *t = calloc(1, sizeof(BTree));
    t->root  = new_node(1);  /* 초기 루트 = 리프 노드 */
    return t;
}

/*
 * btree_insert: 트리에 (key, record_ptr) 쌍을 삽입한다.
 *
 * insert_rec()이 1을 반환하면 루트가 분할된 것이므로
 * 새 루트를 만들어 기존 루트와 분할된 오른쪽 노드를 자식으로 연결한다.
 *
 *  [분할 전]         [분할 후]
 *   (root)     →     (new_root)
 *                    /        \
 *               (old_root)  (s.right)
 *                   승격 키: s.key
 */
void btree_insert(BTree *tree, int key, void *record_ptr) {
    Split s;
    int promoted = insert_rec(tree->root, key, record_ptr, &s);
    if (promoted) {
        /* 루트가 분할됨 → 새 루트 생성 (내부 노드) */
        BTNode *new_root      = new_node(0);   /* is_leaf = 0 (내부 노드) */
        new_root->keys[0]     = s.key;          /* 승격된 키 */
        new_root->ptrs[0]     = s.ptr;          /* 승격된 키의 레코드 포인터 */
        new_root->children[0] = tree->root;     /* 왼쪽 자식 = 기존 루트 */
        new_root->children[1] = s.right;        /* 오른쪽 자식 = 분할된 노드 */
        new_root->num_keys    = 1;
        tree->root = new_root;
    }
}

/*
 * btree_search: key에 해당하는 레코드 포인터를 반환한다.
 *
 * B 트리 탐색의 특징:
 *   - 내부 노드에도 레코드가 있으므로, 내려가는 도중 발견하면 즉시 반환.
 *   - B+ 트리와 달리 항상 리프까지 내려가지 않아도 된다.
 *
 * 예시 (키 30 탐색):
 *   루트 내부노드 keys=[20, 40]
 *     → 30 > 20, 30 < 40 → children[1] 로 이동
 *   리프 keys=[25, 30, 35]
 *     → keys[1] == 30 → ptrs[1] 반환
 */
void *btree_search(BTree *tree, int key) {
    BTNode *node = tree->root;
    while (node != NULL) {
        int i = 0;
        /* key보다 작은 키를 건너뜀 */
        while (i < node->num_keys && node->keys[i] < key)
            i++;

        if (i < node->num_keys && node->keys[i] == key)
            return node->ptrs[i];   /* 발견! 레코드 포인터 반환 */

        if (node->is_leaf)
            return NULL;            /* 리프까지 내려왔는데 없음 */

        /* children[i]로 이동: keys[i] > key 이므로 왼쪽 서브트리 */
        node = node->children[i];
    }
    return NULL;
}

/*
 * range_rec: 범위 탐색 재귀 헬퍼.
 *
 * B 트리의 범위 탐색은 트리 전체를 재귀적으로 순회해야 한다.
 * (B+ 트리처럼 리프 연결 리스트가 없기 때문)
 *
 * 순회 규칙:
 *   1. children[i]에는 keys[i-1] < k < keys[i] 범위의 키만 있다.
 *      따라서 lo < keys[i]일 때만 children[i]를 방문할 가치가 있다.
 *   2. keys[i] > hi 이면 이후 키와 자식은 범위 밖 → 조기 종료.
 *   3. lo <= keys[i] <= hi 이면 현재 키를 카운트에 포함.
 *   4. 마지막 자식(children[num_keys])은 가장 큰 키보다 큰 값들을 가짐.
 *
 * 예시 (lo=15, hi=35, 트리: 20─[10,20,30]─40):
 *   i=0: lo(15) < keys[0](10)? NO → children[0] 미방문
 *        keys[0](10) > hi(35)? NO
 *        lo(15) <= keys[0](10)? NO → 카운트 안 함
 *   i=1: lo(15) < keys[1](20)? YES → children[1] 방문
 *        keys[1](20) > hi? NO
 *        lo <= keys[1](20) <= hi? YES → count++
 *   ...
 */
static int range_rec(BTNode *node, int lo, int hi) {
    if (node == NULL) return 0;
    int count = 0;

    for (int i = 0; i < node->num_keys; i++) {
        /*
         * children[i]의 키들은 모두 keys[i]보다 작다.
         * lo < keys[i]일 때만 children[i]에 범위 내 키가 있을 수 있음.
         */
        if (!node->is_leaf && lo < node->keys[i])
            count += range_rec(node->children[i], lo, hi);

        /* keys[i]가 hi를 초과하면 이후는 모두 범위 밖 → 즉시 반환 */
        if (node->keys[i] > hi)
            return count;

        /* 현재 키가 [lo, hi] 범위 안에 있으면 카운트 */
        if (node->keys[i] >= lo)
            count++;
    }

    /*
     * 가장 오른쪽 자식 방문.
     * children[num_keys]에는 마지막 키보다 큰 값들이 있으므로
     * hi가 충분히 크면 여기도 확인해야 한다.
     */
    if (!node->is_leaf)
        count += range_rec(node->children[node->num_keys], lo, hi);

    return count;
}

/*
 * btree_range: lo 이상 hi 이하 키의 개수를 반환한다.
 * 내부적으로 range_rec()을 루트부터 호출한다.
 */
int btree_range(BTree *tree, int lo, int hi) {
    return range_rec(tree->root, lo, hi);
}

/* ====================================================================
 * 메모리 해제
 * ==================================================================== */

/*
 * free_node: 재귀적으로 노드와 그 하위 노드를 모두 해제한다.
 *
 * 후위 순회(post-order): 자식을 먼저 해제하고 자신을 해제.
 * 내부 노드는 자식이 있으므로, 자식들을 먼저 순회 후 자신을 free.
 * 리프 노드는 자식이 없으므로 바로 free.
 */
static void free_node(BTNode *node) {
    if (node == NULL) return;
    if (!node->is_leaf) {
        /* 내부 노드: children[0..num_keys] 까지 재귀 해제 */
        for (int i = 0; i <= node->num_keys; i++)
            free_node(node->children[i]);
    }
    free(node);
}

/*
 * btree_free: 트리 전체 메모리 해제.
 * 루트부터 모든 노드를 해제한 뒤 BTree 구조체 자체도 해제한다.
 */
void btree_free(BTree *tree) {
    free_node(tree->root);
    free(tree);
}
