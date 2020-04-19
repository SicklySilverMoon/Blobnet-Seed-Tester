#include "random.h"

void nextvalue(unsigned long* currentValue)
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
void randomp4(unsigned long* currentValue, int* array)
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