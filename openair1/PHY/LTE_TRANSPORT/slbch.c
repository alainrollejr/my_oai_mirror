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

/*! \file PHY/LTE_TRANSPORT/pss.c
* \brief Top-level routines for generating primary synchronization signal (PSS) V8.6 2009-03
* \author F. Kaltenberger, O. Tonelli, R. Knopp
* \date 2011
* \version 0.1
* \company Eurecom
* \email: florian.kaltenberger@eurecom.fr, oscar.tonelli@yahoo.it,knopp@eurecom.fr
* \note
* \warning
*/
/* file: slbch.c
   purpose: TX and RX procedures for Sidelink Broadcast Channel
   author: raymond.knopp@eurecom.fr
   date: 02.05.2018
*/

//#include "defs.h"
#include "PHY/defs.h"
#include "PHY/extern.h"

#define PSBCH_A 40
#define PSBCH_E 1008 //12REs/PRB*6PRBs*7symbols*2 bits/RB



	  
int generate_slbch(int32_t **txdataF,
		   short amp,
		   LTE_DL_FRAME_PARMS *frame_parms,
		   int subframe,
		   uint8_t *slmib) {
  
  uint8_t slbch_a[PSBCH_A>>3];
  uint32_t psbch_D;
  uint8_t psbch_d[96+(3*(16+PSBCH_A))];
  uint8_t psbch_w[3*3*(16+PSBCH_A)];
  uint8_t psbch_e[PSBCH_E];
  uint8_t RCC;
  int a;

  psbch_D    = 16+PSBCH_A;
  
  AssertFatal(frame_parms->Ncp==NORMAL,"Only Normal Prefix supported for Sidelink\n");
  AssertFatal(frame_parms->Ncp==NORMAL,"Only Normal Prefix supported for Sidelink\n");

  bzero(slbch_a,PSBCH_A>>3);
  bzero(psbch_e,PSBCH_E);
  memset(psbch_d,LTE_NULL,96);
    
  for (int i=0; i<(PSBCH_A>>3); i++)
    slbch_a[(PSBCH_A>>3)-i-1] = slmib[i];

  ccodelte_encode(PSBCH_A,2,slbch_a,psbch_d+96,0);
  RCC = sub_block_interleaving_cc(psbch_D,psbch_d+96,psbch_w);
  
  lte_rate_matching_cc(RCC,PSBCH_E,psbch_w,psbch_e);
  //  for (int i=0;i<PSBCH_E;i++) printf("PSBCH E[%d] %d\n",i,psbch_e[i]);
  pbch_scrambling(frame_parms,
		  psbch_e,
		  PSBCH_E,
		  1);
  int symb=0;
  uint8_t *eptr = psbch_e;
  int16_t *txptr;
  int k;

  a = (amp*SQRT_18_OVER_32_Q15)>>(15-2);
  int Nsymb=14;

  int16_t precin[144*12];
  int16_t precout[144*12];

  for (int i=0;i<144*7;i++)
    if (*eptr++ == 1) precin[i] =-a;
    else              precin[i] = a;
  
  dft_lte((int32_t*)precout,
	  (int32_t*)precin,
	  72,
	  12);

  int j=0;
  for (symb=0;symb<10;symb++) { 
    k = (frame_parms->ofdm_symbol_size<<1)-72;
    //    printf("Generating PSBCH in symbol %d offset %d\n",symb,
    //	       (subframe*Nsymb*frame_parms->ofdm_symbol_size)+(symb*frame_parms->ofdm_symbol_size));

    txptr = (int16_t*)&txdataF[0][(subframe*Nsymb*frame_parms->ofdm_symbol_size)+(symb*frame_parms->ofdm_symbol_size)];


    
    // first half (negative frequencies)
    for (int i=0;i<72;i++,j++,k++) txptr[k] = precout[j];
    // second half (positive frequencies)
    for (int i=0,k=0;i<72;i++,j++,k++) txptr[k] = precout[j];
     
    if (symb==0) symb+=3;
  }

  // scale by sqrt(72/62)
  // note : we have to scale for TX power requirements too, beta_PSBCH !

  //  //printf("[PSS] amp=%d, a=%d\n",amp,a);
  
  
  return(0);
}

int rx_psbch(PHY_VARS_UE *ue) {
  
  
  int16_t **rxdataF      = ue->sl_rxdataF;
  int16_t **rxdataF_ext  = ue->pusch_slbch->rxdataF_ext;
  int16_t **drs_ch_estimates = ue->pusch_sldch->drs_ch_estimates;
  int16_t **rxdataF_comp     = ue->pusch_sldch->rxdataF_comp;
  int16_t **ul_ch_mag        = ue->pusch_sldch->ul_ch_mag;
  int32_t avgs;
  uint8_t log2_maxh=0;
  int32_t avgU[2];
  int Nsymb=7;

  RU_t ru_tmp;
  memset((void*)&ru_tmp,0,sizeof(RU_t));
  
  memcpy((void*)&ru_tmp.frame_parms,(void*)&ue->frame_parms,sizeof(LTE_DL_FRAME_PARMS));
  ru_tmp.N_TA_offset=0;
  ru_tmp.common.rxdata_7_5kHz     = (int32_t**)malloc16(ue->frame_parms.nb_antennas_rx*sizeof(int32_t*)); 
  for (int aa=0;aa<ue->frame_parms.nb_antennas_rx;aa++) 
    ru_tmp.common.rxdata_7_5kHz[aa] = (int32_t*)&ue->common_vars.rxdata_syncSL[aa][ue->rx_offsetSL*2];
  ru_tmp.common.rxdataF = (int32_t**)rxdataF;
  ru_tmp.nb_rx = ue->frame_parms.nb_antennas_rx;
  
  for (int l=0; l<11; l++) {
    slot_fep_ul(&ru_tmp,l%7,(l>6)?1:0,0);
    ulsch_extract_rbs_single((int32_t**)rxdataF,
			     (int32_t**)rxdataF_ext,
			     (ue->frame_parms.N_RB_UL/2)-3,
			     6,
			     l,
			     0,
			     &ue->frame_parms);
    if (l==0) l+=2;
  }
#ifdef PSBCH_DEBUG
  write_output("slbch_rxF.m",
	       "slbchrxF",
	       &rxdataF[0][0],
	       14*ue->frame_parms.ofdm_symbol_size,1,1);
  write_output("slbch_rxF_ext.m","slbchrxF_ext",rxdataF_ext[0],14*12*ue->frame_parms.N_RB_DL,1,1);
#endif
  
  lte_ul_channel_estimation(&ue->frame_parms,
			    (int32_t**)drs_ch_estimates,
			    (int32_t**)NULL,
			    (int32_t**)rxdataF_ext,
			    6,
			    0,
			    0,
			    ue->gh[0][0], //u
			    0, //v
			    (ue->frame_parms.Nid_SL>>1)&7, //cyclic_shift
			    3,
			    1, // interpolation
			    0);
  
  lte_ul_channel_estimation(&ue->frame_parms,
			    (int32_t**)drs_ch_estimates,
			    (int32_t**)NULL,
			    (int32_t**)rxdataF_ext,
			    6,
			    0,
			    0,
			    ue->gh[0][1],//u
			    0,//v
			    (ue->frame_parms.Nid_SL>>1)&7,//cyclic_shift,
			    10,
			    1, // interpolation
			    0);
  
  ulsch_channel_level(drs_ch_estimates,
		      &ue->frame_parms,
		      avgU,
		      2);
  
#ifdef PSBCH_DEBUG
  write_output("drsbch_ext0.m","drsbchest0",drs_ch_estimates[0],ue->frame_parms.N_RB_UL*12*14,1,1);
#endif
  
  avgs = 0;
  
  for (int aarx=0; aarx<ue->frame_parms.nb_antennas_rx; aarx++)
    avgs = cmax(avgs,avgU[aarx]);
  
  //      log2_maxh = 4+(log2_approx(avgs)/2);
  
  log2_maxh = (log2_approx(avgs)/2)+ log2_approx(ue->frame_parms.nb_antennas_rx-1)+4;
  
  
  for (int l=0; l<10; l++) {
    
    
    ulsch_channel_compensation(
			       rxdataF_ext,
			       drs_ch_estimates,
			       ul_ch_mag,
			       NULL,
			       rxdataF_comp,
			       &ue->frame_parms,
			       l,
			       2, //Qm
			       6, //nb_rb
			       log2_maxh); // log2_maxh+I0_shift
    
    if (ue->frame_parms.nb_antennas_rx > 1)
      ulsch_detection_mrc(&ue->frame_parms,
			  rxdataF_comp,
			  ul_ch_mag,
			  NULL,
			  l,
			  6 //nb_rb
			  );
    
    freq_equalization(&ue->frame_parms,
		      rxdataF_comp,
		      ul_ch_mag,
		      NULL,
		      l,
		      72,
		      2);
    
    if (l==0) l=3;
  }
  lte_idft(&ue->frame_parms,
	   rxdataF_comp[0],
	   72);
  
#ifdef PSBCH_DEBUG
  write_output("slbch_rxF_comp.m","slbchrxF_comp",rxdataF_comp[0],ue->frame_parms.N_RB_UL*12*14,1,1);
#endif
  int8_t llr[PSBCH_E];
  int8_t *llrp = llr;
  
  for (int l=0; l<10; l++) {
    pbch_quantize(llrp,
		  &rxdataF_comp[0][l*ue->frame_parms.N_RB_UL*12*2],
		  72*2);
    llrp += 72*2;
    if (l==0) l=3;
  }
  pbch_unscrambling(&ue->frame_parms,
		    llr,
		    PSBCH_E,
		    0,
		    1);
  
#ifdef PSBCH_DEBUG
  write_output("slbch_llr.m","slbch_llr",llr,PSBCH_E,1,4);
#endif
  
  uint8_t slbch_a[2+(PSBCH_A>>3)];
  uint32_t psbch_D;
  uint8_t psbch_d_rx[96+(3*(16+PSBCH_A))];
  uint8_t dummy_w_rx[3*3*(16+PSBCH_A)];
  uint8_t psbch_w_rx[3*3*(16+PSBCH_A)];	  
  uint8_t *psbch_e_rx=llr;
  uint8_t RCC;
  int a;
  uint8_t *decoded_output = ue->slss_rx.slmib;
  
  psbch_D    = 16+PSBCH_A;
  
  
  memset(dummy_w_rx,0,3*3*(psbch_D));
  RCC = generate_dummy_w_cc(psbch_D,
			    dummy_w_rx);
  
  
  lte_rate_matching_cc_rx(RCC,PSBCH_E,psbch_w_rx,dummy_w_rx,psbch_e_rx);
  
  sub_block_deinterleaving_cc(psbch_D,
			      &psbch_d_rx[96],
			      &psbch_w_rx[0]);
  
  memset(slbch_a,0,((16+PSBCH_A)>>3));
  
  
  
  
  phy_viterbi_lte_sse2(psbch_d_rx+96,slbch_a,16+PSBCH_A);
  
  // Fix byte endian of PSBCH (bit 39 goes in first)
  for (int i=0; i<(PSBCH_A>>3); i++)
    decoded_output[(PSBCH_A>>3)-i-1] = slbch_a[i];
  
  //  for (int i=0; i<(PSBCH_A>>3); i++) printf("SLBCH %d : %x\n",i,decoded_output[i]);
  
#ifdef DEBUG_PSBCH
  LOG_I(PHY,"PSBCH CRC %x : %x\n",
	crc16(slbch_a,PSBCH_A),
	((uint16_t)slbch_a[PSBCH_A>>3]<<8)+slbch_a[(PSBCH_A>>3)+1]);
#endif
  
  uint16_t crc = (crc16(slbch_a,PSBCH_A)>>16) ^
    (((uint16_t)slbch_a[PSBCH_A>>3]<<8)+slbch_a[(PSBCH_A>>3)+1]);

  if (crc>0) return(-1);
  else       return(0);

}
