#include "direction.h"

char left(char dir) {
    switch (dir) {
        case (MOVE_DOWN):
            return MOVE_RIGHT;
        case (MOVE_UP):
            return MOVE_LEFT;
        case (MOVE_RIGHT):
            return MOVE_UP;
        case (MOVE_LEFT):
            return MOVE_DOWN;
        default:
            return 0;
    }
}
char back(char dir) {
    switch (dir) {
        case (MOVE_DOWN):
            return MOVE_UP;
        case (MOVE_UP):
            return MOVE_DOWN;
        case (MOVE_RIGHT):
            return MOVE_LEFT;
        case (MOVE_LEFT):
            return MOVE_RIGHT;
        default:
            return 0;
    }
}

char right(char dir) {
    switch (dir) {
        case (MOVE_DOWN):
            return MOVE_LEFT;
        case (MOVE_UP):
            return MOVE_RIGHT;
        case (MOVE_RIGHT):
            return MOVE_DOWN;
        case (MOVE_LEFT):
            return MOVE_UP;
        default:
            return 0;
    }
}