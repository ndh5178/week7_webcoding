"""
Riot tier sample name backfill script.

입력: players.csv (name 컬럼에 puuid 저장)
출력: players_named_sample.csv
- 출력 파일이 없으면 새로 생성
- 출력 파일이 이미 있으면 기존 결과를 이어받아, 아직 puuid인 행만 추가 변환
- CHALLENGER/GRANDMASTER/MASTER/DIAMOND는 최대 500건, EMERALD 이하 티어는 최대 100건만 Riot ID로 변환
- 행이 변환될 때마다 바로 파일에 반영

Usage:
  python fill_top_tier_names.py --key RGAPI-xxxx
  python fill_top_tier_names.py --key RGAPI-xxxx --input players.csv --output players_named_sample.csv
"""

import argparse
import csv
import os
import sys
import time

import requests

ACCOUNT_URL = "https://asia.api.riotgames.com"
TIER_LIMITS = {
    "CHALLENGER": 500,
    "GRANDMASTER": 500,
    "MASTER": 500,
    "DIAMOND": 500,
    "EMERALD": 100,
    "PLATINUM": 100,
    "GOLD": 100,
    "SILVER": 100,
    "BRONZE": 100,
    "IRON": 100,
}
TIERS = [
    "CHALLENGER",
    "GRANDMASTER",
    "MASTER",
    "DIAMOND",
    "EMERALD",
    "PLATINUM",
    "GOLD",
    "SILVER",
    "BRONZE",
    "IRON",
]


def riot_get(url, params, retry_label):
    while True:
        r = requests.get(url, params=params, timeout=10)
        if r.status_code == 200:
            return r
        if r.status_code == 429:
            retry = int(r.headers.get("Retry-After", 10))
            print(f"[429] {retry_label} 속도 초과 — {retry}초 대기...")
            time.sleep(retry + 1)
            continue
        if r.status_code in (401, 403):
            print(f"[{r.status_code}] API 키가 만료됐거나 잘못됐습니다.")
            sys.exit(1)
        if r.status_code == 404:
            print(f"[404] {retry_label} 없음 — 원래 puuid 유지")
            return r

        print(f"[{r.status_code}] {retry_label} 오류 — 원래 puuid 유지")
        return r


def get_riot_id(api_key, puuid, cache):
    if puuid in cache:
        return cache[puuid]

    url = f"{ACCOUNT_URL}/riot/account/v1/accounts/by-puuid/{puuid}"
    r = riot_get(url, {"api_key": api_key}, "Riot ID 조회")
    if r.status_code != 200:
        cache[puuid] = puuid
        return puuid

    data = r.json()
    game_name = data.get("gameName")
    tag_line = data.get("tagLine")

    if game_name and tag_line:
        riot_id = f"{game_name}#{tag_line}"
    else:
        riot_id = puuid

    cache[puuid] = riot_id
    return riot_id


def is_converted(value):
    return bool(value) and value != "Unknown" and "#" in value


def load_input_rows(input_path):
    with open(input_path, newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        fieldnames = reader.fieldnames or ["id", "name", "winrate", "rank"]
        rows = [row for row in reader]
    return fieldnames, rows


def load_existing_output(output_path):
    existing_rows = {}
    tier_counts = {tier: 0 for tier in TIERS}

    if not os.path.exists(output_path):
        return existing_rows, tier_counts

    with open(output_path, newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for row in reader:
            existing_rows[row["id"]] = row
            tier = row["rank"].split("_", 1)[0]
            if tier in tier_counts and is_converted(row["name"]):
                tier_counts[tier] += 1

    return existing_rows, tier_counts


def write_all_rows(output_path, fieldnames, rows):
    with open(output_path, "w", newline="", encoding="utf-8") as outfile:
        writer = csv.DictWriter(outfile, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)
        outfile.flush()


def main():
    parser = argparse.ArgumentParser(description="티어별 샘플 Riot ID 후처리")
    parser.add_argument("--key", required=True, help="Riot API 키")
    parser.add_argument("--input", default="players.csv", help="입력 CSV")
    parser.add_argument("--output", default="players_named_sample.csv", help="출력 CSV")
    args = parser.parse_args()

    cache = {}
    converted_now = 0

    fieldnames, input_rows = load_input_rows(args.input)
    existing_rows, tier_counts = load_existing_output(args.output)

    merged_rows = []
    for row in input_rows:
        current = existing_rows.get(row["id"], row.copy())
        merged_rows.append(current)

    if not os.path.exists(args.output):
        write_all_rows(args.output, fieldnames, merged_rows)

    total_rows = len(merged_rows)

    try:
        for index, row in enumerate(merged_rows):
            tier = row["rank"].split("_", 1)[0]
            if tier not in tier_counts:
                continue
            if tier_counts[tier] >= TIER_LIMITS[tier]:
                continue
            if is_converted(row["name"]):
                continue

            original_puuid = row["name"]
            new_name = get_riot_id(args.key, original_puuid, cache)
            if new_name != row["name"]:
                row["name"] = new_name
                if is_converted(new_name):
                    tier_counts[tier] += 1
                    converted_now += 1
                    write_all_rows(args.output, fieldnames, merged_rows)
                    print(f"[SAVE] {tier} 추가 변환 {converted_now:,}건 / 현재 {tier_counts[tier]:,}/{TIER_LIMITS[tier]:,}건")

            if (index + 1) % 10000 == 0:
                print(f"[PROGRESS] {index + 1:,}/{total_rows:,}행 확인")
    except KeyboardInterrupt:
        write_all_rows(args.output, fieldnames, merged_rows)
        print("\n[INTERRUPT] 현재까지의 변환 결과를 저장했습니다.")
        raise SystemExit(130)

    write_all_rows(args.output, fieldnames, merged_rows)

    print(f"완료: {args.output}")
    print(f"전체 행: {total_rows:,}")
    print(f"이번 실행에서 추가 변환: {converted_now:,}")
    for tier in TIERS:
        print(f"- {tier}: {tier_counts[tier]:,}/{TIER_LIMITS[tier]:,}건")


if __name__ == "__main__":
    main()
