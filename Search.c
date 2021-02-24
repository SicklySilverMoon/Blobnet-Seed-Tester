#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <limits.h>

#define DEBUG 1

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

enum {
    ODD = 0,
    EVEN = 1
};

enum {
    MS, TW
};

/*
 * How each move is represented via the index position system
 */
#define MOVE_DOWN    32
#define MOVE_UP     -32
#define MOVE_RIGHT    1
#define MOVE_LEFT    -1

typedef signed char Dir;
static const Dir diridx[4] = { MOVE_UP, MOVE_RIGHT, MOVE_DOWN, MOVE_LEFT };
static const Dir yxtodir[3][3] = {
    { -1, NORTH, -1 },
    { WEST, -1, EAST },
    { -1, SOUTH, -1 },
};

struct prng {
    uint32_t value;
};

static void msadvance(struct prng* rng);
static int msrandn(struct prng* rng, int max);

static void twadvance(struct prng* rng);
static void twadvance79(struct prng* rng);
static void twadvance78(struct prng* rng);
static void twadvance71(struct prng* rng);
static void twadvance4(struct prng* rng);
static void twadvance7(struct prng* rng);
static void twrandomp4(struct prng* rng, int* array);

typedef struct Blob {
    int index;
    Dir dir;
} Blob;

static bool verifyRoute(void);
#define canEnter(tile) (tile == FLOOR)
static void moveBlobMS(struct prng* rng, Blob* b, unsigned char upper[]);
static void moveBlobTW(struct prng* rng, Blob* b, unsigned char upper[]);
static void moveChip(char dir, int* chipIndex, unsigned char upper[]);
static void searchSeed(int rngtype, uint_fast32_t seed, int step);
static void* searchPools(void* args);

static int twIntro(struct prng rng, int step, unsigned char upper[]);

typedef struct PoolInfo {
    uint_fast32_t poolStart;
    uint_fast32_t poolEnd;
} PoolInfo;

static char* route;
static int routeLength = 0;

#define NUM_BLOBS 80
static Blob monsterListInitial[NUM_BLOBS]; //80 blobs in the level, the list simply keeps track of the current position/index of each blob as it appears in the level (order is of course that of the monster list)
static unsigned char mapInitial[1024];
static int chipIndexInitial;
static pthread_t* threadIDs;

int main(int argc, const char* argv[]) {
    if (argc == 1) {
        printf("Please enter the filename for the route to test\n");
        return 0;
    }
    
    FILE* file;

    file = fopen("blobnet.bin", "rb");
    fread(mapInitial, sizeof(mapInitial), 1, file);
    fclose(file);

    file = fopen(argv[1], "rb"); //The route filename should be provided via command line
    if (file == NULL) {
        perror(argv[1]);
        return 1;
    }
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
                if (listIndex < NUM_BLOBS) {
                    Blob b = {c, NORTH}; //All the blobs are facing up
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
    uint_fast32_t firstSeed = 0;
    uint_fast32_t lastSeed = 0x7FFFFFFFUL;
    uint_fast32_t seedPoolSize = (lastSeed-firstSeed+1)/numThreads;

    #if DEBUG
        clock_t time_a = clock();
    #endif

    for (long threadNum = 0; threadNum < numThreads - 1; threadNum++) {  //Run a number of threads equal to system threads - 1
        PoolInfo* poolInfo = malloc(sizeof(PoolInfo)); //Starting seed and ending seed
        poolInfo->poolStart = firstSeed + seedPoolSize * threadNum;
        poolInfo->poolEnd = firstSeed + seedPoolSize * (threadNum + 1) - 1;
        #if DEBUG
            printf("Thread #%ld: start=0x%" PRIXFAST32 "\tend=0x%" PRIXFAST32 "\n", threadNum, poolInfo->poolStart, poolInfo->poolEnd);
        #endif

        pthread_create(&threadIDs[threadNum], NULL, searchPools, (void*) poolInfo);
    }

    PoolInfo* poolInfo = malloc(sizeof(PoolInfo)); //Use the already existing main thread to do the last pool
    poolInfo->poolStart = firstSeed + seedPoolSize * (numThreads - 1);
    poolInfo->poolEnd = lastSeed;
    #if DEBUG
        printf("Main thread: start=0x%" PRIXFAST32 "\tend=0x%" PRIXFAST32 "\n", poolInfo->poolStart, poolInfo->poolEnd);
    #endif
    searchPools((void*) poolInfo);

    for (int t = 0; t < numThreads - 1; t++) { //Make the main thread wait for the other threads to finish so the program doesn't end early
        pthread_join(threadIDs[t], NULL);
    }

    #if DEBUG
        uint_fast32_t numSeeds = lastSeed - firstSeed + 1;
        clock_t time_b = clock();
        double duration = time_b - time_a;
        
        printf("searched %" PRIuFAST32 " seeds in %f ms\n", numSeeds, duration * (1e3 / CLOCKS_PER_SEC));
        printf("average %.1f us/seed\n", (duration * (1e9 / CLOCKS_PER_SEC)) / numSeeds);
    #endif
}

static void* searchPools(void* args) {
    PoolInfo* poolInfo = ((PoolInfo*) args);
    for (uint_fast32_t seed = poolInfo->poolStart; seed <= poolInfo->poolEnd; seed++) {
        searchSeed(TW, seed, EVEN);
        searchSeed(TW, seed, ODD);
        searchSeed(MS, seed, EVEN);
        searchSeed(MS, seed, ODD);
    }
    return NULL;
}

static bool verifyRoute(void) {
    int chipIndex = chipIndexInitial;
    unsigned char map[1024];
    Blob monsterList[NUM_BLOBS];
    memcpy(map, mapInitial, 1024);
    memcpy(monsterList, monsterListInitial, sizeof(Blob) * NUM_BLOBS);

    int chipsNeeded = 88;
    for (int i = 0; i < routeLength; i++) {
        int dir = route[i];
        chipIndex = chipIndex + dir;
        if (map[chipIndex] == WALL) {
            printf("hit a wall at move %d\n", i);
            return false;
        }
        if (map[chipIndex] == COSMIC_CHIP) {
            map[chipIndex] = FLOOR;
            chipsNeeded -= 1;
        }
    }
    int ok = true;
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

static void searchSeed(int rngtype, uint_fast32_t startingSeed, int step) { //Step: 1 = EVEN, 0 = ODD
    struct prng rng = {startingSeed};
    int chipIndex = chipIndexInitial;
    unsigned char map[1024];
    Blob monsterList[NUM_BLOBS];

    // Optimization for MSCC:
    // When a blob moves, it first selects a facing direction. If this
    // direction isn't one of the 4 valid directions, it will reroll until it
    // is. For the _very first move_ of the _very first blob_ of a seed,
    // rerolling is equivalent to starting at a different seed.
    // Assuming we search the entire rng state space, we will eventually
    // encounter that seed directly, so there's no need to continue evaluating
    // this seed.
    //
    // This eliminates 5/9 of the search space.
    if (rngtype == MS) {
        struct prng tmp = rng;
        int xdir = msrandn(&tmp, 3);
        int ydir = msrandn(&tmp, 3);
        if (yxtodir[ydir][xdir] == -1) {
            return;
        }
    }
    // Optimization for TW:
    // By simulating only the starting area via single equation RNG advancement,
    // only looking for collisions on tiles where its known they can occur,
    // and only simulating 2 blobs, TW's seed space can be reduced by roughly 80%
    else {
        unsigned char tempMap[1024];
        memcpy(tempMap, mapInitial, 1024);
        if (!twIntro(rng, step, tempMap))
            return;
    }

    memcpy(map, mapInitial, 1024);
    memcpy(monsterList, monsterListInitial, sizeof(Blob) * NUM_BLOBS); //Set up copies of the arrays to be used so we don't have to read from file each time

    if (step == EVEN) {
        moveChip(route[0], &chipIndex, map);
    }
    int i=step;
    while (i < routeLength) {
        moveChip(route[i++], &chipIndex, map);
        if (map[chipIndex] == BLOB_N) return;

        for (int j=0; j < NUM_BLOBS; j++) {
            if (rngtype == TW) {
                moveBlobTW(&rng, &monsterList[j], map);
            } else {
                moveBlobMS(&rng, &monsterList[j], map);
            }
        }
        if (map[chipIndex] == BLOB_N) return;

        moveChip(route[i++], &chipIndex, map);
        if (map[chipIndex] == BLOB_N) return;
    }
    printf("Successful seed: %" PRIuFAST32 ", Step: %s, RNG: %s\n", startingSeed, step == EVEN ? "even" : "odd", rngtype == TW ? "TW" : "MS");
}

/* TW search */

static void moveChip(char dir, int* chipIndex, unsigned char map[]) {
    *chipIndex = *chipIndex + dir;
    if (map[*chipIndex] == COSMIC_CHIP) map[*chipIndex] = FLOOR;
}

static const Dir twturndirs[4][4] = {
    // ahead, left, back, right
    { NORTH, WEST, SOUTH, EAST }, // NORTH
    { EAST, NORTH, WEST, SOUTH }, // EAST
    { SOUTH, EAST, NORTH, WEST }, // SOUTH
    { WEST, SOUTH, EAST, NORTH }, // WEST
};

static void moveBlobTW(struct prng* rng, Blob* b, unsigned char upper[]) {
    int order[4] = {0, 1, 2, 3};
    twrandomp4(rng, order);

    for (int i=0; i<4; i++) {
        int dir = twturndirs[b->dir][order[i]];
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

//Extremely blobnet and route 444 specific, will need to be rewritten if different levels are tested
static int twIntro(struct prng rng, int step, unsigned char upper[]) {
    if (step == EVEN) {
        Blob blob5 = monsterListInitial[4];
        Blob blob6 = monsterListInitial[5];
    
        upper[81] = FLOOR; //extremely hardcoded index values for relevant positions
        twadvance4(&rng);
        moveBlobTW(&rng, &blob5, upper);
        moveBlobTW(&rng, &blob6, upper);
        twadvance78(&rng);
        if (upper[81] == BLOB_N || upper[80] == BLOB_N)
            return 0;
        
        moveBlobTW(&rng, &blob5, upper);
        if (upper[79] == BLOB_N)
            return 0;
        return 1;
    }
    else {
        Blob blob5 = monsterListInitial[4], blob13 = monsterListInitial[12];
        
        twadvance4(&rng);
        moveBlobTW(&rng, &blob5, upper);
        twadvance7(&rng);
        moveBlobTW(&rng, &blob13, upper);
        twadvance71(&rng);
        
        if (upper[80] == BLOB_N)
            return 0;
        
        moveBlobTW(&rng, &blob5, upper);
        twadvance7(&rng);
        moveBlobTW(&rng, &blob13, upper);
        twadvance79(&rng);
        if (upper[79] == BLOB_N)
            return 0;
        
        moveBlobTW(&rng, &blob13, upper);
        twadvance79(&rng);
        moveBlobTW(&rng, &blob13, upper);
        twadvance79(&rng);
        upper[140] = FLOOR;
        moveBlobTW(&rng, &blob13, upper);
        twadvance79(&rng);
        if (upper[140] == BLOB_N)
            return 0;
        return 1;
    }
    printf("ERROR: REACHED END OF twIntro function!\n");
    return 0;
}

static void twadvance(struct prng* rng) {
    rng->value = ((rng->value * 1103515245UL) + 12345UL) & 0x7FFFFFFFUL; //Same generator/advancement Tile World uses
}

/*
 * Advance the RNG state by 79 values all at once
*/
static void twadvance79(struct prng* rng) {
    rng->value = ((rng->value * 2441329573UL) + 2062159411UL) & 0x7FFFFFFFUL;
}

static void twadvance4(struct prng* rng) {
    rng->value = ((rng->value * 3993403153UL) + 1449466924UL) & 0x7FFFFFFFUL;
}

static void twadvance78(struct prng* rng) {
    rng->value = ((rng->value * 1450606361UL) + 45289890UL) & 0x7FFFFFFFUL;
}

static void twadvance7(struct prng* rng) {
    rng->value = ((rng->value * 456479493UL) + 1051550459UL) & 0x7FFFFFFFUL;
}

static void twadvance71(struct prng* rng) {
    rng->value = ((rng->value * 1212309509UL) + 662438587UL) & 0x7FFFFFFFUL;
}

/* Randomly permute a list of four values. Three random numbers are
 * used, with the ranges [0,1], [0,1,2], and [0,1,2,3].
 */
static void twrandomp4(struct prng* rng, int* array) {
    int	n, t;

    twadvance(rng);
    n = rng->value >> 30;
    t = array[n];  array[n] = array[1];  array[1] = t;
    n = (int)((3.0 * (rng->value & 0x0FFFFFFFUL)) / (double)0x10000000UL);
    t = array[n];  array[n] = array[2];  array[2] = t;
    n = (rng->value >> 28) & 3;
    t = array[n];  array[n] = array[3];  array[3] = t;
}

/* MSCC Search */

// current dir => turn => new direction
static const Dir msturndirs[4][4] = {
    // left, right, back
    { WEST, EAST, SOUTH }, // NORTH
    { NORTH, SOUTH, WEST }, // EAST
    { EAST, WEST, NORTH }, // SOUTH
    { SOUTH, NORTH, EAST }, // WEST
};

static void moveBlobMS(struct prng* rng, Blob* b, unsigned char upper[]) {
    int dir;
    int facedir, xdir, ydir;
    do {
        xdir = msrandn(rng, 3);
        ydir = msrandn(rng, 3);
        facedir = yxtodir[ydir][xdir];
    } while (facedir == -1);

    int index = b->index + diridx[facedir];
    if (canEnter(upper[index])) {
        goto ok;
    }

    unsigned int todo = 7;
    do {
        int turn = msrandn(rng, 3);
        if (todo & (1U<<turn)) {
            todo &= ~(1U<<turn);
            dir = msturndirs[facedir][turn];
            index = b->index + diridx[dir];
            if (canEnter(upper[index])) {
                goto ok;
            }
        }
    } while (todo);
    return;
ok:
    upper[b->index] = FLOOR;
    upper[index] = BLOB_N;
    //b->dir = dir;
    b->index = index;
    return;
}

static void msadvance(struct prng* rng) {
    rng->value = ((rng->value * 0x343FDul) + 0x269EC3ul);
}

static int msrandn(struct prng* rng, int n) {
    msadvance(rng);
    return (int)((rng->value>>16)&0x7fff) % n;
}
