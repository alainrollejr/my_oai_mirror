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

#include "PHY/defs_gNB.h"

#ifndef __NRLDPC_CODING_INTERFACE__H__
#define __NRLDPC_CODING_INTERFACE__H__

/**
 * \typedef nrLDPC_segment_decoding_parameters_t
 * \struct nrLDPC_segment_decoding_parameters_s
 * \brief decoding parameter of segments
 * \var E input llr segment size
 * \var R
 * \var llr input llr segment array
 * \var d Pointers to code blocks before LDPC decoding (38.212 V15.4.0 section 5.3.2)
 * \var d_to_be_cleared
 * pointer to the flag used to clear d properly
 * when true, clear d after rate dematching
 * \var c Pointers to code blocks after LDPC decoding (38.212 V15.4.0 section 5.2.2)
 * \var decodeSuccess
 * flag indicating that the decoding of the segment was successful
 * IT MUST BE FILLED BY THE IMPLEMENTATION
 */
typedef struct nrLDPC_segment_decoding_parameters_s{
  int E;
  uint8_t R;
  short *llr;
  int16_t *d;
  bool *d_to_be_cleared;
  uint8_t *c;
  bool decodeSuccess;
} nrLDPC_segment_decoding_parameters_t;

/**
 * \typedef nrLDPC_TB_decoding_parameters_t
 * \struct nrLDPC_TB_decoding_parameters_s
 * \brief decoding parameter of transport blocks
 * \var rnti RNTI
 * \var nb_rb number of resource blocks
 * \var Qm modulation order
 * \var mcs MCS
 * \var nb_layers number of layers
 * \var BG LDPC base graph id
 * \var rv_index
 * \var max_ldpc_iterations maximum number of LDPC iterations
 * \var abort_decode pointer to decode abort flag
 * \var G
 * \var tbslbrm Transport block size LBRM
 * \var A Transport block size (This is A from 38.212 V15.4.0 section 5.1)
 * \var K
 * \var Z lifting size
 * \var F filler bits size
 * \var C number of segments 
 * \var segments array of segments parameters
 */
typedef struct nrLDPC_TB_decoding_parameters_s{

  uint16_t rnti;
  uint16_t nb_rb;
  uint8_t Qm;
  uint8_t mcs;
  uint8_t nb_layers;

  uint8_t BG;
  uint8_t rv_index;
  uint8_t max_ldpc_iterations;
  decode_abort_t *abort_decode;

  uint32_t G;
  uint32_t tbslbrm;
  uint32_t A;
  uint32_t K;
  uint32_t Z;
  uint32_t F;

  uint32_t C;
  nrLDPC_segment_decoding_parameters_t *segments;
} nrLDPC_TB_decoding_parameters_t;

/**
 * \typedef nrLDPC_slot_decoding_parameters_t
 * \struct nrLDPC_slot_decoding_parameters_s
 * \brief decoding parameter of slot
 * \var frame frame index
 * \var slot slot index
 * \var nb_pusch number of uplink shared channels
 * \var respDecode pointer to the queue for decoding tasks
 * \var threadPool pointer to the thread pool
 * \var TBs array of TBs decoding parameters
 */
typedef struct nrLDPC_slot_decoding_parameters_s{
  int frame;
  int slot;
  int nb_pusch;
  notifiedFIFO_t *respDecode;
  tpool_t *threadPool;
  nrLDPC_TB_decoding_parameters_t *TBs;
} nrLDPC_slot_decoding_parameters_t;

typedef int32_t(nrLDPC_coding_init_t)(void);
typedef int32_t(nrLDPC_coding_shutdown_t)(void);

/**
 * \brief slot decoding function interface
 * \param nrLDPC_slot_decoding_parameters pointer to the structure holding the parameters necessary for decoding
 */
typedef int32_t(nrLDPC_coding_decoder_t)(nrLDPC_slot_decoding_parameters_t *nrLDPC_slot_decoding_parameters);

typedef int32_t(nrLDPC_coding_encoder_t)
  (PHY_VARS_gNB *gNB,
   processingData_L1tx_t *msgTx,
   int frame_tx,
   uint8_t slot_tx,
   NR_DL_FRAME_PARMS* frame_parms,
   unsigned char ** output,
   time_stats_t *tinput,
   time_stats_t *tprep,
   time_stats_t *tparity,
   time_stats_t *toutput,
   time_stats_t *dlsch_rate_matching_stats,
   time_stats_t *dlsch_interleaving_stats,
   time_stats_t *dlsch_segmentation_stats);

typedef struct nrLDPC_coding_interface_s {
  nrLDPC_coding_init_t *nrLDPC_coding_init;
  nrLDPC_coding_shutdown_t *nrLDPC_coding_shutdown;
  nrLDPC_coding_decoder_t *nrLDPC_coding_decoder;
  nrLDPC_coding_encoder_t *nrLDPC_coding_encoder;
} nrLDPC_coding_interface_t;

int load_nrLDPC_coding_interface(char *version, nrLDPC_coding_interface_t *interface);
int free_nrLDPC_coding_interface(nrLDPC_coding_interface_t *interface);

//TODO replace the global structure below
// Global var to limit the rework of the dirty legacy code
extern nrLDPC_coding_interface_t nrLDPC_coding_interface;

#endif
