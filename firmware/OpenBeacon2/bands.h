/*
 * bands.h
 *
 *  Created on: 31 January 2016
 *      Author: Jason Milldrum
 *     Company: Etherkit
 *
 *     Copyright (c) 2016, Jason Milldrum
 *     All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without modification,
 *  are permitted provided that the following conditions are met:
 *
 *  - Redistributions of source code must retain the above copyright notice, this list
 *  of conditions and the following disclaimer.
 *
 *  - Redistributions in binary form must reproduce the above copyright notice, this list
 *  of conditions and the following disclaimer in the documentation and/or other
 *  materials provided with the distribution.
 *
 *  - Neither the name of Etherkit nor the names of its contributors may be
 *  used to endorse or promote products derived from this software without specific
 *  prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
 *  EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 *  OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT
 *  SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 *  TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 *  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef BANDS_H_
#define BANDS_H_

#define BAND_COUNT            11

typedef struct bands
{
    char name[10];
    uint32_t lower_limit;
    uint32_t upper_limit;
    uint32_t wspr;
} BandTable;

const BandTable band_table[] PROGMEM = {
  {"160 m", 1800000UL, 2000000UL, 1838100UL},
  {"80 m", 3500000UL, 4000000UL, 3594100UL},
  {"40 m", 7000000UL, 7300000UL, 7040100UL},
  {"30 m", 10100000UL, 10150000UL, 10140200UL},
  {"20 m", 14000000UL, 14350000UL, 14097100UL},
  {"17 m", 18068000UL, 18168000UL, 18106100UL},
  {"15 m", 21000000UL, 21450000UL, 21096100UL},
  {"12 m", 24890000UL, 24990000UL, 24926100UL},
  {"10 m", 28000000UL, 29700000UL, 28126100UL},
  {"6 m", 50000000UL, 54000000UL, 50294500UL},
  {"2 m", 144000000UL, 148000000UL, 144490500UL}
};
#endif /* BANDS_H_ */
