"""
Riot Games API — KR 랭크 플레이어 수집기
Usage: python collect_players.py --key RGAPI-xxxx [--target 1000000]

출력: players.csv  (id, name, winrate, rank)
  - name    : Riot ID (예: Faker#KR1)
  - winrate : 승률 정수 % (예: 65)
  - rank    : 티어+디비전 (예: GOLD_II / CHALLENGER)
"""

import argparse
import csv
import os
import sys
import time

import requests

# ────────────────────────────────────────────────
# 설정
# ────────────────────────────────────────────────
BASE_URL = "https://kr.api.riotgames.com"
QUEUE = "RANKED_SOLO_5x5"
OUTPUT = "players.csv"
CHECKPOINT = "collect_checkpoint.txt"

TIERS = ["DIAMOND", "EMERALD", "PLATINUM", "GOLD", "SILVER", "BRONZE", "IRON"]
DIVISIONS = ["IV", "III", "II", "I"]
APEX_TIERS = ["CHALLENGER", "GRANDMASTER", "MASTER"]
FILL_TIERS = ["IRON", "BRONZE", "SILVER", "GOLD", "PLATINUM", "EMERALD", "DIAMOND"]
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


def riot_get(url, params, retry_label):
    while True:
        r = requests.get(url, params=params, timeout=10)
        if r.status_code == 200:
            return r
        if r.status_code == 429:
            retry = int(r.headers.get("Retry-After", 10))
            print(f"\n  [429] {retry_label} 속도 초과 — {retry}초 대기...")
            time.sleep(retry + 1)
            continue
        if r.status_code in (401, 403):
            print(f"\n  [{r.status_code}] API 키가 만료됐거나 잘못됐습니다. developer.riotgames.com에서 재발급 후 다시 실행하세요.")
            sys.exit(1)

        print(f"\n  [{r.status_code}] {retry_label} 오류 — 5초 후 재시도")
        time.sleep(5)


def get_entries(api_key, tier, division, page):
    url = f"{BASE_URL}/lol/league/v4/entries/{QUEUE}/{tier}/{division}"
    params = {"page": page, "api_key": api_key}
    r = riot_get(url, params, f"{tier} {division} 페이지 수집")
    return r.json()


def get_apex(api_key, tier_name):
    endpoint_map = {
        "CHALLENGER": f"/lol/league/v4/challengerleagues/by-queue/{QUEUE}",
        "GRANDMASTER": f"/lol/league/v4/grandmasterleagues/by-queue/{QUEUE}",
        "MASTER": f"/lol/league/v4/masterleagues/by-queue/{QUEUE}",
    }
    url = BASE_URL + endpoint_map[tier_name]
    r = riot_get(url, {"api_key": api_key}, f"{tier_name} 수집")
    return r.json().get("entries", [])


def load_checkpoint():
    if not os.path.exists(CHECKPOINT):
        return None

    with open(CHECKPOINT, encoding="utf-8") as f:
        line = f.read().strip()

    if not line:
        return None

    parts = line.split(",")
    if len(parts) < 4:
        return None

    return parts[0], parts[1], int(parts[2]), int(parts[3])


def save_checkpoint(tier, division, page, collected):
    with open(CHECKPOINT, "w", encoding="utf-8") as f:
        f.write(f"{tier},{division},{page},{collected}")


def write_row(writer, auto_id, entry, rank_label):
    name = entry.get("puuid", "Unknown")
    wins = entry.get("wins", 0)
    losses = entry.get("losses", 0)
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


def make_page_state(start_tier, start_div, start_page):
    state = {}
    for tier in TIERS:
        for div in DIVISIONS:
            state[(tier, div)] = 1
    if 0 <= start_tier < len(TIERS) and 0 <= start_div < len(DIVISIONS):
        state[(TIERS[start_tier], DIVISIONS[start_div])] = start_page
    return state


def collect_apex(writer, csvfile, api_key, auto_id, collected, target, tier_counts):
    for apex in APEX_TIERS:
        if tier_counts[apex] > 0:
            print(f"[{apex}] 기존 수집분 유지 ({tier_counts[apex]:,}건)")
            continue

        print(f"[{apex}] 수집 중...")
        entries = get_apex(api_key, apex)
        for entry in entries:
            if collected >= target:
                break
            write_row(writer, auto_id, entry, apex)
            auto_id += 1
            collected += 1
            tier_counts[apex] += 1

        csvfile.flush()
        time.sleep(REQ_INTERVAL)
        if collected >= target:
            break

    print(f"  → Apex 수집 완료: {collected:,}건")
    return auto_id, collected


def collect_tier_page(writer, csvfile, api_key, tier, div, page, auto_id, collected, target, tier_counts):
    entries = get_entries(api_key, tier, div, page)
    if not entries:
        return auto_id, collected, False

    rank_label = f"{tier}_{div}"
    for entry in entries:
        if collected >= target:
            break
        write_row(writer, auto_id, entry, rank_label)
        auto_id += 1
        collected += 1
        tier_counts[tier] += 1

    csvfile.flush()
    return auto_id, collected, True


# ────────────────────────────────────────────────
# 메인
# ────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description="Riot KR 랭크 플레이어 수집")
    parser.add_argument("--key", required=True, help="Riot API 키")
    parser.add_argument("--target", type=int, default=1_000_000, help="목표 수집 수")
    args = parser.parse_args()

    api_key = args.key
    target = args.target

    collected = 0
    auto_id = 1
    start_tier = 0
    start_div = 0
    start_page = 1
    resume_mode = False
    tier_counts = {tier: 0 for tier in TIER_TARGETS}
    exhausted = set()
    page_state = make_page_state(start_tier, start_div, start_page)

    cp = load_checkpoint()
    if cp:
        cp_tier, cp_div, cp_page, collected = cp
        auto_id = collected + 1
        if cp_tier in TIERS:
            start_tier = TIERS.index(cp_tier)
            start_div = DIVISIONS.index(cp_div) if cp_div in DIVISIONS else 0
            start_page = cp_page
            page_state[(cp_tier, cp_div)] = cp_page
        resume_mode = True
        tier_counts = rebuild_tier_counts_from_csv()
        print(f"체크포인트 감지: {cp_tier} {cp_div} {cp_page}p부터 이어받기 ({collected:,}건 기수집)")

    csv_mode = "a" if resume_mode else "w"
    fixed_sum = sum(v for v in TIER_TARGETS.values() if isinstance(v, int))

    csvfile = open(OUTPUT, csv_mode, newline="", encoding="utf-8")
    writer = csv.writer(csvfile)

    if not resume_mode:
        writer.writerow(["id", "name", "winrate", "rank"])

    print(f"목표: {target:,}건  |  출력: {OUTPUT}")
    print(
        "티어별 할당: Apex=전체, "
        + ", ".join(f"{tier}={TIER_TARGETS[tier]:,}" for tier in TIERS if isinstance(TIER_TARGETS[tier], int))
        + f", IRON=나머지({target - fixed_sum:,}예상)"
    )
    print("필드: id / Riot ID / 승률(%) / 랭크")
    print("=" * 50)

    try:
        auto_id, collected = collect_apex(
            writer, csvfile, api_key, auto_id, collected, target, tier_counts
        )

        # ── quota 기반 1차 수집 ──
        for ti, tier in enumerate(TIERS):
            if resume_mode and ti < start_tier:
                continue

            tier_limit = get_tier_limit(tier, target, collected, tier_counts)
            if tier_limit == 0:
                print(f"[{tier}] quota 충족으로 건너뜀 ({tier_counts[tier]:,}건)")
                continue

            for di, div in enumerate(DIVISIONS):
                if resume_mode and ti == start_tier and di < start_div:
                    continue

                while collected < target:
                    tier_limit = get_tier_limit(tier, target, collected, tier_counts)
                    if tier_limit == 0:
                        break

                    page = page_state[(tier, div)]
                    auto_id, collected, has_entries = collect_tier_page(
                        writer,
                        csvfile,
                            api_key,
                        tier,
                        div,
                        page,
                        auto_id,
                        collected,
                        target,
                        tier_counts,
                        )
                    if not has_entries:
                        exhausted.add((tier, div))
                        break

                    save_checkpoint(tier, div, page, collected)
                    page_state[(tier, div)] = page + 1

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

                    time.sleep(REQ_INTERVAL)

                print()
                if collected >= target:
                    break

            if collected >= target:
                break

        # ── 목표치 부족 시 하위 티어 추가 보충 ──
        if collected < target:
            print("-" * 50)
            print(f"quota 수집 후 부족분 발생: {target - collected:,}건 — 추가 보충 수집 시작")

        while collected < target:
            progress_made = False
            for tier in FILL_TIERS:
                for div in DIVISIONS:
                    if (tier, div) in exhausted:
                        continue

                    page = page_state[(tier, div)]
                    auto_id, collected, has_entries = collect_tier_page(
                        writer,
                        csvfile,
                            api_key,
                        tier,
                        div,
                        page,
                        auto_id,
                        collected,
                        target,
                        tier_counts,
                        )
                    if not has_entries:
                        exhausted.add((tier, div))
                        continue

                    progress_made = True
                    save_checkpoint(tier, div, page, collected)
                    page_state[(tier, div)] = page + 1

                    pct = collected / target * 100
                    print(
                        f"\r  [FILL {tier} {div}] p={page}  total {collected:>9,}/{target:,}  ({pct:.1f}%)",
                        end="",
                        flush=True,
                    )
                    time.sleep(REQ_INTERVAL)

                    if collected >= target:
                        break
                if collected >= target:
                    break

            if progress_made:
                print()
            else:
                print("\n더 이상 추가 수집 가능한 페이지가 없어 목표치를 채우지 못했습니다.")
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
