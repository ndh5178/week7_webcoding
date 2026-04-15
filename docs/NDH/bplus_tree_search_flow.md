# B+ 트리 탐색 흐름 정리

이 문서는 **현재 프로젝트 코드 기준**으로  
`B+ 트리에서 ID를 어떻게 찾는지`를 정리한 문서입니다.

여기서는 웹, 서버, API 흐름은 빼고  
**B+ 트리 내부에서 탐색이 어떻게 진행되는지**만 봅니다.

관련 파일:
- [bplus_tree.c](C:\Users\user\Desktop\정글\수요코딩회\7주차\week7_webcoding\src\bplus_tree\bplus_tree.c)
- [bplus_tree.h](C:\Users\user\Desktop\정글\수요코딩회\7주차\week7_webcoding\src\bplus_tree\bplus_tree.h)

---

## 1. 현재 코드에서 중요한 구조

현재 헤더 파일에는 B+ 트리의 핵심 구조가 이렇게 정의되어 있습니다.

```c
#define BP_ORDER 32

typedef struct BPLeaf {
    int            keys[BP_ORDER + 1];
    void          *ptrs[BP_ORDER + 1];
    int            num_keys;
    struct BPLeaf *next;
} BPLeaf;
```

```c
typedef struct BPNode {
    int            keys[BP_ORDER + 1];
    struct BPNode *children[BP_ORDER + 2];
    int            num_keys;
    int            is_leaf;
    BPLeaf        *leaf;
} BPNode;
```

핵심 해석:
- `BP_ORDER = 32`
- 내부 노드는 `keys[]` 와 `children[]` 로 다음 자식을 고르는 역할
- 리프 노드는 `BPLeaf` 안에 실제 `key -> ptr`를 저장
- 리프들은 `next`로 연결되어 있어서 범위 탐색이 가능

즉 현재 구현도 **실제 데이터 포인터는 leaf에 있다**는 B+ 트리 구조를 유지합니다.

---

## 2. 탐색 시작 함수는 `bptree_search()`

ID 하나를 찾을 때 시작점은 이 함수입니다.

```c
void *bptree_search(BPTree *tree, int key) {
    BPLeaf *leaf;
    int i;

    if (!tree || !tree->root) {
        return NULL;
    }

    leaf = find_leaf_node(tree->root, key);
    if (!leaf) {
        return NULL;
    }

    for (i = 0; i < leaf->num_keys; ++i) {
        g_op_count++;
        if (leaf->keys[i] == key) {
            return leaf->ptrs[i];
        }
        if (leaf->keys[i] > key) {
            return NULL;
        }
    }

    return NULL;
}
```

이 함수의 흐름은 두 단계입니다.

1. `find_leaf_node()`로 key가 있을 만한 리프를 찾는다.
2. 그 리프 안에서 실제 key를 확인하고 `ptr`를 반환한다.

---

## 3. 현재 구현에서 가장 중요한 점

예전 설명과 비교했을 때 현재 코드에서 달라진 핵심은 이겁니다.

- 내부 노드에서 자식 선택할 때 `upper_bound_keys()`를 직접 쓰지 않음
- 리프에서 최종 key 찾을 때 `lower_bound_keys()`를 직접 쓰지 않음
- 대신 **비교 횟수를 세기 위해 순차 비교 방식**이 들어감

즉 현재 코드는  
`탐색 원리 설명용`이면서 동시에  
`비교 횟수 측정용`으로도 바뀌어 있습니다.

그 비교 횟수는 전역 카운터 `g_op_count`로 셉니다.

---

## 4. leaf까지 내려가는 함수: `find_leaf_node()`

현재 코드 기준 함수는 이렇게 생겼습니다.

```c
static BPLeaf *find_leaf_node(BPNode *node, int key) {
    BPNode *current = node;

    while (current && !current->is_leaf) {
        int idx = 0;
        while (idx < current->num_keys && current->keys[idx] <= key) {
            g_op_count++;
            idx++;
        }
        if (idx < current->num_keys) {
            g_op_count++;
        }
        current = current->children[idx];
    }

    return current ? current->leaf : NULL;
}
```

이 함수가 하는 일은:

1. 루트에서 시작한다.
2. 현재 노드가 리프가 아니면, `keys[]`를 왼쪽부터 본다.
3. `key`보다 작거나 같은 동안 계속 오른쪽으로 이동한다.
4. 최종적으로 갈 자식 인덱스 `idx`를 정한다.
5. 그 자식으로 내려간다.
6. 리프에 도달하면 그 리프의 `BPLeaf *`를 반환한다.

---

## 5. 내부 노드에서 자식을 고르는 방식

현재 구현은 내부 노드에서 이렇게 비교합니다.

```c
while (idx < current->num_keys && current->keys[idx] <= key) {
    g_op_count++;
    idx++;
}
if (idx < current->num_keys) {
    g_op_count++;
}
current = current->children[idx];
```

이 의미는:

- `keys[idx] <= key` 인 동안 계속 오른쪽으로 간다
- 처음으로 `key`보다 큰 값을 만나면 거기서 멈춘다
- 멈춘 위치의 `idx`에 해당하는 자식으로 내려간다

예를 들어 현재 노드의 key가:

```text
[10, 20, 30]
```

라면 자식 구간은 이렇게 해석할 수 있습니다.

- `child[0]`: 10보다 작은 값
- `child[1]`: 10 이상 20 미만
- `child[2]`: 20 이상 30 미만
- `child[3]`: 30 이상

즉 내부 노드는 실제 데이터를 찾는 곳이 아니라  
**어느 구간으로 내려가야 하는지 정하는 안내판 역할**을 합니다.

---

## 6. 리프에 도착한 뒤 실제 key를 찾는 방식

리프에 도달하면 현재 구현은 이렇게 확인합니다.

```c
for (i = 0; i < leaf->num_keys; ++i) {
    g_op_count++;
    if (leaf->keys[i] == key) {
        return leaf->ptrs[i];
    }
    if (leaf->keys[i] > key) {
        return NULL;
    }
}
```

이 로직은 다음 뜻입니다.

1. 리프 안의 key를 앞에서부터 확인한다.
2. 같은 key를 찾으면 바로 대응하는 포인터를 반환한다.
3. 현재 key가 찾는 값보다 커지는 순간, 뒤에는 더 볼 필요가 없으므로 `NULL`을 반환한다.

즉 리프 내부는 현재 코드 기준으로  
**정렬된 배열을 앞에서부터 확인하는 방식**입니다.

완전 무식하게 끝까지 보는 것은 아니고,

- `== key` 이면 성공
- `> key` 이면 조기 종료

가 들어가 있습니다.

---

## 7. `g_op_count`는 왜 중요한가

현재 구현은 단순히 찾기만 하는 코드가 아니라  
**비교 횟수도 측정하는 코드**입니다.

예를 들어:

- 내부 노드에서 어떤 자식으로 내려갈지 판단할 때 비교
- 리프에서 실제 key를 확인할 때 비교
- 범위 탐색에서 각 key를 검사할 때 비교

이 비교들을 모두 `g_op_count++`로 셉니다.

즉 지금 프로젝트에서는 시간뿐 아니라  
**탐색 과정에서 몇 번 비교했는가**도 같이 보여주기 위해
탐색 함수가 약간 계측된 상태입니다.

---

## 8. 최종적으로 반환하는 값은 무엇인가

이 줄이 가장 중요합니다.

```c
return leaf->ptrs[i];
```

즉 B+ 트리는 key를 찾고 끝나는 것이 아니라  
그 key에 연결된 **실제 레코드 주소**를 돌려줍니다.

현재 프로젝트에서는 보통 이런 형태로 들어갑니다.

```c
bptree_insert(tree, players[i].id, &players[i]);
```

즉:
- key: `players[i].id`
- value: `&players[i]`

따라서 검색 결과는 보통 `Player *`로 해석됩니다.

---

## 9. 현재 코드 기준 탐색 흐름 한 번에 보기

예를 들어 `id = 500000`을 찾는다고 하면 흐름은 이렇습니다.

1. `bptree_search(tree, 500000)` 호출
2. `find_leaf_node(tree->root, 500000)` 호출
3. 루트에서 시작
4. 내부 노드의 `keys[]`를 앞에서부터 비교
5. 해당 key가 들어 있을 구간의 자식으로 이동
6. 리프 노드가 나올 때까지 반복
7. 리프에 도착하면 `leaf->keys[]`를 앞에서부터 확인
8. `== 500000` 이면 `leaf->ptrs[i]` 반환
9. `> 500000` 이 먼저 나오면 `NULL` 반환

즉 현재 코드 흐름은 한 줄로 요약하면:

`내부 노드에서 구간을 정해 leaf까지 내려가고, leaf에서 실제 key를 확인한 뒤 record pointer를 반환한다`

입니다.

---

## 10. 범위 탐색은 왜 B+ 트리가 유리한가

현재 범위 탐색 함수는 이렇게 시작합니다.

```c
int bptree_range(BPTree *tree, int lo, int hi) {
    BPLeaf *leaf;
    int count = 0;

    if (!tree || !tree->root || lo > hi) {
        return 0;
    }

    leaf = find_leaf_node(tree->root, lo);
    while (leaf) {
        int i;

        for (i = 0; i < leaf->num_keys; ++i) {
            g_op_count++;
            if (leaf->keys[i] < lo) {
                continue;
            }
            if (leaf->keys[i] > hi) {
                return count;
            }
            count += 1;
        }

        leaf = leaf->next;
    }

    return count;
}
```

핵심은:

1. 먼저 `lo`가 있을 만한 leaf까지 한 번 내려간다.
2. 그 다음부터는 `leaf->next`를 따라가며 순차적으로 본다.

이 구조 때문에 B+ 트리는 범위 탐색에서 강합니다.

내부 노드로 다시 돌아갈 필요 없이  
**연결된 리프만 따라가면 되기 때문**입니다.

---

## 11. 현재 코드에서 읽어야 할 핵심 함수 순서

현재 폴더 기준으로 B+ 트리 탐색 흐름을 따라가려면
아래 순서로 보면 가장 좋습니다.

1. `bptree_search()`
2. `find_leaf_node()`
3. `bptree_range()`
4. `bptree_insert()`
5. `insert_recursive()`

탐색만 이해하려면 우선

`bptree_search -> find_leaf_node -> leaf 내부 비교`

이 3단계만 보면 됩니다.

---

## 12. 한 문장 정리

현재 프로젝트의 B+ 트리 탐색은  
**루트에서 시작해 내부 노드의 key들을 비교하며 해당 리프까지 내려간 뒤, 리프에서 실제 key를 확인하고 연결된 레코드 포인터를 반환하는 방식**입니다.
