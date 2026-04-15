"""
Riot Games API — KR 랭크 플레이어 수집기
Usage: python collect_players.py --key RGAPI-xxxx [--target 1000000]

출력: players.csv  (id, name, winrate, rank)
  - name    : 소환사 닉네임
  - winrate : 승률 정수 % (예: 65)
  - rank    : 티어+디비전 (예: GOLD_II / CHALLENGER)
"""

import requests
import time
import csv
import argparse
import os
import sys

# ────────────────────────────────────────────────
# 설정
# ────────────────────────────────────────────────
BASE_URL   = "https://kr.api.riotgames.com"
QUEUE      = "RANKED_SOLO_5x5"
OUTPUT     = "players.csv"
CHECKPOINT = "collect_checkpoint.txt"

TIERS      = ["DIAMOND", "EMERALD", "PLATINUM", "GOLD", "SILVER", "BRONZE", "IRON"]
DIVISIONS  = ["IV", "III", "II", "I"]
APEX_TIERS = ["CHALLENGER", "GRANDMASTER", "MASTER"]
TIER_TARGETS = {
    "CHALLENGER": None,
    "GRANDMASTER": None,
    "MASTER": None,
    "DIAMOND": 120_000,
    "EMERALD": 120_000,
    "PLATINUM": 120_000,
    "GOLD": 120_000,
    "SILVER": 120_000,
    "BRONZE": 120_000,
    "IRON": "remainder",
}

REQ_INTERVAL = 1.3  # 초 (100 req/2min 제한 준수)

# ────────────────────────────────────────────────
# 유틸
# ────────────────────────────────────────────────

def calc_winrate(wins, losses):
    total = wins + losses
    if total == 0:
        return 0
    return round(wins * 100 / total)


def get_entries(api_key, tier, division, page):
    url    = f"{BASE_URL}/lol/league/v4/entries/{QUEUE}/{tier}/{division}"
    params = {"page": page, "api_key": api_key}
    while True:
        r = requests.get(url, params=params, timeout=10)
        if r.status_code == 200:
            return r.json()
        elif r.status_code == 429:
            retry = int(r.headers.get("Retry-After", 10))
            print(f"\n  [429] 속도 초과 — {retry}초 대기...")
            time.sleep(retry + 1)
        elif r.status_code in (401, 403):
            print(f"\n  [{r.status_code}] API 키가 만료됐거나 잘못됐습니다. developer.riotgames.com에서 재발급 후 다시 실행하세요.")
            sys.exit(1)
        else:
            print(f"\n  [{r.status_code}] 오류 — 5초 후 재시도")
            time.sleep(5)


def get_apex(api_key, tier_name):
    endpoint_map = {
        "CHALLENGER":  f"/lol/league/v4/challengerleagues/by-queue/{QUEUE}",
        "GRANDMASTER": f"/lol/league/v4/grandmasterleagues/by-queue/{QUEUE}",
        "MASTER":      f"/lol/league/v4/masterleagues/by-queue/{QUEUE}",
    }
    url = BASE_URL + endpoint_map[tier_name]
    r = requests.get(url, params={"api_key": api_key}, timeout=10)
    if r.status_code == 200:
        return r.json().get("entries", [])
    return []


def load_checkpoint():
    if os.path.exists(CHECKPOINT):
        with open(CHECKPOINT) as f:
            line = f.read().strip()
            if line:
                parts = line.split(",")
                if len(parts) >= 4:
                    return parts[0], parts[1], int(parts[2]), int(parts[3])
    return None


def save_checkpoint(tier, division, page, collected):
    with open(CHECKPOINT, "w") as f:
        f.write(f"{tier},{division},{page},{collected}")


def write_row(writer, auto_id, entry, rank_label):
    name    = entry.get("summonerName", "Unknown")
    wins    = entry.get("wins", 0)
    losses  = entry.get("losses", 0)
    winrate = calc_winrate(wins, losses)
    writer.writerow([auto_id, name, winrate, rank_label])


def get_tier_from_rank(rank_label):
    return rank_label.split("_", 1)[0]


def rebuild_tier_counts_from_csv():
    counts = {tier: 0 for tier in TIER_TARGETS}
    if not os.path.exists(OUTPUT):
        return counts

    with open(OUTPUT, newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for row in reader:
            tier = get_tier_from_rank(row["rank"])
            if tier in counts:
                counts[tier] += 1
    return counts


def get_tier_limit(tier, total_target, collected, tier_counts):
    tier_target = TIER_TARGETS[tier]
    if tier_target == "remainder":
        return max(0, total_target - collected)
    if tier_target is None:
        return None
    return max(0, tier_target - tier_counts[tier])


# ────────────────────────────────────────────────
# 메인
# ────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description="Riot KR 랭크 플레이어 수집")
    parser.add_argument("--key",    required=True, help="Riot API 키")
    parser.add_argument("--target", type=int, default=1_000_000, help="목표 수집 수")
    args = parser.parse_args()

    api_key = args.key
    target  = args.target

    collected   = 0
    auto_id     = 1
    start_tier  = 0
    start_div   = 0
    start_page  = 1
    resume_mode = False
    tier_counts = {tier: 0 for tier in TIER_TARGETS}

    cp = load_checkpoint()
    if cp:
        cp_tier, cp_div, cp_page, collected = cp
        auto_id = collected + 1
        if cp_tier in TIERS:
            start_tier = TIERS.index(cp_tier)
            start_div  = DIVISIONS.index(cp_div) if cp_div in DIVISIONS else 0
            start_page = cp_page
        resume_mode = True
        tier_counts = rebuild_tier_counts_from_csv()
        print(f"체크포인트 감지: {cp_tier} {cp_div} {cp_page}p부터 이어받기 ({collected:,}건 기수집)")

    csv_mode = "a" if resume_mode else "w"
    csvfile  = open(OUTPUT, csv_mode, newline="", encoding="utf-8")
    writer   = csv.writer(csvfile)
    if not resume_mode:
        writer.writerow(["id", "name", "winrate", "rank"])

    fixed_sum = sum(v for v in TIER_TARGETS.values() if isinstance(v, int))
    print(f"목표: {target:,}건  |  출력: {OUTPUT}")
    print(f"티어별 할당: Apex=전체, " +
          ", ".join(f"{t}={TIER_TARGETS[t]:,}" for t in TIERS if isinstance(TIER_TARGETS[t], int)) +
          f", IRON=나머지({target - fixed_sum:,}예상)")
    print(f"필드: id / 닉네임 / 승률(%) / 랭크")
    print("=" * 50)

    try:
        # ── Apex 티어 ──
        if not resume_mode:
            for apex in APEX_TIERS:
                print(f"[{apex}] 수집 중...")
                entries = get_apex(api_key, apex)
                for e in entries:
                    if collected >= target:
                        break
                    write_row(writer, auto_id, e, apex)
                    auto_id += 1
                    collected += 1
                    tier_counts[apex] += 1
                time.sleep(REQ_INTERVAL)
                if collected >= target:
                    break
            print(f"  → Apex 수집 완료: {collected:,}건")

        # ── 일반 티어 순회 ──
        for ti, tier in enumerate(TIERS):
            if ti < start_tier:
                continue

            tier_limit = get_tier_limit(tier, target, collected, tier_counts)
            if tier_limit == 0:
                print(f"[{tier}] quota 충족으로 건너뜀 ({tier_counts[tier]:,}건)")
                continue

            for di, div in enumerate(DIVISIONS):
                if ti == start_tier and di < start_div:
                    continue

                page = start_page if (ti == start_tier and di == start_div) else 1
                start_page = 1

                while collected < target:
                    tier_limit = get_tier_limit(tier, target, collected, tier_counts)
                    if tier_limit == 0:
                        break

                    entries = get_entries(api_key, tier, div, page)
                    if not entries:
                        break

                    rank_label = f"{tier}_{div}"
                    for e in entries:
                        if collected >= target:
                            break
                        tier_limit = get_tier_limit(tier, target, collected, tier_counts)
                        if tier_limit == 0:
                            break

                        write_row(writer, auto_id, e, rank_label)
                        auto_id += 1
                        collected += 1
                        tier_counts[tier] += 1

                    csvfile.flush()
                    save_checkpoint(tier, div, page, collected)

                    pct = collected / target * 100
                    if TIER_TARGETS[tier] == "remainder":
                        quota_text = f"remainder={tier_counts[tier]:,}"
                    else:
                        quota_text = f"{tier_counts[tier]:,}/{TIER_TARGETS[tier]:,}"

                    print(
                        f"\r  [{tier} {div}] p={page}  total {collected:>9,}/{target:,}  ({pct:.1f}%)  tier {quota_text}",
                        end="",
                        flush=True,
                    )

                    page += 1
                    time.sleep(REQ_INTERVAL)

                print()
                if collected >= target:
                    break

            if collected >= target:
                break

    except KeyboardInterrupt:
        print("\n\n중단됨. 체크포인트 저장됨 — 다음 실행 시 이어받기됩니다.")

    finally:
        csvfile.close()

    print("=" * 50)
    print(f"수집 완료: {collected:,}건 → {OUTPUT}")

    if collected >= target and os.path.exists(CHECKPOINT):
        os.remove(CHECKPOINT)


if __name__ == "__main__":
    main()
