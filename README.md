# GameRank DB — B+ 트리 인덱스 구현 프로젝트

> 게임 플레이어 랭킹 DB를 컨셉으로 한 B+ 트리 인덱스 구현 및 성능 벤치마크

---

## 프로젝트 개요

100만 명 이상의 플레이어 데이터를 대상으로 **선형 탐색**, **B 트리**, **B+ 트리** 세 가지 탐색 방식의 성능을 비교한다.

| 탐색 방식 | 시간 복잡도 | 특징 |
|---|---|---|
| 선형 탐색 | O(n) | 인덱스 없음, 처음부터 순차 탐색 |
| B 트리 | O(log n) | 내부 노드에도 데이터 저장 |
| B+ 트리 | O(log n) | 리프 노드만 데이터 저장, 범위 탐색 최적 |

---

## 폴더 구조

```
gamerank-db/
├── src/
│   ├── common/          # 공통 헤더 (player.h 등)
│   ├── linear/          # 선형 탐색 구현
│   ├── btree/           # B 트리 구현
│   ├── bplus_tree/      # B+ 트리 구현
│   └── benchmark/       # 성능 측정 코드
├── web/                 # 벤치마크 시각화 데모
│   ├── index.html       # 메인 페이지
│   ├── css/             # 스타일시트
│   │   └── styles.css
│   ├── js/              # 클라이언트 스크립트
│   │   └── app.js
│   ├── assets/          # 결과 데이터 (results.json)
│   └── server/          # 로컬 웹 서버
│       └── server.pl
├── tests/               # 트리 단위 테스트
├── docs/                # 개념 정리 문서
├── Makefile
└── README.md
```

---

## 빌드 및 실행

```bash
# 전체 빌드
make all

# 데이터 생성 (100만 건)
make datagen

# 벤치마크 실행
make bench

# CLI 엣지 케이스 실행
make edgecheck

# 결과 출력
make result
```

개별 케이스만 보고 싶으면 아래처럼 실행할 수 있습니다.

```bash
./bin/edge_cases list
./bin/edge_cases all
./bin/edge_cases missing-id
./bin/edge_cases shell-bad-csv
```

---

## 벤치마크 방식

- 레코드 수: 10만 / 50만 / 100만 / 500만
- 탐색 유형: 단일 탐색(`WHERE id = ?`), 이름 브리지 비교(`WHERE name = ?`를 선형 탐색으로 찾은 뒤 해당 `id`로 트리 조회), 범위 탐색(`WHERE id BETWEEN ? AND ?`)
- 측정 단위: 마이크로초(μs)
- 측정 함수: `gettimeofday()`

`name` 자체를 트리에서 직접 찾는 벤치마크는 아닙니다. 현재 인덱스는 `id` 기준만 있으므로, `name` 비교는 선형 탐색으로 대상을 찾고 그 결과의 `id`를 B 트리/B+ 트리에서 다시 조회하는 방식으로 비교합니다.

---

## 팀원

| 역할 | 담당 |
|---|---|
| 구현 조 | B+ 트리 핵심 로직, 개념 정리 |
| 개발 조 | SQL 처리기 연동, 벤치마크 코드 |

---

## 참고

- [B+ 트리 개념 정리](docs/bplus_tree_개념정리.md)
- [기획 및 설계 문서](docs/planning.md)
