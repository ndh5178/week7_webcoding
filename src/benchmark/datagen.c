#include <stdio.h>
#include <stdlib.h>

#include "../common/dataset_io.h"

int main(int argc, char **argv) {
    int count = 1000000;
    const char *output_path = NULL;

    if (argc >= 2) {
        count = atoi(argv[1]);
        if (count <= 0) {
            fprintf(stderr, "count must be positive\n");
            return 1;
        }
    }

    if (argc >= 3) {
        output_path = argv[2];
    }

    if (output_path) {
        if (!write_players_csv(output_path, count)) {
            fprintf(stderr, "failed to write csv: %s\n", output_path);
            return 1;
        }
        printf("Wrote %d rows to %s\n", count, output_path);
    } else {
        Player *players = (Player *)malloc(sizeof(Player) * count);
        int i;

        if (!players) {
            fprintf(stderr, "memory allocation failed\n");
            return 1;
        }

        populate_players(players, count);
        printf("id,닉네임,승률(%%),랭크\n");
        for (i = 0; i < count; ++i) {
            printf("%d,%s,%.2f,%s\n", players[i].id, players[i].name, players[i].win_rate, players[i].rank);
        }
        free(players);
    }

    return 0;
}
