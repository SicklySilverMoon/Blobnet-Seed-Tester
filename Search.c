#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "random.h"
#include "blob.h"
#include "macros.h"
#include "direction.h"

void searchSeed(unsigned long seed, int step);
void moveChip(char dir, int *chipIndex, unsigned char upper[]);
void* searchPools(void* args);

typedef struct POOLINFO {
    unsigned long poolStart;
    unsigned long poolEnd;
} POOLINFO;

static char* route;
static int routeLength = 0;

static BLOB monsterListInitial[80]; //80 blobs in the level, the list simply keeps track of the current position/index of each blob as it appears in the level (order is of course that of the monster list)
static unsigned char mapInitial[1024];
static int chipIndexInitial;
static pthread_t* threadIDs;

int main(int argc, const char* argv[]) {
    FILE *file;
    
    file = fopen("blobnet.bin", "rb");
    fread(mapInitial, sizeof(mapInitial), 1, file);
    fclose(file);
    
    file = fopen("route.bin", "rb");
    char character;
    while ((character = fgetc(file)) != EOF) { //Sharpeye's code for getting a file size that doesn't rely on SEEK_END
        routeLength++; //EOF is not a part of the original file and therefore incrementing the variable even after hitting means that the variable is equal to the file size
    }
    rewind(file);
    if (!(routeLength & 0x1)) { //The search loop requires an odd number of moves total to avoid invoking UB and get a more efficient loop, so if the route length is even we add 1 to it and insert a move with a direction of 0 onto the end of the route
        routeLength++;
        route = malloc(routeLength*sizeof(char)); //Create an array who's size is based on the route file size
        route[routeLength - 1] = 0;
    }
    else {
        route = malloc(routeLength*sizeof(char)); //Create an array who's size is based on the route file size
    }
    fread(route, routeLength, 1, file);
    fclose(file);
    
    int listIndex = 0; //Put all the blobs into a list
    for (int c = 0; c < sizeof(mapInitial); c++) {
        char tile = mapInitial[c];
        switch (tile) {
            case(BLOB_N): ;
                BLOB b = {c, MOVE_UP}; //All the blobs are facing up
                monsterListInitial[listIndex] = b;
                listIndex++;
                break;
            case(CHIP_S): //Chip-S, the only Chip tile that appears in blobnet
                chipIndexInitial = c;
                mapInitial[c] = GRAVEL; //Set the tile to gravel for map consistency once Chip is removed
                break;
        }
    }
    
    for (int r = 0; r < routeLength; r++) {
        char direction = route[r];
        switch (direction) {
            case ('d'):
                route[r] = MOVE_DOWN;
                break;
            case ('u'):
                route[r] = MOVE_UP;
                break;
            case ('r'):
                route[r] = MOVE_RIGHT;
                break;
            case ('l'):
                route[r] = MOVE_LEFT;
                break;
            case (0):
                break; //See the thing about even routes above
            default:
                printf("ILLEGAL DIRECTION CHARACTER AT ROUTE POSITION %d, CHARACTER: %c\n", r, direction);
                route[r] = 0; //don't want things getting messed up
                break;
        }
    }
    
    threadIDs = malloc((NUM_THREADS - 1) * sizeof(pthread_t));
    unsigned long lastSeed = 2147483647UL;
    unsigned long seedPoolSize = lastSeed/NUM_THREADS;
    printf("POOL SIZE: %lu\n", seedPoolSize);

    for (long threadNum = 0; threadNum < NUM_THREADS - 1; threadNum++) {  //Run a number of threads equal to system threads - 1
        POOLINFO* poolInfo = malloc(sizeof(POOLINFO)); //Starting seed and ending seed
        poolInfo->poolStart = seedPoolSize * threadNum;
        poolInfo->poolEnd = seedPoolSize * (threadNum + 1) - 1;
        
        pthread_create(&threadIDs[threadNum], NULL, searchPools, (void*) poolInfo);
    }
    
    POOLINFO* poolInfo = malloc(sizeof(POOLINFO)); //Use the already existing main thread to do the last pool
    poolInfo->poolStart = seedPoolSize * (NUM_THREADS - 1);
    poolInfo->poolEnd = lastSeed;
    searchPools((void*) poolInfo);
    
    for (int t = 0; t < NUM_THREADS - 1; t++) { //Make the main thread wait for the other threads to finish so the program doesn't end early
        pthread_join(threadIDs[t], NULL);
    }
}

void* searchPools(void* args) {
    //unsigned long seedsTested = 0;
    POOLINFO *poolInfo = ((POOLINFO*) args);
    
    for (unsigned long seed = poolInfo->poolStart; seed <= poolInfo->poolEnd; seed++) {
        searchSeed(seed, 1); //Search EVEN than odd
        searchSeed(seed, 0);
    //    seedsTested++;
    }
    
    //printf("EXITING THREAD ID: %I64d, SEEDS TESTED: %lu\n", pthread_self(), seedsTested);
    //pthread_exit(0);
}

void searchSeed(unsigned long seed, int step) { //step, 1 for EVEN, 0 for ODD
    unsigned long startingSeed = seed;
    int chipIndex = chipIndexInitial;
    unsigned char map[1024];
    BLOB monsterList[80];
    memcpy(map, mapInitial, 1024);
    memcpy(monsterList, monsterListInitial, sizeof(struct BLOB)*80); //Set up copies of the arrays to be used so we don't have to read from file each time
    
    if (step) moveChip(route[0], &chipIndex, map);
    int i=step;
    while (i < routeLength) {
        moveChip(route[i++], &chipIndex, map);
        if (map[chipIndex] == BLOB_N) return;
        
        for (int j=0; j < 80; j++) {
            moveBlob(&seed, &monsterList[j], map);
        }
        if (map[chipIndex] == BLOB_N) return;
        
        moveChip(route[i++], &chipIndex, map);
        if (map[chipIndex] == BLOB_N) return;
    }
    printf("Successful seed: %lu; Step: %c\n", startingSeed, step ? 'E' : 'O');
}

void moveChip(char dir, int *chipIndex, unsigned char map[]) {
    *chipIndex = *chipIndex + dir;
    if (map[*chipIndex] == COSMIC_CHIP) map[*chipIndex] = FLOOR;
}