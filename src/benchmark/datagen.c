/*
 * datagen.c — 플레이어 데이터 로더/생성기
 *
 * 모드 1 (기본): 랜덤 데이터 생성
 *   ./bin/datagen [N]
 *
 * 모드 2: Riot API 수집 CSV 로드
 *   ./bin/datagen --csv players.csv
 *   CSV 형식: id, name(닉네임), winrate(승률%), rank(티어_디비전)
 *
 * 두 경우 모두 players_bin.dat (바이너리) 저장 → benchmark.c가 읽어 사용
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
    int  id;
    char name[32];
    int  score;
    char tier[16];
} Player;

/* ──────────────────────────────────────────────
 * 유틸
 * ────────────────────────────────────────────── */

static const char *NAMES[] = {
    "ShadowBlade","NightFury","CrimsonAce","StarlightX","IronWolf",
    "PixelKing","VoidHunter","FrostByte","ThunderX","NeonRift",
    "DarkMatter","CyberGhost","RedStorm","BlueFang","GoldRush",
    "SilverEdge","PurpleRain","OmegaZero","AlphaStrike","BetaForce"
};
#define NAME_COUNT 20

static const char *get_tier(int score) {
    if (score >= 9500) return "Challenger";
    if (score >= 8500) return "Diamond";
    if (score >= 7000) return "Platinum";
    if (score >= 5000) return "Gold";
    return "Silver";
}

/* ──────────────────────────────────────────────
 * CSV 로드 모드
 * ────────────────────────────────────────────── */
static int load_from_csv(const char *csv_path) {
    FILE *fp = fopen(csv_path, "r");
    if (!fp) {
        fprintf(stderr, "CSV 파일을 열 수 없습니다: %s\n", csv_path);
        return 1;
    }

    /* 줄 수 먼저 세기 (헤더 1줄 제외) */
    int n = 0;
    char line[256];
    if (!fgets(line, sizeof(line), fp)) { fclose(fp); return 1; } /* 헤더 스킵 */
    while (fgets(line, sizeof(line), fp)) n++;
    rewind(fp);
    if (!fgets(line, sizeof(line), fp)) { fclose(fp); return 1; } /* 헤더 다시 스킵 */

    printf("CSV에서 %d개 레코드 로드 중... (%s)\n", n, csv_path);

    Player *table = malloc(sizeof(Player) * n);
    if (!table) { fprintf(stderr, "메모리 부족\n"); fclose(fp); return 1; }

    int i = 0;
    while (i < n && fgets(line, sizeof(line), fp)) {
        /* 형식: id,name,score,tier */
        char id_s[16], name_s[64], score_s[16], tier_s[32];
        /* CSV 파싱: 쉼표 구분 */
        char *tok = strtok(line, ",");
        if (!tok) continue;
        strncpy(id_s, tok, sizeof(id_s)-1);

        tok = strtok(NULL, ",");
        if (!tok) continue;
        strncpy(name_s, tok, sizeof(name_s)-1);

        tok = strtok(NULL, ",");
        if (!tok) continue;
        strncpy(score_s, tok, sizeof(score_s)-1);

        tok = strtok(NULL, "\r\n,");
        if (!tok) continue;
        strncpy(tier_s, tok, sizeof(tier_s)-1);

        table[i].id    = atoi(id_s);
        strncpy(table[i].name,  name_s,  31);  table[i].name[31]  = '\0';
        table[i].score = atoi(score_s);
        strncpy(table[i].tier,  tier_s,  15);  table[i].tier[15]  = '\0';
        i++;
    }
    fclose(fp);
    n = i; /* 실제 파싱된 수 */

    /* 바이너리로 저장 */
    FILE *out = fopen("players_bin.dat", "wb");
    if (!out) { fprintf(stderr, "출력 파일 생성 실패\n"); free(table); return 1; }
    fwrite(&n, sizeof(int), 1, out);
    fwrite(table, sizeof(Player), n, out);
    fclose(out);

    printf("로드 완료: %d개 → players_bin.dat\n", n);
    printf("%-10s %-32s %-10s %s\n", "ID", "닉네임", "승률(%)", "랭크");
    int show = n < 5 ? n : 5;
    for (int j = 0; j < show; j++)
        printf("%-10d %-32s %-8d %s\n",
               table[j].id, table[j].name, table[j].score, table[j].tier);

    free(table);
    return 0;
}

/* ──────────────────────────────────────────────
 * 랜덤 생성 모드 (기존)
 * ────────────────────────────────────────────── */
static int generate_random(int n) {
    srand((unsigned)time(NULL));
    Player *table = malloc(sizeof(Player) * n);
    if (!table) { fprintf(stderr, "메모리 부족\n"); return 1; }

    printf("랜덤 레코드 %d개 생성 중...\n", n);
    for (int i = 0; i < n; i++) {
        table[i].id    = i + 1;
        int ni = (i + rand() % 5) % NAME_COUNT;
        snprintf(table[i].name, 32, "%s_%d", NAMES[ni], i % 9999);
        table[i].score = rand() % 10000;
        strncpy(table[i].tier, get_tier(table[i].score), 16);
    }

    /* 바이너리로 저장 */
    FILE *out = fopen("players_bin.dat", "wb");
    if (!out) { fprintf(stderr, "출력 파일 생성 실패\n"); free(table); return 1; }
    fwrite(&n, sizeof(int), 1, out);
    fwrite(table, sizeof(Player), n, out);
    fclose(out);

    printf("생성 완료: %d개 → players_bin.dat\n", n);
    printf("%-10s %-24s %-8s %s\n", "ID", "Name", "Score", "Tier");
    for (int i = 0; i < 5; i++)
        printf("%-10d %-24s %-8d %s\n",
               table[i].id, table[i].name, table[i].score, table[i].tier);

    free(table);
    return 0;
}

/* ──────────────────────────────────────────────
 * main
 * ────────────────────────────────────────────── */
int main(int argc, char *argv[]) {
    if (argc >= 3 && strcmp(argv[1], "--csv") == 0) {
        return load_from_csv(argv[2]);
    }
    int n = 1000000;
    if (argc >= 2) n = atoi(argv[1]);
    return generate_random(n);
}
