# 현재 폴더 기준 ID 조회 코드 흐름

이 문서는 **지금 현재 폴더 상태**를 기준으로,  
사용자가 웹에서 `ID`를 입력했을 때 그 값이 어떻게 읽히고, 어떤 코드 흐름을 타고, 최종 결과가 다시 웹으로 돌아오는지 정리한 문서입니다.

핵심 흐름은 아래 한 줄로 요약할 수 있습니다.

`브라우저 입력 -> /api/search 요청 -> Perl 서버 -> query_server -> B+ Tree 탐색 -> JSON 응답 -> 화면 반영`

---

## 1. 브라우저에서 ID 입력을 읽는 부분

파일:
- [C:\Users\user\Desktop\정글\수요코딩회\7주차\week7_webcoding\web\js\app.js](C:\Users\user\Desktop\정글\수요코딩회\7주차\week7_webcoding\web\js\app.js)

사용자가 입력한 ID는 `targetIdInput`에서 읽습니다.

```js
const targetIdInput = document.getElementById("target-id");
```

실제로 값을 꺼내는 함수는 `getTargetId()`입니다.

```js
function getTargetId() {
  const value = Number(targetIdInput.value);
  const count = Number(datasetSize.value);
  return Number.isInteger(value) && value > 0 ? value : Math.floor(count / 2);
}
```

의미:
- 사용자가 숫자를 입력하면 그 값을 사용
- 입력이 없으면 현재 데이터셋 중간값을 기본 ID로 사용

---

## 2. 검색 버튼을 눌렀을 때 실행되는 함수

같은 파일에서 버튼 클릭은 `runCurrentMode()`로 연결됩니다.

```js
runButton.addEventListener("click", runCurrentMode);
```

그리고 `single` 모드일 때 실제 검색 요청을 보냅니다.

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

여기서 중요한 줄은 이 부분입니다.

```js
fetchJson(buildUrl(`api/search?id=${targetId}`), "검색 실패");
```

즉 브라우저는 직접 C를 실행하지 않고,
서버에 `api/search?id=500000` 같은 요청을 보냅니다.

---

## 3. `/api/search` 요청을 서버가 받는 부분

파일:
- [C:\Users\user\Desktop\정글\수요코딩회\7주차\week7_webcoding\web\server\server.pl](C:\Users\user\Desktop\정글\수요코딩회\7주차\week7_webcoding\web\server\server.pl)

서버 라우팅 부분:

```perl
} elsif ($path eq '/api/search') {
    handle_search($client, $query);
}
```

즉 `/api/search`는 `handle_search()`로 들어갑니다.

---

## 4. 서버가 query string에서 ID를 읽는 부분

`handle_search()` 안에서 먼저 `id` 값을 꺼냅니다.

```perl
my %params = parse_query_string($query);
my $target_id = $params{id};
```

그리고 유효성 검사를 합니다.

```perl
if (!defined $target_id || $target_id !~ /^\d+$/ || $target_id <= 0) {
    return send_json($client, '400 Bad Request', {
        ok => JSON::PP::false,
        message => 'id must be a positive integer'
    });
}
```

의미:
- `id`가 없거나
- 숫자가 아니거나
- 0 이하이면
- 바로 에러 JSON 반환

---

## 5. 서버가 현재 CSV가 준비되어 있는지 확인하는 부분

```perl
if (!-f $current_csv_path) {
    return send_json($client, '400 Bad Request', {
        ok => JSON::PP::false,
        message => 'csv data has not been generated yet'
    });
}
```

이 프로젝트는 지금
- CSV를 원본 데이터로 두고
- 그 CSV를 읽어 메모리에 적재한 뒤
- 인덱스를 만들고 탐색하는 구조입니다.

그래서 검색 전에 현재 활성 CSV가 꼭 있어야 합니다.

---

## 6. 서버가 검색 엔진을 재사용하는 부분

지금 구조에서 중요한 최적화 포인트는 `ensure_query_engine()`입니다.

```perl
my ($ok, $engine_error) = ensure_query_engine($current_csv_path);
if (!$ok) {
    return send_json($client, '500 Internal Server Error', {
        ok => JSON::PP::false,
        message => $engine_error
    });
}
```

이 함수의 의미:
- 현재 CSV에 맞는 검색 엔진이 이미 떠 있으면 그대로 재사용
- 없으면 새로 시작

즉 예전처럼 검색할 때마다:
- CSV 다시 읽기
- B-Tree 다시 만들기
- B+ Tree 다시 만들기

를 반복하지 않게 하려는 구조입니다.

---

## 7. 서버가 검색 엔진 프로세스에 명령을 보내는 부분

검색 엔진이 준비되면 Perl 서버는 stdin/stdout으로 명령을 주고받습니다.

```perl
print {$engine_in} "search $target_id\n";
$output = <$engine_out>;
```

의미:
- 서버가 검색 엔진에 `"search 500000"` 같은 명령을 보냄
- 검색 엔진이 JSON 한 줄을 반환

즉 구조는:

`브라우저 -> Perl 서버 -> query_server 프로세스`

입니다.

---

## 8. 검색 엔진이 처음 시작될 때 하는 일

파일:
- [C:\Users\user\Desktop\정글\수요코딩회\7주차\week7_webcoding\src\demo\query_server.c](C:\Users\user\Desktop\정글\수요코딩회\7주차\week7_webcoding\src\demo\query_server.c)

검색 엔진은 시작될 때 CSV를 한 번 읽고 인덱스를 만듭니다.

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
- CSV를 `Player 배열`로 메모리에 적재
- 각 Player의 `id`를 key로 사용해서
  - `B-Tree`
  - `B+ Tree`
  를 생성

즉 검색 전에 메모리 안에는 이미:
- `players[]`
- `btree`
- `bptree`

가 준비되어 있습니다.

---

## 9. 검색 엔진이 `search <id>` 명령을 처리하는 부분

`query_server.c`의 명령 루프:

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

즉 Perl 서버가 `"search 500000"`을 보내면:
- `target_id = 500000`
- `print_search_result(...)` 호출

로 이어집니다.

---

## 10. 실제로 ID를 찾는 핵심 코드

`print_search_result()` 안에서 진짜 탐색이 일어납니다.

```c
linear_found = linear_search(players, count, target_id);
btree_found = (Player *)btree_search(btree, target_id);
bptree_found = (Player *)bptree_search(bptree, target_id);
```

즉 같은 `ID`를 세 방식으로 찾습니다.

- `linear_search`
  - 배열 처음부터 순차 검색
- `btree_search`
  - B-Tree 인덱스 탐색
- `bptree_search`
  - B+ Tree 인덱스 탐색

그리고 각각의 시간도 측정합니다.

```c
start = now_ns();
linear_found = linear_search(players, count, target_id);
linear_us = (double)(now_ns() - start) / 1000.0;
```

이런 방식으로 B-Tree, B+ Tree도 각각 시간 측정 후 JSON으로 묶습니다.

---

## 11. B+ Tree에서 실제로 리프까지 내려가는 부분

파일:
- [C:\Users\user\Desktop\정글\수요코딩회\7주차\week7_webcoding\src\bplus_tree\bplus_tree.c](C:\Users\user\Desktop\정글\수요코딩회\7주차\week7_webcoding\src\bplus_tree\bplus_tree.c)

핵심 함수는 `bptree_search()`입니다.

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

1. `find_leaf_node()`로 루트부터 리프까지 내려감
2. 리프 노드 내부에서 key를 찾음
3. 찾으면 해당 위치의 `record_ptr` 반환

즉 B+ Tree는 최종적으로 `id -> Player*` 매핑을 돌려주는 구조입니다.

---

## 12. 내부 노드에서 어느 방향으로 내려가는가

`find_leaf_node()` 안의 핵심:

```c
while (current && !current->is_leaf) {
    int idx = upper_bound_keys(current->keys, current->num_keys, key);
    current = current->children[idx];
}
```

이 부분은:
- 현재 노드의 key 배열을 보고
- 내가 찾는 `id`가 어느 구간에 속하는지 계산
- 그 자식 노드로 이동

하는 과정입니다.

쉽게 말하면:
- 루트: 큰 구역
- 내부 노드: 더 작은 구역
- 리프: 실제 데이터 위치

를 의미합니다.

---

## 13. 최종적으로 검색 엔진이 반환하는 JSON

`query_server.c`는 결과를 JSON으로 출력합니다.

형태는 대략 이렇습니다.

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

서버는 이 JSON을 그대로 다시 브라우저에 전달합니다.

---

## 14. 브라우저가 결과를 화면에 반영하는 부분

다시 [web/js/app.js](C:\Users\user\Desktop\정글\수요코딩회\7주차\week7_webcoding\web\js\app.js) 로 돌아오면,
검색 결과는 `renderSingle()`에서 반영합니다.

```js
resultEls.linearTime.textContent = formatTimeUs(payload.timings.linear_us);
resultEls.btreeTime.textContent = formatTimeUs(payload.timings.btree_us);
resultEls.bptreeTime.textContent = formatTimeUs(payload.timings.bptree_us);
```

그리고 실제 플레이어 데이터가 있으면 테이블도 갱신합니다.

```js
renderRows([{
  ...payload.player,
  width: 120,
}]);
```

즉 최종적으로 사용자가 보는 것은:
- 탐색 시간
- 어떤 플레이어가 나왔는지
- 테이블 갱신

입니다.

---

## 15. 전체 흐름 다시 한 번 요약

1. 사용자가 브라우저에서 ID 입력
2. `app.js`가 `/api/search?id=...` 호출
3. `server.pl`이 현재 CSV와 검색 엔진 준비
4. `query_server`가 이미 메모리에 올려둔 데이터와 인덱스 사용
5. `bptree_search()`가 리프까지 내려가 `Player*` 반환
6. JSON 생성
7. Perl 서버가 브라우저에 전달
8. 브라우저가 결과 카드와 테이블 갱신

---

## 16. 코드 읽는 추천 순서

직접 흐름을 따라가고 싶으면 이 순서가 가장 편합니다.

1. [web/js/app.js](C:\Users\user\Desktop\정글\수요코딩회\7주차\week7_webcoding\web\js\app.js)
2. [web/server/server.pl](C:\Users\user\Desktop\정글\수요코딩회\7주차\week7_webcoding\web\server\server.pl)
3. [src/demo/query_server.c](C:\Users\user\Desktop\정글\수요코딩회\7주차\week7_webcoding\src\demo\query_server.c)
4. [src/bplus_tree/bplus_tree.c](C:\Users\user\Desktop\정글\수요코딩회\7주차\week7_webcoding\src\bplus_tree\bplus_tree.c)
5. [src/btree/btree.c](C:\Users\user\Desktop\정글\수요코딩회\7주차\week7_webcoding\src\btree\btree.c)
6. [src/linear/linear_search.c](C:\Users\user\Desktop\정글\수요코딩회\7주차\week7_webcoding\src\linear\linear_search.c)

---

## 17. 지금 상태에서 중요한 포인트

- 검색은 이제 `query_server` 재사용 구조라서 같은 데이터셋에서는 훨씬 빨라졌음
- CSV는 원본 데이터 역할
- 실제 탐색은 메모리에 올린 뒤 인덱스를 사용
- ID 검색의 핵심은 결국 `bptree_search()`가 `id`를 key로 `Player*`를 찾는 과정

한 줄 정리:

`지금 프로젝트에서 ID 조회는, 브라우저가 서버에 ID를 보내면 서버가 메모리에 올려둔 B+ Tree에서 그 ID에 대응하는 Player 포인터를 찾아 JSON으로 돌려주는 구조다.`
