#ifndef BTREE_H
#define BTREE_H

#define BT_ORDER 32

typedef struct BTNode {
    int            keys[BT_ORDER + 1];
    void          *ptrs[BT_ORDER + 1];
    struct BTNode *children[BT_ORDER + 2];
    int            num_keys;
    int            is_leaf;
} BTNode;

typedef struct {
    BTNode *root;
} BTree;

BTree *btree_create(void);
void   btree_insert(BTree *tree, int key, void *record_ptr);
void  *btree_search(BTree *tree, int key);
int    btree_range(BTree *tree, int lo, int hi);
void   btree_free(BTree *tree);

#endif
