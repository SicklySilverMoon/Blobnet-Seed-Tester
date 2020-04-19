#include "macros.h"
#include "direction.h"
#include "random.h"
#include "blob.h"

static int canEnter(unsigned char tile);

void moveBlob(unsigned long* seed, BLOB* b, unsigned char upper[]) {
    int directions[4] = {b->dir, left(b->dir), back(b->dir), right(b->dir)};
    
    randomp4(seed, directions);
    
    for (int i=0; i<4; i++) {
        int dir = directions[i];
        unsigned char tile = upper[b->index + dir];
        
        if (canEnter(tile)) {
            upper[b->index] = FLOOR;
            upper[b->index + dir] = BLOB_N;
            b->dir = dir;
            b->index += dir;
            return;
        }
    }
}

static int canEnter(unsigned char tile) {
    return (tile == FLOOR);
}