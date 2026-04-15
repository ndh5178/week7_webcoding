# bptree_search에서 key, idx, i가 움직이는 방식

이 문서는 현재 프로젝트의 B+ 트리 코드에서  
`void *bptree_search(BPTree *tree, int key)`가 실제로 어떻게 동작하는지,  
특히 `key`, `idx`, `i`가 어떤 역할을 하는지 예시 숫자를 넣어서 정리한 문서입니다.

관련 파일:
- [bplus_tree.c](C:\Users\user\Desktop\정글\수요코딩회\7주차\week7_webcoding\src\bplus_tree\bplus_tree.c)

---

## 1. 찾고 싶은 ID는 어디에 들어가나

탐색 함수 시작은 이렇게 생겼습니다.

```c
void *bptree_search(BPTree *tree, int key) {
    BPLeaf *leaf;
    int i;
```

여기서:
- `tree`: B+ 트리 전체
- `key`: 찾고 싶은 ID 값
- `leaf`: 최종적으로 도착한 리프 노드
- `i`: 리프 안에서 몇 번째 key를 보고 있는지 나타내는 인덱스

즉 `ID 인덱스`라고 부를 값은 현재 코드에서는 `key`입니다.

예를 들어:

```c
Player *found = (Player *)bptree_search(tree, 500000);
```

라고 호출하면 함수 안에서는:

```c
tree = B+ 트리 주소
key = 500000
leaf = 아직 모름
i = 아직 사용 안 함
```

이 상태로 시작합니다.

---

## 2. 먼저 leaf까지 내려간다

`bptree_search()` 안에서 제일 먼저 하는 일은 이 줄입니다.

```c
leaf = find_leaf_node(tree->root, key);
```

예를 들어 실제로는 이렇게 됩니다.

```c
leaf = find_leaf_node(tree->root, 500000);
```

즉 `500000`이라는 값은 그대로 들고 내려가고,  
내부 노드에서는 이 값을 기준으로 어느 자식으로 갈지 결정합니다.

---

## 3. 내부 노드에서 움직이는 값은 `idx`

현재 구현의 `find_leaf_node()`는 이렇게 생겼습니다.

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

여기서 중요한 건:
- `key`는 계속 같은 값
- `idx`는 현재 노드에서 몇 번째 자식으로 내려갈지 결정하는 값

즉:
- `key = 찾고 싶은 ID`
- `idx = 현재 노드에서 선택된 자식 번호`

---

## 4. 직접 대입해 보기: 루트에서 한 번 내려가기

예를 들어 현재 루트 노드가 이렇게 생겼다고 가정해봅시다.

```text
current->keys = [200000, 400000, 700000]
key = 500000
```

그럼 코드가 이렇게 돌아갑니다.

```c
idx = 0;
```

### 첫 번째 비교

```c
current->keys[0] <= key
200000 <= 500000
```

참이므로:

```c
idx = 1;
```

### 두 번째 비교

```c
current->keys[1] <= key
400000 <= 500000
```

참이므로:

```c
idx = 2;
```

### 세 번째 비교

```c
current->keys[2] <= key
700000 <= 500000
```

거짓이므로 멈춥니다.

그러면 최종적으로:

```c
current = current->children[2];
```

즉 `500000`은:
- `200000`보다 크고
- `400000`보다 크고
- `700000`보다 작으므로

`children[2]` 쪽으로 내려가는 것입니다.

---

## 5. 한 번 더 내려가기

이번에는 그 아래 노드가 이렇게 생겼다고 해봅시다.

```text
current->keys = [450000, 480000, 520000, 600000]
key = 500000
```

다시 `idx = 0`부터 시작합니다.

### 첫 번째 비교

```c
450000 <= 500000
```

참 → `idx = 1`

### 두 번째 비교

```c
480000 <= 500000
```

참 → `idx = 2`

### 세 번째 비교

```c
520000 <= 500000
```

거짓 → 멈춤

그래서:

```c
current = current->children[2];
```

이렇게 다시 더 깊은 자식으로 내려갑니다.

즉 `key = 500000`은 변하지 않고,  
각 노드에서 `idx`만 계산되어 다음 자식 포인터를 선택합니다.

---

## 6. 리프에 도착하면 `i`가 움직인다

리프에 도착한 뒤에는 `bptree_search()` 안의 이 부분이 실행됩니다.

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

여기서:
- `i`는 리프 안에서 몇 번째 key를 보고 있는지 나타냅니다.
- `idx`는 더 이상 안 쓰고, 이제 `i`가 실제 위치를 찾습니다.

---

## 7. 직접 대입해 보기: 리프에서 찾기

예를 들어 최종적으로 도착한 리프가 이렇게 생겼다고 해봅시다.

```text
leaf->keys = [498000, 499900, 500000, 500120]
leaf->ptrs = [&playerA, &playerB, &playerC, &playerD]
leaf->num_keys = 4
key = 500000
```

그럼 루프는 이렇게 돕니다.

### i = 0

```c
leaf->keys[0] == 500000   -> 498000 == 500000   -> 거짓
leaf->keys[0] > 500000    -> 498000 > 500000    -> 거짓
```

계속 진행

### i = 1

```c
leaf->keys[1] == 500000   -> 499900 == 500000   -> 거짓
leaf->keys[1] > 500000    -> 499900 > 500000    -> 거짓
```

계속 진행

### i = 2

```c
leaf->keys[2] == 500000   -> 참
```

그러면 바로:

```c
return leaf->ptrs[2];
```

즉 최종적으로 `&playerC`가 반환됩니다.

---

## 8. 못 찾는 경우는 어떻게 끝나는가

예를 들어 리프가 이렇게 생겼다고 해봅시다.

```text
leaf->keys = [498000, 499900, 500120, 500300]
key = 500000
```

그럼:

### i = 0
- `498000 == 500000` 거짓
- `498000 > 500000` 거짓

### i = 1
- `499900 == 500000` 거짓
- `499900 > 500000` 거짓

### i = 2
- `500120 == 500000` 거짓
- `500120 > 500000` 참

이 순간:

```c
return NULL;
```

을 합니다.

즉 현재 key가 찾는 값보다 커졌다는 건  
리프가 정렬되어 있으므로 뒤에는 더 볼 필요가 없다는 뜻입니다.

---

## 9. 변수 역할만 한 번에 정리

현재 탐색 과정에서 핵심 변수는 이렇게 보면 됩니다.

- `key`
  - 찾고 싶은 ID 값
  - 예: `500000`

- `idx`
  - 내부 노드에서 어느 자식으로 내려갈지 고르는 번호
  - 예: `children[2]`

- `i`
  - 리프 안에서 몇 번째 key를 보고 있는지 나타내는 번호
  - 예: `leaf->keys[2]`

- `leaf`
  - 최종적으로 도착한 리프 노드

---

## 10. 한 문장 요약

현재 `bptree_search(tree, key)`에서  
`key`에는 찾고 싶은 ID가 들어가고, 내부 노드에서는 `idx`가 움직이며 어느 자식으로 내려갈지 결정하고, 리프에 도착한 뒤에는 `i`가 움직이며 실제 key 위치를 찾아 해당 포인터를 반환합니다.
