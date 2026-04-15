#ifndef PLAYER_H
#define PLAYER_H

typedef struct {
    int   id;
    char  name[32];
    double win_rate;
    char  rank[24];
} Player;

#endif
