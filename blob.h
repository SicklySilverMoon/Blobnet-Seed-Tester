typedef struct BLOB {
    int index;
    char dir;
} BLOB;

void moveBlob(unsigned long* seed, BLOB* b, unsigned char upper[]);