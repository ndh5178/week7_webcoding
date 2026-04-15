# B+ 트리 탐색 흐름 정리

이 문서는 **현재 프로젝트의 B+ 트리 구현 기준**으로  
`ID 하나를 B+ 트리에서 어떻게 찾는지`를 코드 중심으로 정리한 문서입니다.

여기서는 웹, 서버, API 흐름은 빼고  
**B+ 트리 내부에서 탐색이 어떻게 진행되는지**만 봅니다.

관련 핵심 파일:
- [C:\Users\user\Desktop\정글\수요코딩회\7주차\week7_webcoding\src\bplus_tree\bplus_tree.c](C:\Users\user\Desktop\정글\수요코딩회\7주차\week7_webcoding\src\bplus_tree\bplus_tree.c)
- [C:\Users\user\Desktop\정글\수요코딩회\7주차\week7_webcoding\src\bplus_tree\bplus_tree.h](C:\Users\user\Desktop\정글\수요코딩회\7주차\week7_webcoding\src\bplus_tree\bplus_tree.h)

---

## 1. B+ 트리에서 실제 데이터를 어디에 저장하는가

현재 구현에서 중요한 점은:

- **내부 노드**는 길 안내 역할
- **리프 노드**는 실제 `key -> record_ptr` 저장 역할

헤더를 보면 구조가 이렇게 나뉘어 있습니다.

```c
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

핵심은:
- `BPNode`는 트리 노드
- `is_leaf == 1` 이면 이 노드는 리프 노드
- 리프 노드일 때는 `leaf` 안에 실제 key와 ptr이 들어 있음

즉 B+ 트리에서 **실제 Player 포인터는 leaf->ptrs[]에 저장**됩니다.

---

## 2. 탐색의 시작점: `bptree_search`

ID 탐색의 시작 함수는 이 함수입니다.

```c
void *bptree_search(BPTree *tree, int key) {
    BPLeaf *leaf;
    int idx;

    if (!tree || !tree->root) {
        return NULL;
    }

    leaf = find_leaf_node(tree->root, key);
    if (!leaf) {
        return NULL;
    }

    idx = lower_bound_keys(leaf->keys, leaf->num_keys, key);
    if (idx < leaf->num_keys && leaf->keys[idx] == key) {
        return leaf->ptrs[idx];
    }

    return NULL;
}
```

이 함수는 크게 두 단계입니다.

1. `find_leaf_node()`로 **해당 key가 있을 만한 leaf까지 내려감**
2. 그 leaf 안에서 **진짜 key가 있는지 확인하고 ptr 반환**

즉 탐색의 본질은:

`루트에서 leaf까지 내려가기 + leaf 내부에서 최종 확인`

입니다.

---

## 3. 왜 먼저 leaf까지 내려가는가

B+ 트리는 B 트리와 다르게  
**실제 데이터가 내부 노드가 아니라 리프에만 있기 때문**입니다.

그래서 탐색 흐름은 항상:

1. 내부 노드는 길 찾기
2. 리프 노드에서 실제 확인

이 됩니다.

즉 B+ 트리에서 내부 노드는:
- "왼쪽으로 가라"
- "오른쪽으로 가라"
- "이 구간은 저 자식이다"

를 알려주는 역할만 합니다.

---

## 4. leaf를 찾는 함수: `find_leaf_node`

실제 이동은 `find_leaf_node()`에서 일어납니다.

```c
static BPLeaf *find_leaf_node(BPNode *node, int key) {
    BPNode *current = node;

    while (current && !current->is_leaf) {
        int idx = upper_bound_keys(current->keys, current->num_keys, key);
        current = current->children[idx];
    }

    return current ? current->leaf : NULL;
}
```

이 함수의 의미를 한 줄씩 보면:

- `current = node`
  - 현재 노드를 루트로 시작

- `while (current && !current->is_leaf)`
  - 리프가 나올 때까지 계속 내려감

- `upper_bound_keys(...)`
  - 현재 내부 노드에서 `key`가 어느 자식 구간으로 가야 하는지 계산

- `current = current->children[idx]`
  - 그 자식으로 이동

- 마지막에 `current->leaf` 반환
  - 최종적으로 도착한 리프 노드의 실제 leaf 구조체 반환

즉 이 함수는  
**"ID가 속한 leaf 페이지를 찾는 함수"** 라고 보면 됩니다.

---

## 5. 내부 노드에서 어느 자식을 선택하는가

중요한 부분은 이 줄입니다.

```c
int idx = upper_bound_keys(current->keys, current->num_keys, key);
```

여기서 `upper_bound_keys()`는

```c
static int upper_bound_keys(const int *keys, int count, int key) {
    int lo = 0;
    int hi = count;

    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (keys[mid] <= key) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }

    return lo;
}
```

이 함수가 하는 일:

- 현재 노드의 key 배열에서
- `key보다 큰 값이 처음 나타나는 위치`를 찾음

쉽게 말하면:
- key가 10보다 작으면 첫 번째 자식
- key가 10 이상 20 미만이면 두 번째 자식
- key가 20 이상 30 미만이면 세 번째 자식

이런 식으로 **구간을 선택**합니다.

---

## 6. 왜 `upper_bound_keys`를 쓰는가

B+ 트리 내부 노드의 key는  
보통 **오른쪽 자식의 최소 key**를 의미합니다.

예를 들어 내부 노드가 이런 key를 가지고 있다고 합시다.

```text
[10, 20, 30]
```

그러면 자식 구간은 보통 이렇게 해석할 수 있습니다.

- child[0] : 10보다 작은 값
- child[1] : 10 이상 20 미만
- child[2] : 20 이상 30 미만
- child[3] : 30 이상

이 규칙 때문에 `key >= keys[i]` 이면 오른쪽으로 가야 하므로  
`upper_bound_keys()`를 쓰는 게 자연스럽습니다.

즉:
- `lower_bound`는 leaf 안에서 최종 위치 찾기
- `upper_bound`는 내부 노드에서 자식 선택하기

이렇게 역할이 나뉩니다.

---

## 7. leaf에 도착한 뒤 최종 위치를 찾는 방식

leaf에 도착하면 `bptree_search()`가 다시 이 코드를 사용합니다.

```c
idx = lower_bound_keys(leaf->keys, leaf->num_keys, key);
if (idx < leaf->num_keys && leaf->keys[idx] == key) {
    return leaf->ptrs[idx];
}
```

여기서 `lower_bound_keys()`는:

```c
static int lower_bound_keys(const int *keys, int count, int key) {
    int lo = 0;
    int hi = count;

    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (keys[mid] < key) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }

    return lo;
}
```

이 함수는  
**`key 이상이 처음 나오는 위치`** 를 찾습니다.

예를 들어 leaf 안에:

```text
[41, 52, 67, 80]
```

가 있고 `key = 67`이면:
- `lower_bound_keys()`는 index 2를 반환
- `leaf->keys[2] == 67` 확인
- 맞으면 `leaf->ptrs[2]` 반환

---

## 8. 반환되는 값은 무엇인가

최종 반환은 이 줄입니다.

```c
return leaf->ptrs[idx];
```

즉 B+ 트리는 key를 찾았다고 끝나는 게 아니라,  
그 key에 연결된 **실제 레코드 포인터**를 반환합니다.

현재 프로젝트에서는 이 포인터가 보통 `Player*`입니다.

예를 들어:

```c
bptree_insert(bptree, players[i].id, &players[i]);
```

이렇게 넣었기 때문에,

```c
Player *found = (Player *)bptree_search(tree, 500000);
```

를 하면 `id == 500000`인 Player 구조체 주소를 받게 됩니다.

---

## 9. 탐색 흐름을 그림처럼 보면

예를 들어 `id = 500000`을 찾는다고 하면:

1. `bptree_search(tree, 500000)` 호출
2. `find_leaf_node(root, 500000)` 호출
3. 루트에서 `500000`이 어느 구간인지 계산
4. 해당 자식으로 이동
5. 내부 노드가 더 있으면 같은 과정을 반복
6. 리프 노드 도착
7. leaf 안에서 `lower_bound_keys()`로 최종 위치 계산
8. key가 정확히 일치하면 `leaf->ptrs[idx]` 반환
9. 없으면 `NULL`

즉 한 줄로 쓰면:

`내부 노드에서는 구간 선택, 리프 노드에서는 실제 key 확인`

입니다.

---

## 10. B+ 트리가 빠른 이유를 이 코드 기준으로 보면

이 코드에서 B+ 트리가 빠른 이유는 크게 두 가지입니다.

### 1. 내부 노드에서 범위를 빠르게 줄인다

```c
int idx = upper_bound_keys(current->keys, current->num_keys, key);
current = current->children[idx];
```

이 과정을 반복하면서  
전체 데이터를 다 보지 않고  
해당 key가 있을 만한 구간으로 바로 내려갑니다.

### 2. 리프에서 정렬된 key 배열을 다시 이진 탐색한다

```c
idx = lower_bound_keys(leaf->keys, leaf->num_keys, key);
```

즉 leaf 안에서도 무식하게 처음부터 끝까지 찾지 않고,  
정렬된 구조를 이용해 빠르게 위치를 찾습니다.

---

## 11. 범위 탐색은 왜 더 유리한가

현재 구현의 범위 탐색 함수:

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

핵심:
- 먼저 `lo`가 있는 leaf까지 한 번 내려감
- 이후에는 `leaf->next`를 따라가며 범위를 순회

즉 B+ 트리의 리프 연결 구조 덕분에  
범위 탐색이 매우 자연스럽습니다.

이게 B 트리보다 B+ 트리가 범위 탐색에 강하다고 설명하는 핵심 이유입니다.

---

## 12. 코드 읽을 때 집중해서 볼 함수들

현재 구현에서 B+ 트리 탐색 흐름을 이해하려면 이 함수들만 보면 됩니다.

1. `bptree_search()`
2. `find_leaf_node()`
3. `upper_bound_keys()`
4. `lower_bound_keys()`
5. `bptree_range()`

즉 순서는:

`bptree_search -> find_leaf_node -> upper_bound_keys -> lower_bound_keys`

입니다.

---

## 13. 한 문장 정리

현재 프로젝트의 B+ 트리 탐색은  
**루트에서 시작해 내부 노드의 key 배열로 적절한 자식을 계속 선택해 leaf까지 내려간 뒤, leaf 안에서 key를 최종 확인하고 해당 record pointer를 반환하는 방식**입니다.
