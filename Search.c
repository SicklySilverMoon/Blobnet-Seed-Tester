#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <pthread.h>
#include <time.h>

// Tiles
#define FLOOR       0x00
#define WALL        0x01
#define COSMIC_CHIP 0x02
#define EXIT        0x15
#define GRAVEL      0x2D
#define BLOB_N      0x5C
#define CHIP_S      0x6E

enum {
    NORTH = 0,
    EAST = 1,
    SOUTH = 2,
    WEST = 3
};

/*
 * How each move is represented via the index position system
 */
#define MOVE_DOWN    32
#define MOVE_UP     -32
#define MOVE_RIGHT    1
#define MOVE_LEFT    -1

typedef signed char DIR;
static const DIR diridx[4] = { MOVE_UP, MOVE_RIGHT, MOVE_DOWN, MOVE_LEFT };

typedef struct BLOB {
    int index;
    DIR dir;
} BLOB;

static bool verifyRoute(void);
static int canEnter(unsigned char tile);
static void moveBlob(unsigned long* seed, BLOB* b, unsigned char upper[]);
static void moveChip(char dir, int *chipIndex, unsigned char upper[]);
static void searchSeed(unsigned long seed, int step);
static void* searchPools(void* args);

static void nextvalue(unsigned long* currentValue);
void advance79(unsigned long* currentValue);
static void randomp4(unsigned long* currentValue, int* array);

typedef struct POOLINFO {
    unsigned long poolStart;
    unsigned long poolEnd;
} POOLINFO;

static char* route;
static int routeLength = 0;

#define NUM_BLOBS 80
static BLOB monsterListInitial[NUM_BLOBS]; //80 blobs in the level, the list simply keeps track of the current position/index of each blob as it appears in the level (order is of course that of the monster list)
static unsigned char mapInitial[1024];
static int chipIndexInitial;
static pthread_t* threadIDs;

int main(int argc, const char* argv[]) {
    if (argc == 1) {
        printf("Please enter the filename for the route to test\n");
        return 0;
    }
    
    FILE *file;

    file = fopen("blobnet.bin", "rb");
    fread(mapInitial, sizeof(mapInitial), 1, file);
    fclose(file);

    file = fopen(argv[1], "rb"); //The route filename should be provided via command line
    char character;
    while ((character = fgetc(file)) != EOF) { //Sharpeye's code for getting a file size that doesn't rely on SEEK_END
        routeLength++; //EOF is not a part of the original file and therefore incrementing the variable even after hitting means that the variable is equal to the file size
    }
    rewind(file);
    //The search loop reads two moves at a time, so add some padding at the end so we don't read past the array bounds.
    route = calloc(routeLength+10, sizeof(char)); //Create an array who's size is based on the route file size, calloc o inits the meory area
    fread(route, routeLength, 1, file);
    fclose(file);

    int listIndex = 0; //Put all the blobs into a list
    for (int c = 0; c < (int)sizeof(mapInitial); c++) {
        char tile = mapInitial[c];
        switch (tile) {
            case(BLOB_N): ;
                BLOB b = {c, NORTH}; //All the blobs are facing up
                if (listIndex < NUM_BLOBS) {
                    monsterListInitial[listIndex] = b;
                    listIndex++;
                }
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
                break; //0 corresponds to a wait/do nothing
            default:
                printf("ILLEGAL DIRECTION CHARACTER AT ROUTE POSITION %d, CHARACTER: %c\n", r, direction);
                route[r] = 0; //don't want things getting messed up
                break;
        }
    }

    if (!verifyRoute()) {
        printf("invalid route\n");
        return 1;
    }

    long numThreads = 2;
    if (getenv("NUMBER_OF_PROCESSORS")) {
        numThreads = strtol(getenv("NUMBER_OF_PROCESSORS"), NULL, 10);
    }

    threadIDs = malloc((numThreads - 1) * sizeof(pthread_t));
    unsigned long firstSeed = 0;
    unsigned long lastSeed = 2147483647UL;
    unsigned long seedPoolSize = (lastSeed-firstSeed+1)/numThreads;

    //clock_t time_a = clock();

    for (long threadNum = 0; threadNum < numThreads - 1; threadNum++) {  //Run a number of threads equal to system threads - 1
        POOLINFO* poolInfo = malloc(sizeof(POOLINFO)); //Starting seed and ending seed
        poolInfo->poolStart = firstSeed + seedPoolSize * threadNum;
        poolInfo->poolEnd = firstSeed + seedPoolSize * (threadNum + 1) - 1;
        //printf("Thread #%ld: start=%#lx\tend=%#lx\n", threadNum, poolInfo->poolStart, poolInfo->poolEnd);

        pthread_create(&threadIDs[threadNum], NULL, searchPools, (void*) poolInfo);
    }

    POOLINFO* poolInfo = malloc(sizeof(POOLINFO)); //Use the already existing main thread to do the last pool
    poolInfo->poolStart = firstSeed + seedPoolSize * (numThreads - 1);
    poolInfo->poolEnd = lastSeed;
    //printf("Main thread: start=%#lx\tend=%#lx\n", poolInfo->poolStart, poolInfo->poolEnd);
    searchPools((void*) poolInfo);

    for (int t = 0; t < numThreads - 1; t++) { //Make the main thread wait for the other threads to finish so the program doesn't end early
        pthread_join(threadIDs[t], NULL);
    }

    //clock_t time_b = clock();

    //printf("searched %ld seeds in %f ms\n", lastSeed-firstSeed+1, (time_b-time_a) * (1e3 / CLOCKS_PER_SEC));
    //printf("average %.1f us/seed\n", (time_b-time_a) * (1e6 / CLOCKS_PER_SEC) / (lastSeed-firstSeed+1));
}

static void* searchPools(void* args) {
    POOLINFO *poolInfo = ((POOLINFO*) args);
    for (unsigned long seed = poolInfo->poolStart; seed <= poolInfo->poolEnd; seed++) {
        searchSeed(seed, 1); //EVEN then ODD
        searchSeed(seed, 0);
    }
    return NULL;
}

static bool verifyRoute(void) {
    int chipIndex = chipIndexInitial;
    unsigned char map[1024];
    BLOB monsterList[NUM_BLOBS];
    memcpy(map, mapInitial, 1024);
    memcpy(monsterList, monsterListInitial, sizeof(struct BLOB)*NUM_BLOBS);

    int chipsNeeded = 88;
    int ok = true;
    for (int i = 0; i < routeLength; i++) {
        int dir = route[i];
        chipIndex = chipIndex + dir;
        if (map[chipIndex] == WALL) {
            printf("hit a wall at move %d\n", i);
            ok = false;
            break;
        }
        if (map[chipIndex] == COSMIC_CHIP) {
            map[chipIndex] = FLOOR;
            chipsNeeded -= 1;
        }
    }
    if (chipsNeeded > 0) {
        printf("route does not collect all chips\n");
        ok = false;
    }
    if (map[chipIndex] != EXIT) {
        printf("route does not reach the exit\n");
        ok = false;
    }
    return ok;
}

static void searchSeed(unsigned long seed, int step) { //Step: 1 = EVEN, 0 = ODD
    unsigned long startingSeed = seed;
    int chipIndex = chipIndexInitial;
    unsigned char map[1024];
    BLOB monsterList[NUM_BLOBS];
    memcpy(map, mapInitial, 1024);
    memcpy(monsterList, monsterListInitial, sizeof(struct BLOB)*NUM_BLOBS); //Set up copies of the arrays to be used so we don't have to read from file each time

    if (step) moveChip(route[0], &chipIndex, map);
    int i=step;
    while (i < routeLength) {
        moveChip(route[i++], &chipIndex, map);
        if (map[chipIndex] == BLOB_N) return;

        for (int j=0; j < NUM_BLOBS; j++) {
            moveBlob(&seed, &monsterList[j], map);
        }
        if (map[chipIndex] == BLOB_N) return;

        moveChip(route[i++], &chipIndex, map);
        if (map[chipIndex] == BLOB_N) return;
    }
    printf("Successful seed: %lu, Step: %c\n", startingSeed, step ? 'E' : 'O');
}

static void moveChip(char dir, int *chipIndex, unsigned char map[]) {
    *chipIndex = *chipIndex + dir;
    if (map[*chipIndex] == COSMIC_CHIP) map[*chipIndex] = FLOOR;
}

static const DIR turndirs[4][4] = {
    // ahead, left, back, right
    { NORTH, WEST, SOUTH, EAST }, // NORTH
    { EAST, NORTH, WEST, SOUTH }, // EAST
    { SOUTH, EAST, NORTH, WEST }, // SOUTH
    { WEST, SOUTH, EAST, NORTH }, // WEST
};

static void moveBlob(unsigned long* seed, BLOB* b, unsigned char upper[]) {
    int order[4] = {0, 1, 2, 3};
    randomp4(seed, order);

    for (int i=0; i<4; i++) {
        int dir = turndirs[b->dir][order[i]];
        unsigned char tile = upper[b->index + diridx[dir]];

        if (canEnter(tile)) {
            upper[b->index] = FLOOR;
            upper[b->index + diridx[dir]] = BLOB_N;
            b->dir = dir;
            b->index += diridx[dir];
            return;
        }
    }
}

static int canEnter(unsigned char tile) {
    return (tile == FLOOR);
}

static void nextvalue(unsigned long* currentValue)
{
    *currentValue = ((*currentValue * 1103515245UL) + 12345UL) & 0x7FFFFFFFUL; //Same generator/advancement Tile World uses
}

/*
 * Advance the RNG state by 79 values all at once
*/
void advance79(unsigned long* currentValue)
{
    *currentValue = ((*currentValue * 2441329573UL) + 2062159411UL) & 0x7FFFFFFFUL;
}

/* Randomly permute a list of four values. Three random numbers are
 * used, with the ranges [0,1], [0,1,2], and [0,1,2,3].
 */
static void randomp4(unsigned long* currentValue, int* array)
{
    int	n, t;

    nextvalue(currentValue);
    n = *currentValue >> 30;
    t = array[n];  array[n] = array[1];  array[1] = t;
    n = (int)((3.0 * (*currentValue & 0x0FFFFFFFUL)) / (double)0x10000000UL);
    t = array[n];  array[n] = array[2];  array[2] = t;
    n = (*currentValue >> 28) & 3;
    t = array[n];  array[n] = array[3];  array[3] = t;
}
