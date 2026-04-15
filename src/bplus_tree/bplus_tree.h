#ifndef BPLUS_TREE_H
#define BPLUS_TREE_H

#define BP_ORDER 32

typedef struct BPLeaf {
    int            keys[BP_ORDER + 1];
    void          *ptrs[BP_ORDER + 1];
    int            num_keys;
    struct BPLeaf *next;
} BPLeaf;

typedef struct BPNode {
    int            keys[BP_ORDER + 1];
    struct BPNode *children[BP_ORDER + 2];
    int            num_keys;
    int            is_leaf;
    BPLeaf        *leaf;
} BPNode;

typedef struct {
    BPNode *root;
} BPTree;

BPTree *bptree_create(void);
void    bptree_insert(BPTree *tree, int key, void *record_ptr);
void   *bptree_search(BPTree *tree, int key);
int     bptree_range(BPTree *tree, int lo, int hi);
void    bptree_free(BPTree *tree);

#endif
