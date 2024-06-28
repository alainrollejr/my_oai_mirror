/*
 * Licensed to the OpenAirInterface (OAI) Software Alliance under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The OpenAirInterface Software Alliance licenses this file to You under
 * the OAI Public License, Version 1.1  (the "License"); you may not use this file
 * except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.openairinterface.org/?page_id=698
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *-------------------------------------------------------------------------------
 * For more information about the OpenAirInterface (OAI) Software Alliance:
 *      contact@openairinterface.org
 */

/* \file vars.h
 * \brief MAC Layer variables
 * \author R. Knopp, K.H. HSU
 * \date 2018
 * \version 0.1
 * \company Eurecom / NTUST
 * \email: knopp@eurecom.fr, kai-hsiang.hsu@eurecom.fr
 * \note
 * \warning
 */

#include <stdint.h>

// table_7_3_1_1_2_2_3_4_5 contains values for number of layers and precoding information for tables 7.3.1.1.2-2/3/4/5 from TS 38.212 subclause 7.3.1.1.2
// the first 6 columns contain table 7.3.1.1.2-2: Precoding information and number of layers, for 4 antenna ports, if transformPrecoder=disabled and maxRank = 2 or 3 or 4
// next six columns contain table 7.3.1.1.2-3: Precoding information and number of layers for 4 antenna ports, if transformPrecoder= enabled, or if transformPrecoder=disabled and maxRank = 1
// next four columns contain table 7.3.1.1.2-4: Precoding information and number of layers, for 2 antenna ports, if transformPrecoder=disabled and maxRank = 2
// next four columns contain table 7.3.1.1.2-5: Precoding information and number of layers, for 2 antenna ports, if transformPrecoder= enabled, or if transformPrecoder= disabled and maxRank = 1
const uint8_t table_7_3_1_1_2_2_3_4_5[64][20] = {
  {1,  0,  1,  0,  1,  0,  1,  0,  1,  0,  1,  0,  1,  0,  1,  0,  1,  0,  1,  0},
  {1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1},
  {1,  2,  1,  2,  1,  2,  1,  2,  1,  2,  1,  2,  2,  0,  2,  0,  1,  2,  0,  0},
  {1,  3,  1,  3,  1,  3,  1,  3,  1,  3,  1,  3,  1,  2,  0,  0,  1,  3,  0,  0},
  {2,  0,  2,  0,  2,  0,  1,  4,  1,  4,  0,  0,  1,  3,  0,  0,  1,  4,  0,  0},
  {2,  1,  2,  1,  2,  1,  1,  5,  1,  5,  0,  0,  1,  4,  0,  0,  1,  5,  0,  0},
  {2,  2,  2,  2,  2,  2,  1,  6,  1,  6,  0,  0,  1,  5,  0,  0,  0,  0,  0,  0},
  {2,  3,  2,  3,  2,  3,  1,  7,  1,  7,  0,  0,  2,  1,  0,  0,  0,  0,  0,  0},
  {2,  4,  2,  4,  2,  4,  1,  8,  1,  8,  0,  0,  2,  2,  0,  0,  0,  0,  0,  0},
  {2,  5,  2,  5,  2,  5,  1,  9,  1,  9,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
  {3,  0,  3,  0,  3,  0,  1,  10, 1,  10, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
  {4,  0,  4,  0,  4,  0,  1,  11, 1,  11, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
  {1,  4,  1,  4,  0,  0,  1,  12, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
  {1,  5,  1,  5,  0,  0,  1,  13, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
  {1,  6,  1,  6,  0,  0,  1,  14, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
  {1,  7,  1,  7,  0,  0,  1,  15, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
  {1,  8,  1,  8,  0,  0,  1,  16, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
  {1,  9,  1,  9,  0,  0,  1,  17, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
  {1,  10, 1,  10, 0,  0,  1,  18, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
  {1,  11, 1,  11, 0,  0,  1,  19, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
  {2,  6,  2,  6,  0,  0,  1,  20, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
  {2,  7,  2,  7,  0,  0,  1,  21, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
  {2,  8,  2,  8,  0,  0,  1,  22, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
  {2,  9,  2,  9,  0,  0,  1,  23, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
  {2,  10, 2,  10, 0,  0,  1,  24, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
  {2,  11, 2,  11, 0,  0,  1,  25, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
  {2,  12, 2,  12, 0,  0,  1,  26, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
  {2,  13, 2,  13, 0,  0,  1,  27, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
  {3,  1,  3,  1,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
  {3,  2,  3,  2,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
  {4,  1,  4,  1,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
  {4,  2,  4,  2,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
  {1,  12, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
  {1,  13, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
  {1,  14, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
  {1,  15, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
  {1,  16, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
  {1,  17, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
  {1,  18, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
  {1,  19, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
  {1,  20, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
  {1,  21, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
  {1,  22, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
  {1,  23, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
  {1,  24, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
  {1,  25, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
  {1,  26, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
  {1,  27, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
  {2,  14, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
  {2,  15, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
  {2,  16, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
  {2,  17, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
  {2,  18, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
  {2,  19, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
  {2,  20, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
  {2,  21, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
  {3,  3,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
  {3,  4,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
  {3,  5,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
  {3,  6,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
  {4,  3,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
  {4,  4,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
  {0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
  {0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0}
};

const uint8_t table_7_3_1_1_2_12[14][3] = {
  {1,0,1},
  {1,1,1},
  {2,0,1},
  {2,1,1},
  {2,2,1},
  {2,3,1},
  {2,0,2},
  {2,1,2},
  {2,2,2},
  {2,3,2},
  {2,4,2},
  {2,5,2},
  {2,6,2},
  {2,7,2}
};

const uint8_t table_7_3_1_1_2_13[10][4] = {
  {1,0,1,1},
  {2,0,1,1},
  {2,2,3,1},
  {2,0,2,1},
  {2,0,1,2},
  {2,2,3,2},
  {2,4,5,2},
  {2,6,7,2},
  {2,0,4,2},
  {2,2,6,2}
};

const uint8_t table_7_3_1_1_2_14[3][5] = {
  {2,0,1,2,1},
  {2,0,1,4,2},
  {2,2,3,6,2}
};

const uint8_t table_7_3_1_1_2_15[4][6] = {
  {2,0,1,2,3,1},
  {2,0,1,4,5,2},
  {2,2,3,6,7,2},
  {2,0,2,4,6,2}
};

const uint8_t table_7_3_1_1_2_16[12][2] = {
  {1,0},
  {1,1},
  {2,0},
  {2,1},
  {2,2},
  {2,3},
  {3,0},
  {3,1},
  {3,2},
  {3,3},
  {3,4},
  {3,5}
};

const uint8_t table_7_3_1_1_2_17[7][3] = {
  {1,0,1},
  {2,0,1},
  {2,2,3},
  {3,0,1},
  {3,2,3},
  {3,4,5},
  {2,0,2}
};

const uint8_t table_7_3_1_1_2_18[3][4] = {
  {2,0,1,2},
  {3,0,1,2},
  {3,3,4,5}
};

const uint8_t table_7_3_1_1_2_19[2][5] = {
  {2,0,1,2,3},
  {3,0,1,2,3}
};

const uint8_t table_7_3_1_1_2_20[28][3] = {
  {1,0,1},
  {1,1,1},
  {2,0,1},
  {2,1,1},
  {2,2,1},
  {2,3,1},
  {3,0,1},
  {3,1,1},
  {3,2,1},
  {3,3,1},
  {3,4,1},
  {3,5,1},
  {3,0,2},
  {3,1,2},
  {3,2,2},
  {3,3,2},
  {3,4,2},
  {3,5,2},
  {3,6,2},
  {3,7,2},
  {3,8,2},
  {3,9,2},
  {3,10,2},
  {3,11,2},
  {1,0,2},
  {1,1,2},
  {1,6,2},
  {1,7,2}
};

const uint8_t table_7_3_1_1_2_21[19][4] = {
  {1,0,1,1},
  {2,0,1,1},
  {2,2,3,1},
  {3,0,1,1},
  {3,2,3,1},
  {3,4,5,1},
  {2,0,2,1},
  {3,0,1,2},
  {3,2,3,2},
  {3,4,5,2},
  {3,6,7,2},
  {3,8,9,2},
  {3,10,11,2},
  {1,0,1,2},
  {1,6,7,2},
  {2,0,1,2},
  {2,2,3,2},
  {2,6,7,2},
  {2,8,9,2}
};

const uint8_t table_7_3_1_1_2_22[6][5] = {
  {2,0,1,2,1},
  {3,0,1,2,1},
  {3,3,4,5,1},
  {3,0,1,6,2},
  {3,2,3,8,2},
  {3,4,5,10,2}
};

const uint8_t table_7_3_1_1_2_23[5][6] = {
  {2,0,1,2,3,1},
  {3,0,1,2,3,1},
  {3,0,1,6,7,2},
  {3,2,3,8,9,2},
  {3,4,5,10,11,2}
};

const uint8_t table_7_3_2_3_3_1[12][5] = {
  {1,1,0,0,0},
  {1,0,1,0,0},
  {1,1,1,0,0},
  {2,1,0,0,0},
  {2,0,1,0,0},
  {2,0,0,1,0},
  {2,0,0,0,1},
  {2,1,1,0,0},
  {2,0,0,1,1},
  {2,1,1,1,0},
  {2,1,1,1,1},
  {2,1,0,1,0}
};

const uint8_t table_7_3_2_3_3_2_oneCodeword[31][10] = {
  {1,1,0,0,0,0,0,0,0,1},
  {1,0,1,0,0,0,0,0,0,1},
  {1,1,1,0,0,0,0,0,0,1},
  {2,1,0,0,0,0,0,0,0,1},
  {2,0,1,0,0,0,0,0,0,1},
  {2,0,0,1,0,0,0,0,0,1},
  {2,0,0,0,1,0,0,0,0,1},
  {2,1,1,0,0,0,0,0,0,1},
  {2,0,0,1,1,0,0,0,0,1},
  {2,1,1,1,0,0,0,0,0,1},
  {2,1,1,1,1,0,0,0,0,1},
  {2,1,0,1,0,0,0,0,0,1},
  {2,1,0,0,0,0,0,0,0,2},
  {2,0,1,0,0,0,0,0,0,2},
  {2,0,0,1,0,0,0,0,0,2},
  {2,0,0,0,1,0,0,0,0,2},
  {2,0,0,0,0,1,0,0,0,2},
  {2,0,0,0,0,0,1,0,0,2},
  {2,0,0,0,0,0,0,1,0,2},
  {2,0,0,0,0,0,0,0,1,2},
  {2,1,1,0,0,0,0,0,0,2},
  {2,0,0,1,1,0,0,0,0,2},
  {2,0,0,0,0,1,1,0,0,2},
  {2,0,0,0,0,0,0,1,1,2},
  {2,1,0,0,0,1,0,0,0,2},
  {2,0,0,1,0,0,0,1,0,2},
  {2,1,1,0,0,1,0,0,0,2},
  {2,0,0,1,1,0,0,1,0,2},
  {2,1,1,0,0,1,1,0,0,2},
  {2,0,0,1,1,0,0,1,1,2},
  {2,1,0,1,0,1,0,1,0,2}
};

const uint8_t table_7_3_2_3_3_2_twoCodeword[4][10] = {
  {2,1,1,1,1,1,0,0,0,2},
  {2,1,1,1,1,1,0,1,0,2},
  {2,1,1,1,1,1,1,1,0,2},
  {2,1,1,1,1,1,1,1,1,2}
};

const uint8_t table_7_3_2_3_3_3_oneCodeword[24][7] = {
  {1,1,0,0,0,0,0},
  {1,0,1,0,0,0,0},
  {1,1,1,0,0,0,0},
  {2,1,0,0,0,0,0},
  {2,0,1,0,0,0,0},
  {2,0,0,1,0,0,0},
  {2,0,0,0,1,0,0},
  {2,1,1,0,0,0,0},
  {2,0,0,1,1,0,0},
  {2,1,1,1,0,0,0},
  {2,1,1,1,1,0,0},
  {3,1,0,0,0,0,0},
  {3,0,1,0,0,0,0},
  {3,0,0,1,0,0,0},
  {3,0,0,0,1,0,0},
  {3,0,0,0,0,1,0},
  {3,0,0,0,0,0,1},
  {3,1,1,0,0,0,0},
  {3,0,0,1,1,0,0},
  {3,0,0,0,0,1,1},
  {3,1,1,1,0,0,0},
  {3,0,0,0,1,1,1},
  {3,1,1,1,1,0,0},
  {3,1,0,1,0,0,0}
};

const uint8_t table_7_3_2_3_3_3_twoCodeword[2][7] = {
  {3,1,1,1,1,1,0},
  {3,1,1,1,1,1,1}
};

const uint8_t table_7_3_2_3_3_4_oneCodeword[58][14] = {
  {1,1,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,0,1,0,0,0,0,0,0,0,0,0,0,1},
  {1,1,1,0,0,0,0,0,0,0,0,0,0,1},
  {2,1,0,0,0,0,0,0,0,0,0,0,0,1},
  {2,0,1,0,0,0,0,0,0,0,0,0,0,1},
  {2,0,0,1,0,0,0,0,0,0,0,0,0,1},
  {2,0,0,0,1,0,0,0,0,0,0,0,0,1},
  {2,1,1,0,0,0,0,0,0,0,0,0,0,1},
  {2,0,0,1,1,0,0,0,0,0,0,0,0,1},
  {2,1,1,1,0,0,0,0,0,0,0,0,0,1},
  {2,1,1,1,1,0,0,0,0,0,0,0,0,1},
  {3,1,0,0,0,0,0,0,0,0,0,0,0,1},
  {3,0,1,0,0,0,0,0,0,0,0,0,0,1},
  {3,0,0,1,0,0,0,0,0,0,0,0,0,1},
  {3,0,0,0,1,0,0,0,0,0,0,0,0,1},
  {3,0,0,0,0,1,0,0,0,0,0,0,0,1},
  {3,0,0,0,0,0,1,0,0,0,0,0,0,1},
  {3,1,1,0,0,0,0,0,0,0,0,0,0,1},
  {3,0,0,1,1,0,0,0,0,0,0,0,0,1},
  {3,0,0,0,0,1,1,0,0,0,0,0,0,1},
  {3,1,1,1,0,0,0,0,0,0,0,0,0,1},
  {3,0,0,0,1,1,1,0,0,0,0,0,0,1},
  {3,1,1,1,1,0,0,0,0,0,0,0,0,1},
  {2,1,0,1,0,0,0,0,0,0,0,0,0,1},
  {3,1,0,0,0,0,0,0,0,0,0,0,0,2},
  {3,0,1,0,0,0,0,0,0,0,0,0,0,2},
  {3,0,0,1,0,0,0,0,0,0,0,0,0,2},
  {3,0,0,0,1,0,0,0,0,0,0,0,0,2},
  {3,0,0,0,0,1,0,0,0,0,0,0,0,2},
  {3,0,0,0,0,0,1,0,0,0,0,0,0,2},
  {3,0,0,0,0,0,0,1,0,0,0,0,0,2},
  {3,0,0,0,0,0,0,0,1,0,0,0,0,2},
  {3,0,0,0,0,0,0,0,0,1,0,0,0,2},
  {3,0,0,0,0,0,0,0,0,0,1,0,0,2},
  {3,0,0,0,0,0,0,0,0,0,0,1,0,2},
  {3,0,0,0,0,0,0,0,0,0,0,0,1,2},
  {3,1,1,0,0,0,0,0,0,0,0,0,0,2},
  {3,0,0,1,1,0,0,0,0,0,0,0,0,2},
  {3,0,0,0,0,1,1,0,0,0,0,0,0,2},
  {3,0,0,0,0,0,0,1,1,0,0,0,0,2},
  {3,0,0,0,0,0,0,0,0,1,1,0,0,2},
  {3,0,0,0,0,0,0,0,0,0,0,1,1,2},
  {3,1,1,0,0,0,0,1,0,0,0,0,0,2},
  {3,0,0,1,1,0,0,0,0,1,0,0,0,2},
  {3,0,0,0,0,1,1,0,0,0,0,1,0,2},
  {3,1,1,0,0,0,0,1,1,0,0,0,0,2},
  {3,0,0,1,1,0,0,0,0,1,1,0,0,2},
  {3,0,0,0,0,1,1,0,0,0,0,1,1,2},
  {1,1,0,0,0,0,0,0,0,0,0,0,0,2},
  {1,0,1,0,0,0,0,0,0,0,0,0,0,2},
  {1,0,0,0,0,0,0,1,0,0,0,0,0,2},
  {1,0,0,0,0,0,0,0,1,0,0,0,0,2},
  {1,1,1,0,0,0,0,0,0,0,0,0,0,2},
  {1,0,0,0,0,0,0,1,1,0,0,0,0,2},
  {2,1,1,0,0,0,0,0,0,0,0,0,0,2},
  {2,0,0,1,1,0,0,0,0,0,0,0,0,2},
  {2,0,0,0,0,0,0,1,1,0,0,0,0,2},
  {2,0,0,0,0,0,0,0,0,1,1,0,0,2}
};

const uint8_t table_7_3_2_3_3_4_twoCodeword[6][14] = {
  {3,1,1,1,1,1,0,0,0,0,0,0,0,1},
  {3,1,1,1,1,1,1,0,0,0,0,0,0,1},
  {2,1,1,1,1,0,0,1,0,0,0,0,0,2},
  {2,1,1,1,1,0,0,1,0,1,0,0,0,2},
  {2,1,1,1,1,0,0,1,1,1,0,0,0,2},
  {2,1,1,1,1,0,0,1,1,1,1,0,0,2},
};

// table 7.2-1 TS 38.321
const uint16_t table_7_2_1[16] = {
  5,    // row index 0
  10,   // row index 1
  20,   // row index 2
  30,   // row index 3
  40,   // row index 4
  60,   // row index 5
  80,   // row index 6
  120,  // row index 7
  160,  // row index 8
  240,  // row index 9
  320,  // row index 10
  480,  // row index 11
  960,  // row index 12
  1920, // row index 13
};
