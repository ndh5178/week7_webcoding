# ID 값으로 데이터를 찾는 흐름

이 문서는 현재 프로젝트에서 `ID 하나를 입력해서 값을 찾는 흐름`을 코드 기준으로 따라가기 위한 정리입니다.

읽는 순서는 아래처럼 보면 됩니다.

1. 프론트에서 `ID`를 읽는다.
2. 프론트가 `/api/search?id=...` 요청을 보낸다.
3. Perl 서버가 현재 데이터셋에 맞는 검색 엔진을 준비한다.
4. 검색 엔진이 메모리에 올려둔 `B-Tree`, `B+ Tree`, `배열`을 사용해 탐색한다.
5. 결과 JSON이 다시 프론트로 돌아온다.

---

## 1. 프론트에서 ID를 읽는 부분

파일:
- [web/js/app.js](C:\Users\user\Desktop\정글\수요코딩회\7주차\week7_webcoding\web\js\app.js)

사용자가 입력한 ID는 `getTargetId()`에서 읽습니다.

```js
function getTargetId() {
  const value = Number(targetIdInput.value);
  const count = Number(datasetSize.value);
  return Number.isInteger(value) && value > 0 ? value : Math.floor(count / 2);
}
```

의미:
- 입력한 값이 있으면 그 값을 사용
- 없으면 현재 데이터셋의 중간값을 기본 ID로 사용

---

## 2. 프론트가 검색 API를 호출하는 부분

같은 파일의 `runCurrentMode()` 안에서 `single` 모드일 때 검색 요청을 보냅니다.

```js
if (currentMode === "single") {
  const targetId = getTargetId();
  runButton.textContent = "탐색 실행 중...";
  const payload = await fetchJson(buildUrl(`api/search?id=${targetId}`), "검색 실패");
  if (!payload.ok) {
    throw new Error(payload.message || "검색 실패");
  }
  renderSingle(payload, benchmark);
}
```

여기서 핵심은 이 줄입니다.

```js
fetchJson(buildUrl(`api/search?id=${targetId}`), "검색 실패")
```

즉 브라우저는 직접 C 코드를 실행하지 않고, 서버에 `id`를 전달합니다.

---

## 3. 서버가 `/api/search` 요청을 받는 부분

파일:
- [web/server/server.pl](C:\Users\user\Desktop\정글\수요코딩회\7주차\week7_webcoding\web\server\server.pl)

맨 아래 라우팅 부분을 보면:

```perl
} elsif ($path eq '/api/search') {
    handle_search($client, $query);
}
```

즉 `/api/search` 요청은 `handle_search()`로 들어갑니다.

---

## 4. 서버가 검색 엔진을 준비하는 부분

`handle_search()` 안의 핵심 흐름:

```perl
if (!-f $current_csv_path) {
    return send_json($client, '400 Bad Request', {
        ok => JSON::PP::false,
        message => 'csv data has not been generated yet'
    });
}

my ($ok, $engine_error) = ensure_query_engine($current_csv_path);
if (!$ok) {
    return send_json($client, '500 Internal Server Error', {
        ok => JSON::PP::false,
        message => $engine_error
    });
}
```

의미:
- 현재 선택된 CSV 파일이 있는지 확인
- 그 CSV에 맞는 검색 엔진이 이미 떠 있으면 재사용
- 없으면 새로 띄움

여기서 중요한 포인트:
- 예전에는 검색할 때마다 CSV를 다시 읽고 트리를 다시 만들었음
- 지금은 검색 엔진 프로세스를 유지해서 같은 데이터셋에서는 재사용함

---

## 5. 서버가 검색 엔진에 명령을 보내는 부분

같은 `handle_search()` 안에서 실제 검색 명령은 이렇게 보냅니다.

```perl
print {$engine_in} "search $target_id\n";
$output = <$engine_out>;
```

의미:
- Perl 서버가 검색 엔진 프로세스의 표준입력(stdin)으로 `search 500000` 같은 명령을 보냄
- 검색 엔진이 표준출력(stdout)으로 JSON 한 줄을 반환

즉 구조적으로는:

`브라우저 -> Perl 서버 -> query_server 프로세스`

입니다.

---

## 6. 검색 엔진이 시작될 때 하는 일

파일:
- [src/demo/query_server.c](C:\Users\user\Desktop\정글\수요코딩회\7주차\week7_webcoding\src\demo\query_server.c)

검색 엔진은 시작하자마자 CSV를 읽고, 인덱스를 메모리에 만듭니다.

```c
players = load_players_csv(csv_path, &count);
btree = btree_create();
bptree = bptree_create();

for (i = 0; i < count; ++i) {
    btree_insert(btree, players[i].id, &players[i]);
    bptree_insert(bptree, players[i].id, &players[i]);
}
```

이 부분의 의미:
- CSV 파일을 배열(`players`)로 읽음
- `id -> Player*` 형태로 B-Tree와 B+ Tree를 구성함

즉 검색 전에 이미 메모리 안에는:
- `Player 배열`
- `B-Tree 인덱스`
- `B+ Tree 인덱스`

가 준비되어 있습니다.

---

## 7. 검색 엔진이 `search <id>` 명령을 받는 부분

`query_server.c`의 명령 처리 루프:

```c
while (fgets(line, sizeof(line), stdin)) {
    int target_id = 0;

    if (strncmp(line, "search ", 7) == 0) {
        target_id = atoi(line + 7);
        ...
        print_search_result(players, count, btree, bptree, target_id);
        continue;
    }
}
```

즉 서버가 `"search 500000"`을 보내면:
- `target_id = 500000`
- `print_search_result(...)` 호출

로 이어집니다.

---

## 8. 실제 탐색이 일어나는 부분

가장 핵심은 `print_search_result()`입니다.

```c
linear_found = linear_search(players, count, target_id);
btree_found = (Player *)btree_search(btree, target_id);
bptree_found = (Player *)bptree_search(bptree, target_id);
```

여기서 같은 `target_id`를 3가지 방식으로 찾습니다.

- `linear_search(...)`
  - 배열 처음부터 끝까지 순회
- `btree_search(...)`
  - B-Tree 인덱스로 탐색
- `bptree_search(...)`
  - B+ Tree 인덱스로 탐색

즉 `ID 찾기`의 핵심 비교 실험이 여기서 일어납니다.

---

## 9. B+ 트리에서 실제로 leaf를 찾는 부분

파일:
- [src/bplus_tree/bplus_tree.c](C:\Users\user\Desktop\정글\수요코딩회\7주차\week7_webcoding\src\bplus_tree\bplus_tree.c)

`bptree_search()`는 내부적으로 먼저 `find_leaf_node()`를 호출합니다.

```c
leaf = find_leaf_node(tree->root, key);
if (!leaf) {
    return NULL;
}

idx = lower_bound_keys(leaf->keys, leaf->num_keys, key);
if (idx < leaf->num_keys && leaf->keys[idx] == key) {
    return leaf->ptrs[idx];
}
```

의미:

1. 루트부터 시작
2. 내부 노드에서 `key`가 어느 자식으로 내려가야 하는지 결정
3. 리프 노드에 도착
4. 리프 안에서 정확히 같은 `key`를 찾음
5. 찾으면 그 위치의 `record_ptr` 반환

즉 B+ 트리는 최종적으로 `id`를 키로 삼아 `Player*`를 돌려줍니다.

---

## 10. 내부 노드에서 어느 방향으로 내려가는가

`find_leaf_node()` 안의 핵심:

```c
while (current && !current->is_leaf) {
    int idx = upper_bound_keys(current->keys, current->num_keys, key);
    current = current->children[idx];
}
```

이 코드는:
- 현재 노드의 key 배열을 보고
- 내가 찾는 `id`가 어느 구간에 속하는지 계산하고
- 그 자식 노드로 내려가는 과정입니다

쉽게 말하면:
- 루트는 `큰 구역`
- 내부 노드는 `더 작은 구역`
- 리프는 `실제 데이터 위치`

를 뜻합니다.

---

## 11. 최종적으로 프론트가 받는 JSON

`query_server.c`는 결과를 JSON으로 출력합니다.

예를 들면 이런 형태입니다.

```json
{
  "ok": true,
  "dataset_size": 1000000,
  "target_id": 500000,
  "found": true,
  "timings": {
    "linear_us": 1790.823,
    "btree_us": 2.501,
    "bptree_us": 1.300
  },
  "player": {
    "id": 500000,
    "name": "StarlightX_9999",
    "score": 4063,
    "tier": "Diamond"
  }
}
```

프론트는 이 JSON을 받아 카드와 테이블을 갱신합니다.

---

## 12. 전체 흐름 한 번에 보기

한 줄 흐름:

`브라우저 입력 -> /api/search -> Perl 서버 -> query_server -> bptree_search -> Player* 반환 -> JSON 응답 -> 화면 갱신`

조금 더 자세히 쓰면:

1. 사용자가 ID를 입력하고 `탐색 실행`
2. `app.js`가 `/api/search?id=...` 호출
3. `server.pl`이 현재 CSV용 검색 엔진 준비
4. 검색 엔진이 이미 메모리에 올린 배열/B-Tree/B+ Tree 사용
5. `bptree_search()`가 리프까지 내려가 `Player*` 반환
6. 결과를 JSON으로 직렬화
7. 브라우저가 결과를 카드와 표에 반영

---

## 13. 이 흐름을 읽을 때 추천 순서

코드를 직접 따라갈 때는 이 순서가 가장 편합니다.

1. [web/js/app.js](C:\Users\user\Desktop\정글\수요코딩회\7주차\week7_webcoding\web\js\app.js)
2. [web/server/server.pl](C:\Users\user\Desktop\정글\수요코딩회\7주차\week7_webcoding\web\server\server.pl)
3. [src/demo/query_server.c](C:\Users\user\Desktop\정글\수요코딩회\7주차\week7_webcoding\src\demo\query_server.c)
4. [src/bplus_tree/bplus_tree.c](C:\Users\user\Desktop\정글\수요코딩회\7주차\week7_webcoding\src\bplus_tree\bplus_tree.c)
5. [src/btree/btree.c](C:\Users\user\Desktop\정글\수요코딩회\7주차\week7_webcoding\src\btree\btree.c)
6. [src/linear/linear_search.c](C:\Users\user\Desktop\정글\수요코딩회\7주차\week7_webcoding\src\linear\linear_search.c)

---

## 14. 지금 볼 때 주의할 점

현재 [web/js/app.js](C:\Users\user\Desktop\정글\수요코딩회\7주차\week7_webcoding\web\js\app.js) 는 일부 한글 문자열 인코딩이 깨져 있는 부분이 있습니다.

하지만 `ID 검색 흐름 자체`를 이해하는 데 중요한 건 아래 함수들이라서, 이 부분만 보면 충분합니다.

- `getTargetId()`
- `fetchJson()`
- `ensureDataset()`
- `runCurrentMode()`

즉 UI 문구가 조금 깨져 보여도 `검색 요청이 어디로 가는지`를 읽는 데는 큰 문제는 없습니다.
