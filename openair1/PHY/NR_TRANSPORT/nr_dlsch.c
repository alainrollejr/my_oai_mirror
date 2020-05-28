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

/*! \file PHY/NR_TRANSPORT/dlsch_decoding.c
* \brief Top-level routines for transmission of the PDSCH 38211 v 15.2.0
* \author Guy De Souza
* \date 2018
* \version 0.1
* \company Eurecom
* \email: desouza@eurecom.fr
* \note
* \warning
*/

//pipeline scrambling and modulation from Ian
#include "PHY/phy_extern.h"
#include "PHY/defs_gNB.h"
#include "common/ran_context.h"
#include "nr_dlsch.h"
#include "nr_dci.h"
#include "nr_sch_dmrs.h"
#include "PHY/MODULATION/nr_modulation.h"
#define thread_for_scrambling_modulation
//#define DEBUG_DLSCH
//#define DEBUG_DLSCH_MAPPING

void nr_pdsch_codeword_scrambling(uint8_t *in,
                                  uint32_t size,
                                  uint8_t q,  //use q
                                  uint32_t Nid,
                                  uint32_t n_RNTI,
                                  uint32_t* out) {  //use q => scrambled_output[q]

  uint8_t reset, b_idx;
  uint32_t x1, x2, s=0;

  reset = 1;
  x2 = (n_RNTI<<15) + (q<<14) + Nid;
  int a = 0;
  for (int i=0; i<size; i++) {
    b_idx = i&0x1f;
    if (b_idx==0) {
	//  printf("bedore s = %d x1 = %d x2 = %d a = %d\n",s,x1,x2,a);	
      s = lte_gold_generic(&x1, &x2, reset);
	//  printf("after ~~~~~s = %d x1 = %d x2 = %d a = %d\n",s,x1,x2,a);
      reset = 0;
	  a++;
      if (i)
        out++;
    }
    *out ^= (((in[i])&1) ^ ((s>>b_idx)&1))<<b_idx;
    //printf("i %d b_idx %d in %d s 0x%08x out 0x%08x\n", i, b_idx, in[i], s, *out);
  }

}


uint8_t nr_generate_pdsch(NR_gNB_DLSCH_t *dlsch,
                          NR_gNB_DCI_ALLOC_t *dci_alloc,
                          uint32_t ***pdsch_dmrs,
                          int32_t** txdataF,
                          int16_t amp,
                          int     frame,
                          uint8_t slot,
                          NR_DL_FRAME_PARMS *frame_parms,
                          nfapi_nr_config_request_t *config,
                          time_stats_t *dlsch_encoding_stats,
                          time_stats_t *dlsch_scrambling_stats,
                          time_stats_t *dlsch_modulation_stats) {

  PHY_VARS_gNB *gNB = RC.gNB[0][0];
  NR_DL_gNB_HARQ_t *harq = dlsch->harq_processes[dci_alloc->harq_pid];
  nfapi_nr_dl_config_dlsch_pdu_rel15_t *rel15 = &harq->dlsch_pdu.dlsch_pdu_rel15;
  nfapi_nr_dl_config_pdcch_parameters_rel15_t pdcch_params = dci_alloc->pdcch_params;
  uint32_t scrambled_output[NR_MAX_NB_CODEWORDS][NR_MAX_PDSCH_ENCODED_LENGTH>>5]; //NR_MAX_NB_CODEWORDS 2
  int16_t **mod_symbs = (int16_t**)dlsch->mod_symbs;
  int16_t **tx_layers = (int16_t**)dlsch->txdataF;
  int8_t Wf[2], Wt[2], l0, l_prime[2], delta;
  uint16_t nb_symbols = rel15->nb_mod_symbols;
  struct timespec tt1, tt2, tt3, tt4, tt5;
  uint8_t Qm = rel15->modulation_order;
  uint32_t encoded_length = nb_symbols*Qm;
  gNB->complete_scrambling_and_modulation = 0;
  gNB->complete_scrambling = 0;
  gNB->complete_modulation = 0;
  /*
  for(int q = 0 ;q<NR_MAX_NB_CODEWORDS;q++){
	gNB->q_scrambling[q] = 0;
  }
  */
  /// CRC, coding, interleaving and rate matching
  AssertFatal(harq->pdu!=NULL,"harq->pdu is null\n");
  start_meas(dlsch_encoding_stats);
  nr_dlsch_encoding(harq->pdu, frame, slot, dlsch, frame_parms);  //the way to encoder
  stop_meas(dlsch_encoding_stats);
  //printf("rel15->nb_codewords : %d\n", rel15->nb_codewords);
#ifdef DEBUG_DLSCH  // ==Show original payload & encoded payload ==***
printf("PDSCH encoding:\nPayload:\n");
for (int i=0; i<harq->B>>7; i++) {
  for (int j=0; j<16; j++)
    printf("0x%02x\t", harq->pdu[(i<<4)+j]);
  printf("\n");
}
printf("\nEncoded payload:\n");
for (int i=0; i<encoded_length>>3; i++) {
  for (int j=0; j<8; j++)
    printf("%d", harq->f[(i<<3)+j]);
  printf("\t");
}
printf("\n");
#endif
	long sum  = 0;
#if 0
//#ifdef thread_for_scrambling_modulation //the way to scrambling & modulation
//	for(int j = 0;j<100;j++){
		gNB->complete_scrambling_and_modulation = 0;
		gNB->complete_modulation = 0;
		clock_gettime(CLOCK_REALTIME, &tt3);
		//clock_gettime(CLOCK_REALTIME, &tt1);
		//pthread_cond_signal(&gNB->thread_modulation.cond_tx);
		for (int q=0; q<rel15->nb_codewords; q++)
			pthread_cond_signal(&gNB->thread_scrambling[q].cond_tx);
		//clock_gettime(CLOCK_REALTIME, &tt2);
		//printf("pthread_cond_signal thread_scrambling [%d]  consumes %ld nanoseconds!\n",j,tt2.tv_nsec - tt1.tv_nsec);
		//while(gNB->complete_modulation != 1);
		//clock_gettime(CLOCK_REALTIME, &tt1);
		while(gNB->complete_scrambling_and_modulation != 1);
		//clock_gettime(CLOCK_REALTIME, &tt2);
		//printf(" busy waiting for  [%d]  consumes %ld nanoseconds!\n",j,tt2.tv_nsec - tt1.tv_nsec);
		clock_gettime(CLOCK_REALTIME, &tt4);
		//usleep(100000);
	//	printf("scrambling_proc[] for all consumes %ld nanoseconds!%%%%%%%%%%%%%%%\n",tt4.tv_nsec - tt3.tv_nsec);
//		sum += (tt4.tv_nsec - tt3.tv_nsec);
//	}
//	printf("averge time = %ld\n",sum/100);
	
#elseif 0//original
  /// scrambling
  start_meas(dlsch_scrambling_stats);
  //printf("nb_codewords = %d encoded_length = %d\n",rel15->nb_codewords,encoded_length);
  for (int q=0; q<rel15->nb_codewords; q++)
    memset((void*)scrambled_output[q], 0, (encoded_length>>5)*sizeof(uint32_t));
  uint16_t n_RNTI = (pdcch_params.search_space_type == NFAPI_NR_SEARCH_SPACE_TYPE_UE_SPECIFIC)? \
  ((pdcch_params.scrambling_id==0)?pdcch_params.rnti:0) : 0;
  uint16_t Nid = (pdcch_params.search_space_type == NFAPI_NR_SEARCH_SPACE_TYPE_UE_SPECIFIC)? \
  pdcch_params.scrambling_id : config->sch_config.physical_cell_id.value;
  for (int q=0; q<rel15->nb_codewords; q++)
    nr_pdsch_codeword_scrambling(harq->f,
                                 encoded_length,
                                 q,
                                 Nid,
                                 n_RNTI,
                                 scrambled_output[q]);

  stop_meas(dlsch_scrambling_stats);
#ifdef DEBUG_DLSCH
printf("PDSCH scrambling:\n");
for (int i=0; i<encoded_length>>8; i++) {
  for (int j=0; j<8; j++)
    printf("0x%08x\t", scrambled_output[0][(i<<3)+j]);
  printf("\n");
}
#endif
 
  /// Modulation
  start_meas(dlsch_modulation_stats);
  for (int q=0; q<rel15->nb_codewords; q++)
    nr_modulation(scrambled_output[q],
                         encoded_length,
                         Qm,
                         mod_symbs[q]);
  stop_meas(dlsch_modulation_stats);
#ifdef DEBUG_DLSCH
printf("PDSCH Modulation: Qm %d(%d)\n", Qm, nb_symbols);
for (int i=0; i<nb_symbols>>3; i++) {
  for (int j=0; j<8; j++) {
    printf("%d %d\t", mod_symbs[0][((i<<3)+j)<<1], mod_symbs[0][(((i<<3)+j)<<1)+1]);
  }
  printf("\n");
}
#endif
#endif

//[START]multi_genetate_pdsch_proc
struct timespec start_ts, end_ts;

for (int q=0; q<rel15->nb_codewords; q++) // ==Look out!NR_MAX_NB_CODEWORDS is 2!So we can't let q>2 until spec change
  memset((void*)scrambled_output[q], 0, (encoded_length>>5)*sizeof(uint32_t));
uint16_t n_RNTI = (pdcch_params.search_space_type == NFAPI_NR_SEARCH_SPACE_TYPE_UE_SPECIFIC) ? ((pdcch_params.scrambling_id==0)?pdcch_params.rnti:0) : 0;
uint16_t Nid = (pdcch_params.search_space_type == NFAPI_NR_SEARCH_SPACE_TYPE_UE_SPECIFIC) ? pdcch_params.scrambling_id : config->sch_config.physical_cell_id.value;
printf("==================[Scr]==================\n");
printf(" [Movement]  [No.]  [Round]  [Cost time] \n");
//Get value for dual thread
for (int q=0; q<thread_num_pdsch; q++){
  gNB->multi_encoder[q].f = harq->f;
  gNB->multi_encoder[q].encoded_length = encoded_length;
  gNB->multi_encoder[q].Nid = Nid;
  gNB->multi_encoder[q].n_RNTI = n_RNTI;
  gNB->multi_encoder[q].scrambled_output = scrambled_output[q]; // ==Need to change ==***
  gNB->multi_encoder[q].Qm = Qm;
  gNB->multi_encoder[q].mod_symbs = mod_symbs[q]; // ==Need to change ==***
}
//Get value for pressure
for (int q=0; q<thread_num_pressure; q++){
  //gNB->pressure_test[q].f = harq->f;
  gNB->pressure_test[q].encoded_length = encoded_length;
  gNB->pressure_test[q].Nid = Nid;
  gNB->pressure_test[q].n_RNTI = n_RNTI;
  //gNB->pressure_test[q].scrambled_output = scrambled_output[q]; // ==Need to change ==***
  gNB->pressure_test[q].Qm = Qm;
  //gNB->pressure_test[q].mod_symbs = mod_symbs[q]; // ==Need to change ==***
}
for(int th=0;th<thread_num_pressure;th++){
  for (int q=0; q<NR_MAX_NB_CODEWORDS; q++){
    gNB->pressure_test[th].mod_symbs_test[q] = (int32_t *)malloc16(NR_MAX_PDSCH_ENCODED_LENGTH*sizeof(int32_t));
  }
  for (int i=0; i<encoded_length>>3; i++) {
    for (int j=0; j<8; j++)
      gNB->pressure_test[th].f_test[(i<<3)+j] = harq->f[(i<<3)+j];
  }
  for (int q=0; q<rel15->nb_codewords; q++) // ==Look out!NR_MAX_NB_CODEWORDS is 2!So we can't let q>2 until spec change
    memset((void*)gNB->pressure_test[th].scrambled_output_test[q], 0, (encoded_length>>5)*sizeof(uint32_t));
}
//Get value for multi pdsch
//scrambling
for (int th=0; th<thread_num_scrambling; th++){
  for (int i=0; i<encoded_length>>3; i++) {
    for (int j=0; j<8; j++)
      gNB->multi_pdsch.f[th][(i<<3)+j] = harq->f[(i<<3)+j];
  }
  for (int q=0; q<rel15->nb_codewords; q++) // ==Look out!NR_MAX_NB_CODEWORDS is 2!So we can't let q>2 until spec change
    memset((void*)gNB->multi_pdsch.scrambled_output_scr[th][q], 0, (encoded_length>>5)*sizeof(uint32_t));
  gNB->multi_pdsch.encoded_length_scr[th] = encoded_length;
  gNB->multi_pdsch.Nid[th] = Nid;
  gNB->multi_pdsch.n_RNTI[th] = n_RNTI;
}
//modulation
for (int th=0; th<thread_num_modulation; th++){
  for (int q=0; q<rel15->nb_codewords; q++) // ==Look out!NR_MAX_NB_CODEWORDS is 2!So we can't let q>2 until spec change
    memset((void*)gNB->multi_pdsch.scrambled_output_mod[th][q], 0, (encoded_length>>5)*sizeof(uint32_t));
  for (int q=0; q<NR_MAX_NB_CODEWORDS; q++){
    gNB->multi_pdsch.mod_symbs[th][q] = (int32_t *)malloc16(NR_MAX_PDSCH_ENCODED_LENGTH*sizeof(int32_t));
  }
  gNB->multi_pdsch.encoded_length_mod[th] = encoded_length;
  gNB->multi_pdsch.Qm[th] = Qm;
}
//Show value for pressure
// printf("\nEncoded payload:\n");
// for (int i=0; i<10; i++) {
//   for (int j=0; j<3; j++)
//     printf("%d", harq->f[(i<<3)+j]);
//   printf("\t");
// }
// printf("\nEncoded payload:\n");
// for (int i=0; i<10; i++) {
//   for (int j=0; j<3; j++)
//     printf("%d", gNB->pressure_test[0].f_test[(i<<3)+j]);
//   printf("\t");
// }
//Awake scrambling threads(or scr_mod threads)
VCD_SIGNAL_DUMPER_DUMP_FUNCTION_BY_NAME(VCD_SIGNAL_DUMPER_FUNCTIONS_MULTI_ENC,1);
clock_gettime(CLOCK_MONOTONIC, &start_ts);  //timing
for (int q=0; q<thread_num_pdsch; q++){
  pthread_cond_signal(&(gNB->multi_encoder[q].cond_scr_mod));
}
for (int q=0; q<thread_num_pressure; q++){
  pthread_cond_signal(&(gNB->pressure_test[q].cond_scr_mod));
}
for (int th=0; th<thread_num_scrambling; th++){
  pthread_cond_signal(&(gNB->multi_pdsch.cond_scr[th]));
}
//Wait scrambling threads(or scr_mod threads)
for (int q=0; q<thread_num_pdsch; q++){
  while(gNB->multi_encoder[q].complete_scr_mod!=1);
}
for (int q=0; q<thread_num_pressure; q++){
  while(gNB->pressure_test[q].complete_scr_mod!=1);
}
for (int th=0; th<thread_num_scrambling; th++){
  while(gNB->multi_pdsch.complete_scr[th]!=1);
}
clock_gettime(CLOCK_MONOTONIC, &end_ts);  //timing
printf("   Total                      %.2f usec\n", (end_ts.tv_nsec - start_ts.tv_nsec) *1.0 / 1000);
printf("==================[Mod]==================\n");
printf(" [Movement]  [No.]  [Round]  [Cost time] \n");
clock_gettime(CLOCK_MONOTONIC, &start_ts);  //timing
//Awake modulation threads
for (int th=0; th<thread_num_modulation; th++){
  pthread_cond_signal(&(gNB->multi_pdsch.cond_mod[th]));
}
//Wait modulation threads
for (int th=0; th<thread_num_modulation; th++){
  while(gNB->multi_pdsch.complete_mod[th]!=1);
}
clock_gettime(CLOCK_MONOTONIC, &end_ts);  //timing
VCD_SIGNAL_DUMPER_DUMP_FUNCTION_BY_NAME(VCD_SIGNAL_DUMPER_FUNCTIONS_MULTI_ENC,0);
//printf("  Movement    No.    Round    Cost time  \n");
printf("   Total                      %.2f usec\n", (end_ts.tv_nsec - start_ts.tv_nsec) *1.0 / 1000);
//[END]multi_genetate_pdsch_proc

  /// Layer mapping
  nr_layer_mapping(mod_symbs,
                         rel15->nb_layers,
                         nb_symbols,
                         tx_layers);
#ifdef DEBUG_DLSCH
printf("Layer mapping (%d layers):\n", rel15->nb_layers);
for (int l=0; l<rel15->nb_layers; l++)
  for (int i=0; i<(nb_symbols/rel15->nb_layers)>>3; i++) {
    for (int j=0; j<8; j++) {
      printf("%d %d\t", tx_layers[l][((i<<3)+j)<<1], tx_layers[l][(((i<<3)+j)<<1)+1]);
    }
    printf("\n");
  }
#endif

  /// Antenna port mapping
    //to be moved to init phase potentially, for now tx_layers 1-8 are mapped on antenna ports 1000-1007

  /// DMRS QPSK modulation
  uint16_t n_dmrs = ((rel15->n_prb+rel15->start_prb)*rel15->nb_re_dmrs)<<1;
  int16_t mod_dmrs[n_dmrs<<1];
  uint8_t dmrs_type = config->pdsch_config.dmrs_type.value;
  uint8_t mapping_type = config->pdsch_config.mapping_type.value;

  l0 = get_l0(mapping_type, 2);//config->pdsch_config.dmrs_typeA_position.value);
  nr_modulation(pdsch_dmrs[l0][0], n_dmrs, DMRS_MOD_ORDER, mod_dmrs); // currently only codeword 0 is modulated. Qm = 2 as DMRS is QPSK modulated

#ifdef DEBUG_DLSCH
printf("DMRS modulation (single symbol %d, %d symbols, type %d):\n", l0, n_dmrs>>1, dmrs_type);
for (int i=0; i<n_dmrs>>4; i++) {
  for (int j=0; j<8; j++) {
    printf("%d %d\t", mod_dmrs[((i<<3)+j)<<1], mod_dmrs[(((i<<3)+j)<<1)+1]);
  }
  printf("\n");
}
#endif


  /// Resource mapping

  // Non interleaved VRB to PRB mapping
  uint16_t start_sc = frame_parms->first_carrier_offset + rel15->start_prb*NR_NB_SC_PER_RB;
  if (start_sc >= frame_parms->ofdm_symbol_size)
    start_sc -= frame_parms->ofdm_symbol_size;

#ifdef DEBUG_DLSCH_MAPPING
 printf("PDSCH resource mapping started (start SC %d\tstart symbol %d\tN_PRB %d\tnb_symbols %d)\n",
	start_sc, rel15->start_symbol, rel15->n_prb, rel15->nb_symbols);
#endif
  //printf("nb_layers = %d\n",rel15->nb_layers);
  for (int ap=0; ap<rel15->nb_layers; ap++) {

    // DMRS params for this ap
    get_Wt(Wt, ap, dmrs_type);
    get_Wf(Wf, ap, dmrs_type);
    delta = get_delta(ap, dmrs_type);
    l_prime[0] = 0; // single symbol ap 0
    uint8_t dmrs_symbol = l0+l_prime[0];
#ifdef DEBUG_DLSCH_MAPPING
printf("DMRS params for ap %d: Wt %d %d \t Wf %d %d \t delta %d \t l_prime %d \t l0 %d\tDMRS symbol %d\n",
ap, Wt[0], Wt[1], Wf[0], Wf[1], delta, l_prime[0], l0, dmrs_symbol);
#endif
    uint8_t k_prime=0;
    uint16_t m=0, n=0, dmrs_idx=0, k=0;
    int txdataF_offset = (slot%2)*frame_parms->samples_per_slot_wCP;
    if (dmrs_type == NFAPI_NR_DMRS_TYPE1) // another if condition to be included to check pdsch config type (reference of k)
      dmrs_idx = rel15->start_prb*6;
    else
      dmrs_idx = rel15->start_prb*4;

    for (int l=rel15->start_symbol; l<rel15->start_symbol+rel15->nb_symbols; l++) {
      k = start_sc;
      for (int i=0; i<rel15->n_prb*NR_NB_SC_PER_RB; i++) {
        if ((l == dmrs_symbol) && (k == ((start_sc+get_dmrs_freq_idx(n, k_prime, delta, dmrs_type))%(frame_parms->ofdm_symbol_size)))) {
          ((int16_t*)txdataF[ap])[((l*frame_parms->ofdm_symbol_size + k)<<1) + (2*txdataF_offset)] = (Wt[l_prime[0]]*Wf[k_prime]*amp*mod_dmrs[dmrs_idx<<1]) >> 15;
          ((int16_t*)txdataF[ap])[((l*frame_parms->ofdm_symbol_size + k)<<1) + 1 + (2*txdataF_offset)] = (Wt[l_prime[0]]*Wf[k_prime]*amp*mod_dmrs[(dmrs_idx<<1) + 1]) >> 15;
#ifdef DEBUG_DLSCH_MAPPING
printf("dmrs_idx %d\t l %d \t k %d \t k_prime %d \t n %d \t txdataF: %d %d\n",
dmrs_idx, l, k, k_prime, n, ((int16_t*)txdataF[ap])[((l*frame_parms->ofdm_symbol_size + k)<<1) + (2*txdataF_offset)],
((int16_t*)txdataF[ap])[((l*frame_parms->ofdm_symbol_size + k)<<1) + 1 + (2*txdataF_offset)]);
#endif
          dmrs_idx++;
          k_prime++;
          k_prime&=1;
          n+=(k_prime)?0:1;
        }

        else {

          ((int16_t*)txdataF[ap])[((l*frame_parms->ofdm_symbol_size + k)<<1) + (2*txdataF_offset)] = (amp * tx_layers[ap][m<<1]) >> 15;
          ((int16_t*)txdataF[ap])[((l*frame_parms->ofdm_symbol_size + k)<<1) + 1 + (2*txdataF_offset)] = (amp * tx_layers[ap][(m<<1) + 1]) >> 15;
#ifdef DEBUG_DLSCH_MAPPING
printf("m %d\t l %d \t k %d \t txdataF: %d %d\n",
m, l, k, ((int16_t*)txdataF[ap])[((l*frame_parms->ofdm_symbol_size + k)<<1) + (2*txdataF_offset)],
((int16_t*)txdataF[ap])[((l*frame_parms->ofdm_symbol_size + k)<<1) + 1 + (2*txdataF_offset)]);
#endif
          m++;
        }
        if (++k >= frame_parms->ofdm_symbol_size)
          k -= frame_parms->ofdm_symbol_size;
      }
    }
  }
  return 0;
}
