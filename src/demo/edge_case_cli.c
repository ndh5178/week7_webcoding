#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../common/player.h"
#include "../linear/linear_search.h"
#include "../btree/btree.h"
#include "../bplus_tree/bplus_tree.h"

long long g_op_count = 0;

typedef int (*ScenarioFn)(void);

typedef struct {
    const char *name;
    const char *description;
    ScenarioFn run;
} Scenario;

static void fill_players(Player *players, int count) {
    int i;

    for (i = 0; i < count; ++i) {
        players[i].id = i + 1;
        snprintf(players[i].name, sizeof(players[i].name), "Player_%02d", i + 1);
        players[i].win_rate = 42.0 + ((i * 17) % 4500) / 100.0;
        snprintf(players[i].rank, sizeof(players[i].rank), "Rank_%d", (i % 5) + 1);
    }
}

static void make_temp_path(char *buffer, size_t size, const char *label) {
    snprintf(buffer, size, "/tmp/week7_edge_cases_%ld_%s", (long)getpid(), label);
}

static char *read_text_file(const char *path) {
    FILE *fp = fopen(path, "rb");
    char *buffer;
    long size;

    if (!fp) {
        return NULL;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }

    size = ftell(fp);
    if (size < 0) {
        fclose(fp);
        return NULL;
    }

    rewind(fp);
    buffer = (char *)malloc((size_t)size + 1);
    if (!buffer) {
        fclose(fp);
        return NULL;
    }

    if (fread(buffer, 1, (size_t)size, fp) != (size_t)size) {
        free(buffer);
        fclose(fp);
        return NULL;
    }

    buffer[size] = '\0';
    fclose(fp);
    return buffer;
}

static int run_command_ok(const char *command) {
    int status = system(command);

    if (status == -1) {
        return 0;
    }

    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static int write_range_fixture_csv(const char *path) {
    FILE *fp = fopen(path, "w");
    int i;

    if (!fp) {
        return 0;
    }

    fprintf(fp, "id,닉네임,승률(%%),랭크\n");
    for (i = 1; i <= 30; ++i) {
        fprintf(fp, "%d,Player_%02d,%.2f,골드\n", i, i, 50.0 + (i % 10));
    }

    return fclose(fp) == 0;
}

static int write_bad_csv(const char *path) {
    FILE *fp = fopen(path, "w");

    if (!fp) {
        return 0;
    }

    fprintf(fp, "id,닉네임,승률(%%),랭크\n");
    fprintf(fp, "1,Alpha,55.00,골드\n");
    fprintf(fp, "2,BrokenLineOnlyTwoColumns\n");

    return fclose(fp) == 0;
}

static int scenario_missing_id(void) {
    Player players[8];
    BTree *btree = btree_create();
    BPTree *bptree = bptree_create();
    int ok = 1;
    int i;

    fill_players(players, 8);
    for (i = 0; i < 8; ++i) {
        btree_insert(btree, players[i].id, &players[i]);
        bptree_insert(bptree, players[i].id, &players[i]);
    }

    ok = ok && linear_search(players, 8, 999) == NULL;
    ok = ok && btree_search(btree, 999) == NULL;
    ok = ok && bptree_search(bptree, 999) == NULL;

    btree_free(btree);
    bptree_free(bptree);
    return ok;
}

static int scenario_range_outside(void) {
    Player players[12];
    BTree *btree = btree_create();
    BPTree *bptree = bptree_create();
    int ok = 1;
    int i;

    fill_players(players, 12);
    for (i = 0; i < 12; ++i) {
        btree_insert(btree, players[i].id, &players[i]);
        bptree_insert(bptree, players[i].id, &players[i]);
    }

    ok = ok && linear_range(players, 12, 50, 60) == 0;
    ok = ok && btree_range(btree, 50, 60) == 0;
    ok = ok && bptree_range(bptree, 50, 60) == 0;
    ok = ok && linear_range(players, 12, 10, 3) == 0;
    ok = ok && btree_range(btree, 10, 3) == 0;
    ok = ok && bptree_range(bptree, 10, 3) == 0;

    btree_free(btree);
    bptree_free(bptree);
    return ok;
}

static int scenario_split_search(void) {
    enum { PLAYER_COUNT = 64 };
    Player *players = (Player *)calloc(PLAYER_COUNT, sizeof(Player));
    BTree *btree = btree_create();
    BPTree *bptree = bptree_create();
    int ok = 1;
    int i;

    if (!players || !btree || !bptree) {
        free(players);
        btree_free(btree);
        bptree_free(bptree);
        return 0;
    }

    fill_players(players, PLAYER_COUNT);
    for (i = 0; i < PLAYER_COUNT; ++i) {
        btree_insert(btree, players[i].id, &players[i]);
        bptree_insert(bptree, players[i].id, &players[i]);
    }

    for (i = 0; i < PLAYER_COUNT; ++i) {
        Player *from_btree = (Player *)btree_search(btree, players[i].id);
        Player *from_bptree = (Player *)bptree_search(bptree, players[i].id);

        if (!from_btree || !from_bptree) {
            ok = 0;
            break;
        }
        if (from_btree->id != players[i].id || from_bptree->id != players[i].id) {
            ok = 0;
            break;
        }
    }

    free(players);
    btree_free(btree);
    bptree_free(bptree);
    return ok;
}

static int scenario_duplicate_id(void) {
    enum { PLAYER_COUNT = 48 };
    Player *players = (Player *)calloc(PLAYER_COUNT, sizeof(Player));
    Player replacement;
    BTree *btree = btree_create();
    BPTree *bptree = bptree_create();
    Player *from_btree;
    Player *from_bptree;
    int target_id = 33;
    int i;

    if (!players || !btree || !bptree) {
        free(players);
        btree_free(btree);
        bptree_free(bptree);
        return 0;
    }

    fill_players(players, PLAYER_COUNT);
    for (i = 0; i < PLAYER_COUNT; ++i) {
        btree_insert(btree, players[i].id, &players[i]);
        bptree_insert(bptree, players[i].id, &players[i]);
    }

    replacement = players[target_id - 1];
    snprintf(replacement.name, sizeof(replacement.name), "Replacement_33");
    replacement.win_rate = 99.99;
    snprintf(replacement.rank, sizeof(replacement.rank), "Rank_X");

    btree_insert(btree, target_id, &replacement);
    bptree_insert(bptree, target_id, &replacement);

    from_btree = (Player *)btree_search(btree, target_id);
    from_bptree = (Player *)bptree_search(bptree, target_id);

    free(players);
    btree_free(btree);
    bptree_free(bptree);

    return from_btree == &replacement &&
           from_bptree == &replacement &&
           strcmp(replacement.name, "Replacement_33") == 0;
}

static int scenario_missing_name(void) {
    Player players[10];

    fill_players(players, 10);
    return linear_search_name(players, 10, "NotExists") == NULL;
}

static int scenario_duplicate_name(void) {
    Player players[4];

    fill_players(players, 4);
    snprintf(players[0].name, sizeof(players[0].name), "Shadow");
    snprintf(players[1].name, sizeof(players[1].name), "Shadow");
    snprintf(players[2].name, sizeof(players[2].name), "Other");
    snprintf(players[3].name, sizeof(players[3].name), "Shadow");

    return linear_search_name(players, 4, "Shadow") == &players[0];
}

static int scenario_shell_missing_name(void) {
    char csv_path[128];
    char output_path[128];
    char command[512];
    char *output = NULL;
    int ok = 0;

    make_temp_path(csv_path, sizeof(csv_path), "shell_missing_name.csv");
    make_temp_path(output_path, sizeof(output_path), "shell_missing_name.log");

    if (write_range_fixture_csv(csv_path)) {
        snprintf(
            command,
            sizeof(command),
            "printf 'SELECT NAME MissingUser\\nQUIT\\n' | ./bin/shell %s > %s",
            csv_path,
            output_path
        );
        if (run_command_ok(command)) {
            output = read_text_file(output_path);
            ok = output && strstr(output, "[선형 탐색] nickname=MissingUser — 없음") != NULL;
        }
    }

    free(output);
    remove(csv_path);
    remove(output_path);
    return ok;
}

static int scenario_shell_bad_csv(void) {
    char csv_path[128];
    char output_path[128];
    char command[512];
    char *output = NULL;
    int ok = 0;

    make_temp_path(csv_path, sizeof(csv_path), "shell_bad_csv.csv");
    make_temp_path(output_path, sizeof(output_path), "shell_bad_csv.log");

    if (write_bad_csv(csv_path)) {
        snprintf(
            command,
            sizeof(command),
            "printf 'LOADCSV %s\\nQUIT\\n' | ./bin/shell > %s",
            csv_path,
            output_path
        );
        if (run_command_ok(command)) {
            output = read_text_file(output_path);
            ok = output && strstr(output, "CSV 파싱 실패") != NULL;
        }
    }

    free(output);
    remove(csv_path);
    remove(output_path);
    return ok;
}

static int scenario_shell_unknown_select(void) {
    char csv_path[128];
    char output_path[128];
    char command[512];
    char *output = NULL;
    int ok = 0;

    make_temp_path(csv_path, sizeof(csv_path), "shell_unknown_select.csv");
    make_temp_path(output_path, sizeof(output_path), "shell_unknown_select.log");

    if (write_range_fixture_csv(csv_path)) {
        snprintf(
            command,
            sizeof(command),
            "printf 'SELECT SCORE 10\\nQUIT\\n' | ./bin/shell %s > %s",
            csv_path,
            output_path
        );
        if (run_command_ok(command)) {
            output = read_text_file(output_path);
            ok = output && strstr(output, "알 수 없는 SELECT 대상: SCORE") != NULL;
        }
    }

    free(output);
    remove(csv_path);
    remove(output_path);
    return ok;
}

static int scenario_shell_bench_too_small(void) {
    char csv_path[128];
    char output_path[128];
    char command[512];
    char *output = NULL;
    int ok = 0;

    make_temp_path(csv_path, sizeof(csv_path), "shell_bench_small.csv");
    make_temp_path(output_path, sizeof(output_path), "shell_bench_small.log");

    if (write_range_fixture_csv(csv_path)) {
        snprintf(
            command,
            sizeof(command),
            "printf 'SELECT ID 1\\nBENCH\\nQUIT\\n' | ./bin/shell %s > %s",
            csv_path,
            output_path
        );
        if (run_command_ok(command)) {
            output = read_text_file(output_path);
            ok = output && strstr(output, "벤치마크 (n=30") != NULL;
        }
    }

    free(output);
    remove(csv_path);
    remove(output_path);
    return ok;
}

static int scenario_empty_bench_guard(void) {
    char output_path[128];
    char command[512];
    char *output = NULL;
    int ok = 0;

    make_temp_path(output_path, sizeof(output_path), "shell_empty_bench.log");
    snprintf(
        command,
        sizeof(command),
        "printf 'BENCH\\nQUIT\\n' | ./bin/shell > %s",
        output_path
    );

    if (run_command_ok(command)) {
        output = read_text_file(output_path);
        ok = output && strstr(output, "레코드가 너무 적습니다") != NULL;
    }

    free(output);
    remove(output_path);
    return ok;
}

static Scenario SCENARIOS[] = {
    {"missing-id", "없는 ID 검색은 NULL/없음으로 처리", scenario_missing_id},
    {"range-outside", "범위 밖 또는 뒤집힌 범위는 0건 처리", scenario_range_outside},
    {"split-search", "split 이후에도 모든 키가 검색 가능", scenario_split_search},
    {"duplicate-id", "duplicate key 재삽입은 최신 포인터로 갱신", scenario_duplicate_id},
    {"missing-name", "없는 name 검색은 NULL 처리", scenario_missing_name},
    {"duplicate-name", "중복 name 검색은 첫 번째 매치를 반환", scenario_duplicate_name},
    {"shell-missing-name", "CLI SELECT NAME miss 케이스", scenario_shell_missing_name},
    {"shell-bad-csv", "CLI LOADCSV malformed CSV 케이스", scenario_shell_bad_csv},
    {"shell-unknown-select", "CLI SELECT 잘못된 필드 입력", scenario_shell_unknown_select},
    {"shell-bench", "CLI BENCH 기본 실행 경계값", scenario_shell_bench_too_small},
    {"empty-bench-guard", "빈 테이블에서 BENCH 보호 메시지", scenario_empty_bench_guard},
};

static const int SCENARIO_COUNT = (int)(sizeof(SCENARIOS) / sizeof(SCENARIOS[0]));

static void print_usage(const char *argv0) {
    int i;

    printf("Usage:\n");
    printf("  %s list\n", argv0);
    printf("  %s all\n", argv0);
    printf("  %s <scenario-name>\n", argv0);
    printf("\n");
    printf("Available scenarios:\n");
    for (i = 0; i < SCENARIO_COUNT; ++i) {
        printf("  %-20s %s\n", SCENARIOS[i].name, SCENARIOS[i].description);
    }
}

static const Scenario *find_scenario(const char *name) {
    int i;

    for (i = 0; i < SCENARIO_COUNT; ++i) {
        if (strcmp(SCENARIOS[i].name, name) == 0) {
            return &SCENARIOS[i];
        }
    }
    return NULL;
}

static int run_one(const Scenario *scenario) {
    int ok = scenario->run();

    printf("[%s] %s - %s\n", ok ? "PASS" : "FAIL", scenario->name, scenario->description);
    return ok;
}

int main(int argc, char **argv) {
    int passed = 0;
    int failed = 0;
    int i;

    if (argc < 2 || strcmp(argv[1], "list") == 0 || strcmp(argv[1], "--help") == 0) {
        print_usage(argv[0]);
        return 0;
    }

    if (strcmp(argv[1], "all") == 0) {
        for (i = 0; i < SCENARIO_COUNT; ++i) {
            if (run_one(&SCENARIOS[i])) {
                passed += 1;
            } else {
                failed += 1;
            }
        }

        printf("\nSummary: %d passed / %d failed\n", passed, failed);
        return failed == 0 ? 0 : 1;
    }

    {
        const Scenario *scenario = find_scenario(argv[1]);
        if (!scenario) {
            fprintf(stderr, "Unknown scenario: %s\n\n", argv[1]);
            print_usage(argv[0]);
            return 1;
        }

        return run_one(scenario) ? 0 : 1;
    }
}
