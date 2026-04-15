#include "dataset_io.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *rank_from_win_rate(double win_rate) {
    if (win_rate >= 85.0) return "챌린저";
    if (win_rate >= 75.0) return "다이아";
    if (win_rate >= 65.0) return "플래티넘";
    if (win_rate >= 55.0) return "골드";
    return "실버";
}

void populate_players(Player *players, int count) {
    int i;

    for (i = 0; i < count; ++i) {
        players[i].id = i + 1;
        snprintf(players[i].name, sizeof(players[i].name), "StarlightX_%d", i % 10000);
        players[i].win_rate = 42.0 + ((i * 173) % 5700) / 100.0;
        snprintf(players[i].rank, sizeof(players[i].rank), "%s", rank_from_win_rate(players[i].win_rate));
    }
}

int write_players_csv(const char *path, int count) {
    FILE *fp;
    Player *players;
    int i;

    if (!path || count <= 0) {
        return 0;
    }

    fp = fopen(path, "w");
    if (!fp) {
        return 0;
    }

    players = (Player *)malloc(sizeof(Player) * count);
    if (!players) {
        fclose(fp);
        return 0;
    }

    populate_players(players, count);
    fprintf(fp, "id,닉네임,승률(%%),랭크\n");
    for (i = 0; i < count; ++i) {
        fprintf(fp, "%d,%s,%.2f,%s\n", players[i].id, players[i].name, players[i].win_rate, players[i].rank);
    }

    free(players);
    fclose(fp);
    return 1;
}

Player *load_players_csv(const char *path, int *out_count) {
    FILE *fp;
    Player *players = NULL;
    int capacity = 0;
    int count = 0;
    char line[256];

    if (out_count) {
        *out_count = 0;
    }

    if (!path) {
        return NULL;
    }

    fp = fopen(path, "r");
    if (!fp) {
        return NULL;
    }

    if (!fgets(line, sizeof(line), fp)) {
        fclose(fp);
        return NULL;
    }

    while (fgets(line, sizeof(line), fp)) {
        Player player;
        int parsed;

        parsed = sscanf(line, "%d,%31[^,],%lf,%23[^\r\n]", &player.id, player.name, &player.win_rate, player.rank);
        if (parsed != 4) {
            continue;
        }

        if (count == capacity) {
            int next_capacity = capacity == 0 ? 1024 : capacity * 2;
            Player *next = (Player *)realloc(players, sizeof(Player) * next_capacity);
            if (!next) {
                free(players);
                fclose(fp);
                return NULL;
            }
            players = next;
            capacity = next_capacity;
        }

        players[count++] = player;
    }

    fclose(fp);

    if (out_count) {
        *out_count = count;
    }
    return players;
}
