/*
   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 *	Adam M Rivera
 *	With direction from: Andrew Tridgell, Jason Short, Justin Beech
 *
 *	Adapted from: http://www.societyofrobots.com/robotforum/index.php?topic=11855.0
 *	Scott Ferguson
 *	scottfromscott@gmail.com
 *
 */
#include "AP_Declination.h"

#include <cmath>

#include <AP_Common/AP_Common.h>
#include <AP_Math/AP_Math.h>

// 1 byte - 4 bits for value + 1 bit for sign + 3 bits for repeats => 8 bits
struct row_value {

    // Offset has a max value of 15
    uint8_t abs_offset : 4;

    // Sign of the offset, 0 = negative, 1 = positive
    uint8_t offset_sign : 1;

    // The highest repeat is 7
    uint8_t repeats : 3;
};

// 730 bytes
static const uint8_t exceptions[10][73] = {
    {150,145,140,135,130,125,120,115,110,105,100,95,90,85,80,75,70,65,60,55,50,45,40,35,30,25,20,15,10,5,0,4,9,14,19,24,29,34,39,44,49,54,59,64,69,74,79,84,89,94,99,104,109,114,119,124,129,134,139,144,149,154,159,164,169,174,179,175,170,165,160,155,150},
    {143,137,131,126,120,115,110,105,100,95,90,85,80,75,71,66,62,57,53,48,44,39,35,31,27,22,18,14,9,5,1,3,7,11,16,20,25,29,34,38,43,47,52,57,61,66,71,76,81,86,91,96,101,107,112,117,123,128,134,140,146,151,157,163,169,175,178,172,166,160,154,148,143},
    {130,124,118,112,107,101,96,92,87,82,78,74,70,65,61,57,54,50,46,42,38,34,31,27,23,19,16,12,8,4,1,2,6,10,14,18,22,26,30,34,38,43,47,51,56,61,65,70,75,79,84,89,94,100,105,111,116,122,128,135,141,148,155,162,170,177,174,166,159,151,144,137,130},
    {111,104,99,94,89,85,81,77,73,70,66,63,60,56,53,50,46,43,40,36,33,30,26,23,20,16,13,10,6,3,0,3,6,9,13,16,20,24,28,32,36,40,44,48,52,57,61,65,70,74,79,84,88,93,98,103,109,115,121,128,135,143,152,162,172,176,165,154,144,134,125,118,111},
    {85,81,77,74,71,68,65,63,60,58,56,53,51,49,46,43,41,38,35,32,29,26,23,19,16,13,10,7,4,1,1,3,6,9,13,16,19,23,26,30,34,38,42,46,50,54,58,62,66,70,74,78,83,87,91,95,100,105,110,117,124,133,144,159,178,160,141,125,112,103,96,90,85},
    {62,60,58,57,55,54,52,51,50,48,47,46,44,42,41,39,36,34,31,28,25,22,19,16,13,10,7,4,2,0,3,5,8,10,13,16,19,22,26,29,33,37,41,45,49,53,56,60,64,67,70,74,77,80,83,86,89,91,94,97,101,105,111,130,109,84,77,74,71,68,66,64,62},
    {46,46,45,44,44,43,42,42,41,41,40,39,38,37,36,35,33,31,28,26,23,20,16,13,10,7,4,1,1,3,5,7,9,12,14,16,19,22,26,29,33,36,40,44,48,51,55,58,61,64,66,68,71,72,74,74,75,74,72,68,61,48,25,2,22,33,40,43,45,46,47,46,46},
    {6,9,12,15,18,21,23,25,27,28,27,24,17,4,14,34,49,56,60,60,60,58,56,53,50,47,43,40,36,32,28,25,21,17,13,9,5,1,2,6,10,14,17,21,24,28,31,34,37,39,41,42,43,43,41,38,33,25,17,8,0,4,8,10,10,10,8,7,4,2,0,3,6},
    {22,24,26,28,30,32,33,31,23,18,81,96,99,98,95,93,89,86,82,78,74,70,66,62,57,53,49,44,40,36,32,27,23,19,14,10,6,1,2,6,10,15,19,23,27,31,35,38,42,45,49,52,55,57,60,61,63,63,62,61,57,53,47,40,33,28,23,21,19,19,19,20,22},
    {168,173,178,176,171,166,161,156,151,146,141,136,131,126,121,116,111,106,101,96,91,86,81,76,71,66,61,56,51,46,41,36,31,26,21,16,11,6,1,3,8,13,18,23,28,33,38,43,48,53,58,63,68,73,78,83,88,93,98,103,108,113,118,123,128,133,138,143,148,153,158,163,168}
};

// 100 bytes
static const uint8_t exception_signs[10][10] = {
    {0,0,0,1,255,255,224,0,0,0},
    {0,0,0,1,255,255,240,0,0,0},
    {0,0,0,1,255,255,248,0,0,0},
    {0,0,0,1,255,255,254,0,0,0},
    {0,0,0,3,255,255,255,0,0,0},
    {0,0,0,3,255,255,255,240,0,0},
    {0,0,0,15,255,255,255,254,0,0},
    {0,3,255,255,252,0,0,7,252,0},
    {0,127,255,255,252,0,0,0,0,0},
    {0,0,31,255,254,0,0,0,0,0}
};

// 76 bytes
static const uint8_t declination_keys[2][37] = {
// Row start values
    {36,30,25,21,18,16,14,12,11,10,9,9,9,8,8,8,7,6,6,5,4,4,4,3,4,4,4},
// Row length values
    {39,38,33,35,37,35,37,36,39,34,41,42,42,28,39,40,43,51,50,39,37,34,44,51,49,48,55}
};

// 1056 total values @ 1 byte each = 1056 bytes
static const row_value declination_values[] = {
    {0,0,4},{1,1,0},{0,0,2},{1,1,0},{0,0,2},{1,1,3},{2,1,1},{3,1,3},{4,1,1},{3,1,1},{2,1,1},{3,1,0},{2,1,0},{1,1,0},{2,1,1},{1,1,0},{2,1,0},{3,1,4},{4,1,1},{3,1,0},{4,1,0},{3,1,2},{2,1,2},{1,1,1},{0,0,0},{1,0,1},{3,0,0},{4,0,0},{6,0,0},{8,0,0},{11,0,0},{13,0,1},{10,0,0},{9,0,0},{7,0,0},{5,0,0},{4,0,0},{2,0,0},{1,0,2},
    {0,0,6},{1,1,0},{0,0,6},{1,1,2},{2,1,0},{3,1,2},{4,1,2},{3,1,3},{2,1,0},{1,1,0},{2,1,0},{1,1,2},{2,1,2},{3,1,3},{4,1,0},{3,1,3},{2,1,1},{1,1,1},{0,0,0},{1,0,1},{2,0,0},{4,0,0},{5,0,0},{6,0,0},{7,0,0},{8,0,0},{9,0,0},{8,0,0},{6,0,0},{7,0,0},{6,0,0},{4,0,1},{3,0,0},{2,0,0},{1,0,0},{2,0,0},{0,0,0},{1,0,0},
    {0,0,1},{1,0,0},{0,0,1},{1,1,0},{0,0,6},{1,0,0},{1,1,0},{0,0,0},{1,1,1},{2,1,1},{3,1,0},{4,1,3},{3,1,0},{4,1,0},{3,1,1},{2,1,0},{1,1,7},{2,1,0},{3,1,6},{2,1,0},{1,1,2},{0,0,0},{1,0,0},{2,0,0},{3,0,1},{5,0,1},{6,0,0},{7,0,0},{6,0,2},{4,0,2},{3,0,1},{2,0,2},{1,0,1},
    {0,0,0},{1,0,0},{0,0,7},{0,0,5},{1,1,1},{2,1,1},{3,1,0},{4,1,5},{3,1,1},{1,1,0},{2,1,0},{1,1,0},{0,0,0},{1,1,0},{0,0,1},{1,1,0},{0,0,0},{2,1,2},{3,1,1},{2,1,0},{3,1,0},{2,1,1},{1,1,0},{0,0,1},{1,0,0},{2,0,1},{4,0,1},{5,0,4},{4,0,0},{3,0,1},{4,0,0},{2,0,0},{3,0,0},{2,0,2},{1,0,2},
    {0,0,0},{1,0,0},{0,0,7},{0,0,5},{1,1,2},{2,1,0},{4,1,0},{3,1,0},{5,1,0},{3,1,0},{5,1,0},{4,1,1},{3,1,0},{2,1,1},{1,1,2},{0,0,2},{1,0,0},{0,0,1},{1,1,0},{2,1,2},{3,1,0},{2,1,1},{1,1,1},{0,0,0},{1,0,0},{2,0,1},{3,0,1},{4,0,0},{5,0,0},{4,0,0},{5,0,0},{4,0,0},{3,0,1},{1,0,0},{3,0,0},{2,0,4},{1,0,3},
    {0,0,1},{1,0,0},{0,0,7},{1,1,0},{0,0,4},{1,1,0},{2,1,1},{3,1,0},{4,1,2},{5,1,0},{4,1,0},{3,1,1},{2,1,1},{1,1,1},{0,0,2},{1,0,1},{2,0,0},{1,0,0},{0,0,0},{1,1,1},{2,1,3},{1,1,1},{1,0,2},{2,0,0},{3,0,1},{4,0,2},{3,0,1},{2,0,0},{1,0,0},{2,0,1},{1,0,0},{2,0,1},{1,0,0},{2,0,0},{1,0,3},
    {0,0,2},{1,0,0},{0,0,5},{1,1,0},{0,0,4},{1,1,2},{2,1,0},{4,1,0},{3,1,0},{4,1,1},{5,1,0},{4,1,0},{3,1,1},{2,1,0},{1,1,1},{0,0,2},{1,0,0},{2,0,0},{1,0,0},{3,0,0},{2,0,0},{1,0,0},{0,0,1},{2,1,2},{1,1,0},{2,1,0},{0,0,1},{1,0,1},{2,0,1},{3,0,2},{4,0,0},{2,0,1},{1,0,2},{2,0,0},{1,0,1},{2,0,0},{1,0,5},
    {0,0,0},{1,0,0},{0,0,7},{0,0,1},{1,1,0},{0,0,2},{1,1,2},{3,1,2},{4,1,3},{3,1,0},{2,1,1},{1,1,0},{0,0,2},{1,0,1},{2,0,0},{3,0,0},{2,0,0},{3,0,0},{2,0,0},{1,0,0},{0,0,0},{1,1,0},{2,1,0},{1,1,0},{2,1,1},{0,0,0},{1,1,0},{1,0,2},{2,0,1},{3,0,1},{2,0,1},{1,0,1},{0,0,0},{1,0,2},{2,0,0},{1,0,5},
    {0,0,4},{1,0,0},{0,0,3},{1,1,0},{0,0,3},{1,1,0},{0,0,0},{1,1,0},{2,1,1},{3,1,1},{4,1,3},{3,1,0},{2,1,0},{1,1,0},{0,0,2},{1,0,0},{2,0,3},{3,0,0},{2,0,0},{3,0,0},{1,0,1},{1,1,1},{2,1,0},{1,1,0},{2,1,0},{1,1,0},{0,0,2},{1,0,0},{2,0,0},{1,0,0},{2,0,0},{3,0,0},{2,0,0},{1,0,0},{0,0,0},{1,0,0},{0,0,0},{1,0,7},{1,0,1},
    {0,0,7},{0,0,5},{1,1,0},{0,0,1},{2,1,0},{1,1,0},{3,1,3},{4,1,1},{3,1,1},{1,1,1},{0,0,1},{1,0,0},{2,0,3},{3,0,0},{2,0,3},{0,0,2},{2,1,0},{1,1,0},{2,1,0},{1,1,0},{0,0,0},{1,1,0},{1,0,0},{0,0,0},{1,0,0},{2,0,0},{1,0,0},{2,0,1},{0,0,0},{1,0,0},{0,0,1},{1,0,0},{0,0,0},{1,0,7},
    {0,0,6},{1,0,0},{0,0,0},{1,1,0},{0,0,4},{1,1,0},{0,0,0},{2,1,0},{1,1,0},{3,1,0},{2,1,0},{4,1,0},{3,1,0},{4,1,1},{2,1,2},{0,0,1},{1,0,0},{2,0,7},{2,0,0},{1,0,1},{0,0,1},{1,1,1},{2,1,0},{1,1,0},{0,0,0},{1,1,0},{0,0,0},{1,0,0},{0,0,0},{1,0,1},{2,0,0},{1,0,0},{0,0,0},{1,0,0},{0,0,2},{1,0,1},{0,0,0},{2,0,0},{1,0,2},{0,0,0},{1,0,0},
    {0,0,7},{0,0,3},{1,1,0},{0,0,2},{1,1,0},{2,1,0},{1,1,0},{3,1,0},{2,1,0},{4,1,0},{3,1,0},{4,1,0},{3,1,0},{2,1,1},{1,1,0},{0,0,0},{1,0,1},{2,0,1},{3,0,0},{2,0,2},{1,0,0},{2,0,0},{1,0,1},{0,0,0},{1,0,0},{0,0,0},{1,1,0},{0,0,0},{2,1,0},{1,1,0},{0,0,0},{1,1,0},{0,0,1},{1,0,0},{0,0,0},{1,0,2},{0,0,3},{1,0,0},{0,0,0},{1,0,6},{0,0,0},{1,0,0},
    {0,0,2},{1,1,0},{0,0,1},{1,0,0},{0,0,3},{1,1,0},{0,0,2},{1,1,2},{2,1,0},{3,1,0},{2,1,0},{3,1,0},{4,1,0},{3,1,1},{2,1,0},{1,1,1},{0,0,0},{1,0,0},{2,0,2},{3,0,0},{2,0,1},{1,0,0},{2,0,0},{1,0,1},{0,0,0},{1,0,0},{0,0,2},{1,1,0},{0,0,0},{1,1,1},{0,0,0},{1,1,0},{0,0,0},{1,0,0},{0,0,0},{1,0,0},{0,0,0},{1,0,0},{0,0,5},{1,0,7},{0,0,0},{1,0,0},
    {0,0,5},{1,0,0},{0,0,4},{1,1,0},{0,0,1},{1,1,1},{2,1,2},{3,1,4},{2,1,0},{1,1,0},{0,0,0},{1,0,1},{2,0,6},{1,0,1},{0,0,0},{1,0,1},{0,0,2},{1,1,1},{0,0,0},{1,1,0},{0,0,1},{1,1,0},{0,0,0},{1,0,0},{0,0,0},{1,0,0},{0,0,7},{1,0,7},
    {0,0,3},{1,0,0},{0,0,7},{1,1,0},{0,0,0},{1,1,0},{2,1,3},{3,1,3},{2,1,0},{1,1,1},{0,0,0},{1,0,1},{2,0,2},{3,0,0},{1,0,0},{2,0,0},{1,0,0},{2,0,0},{0,0,1},{1,0,1},{0,0,2},{1,1,0},{0,0,0},{1,1,0},{0,0,1},{1,1,0},{0,0,3},{1,0,0},{0,0,2},{1,1,0},{0,0,3},{1,0,0},{0,0,0},{1,0,0},{2,0,0},{1,0,1},{2,0,0},{0,0,0},{1,0,0},
    {0,0,1},{1,0,0},{0,0,2},{1,0,0},{0,0,5},{1,1,2},{2,1,1},{3,1,0},{2,1,0},{3,1,2},{2,1,1},{1,1,0},{0,0,1},{1,0,0},{2,0,0},{1,0,0},{2,0,4},{1,0,1},{0,0,0},{1,0,1},{0,0,0},{1,0,0},{0,0,0},{1,1,0},{0,0,0},{1,1,1},{0,0,7},{0,0,0},{1,1,0},{0,0,0},{1,1,0},{0,0,3},{1,0,1},{0,0,0},{1,0,0},{2,0,0},{1,0,0},{2,0,0},{1,0,0},{1,0,0},
    {0,0,0},{1,0,1},{0,0,1},{1,0,0},{0,0,0},{1,0,0},{0,0,3},{1,1,0},{0,0,0},{1,1,0},{2,1,2},{3,1,0},{2,1,0},{4,1,0},{3,1,0},{2,1,2},{1,1,0},{0,0,0},{1,0,2},{2,0,4},{1,0,0},{2,0,0},{0,0,0},{1,0,0},{0,0,0},{1,0,1},{0,0,2},{1,1,0},{0,0,0},{1,1,0},{0,0,0},{1,1,0},{0,0,5},{1,1,0},{0,0,0},{1,1,1},{0,0,0},{1,1,0},{0,0,1},{1,0,4},{2,0,1},{1,0,0},{1,0,0},
    {0,0,0},{2,0,0},{1,0,0},{0,0,0},{1,0,1},{0,0,0},{1,0,0},{0,0,3},{1,1,0},{0,0,0},{2,1,2},{3,1,0},{2,1,0},{3,1,0},{4,1,0},{3,1,0},{2,1,1},{1,1,1},{1,0,0},{0,0,0},{2,0,0},{1,0,0},{2,0,1},{1,0,0},{2,0,2},{1,0,0},{0,0,0},{1,0,1},{0,0,0},{1,0,0},{0,0,0},{1,0,0},{1,1,0},{0,0,1},{1,1,0},{0,0,0},{1,1,0},{0,0,1},{1,1,0},{0,0,2},{1,1,3},{0,0,0},{1,1,0},{0,0,2},{1,0,0},{2,0,0},{1,0,1},{2,0,0},{1,0,0},{2,0,0},{1,0,0},
    {0,0,0},{1,0,1},{2,0,0},{1,0,1},{0,0,0},{1,0,0},{0,0,0},{1,0,0},{0,0,0},{1,1,0},{0,0,0},{2,1,0},{1,1,0},{2,1,0},{3,1,1},{2,1,0},{4,1,1},{3,1,0},{2,1,1},{1,1,0},{0,0,1},{1,0,0},{2,0,0},{1,0,0},{2,0,2},{1,0,0},{2,0,1},{1,0,0},{0,0,0},{1,0,2},{0,0,0},{1,0,0},{0,0,3},{1,1,0},{0,0,1},{1,1,0},{0,0,0},{1,1,0},{0,0,0},{1,1,0},{0,0,0},{1,1,2},{2,1,0},{1,1,1},{0,0,1},{1,0,3},{2,0,0},{1,0,0},{2,0,1},{2,0,0},
    {0,0,0},{2,0,0},{1,0,0},{2,0,0},{1,0,4},{0,0,1},{1,1,0},{0,0,0},{2,1,0},{1,1,0},{3,1,3},{4,1,1},{3,1,0},{2,1,1},{1,1,0},{0,0,0},{1,0,2},{2,0,0},{1,0,0},{2,0,4},{1,0,0},{0,0,0},{1,0,3},{0,0,0},{1,0,0},{0,0,4},{1,1,0},{0,0,0},{1,1,0},{0,0,0},{1,1,4},{2,1,1},{1,1,1},{0,0,2},{1,0,1},{2,0,2},{1,0,0},{2,0,0},{2,0,0},
    {0,0,0},{2,0,3},{1,0,3},{0,0,2},{1,1,0},{2,1,2},{4,1,0},{3,1,0},{4,1,2},{3,1,1},{1,1,1},{0,0,0},{1,0,2},{2,0,4},{1,0,0},{2,0,1},{0,0,0},{1,0,0},{2,0,0},{0,0,0},{1,0,2},{0,0,0},{1,0,0},{0,0,3},{1,1,4},{2,1,0},{1,1,0},{2,1,2},{1,1,2},{0,0,1},{1,0,0},{2,0,1},{1,0,0},{3,0,0},{1,0,0},{2,0,0},{2,0,0},
    {0,0,0},{2,0,4},{1,0,3},{0,0,0},{1,1,2},{3,1,1},{4,1,2},{5,1,0},{4,1,0},{3,1,1},{1,1,1},{0,0,0},{1,0,1},{2,0,0},{1,0,0},{2,0,1},{3,0,0},{2,0,2},{1,0,2},{2,0,0},{1,0,5},{0,0,4},{1,1,1},{2,1,4},{3,1,0},{2,1,1},{1,1,1},{0,0,0},{1,0,2},{2,0,1},{3,0,0},{2,0,0},{1,0,0},{3,0,0},
    {0,0,0},{2,0,1},{3,0,0},{2,0,1},{1,0,0},{2,0,0},{1,0,0},{0,0,2},{1,1,0},{2,1,0},{3,1,1},{5,1,4},{3,1,1},{1,1,1},{1,0,0},{0,0,0},{2,0,1},{1,0,0},{3,0,0},{2,0,2},{3,0,0},{2,0,1},{1,0,1},{2,0,1},{1,0,0},{2,0,0},{1,0,3},{0,0,0},{1,0,0},{1,1,0},{0,0,0},{1,1,0},{2,1,2},{3,1,0},{2,1,0},{3,1,2},{2,1,0},{1,1,1},{0,0,0},{1,0,2},{2,0,1},{3,0,0},{2,0,1},{3,0,0},
    {0,0,0},{3,0,1},{2,0,0},{3,0,0},{2,0,0},{1,0,0},{2,0,0},{1,0,1},{0,0,1},{2,1,1},{3,1,0},{4,1,0},{6,1,0},{5,1,0},{7,1,0},{6,1,0},{5,1,0},{3,1,1},{1,1,0},{0,0,1},{1,0,0},{2,0,3},{3,0,0},{2,0,0},{3,0,0},{2,0,0},{3,0,0},{2,0,1},{1,0,0},{2,0,5},{1,0,2},{0,0,2},{1,1,0},{2,1,0},{3,1,2},{4,1,0},{3,1,0},{4,1,0},{3,1,0},{2,1,1},{1,1,0},{1,0,0},{0,0,0},{2,0,0},{1,0,0},{2,0,0},{3,0,0},{2,0,0},{3,0,0},{2,0,0},{2,0,0},
    {0,0,0},{2,0,0},{3,0,1},{2,0,0},{3,0,0},{2,0,1},{1,0,1},{0,0,1},{2,1,1},{4,1,0},{6,1,0},{7,1,1},{8,1,0},{7,1,0},{5,1,0},{3,1,0},{2,1,0},{1,1,0},{0,0,0},{1,0,1},{2,0,1},{3,0,0},{2,0,0},{3,0,2},{2,0,0},{3,0,2},{1,0,0},{3,0,0},{2,0,0},{3,0,0},{2,0,4},{1,0,1},{0,0,1},{1,1,0},{2,1,0},{3,1,0},{4,1,0},{5,1,0},{4,1,1},{5,1,0},{4,1,0},{2,1,1},{1,1,0},{0,0,0},{1,0,0},{2,0,3},{3,0,1},{2,0,0},{3,0,0},
    {0,0,0},{3,0,2},{2,0,0},{3,0,0},{2,0,2},{1,0,0},{0,0,1},{2,1,0},{3,1,0},{5,1,0},{8,1,0},{9,1,0},{10,1,1},{7,1,0},{5,1,0},{3,1,0},{1,1,0},{0,0,0},{1,0,1},{2,0,0},{3,0,0},{2,0,0},{3,0,3},{4,0,0},{3,0,7},{2,0,0},{3,0,0},{2,0,0},{3,0,0},{2,0,0},{1,0,0},{2,0,0},{0,0,2},{2,1,0},{3,1,0},{4,1,0},{5,1,0},{7,1,0},{5,1,0},{6,1,0},{4,1,1},{2,1,0},{0,0,1},{1,0,1},{2,0,1},{3,0,2},{2,0,0},{3,0,0},
    {0,0,0},{3,0,5},{2,0,1},{1,0,0},{0,0,0},{1,1,0},{2,1,0},{5,1,0},{8,1,0},{12,1,0},{14,1,0},{13,1,0},{9,1,0},{6,1,0},{3,1,0},{1,1,0},{0,0,0},{2,0,0},{1,0,0},{3,0,0},{2,0,0},{3,0,0},{4,0,0},{3,0,1},{4,0,0},{3,0,0},{4,0,1},{3,0,0},{4,0,0},{3,0,2},{4,0,0},{3,0,1},{4,0,0},{3,0,0},{2,0,0},{3,0,0},{2,0,2},{0,0,1},{1,1,0},{2,1,0},{4,1,0},{5,1,0},{7,1,0},{8,1,0},{6,1,1},{5,1,0},{3,1,0},{1,1,1},{1,0,1},{2,0,0},{3,0,0},{2,0,0},{3,0,1},{2,0,0},{3,0,0},
};

float
AP_Declination::get_declination(float lat, float lon)
{
    int16_t decSW, decSE, decNW, decNE, lonmin, latmin;
    uint8_t latmin_index,lonmin_index;
    float decmin, decmax;

    // Constrain to valid inputs
    lat = constrain_float(lat, -90, 90);
    lon = constrain_float(lon, -180, 180);

    latmin = floorf(lat/5)*5;
    lonmin = floorf(lon/5)*5;

    latmin_index= (90+latmin)/5;
    lonmin_index= (180+lonmin)/5;

    decSW = get_lookup_value(latmin_index, lonmin_index);
    decSE = get_lookup_value(latmin_index, lonmin_index+1);
    decNE = get_lookup_value(latmin_index+1, lonmin_index+1);
    decNW = get_lookup_value(latmin_index+1, lonmin_index);

    /* approximate declination within the grid using bilinear interpolation */
    decmin = (lon - lonmin) / 5 * (decSE - decSW) + decSW;
    decmax = (lon - lonmin) / 5 * (decNE - decNW) + decNW;
    return (lat - latmin) / 5 * (decmax - decmin) + decmin;
}

int16_t
AP_Declination::get_lookup_value(uint8_t x, uint8_t y)
{
    // return value
    int16_t val = 0;

    // These are exception indices
    if(x <= 6 || x >= 34)
    {
        // If the x index is in the upper range we need to translate it
        // to match the 10 indices in the exceptions lookup table
        if(x >= 34) x -= 27;

        // Read the unsigned value from the array
        val = exceptions[x][y];

        // Read the 8 bit compressed sign values
        uint8_t sign = exception_signs[x][y/8];

        // Check the sign bit for this index
        if(sign & (0x80 >> y%8))
            val = -val;

        return val;
    }

    // Because the values were removed from the start of the
    // original array (0-6) to the exception array, all the indices
    // in this main lookup need to be shifted left 7
    // EX: User enters 7 -> 7 is the first row in this array so it needs to be zero
    if(x >= 7) x -= 7;

    // If we are looking for the first value we can just use the
    // row start value from declination_keys
    if (y == 0) {
        return declination_keys[0][x];
    }

    // Init vars
    row_value stval;
    int16_t offset = 0;

    // These will never exceed the second dimension length of 73
    uint8_t current_virtual_index = 0, r;

    // This could be the length of the array or less (1075 or less)
    uint16_t start_index = 0, i;

    // Init value to row start
    val = declination_keys[0][x];

    // Find the first element in the 1D array
    // that corresponds with the target row
    for(i = 0; i < x; i++) {
        start_index += declination_keys[1][i];
    }

    // Traverse the row until we find our value
    for(i = start_index; i < (start_index + declination_keys[1][x]) && current_virtual_index <= y; i++) {

        // Pull out the row_value struct
        memcpy((void*) &stval, (const char *)&declination_values[i], sizeof(struct row_value));

        // Pull the first offset and determine sign
        offset = stval.abs_offset;
        offset = (stval.offset_sign == 1) ? -offset : offset;

        // Add offset for each repeat
        // This will at least run once for zero repeat
        for(r = 0; r <= stval.repeats && current_virtual_index <= y; r++) {
            val += offset;
            current_virtual_index++;
        }
    }
    return val;
}
