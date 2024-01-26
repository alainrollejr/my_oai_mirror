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

/*! \file rrc_gNB.c
 * \brief rrc procedures for gNB
 * \author Navid Nikaein and  Raymond Knopp , WEI-TAI CHEN
 * \date 2011 - 2014 , 2018
 * \version 1.0
 * \company Eurecom, NTUST
 * \email: navid.nikaein@eurecom.fr and raymond.knopp@eurecom.fr, kroempa@gmail.com
 */
#define RRC_GNB_C
#define RRC_GNB_C

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "nr_rrc_config.h"
#include "nr_rrc_defs.h"
#include "nr_rrc_extern.h"
#include "assertions.h"
#include "common/ran_context.h"
#include "oai_asn1.h"
#include "rrc_gNB_radio_bearers.h"

#include "RRC/L2_INTERFACE/openair_rrc_L2_interface.h"
#include "LAYER2/NR_MAC_gNB/mac_proto.h"
#include "common/utils/LOG/log.h"
#include "RRC/NR/MESSAGES/asn1_msg.h"
#include "openair2/E1AP/e1ap_asnc.h"

#include "NR_BCCH-BCH-Message.h"
#include "NR_UL-DCCH-Message.h"
#include "NR_DL-DCCH-Message.h"
#include "NR_DL-CCCH-Message.h"
#include "NR_UL-CCCH-Message.h"
#include "NR_RRCReject.h"
#include "NR_RejectWaitTime.h"
#include "NR_RRCSetup.h"

#include "NR_CellGroupConfig.h"
#include "NR_MeasResults.h"
#include "NR_UL-CCCH-Message.h"
#include "NR_RRCSetupRequest-IEs.h"
#include "NR_RRCSetupComplete-IEs.h"
#include "NR_RRCReestablishmentRequest-IEs.h"
#include "NR_MIB.h"
#include "uper_encoder.h"
#include "uper_decoder.h"

#include "platform_types.h"
#include "common/utils/LOG/vcd_signal_dumper.h"

#include "T.h"

#include "RRC/NAS/nas_config.h"
#include "RRC/NAS/rb_config.h"

#include "openair3/SECU/secu_defs.h"

#include "rrc_gNB_NGAP.h"

#include "rrc_gNB_GTPV1U.h"

#include "nr_pdcp/nr_pdcp_entity.h"
#include "nr_pdcp/nr_pdcp.h"
#include "pdcp_primitives.h"
#include "nr_pdcp/nr_pdcp_oai_api.h"

#include "intertask_interface.h"
#include "SIMULATION/TOOLS/sim.h" // for taus

#include "executables/softmodem-common.h"
#include <openair2/RRC/NR/rrc_gNB_UE_context.h>
#include <openair2/X2AP/x2ap_eNB.h>
#include <openair3/SECU/key_nas_deriver.h>
#include <openair3/ocp-gtpu/gtp_itf.h>
#include <openair2/RRC/NR/nr_rrc_proto.h>
#include "openair2/LAYER2/nr_pdcp/nr_pdcp_e1_api.h"
#include "openair2/F1AP/f1ap_common.h"
#include "openair2/F1AP/f1ap_ids.h"
#include "openair2/SDAP/nr_sdap/nr_sdap_entity.h"
#include "cucp_cuup_if.h"
#include "PHY/defs_gNB.h"

#include "BIT_STRING.h"
#include "assertions.h"

//#define XER_PRINT

extern RAN_CONTEXT_t RC;

static inline uint64_t bitStr_to_uint64(BIT_STRING_t *asn);

mui_t rrc_gNB_mui = 0;


// temp static storage of AS Security settings for SRB1 and SRB2
static e_NR_IntegrityProtAlgorithm _int_algo = 0;
static NR_CipheringAlgorithm_t     _cip_algo = 0;
static uint8_t _nr_control_plane_int_key[16] = {0};
static uint8_t _nr_control_plane_cip_key[16] = {0};
static uint8_t    _nr_data_plane_int_key[16] = {0};
static uint8_t    _nr_data_plane_cip_key[16] = {0};
int find_configured_cell(uint64_t nr_cellid, const module_id_t gnb_mod_idP)
{
  int i = 0;
  int retVal = -1;
  gNB_RRC_INST   *rrc= RC.nrrrc[gnb_mod_idP];
  for (i = 0; i< MAX_NUM_CCs; i++)
  {
    if (rrc->configuration[i].cell_identity == nr_cellid)
    {
      retVal = i;
      break;
    }
  }

  return retVal;
}

NR_DRB_ToAddModList_t *fill_DRB_configList(gNB_RRC_UE_t *ue)
{
  gNB_RRC_INST *rrc = RC.nrrrc[0];
  if (ue->nb_of_pdusessions == 0) {
    LOG_I(NR_RRC, "%s:%d: No PDU sessions, exiting\n", __FUNCTION__, __LINE__);
    return NULL;
  }
  int CC_id = ue->primaryCC_id;
  int nb_drb_to_setup = rrc->configuration[CC_id].drbs;
  long drb_priority[MAX_DRBS_PER_UE] = {0};
  uint8_t drb_id_to_setup_start = 0;
  NR_DRB_ToAddModList_t *DRB_configList = CALLOC(sizeof(*DRB_configList), 1);

  for (int i = 0; i < ue->nb_of_pdusessions; i++) {
    if (ue->pduSession[i].status >= PDU_SESSION_STATUS_DONE) {
      continue;
    }
    LOG_I(NR_RRC, "adding rnti %x pdusession %d, nb drb %d\n", ue->rnti, ue->pduSession[i].param.pdusession_id, nb_drb_to_setup);
    for (long drb_id_add = 1; drb_id_add <= nb_drb_to_setup; drb_id_add++) {
      uint8_t drb_id;
      // Reference TS23501 Table 5.7.4-1: Standardized 5QI to QoS characteristics mapping
      for (int qos_flow_index = 0; qos_flow_index < ue->pduSession[i].param.nb_qos; qos_flow_index++) {
        switch (ue->pduSession[i].param.qos[qos_flow_index].fiveQI) {
          case 1 ... 4: /* GBR */
            drb_id = next_available_drb(ue, &ue->pduSession[i], GBR_FLOW);
            break;
          case 5 ... 9: /* Non-GBR */
            if (rrc->configuration[CC_id].drbs > 1) { /* Force the creation from gNB Conf file */
              LOG_W(NR_RRC, "Adding %d DRBs, from gNB config file (not decided by 5GC\n", rrc->configuration[CC_id].drbs);
              drb_id = next_available_drb(ue, &ue->pduSession[i], GBR_FLOW);
            } else {
              drb_id = next_available_drb(ue, &ue->pduSession[i], NONGBR_FLOW);
            }
            break;

          default:
            LOG_E(NR_RRC, "not supported 5qi %lu\n", ue->pduSession[i].param.qos[qos_flow_index].fiveQI);
            ue->pduSession[i].status = PDU_SESSION_STATUS_FAILED;
            continue;
        }
        drb_priority[drb_id - 1] = ue->pduSession[i].param.qos[qos_flow_index].allocation_retention_priority.priority_level;
        if (drb_priority[drb_id - 1] < 0 || drb_priority[drb_id - 1] > NGAP_PRIORITY_LEVEL_NO_PRIORITY) {
          LOG_E(NR_RRC, "invalid allocation_retention_priority.priority_level %ld set to _NO_PRIORITY\n", drb_priority[drb_id - 1]);
          drb_priority[drb_id - 1] = NGAP_PRIORITY_LEVEL_NO_PRIORITY;
        }

        if (drb_is_active(ue, drb_id)) { /* Non-GBR flow using the same DRB or a GBR flow with no available DRBs*/
          nb_drb_to_setup--;
        } else {
          generateDRB(ue,
                      drb_id,
                      &ue->pduSession[i],
                      rrc->configuration[CC_id].enable_sdap,
                      rrc->security.do_drb_integrity,
                      rrc->security.do_drb_ciphering);
          NR_DRB_ToAddMod_t *DRB_config = generateDRB_ASN1(&ue->established_drbs[drb_id - 1]);
          if (drb_id_to_setup_start == 0)
            drb_id_to_setup_start = DRB_config->drb_Identity;
          asn1cSeqAdd(&DRB_configList->list, DRB_config);
        }
        LOG_D(RRC, "DRB Priority %ld\n", drb_priority[drb_id]); // To supress warning for now
      }
    }
  }
  if (DRB_configList->list.count == 0) {
    free(DRB_configList);
    return NULL;
  }
  return DRB_configList;
}

static void freeDRBlist(NR_DRB_ToAddModList_t *list)
{
  //ASN_STRUCT_FREE(asn_DEF_NR_DRB_ToAddModList, list);
  return;
}

// AGP: removed in OAI@W32. Left in SQN version for compatibility
static void nr_rrc_addmod_srbs(int rnti,
                               const NR_SRB_INFO_TABLE_ENTRY *srb_list,
                               const int nb_srb,
                               const struct NR_CellGroupConfig__rlc_BearerToAddModList *bearer_list)
{
  if (srb_list == NULL || bearer_list == NULL)
    return;
  LOG_I(NR_RRC, "%s: nb_srb=%d\n", __FUNCTION__, nb_srb);
  
  for (int i = 0; i < nb_srb; i++) {
    if (srb_list[i].Active) {
      LOG_I(NR_RRC, "%s: srb_list[%d].Active\n", __FUNCTION__, i);
      for (int j = 0; j < bearer_list->list.count; j++) {
        const NR_RLC_BearerConfig_t *bearer = bearer_list->list.array[j];
        if (bearer->servedRadioBearer != NULL
            && bearer->servedRadioBearer->present == NR_RLC_BearerConfig__servedRadioBearer_PR_srb_Identity
            && i == bearer->servedRadioBearer->choice.srb_Identity) {
          nr_rlc_add_srb(rnti, i, bearer);
        }
      }
    }
  }
}

static void nr_rrc_addmod_drbs(int rnti,
                               const NR_DRB_ToAddModList_t *drb_list,
                               const struct NR_CellGroupConfig__rlc_BearerToAddModList *bearer_list)
{
  if (drb_list == NULL || bearer_list == NULL)
    return;

  for (int i = 0; i < drb_list->list.count; i++) {
    const NR_DRB_ToAddMod_t *drb = drb_list->list.array[i];
    for (int j = 0; j < bearer_list->list.count; j++) {
      const NR_RLC_BearerConfig_t *bearer = bearer_list->list.array[j];
      if (bearer->servedRadioBearer != NULL
          && bearer->servedRadioBearer->present == NR_RLC_BearerConfig__servedRadioBearer_PR_drb_Identity
          && drb->drb_Identity == bearer->servedRadioBearer->choice.drb_Identity) {
        nr_rlc_add_drb(rnti, drb->drb_Identity, bearer);
      }
    }
  }
}

typedef struct deliver_dl_rrc_message_data_s {
  const gNB_RRC_INST *rrc;
  f1ap_dl_rrc_message_t *dl_rrc;
} deliver_dl_rrc_message_data_t;
static void rrc_deliver_dl_rrc_message(void *deliver_pdu_data, ue_id_t ue_id, int srb_id, char *buf, int size, int sdu_id)
{
  DevAssert(deliver_pdu_data != NULL);

  if (RC.ss.mode > SS_GNB) {
    const gNB_RRC_INST *rrc = (const gNB_RRC_INST *)deliver_pdu_data;
    f1ap_dl_rrc_message_t dl_rrc = {.old_gNB_DU_ue_id = NULL,
                                  .rrc_container = (uint8_t *)buf,
                                  .rrc_container_length = size,
                                  // .rnti = ue_id,
                                  .gNB_CU_ue_id = ue_id,
                                  .gNB_DU_ue_id = ue_id,
                                  .srb_id = srb_id};
    rrc->mac_rrc.dl_rrc_message_transfer(&dl_rrc);
  } else {
    deliver_dl_rrc_message_data_t *data = (deliver_dl_rrc_message_data_t *)deliver_pdu_data;
    data->dl_rrc->rrc_container = (uint8_t *)buf;
    data->dl_rrc->rrc_container_length = size;
    DevAssert(data->dl_rrc->srb_id == srb_id);
    data->rrc->mac_rrc.dl_rrc_message_transfer(data->dl_rrc);  
  }
}

void nr_rrc_transfer_protected_rrc_message(const gNB_RRC_INST *rrc, const gNB_RRC_UE_t *ue_p, uint8_t srb_id, const uint8_t* buffer, int size)
{
  DevAssert(size > 0);
  f1_ue_data_t ue_data = cu_get_f1_ue_data(ue_p->rnti);
  f1ap_dl_rrc_message_t dl_rrc = {.gNB_CU_ue_id = ue_p->rrc_ue_id, .gNB_DU_ue_id = ue_data.secondary_ue, .srb_id = srb_id};
  deliver_dl_rrc_message_data_t data = {.rrc = rrc, .dl_rrc = &dl_rrc};
  nr_pdcp_data_req_srb(ue_p->rrc_ue_id, srb_id, rrc_gNB_mui++, size, (unsigned char *const)buffer, rrc_deliver_dl_rrc_message, &data);
}

static int get_dl_band(const f1ap_served_cell_info_t *cell_info)
{
  return cell_info->mode == F1AP_MODE_TDD ? cell_info->tdd.freqinfo.band : cell_info->fdd.dl_freqinfo.band;
}

static int get_ul_band(const f1ap_served_cell_info_t *cell_info)
{
  return cell_info->mode == F1AP_MODE_TDD ? cell_info->tdd.freqinfo.band : cell_info->fdd.ul_freqinfo.band;
}

static int get_ssb_scs(const f1ap_served_cell_info_t *cell_info)
{
  return cell_info->mode == F1AP_MODE_TDD ? cell_info->tdd.tbw.scs : cell_info->fdd.dl_tbw.scs;
}

static int get_dl_arfcn(const f1ap_served_cell_info_t *cell_info)
{
  return cell_info->mode == F1AP_MODE_TDD ? cell_info->tdd.freqinfo.arfcn : cell_info->fdd.dl_freqinfo.arfcn;
}

static int get_dl_bw(const f1ap_served_cell_info_t *cell_info)
{
  return cell_info->mode == F1AP_MODE_TDD ? cell_info->tdd.tbw.nrb : cell_info->fdd.dl_tbw.nrb;
}

static int get_ssb_arfcn(const f1ap_served_cell_info_t *cell_info, const NR_MIB_t *mib, const NR_SIB1_t *sib1)
{
  DevAssert(cell_info != NULL && sib1 != NULL && mib != NULL);
  const NR_FrequencyInfoDL_SIB_t* freq_info = &sib1->servingCellConfigCommon->downlinkConfigCommon.frequencyInfoDL;
  AssertFatal(freq_info->scs_SpecificCarrierList.list.count == 1, "cannot handle more than one carrier, but has %d\n", freq_info->scs_SpecificCarrierList.list.count);
  AssertFatal(freq_info->scs_SpecificCarrierList.list.array[0]->offsetToCarrier == 0, "cannot handle offsetToCarrier != 0, but is %ld\n", freq_info->scs_SpecificCarrierList.list.array[0]->offsetToCarrier);

  long offsetToPointA = freq_info->offsetToPointA;
  long kssb = mib->ssb_SubcarrierOffset;
  uint32_t dl_arfcn = get_dl_arfcn(cell_info);
  int scs = get_ssb_scs(cell_info);
  int band = get_dl_band(cell_info);
  uint64_t scaling = band < 100 ? 1 : 3;

  uint64_t freqpointa = from_nrarfcn(band, scs, dl_arfcn);
  // offsetToPointA and kSSB are both on 15kHz SCS (see 38.211 sections 7.4.3.1
  // and 4.4.4.2)
  // SSB uses the SCS of the cell and is 20 RBs wide, so use 10
  uint64_t freqssb = freqpointa + scaling * 15000 * (offsetToPointA * 12 + kssb)  + 10ll * 12 * (1 << scs) * 15000;
  int bw_index = get_supported_band_index(scs, band, get_dl_bw(cell_info));
  int band_size_hz = get_supported_bw_mhz(band > 256 ? FR2 : FR1, bw_index) * 1000 * 1000;
  uint32_t ssb_arfcn = to_nrarfcn(band, freqssb, scs, band_size_hz);

  LOG_W(RRC, "freqpointa %ld Hz/%d offsetToPointA %ld kssb %ld scs %d band %d band_size_hz %d freqssb %ld Hz/%d\n", freqpointa, dl_arfcn, offsetToPointA, kssb, scs, band, band_size_hz, freqssb, ssb_arfcn);

  if (RC.nrmac) {
    // debugging: let's test this is the correct ARFCN
    // in the CU, we have no access to the SSB ARFCN and therefore need to
    // compute it ourselves. If we are running in monolithic, though, we have
    // access to the MAC structures and hence can read and compare to the
    // original SSB ARFCN. If the below creates problems, it can safely be
    // taken out (but the reestablishment will likely not work).
    const NR_ServingCellConfigCommon_t *scc = RC.nrmac[0]->common_channels[0].ServingCellConfigCommon;
    uint32_t scc_ssb_arfcn = *scc->downlinkConfigCommon->frequencyInfoDL->absoluteFrequencySSB;
    AssertFatal(ssb_arfcn == scc_ssb_arfcn, "fuck: SCC SSB ARFCN original %d vs. computed %d\n", scc_ssb_arfcn, ssb_arfcn);
  }

  return ssb_arfcn;
}

///---------------------------------------------------------------------------------------------------------------///
///---------------------------------------------------------------------------------------------------------------///

static void init_NR_SI(gNB_RRC_INST *rrc)
{

  // static int mac_common_config_done[MAX_NUM_CCs] = {0}; //TODO W38: to check if needed
  // LOG_D(NR_RRC,"%s()\n",__FUNCTION__);

  // // From 3GPP 38331, NOTE 1:	Upper layers provide the 5G-S-TMSI if the UE is registered in the TA of the current cell."
  // if (RC.ss.mode == SS_HWTMODEM) {
  //   configuration->tac = time(NULL); //TODO W38: to find place to set it
  // }

  //   if (NODE_IS_DU(rrc->node_type) || NODE_IS_MONOLITHIC(rrc->node_type)) //TODO W38: to set mib/sib1 from ttcn in right place
  //   if(rrc->carrier[CC_id].mib == NULL){
  //     rrc->carrier[CC_id].mib = get_new_MIB_NR(rrc->carrier[CC_id].servingcellconfigcommon);
  //   }else{
  //     reconfig_MIB_NR(rrc->carrier[CC_id].mib, rrc->carrier[CC_id].servingcellconfigcommon);
  //   }

  // if((get_softmodem_params()->sa) && ( (NODE_IS_DU(rrc->node_type) || NODE_IS_MONOLITHIC(rrc->node_type)))) {
  //   if ( rrc->carrier[CC_id].SIB1 == NULL){
  //     NR_BCCH_DL_SCH_Message_t *sib1 = get_SIB1_NR(configuration);
     
  //     rrc->carrier[CC_id].SIB1 = calloc(NR_MAX_SIB_LENGTH / 8, sizeof(*(rrc->carrier[CC_id].SIB1)));
  //     AssertFatal(rrc->carrier[CC_id].SIB1 != NULL, "out of memory\n");
  //     rrc->carrier[CC_id].sizeof_SIB1 = encode_SIB1_NR(sib1, rrc->carrier[CC_id].SIB1, NR_MAX_SIB_LENGTH / 8);
  //     rrc->carrier[CC_id].siblock1 = sib1;
      
  //     nr_mac_config_sib1(RC.nrmac[rrc->module_id], sib1);
  //   }else{
      
  //     reconfig_SIB1_NR(rrc->carrier[CC_id].siblock1,configuration);
  //     memset(rrc->carrier[CC_id].SIB1,0,rrc->carrier[CC_id].sizeof_SIB1);
  //     rrc->carrier[CC_id].sizeof_SIB1 = encode_SIB1_NR(rrc->carrier[CC_id].siblock1, rrc->carrier[CC_id].SIB1, NR_MAX_SIB_LENGTH / 8);
  //     nr_mac_config_sib1(RC.nrmac[rrc->module_id], rrc->carrier[CC_id].siblock1);
  //   }
  // }
  if (!NODE_IS_DU(rrc->node_type)) {
    for(uint8_t CC_id=0;CC_id < MAX_NUM_CCs; CC_id++){
      rrc->carrier[CC_id].SIB23 = (uint8_t *) malloc16(100);
      AssertFatal(rrc->carrier[CC_id].SIB23 != NULL, "cannot allocate memory for SIB");
      rrc->carrier[CC_id].sizeof_SIB23 = do_SIB23_NR(&(rrc->carrier[CC_id]), &rrc->configuration[CC_id]);
      LOG_I(NR_RRC,"do_SIB23_NR, size %d \n ", rrc->carrier[CC_id].sizeof_SIB23);
      AssertFatal(rrc->carrier[CC_id].sizeof_SIB23 != 255,"FATAL, RC.nrrrc[mod].carrier[CC_id].sizeof_SIB23 == 255");
    }
  }

  

  LOG_I(NR_RRC,"Done init_NR_SI\n");
    // /* set flag to indicate that cell information is configured. This is required //TODO W38: to check
  //  * in DU to trigger F1AP_SETUP procedure */
  // pthread_mutex_lock(&rrc->cell_info_mutex);
  // rrc->cell_info_configured=1;
  // pthread_mutex_unlock(&rrc->cell_info_mutex);

  if (get_softmodem_params()->phy_test > 0 || get_softmodem_params()->do_ra > 0) {
    AssertFatal(NODE_IS_MONOLITHIC(rrc->node_type), "phy_test and do_ra only work in monolithic\n");
    rrc_gNB_ue_context_t *ue_context_p = rrc_gNB_allocate_new_ue_context(rrc);
    gNB_RRC_UE_t *UE = &ue_context_p->ue_context;
    UE->spCellConfig = calloc(1, sizeof(struct NR_SpCellConfig));
    UE->spCellConfig->spCellConfigDedicated = RC.nrmac[0]->common_channels[0].pre_ServingCellConfig;
    LOG_I(NR_RRC,"Adding new user (%p)\n",ue_context_p);
    if (!NODE_IS_CU(RC.nrrrc[0]->node_type)) {
      rrc_add_nsa_user(rrc,ue_context_p,NULL);
    }
  }
  LOG_I(NR_RRC, "swetank: Exit function %s\n", __FUNCTION__);
}

static void rrc_gNB_CU_DU_init(gNB_RRC_INST *rrc)
{
  switch (rrc->node_type) {
    case ngran_gNB_CUCP:
      mac_rrc_dl_f1ap_init(&rrc->mac_rrc);
      cucp_cuup_message_transfer_e1ap_init(rrc);
      break;
    case ngran_gNB_CU:
      mac_rrc_dl_f1ap_init(&rrc->mac_rrc);
      cucp_cuup_message_transfer_direct_init(rrc);
      break;
    case ngran_gNB:
      mac_rrc_dl_direct_init(&rrc->mac_rrc);
      cucp_cuup_message_transfer_direct_init(rrc);
       break;
    case ngran_gNB_DU:
      /* silently drop this, as we currently still need the RRC at the DU. As
       * soon as this is not the case anymore, we can add the AssertFatal() */
      //AssertFatal(1==0,"nothing to do for DU\n");
      break;
    default:
      AssertFatal(0 == 1, "Unknown node type %d\n", rrc->node_type);
      break;
  }
  cu_init_f1_ue_data();
}

void openair_rrc_gNB_configuration(gNB_RRC_INST *rrc, NRRrcConfigurationReqList *configuration)
{
  AssertFatal(rrc != NULL, "RC.nrrrc not initialized!");
  AssertFatal(NUMBER_OF_UE_MAX < (module_id_t)0xFFFFFFFFFFFFFFFF, " variable overflow");
  AssertFatal(configuration!=NULL,"configuration input is null\n");
  static int init_config_flag = 1;  //W38: this is flag is used to avoid memory leakage? but was not working as expected?
  rrc->module_id = 0;
  
  if (init_config_flag){
    rrc_gNB_CU_DU_init(rrc);
    uid_linear_allocator_init(&rrc->uid_allocator);
     RB_INIT(&rrc->rrc_ue_head);
  }
 
  // for(int CC_id = 0; CC_id < MAX_NUM_CCs; CC_id++) {
  //   rrc->configuration[CC_id] = configuration->configuration[CC_id];
    
  // }
  

  //TODO w38: come back here to fix it. memory leakage and update ul_tda
  // for(int CC_id = 0; CC_id < MAX_NUM_CCs; CC_id++) {
  //     nr_rrc_config_ul_tda(RC.nrmac[0]->common_channels[CC_id].ServingCellConfigCommon,RC.nrmac[0]->radio_config[CC_id].minRXTXTIME); //TODO W38: calling the function again, cauing memory leakage
  //   }
  if(!init_config_flag)
  nr_mac_update_config();
  // System Information INIT
  init_NR_SI(rrc); //TODO W38 init_NR_SI need to handle all cells done
  
  for(int CC_id = 0; CC_id < MAX_NUM_CCs; CC_id++) {
    rrc_mac_config_dedicate_scheduling(rrc->module_id, rrc->carrier[CC_id].dcchDtchConfig);
  }
  if (init_config_flag){
    for(int CC_id = 0; CC_id < MAX_NUM_CCs; CC_id++){
      rrc->carrier[CC_id].sizeof_paging = 0;
      rrc->carrier[CC_id].paging = (uint8_t *) calloc(1, 256);
      init_config_flag = 0;
    }
  }
  return;
} // END openair_rrc_gNB_configuration

char rrc_gNB_rblist_configuration(const module_id_t gnb_mod_idP,NRRrcRblistCfgReq *RblistConfig)
{
  int CC_id = RblistConfig->cell_index;

  LOG_A(NR_RRC,"Inside rrc_gNB_rblist_configuration, CC_id: %d \n",CC_id);
  AssertFatal(RblistConfig!=NULL,"RB list configuration input is null\n");
  AssertFatal(RblistConfig->rb_count <= MAX_NR_RBS ,"RB Count exceed the Max RBs Supported");

  for(int i=0;i<RblistConfig->rb_count;i++)
  {
    LOG_A(NR_RRC,"%s config RB Id:%d \n",__FUNCTION__,RblistConfig->rb_list[i].RbId);
    int rbIndex = RblistConfig->rb_list[i].RbId; /*RB ID is used as a unique index in RB Array inside RC*/
    AssertFatal(rbIndex <= MAX_NR_RBS, "RB Index exceed the Max RBs Supported");
    NRRBConfig * rb_config =&RC.NR_RB_Config[CC_id][rbIndex];
    if(rb_config->isRBConfigValid){
      //Free old RBConfig storage
      if(rb_config->Sdap){
        ASN_STRUCT_FREE(asn_DEF_NR_SDAP_Config,rb_config->Sdap);
        rb_config->Sdap = NULL;
      }
      if(rb_config->Pdcp){
        ASN_STRUCT_FREE(asn_DEF_NR_PDCP_Config,rb_config->Pdcp);
        rb_config->Pdcp = NULL;
      }
      if(rb_config->pdcpTransparentSN_Size){
        FREEMEM(rb_config->pdcpTransparentSN_Size);
        rb_config->pdcpTransparentSN_Size = NULL;
      }
      if(rb_config->RlcBearer){
        ASN_STRUCT_FREE(asn_DEF_NR_RLC_BearerConfig,rb_config->RlcBearer);
        rb_config->RlcBearer = NULL;
      }
      if(rb_config->DiscardULData){
        FREEMEM(rb_config->DiscardULData);
        rb_config->DiscardULData = NULL;
      }
      rb_config->isRBConfigValid = false;
    }

    rb_config->Sdap = RblistConfig->rb_list[i].RbConfig.Sdap;
    rb_config->Pdcp = RblistConfig->rb_list[i].RbConfig.Pdcp;
    rb_config->pdcpTransparentSN_Size = RblistConfig->rb_list[i].RbConfig.pdcpTransparentSN_Size;
    rb_config->RlcBearer = RblistConfig->rb_list[i].RbConfig.RlcBearer;
    rb_config->DiscardULData = RblistConfig->rb_list[i].RbConfig.DiscardULData;
    rb_config->isRBConfigValid = true;
  }
  return 0;
}

static void rrc_gNB_process_AdditionRequestInformation(const module_id_t gnb_mod_idP, x2ap_ENDC_sgnb_addition_req_t *m)
{
  struct NR_CG_ConfigInfo *cg_configinfo = NULL;
  asn_dec_rval_t dec_rval = uper_decode_complete(NULL,
                            &asn_DEF_NR_CG_ConfigInfo,
                            (void **)&cg_configinfo,
                            (uint8_t *)m->rrc_buffer,
                            (int) m->rrc_buffer_size);//m->rrc_buffer_size);
  gNB_RRC_INST         *rrc=RC.nrrrc[gnb_mod_idP];

  if ((dec_rval.code != RC_OK) && (dec_rval.consumed == 0)) {
    AssertFatal(1==0,"NR_UL_DCCH_MESSAGE decode error\n");
    // free the memory
    SEQUENCE_free(&asn_DEF_NR_CG_ConfigInfo, cg_configinfo, 1);
    return;
  }

  xer_fprint(stdout,&asn_DEF_NR_CG_ConfigInfo, cg_configinfo);
  // recreate enough of X2 EN-DC Container
  AssertFatal(cg_configinfo->criticalExtensions.choice.c1->present == NR_CG_ConfigInfo__criticalExtensions__c1_PR_cg_ConfigInfo,
              "ueCapabilityInformation not present\n");
  parse_CG_ConfigInfo(rrc,cg_configinfo,m);
  LOG_A(NR_RRC, "Successfully parsed CG_ConfigInfo of size %zu bits. (%zu bytes)\n",
        dec_rval.consumed, (dec_rval.consumed +7/8));
}

//-----------------------------------------------------------------------------
unsigned int rrc_gNB_get_next_transaction_identifier_reset(module_id_t gnb_mod_idP, bool reset)
//-----------------------------------------------------------------------------
{
  static unsigned int transaction_id[NUMBER_OF_gNB_MAX] = {0};
  if (reset) {
    __atomic_store_n(&transaction_id[gnb_mod_idP], 0, __ATOMIC_SEQ_CST);
    LOG_T(NR_RRC, "reset xid to 0\n");
    return 0;
  }
  // used also in NGAP thread, so need thread safe operation
  unsigned int tmp = __atomic_add_fetch(&transaction_id[gnb_mod_idP], 1, __ATOMIC_SEQ_CST);
  tmp %= NR_RRC_TRANSACTION_IDENTIFIER_NUMBER;
  LOG_T(NR_RRC, "generated xid is %d\n", tmp);
  return tmp;
}

unsigned int rrc_gNB_get_next_transaction_identifier(module_id_t gnb_mod_idP)
{
  return rrc_gNB_get_next_transaction_identifier_reset(gnb_mod_idP, false);
}

static NR_SRB_ToAddModList_t *createSRBlist(gNB_RRC_UE_t *ue, bool reestablish)
{
  if (!ue->Srb[1].Active) {
    LOG_E(NR_RRC, "Call SRB list while SRB1 doesn't exist\n");
    return NULL;
  }
  NR_SRB_ToAddModList_t *list = CALLOC(sizeof(*list), 1);
  for (int i = 0; i < maxSRBs; i++)
    if (ue->Srb[i].Active) {
      LOG_W(NR_RRC, ">>> %s: ue->Srb[%d].Active\n", __FUNCTION__, i);
      asn1cSequenceAdd(list->list, NR_SRB_ToAddMod_t, srb);
      srb->srb_Identity = i;
      if (reestablish && i == 2) {
        asn1cCallocOne(srb->reestablishPDCP, NR_SRB_ToAddMod__reestablishPDCP_true);
      }
    }
  return list;
}

static NR_DRB_ToAddModList_t *createDRBlist(gNB_RRC_UE_t *ue, bool reestablish)
{
  NR_DRB_ToAddMod_t *DRB_config = NULL;
  NR_DRB_ToAddModList_t *DRB_configList = CALLOC(sizeof(*DRB_configList), 1);

  for (int i = 0; i < MAX_DRBS_PER_UE; i++) {
    if (ue->established_drbs[i].status != DRB_INACTIVE) {
      DRB_config = generateDRB_ASN1(&ue->established_drbs[i]);
      if (reestablish) {
        ue->established_drbs[i].reestablishPDCP = NR_DRB_ToAddMod__reestablishPDCP_true;
        asn1cCallocOne(DRB_config->reestablishPDCP, NR_DRB_ToAddMod__reestablishPDCP_true);
      }
      asn1cSeqAdd(&DRB_configList->list, DRB_config);
    }
  }
  return DRB_configList;
}

static void freeSRBlist(NR_SRB_ToAddModList_t *l)
{
  if (l) {
    for (int i = 0; i < l->list.count; i++)
      free(l->list.array[i]);
    free(l);
  } else
    LOG_E(NR_RRC, "Call free SRB list on NULL pointer\n");
}

int rrc_gNB_process_SS_PAGING_IND(MessageDef *msg_p, const char *msg_name, instance_t instance)
{
  const unsigned int Ttab[4] = {32,64,128,256};
  uint8_t Tc;
  /* uint8_t Tue; */
  uint32_t pfoffset;
  uint32_t N;  /* N: min(T,nB). total count of PF in one DRX cycle */
  uint32_t Ns = 0;  /* Ns: max(1,nB/T) */
  uint8_t i_s;  /* i_s = floor(UE_ID/N) mod Ns */
  uint32_t T;  /* DRX cycle */
  uint8_t CC_id = SS_NR_PAGING_IND(msg_p).cell_index;
  frame_type_t frame_type = RC.gNB[instance]->frame_parms.frame_type;
  uint32_t length;
  uint8_t buffer[RRC_BUF_SIZE];
  uint8_t *message_buffer;
  MessageDef *message_p;
    
  struct NR_SIB1 *sib1 = RC.nrmac[0]->common_channels[CC_id].sib1->message.choice.c1->choice.systemInformationBlockType1;
  
  // TODO: set configuration from TTCN SYS message

  /* get default DRX cycle from configuration */
  Tc = sib1->servingCellConfigCommon->downlinkConfigCommon.pcch_Config.defaultPagingCycle;

  /* set T = min(Tc,Tue) */
  T = Ttab[Tc];
  /* set N = PCCH-Config->nAndPagingFrameOffset */
  switch (sib1->servingCellConfigCommon->downlinkConfigCommon.pcch_Config.nAndPagingFrameOffset.present) {
    case NR_PCCH_Config__nAndPagingFrameOffset_PR_oneT:
      N = T;
      pfoffset = 0;
      break;
    case NR_PCCH_Config__nAndPagingFrameOffset_PR_halfT:
      N = T/2;
      pfoffset = 1;
      break;
    case NR_PCCH_Config__nAndPagingFrameOffset_PR_quarterT:
      N = T/4;
      pfoffset = 3;
      break;
    case NR_PCCH_Config__nAndPagingFrameOffset_PR_oneEighthT:
      N = T/8;
      pfoffset = 7;
      break;
    case NR_PCCH_Config__nAndPagingFrameOffset_PR_oneSixteenthT:
      N = T/16;
      pfoffset = 15;
      break;
    default:
      LOG_E(NR_RRC, "[gNB %ld] In rrc_gNB_process_SS_PAGING_IND:  pfoffset error (pfoffset %d)\n",
            instance, sib1->servingCellConfigCommon->downlinkConfigCommon.pcch_Config.nAndPagingFrameOffset.present);
      return (-1);

  }

  switch (sib1->servingCellConfigCommon->downlinkConfigCommon.pcch_Config.ns) {
    case NR_PCCH_Config__ns_four:
      if(*sib1->servingCellConfigCommon->downlinkConfigCommon.initialDownlinkBWP.pdcch_ConfigCommon->choice.setup->pagingSearchSpace == 0){
        LOG_E(NR_RRC, "[gNB %ld] In rrc_gNB_process_SS_PAGING_IND:  ns error only 1 or 2 is allowed when pagingSearchSpace is 0\n",
              instance);
        return (-1);
      } else {
        Ns = 4;
      }
      break;
    case NR_PCCH_Config__ns_two:
      Ns = 2;
      break;
    case NR_PCCH_Config__ns_one:
      Ns = 1;
      break;
    default:
      LOG_E(NR_RRC, "[gNB %ld] In rrc_gNB_process_SS_PAGING_IND: ns error (ns %ld)\n",
            instance, sib1->servingCellConfigCommon->downlinkConfigCommon.pcch_Config.ns);
      return (-1);
  }

  /* insert data to UE_PF_PO or update data in UE_PF_PO */
  pthread_mutex_lock(&ue_pf_po_mutex);
  uint8_t i = 0;

  for (i = 0; i < MAX_MOBILES_PER_GNB; i++) {
    if ((UE_PF_PO[CC_id][i].enable_flag == true && UE_PF_PO[CC_id][i].ue_index_value == (uint16_t)(SS_NR_PAGING_IND(msg_p).ue_index_value)) || (UE_PF_PO[CC_id][i].enable_flag != true)) {
      /* set T = min(Tc,Tue) */
      UE_PF_PO[CC_id][i].T = T;
      /* set UE_ID */
      UE_PF_PO[CC_id][i].ue_index_value = (uint16_t)SS_NR_PAGING_IND(msg_p).ue_index_value;
      /* calculate PF and PO */
      /* set PF_min and PF_offset: (SFN + PF_offset) mod T = (T div N)*(UE_ID mod N) */
      /* UE_PF_PO[CC_id][i].PF_min = (T / N) * (UE_PF_PO[CC_id][i].ue_index_value % N); */
      UE_PF_PO[CC_id][i].PF_min = (SS_NR_PAGING_IND(msg_p).sfn % T) + (UE_PF_PO[CC_id][i].ue_index_value % N);
      /* UE_PF_PO[CC_id][i].PF_min =  SS_NR_PAGING_IND(msg_p).sfn % T; */
      UE_PF_PO[CC_id][i].PF_offset = pfoffset;
      /* set i_s */
      /* i_s = floor(UE_ID/N) mod Ns */
      i_s = (uint8_t)((UE_PF_PO[CC_id][i].ue_index_value / N) % Ns);
      UE_PF_PO[CC_id][i].i_s = i_s;

      /* if (Ns == 1)
      {
        UE_PF_PO[CC_id][i].PO = (frame_type == FDD) ? 9 : 0;
      }
      else if (Ns == 2)
      {
        UE_PF_PO[CC_id][i].PO = (frame_type == FDD) ? (4 + (5 * i_s)) : (5 * i_s);
      }
      else if (Ns == 4)
      {
        UE_PF_PO[CC_id][i].PO = (frame_type == FDD) ? (4 * (i_s & 1) + (5 * (i_s >> 1))) : ((i_s & 1) + (5 * (i_s >> 1)));
      } */
      // TODO: ???
      UE_PF_PO[CC_id][i].PO = SS_NR_PAGING_IND(msg_p).slot;

      if (UE_PF_PO[CC_id][i].enable_flag == true) {
        //paging exist UE log
        LOG_D(NR_RRC, "[gNB %ld] CC_id %d In rrc_gNB_process_SS_PAGING_IND: Update exist UE %d, T %d, N %d, PF %d, i_s %d, PF_offset %d, PO %d\n", instance, CC_id, UE_PF_PO[CC_id][i].ue_index_value,
              T, N, UE_PF_PO[CC_id][i].PF_min, UE_PF_PO[CC_id][i].i_s, UE_PF_PO[CC_id][i].PF_offset, UE_PF_PO[CC_id][i].PO);
      } else {
        /* set enable_flag */
        UE_PF_PO[CC_id][i].enable_flag = true;
        //paging new UE log
        LOG_D(NR_RRC, "[gNB %ld] CC_id %d In rrc_gNB_process_SS_PAGING_IND: Insert a new UE %d, T %d, N %d, PF %d, i_s %d, PF_offset %d, PO %d\n", instance, CC_id, UE_PF_PO[CC_id][i].ue_index_value,
              T, N, UE_PF_PO[CC_id][i].PF_min, UE_PF_PO[CC_id][i].i_s, UE_PF_PO[CC_id][i].PF_offset, UE_PF_PO[CC_id][i].PO);
      }
      break;
    }
  }

  pthread_mutex_unlock(&ue_pf_po_mutex);

  /* Create message for PDCP (DLInformationTransfer_t) */
  length = do_NR_Paging(instance, buffer, NULL, SS_NR_PAGING_IND(msg_p).num_paging_record,  SS_NR_PAGING_IND(msg_p).paging_recordList);

  if (SS_NR_PAGING_IND(msg_p).paging_recordList) {
    free(SS_NR_PAGING_IND(msg_p).paging_recordList);
    SS_NR_PAGING_IND(msg_p).paging_recordList = NULL;
  }

  if (length == -1) {
    LOG_I(NR_RRC, "do_NR_Paging error\n");
    return -1;
  }

  message_p = itti_alloc_new_message(TASK_RRC_GNB, instance, RRC_PCCH_DATA_REQ);
  message_buffer = itti_malloc(TASK_RRC_GNB, TASK_RRC_GNB, length);
  memcpy(message_buffer, buffer, length);
  RRC_PCCH_DATA_REQ(message_p).sdu_size = length;
  RRC_PCCH_DATA_REQ(message_p).sdu_p = message_buffer;
  RRC_PCCH_DATA_REQ(message_p).mode = 0; /* not used */
  RRC_PCCH_DATA_REQ(message_p).rnti = P_RNTI;
  RRC_PCCH_DATA_REQ(message_p).ue_index = 0;
  RRC_PCCH_DATA_REQ(message_p).CC_id = CC_id;
  LOG_A(NR_RRC, "[gNB %ld] CC_id %d In rrc_gNB_process_SS_PAGING_IND: send encoded buffer to RRC GNB buffer_size %d\n", instance, CC_id, length);
  itti_send_msg_to_task(TASK_RRC_GNB, instance, message_p);

  return 0;
}

//-----------------------------------------------------------------------------
static void rrc_gNB_generate_RRCSetup(instance_t instance,
                                      rnti_t rnti,
                                      rrc_gNB_ue_context_t *const ue_context_pP,
                                      const uint8_t *masterCellGroup,
                                      int masterCellGroup_len,
                                      const int CC_id)
//-----------------------------------------------------------------------------
{
  LOG_I(NR_RRC, "rrc_gNB_generate_RRCSetup for RNTI %04x\n", rnti);

  gNB_RRC_UE_t *ue_p = &ue_context_pP->ue_context;
  gNB_RRC_INST *rrc = RC.nrrrc[instance];
  unsigned char buf[1024];
  uint8_t xid = rrc_gNB_get_next_transaction_identifier_reset(instance, true);
  ue_p->xids[xid] = RRC_SETUP;
  NR_SRB_ToAddModList_t *SRBs = createSRBlist(ue_p, false);

  int size = do_RRCSetup(ue_context_pP, buf, xid, masterCellGroup, masterCellGroup_len, &rrc->configuration[CC_id], SRBs);
  AssertFatal(size > 0, "do_RRCSetup failed\n");
  AssertFatal(size <= 1024, "memory corruption\n");

  LOG_DUMPMSG(NR_RRC, DEBUG_RRC,
              (char *)buf,
              size,
              "[MSG] RRC Setup\n");
  nr_pdcp_add_srbs(true, rnti, SRBs, 0, NULL, NULL);

  freeSRBlist(SRBs);
  f1_ue_data_t ue_data = cu_get_f1_ue_data(ue_p->rnti);
  f1ap_dl_rrc_message_t dl_rrc = {
    .gNB_CU_ue_id = ue_p->rrc_ue_id,
    .gNB_DU_ue_id = ue_data.secondary_ue,
    .nr_cellid = CC_id,
    .rrc_container = buf,
    .rrc_container_length = size,
    .srb_id = CCCH
  };
  rrc->mac_rrc.dl_rrc_message_transfer(&dl_rrc);
}

static void rrc_gNB_generate_RRCReject(module_id_t module_id, rrc_gNB_ue_context_t *const ue_context_pP)
//-----------------------------------------------------------------------------
{
  LOG_I(NR_RRC, "rrc_gNB_generate_RRCReject \n");
  gNB_RRC_INST *rrc = RC.nrrrc[module_id];
  gNB_RRC_UE_t *ue_p = &ue_context_pP->ue_context;

  unsigned char buf[1024];
  int size = do_RRCReject(module_id, buf);
  AssertFatal(size > 0, "do_RRCReject failed\n");
  AssertFatal(size <= 1024, "memory corruption\n");

  LOG_DUMPMSG(NR_RRC, DEBUG_RRC,
              (char *)buf,
              size,
              "[MSG] RRCReject \n");
  LOG_I(NR_RRC, " [RAPROC] ue %04x Logical Channel DL-CCCH, Generating NR_RRCReject (bytes %d)\n", ue_p->rnti, size);

  f1_ue_data_t ue_data = cu_get_f1_ue_data(ue_p->rnti);
  f1ap_dl_rrc_message_t dl_rrc = {
    .gNB_CU_ue_id = ue_p->rrc_ue_id,
    .gNB_DU_ue_id = ue_data.secondary_ue,
    .rrc_container = buf,
    .rrc_container_length = size,
    .srb_id = CCCH,
    .execute_duplication  = 1,
    .RAT_frequency_priority_information.en_dc = 0
  };
  rrc->mac_rrc.dl_rrc_message_transfer(&dl_rrc);
}

//-----------------------------------------------------------------------------
/*
* Process the rrc setup complete message from UE (SRB1 Active)
*/
static void rrc_gNB_process_RRCSetupComplete(const protocol_ctxt_t *const ctxt_pP, rrc_gNB_ue_context_t *ue_context_pP, NR_RRCSetupComplete_IEs_t *rrcSetupComplete)
//-----------------------------------------------------------------------------
{
  LOG_A(NR_RRC, PROTOCOL_NR_RRC_CTXT_UE_FMT" [RAPROC] Logical Channel UL-DCCH, " "processing NR_RRCSetupComplete from UE (SRB1 Active)\n",
      PROTOCOL_NR_RRC_CTXT_UE_ARGS(ctxt_pP));
  ue_context_pP->ue_context.Srb[1].Active = 1;
  ue_context_pP->ue_context.Srb[2].Active = 0;
  ue_context_pP->ue_context.StatusRrc = NR_RRC_CONNECTED;

  if (RC.ss.mode >= SS_SOFTMODEM) return;

  AssertFatal(ctxt_pP->rntiMaybeUEid == ue_context_pP->ue_context.rrc_ue_id, "logic bug: inconsistent IDs, must use CU UE ID!\n");
  if (get_softmodem_params()->sa) {
    rrc_gNB_send_NGAP_NAS_FIRST_REQ(ctxt_pP, ue_context_pP, rrcSetupComplete);
  } else {
    rrc_gNB_generate_SecurityModeCommand(ctxt_pP, ue_context_pP);
  }
}

//-----------------------------------------------------------------------------
static void rrc_gNB_generate_defaultRRCReconfiguration(const protocol_ctxt_t *const ctxt_pP, rrc_gNB_ue_context_t *ue_context_pP)
//-----------------------------------------------------------------------------
{
  gNB_RRC_INST *rrc = RC.nrrrc[ctxt_pP->module_id];
  gNB_RRC_UE_t *ue_p = &ue_context_pP->ue_context;
  const int CC_id = ue_p->primaryCC_id;
  AssertFatal(ue_p->nb_of_pdusessions == 0, "logic bug: PDU sessions present before RRC Connection established\n");
  uint8_t xid = rrc_gNB_get_next_transaction_identifier(ctxt_pP->module_id);
  ue_p->xids[xid] = RRC_DEFAULT_RECONF;

  const nr_rrc_du_container_t *du = rrc->du;
  DevAssert(du != NULL);

  struct NR_RRCReconfiguration_v1530_IEs__dedicatedNAS_MessageList *dedicatedNAS_MessageList = CALLOC(1, sizeof(*dedicatedNAS_MessageList));

  /* Add all NAS PDUs to the list */
  for (int i = 0; i < ue_p->nb_of_pdusessions; i++) {
    if (ue_p->pduSession[i].param.nas_pdu.buffer != NULL) {
      asn1cSequenceAdd(dedicatedNAS_MessageList->list, NR_DedicatedNAS_Message_t, msg);
      OCTET_STRING_fromBuf(msg, (char *)ue_p->pduSession[i].param.nas_pdu.buffer, ue_p->pduSession[i].param.nas_pdu.length);
    }

    ue_p->pduSession[i].status = PDU_SESSION_STATUS_DONE;
    LOG_D(NR_RRC, "setting the status for the default DRB (index %d) to (%d,%s)\n", i, ue_p->pduSession[i].status, "PDU_SESSION_STATUS_DONE");
  }

  if (ue_p->nas_pdu.length) {
    asn1cSequenceAdd(dedicatedNAS_MessageList->list, NR_DedicatedNAS_Message_t, msg);
    OCTET_STRING_fromBuf(msg, (char *)ue_p->nas_pdu.buffer, ue_p->nas_pdu.length);
  }

  /* If list is empty free the list and reset the address */
  if (dedicatedNAS_MessageList->list.count == 0) {
    free(dedicatedNAS_MessageList);
    dedicatedNAS_MessageList = NULL;
  }

  AssertFatal(du->setup_req->num_cells_available == 1, "cannot handle more than one cell, but have %d\n", du->setup_req->num_cells_available);
  f1ap_served_cell_info_t *cell_info = &du->setup_req->cell[0].info;
  int scs = get_ssb_scs(cell_info);
  int band = get_dl_band(cell_info);
  uint32_t ssb_arfcn = get_ssb_arfcn(cell_info, du->mib, du->sib1);
  NR_MeasConfig_t *measconfig = get_defaultMeasConfig(ssb_arfcn, band, scs);

  uint8_t buffer[RRC_BUF_SIZE] = {0};
  int size = do_RRCReconfiguration(ue_p, //TODO: to check ccid
                                   buffer,
                                   RRC_BUF_SIZE,
                                   xid,
                                   NULL, //*SRB_configList2,
                                   NULL, //*DRB_configList,
                                   NULL,
                                   NULL,
                                   measconfig,
                                   dedicatedNAS_MessageList,
                                   ue_p->masterCellGroup);
  AssertFatal(size > 0, "cannot encode RRCReconfiguration in %s()\n", __func__);
  LOG_W(NR_RRC, "do_RRCReconfiguration(): size %d\n", size);
  free_defaultMeasConfig(measconfig);

  if (LOG_DEBUGFLAG(DEBUG_ASN1)) {
    xer_fprint(stdout, &asn_DEF_NR_CellGroupConfig, ue_p->masterCellGroup);
  }

  // suspicious if it is always malloced before ?
  free(ue_p->nas_pdu.buffer);

  LOG_DUMPMSG(NR_RRC, DEBUG_RRC,(char *)buffer, size, "[MSG] RRC Reconfiguration\n");

  /* Free all NAS PDUs */
  for (int i = 0; i < ue_p->nb_of_pdusessions; i++) {
    if (ue_p->pduSession[i].param.nas_pdu.buffer != NULL) {
      free(ue_p->pduSession[i].param.nas_pdu.buffer);
      ue_p->pduSession[i].param.nas_pdu.buffer = NULL;
    }
  }

  LOG_I(NR_RRC, "[gNB %d] Frame %d, Logical Channel DL-DCCH, Generate NR_RRCReconfiguration (bytes %d, UE id %x)\n",
          ctxt_pP->module_id,
          ctxt_pP->frame,
          size,
          ue_context_pP->ue_context.rnti);
  AssertFatal(!NODE_IS_DU(rrc->node_type), "illegal node type DU!\n");

  nr_rrc_transfer_protected_rrc_message(rrc, ue_p, DCCH, buffer, size);
}
void
rrc_gNB_store_RRCReconfiguration(
  const protocol_ctxt_t     *const ctxt_pP,
  rrc_gNB_ue_context_t      *ue_context_pP,
  NR_RRCReconfiguration_t * rrcReconfiguration
)
//-----------------------------------------------------------------------------
{
  gNB_RRC_UE_t *ue_p = &ue_context_pP->ue_context;
  NR_SRB_ToAddModList_t *srb2addListP = NULL;
  NR_DRB_ToAddModList_t *drb2addList = NULL;
  NR_DRB_ToReleaseList_t *drb2releaseList = NULL;
  NR_CellGroupConfig_t  *cellGroupConfig = NULL;
  long xid;

  AssertFatal (rrcReconfiguration!=NULL, "%s rrcReconfiguration is NULL\n",__FUNCTION__);
  xid = rrcReconfiguration->rrc_TransactionIdentifier;
  LOG_D(NR_RRC, "rrc_gNB_store_RRCReconfiguration for transaction %ld\n",xid);

  drb2addList = fill_DRB_configList(ue_p);
  drb2releaseList = CALLOC(sizeof(*drb2releaseList), 1);

  for (int i = 0; i < NB_RB_MAX; i++) {
    asn1cSequenceAdd(drb2releaseList->list, NR_DRB_Identity_t, DRB_release);
    DRB_release = i + 1;
  }

  if(rrcReconfiguration->criticalExtensions.present == NR_RRCReconfiguration__criticalExtensions_PR_rrcReconfiguration && rrcReconfiguration->criticalExtensions.choice.rrcReconfiguration){
    NR_RRCReconfiguration_IEs_t * ie = rrcReconfiguration->criticalExtensions.choice.rrcReconfiguration;

    if(ie->radioBearerConfig){
      if(ie->radioBearerConfig->srb_ToAddModList){
        LOG_D(NR_RRC, "%s store srb_ToAddModList\n",__FUNCTION__);
        srb2addListP = ie->radioBearerConfig->srb_ToAddModList;
        ie->radioBearerConfig->srb_ToAddModList = NULL; /* to avoid the content be released externally */
        /* Set the SRB active in UE context */
        if (srb2addListP != NULL) {
          for (int i = 0; (i < srb2addListP->list.count) && (i < 3); i++) {
            if (srb2addListP->list.array[i]->srb_Identity == 1) {
              ue_p->Srb[1].Active = 1;
            } else if (srb2addListP->list.array[i]->srb_Identity == 2) {
              ue_p->Srb[2].Active = 1;
              LOG_I(NR_RRC, "[gNB %d] Frame      %d CC %d : SRB2 is now active\n", ctxt_pP->module_id, ctxt_pP->frame, ue_p->primaryCC_id);
            } else {
              LOG_W(NR_RRC, "[gNB %d] Frame %d CC %d: invalid SRB identity %ld\n", ctxt_pP->module_id, ctxt_pP->frame, ue_p->primaryCC_id, srb2addListP->list.array[i]->srb_Identity);
            }
          }
        }
      }

      if(ie->radioBearerConfig->drb_ToAddModList) {
        drb2addList = ie->radioBearerConfig->drb_ToAddModList;
        ie->radioBearerConfig->drb_ToAddModList = NULL; /* to avoid the content be released externally*/
        for (int i = 0; (i < drb2addList->list.count); i++) {
          long drb_id = drb2addList->list.array[i]->drb_Identity;
          ue_p->established_drbs[drb_id].drb_id = drb_id;
          ue_p->established_drbs[drb_id].status = DRB_ACTIVE;
          if (drb2addList->list.array[i]->cnAssociation->present) {
            ue_p->established_drbs[drb_id].cnAssociation.present = NR_DRB_ToAddMod__cnAssociation_PR_sdap_Config;
            ue_p->established_drbs[drb_id].cnAssociation.sdap_config.defaultDRB = drb2addList->list.array[i]->cnAssociation->choice.sdap_Config->defaultDRB;
            ue_p->established_drbs[drb_id].cnAssociation.sdap_config.pdusession_id = drb2addList->list.array[i]->cnAssociation->choice.sdap_Config->pdu_Session;
            ue_p->established_drbs[drb_id].cnAssociation.sdap_config.sdap_HeaderDL = drb2addList->list.array[i]->cnAssociation->choice.sdap_Config->sdap_HeaderDL;
            ue_p->established_drbs[drb_id].cnAssociation.sdap_config.sdap_HeaderUL = drb2addList->list.array[i]->cnAssociation->choice.sdap_Config->sdap_HeaderUL;
            for (int j = 0; j < drb2addList->list.array[i]->cnAssociation->choice.sdap_Config->mappedQoS_FlowsToAdd->list.count; ++j) {
              ue_p->established_drbs[drb_id].cnAssociation.sdap_config.mappedQoS_FlowsToAdd[i] = drb2addList->list.array[i]->cnAssociation->choice.sdap_Config->mappedQoS_FlowsToAdd->list.array[j];
            }
          }
          // Struct is marked as OPTIONAL, but no present flag to check
          ue_p->established_drbs[drb_id].pdcp_config.discardTimer = *drb2addList->list.array[i]->pdcp_Config->drb->discardTimer;
          ue_p->established_drbs[drb_id].pdcp_config.pdcp_SN_SizeUL = *drb2addList->list.array[i]->pdcp_Config->drb->pdcp_SN_SizeUL;
          ue_p->established_drbs[drb_id].pdcp_config.pdcp_SN_SizeDL = *drb2addList->list.array[i]->pdcp_Config->drb->pdcp_SN_SizeDL;

          LOG_I(NR_RRC, "[gNB %d] Frame %d CC %d : DRB%d is now active\n", ctxt_pP->module_id, ctxt_pP->frame, ue_p->primaryCC_id, drb_id);
        }
      }

      if(ie->radioBearerConfig->drb_ToReleaseList){
        LOG_D(NR_RRC, "%s store drb_ToReleaseList\n",__FUNCTION__);
        drb2releaseList = (NR_DRB_ToAddModList_t*)ie->radioBearerConfig->drb_ToReleaseList;
        ie->radioBearerConfig->drb_ToReleaseList = NULL; /* to avoid the content be released externally*/
      }
    }

    if(ie->nonCriticalExtension){
      if(ie->nonCriticalExtension->masterCellGroup){
        uper_decode(NULL,
              &asn_DEF_NR_CellGroupConfig,
              (void **)&cellGroupConfig,
              (uint8_t *)ie->nonCriticalExtension->masterCellGroup->buf,
              ie->nonCriticalExtension->masterCellGroup->size, 0, 0);

        if (LOG_DEBUGFLAG(DEBUG_ASN1) ) {
          xer_fprint(stdout, &asn_DEF_NR_CellGroupConfig, (const void *)cellGroupConfig);
        }

        if(cellGroupConfig){
#if 1
          NR_CellGroupConfig_t   *masterCellGroup = ue_context_pP->ue_context.masterCellGroup;
          if(masterCellGroup && cellGroupConfig->rlc_BearerToAddModList){
            /* we only care the added Bearer configuration here */
            int count = cellGroupConfig->rlc_BearerToAddModList->list.count;
            if(count && masterCellGroup->rlc_BearerToAddModList==NULL){
              /* code would not get here */
              masterCellGroup->rlc_BearerToAddModList = CALLOC(1,sizeof(struct NR_CellGroupConfig__rlc_BearerToAddModList));
            }
            LOG_D(NR_RRC, "%s add rlc_BearerConfig into UE masterCellGroup\n",__FUNCTION__);
            for(int i=0; i < count; i++){
              ASN_SEQUENCE_ADD(&masterCellGroup->rlc_BearerToAddModList->list, cellGroupConfig->rlc_BearerToAddModList->list.array[i]);
            }
            if (LOG_DEBUGFLAG(DEBUG_ASN1) ) {
              xer_fprint(stdout, &asn_DEF_NR_CellGroupConfig, (const void *)masterCellGroup);
            }
            cellGroupConfig->rlc_BearerToAddModList->list.free = NULL; /*not free item pointer as it already added to another list */
            asn_sequence_empty(&cellGroupConfig->rlc_BearerToAddModList->list);
          }
          ASN_STRUCT_FREE(asn_DEF_NR_CellGroupConfig,cellGroupConfig);
#else
          if(ue_context_pP->ue_context.masterCellGroup){
            /* There is issue to free masterCellGroup content here.  */
            ASN_STRUCT_FREE(asn_DEF_NR_CellGroupConfig,ue_context_pP->ue_context.masterCellGroup);
          }
          LOG_D(NR_RRC, "%s store cellGroupConfig\n",__FUNCTION__);
          ue_context_pP->ue_context.masterCellGroup = cellGroupConfig;
#endif
        }
      }
    }
  }

  gNB_RRC_INST *rrc = RC.nrrrc[ctxt_pP->module_id];
  if (NODE_IS_DU(rrc->node_type) || NODE_IS_MONOLITHIC(rrc->node_type)) {
    //int CC_id = 0; //bugz129620 to do, to acquire ccid info
    //nr_rrc_mac_update_cellgroup(CC_id, ue_p->rnti, ue_p->masterCellGroup);
    

    // uint32_t delay_ms = ue_context_pP->ue_context.masterCellGroup &&
    //                     ue_context_pP->ue_context.masterCellGroup->spCellConfig &&
    //                     ue_context_pP->ue_context.masterCellGroup->spCellConfig->spCellConfigDedicated &&
    //                     ue_context_pP->ue_context.masterCellGroup->spCellConfig->spCellConfigDedicated->downlinkBWP_ToAddModList ?
    //                     NR_RRC_RECONFIGURATION_DELAY_MS + NR_RRC_BWP_SWITCHING_DELAY_MS : NR_RRC_RECONFIGURATION_DELAY_MS;

    // note be careful, do not mix MAC UE database and RRC UE database  
    NR_UE_info_t *UE=find_nr_UE(&RC.nrmac[0]->UE_info, 0, ue_context_pP->ue_context.rnti);
    NR_SCHED_LOCK(&RC.nrmac[0]->sched_lock);
    nr_mac_prepare_cellgroup_update(RC.nrmac[0], UE, ue_p->masterCellGroup);
    NR_SCHED_UNLOCK(&RC.nrmac[0]->sched_lock);
    NR_SCHED_LOCK(&RC.nrmac[0]->sched_lock);
    nr_mac_enable_ue_rrc_processing_timer(RC.nrmac[0], UE, /* apply_cellGroup = */ false);//TODO W38: this is unconformtable, for OAI, MAC process this message, it has all info needed. eg. app_cellGroup reconfig_cellgroup. RRC does not
    NR_SCHED_UNLOCK(&RC.nrmac[0]->sched_lock);
  }
}

//-----------------------------------------------------------------------------
void rrc_gNB_generate_dedicatedRRCReconfiguration(const protocol_ctxt_t *const ctxt_pP, rrc_gNB_ue_context_t *ue_context_pP)
//-----------------------------------------------------------------------------
{
  gNB_RRC_INST *rrc = RC.nrrrc[ctxt_pP->module_id];

  uint8_t xid = rrc_gNB_get_next_transaction_identifier(ctxt_pP->module_id);
  int CC_id = ue_context_pP->ue_context.primaryCC_id;
  int drb_id_to_setup_start = 1;
  gNB_RRC_UE_t *ue_p = &ue_context_pP->ue_context;
  NR_DRB_ToAddModList_t *DRB_configList = fill_DRB_configList(ue_p);
  int nb_drb_to_setup = DRB_configList ? DRB_configList->list.count : 0;
  ue_p->xids[xid] = RRC_PDUSESSION_ESTABLISH;
  struct NR_RRCReconfiguration_v1530_IEs__dedicatedNAS_MessageList *dedicatedNAS_MessageList = NULL;
  NR_DedicatedNAS_Message_t *dedicatedNAS_Message = NULL;
  dedicatedNAS_MessageList = CALLOC(1, sizeof(struct NR_RRCReconfiguration_v1530_IEs__dedicatedNAS_MessageList));

  for (int i=0; i < nb_drb_to_setup; i++) {
    NR_DRB_ToAddMod_t *DRB_config = DRB_configList->list.array[i];
    if (drb_id_to_setup_start == 1)
      drb_id_to_setup_start = DRB_config->drb_Identity;
    int j = ue_p->nb_of_pdusessions - 1;
    AssertFatal(j >= 0, "");
    if (ue_p->pduSession[j].param.nas_pdu.buffer != NULL) {
      dedicatedNAS_Message = CALLOC(1, sizeof(NR_DedicatedNAS_Message_t));
      memset(dedicatedNAS_Message, 0, sizeof(OCTET_STRING_t));
      OCTET_STRING_fromBuf(dedicatedNAS_Message,
                           (char *)ue_p->pduSession[j].param.nas_pdu.buffer,
                           ue_p->pduSession[j].param.nas_pdu.length);
      ue_p->pduSession[j].status = PDU_SESSION_STATUS_DONE;
      asn1cSeqAdd(&dedicatedNAS_MessageList->list, dedicatedNAS_Message);

      LOG_I(NR_RRC, "add NAS info with size %d (pdusession idx %d)\n", ue_p->pduSession[j].param.nas_pdu.length, j);
    } else {
      // TODO
      LOG_E(NR_RRC, "no NAS info (pdusession idx %d)\n", j);
    }

    ue_p->pduSession[j].xid = xid;
  }
  freeDRBlist(DRB_configList);

  /* If list is empty free the list and reset the address */
  if (dedicatedNAS_MessageList->list.count == 0) {
    free(dedicatedNAS_MessageList);
    dedicatedNAS_MessageList = NULL;
  }

  /* Free all NAS PDUs */
  for (int i = 0; i < ue_p->nb_of_pdusessions; i++) {
    if (ue_p->pduSession[i].param.nas_pdu.buffer != NULL) {
      /* Free the NAS PDU buffer and invalidate it */
      free(ue_p->pduSession[i].param.nas_pdu.buffer);
      ue_p->pduSession[i].param.nas_pdu.buffer = NULL;
    }
  }

  NR_CellGroupConfig_t *cellGroupConfig = ue_p->masterCellGroup;

  uint8_t buffer[RRC_BUF_SIZE] = {0};
  NR_SRB_ToAddModList_t *SRBs = createSRBlist(ue_p, false);
  NR_DRB_ToAddModList_t *DRBs = createDRBlist(ue_p, false);

  int size = do_RRCReconfiguration(ue_p,
                                   buffer,
                                   RRC_BUF_SIZE,
                                   xid,
                                   SRBs,
                                   DRBs,
                                   ue_p->DRB_ReleaseList,
                                   NULL,
                                   NULL,
                                   dedicatedNAS_MessageList,
                                   cellGroupConfig);
  LOG_DUMPMSG(NR_RRC,DEBUG_RRC,(char *)buffer,size,"[MSG] RRC Reconfiguration\n");
  freeSRBlist(SRBs);
  freeDRBlist(DRBs);
  ASN_STRUCT_FREE(asn_DEF_NR_DRB_ToReleaseList, ue_p->DRB_ReleaseList);
  ue_p->DRB_ReleaseList = NULL;

  LOG_I(NR_RRC, "[gNB %d] Frame %d, Logical Channel DL-DCCH, Generate RRCReconfiguration (bytes %d, UE RNTI %x)\n", ctxt_pP->module_id, ctxt_pP->frame, size, ue_p->rnti);
  LOG_D(NR_RRC,
        "[FRAME %05d][RRC_gNB][MOD %u][][--- PDCP_DATA_REQ/%d Bytes (rrcReconfiguration to UE %x MUI %d) --->][PDCP][MOD %u][RB %u]\n",
        ctxt_pP->frame,
        ctxt_pP->module_id,
        size,
        ue_p->rnti,
        rrc_gNB_mui,
        ctxt_pP->module_id,
        DCCH);

  nr_rrc_transfer_protected_rrc_message(rrc, ue_p, DCCH, buffer, size);
}

//-----------------------------------------------------------------------------
void
rrc_gNB_modify_dedicatedRRCReconfiguration(
  const protocol_ctxt_t     *const ctxt_pP,
  rrc_gNB_ue_context_t      *ue_context_pP)
//-----------------------------------------------------------------------------
{
  gNB_RRC_UE_t *ue_p = &ue_context_pP->ue_context;
  NR_DRB_ToAddModList_t *DRB_configList = fill_DRB_configList(ue_p);
  int qos_flow_index = 0;
  int CC_id = ue_context_pP->ue_context.primaryCC_id;
  uint8_t xid = rrc_gNB_get_next_transaction_identifier(ctxt_pP->module_id);
  ue_p->xids[xid] = RRC_PDUSESSION_MODIFY;

  struct NR_RRCReconfiguration_v1530_IEs__dedicatedNAS_MessageList *dedicatedNAS_MessageList =
      CALLOC(1, sizeof(*dedicatedNAS_MessageList));
  NR_DRB_ToAddMod_t *DRB_config = NULL;

  for (int i = 0; i < ue_p->nb_of_pdusessions; i++) {
    // bypass the new and already configured pdu sessions
    if (ue_p->pduSession[i].status >= PDU_SESSION_STATUS_DONE) {
      ue_p->pduSession[i].xid = xid;
      continue;
    }

    if (ue_p->pduSession[i].cause != NGAP_CAUSE_NOTHING) {
      // set xid of failure pdu session
      ue_p->pduSession[i].xid = xid;
      ue_p->pduSession[i].status = PDU_SESSION_STATUS_FAILED;
      continue;
    }

    // search exist DRB_config
    int j;
    for (j = 0; i < MAX_DRBS_PER_UE; j++) {
      if (ue_p->established_drbs[j].status != DRB_INACTIVE
          && ue_p->established_drbs[j].cnAssociation.sdap_config.pdusession_id == ue_p->pduSession[i].param.pdusession_id)
        break;
    }

    if (j == MAX_DRBS_PER_UE) {
      ue_p->pduSession[i].xid = xid;
      ue_p->pduSession[i].status = PDU_SESSION_STATUS_FAILED;
      ue_p->pduSession[i].cause = NGAP_CAUSE_RADIO_NETWORK;
      ue_p->pduSession[i].cause_value = NGAP_CauseRadioNetwork_unspecified;
      continue;
    }

    // Reference TS23501 Table 5.7.4-1: Standardized 5QI to QoS characteristics mapping
    for (qos_flow_index = 0; qos_flow_index < ue_p->pduSession[i].param.nb_qos; qos_flow_index++) {
      switch (ue_p->pduSession[i].param.qos[qos_flow_index].fiveQI) {
        case 1: //100ms
        case 2: //150ms
        case 3: //50ms
        case 4: //300ms
        case 5: //100ms
        case 6: //300ms
        case 7: //100ms
        case 8: //300ms
        case 9: //300ms Video (Buffered Streaming)TCP-based (e.g., www, e-mail, chat, ftp, p2p file sharing, progressive video, etc.)
          // TODO
          break;

        default:
          LOG_E(NR_RRC, "not supported 5qi %lu\n", ue_p->pduSession[i].param.qos[qos_flow_index].fiveQI);
          ue_p->pduSession[i].status = PDU_SESSION_STATUS_FAILED;
          ue_p->pduSession[i].xid = xid;
          ue_p->pduSession[i].cause = NGAP_CAUSE_RADIO_NETWORK;
          ue_p->pduSession[i].cause_value = NGAP_CauseRadioNetwork_not_supported_5QI_value;
          continue;
      }
      LOG_I(NR_RRC,
            "PDU SESSION ID %ld, DRB ID %ld (index %d), QOS flow %d, 5QI %ld \n",
            DRB_config->cnAssociation->choice.sdap_Config->pdu_Session,
            DRB_config->drb_Identity,
            i,
            qos_flow_index,
            ue_p->pduSession[i].param.qos[qos_flow_index].fiveQI);
    }

    asn1cSeqAdd(&DRB_configList->list, DRB_config);

    ue_p->pduSession[i].status = PDU_SESSION_STATUS_DONE;
    ue_p->pduSession[i].xid = xid;

    if (ue_p->pduSession[i].param.nas_pdu.buffer != NULL) {
      asn1cSequenceAdd(dedicatedNAS_MessageList->list,NR_DedicatedNAS_Message_t, dedicatedNAS_Message);
      OCTET_STRING_fromBuf(dedicatedNAS_Message, (char *)ue_p->pduSession[i].param.nas_pdu.buffer, ue_p->pduSession[i].param.nas_pdu.length);
      LOG_I(NR_RRC, "add NAS info with size %d (pdusession id %d)\n", ue_p->pduSession[i].param.nas_pdu.length, ue_p->pduSession[i].param.pdusession_id);
    } else {
      LOG_W(NR_RRC, "no NAS info (pdusession id %d)\n", ue_p->pduSession[i].param.pdusession_id);
    }
  }

  /* If list is empty free the list and reset the address */
  if (dedicatedNAS_MessageList->list.count == 0) {
    free(dedicatedNAS_MessageList);
    dedicatedNAS_MessageList = NULL;
  }

  uint8_t buffer[RRC_BUF_SIZE];
  int size = do_RRCReconfiguration(ue_p,
                                   buffer,
                                   RRC_BUF_SIZE,
                                   xid,
                                   NULL,
                                   DRB_configList,
                                   NULL,
                                   NULL,
                                   NULL,
                                   dedicatedNAS_MessageList,
                                   NULL);
  LOG_DUMPMSG(NR_RRC, DEBUG_RRC, (char *)buffer, size, "[MSG] RRC Reconfiguration\n");

  /* Free all NAS PDUs */
  for (int i = 0; i < ue_p->nb_of_pdusessions; i++) {
    if (ue_p->pduSession[i].param.nas_pdu.buffer != NULL) {
      /* Free the NAS PDU buffer and invalidate it */
      free(ue_p->pduSession[i].param.nas_pdu.buffer);
      ue_p->pduSession[i].param.nas_pdu.buffer = NULL;
    }
  }

  LOG_I(NR_RRC, "[gNB %d] Frame %d, Logical Channel DL-DCCH, Generate RRCReconfiguration (bytes %d, UE RNTI %x)\n", ctxt_pP->module_id, ctxt_pP->frame, size, ue_p->rnti);
  LOG_D(NR_RRC,
        "[FRAME %05d][RRC_gNB][MOD %u][][--- PDCP_DATA_REQ/%d Bytes (rrcReconfiguration to UE %x MUI %d) --->][PDCP][MOD %u][RB %u]\n",
        ctxt_pP->frame,
        ctxt_pP->module_id,
        size,
        ue_p->rnti,
        rrc_gNB_mui,
        ctxt_pP->module_id,
        DCCH);

  gNB_RRC_INST *rrc = RC.nrrrc[ctxt_pP->module_id];
  nr_rrc_transfer_protected_rrc_message(rrc, ue_p, DCCH, buffer, size);
}

//-----------------------------------------------------------------------------
void
rrc_gNB_generate_dedicatedRRCReconfiguration_release(
    const protocol_ctxt_t   *const ctxt_pP,
    rrc_gNB_ue_context_t    *const ue_context_pP,
    uint8_t                  xid,
    uint32_t                 nas_length,
    uint8_t                 *nas_buffer)
//-----------------------------------------------------------------------------
{
  gNB_RRC_UE_t *ue_p = &ue_context_pP->ue_context;
  int CC_id = ue_context_pP->ue_context.primaryCC_id;

  NR_DRB_ToReleaseList_t *DRB_Release_configList2 = CALLOC(sizeof(*DRB_Release_configList2), 1);

  for (int i = 0; i < NB_RB_MAX; i++) {
    if ((ue_p->pduSession[i].status == PDU_SESSION_STATUS_TORELEASE) && ue_p->pduSession[i].xid == xid) {
      asn1cSequenceAdd(DRB_Release_configList2->list, NR_DRB_Identity_t, DRB_release);
      *DRB_release = i + 1;
    }
  }

  /* If list is empty free the list and reset the address */
  struct NR_RRCReconfiguration_v1530_IEs__dedicatedNAS_MessageList *dedicatedNAS_MessageList = NULL;
  if (nas_length > 0) {
    dedicatedNAS_MessageList = CALLOC(1, sizeof(*dedicatedNAS_MessageList));
    asn1cSequenceAdd(dedicatedNAS_MessageList->list, NR_DedicatedNAS_Message_t, dedicatedNAS_Message);
    OCTET_STRING_fromBuf(dedicatedNAS_Message, (char *)nas_buffer, nas_length);
    LOG_I(NR_RRC,"add NAS info with size %d\n", nas_length);
  } else {
    LOG_W(NR_RRC,"dedlicated NAS list is empty\n");
  }

  uint8_t buffer[RRC_BUF_SIZE] = {0};
  int size = do_RRCReconfiguration(ue_p,
                                   buffer,
                                   RRC_BUF_SIZE,
                                   xid,
                                   NULL,
                                   NULL,
                                   DRB_Release_configList2,
                                   NULL,
                                   NULL,
                                   dedicatedNAS_MessageList,
                                   NULL);
  LOG_DUMPMSG(NR_RRC,DEBUG_RRC,(char *)buffer,size, "[MSG] RRC Reconfiguration\n");

  /* Free all NAS PDUs */
  if (nas_length > 0) {
    /* Free the NAS PDU buffer and invalidate it */
    free(nas_buffer);
  }

  LOG_I(NR_RRC, "[gNB %d] Frame %d, Logical Channel DL-DCCH, Generate NR_RRCReconfiguration (bytes %d, UE RNTI %x)\n", ctxt_pP->module_id, ctxt_pP->frame, size, ue_p->rnti);
  LOG_D(NR_RRC,
        "[FRAME %05d][RRC_gNB][MOD %u][][--- PDCP_DATA_REQ/%d Bytes (rrcReconfiguration to UE %x MUI %d) --->][PDCP][MOD %u][RB %u]\n",
        ctxt_pP->frame,
        ctxt_pP->module_id,
        size,
        ue_p->rnti,
        rrc_gNB_mui,
        ctxt_pP->module_id,
        DCCH);

  gNB_RRC_INST *rrc = RC.nrrrc[ctxt_pP->module_id];
  nr_rrc_transfer_protected_rrc_message(rrc, ue_p, DCCH, buffer, size);
}

//-----------------------------------------------------------------------------
/*
* Process the RRC Reconfiguration Complete from the UE
*/
static void rrc_gNB_process_RRCReconfigurationComplete(const protocol_ctxt_t *const ctxt_pP, rrc_gNB_ue_context_t *ue_context_pP, const uint8_t xid)
{
  int                                 drb_id;
  uint8_t kRRCenc[16] = {0};
  uint8_t kRRCint[16] = {0};
  uint8_t kUPenc[16] = {0};
  uint8_t kUPint[16] = {0};
  gNB_RRC_UE_t *ue_p = &ue_context_pP->ue_context;
  NR_DRB_ToAddModList_t *DRB_configList = createDRBlist(ue_p, false);

  if (RC.ss.mode > SS_GNB) {
    // todo fix: use ue ctx instead of static vars 
    memcpy(kRRCenc, _nr_control_plane_cip_key, 16);
    memcpy(kRRCint, _nr_control_plane_int_key, 16);
    memcpy(kUPint, _nr_data_plane_int_key, 16);
    memcpy(kUPenc, _nr_data_plane_cip_key, 16);

    ue_p->integrity_algorithm = _int_algo;
    ue_p->ciphering_algorithm = _cip_algo;
  } else {
    /* Derive the keys from kgnb */
    if (DRB_configList != NULL) {
      nr_derive_key(UP_ENC_ALG, ue_p->ciphering_algorithm, ue_p->kgnb, kUPenc);
      nr_derive_key(UP_INT_ALG, ue_p->integrity_algorithm, ue_p->kgnb, kUPint);
    }
    nr_derive_key(RRC_ENC_ALG, ue_p->ciphering_algorithm, ue_p->kgnb, kRRCenc);
    nr_derive_key(RRC_INT_ALG, ue_p->integrity_algorithm, ue_p->kgnb, kRRCint);
  }

  /* Refresh SRBs/DRBs */
  LOG_D(NR_RRC, "Configuring PDCP DRBs/SRBs for UE %04x\n", ue_p->rnti);
  ue_context_pP->ue_context.ue_reconfiguration_after_reestablishment_counter++;

  NR_SRB_ToAddModList_t *SRBs = createSRBlist(ue_p, false);

  nr_pdcp_add_srbs(ctxt_pP->enb_flag,
                   ctxt_pP->rntiMaybeUEid,
                   SRBs,
                   (ue_p->integrity_algorithm << 4) | ue_p->ciphering_algorithm,
                   kRRCenc,
                   kRRCint);
  nr_pdcp_add_drbs(ctxt_pP->enb_flag,
                   ctxt_pP->rntiMaybeUEid,
                   DRB_configList,
                   (ue_p->integrity_algorithm << 4) | ue_p->ciphering_algorithm,
                   kUPenc,
                   kUPint);

  /* Refresh DRBs */
  if (!NODE_IS_CU(RC.nrrrc[ctxt_pP->module_id]->node_type)) {
    LOG_D(NR_RRC,"Configuring RLC DRBs/SRBs for UE %04x\n",ue_context_pP->ue_context.rnti);
    const struct NR_CellGroupConfig__rlc_BearerToAddModList *bearer_list =
        ue_context_pP->ue_context.masterCellGroup->rlc_BearerToAddModList;
    if (RC.ss.mode > SS_GNB) {
      // AGP: removed in OAI@W32. Left in SQN version for compatibility
      nr_rrc_addmod_srbs(ctxt_pP->rntiMaybeUEid, ue_p->Srb, maxSRBs, bearer_list);
    }
    nr_rrc_addmod_drbs(ctxt_pP->rntiMaybeUEid, DRB_configList, bearer_list);
  }
  freeSRBlist(SRBs);

  /* Loop through DRBs and establish if necessary */
  if (DRB_configList != NULL) {
    for (int i = 0; i < DRB_configList->list.count; i++) {
      if (DRB_configList->list.array[i]) {
        drb_id = (int)DRB_configList->list.array[i]->drb_Identity;
        LOG_A(NR_RRC,
              "[gNB %d] Frame  %d : Logical Channel UL-DCCH, Received NR_RRCReconfigurationComplete from UE rnti %lx, reconfiguring DRB %d\n",
              ctxt_pP->module_id,
              ctxt_pP->frame,
              ctxt_pP->rntiMaybeUEid,
              (int)DRB_configList->list.array[i]->drb_Identity);
        //(int)*DRB_configList->list.array[i]->pdcp_Config->moreThanOneRLC->primaryPath.logicalChannel);

        if (ue_p->DRB_active[drb_id - 1] == 0) {
          ue_p->DRB_active[drb_id - 1] = DRB_ACTIVE;
          LOG_D(NR_RRC, "[gNB %d] Frame %d: Establish RLC UM Bidirectional, DRB %d Active\n",
                  ctxt_pP->module_id, ctxt_pP->frame, (int)DRB_configList->list.array[i]->drb_Identity);

          LOG_D(NR_RRC,
                  PROTOCOL_NR_RRC_CTXT_UE_FMT" RRC_gNB --- MAC_CONFIG_REQ  (DRB) ---> MAC_gNB\n",
                  PROTOCOL_NR_RRC_CTXT_UE_ARGS(ctxt_pP));

          //if (DRB_configList->list.array[i]->pdcp_Config->moreThanOneRLC->primaryPath.logicalChannel) {
          //  nr_DRB2LCHAN[i] = (uint8_t) * DRB_configList->list.array[i]->pdcp_Config->moreThanOneRLC->primaryPath.logicalChannel;
          //}

            // rrc_mac_config_req_eNB
        } else { // remove LCHAN from MAC/PHY
          if (ue_p->DRB_active[drb_id] == 1) {
            /* TODO : It may be needed if gNB goes into full stack working. */
            // DRB has just been removed so remove RLC + PDCP for DRB
            /*      rrc_pdcp_config_req (ctxt_pP->module_id, frameP, 1, CONFIG_ACTION_REMOVE,
            (ue_mod_idP * NB_RB_MAX) + DRB2LCHAN[i],UNDEF_SECURITY_MODE);
            */
            /*rrc_rlc_config_req(ctxt_pP,
                                SRB_FLAG_NO,
                                MBMS_FLAG_NO,
                                CONFIG_ACTION_REMOVE,
                                nr_DRB2LCHAN[i]);*/
          }

          // ue_p->DRB_active[drb_id] = 0;
          LOG_D(NR_RRC, PROTOCOL_NR_RRC_CTXT_UE_FMT" RRC_eNB --- MAC_CONFIG_REQ  (DRB) ---> MAC_eNB\n",
                  PROTOCOL_NR_RRC_CTXT_UE_ARGS(ctxt_pP));

          // rrc_mac_config_req_eNB

        } // end else of if (ue_p->DRB_active[drb_id] == 0)
      } // end if (DRB_configList->list.array[i])
    } // end for (int i = 0; i < DRB_configList->list.count; i++)

  } // end if DRB_configList != NULL
  freeDRBlist(DRB_configList);
}

//-----------------------------------------------------------------------------
static void rrc_gNB_generate_RRCReestablishment(rrc_gNB_ue_context_t *ue_context_pP,
                                                const uint8_t *masterCellGroup_from_DU,
                                                const rnti_t old_rnti,
                                                const nr_rrc_du_container_t *du)
//-----------------------------------------------------------------------------
{
  module_id_t module_id = 0;
  gNB_RRC_INST *rrc = RC.nrrrc[module_id];
  int enable_ciphering = 0;
  gNB_RRC_UE_t *ue_p = &ue_context_pP->ue_context;

  uint8_t buffer[RRC_BUF_SIZE] = {0};
  uint8_t xid = rrc_gNB_get_next_transaction_identifier(module_id);
  ue_p->xids[xid] = RRC_REESTABLISH;
  NR_SRB_ToAddModList_t *SRBs = createSRBlist(ue_p, true);
  const f1ap_served_cell_info_t *cell_info = &du->setup_req->cell[0].info;
  uint32_t ssb_arfcn = get_ssb_arfcn(cell_info, du->mib, du->sib1);
  int size = do_RRCReestablishment(ue_context_pP, buffer, RRC_BUF_SIZE, xid, cell_info->nr_pci, ssb_arfcn);

  LOG_I(NR_RRC, "[RAPROC] UE %04x Logical Channel DL-DCCH, Generating NR_RRCReestablishment (bytes %d)\n", ue_p->rnti, size);

  uint8_t kRRCenc[16] = {0};
  uint8_t kRRCint[16] = {0};
  uint8_t kUPenc[16] = {0};
  /* Derive the keys from kgnb */
  if (ue_p->Srb[1].Active)
    nr_derive_key(UP_ENC_ALG, ue_p->ciphering_algorithm, ue_p->kgnb, kUPenc);

  nr_derive_key(RRC_ENC_ALG, ue_p->ciphering_algorithm, ue_p->kgnb, kRRCenc);
  nr_derive_key(RRC_INT_ALG, ue_p->integrity_algorithm, ue_p->kgnb, kRRCint);
  freeSRBlist(SRBs);
  LOG_I(NR_RRC, "Set PDCP security UE %d RNTI %04x nca %ld nia %d in RRCReestablishment\n", ue_p->rrc_ue_id, ue_p->rnti, ue_p->ciphering_algorithm, ue_p->integrity_algorithm);
  uint8_t security_mode =
      enable_ciphering ? ue_p->ciphering_algorithm | (ue_p->integrity_algorithm << 4) : 0 | (ue_p->integrity_algorithm << 4);

  for (int srb_id = 1; srb_id < maxSRBs; srb_id++) {
    if (ue_p->Srb[srb_id].Active) {
      nr_pdcp_config_set_security(ue_p->rrc_ue_id, srb_id, security_mode, kRRCenc, kRRCint, kUPenc);
    }
  }

  nr_pdcp_reestablishment(ue_p->rrc_ue_id);

  f1_ue_data_t ue_data = cu_get_f1_ue_data(ue_p->rnti);
  uint32_t old_gNB_DU_ue_id = old_rnti;
  f1ap_dl_rrc_message_t dl_rrc = {.gNB_CU_ue_id = ue_p->rrc_ue_id,
                                  .gNB_DU_ue_id = ue_data.secondary_ue,
                                  .srb_id = DCCH,
                                  .old_gNB_DU_ue_id = &old_gNB_DU_ue_id};
  deliver_dl_rrc_message_data_t data = {.rrc = rrc, .dl_rrc = &dl_rrc};
  nr_pdcp_data_req_srb(ue_p->rrc_ue_id, DCCH, rrc_gNB_mui++, size, (unsigned char *const)buffer, rrc_deliver_dl_rrc_message, &data);
}

/// @brief Function tha processes RRCReestablishmentComplete message sent by the UE, after RRCReestasblishment request.
/// @param ctxt_pP Protocol context containing information regarding the UE and gNB
/// @param reestablish_rnti is the old C-RNTI
/// @param ue_context_pP  UE context container information regarding the UE
/// @param xid Transaction Identifier used in RRC messages
static void rrc_gNB_process_RRCReestablishmentComplete(const protocol_ctxt_t *const ctxt_pP,
                                                       rrc_gNB_ue_context_t *ue_context_pP,
                                                       const uint8_t xid)
{
  gNB_RRC_UE_t *ue_p = &ue_context_pP->ue_context;
  LOG_I(NR_RRC,
        "[RAPROC] UE %04x Logical Channel UL-DCCH, processing NR_RRCReestablishmentComplete from UE (SRB1 Active)\n",
        ue_p->rnti);

  int CC_id = ue_context_pP->ue_context.primaryCC_id;
  int i = 0;

  uint8_t new_xid = rrc_gNB_get_next_transaction_identifier(ctxt_pP->module_id);
  ue_p->xids[new_xid] = RRC_REESTABLISH_COMPLETE;
  ue_p->StatusRrc = NR_RRC_CONNECTED;

  ue_p->Srb[1].Active = 1;
  if (get_softmodem_params()->sa) {
    uint8_t send_security_mode_command = false;
    nr_rrc_pdcp_config_security(ctxt_pP, ue_context_pP, send_security_mode_command);
    LOG_D(NR_RRC, "RRC Reestablishment - set security successfully \n");
  }

  gNB_RRC_INST *rrc = RC.nrrrc[ctxt_pP->module_id];
  NR_CellGroupConfig_t *cellGroupConfig = calloc(1, sizeof(NR_CellGroupConfig_t));

  cellGroupConfig->spCellConfig = ue_p->masterCellGroup->spCellConfig;
  cellGroupConfig->mac_CellGroupConfig = ue_p->masterCellGroup->mac_CellGroupConfig;
  cellGroupConfig->physicalCellGroupConfig = ue_p->masterCellGroup->physicalCellGroupConfig;
  cellGroupConfig->rlc_BearerToReleaseList = NULL;
  cellGroupConfig->rlc_BearerToAddModList = calloc(1, sizeof(*cellGroupConfig->rlc_BearerToAddModList));

  /*
   * Get SRB2, DRB configuration from the existing UE context,
   * also start from SRB2 (i=1) and not from SRB1 (i=0).
   */
  for (i = 1; i < ue_p->masterCellGroup->rlc_BearerToAddModList->list.count; ++i)
    asn1cSeqAdd(&cellGroupConfig->rlc_BearerToAddModList->list, ue_p->masterCellGroup->rlc_BearerToAddModList->list.array[i]);

  for (i = 0; i < cellGroupConfig->rlc_BearerToAddModList->list.count; i++) {
    asn1cCallocOne(cellGroupConfig->rlc_BearerToAddModList->list.array[i]->reestablishRLC,
                   NR_RLC_BearerConfig__reestablishRLC_true);
  }

  NR_SRB_ToAddModList_t *SRBs = createSRBlist(ue_p, true);
  NR_DRB_ToAddModList_t *DRBs = createDRBlist(ue_p, true);

  uint8_t buffer[RRC_BUF_SIZE] = {0};
  int size = do_RRCReconfiguration(ue_p,
                                   buffer,
                                   RRC_BUF_SIZE,
                                   new_xid,
                                   SRBs,
                                   DRBs,
                                   NULL,
                                   NULL,
                                   NULL, // MeasObj_list,
                                   NULL,
                                   cellGroupConfig);
  freeSRBlist(SRBs);
  freeDRBlist(DRBs);
  LOG_DUMPMSG(NR_RRC, DEBUG_RRC, (char *)buffer, size, "[MSG] RRC Reconfiguration\n");

  AssertFatal(size > 0, "cannot encode RRC Reconfiguration\n");
  LOG_I(NR_RRC,
        "UE %d RNTI %04x: Generate NR_RRCReconfiguration after reestablishment complete (bytes %d)\n",
        ue_p->rrc_ue_id,
        ue_p->rnti,
        size);
  nr_rrc_transfer_protected_rrc_message(rrc, ue_p, DCCH, buffer, size);
  // nr_rrc_mac_update_cellgroup(CC_id, ue_context_pP->ue_context.rnti, cellGroupConfig); //TODO W38

  // if (NODE_IS_DU(RC.nrrrc[ctxt_pP->module_id]->node_type) || NODE_IS_MONOLITHIC(RC.nrrrc[ctxt_pP->module_id]->node_type)) {
  //   uint32_t delay_ms = ue_p->masterCellGroup && ue_p->masterCellGroup->spCellConfig
  //                               && ue_p->masterCellGroup->spCellConfig->spCellConfigDedicated
  //                               && ue_p->masterCellGroup->spCellConfig->spCellConfigDedicated->downlinkBWP_ToAddModList
  //                           ? NR_RRC_RECONFIGURATION_DELAY_MS + NR_RRC_BWP_SWITCHING_DELAY_MS
  //                           : NR_RRC_RECONFIGURATION_DELAY_MS;

  //   nr_mac_enable_ue_rrc_processing_timer(ctxt_pP->module_id,
  //                                         CC_id,
  //                                         ue_p->rnti,
  //                                         *RC.nrrrc[ctxt_pP->module_id]->carrier[CC_id].servingcellconfigcommon->ssbSubcarrierSpacing,
  //                                         delay_ms);
  // }
}
//-----------------------------------------------------------------------------

int nr_rrc_reconfiguration_req(rrc_gNB_ue_context_t         *const ue_context_pP,
                               protocol_ctxt_t              *const ctxt_pP,
                               const int                    dl_bwp_id,
                               const int                    ul_bwp_id) {

  uint8_t xid = rrc_gNB_get_next_transaction_identifier(ctxt_pP->module_id);
  gNB_RRC_UE_t *ue_p = &ue_context_pP->ue_context;
  int CC_id = ue_context_pP->ue_context.primaryCC_id;

  NR_CellGroupConfig_t *masterCellGroup = ue_p->masterCellGroup;
  if (dl_bwp_id > 0) {
    *masterCellGroup->spCellConfig->spCellConfigDedicated->firstActiveDownlinkBWP_Id = dl_bwp_id;
    *masterCellGroup->spCellConfig->spCellConfigDedicated->defaultDownlinkBWP_Id = dl_bwp_id;
  }
  if (ul_bwp_id > 0) {
    *masterCellGroup->spCellConfig->spCellConfigDedicated->uplinkConfig->firstActiveUplinkBWP_Id = ul_bwp_id;
  }

  uint8_t buffer[RRC_BUF_SIZE];
  int size = do_RRCReconfiguration(ue_p, buffer, RRC_BUF_SIZE, xid, NULL, NULL, NULL, NULL, NULL, NULL, masterCellGroup);

  // nr_rrc_mac_update_cellgroup(CC_id, ue_context_pP->ue_context.rnti, masterCellGroup); //TODO W38 this function removed @OAI

  gNB_RRC_INST *rrc = RC.nrrrc[ctxt_pP->module_id];
  nr_rrc_transfer_protected_rrc_message(rrc, ue_p, DCCH, buffer, size);

  // if (NODE_IS_DU(rrc->node_type) || NODE_IS_MONOLITHIC(rrc->node_type)) {  //TODO W38
  //   nr_rrc_mac_update_cellgroup(CC_id, ue_context_pP->ue_context.rnti, masterCellGroup);

  //   uint32_t delay_ms = ue_p->masterCellGroup && ue_p->masterCellGroup->spCellConfig
  //                               && ue_p->masterCellGroup->spCellConfig->spCellConfigDedicated
  //                               && ue_p->masterCellGroup->spCellConfig->spCellConfigDedicated->downlinkBWP_ToAddModList
  //                           ? NR_RRC_RECONFIGURATION_DELAY_MS + NR_RRC_BWP_SWITCHING_DELAY_MS
  //                           : NR_RRC_RECONFIGURATION_DELAY_MS;

  //   nr_mac_enable_ue_rrc_processing_timer(ctxt_pP->module_id, CC_id, ue_p->rnti, *rrc->carrier[CC_id].servingcellconfigcommon->ssbSubcarrierSpacing, delay_ms);
  // }

  return 0;
}

/*------------------------------------------------------------------------------*/
static int nr_rrc_gNB_decode_ccch(module_id_t module_id, const f1ap_initial_ul_rrc_message_t *msg, int CC_id)
{
  asn_dec_rval_t                                    dec_rval;
  NR_UL_CCCH_Message_t *ul_ccch_msg = NULL;
  gNB_RRC_INST *gnb_rrc_inst = RC.nrrrc[module_id];
  NR_RRCSetupRequest_IEs_t                         *rrcSetupRequest = NULL;
  NR_RRCReestablishmentRequest_IEs_t rrcReestablishmentRequest;
  rnti_t rnti = msg->crnti;

  /* look up corresponding DU. For the moment, there is only one */
  const nr_rrc_du_container_t *du = gnb_rrc_inst->du;
  AssertFatal(du != NULL, "received CCCH message, but no corresponding DU found\n");

  LOG_I(NR_RRC, "Decoding CCCH: RNTI %04x, payload_size %d\n", rnti, msg->rrc_container_length);
  dec_rval =
      uper_decode(NULL, &asn_DEF_NR_UL_CCCH_Message, (void **)&ul_ccch_msg, msg->rrc_container, msg->rrc_container_length, 0, 0);

  if (dec_rval.code != RC_OK || dec_rval.consumed == 0) {
    LOG_E(NR_RRC, " FATAL Error in receiving CCCH\n");
    return -1;
  }

  if (ul_ccch_msg->message.present == NR_UL_CCCH_MessageType_PR_c1) {
    switch (ul_ccch_msg->message.choice.c1->present) {
      case NR_UL_CCCH_MessageType__c1_PR_NOTHING:
        /* TODO */
        LOG_I(NR_RRC, "Received PR_NOTHING on UL-CCCH-Message\n");
        break;

      case NR_UL_CCCH_MessageType__c1_PR_rrcSetupRequest:
        LOG_D(NR_RRC, "Received RRCSetupRequest on UL-CCCH-Message (UE rnti %04x)\n", rnti);
        {
          rrcSetupRequest = &ul_ccch_msg->message.choice.c1->choice.rrcSetupRequest->rrcSetupRequest;
          rrc_gNB_ue_context_t *ue_context_p = NULL;
          if (NR_InitialUE_Identity_PR_randomValue == rrcSetupRequest->ue_Identity.present) {
            LOG_A(NR_RRC, "NR_InitialUE_Identity_PR_randomValue\n");
            /* randomValue                         BIT STRING (SIZE (39)) */
            if (rrcSetupRequest->ue_Identity.choice.randomValue.size != 5) { // 39-bit random value
              LOG_E(NR_RRC, "wrong InitialUE-Identity randomValue size, expected 5, provided %lu", (long unsigned int)rrcSetupRequest->ue_Identity.choice.randomValue.size);
              ASN_STRUCT_FREE(asn_DEF_NR_UL_CCCH_Message, ul_ccch_msg);
              return -1;
            }
            uint64_t random_value = 0;
            memcpy(((uint8_t *)&random_value) + 3, rrcSetupRequest->ue_Identity.choice.randomValue.buf, rrcSetupRequest->ue_Identity.choice.randomValue.size);

            /* if there is already a registered UE (with another RNTI) with this random_value,
             * the current one must be removed from MAC/PHY (zombie UE)
             */
            if ((ue_context_p = rrc_gNB_ue_context_random_exist(gnb_rrc_inst, random_value))) {
              LOG_W(NR_RRC, "new UE rnti (coming with random value) is already there, removing UE %x from MAC/PHY\n", rnti);
              AssertFatal(false, "not implemented\n");
            }

            ue_context_p = rrc_gNB_create_ue_context(rnti, gnb_rrc_inst, random_value, msg->gNB_DU_ue_id);
          } else if (NR_InitialUE_Identity_PR_ng_5G_S_TMSI_Part1 == rrcSetupRequest->ue_Identity.present) {
            LOG_A(NR_RRC, "NR_InitialUE_Identity_PR_ng_5G_S_TMSI_Part1\n");
            /* TODO */
            /* <5G-S-TMSI> = <AMF Set ID><AMF Pointer><5G-TMSI> 48-bit */
            /* ng-5G-S-TMSI-Part1                  BIT STRING (SIZE (39)) */
            if (rrcSetupRequest->ue_Identity.choice.ng_5G_S_TMSI_Part1.size != 5) {
              LOG_E(NR_RRC, "wrong ng_5G_S_TMSI_Part1 size, expected 5, provided %lu \n", (long unsigned int)rrcSetupRequest->ue_Identity.choice.ng_5G_S_TMSI_Part1.size);
              ASN_STRUCT_FREE(asn_DEF_NR_UL_CCCH_Message, ul_ccch_msg);
              return -1;
            }

            uint64_t s_tmsi_part1 = bitStr_to_uint64(&rrcSetupRequest->ue_Identity.choice.ng_5G_S_TMSI_Part1);

            // memcpy(((uint8_t *) & random_value) + 3,
            //         rrcSetupRequest->ue_Identity.choice.ng_5G_S_TMSI_Part1.buf,
            //         rrcSetupRequest->ue_Identity.choice.ng_5G_S_TMSI_Part1.size);

            if ((ue_context_p = rrc_gNB_ue_context_5g_s_tmsi_exist(gnb_rrc_inst, s_tmsi_part1))) {
              gNB_RRC_UE_t *UE = &ue_context_p->ue_context;
              LOG_I(NR_RRC, " 5G-S-TMSI-Part1 exists, old rnti %04x => %04x\n", UE->rnti, rnti);
              AssertFatal(false, "not implemented\n");

              /* replace rnti in the context */
              UE->rnti = rnti;
            } else {
              LOG_I(NR_RRC, "UE %04x 5G-S-TMSI-Part1 doesn't exist, setting ng_5G_S_TMSI_Part1 => %ld\n", rnti, s_tmsi_part1);

              ue_context_p = rrc_gNB_create_ue_context(rnti, gnb_rrc_inst, s_tmsi_part1, msg->gNB_DU_ue_id);
              AssertFatal(ue_context_p != NULL, "out of memory\n");
              gNB_RRC_UE_t *UE = &ue_context_p->ue_context;
              UE->Initialue_identity_5g_s_TMSI.presence = true;
              UE->ng_5G_S_TMSI_Part1 = s_tmsi_part1;
            }
          } else {
            /* TODO */
            uint64_t random_value = 0;
            memcpy(((uint8_t *)&random_value) + 3, rrcSetupRequest->ue_Identity.choice.randomValue.buf, rrcSetupRequest->ue_Identity.choice.randomValue.size);

            ue_context_p = rrc_gNB_create_ue_context(rnti, gnb_rrc_inst, random_value, msg->gNB_DU_ue_id);
            LOG_E(NR_RRC, "RRCSetupRequest without random UE identity or S-TMSI not supported, let's reject the UE %04x\n", rnti);
            rrc_gNB_generate_RRCReject(module_id, ue_context_p);
            break;
          }
          gNB_RRC_UE_t *UE = &ue_context_p->ue_context;
          UE->primaryCC_id = CC_id;
          UE->establishment_cause = rrcSetupRequest->establishmentCause;
          UE->Srb[1].Active = 1;
          rrc_gNB_generate_RRCSetup(module_id,
                                    rnti,
#if 1
                                    ue_context_p,
#else
// <= W31
                                    rrc_gNB_get_ue_context_by_rnti(gnb_rrc_inst, rnti),
#endif
                                    msg->du2cu_rrc_container,
                                    msg->du2cu_rrc_container_length,
                                    CC_id);
        }
        break;

      case NR_UL_CCCH_MessageType__c1_PR_rrcResumeRequest:
        LOG_I(NR_RRC, "receive rrcResumeRequest message \n");
        break;

      case NR_UL_CCCH_MessageType__c1_PR_rrcReestablishmentRequest: {
        LOG_DUMPMSG(NR_RRC,
                    DEBUG_RRC,
                    (char *)(msg->rrc_container),
                    msg->rrc_container_length,
                    "[MSG] RRC Reestablishment Request\n");
        rrcReestablishmentRequest = ul_ccch_msg->message.choice.c1->choice.rrcReestablishmentRequest->rrcReestablishmentRequest;
        const NR_ReestablishmentCause_t cause = rrcReestablishmentRequest.reestablishmentCause;
        const long physCellId = rrcReestablishmentRequest.ue_Identity.physCellId;
        LOG_I(NR_RRC,
              "UE %04x physCellId %ld NR_RRCReestablishmentRequest cause %s\n",
              rnti,
              physCellId,
              cause == NR_ReestablishmentCause_otherFailure
                  ? "Other Failure"
                  : (cause == NR_ReestablishmentCause_handoverFailure ? "Handover Failure" : "reconfigurationFailure"));

        AssertFatal(du->setup_req->num_cells_available == 1, "cannot handle more than one cell\n");
        const f1ap_served_cell_info_t *cell_info = &du->setup_req->cell[0].info;   //TODO W38:
        if (physCellId != cell_info->nr_pci) {
          /* UE was moving from previous cell so quickly that RRCReestablishment for previous cell was received in this cell */
          LOG_E(NR_RRC,
                " NR_RRCReestablishmentRequest ue_Identity.physCellId(%ld) is not equal to current physCellId(%d), fallback to RRC establishment\n",
                physCellId,
                cell_info->nr_pci);
          /* 38.401 8.7: "If the UE accessed from a gNB-DU other than the
           * original one, the gNB-CU should trigger the UE Context Setup
           * procedure" we did not implement this yet; TBD for Multi-DU */
          AssertFatal(false, "not implemented\n");
          break;
        }

        for (int i = 0; i < rrcReestablishmentRequest.ue_Identity.shortMAC_I.size; i++) {
          LOG_D(NR_RRC, "rrcReestablishmentRequest.ue_Identity.shortMAC_I.buf[%d] = %x\n", i, rrcReestablishmentRequest.ue_Identity.shortMAC_I.buf[i]);
        }

        rnti_t old_rnti = rrcReestablishmentRequest.ue_Identity.c_RNTI;
        // TODO: we need to check within a specific DU!
        rrc_gNB_ue_context_t *ue_context_p = rrc_gNB_get_ue_context_by_rnti(gnb_rrc_inst, old_rnti);
        gNB_RRC_UE_t *UE = &ue_context_p->ue_context;
        /* in case we need to do RRC Setup, give the UE a new random identity */
        uint64_t random_value;
        fill_random(&random_value, sizeof(random_value));
        random_value = random_value & 0x7fffffffff; /* random value is 39 bits */
        if (ue_context_p == NULL) {
          LOG_E(NR_RRC, "NR_RRCReestablishmentRequest without UE context, fallback to RRC setup\n");
          ue_context_p = rrc_gNB_create_ue_context(rnti, gnb_rrc_inst, random_value, msg->gNB_DU_ue_id);
          ue_context_p->ue_context.Srb[1].Active = 1;
          rrc_gNB_generate_RRCSetup(module_id, rnti, ue_context_p, msg->du2cu_rrc_container, msg->du2cu_rrc_container_length, CC_id);
          break;
        }

        // 3GPP TS 38.321 version 15.13.0 Section 7.1 Table 7.1-1: RNTI values
        if (rrcReestablishmentRequest.ue_Identity.c_RNTI < 0x1 || rrcReestablishmentRequest.ue_Identity.c_RNTI > 0xffef) {
          /* c_RNTI range error should not happen */
          LOG_E(NR_RRC,
                "NR_RRCReestablishmentRequest c_RNTI %04lx range error, fallback to RRC setup\n",
                rrcReestablishmentRequest.ue_Identity.c_RNTI);
          ue_context_p = rrc_gNB_create_ue_context(rnti, gnb_rrc_inst, random_value, msg->gNB_DU_ue_id);
          ue_context_p->ue_context.Srb[1].Active = 1;
          rrc_gNB_generate_RRCSetup(module_id, rnti, ue_context_p, msg->du2cu_rrc_container, msg->du2cu_rrc_container_length, CC_id);
          break;
        }

        /* TODO: start timer in ITTI and drop UE if it does not come back */

        // update with new RNTI, and update secondary UE association
        UE->rnti = rnti;
        cu_remove_f1_ue_data(UE->rrc_ue_id);
        f1_ue_data_t ue_data = {.secondary_ue = rnti};
        cu_add_f1_ue_data(UE->rrc_ue_id, &ue_data);

        UE->reestablishment_cause = cause;
        UE->primaryCC_id = CC_id;
        LOG_D(NR_RRC, "Accept RRCReestablishmentRequest from UE physCellId %ld cause %ld\n", physCellId, cause);
        rrc_gNB_generate_RRCReestablishment(ue_context_p,
                                            msg->du2cu_rrc_container,
                                            old_rnti,
                                            du);
      } break;

      case NR_UL_CCCH_MessageType__c1_PR_rrcSystemInfoRequest:
        LOG_I(NR_RRC, "UE %04x receive rrcSystemInfoRequest message \n", rnti);
        /* TODO */
        break;

      default:
        LOG_E(NR_RRC, "UE %04x Unknown message\n", rnti);
        break;
    }
  }
  ASN_STRUCT_FREE(asn_DEF_NR_UL_CCCH_Message, ul_ccch_msg);
  return 0;
}

/*! \fn uint64_t bitStr_to_uint64(BIT_STRING_t *)
 *\brief  This function extract at most a 64 bits value from a BIT_STRING_t object, the exact bits number depend on the BIT_STRING_t contents.
 *\param[in] pointer to the BIT_STRING_t object.
 *\return the extracted value.
 */
static inline uint64_t bitStr_to_uint64(BIT_STRING_t *asn) {
  uint64_t result = 0;
  int index;
  int shift;

  DevCheck ((asn->size > 0) && (asn->size <= 8), asn->size, 0, 0);

  shift = ((asn->size - 1) * 8) - asn->bits_unused;
  for (index = 0; index < (asn->size - 1); index++) {
    result |= (uint64_t)asn->buf[index] << shift;
    shift -= 8;
  }

  result |= asn->buf[index] >> asn->bits_unused;

  return result;
}

static void rrc_gNB_process_MeasurementReport(rrc_gNB_ue_context_t *ue_context, NR_MeasurementReport_t *measurementReport)
{
  if (LOG_DEBUGFLAG(DEBUG_ASN1))
    xer_fprint(stdout, &asn_DEF_NR_MeasurementReport, (void *)measurementReport);

  DevAssert(measurementReport->criticalExtensions.present == NR_MeasurementReport__criticalExtensions_PR_measurementReport
            && measurementReport->criticalExtensions.choice.measurementReport != NULL);

  gNB_RRC_UE_t *ue_ctxt = &ue_context->ue_context;
  ASN_STRUCT_FREE(asn_DEF_NR_MeasResults, ue_ctxt->measResults);
  ue_ctxt->measResults = NULL;

  const NR_MeasId_t id = measurementReport->criticalExtensions.choice.measurementReport->measResults.measId;
  AssertFatal(id, "unexpected MeasResult for MeasurementId %ld received\n", id);
  asn1cCallocOne(ue_ctxt->measResults, measurementReport->criticalExtensions.choice.measurementReport->measResults);
  /* we "keep" the measurement report, so set to 0 */
  free(measurementReport->criticalExtensions.choice.measurementReport);
  measurementReport->criticalExtensions.choice.measurementReport = NULL;
}

static int handle_rrcReestablishmentComplete(const protocol_ctxt_t *const ctxt_pP,
                                             rrc_gNB_ue_context_t *ue_context_p,
                                             const NR_RRCReestablishmentComplete_t *reestablishment_complete)
{
  DevAssert(ue_context_p != NULL);
  gNB_RRC_UE_t *UE = &ue_context_p->ue_context;

  DevAssert(reestablishment_complete->criticalExtensions.present
            == NR_RRCReestablishmentComplete__criticalExtensions_PR_rrcReestablishmentComplete);
  rrc_gNB_process_RRCReestablishmentComplete(ctxt_pP,
                                             ue_context_p,
                                             reestablishment_complete->rrc_TransactionIdentifier);
  UE->ue_reestablishment_counter++;
  return 0;
}

static int handle_ueCapabilityInformation(const protocol_ctxt_t *const ctxt_pP,
                                          rrc_gNB_ue_context_t *ue_context_p,
                                          const NR_UECapabilityInformation_t *ue_cap_info)
{
  AssertFatal(ue_context_p != NULL, "Processing %s() for UE %lx, ue_context_p is NULL\n", __func__, ctxt_pP->rntiMaybeUEid);
  gNB_RRC_UE_t *UE = &ue_context_p->ue_context;

  LOG_I(NR_RRC, "got UE capabilities for UE %lx\n", ctxt_pP->rntiMaybeUEid);
  int eutra_index = -1;

  if (ue_cap_info->criticalExtensions.present == NR_UECapabilityInformation__criticalExtensions_PR_ueCapabilityInformation) {
    const NR_UE_CapabilityRAT_ContainerList_t *ue_CapabilityRAT_ContainerList =
        ue_cap_info->criticalExtensions.choice.ueCapabilityInformation->ue_CapabilityRAT_ContainerList;

    /* Encode UE-CapabilityRAT-ContainerList for sending to the DU */
    free(UE->ue_cap_buffer.buf);
    UE->ue_cap_buffer.len = uper_encode_to_new_buffer(&asn_DEF_NR_UE_CapabilityRAT_ContainerList,
                                                      NULL,
                                                      ue_CapabilityRAT_ContainerList,
                                                      (void **)&UE->ue_cap_buffer.buf);
    if (UE->ue_cap_buffer.len <= 0) {
      LOG_E(RRC, "could not encode UE-CapabilityRAT-ContainerList, abort handling capabilities\n");
      return -1;
    }

    for (int i = 0; i < ue_CapabilityRAT_ContainerList->list.count; i++) {
      const NR_UE_CapabilityRAT_Container_t *ue_cap_container = ue_CapabilityRAT_ContainerList->list.array[i];
      if (ue_cap_container->rat_Type == NR_RAT_Type_nr) {
        if (UE->UE_Capability_nr) {
          ASN_STRUCT_FREE(asn_DEF_NR_UE_NR_Capability, UE->UE_Capability_nr);
          UE->UE_Capability_nr = 0;
        }

        asn_dec_rval_t dec_rval = uper_decode(NULL,
                                              &asn_DEF_NR_UE_NR_Capability,
                                              (void **)&UE->UE_Capability_nr,
                                              ue_cap_container->ue_CapabilityRAT_Container.buf,
                                              ue_cap_container->ue_CapabilityRAT_Container.size,
                                              0,
                                              0);
        if (LOG_DEBUGFLAG(DEBUG_ASN1)) {
          xer_fprint(stdout, &asn_DEF_NR_UE_NR_Capability, UE->UE_Capability_nr);
        }

        if ((dec_rval.code != RC_OK) && (dec_rval.consumed == 0)) {
          LOG_E(NR_RRC,
                PROTOCOL_NR_RRC_CTXT_UE_FMT " Failed to decode nr UE capabilities (%zu bytes)\n",
                PROTOCOL_NR_RRC_CTXT_UE_ARGS(ctxt_pP),
                dec_rval.consumed);
          ASN_STRUCT_FREE(asn_DEF_NR_UE_NR_Capability, UE->UE_Capability_nr);
          UE->UE_Capability_nr = 0;
        }

        UE->UE_Capability_size = ue_cap_container->ue_CapabilityRAT_Container.size;
        if (eutra_index != -1) {
          LOG_E(NR_RRC, "fatal: more than 1 eutra capability\n");
          exit(1);
        }
        eutra_index = i;
      }

      if (ue_cap_container->rat_Type == NR_RAT_Type_eutra_nr) {
        if (UE->UE_Capability_MRDC) {
          ASN_STRUCT_FREE(asn_DEF_NR_UE_MRDC_Capability, UE->UE_Capability_MRDC);
          UE->UE_Capability_MRDC = 0;
        }
        asn_dec_rval_t dec_rval = uper_decode(NULL,
                                              &asn_DEF_NR_UE_MRDC_Capability,
                                              (void **)&UE->UE_Capability_MRDC,
                                              ue_cap_container->ue_CapabilityRAT_Container.buf,
                                              ue_cap_container->ue_CapabilityRAT_Container.size,
                                              0,
                                              0);

        if (LOG_DEBUGFLAG(DEBUG_ASN1)) {
          xer_fprint(stdout, &asn_DEF_NR_UE_MRDC_Capability, UE->UE_Capability_MRDC);
        }

        if ((dec_rval.code != RC_OK) && (dec_rval.consumed == 0)) {
          LOG_E(NR_RRC,
                PROTOCOL_NR_RRC_CTXT_FMT " Failed to decode nr UE capabilities (%zu bytes)\n",
                PROTOCOL_NR_RRC_CTXT_UE_ARGS(ctxt_pP),
                dec_rval.consumed);
          ASN_STRUCT_FREE(asn_DEF_NR_UE_MRDC_Capability, UE->UE_Capability_MRDC);
          UE->UE_Capability_MRDC = 0;
        }
        UE->UE_MRDC_Capability_size = ue_cap_container->ue_CapabilityRAT_Container.size;
      }

      if (ue_cap_container->rat_Type == NR_RAT_Type_eutra) {
        // TODO
      }
    }

    if (eutra_index == -1)
      return -1;
  }

  if (get_softmodem_params()->sa) {
    rrc_gNB_send_NGAP_UE_CAPABILITIES_IND(ctxt_pP, ue_context_p, ue_cap_info);
  }

  // we send the UE capabilities request before RRC connection is complete,
  // so we cannot have a PDU session yet
  AssertFatal(UE->nb_of_pdusessions == 0, "logic bug: received capabilities while PDU session established\n");
  // TODO: send UE context modification response with UE capabilities to
  // allow DU configure CellGroupConfig
  if (RC.ss.mode == SS_GNB) {
    rrc_gNB_generate_defaultRRCReconfiguration(ctxt_pP, ue_context_p);
  }

  return 0;
}

static int handle_rrcSetupComplete(const protocol_ctxt_t *const ctxt_pP,
                                   rrc_gNB_ue_context_t *ue_context_p,
                                   const NR_RRCSetupComplete_t *setup_complete)
{
  if (!ue_context_p) {
    LOG_I(NR_RRC, "Processing NR_RRCSetupComplete UE %lx, ue_context_p is NULL\n", ctxt_pP->rntiMaybeUEid);
    return -1;
  }
  gNB_RRC_UE_t *UE = &ue_context_p->ue_context;

  NR_RRCSetupComplete_IEs_t *setup_complete_ies = setup_complete->criticalExtensions.choice.rrcSetupComplete;

  if (setup_complete_ies->ng_5G_S_TMSI_Value != NULL) {
    if (setup_complete_ies->ng_5G_S_TMSI_Value->present == NR_RRCSetupComplete_IEs__ng_5G_S_TMSI_Value_PR_ng_5G_S_TMSI_Part2) {
      if (setup_complete_ies->ng_5G_S_TMSI_Value->choice.ng_5G_S_TMSI_Part2.size != 2) {
        LOG_E(NR_RRC,
              "wrong ng_5G_S_TMSI_Part2 size, expected 2, provided %lu",
              (long unsigned int)
                  setup_complete->criticalExtensions.choice.rrcSetupComplete->ng_5G_S_TMSI_Value->choice.ng_5G_S_TMSI_Part2.size);
        return -1;
      }

      if (UE->ng_5G_S_TMSI_Part1 != 0) {
        UE->ng_5G_S_TMSI_Part2 = BIT_STRING_to_uint16(&setup_complete_ies->ng_5G_S_TMSI_Value->choice.ng_5G_S_TMSI_Part2);
      }

      /* TODO */
    } else if (setup_complete_ies->ng_5G_S_TMSI_Value->present == NR_RRCSetupComplete_IEs__ng_5G_S_TMSI_Value_PR_ng_5G_S_TMSI) {
      if (setup_complete_ies->ng_5G_S_TMSI_Value->choice.ng_5G_S_TMSI.size != 6) {
        LOG_E(NR_RRC,
              "wrong ng_5G_S_TMSI size, expected 6, provided %lu",
              (long unsigned int)setup_complete_ies->ng_5G_S_TMSI_Value->choice.ng_5G_S_TMSI.size);
        return -1;
      }

      uint64_t fiveg_s_TMSI = bitStr_to_uint64(&setup_complete_ies->ng_5G_S_TMSI_Value->choice.ng_5G_S_TMSI);
      LOG_I(NR_RRC,
            "Received rrcSetupComplete, 5g_s_TMSI: 0x%lX, amf_set_id: 0x%lX(%ld), amf_pointer: 0x%lX(%ld), 5g TMSI: 0x%X \n",
            fiveg_s_TMSI,
            fiveg_s_TMSI >> 38,
            fiveg_s_TMSI >> 38,
            (fiveg_s_TMSI >> 32) & 0x3F,
            (fiveg_s_TMSI >> 32) & 0x3F,
            (uint32_t)fiveg_s_TMSI);
      if (UE->Initialue_identity_5g_s_TMSI.presence) {
        UE->Initialue_identity_5g_s_TMSI.amf_set_id = fiveg_s_TMSI >> 38;
        UE->Initialue_identity_5g_s_TMSI.amf_pointer = (fiveg_s_TMSI >> 32) & 0x3F;
        UE->Initialue_identity_5g_s_TMSI.fiveg_tmsi = (uint32_t)fiveg_s_TMSI;
      }
    }
  }

  rrc_gNB_process_RRCSetupComplete(ctxt_pP, ue_context_p, setup_complete->criticalExtensions.choice.rrcSetupComplete);
  LOG_I(NR_RRC, PROTOCOL_NR_RRC_CTXT_UE_FMT " UE State = NR_RRC_CONNECTED \n", PROTOCOL_NR_RRC_CTXT_UE_ARGS(ctxt_pP));
  return 0;
}

static void handle_rrcReconfigurationComplete(const protocol_ctxt_t *const ctxt_pP,
                                              rrc_gNB_ue_context_t *ue_context_p,
                                              const NR_RRCReconfigurationComplete_t *reconfig_complete)
{
  LOG_I(NR_RRC, "Receive RRC Reconfiguration Complete message UE %lx\n", ctxt_pP->rntiMaybeUEid);
  AssertFatal(ue_context_p != NULL, "Processing %s() for UE %lx, ue_context_p is NULL\n", __func__, ctxt_pP->rntiMaybeUEid);
  gNB_RRC_UE_t *UE = &ue_context_p->ue_context;

  uint8_t xid = reconfig_complete->rrc_TransactionIdentifier;
  rrc_gNB_process_RRCReconfigurationComplete(ctxt_pP, ue_context_p, xid);

  bool successful_reconfig = true;
  if (get_softmodem_params()->sa) {
    switch (UE->xids[xid]) {
      case RRC_PDUSESSION_RELEASE: {
        gtpv1u_gnb_delete_tunnel_req_t req = {0};
        gtpv1u_delete_ngu_tunnel(ctxt_pP->instance, &req);
        // NGAP_PDUSESSION_RELEASE_RESPONSE
        rrc_gNB_send_NGAP_PDUSESSION_RELEASE_RESPONSE(ctxt_pP, ue_context_p, xid);
      } break;
      case RRC_PDUSESSION_ESTABLISH:
        if (UE->nb_of_pdusessions > 0)
          rrc_gNB_send_NGAP_PDUSESSION_SETUP_RESP(ctxt_pP, ue_context_p, xid);
        break;
      case RRC_PDUSESSION_MODIFY:
        rrc_gNB_send_NGAP_PDUSESSION_MODIFY_RESP(ctxt_pP, ue_context_p, xid);
        break;
      case RRC_DEFAULT_RECONF:
        rrc_gNB_send_NGAP_INITIAL_CONTEXT_SETUP_RESP(ctxt_pP, ue_context_p);
        break;
      case RRC_REESTABLISH_COMPLETE:
      case RRC_DEDICATED_RECONF:
        /* do nothing */
        break;
      default:
        LOG_E(RRC, "Received unexpected transaction type %d for xid %d\n", UE->xids[xid], xid);
        successful_reconfig = false;
        break;
    }
  }

  gNB_RRC_INST *rrc = RC.nrrrc[0];
  f1_ue_data_t ue_data = cu_get_f1_ue_data(UE->rnti);
  f1ap_ue_context_modif_req_t ue_context_modif_req = {
    .gNB_CU_ue_id = UE->rrc_ue_id,
    .gNB_DU_ue_id = ue_data.secondary_ue,
    .plmn.mcc = rrc->configuration[0].mcc[0],
    .plmn.mnc = rrc->configuration[0].mnc[0],
    .plmn.mnc_digit_length = rrc->configuration[0].mnc_digit_length[0],
    .nr_cellid = rrc->nr_cellid,
    .servCellId = 0, /* TODO: correct value? */
    .ReconfigComplOutcome = successful_reconfig ? RRCreconf_success : RRCreconf_failure,
  };
  rrc->mac_rrc.ue_context_modification_request(&ue_context_modif_req);
}
//-----------------------------------------------------------------------------
int rrc_gNB_decode_dcch(const protocol_ctxt_t *const ctxt_pP,
                        const rb_id_t Srb_id,
                        const uint8_t *const Rx_sdu,
                        const sdu_size_t sdu_sizeP)
//-----------------------------------------------------------------------------
{
  gNB_RRC_INST *gnb_rrc_inst = RC.nrrrc[ctxt_pP->module_id];
  rrc_gNB_ue_context_t *ue_context_p = NULL;

  if (RC.ss.mode > SS_GNB) {
    ue_context_p = rrc_gNB_get_ue_context_by_rnti(gnb_rrc_inst, ctxt_pP->rntiMaybeUEid);
  } else {
    /* we look up by CU UE ID! Do NOT change back to RNTI! */
    ue_context_p = rrc_gNB_get_ue_context(gnb_rrc_inst, ctxt_pP->rntiMaybeUEid);
  }

  if (!ue_context_p) {
    LOG_E(RRC, "could not find UE context for CU UE ID %lu, aborting transaction\n", ctxt_pP->rntiMaybeUEid);
    return -1;
  }

  if ((Srb_id != 1) && (Srb_id != 2)) {
    LOG_E(NR_RRC, "Received message on SRB%ld, should not have ...\n", Srb_id);
  } else {
    LOG_D(NR_RRC, "Received message on SRB%ld\n", Srb_id);
  }

  LOG_D(NR_RRC, "Decoding UL-DCCH Message\n");
  {
    for (int i = 0; i < sdu_sizeP; i++) {
      LOG_T(NR_RRC, "%x.", Rx_sdu[i]);
    }

    LOG_T(NR_RRC, "\n");
  }

  NR_UL_DCCH_Message_t *ul_dcch_msg = NULL;
  asn_dec_rval_t dec_rval = uper_decode(NULL, &asn_DEF_NR_UL_DCCH_Message, (void **)&ul_dcch_msg, Rx_sdu, sdu_sizeP, 0, 0);

  if ((dec_rval.code != RC_OK) && (dec_rval.consumed == 0)) {
    LOG_E(NR_RRC, "Failed to decode UL-DCCH (%zu bytes)\n", dec_rval.consumed);
    return -1;
  }

  if (LOG_DEBUGFLAG(DEBUG_ASN1)) {
    xer_fprint(stdout, &asn_DEF_NR_UL_DCCH_Message, (void *)ul_dcch_msg);
  }

  

  //int CC_id = ue_context_p->ue_context.primaryCC_id;
  if (ul_dcch_msg->message.present == NR_UL_DCCH_MessageType_PR_c1) {
    switch (ul_dcch_msg->message.choice.c1->present) {
      case NR_UL_DCCH_MessageType__c1_PR_NOTHING:
        LOG_I(NR_RRC, "Received PR_NOTHING on UL-DCCH-Message\n");
        break;

      case NR_UL_DCCH_MessageType__c1_PR_rrcReconfigurationComplete:
        handle_rrcReconfigurationComplete(ctxt_pP, ue_context_p, ul_dcch_msg->message.choice.c1->choice.rrcReconfigurationComplete);
        break;

      case NR_UL_DCCH_MessageType__c1_PR_rrcSetupComplete:
        if (handle_rrcSetupComplete(ctxt_pP, ue_context_p, ul_dcch_msg->message.choice.c1->choice.rrcSetupComplete) == -1)
          return -1;
        break;

      case NR_UL_DCCH_MessageType__c1_PR_measurementReport:
        DevAssert(ul_dcch_msg != NULL
                  && ul_dcch_msg->message.present == NR_UL_DCCH_MessageType_PR_c1
                  && ul_dcch_msg->message.choice.c1
                  && ul_dcch_msg->message.choice.c1->present == NR_UL_DCCH_MessageType__c1_PR_measurementReport);
        rrc_gNB_process_MeasurementReport(ue_context_p, ul_dcch_msg->message.choice.c1->choice.measurementReport);
        break;

      case NR_UL_DCCH_MessageType__c1_PR_ulInformationTransfer:
        LOG_D(NR_RRC, "Recived RRC GNB UL Information Transfer \n");
        if (!ue_context_p) {
          LOG_W(NR_RRC, "Processing ulInformationTransfer UE %lx, ue_context_p is NULL\n", ctxt_pP->rntiMaybeUEid);
          break;
        }

        LOG_D(NR_RRC, "[MSG] RRC UL Information Transfer \n");
        LOG_DUMPMSG(RRC, DEBUG_RRC, (char *)Rx_sdu, sdu_sizeP, "[MSG] RRC UL Information Transfer \n");

        if (get_softmodem_params()->sa) {
          rrc_gNB_send_NGAP_UPLINK_NAS(ctxt_pP, ue_context_p, ul_dcch_msg);
        }
        break;

      case NR_UL_DCCH_MessageType__c1_PR_securityModeComplete:
        // to avoid segmentation fault
        if (!ue_context_p) {
          LOG_I(NR_RRC, "Processing securityModeComplete UE %lx, ue_context_p is NULL\n", ctxt_pP->rntiMaybeUEid);
          break;
        }

        LOG_I(NR_RRC,
              PROTOCOL_NR_RRC_CTXT_UE_FMT " received securityModeComplete on UL-DCCH %d from UE\n",
              PROTOCOL_NR_RRC_CTXT_UE_ARGS(ctxt_pP),
              DCCH);
        LOG_D(NR_RRC,
              PROTOCOL_NR_RRC_CTXT_UE_FMT
              " RLC RB %02d --- RLC_DATA_IND %d bytes "
              "(securityModeComplete) ---> RRC_eNB\n",
              PROTOCOL_NR_RRC_CTXT_UE_ARGS(ctxt_pP),
              DCCH,
              sdu_sizeP);

        if (LOG_DEBUGFLAG(DEBUG_ASN1)) {
          xer_fprint(stdout, &asn_DEF_NR_UL_DCCH_Message, (void *)ul_dcch_msg);
        }

        /* configure ciphering */
        // FIXME: Commented for Bug 124092, no need to NGAP key recalculation
        // nr_rrc_pdcp_config_security(ctxt_pP, ue_context_p, 1);
        nr_pdcp_config_set_smc(ctxt_pP->rntiMaybeUEid, true);

        if (RC.ss.mode == SS_GNB) {
          rrc_gNB_generate_UECapabilityEnquiry(ctxt_pP, ue_context_p);
        }
        break;

      case NR_UL_DCCH_MessageType__c1_PR_securityModeFailure:
        LOG_DUMPMSG(NR_RRC, DEBUG_RRC, (char *)Rx_sdu, sdu_sizeP, "[MSG] NR RRC Security Mode Failure\n");
        LOG_W(NR_RRC,
              PROTOCOL_RRC_CTXT_UE_FMT
              " RLC RB %02d --- RLC_DATA_IND %d bytes "
              "(securityModeFailure) ---> RRC_gNB\n",
              PROTOCOL_RRC_CTXT_UE_ARGS(ctxt_pP),
              DCCH,
              sdu_sizeP);

        if (LOG_DEBUGFLAG(DEBUG_ASN1)) {
          xer_fprint(stdout, &asn_DEF_NR_UL_DCCH_Message, (void *)ul_dcch_msg);
        }

        rrc_gNB_generate_UECapabilityEnquiry(ctxt_pP, ue_context_p);
        break;

      case NR_UL_DCCH_MessageType__c1_PR_ueCapabilityInformation:
        if (handle_ueCapabilityInformation(ctxt_pP, ue_context_p, ul_dcch_msg->message.choice.c1->choice.ueCapabilityInformation)
            == -1)
          return -1;
        break;

      case NR_UL_DCCH_MessageType__c1_PR_rrcReestablishmentComplete:
        if (handle_rrcReestablishmentComplete(ctxt_pP, ue_context_p, ul_dcch_msg->message.choice.c1->choice.rrcReestablishmentComplete)
            == -1)
          return -1;
        break;

      default:
        break;
    }
  }
  ASN_STRUCT_FREE(asn_DEF_NR_UL_DCCH_Message, ul_dcch_msg);
  return 0;
}

static bool rrc_gNB_plmn_matches(const gNB_RRC_INST *rrc, const f1ap_served_cell_info_t *info)
{
  const gNB_RrcConfigurationReq *conf = &rrc->configuration;
  return conf->num_plmn == 1 // F1 supports only one
    && conf->mcc[0] == info->plmn.mcc
    && conf->mnc[0] == info->plmn.mnc
    && rrc->nr_cellid == info->nr_cellid;
}

static void rrc_gNB_process_f1_setup_req(f1ap_setup_req_t *req, sctp_assoc_t assoc_id)
{
  gNB_RRC_INST *rrc = RC.nrrrc[0];
  DevAssert(rrc);

  LOG_I(NR_RRC, "Received F1 Setup Request from gNB_DU %lu (%s) on assoc_id %d\n", req->gNB_DU_id, req->gNB_DU_name, assoc_id);

  // check:
  // - it is the first DU
  // - it is one cell
  // - PLMN and Cell ID matches
  // else reject
  f1ap_setup_failure_t fail = {.cause = F1AP_CauseRadioNetwork_gNB_CU_Cell_Capacity_Exceeded};
  if (rrc->du != NULL) {
    const f1ap_setup_req_t *other = rrc->du->setup_req;
    LOG_E(NR_RRC, "can only handle one DU, but already serving DU %ld (%s)\n", other->gNB_DU_id, other->gNB_DU_name);
    rrc->mac_rrc.f1_setup_failure(&fail);
    return;
  }
  if (req->num_cells_available != 1) {
    LOG_E(NR_RRC, "can only handle on DU cell, but gNB_DU %ld has %d\n", req->gNB_DU_id, req->num_cells_available);
    rrc->mac_rrc.f1_setup_failure(&fail);
    return;
  }
  f1ap_served_cell_info_t *cell_info = &req->cell[0].info;
  if (!rrc_gNB_plmn_matches(rrc, cell_info)) {
    LOG_E(NR_RRC,
          "PLMN mismatch: CU %d%d cellID %ld, DU %d%d cellID %ld\n",
          rrc->configuration[0].mcc[0],
          rrc->configuration[0].mnc[0],
          rrc->nr_cellid,
          cell_info->plmn.mcc,
          cell_info->plmn.mnc,
          cell_info->nr_cellid);
    rrc->mac_rrc.f1_setup_failure(&fail);
    return;
  }
  // if there is no system info or no SIB1 and we run in SA mode, we cannot handle it
  const f1ap_gnb_du_system_info_t *sys_info = req->cell[0].sys_info;
  if (sys_info == NULL || sys_info->mib == NULL || (sys_info->sib1 == NULL && get_softmodem_params()->sa)) {
    LOG_E(NR_RRC, "no system information provided by DU, rejecting\n");
    rrc->mac_rrc.f1_setup_failure(&fail);
    return;
  }

  /* do we need the MIB? for the moment, just check it is valid, then drop it */
  NR_BCCH_BCH_Message_t *mib = NULL;
  asn_dec_rval_t dec_rval =
      uper_decode_complete(NULL, &asn_DEF_NR_BCCH_BCH_Message, (void **)&mib, sys_info->mib, sys_info->mib_length);
  if (dec_rval.code != RC_OK || mib->message.present != NR_BCCH_BCH_MessageType_PR_mib
      || mib->message.choice.messageClassExtension == NULL) {
    LOG_E(RRC, "Failed to decode NR_BCCH_BCH_MESSAGE (%zu bits) of DU, rejecting DU\n", dec_rval.consumed);
    rrc->mac_rrc.f1_setup_failure(&fail);
    ASN_STRUCT_FREE(asn_DEF_NR_BCCH_BCH_Message, mib);
    return;
  }

  NR_SIB1_t *sib1 = NULL;
  if (sys_info->sib1) {
    dec_rval = uper_decode_complete(NULL, &asn_DEF_NR_SIB1, (void **)&sib1, sys_info->sib1, sys_info->sib1_length);
    if (dec_rval.code != RC_OK) {
      LOG_E(RRC, "Failed to decode NR_SIB1 (%zu bits) of DU, rejecting DU\n", dec_rval.consumed);
      rrc->mac_rrc.f1_setup_failure(&fail);
      ASN_STRUCT_FREE(asn_DEF_NR_SIB1, sib1);
      return;
    }
    if (LOG_DEBUGFLAG(DEBUG_ASN1))
      xer_fprint(stdout, &asn_DEF_NR_SIB1, sib1);
  }

  LOG_I(RRC, "Accepting DU %ld (%s), sending F1 Setup Response\n", req->gNB_DU_id, req->gNB_DU_name);

  // we accept the DU
  rrc->du = calloc(1, sizeof(*rrc->du));
  AssertFatal(rrc->du != NULL, "out of memory\n");
  rrc->du->assoc_id = assoc_id;

  /* ITTI will free the setup request message via free(). So the memory
   * "inside" of the message will remain, but the "outside" container no, so
   * allocate memory and copy it in */
  rrc->du->setup_req = malloc(sizeof(*rrc->du->setup_req));
  AssertFatal(rrc->du->setup_req != NULL, "out of memory\n");
  *rrc->du->setup_req = *req;
  rrc->du->mib = mib->message.choice.mib;
  mib->message.choice.mib = NULL;
  ASN_STRUCT_FREE(asn_DEF_NR_BCCH_BCH_MessageType, mib);
  rrc->du->sib1 = sib1;

  served_cells_to_activate_t cell = {
      .plmn = cell_info->plmn,
      .nr_cellid = cell_info->nr_cellid,
      .nrpci = cell_info->nr_pci,
      .num_SI = 0,
  };
  f1ap_setup_resp_t resp = {.num_cells_to_activate = 1, .cells_to_activate[0] = cell};
  if (rrc->node_name != NULL)
    resp.gNB_CU_name = strdup(rrc->node_name);
  rrc->mac_rrc.f1_setup_response(&resp);

  /*
  MessageDef *msg_p2 = itti_alloc_new_message(TASK_RRC_GNB, 0, F1AP_GNB_CU_CONFIGURATION_UPDATE);
  F1AP_GNB_CU_CONFIGURATION_UPDATE(msg_p2).gNB_CU_name = rrc->node_name;
  F1AP_GNB_CU_CONFIGURATION_UPDATE(msg_p2).cells_to_activate[0].plmn.mcc = rrc->configuration.mcc[0];
  F1AP_GNB_CU_CONFIGURATION_UPDATE(msg_p2).cells_to_activate[0].plmn.mnc = rrc->configuration.mnc[0];
  F1AP_GNB_CU_CONFIGURATION_UPDATE(msg_p2).cells_to_activate[0].plmn.mnc_digit_length = rrc->configuration.mnc_digit_length[0];
  F1AP_GNB_CU_CONFIGURATION_UPDATE(msg_p2).cells_to_activate[0].nr_cellid = rrc->nr_cellid;
  F1AP_GNB_CU_CONFIGURATION_UPDATE(msg_p2).cells_to_activate[0].nrpci = req->cell[0].info.nr_pci;
  int num_SI = 0;

  if (rrc->carrier.SIB23) {
    F1AP_GNB_CU_CONFIGURATION_UPDATE(msg_p2).cells_to_activate[0].SI_container[2] = rrc->carrier.SIB23;
    F1AP_GNB_CU_CONFIGURATION_UPDATE(msg_p2).cells_to_activate[0].SI_container_length[2] = rrc->carrier.sizeof_SIB23;
    num_SI++;
  }

  F1AP_GNB_CU_CONFIGURATION_UPDATE(msg_p2).cells_to_activate[0].num_SI = num_SI;
  F1AP_GNB_CU_CONFIGURATION_UPDATE(msg_p2).num_cells_to_activate = 1;
  // send
  itti_send_msg_to_task(TASK_CU_F1, 0, msg_p2);
  */

}

void rrc_gNB_process_initial_ul_rrc_message(const f1ap_initial_ul_rrc_message_t *ul_rrc)
{
  // first get RRC instance (note, no the ITTI instance)
  module_id_t i = 0;
  gNB_RRC_INST *rrc = NULL;
  for (i=0; i < RC.nb_nr_inst; i++) {
    rrc = RC.nrrrc[i];
    if (rrc->nr_cellid == ul_rrc->nr_cellid)
      break;
  }
  //AssertFatal(i != RC.nb_nr_inst, "Cell_id not found\n");
  // TODO REMOVE_DU_RRC in monolithic mode, the MAC does not have the
  // nr_cellid. Thus, the above check would fail. For the time being, just put
  // a warning, as we handle one DU only anyway
  if (i == RC.nb_nr_inst) {
    i = 0;
    LOG_W(RRC, "initial UL RRC message nr_cellid %ld does not match RRC's %ld\n", ul_rrc->nr_cellid, RC.nrrrc[0]->nr_cellid);
  }else{
    LOG_W(RRC, "initial UL RRC message nr_cellid %ld  match RRC's %ld\n", ul_rrc->nr_cellid, RC.nrrrc[0]->nr_cellid);
  }

  protocol_ctxt_t ctxt = { 0 };
  PROTOCOL_CTXT_SET_BY_INSTANCE(&ctxt, i, GNB_FLAG_YES, ul_rrc->crnti, 0, 0);

  LOG_I(NR_RRC,"fxn:%s NRRRC Sending CCCH_PDU_IND/SS_NRRRC_PDU_IND to TASK_SS_SRB_GNB (msg_Id:%d) [SFN: %d, SF: %d] physCellId   %d \n",
    __FUNCTION__, SS_NRRRC_PDU_IND, ctxt.frame, ctxt.subframe, RC.nrrrc[0]->carrier[ul_rrc->nr_cellid].physCellId);
  MessageDef *message_p = itti_alloc_new_message (TASK_SS_SRB_GNB, INSTANCE_DEFAULT, SS_NRRRC_PDU_IND);
  if (message_p) {
    /* Populate the message to SS */
    SS_NRRRC_PDU_IND (message_p).sdu_size = ul_rrc->rrc_container_length;
    SS_NRRRC_PDU_IND (message_p).srb_id = 0;
    SS_NRRRC_PDU_IND (message_p).rnti = ul_rrc->crnti;
    SS_NRRRC_PDU_IND (message_p).frame = ctxt.frame;
    SS_NRRRC_PDU_IND (message_p).subframe = ctxt.subframe;
    SS_NRRRC_PDU_IND (message_p).physCellId = RC.nrrrc[0]->carrier[ul_rrc->nr_cellid].physCellId;
    memset (SS_NRRRC_PDU_IND (message_p).sdu, 0, SDU_SIZE);
    memcpy (SS_NRRRC_PDU_IND (message_p).sdu, ul_rrc->rrc_container, ul_rrc->rrc_container_length);

    int send_res = itti_send_msg_to_task (TASK_SS_SRB_GNB, INSTANCE_DEFAULT, message_p);
    if (send_res < 0) {
      LOG_E(RRC,"Error in sending CCCH_PDU_IND/SS_NRRRC_PDU_IND to TASK_SS_SRB_GNB (msg_Id:%d) to TASK_SS_SRB_GNB\n", SS_NRRRC_PDU_IND);
    }
  }
  nr_rrc_gNB_decode_ccch(i, ul_rrc, ul_rrc->nr_cellid);
  if (ul_rrc->rrc_container)
    free(ul_rrc->rrc_container);
  if (ul_rrc->du2cu_rrc_container)
    free(ul_rrc->du2cu_rrc_container);
}

void rrc_gNB_process_release_request(const module_id_t gnb_mod_idP, x2ap_ENDC_sgnb_release_request_t *m)
{
  gNB_RRC_INST *rrc = RC.nrrrc[gnb_mod_idP];
  rrc_remove_nsa_user(rrc, m->rnti);
}

void rrc_gNB_process_dc_overall_timeout(const module_id_t gnb_mod_idP, x2ap_ENDC_dc_overall_timeout_t *m)
{
  gNB_RRC_INST *rrc = RC.nrrrc[gnb_mod_idP];
  rrc_remove_nsa_user(rrc, m->rnti);
}

static void rrc_CU_process_ue_context_setup_response(MessageDef *msg_p, instance_t instance)
{
  f1ap_ue_context_setup_t *resp = &F1AP_UE_CONTEXT_SETUP_RESP(msg_p);
  gNB_RRC_INST *rrc = RC.nrrrc[instance];
  rrc_gNB_ue_context_t *ue_context_p = rrc_gNB_get_ue_context(rrc, resp->gNB_CU_ue_id);
  if (!ue_context_p) {
    LOG_E(RRC, "could not find UE context for CU UE ID %u, aborting transaction\n", resp->gNB_CU_ue_id);
    return;
  }
  gNB_RRC_UE_t *UE = &ue_context_p->ue_context;

  NR_CellGroupConfig_t *cellGroupConfig = NULL;
  asn_dec_rval_t dec_rval = uper_decode_complete(NULL,
                                                 &asn_DEF_NR_CellGroupConfig,
                                                 (void **)&cellGroupConfig,
                                                 (uint8_t *)resp->du_to_cu_rrc_information->cellGroupConfig,
                                                 resp->du_to_cu_rrc_information->cellGroupConfig_length);
  AssertFatal(dec_rval.code == RC_OK && dec_rval.consumed > 0, "Cell group config decode error\n");

  if (UE->masterCellGroup) {
    ASN_STRUCT_FREE(asn_DEF_NR_CellGroupConfig, UE->masterCellGroup);
    LOG_I(RRC, "UE %04x replacing existing CellGroupConfig with new one received from DU\n", UE->rnti);
  }
  UE->masterCellGroup = cellGroupConfig;
  if (LOG_DEBUGFLAG(DEBUG_ASN1))
    xer_fprint(stdout, &asn_DEF_NR_CellGroupConfig, UE->masterCellGroup);

  /* at this point, we don't have to do anything: the UE context setup request
   * includes the Security Command, whose response will trigger the following
   * messages (UE capability, to be specific) */
}

static void rrc_CU_process_ue_context_release_request(MessageDef *msg_p)
{
  const int instance = 0;
  f1ap_ue_context_release_req_t *req = &F1AP_UE_CONTEXT_RELEASE_REQ(msg_p);
  gNB_RRC_INST *rrc = RC.nrrrc[instance];
  rrc_gNB_ue_context_t *ue_context_p = rrc_gNB_get_ue_context(rrc, req->gNB_CU_ue_id);
  if (!ue_context_p) {
    LOG_E(RRC, "could not find UE context for CU UE ID %u, aborting transaction\n", req->gNB_CU_ue_id);
    return;
  }

  /* TODO: marshall types correctly */
  LOG_I(NR_RRC, "received UE Context Release Request for UE %u, forwarding to AMF\n", req->gNB_CU_ue_id);
  rrc_gNB_send_NGAP_UE_CONTEXT_RELEASE_REQ(instance,
                                           ue_context_p,
                                           NGAP_CAUSE_RADIO_NETWORK,
                                           NGAP_CAUSE_RADIO_NETWORK_RADIO_CONNECTION_WITH_UE_LOST);
}

static void rrc_delete_ue_data(gNB_RRC_UE_t *UE)
{
  ASN_STRUCT_FREE(asn_DEF_NR_UE_NR_Capability, UE->UE_Capability_nr);
  ASN_STRUCT_FREE(asn_DEF_NR_CellGroupConfig, UE->masterCellGroup);
  ASN_STRUCT_FREE(asn_DEF_NR_MeasResults, UE->measResults);
}

static void rrc_CU_process_ue_context_release_complete(MessageDef *msg_p)
{
  const int instance = 0;
  f1ap_ue_context_release_complete_t *complete = &F1AP_UE_CONTEXT_RELEASE_COMPLETE(msg_p);
  gNB_RRC_INST *rrc = RC.nrrrc[instance];
  rrc_gNB_ue_context_t *ue_context_p = rrc_gNB_get_ue_context(rrc, complete->gNB_CU_ue_id);
  if (!ue_context_p) {
    LOG_E(RRC, "could not find UE context for CU UE ID %u, aborting transaction\n", complete->gNB_CU_ue_id);
    return;
  }
  gNB_RRC_UE_t *UE = &ue_context_p->ue_context;

  nr_pdcp_remove_UE(UE->rrc_ue_id);
  newGtpuDeleteAllTunnels(instance, UE->rrc_ue_id);
  rrc_gNB_send_NGAP_UE_CONTEXT_RELEASE_COMPLETE(instance, UE->rrc_ue_id);
  LOG_I(NR_RRC, "removed UE CU UE ID %u/RNTI %04x \n", UE->rrc_ue_id, UE->rnti);
  rrc_delete_ue_data(UE);
  rrc_gNB_remove_ue_context(rrc, ue_context_p);
}

static void rrc_CU_process_ue_context_modification_response(MessageDef *msg_p, instance_t instance)
{
  f1ap_ue_context_modif_resp_t *resp = &F1AP_UE_CONTEXT_MODIFICATION_RESP(msg_p);
  protocol_ctxt_t ctxt = {.rntiMaybeUEid = resp->gNB_CU_ue_id, .module_id = instance, .instance = instance, .enb_flag = 1, .eNB_index = instance};
  gNB_RRC_INST *rrc = RC.nrrrc[ctxt.module_id];
  rrc_gNB_ue_context_t *ue_context_p = rrc_gNB_get_ue_context(rrc, resp->gNB_CU_ue_id);
  if (!ue_context_p) {
    LOG_E(RRC, "could not find UE context for CU UE ID %u, aborting transaction\n", resp->gNB_CU_ue_id);
    return;
  }
  gNB_RRC_UE_t *UE = &ue_context_p->ue_context;

  if (resp->drbs_to_be_setup_length > 0) {
    e1ap_bearer_setup_req_t req = {0};
    req.numPDUSessionsMod = UE->nb_of_pdusessions;
    req.gNB_cu_cp_ue_id = UE->rrc_ue_id;
    req.gNB_cu_up_ue_id = UE->rrc_ue_id;
    for (int i = 0; i < req.numPDUSessionsMod; i++) {
      req.pduSessionMod[i].numDRB2Modify = resp->drbs_to_be_setup_length;
      for (int j = 0; j < resp->drbs_to_be_setup_length; j++) {
        f1ap_drb_to_be_setup_t *drb_f1 = resp->drbs_to_be_setup + j;
        DRB_nGRAN_to_setup_t *drb_e1 = req.pduSessionMod[i].DRBnGRanModList + j;

        drb_e1->id = drb_f1->drb_id;
        drb_e1->numDlUpParam = drb_f1->up_dl_tnl_length;
        drb_e1->DlUpParamList[0].tlAddress = drb_f1->up_dl_tnl[0].tl_address;
        drb_e1->DlUpParamList[0].teId = drb_f1->up_dl_tnl[0].teid;
      }
    }

    // send the F1 response message up to update F1-U tunnel info
    // it seems the rrc transaction id (xid) is not needed here
    rrc->cucp_cuup.bearer_context_mod(&req, instance);
  }

  if (resp->du_to_cu_rrc_information != NULL && resp->du_to_cu_rrc_information->cellGroupConfig != NULL) {
    LOG_W(RRC, "UE context modification response contains new CellGroupConfig for UE %04x, triggering reconfiguration\n", UE->rnti);
    NR_CellGroupConfig_t *cellGroupConfig = NULL;
    asn_dec_rval_t dec_rval = uper_decode_complete(NULL,
                                                   &asn_DEF_NR_CellGroupConfig,
                                                   (void **)&cellGroupConfig,
                                                   (uint8_t *)resp->du_to_cu_rrc_information->cellGroupConfig,
                                                   resp->du_to_cu_rrc_information->cellGroupConfig_length);
    AssertFatal(dec_rval.code == RC_OK && dec_rval.consumed > 0, "Cell group config decode error\n");

    if (UE->masterCellGroup) {
      ASN_STRUCT_FREE(asn_DEF_NR_CellGroupConfig, UE->masterCellGroup);
      LOG_I(RRC, "UE %04x replacing existing CellGroupConfig with new one received from DU\n", UE->rnti);
    }
    UE->masterCellGroup = cellGroupConfig;

    rrc_gNB_generate_dedicatedRRCReconfiguration(&ctxt, ue_context_p);
  }
}

static void rrc_CU_process_ue_modification_required(MessageDef *msg_p)
{
  f1ap_ue_context_modif_required_t *required = &F1AP_UE_CONTEXT_MODIFICATION_REQUIRED(msg_p);
  protocol_ctxt_t ctxt = {.rntiMaybeUEid = required->gNB_CU_ue_id, .module_id = 0, .instance = 0, .enb_flag = 1, .eNB_index = 0};
  gNB_RRC_INST *rrc = RC.nrrrc[ctxt.module_id];
  rrc_gNB_ue_context_t *ue_context_p = rrc_gNB_get_ue_context(rrc, required->gNB_CU_ue_id);
  if (ue_context_p == NULL) {
    LOG_E(RRC, "Could not find UE context for CU UE ID %d, cannot handle UE context modification request\n", required->gNB_CU_ue_id);
    f1ap_ue_context_modif_refuse_t refuse = {
      .gNB_CU_ue_id = required->gNB_CU_ue_id,
      .gNB_DU_ue_id = required->gNB_DU_ue_id,
      .cause = F1AP_CAUSE_RADIO_NETWORK,
      .cause_value = F1AP_CauseRadioNetwork_unknown_or_already_allocated_gnb_cu_ue_f1ap_id,
    };
    rrc->mac_rrc.ue_context_modification_refuse(&refuse);
    return;
  }

  if (required->du_to_cu_rrc_information && required->du_to_cu_rrc_information->cellGroupConfig) {
    gNB_RRC_UE_t *UE = &ue_context_p->ue_context;
    LOG_I(RRC,
          "UE Context Modification Required: new CellGroupConfig for UE ID %d/RNTI %04x, triggering reconfiguration\n",
          UE->rrc_ue_id,
          UE->rnti);

    NR_CellGroupConfig_t *cellGroupConfig = NULL;
    asn_dec_rval_t dec_rval = uper_decode_complete(NULL,
                                                   &asn_DEF_NR_CellGroupConfig,
                                                   (void **)&cellGroupConfig,
                                                   (uint8_t *)required->du_to_cu_rrc_information->cellGroupConfig,
                                                   required->du_to_cu_rrc_information->cellGroupConfig_length);
    if (dec_rval.code != RC_OK && dec_rval.consumed == 0) {
      LOG_E(RRC, "Cell group config decode error, refusing reconfiguration\n");
      f1ap_ue_context_modif_refuse_t refuse = {
        .gNB_CU_ue_id = required->gNB_CU_ue_id,
        .gNB_DU_ue_id = required->gNB_DU_ue_id,
        .cause = F1AP_CAUSE_PROTOCOL,
        .cause_value = F1AP_CauseProtocol_transfer_syntax_error,
      };
      rrc->mac_rrc.ue_context_modification_refuse(&refuse);
      return;
    }

    if (UE->masterCellGroup) {
      ASN_STRUCT_FREE(asn_DEF_NR_CellGroupConfig, UE->masterCellGroup);
      LOG_I(RRC, "UE %d/RNTI %04x replacing existing CellGroupConfig with new one received from DU\n", UE->rrc_ue_id, UE->rnti);
    }
    UE->masterCellGroup = cellGroupConfig;
    if (LOG_DEBUGFLAG(DEBUG_ASN1))
      xer_fprint(stdout, &asn_DEF_NR_CellGroupConfig, UE->masterCellGroup);

    /* trigger reconfiguration */
    nr_rrc_reconfiguration_req(ue_context_p, &ctxt, 0, 0);
    //rrc_gNB_generate_dedicatedRRCReconfiguration(&ctxt, ue_context_p);
    //rrc_gNB_generate_defaultRRCReconfiguration(&ctxt, ue_context_p);
    return;
  }
  LOG_W(RRC,
        "nothing to be done after UE Context Modification Required for UE ID %d/RNTI %04x\n",
        required->gNB_CU_ue_id,
        required->gNB_DU_ue_id);
}

unsigned int mask_flip(unsigned int x) {
  return((((x>>8) + (x<<8))&0xffff)>>6);
}

static unsigned int get_dl_bw_mask(const f1ap_served_cell_info_t *cell_info, const NR_UE_NR_Capability_t *cap)
{
  int common_band = get_dl_band(cell_info);
  int common_scs  = get_ssb_scs(cell_info);
// static unsigned int get_dl_bw_mask(const gNB_RRC_INST *rrc, const NR_UE_NR_Capability_t *cap, const int CC_id)  //TODO W38: to check if ccid needed
// {
//   int common_band = *rrc->carrier[CC_id].servingcellconfigcommon->downlinkConfigCommon->frequencyInfoDL->frequencyBandList.list.array[0];
//   int common_scs  = rrc->carrier[CC_id].servingcellconfigcommon->downlinkConfigCommon->frequencyInfoDL->scs_SpecificCarrierList.list.array[0]->subcarrierSpacing;
  for (int i=0;i<cap->rf_Parameters.supportedBandListNR.list.count;i++) {
     NR_BandNR_t *bandNRinfo = cap->rf_Parameters.supportedBandListNR.list.array[i];
     if (bandNRinfo->bandNR == common_band) {
       if (common_band < 257) { // FR1
          switch (common_scs) {
            case NR_SubcarrierSpacing_kHz15 :
               if (bandNRinfo->channelBWs_DL &&
                   bandNRinfo->channelBWs_DL->choice.fr1 &&
                   bandNRinfo->channelBWs_DL->choice.fr1->scs_15kHz)
                     return(mask_flip((unsigned int)*(uint16_t*)bandNRinfo->channelBWs_DL->choice.fr1->scs_15kHz->buf));
        break;
            case NR_SubcarrierSpacing_kHz30 :
               if (bandNRinfo->channelBWs_DL &&
                   bandNRinfo->channelBWs_DL->choice.fr1 &&
                   bandNRinfo->channelBWs_DL->choice.fr1->scs_30kHz)
                     return(mask_flip((unsigned int)*(uint16_t*)bandNRinfo->channelBWs_DL->choice.fr1->scs_30kHz->buf));
              break;
            case NR_SubcarrierSpacing_kHz60 :
               if (bandNRinfo->channelBWs_DL &&
                   bandNRinfo->channelBWs_DL->choice.fr1 &&
                   bandNRinfo->channelBWs_DL->choice.fr1->scs_60kHz)
                     return(mask_flip((unsigned int)*(uint16_t*)bandNRinfo->channelBWs_DL->choice.fr1->scs_60kHz->buf));
              break;
          }
       }
       else {
          switch (common_scs) {
            case NR_SubcarrierSpacing_kHz60 :
               if (bandNRinfo->channelBWs_DL &&
                   bandNRinfo->channelBWs_DL->choice.fr2 &&
                   bandNRinfo->channelBWs_DL->choice.fr2->scs_60kHz)
                     return(mask_flip((unsigned int)*(uint16_t*)bandNRinfo->channelBWs_DL->choice.fr2->scs_60kHz->buf));
              break;
            case NR_SubcarrierSpacing_kHz120 :
               if (bandNRinfo->channelBWs_DL &&
                   bandNRinfo->channelBWs_DL->choice.fr2 &&
                   bandNRinfo->channelBWs_DL->choice.fr2->scs_120kHz)
                     return(mask_flip((unsigned int)*(uint16_t*)bandNRinfo->channelBWs_DL->choice.fr2->scs_120kHz->buf));
              break;
       }
     }
   }
  }
  return(0);
}

static unsigned int get_ul_bw_mask(const f1ap_served_cell_info_t *cell_info, const NR_UE_NR_Capability_t *cap)
{
  int common_band = get_ul_band(cell_info);
  int common_scs  = get_ssb_scs(cell_info);
// static unsigned int get_ul_bw_mask(const gNB_RRC_INST *rrc, const NR_UE_NR_Capability_t *cap, const int CC_id) //TODO W38:
// {
//   int common_band = *rrc->carrier[CC_id].servingcellconfigcommon->uplinkConfigCommon->frequencyInfoUL->frequencyBandList->list.array[0];
//   int common_scs  = rrc->carrier[CC_id].servingcellconfigcommon->uplinkConfigCommon->frequencyInfoUL->scs_SpecificCarrierList.list.array[0]->subcarrierSpacing;
  for (int i=0;i<cap->rf_Parameters.supportedBandListNR.list.count;i++) {
     NR_BandNR_t *bandNRinfo = cap->rf_Parameters.supportedBandListNR.list.array[i];
     if (bandNRinfo->bandNR == common_band) {
       if (common_band < 257) { // FR1
          switch (common_scs) {
            case NR_SubcarrierSpacing_kHz15 :
               if (bandNRinfo->channelBWs_UL &&
                   bandNRinfo->channelBWs_UL->choice.fr1 &&
                   bandNRinfo->channelBWs_UL->choice.fr1->scs_15kHz)
                     return(mask_flip((unsigned int)*(uint16_t*)bandNRinfo->channelBWs_UL->choice.fr1->scs_15kHz->buf));
        break;
            case NR_SubcarrierSpacing_kHz30 :
               if (bandNRinfo->channelBWs_UL &&
                   bandNRinfo->channelBWs_UL->choice.fr1 &&
                   bandNRinfo->channelBWs_UL->choice.fr1->scs_30kHz)
                     return(mask_flip((unsigned int)*(uint16_t*)bandNRinfo->channelBWs_UL->choice.fr1->scs_30kHz->buf));
              break;
            case NR_SubcarrierSpacing_kHz60 :
               if (bandNRinfo->channelBWs_UL &&
                   bandNRinfo->channelBWs_UL->choice.fr1 &&
                   bandNRinfo->channelBWs_UL->choice.fr1->scs_60kHz)
                     return(mask_flip((unsigned int)*(uint16_t*)bandNRinfo->channelBWs_UL->choice.fr1->scs_60kHz->buf));
              break;
          }
       }
       else {
          switch (common_scs) {
            case NR_SubcarrierSpacing_kHz60 :
               if (bandNRinfo->channelBWs_UL &&
                   bandNRinfo->channelBWs_UL->choice.fr2 &&
                   bandNRinfo->channelBWs_UL->choice.fr2->scs_60kHz)
                     return(mask_flip((unsigned int)*(uint16_t*)bandNRinfo->channelBWs_UL->choice.fr2->scs_60kHz->buf));
              break;
            case NR_SubcarrierSpacing_kHz120 :
               if (bandNRinfo->channelBWs_UL &&
                   bandNRinfo->channelBWs_UL->choice.fr2 &&
                   bandNRinfo->channelBWs_UL->choice.fr2->scs_120kHz)
                     return(mask_flip((unsigned int)*(uint16_t*)bandNRinfo->channelBWs_UL->choice.fr2->scs_120kHz->buf));
              break;
       }
     }
   }
  }
  return(0);
}

static int get_ul_mimo_layersCB(const f1ap_served_cell_info_t *cell_info, const NR_UE_NR_Capability_t *cap)
{
  int common_scs  = get_ssb_scs(cell_info);
// static int get_ul_mimo_layersCB(const gNB_RRC_INST *rrc, const NR_UE_NR_Capability_t *cap, const int CC_id) //TODO W38:
// {
//   int common_scs  = rrc->carrier[CC_id].servingcellconfigcommon->uplinkConfigCommon->frequencyInfoUL->scs_SpecificCarrierList.list.array[0]->subcarrierSpacing;

  // check featureSet
  NR_FeatureSets_t *fs=cap->featureSets;
  if (fs) {
    // go through UL feature sets and look for one with current SCS
    for (int i=0;i<fs->featureSetsUplinkPerCC->list.count;i++) {
       if (fs->featureSetsUplinkPerCC->list.array[i]->supportedSubcarrierSpacingUL == common_scs &&
           fs->featureSetsUplinkPerCC->list.array[i]->mimo_CB_PUSCH &&
           fs->featureSetsUplinkPerCC->list.array[i]->mimo_CB_PUSCH->maxNumberMIMO_LayersCB_PUSCH)
           return(1<<*fs->featureSetsUplinkPerCC->list.array[i]->mimo_CB_PUSCH->maxNumberMIMO_LayersCB_PUSCH);
    }
  }
  return(1);
}

static int get_ul_mimo_layers(const f1ap_served_cell_info_t *cell_info, const NR_UE_NR_Capability_t *cap)
{
  int common_scs  = get_ssb_scs(cell_info);
// static int get_ul_mimo_layers(const gNB_RRC_INST *rrc, const NR_UE_NR_Capability_t *cap, const int CC_id)  //TODO W38:
// {
//   int common_scs  = rrc->carrier[CC_id].servingcellconfigcommon->uplinkConfigCommon->frequencyInfoUL->scs_SpecificCarrierList.list.array[0]->subcarrierSpacing;

  // check featureSet
  NR_FeatureSets_t *fs=cap->featureSets;
  if (fs) {
    // go through UL feature sets and look for one with current SCS
    for (int i=0;i<fs->featureSetsUplinkPerCC->list.count;i++) {
       if (fs->featureSetsUplinkPerCC->list.array[i]->supportedSubcarrierSpacingUL == common_scs &&
           fs->featureSetsUplinkPerCC->list.array[i]->maxNumberMIMO_LayersNonCB_PUSCH)
           return(1<<*fs->featureSetsUplinkPerCC->list.array[i]->maxNumberMIMO_LayersNonCB_PUSCH);
    }
  }
  return(1);
}

static int get_dl_mimo_layers(const f1ap_served_cell_info_t *cell_info, const NR_UE_NR_Capability_t *cap)
{
  int common_scs  = get_ssb_scs(cell_info);
// static int get_dl_mimo_layers(const gNB_RRC_INST *rrc, const NR_UE_NR_Capability_t *cap, const int CC_id) //TODO W38:
// {
//   int common_scs  = rrc->carrier[CC_id].servingcellconfigcommon->downlinkConfigCommon->frequencyInfoDL->scs_SpecificCarrierList.list.array[0]->subcarrierSpacing;

  // check featureSet
  NR_FeatureSets_t *fs=cap->featureSets;
  if (fs) {
    // go through UL feature sets and look for one with current SCS
    for (int i=0;i<fs->featureSetsDownlinkPerCC->list.count;i++) {
       if (fs->featureSetsUplinkPerCC->list.array[i]->supportedSubcarrierSpacingUL == common_scs &&
           fs->featureSetsDownlinkPerCC->list.array[i]->maxNumberMIMO_LayersPDSCH)
           return(2<<*fs->featureSetsDownlinkPerCC->list.array[i]->maxNumberMIMO_LayersPDSCH);
    }
  }
  return(1);
}


void nr_rrc_ss_subframe_process(protocol_ctxt_t *const ctxt_pP)
{
  if(RC.ss.mode < SS_SOFTMODEM){
    return;
  }

  MessageDef *msg;
  gNB_RRC_INST *rrc = RC.nrrrc[ctxt_pP->module_id];
  rrc_gNB_ue_context_t *ue_context_p = NULL;
  RB_FOREACH(ue_context_p, rrc_nr_ue_tree_s, &rrc->rrc_ue_head)
  {
    gNB_RRC_UE_t *UE = &ue_context_p->ue_context;
    ctxt_pP->rntiMaybeUEid = UE->rnti;

    if (UE->ue_release_timer_rrc > 0) {
      UE->ue_release_timer_rrc++;

      if (UE->ue_release_timer_rrc >= UE->ue_release_timer_thres_rrc) {
        LOG_I(NR_RRC, "Removing UE %x instance ue_release_timer_rrc timeout\n", UE->rnti);
        UE->ue_release_timer_rrc = 0;
         NR_SCHED_LOCK(&RC.nrmac[ctxt_pP->module_id]->sched_lock);
         mac_remove_nr_ue(RC.nrmac[ctxt_pP->module_id], UE->primaryCC_id, UE->rnti);
         NR_SCHED_UNLOCK(&RC.nrmac[ctxt_pP->module_id]->sched_lock);
        rrc_rlc_remove_ue(ctxt_pP);
        nr_pdcp_remove_UE(ctxt_pP->rntiMaybeUEid);

        /* remove RRC UE Context */
        LOG_I(NR_RRC, "remove UE %04x \n", UE->rnti);
        rrc_gNB_remove_ue_context(rrc, ue_context_p);
        break;/* break  RB_FOREACH */
      }
    }
  }
}

int rrc_gNB_process_e1_setup_req(e1ap_setup_req_t *req, instance_t instance) {

  AssertFatal(req->supported_plmns <= PLMN_LIST_MAX_SIZE, "Supported PLMNs is more than PLMN_LIST_MAX_SIZE\n");
  gNB_RRC_INST *rrc = RC.nrrrc[0]; //TODO: remove hardcoding of RC index here
  MessageDef *msg_p = itti_alloc_new_message(TASK_RRC_GNB, instance, E1AP_SETUP_RESP);

  e1ap_setup_resp_t *resp = &E1AP_SETUP_RESP(msg_p);
  resp->transac_id = req->transac_id;

  for (int i=0; i < req->supported_plmns; i++) {
    if (rrc->configuration[0].mcc[i] == req->plmns[i].mcc &&  //bugz128620: assuming this message is not used for Simulation. to verify
        rrc->configuration[0].mnc[i] == req->plmns[i].mnc) {
      LOG_E(NR_RRC, "PLMNs received from CUUP (mcc:%d, mnc:%d) did not match with PLMNs in RRC (mcc:%d, mnc:%d)\n",
            req->plmns[i].mcc, req->plmns[i].mnc, rrc->configuration[0].mcc[i], rrc->configuration[0].mnc[i]);
      return -1;
    }
  }

  itti_send_msg_to_task(TASK_CUCP_E1, instance, msg_p);

  return 0;
}

void prepare_and_send_ue_context_modification_f1(rrc_gNB_ue_context_t *ue_context_p, e1ap_bearer_setup_resp_t *e1ap_resp)
{
  /* Generate a UE context modification request message towards the DU to
   * instruct the DU for SRB2 and DRB configuration and get the updates on
   * master cell group config from the DU*/

  gNB_RRC_INST *rrc = RC.nrrrc[0];
  gNB_RRC_UE_t *UE = &ue_context_p->ue_context;

  /* Instruction towards the DU for DRB configuration and tunnel creation */
  int nb_drb = e1ap_resp->pduSession[0].numDRBSetup;
  f1ap_drb_to_be_setup_t drbs[nb_drb];
  for (int i = 0; i < nb_drb; i++) {
    drbs[i].drb_id = e1ap_resp->pduSession[0].DRBnGRanList[i].id;
    drbs[i].rlc_mode = rrc->um_on_default_drb ? RLC_MODE_UM : RLC_MODE_AM;
    drbs[i].up_ul_tnl[0].tl_address = e1ap_resp->pduSession[0].DRBnGRanList[i].UpParamList[0].tlAddress;
    drbs[i].up_ul_tnl[0].port = rrc->eth_params_s.my_portd;
    drbs[i].up_ul_tnl[0].teid = e1ap_resp->pduSession[0].DRBnGRanList[i].UpParamList[0].teId;
    drbs[i].up_ul_tnl_length = 1;
  }

  /* Instruction towards the DU for SRB2 configuration */
  int nb_srb = 0;
  f1ap_srb_to_be_setup_t srbs[1];
  if (UE->Srb[2].Active == 0) {
    UE->Srb[2].Active = 1;
    nb_srb = 1;
    srbs[0].srb_id = 2;
    srbs[0].lcid = 2;
  }

  cu_to_du_rrc_information_t cu2du = {0};
  cu_to_du_rrc_information_t *cu2du_p = NULL;
  if (UE->ue_cap_buffer.len > 0 && UE->ue_cap_buffer.buf != NULL) {
    cu2du_p = &cu2du;
    cu2du.uE_CapabilityRAT_ContainerList = UE->ue_cap_buffer.buf;
    cu2du.uE_CapabilityRAT_ContainerList_length = UE->ue_cap_buffer.len;
  }
  f1_ue_data_t ue_data = cu_get_f1_ue_data(UE->rnti);
  f1ap_ue_context_modif_req_t ue_context_modif_req = {
      .gNB_CU_ue_id = UE->rrc_ue_id,
      .gNB_DU_ue_id = ue_data.secondary_ue,
      .plmn.mcc = rrc->configuration[0].mcc[0],
      .plmn.mnc = rrc->configuration[0].mnc[0],
      .plmn.mnc_digit_length = rrc->configuration[0].mnc_digit_length[0],
      .nr_cellid = rrc->nr_cellid,
      .servCellId = 0, /* TODO: correct value? */
      .srbs_to_be_setup_length = nb_srb,
      .srbs_to_be_setup = srbs,
      .drbs_to_be_setup_length = nb_drb,
      .drbs_to_be_setup = drbs,
      .cu_to_du_rrc_information = cu2du_p,
  };
  rrc->mac_rrc.ue_context_modification_request(&ue_context_modif_req);
}

void rrc_gNB_process_e1_bearer_context_setup_resp(e1ap_bearer_setup_resp_t *resp, instance_t instance) {
  // Find the UE context from UE ID and send ITTI message to F1AP to send UE context modification message to DU

  rrc_gNB_ue_context_t *ue_context_p = rrc_gNB_get_ue_context(RC.nrrrc[instance], resp->gNB_cu_cp_ue_id);
  AssertFatal(ue_context_p != NULL, "did not find UE with CU UE ID %d\n", resp->gNB_cu_cp_ue_id);
  protocol_ctxt_t ctxt = {0};
  PROTOCOL_CTXT_SET_BY_MODULE_ID(&ctxt, 0, GNB_FLAG_YES, resp->gNB_cu_cp_ue_id, 0, 0, 0);

  // currently: we don't have "infrastructure" to save the CU-UP UE ID, so we
  // assume (and below check) that CU-UP UE ID == CU-CP UE ID
  AssertFatal(resp->gNB_cu_cp_ue_id == resp->gNB_cu_up_ue_id,
              "cannot handle CU-UP UE ID different from CU-CP UE ID (%d vs %d)\n",
              resp->gNB_cu_cp_ue_id,
              resp->gNB_cu_up_ue_id);

  gtpv1u_gnb_create_tunnel_resp_t create_tunnel_resp={0};
  create_tunnel_resp.num_tunnels = resp->numPDUSessions;
  for (int i=0; i < resp->numPDUSessions; i++) {
    create_tunnel_resp.pdusession_id[i]  = resp->pduSession[i].id;
    create_tunnel_resp.gnb_NGu_teid[i] = resp->pduSession[i].teId;
    memcpy(create_tunnel_resp.gnb_addr.buffer,
           &resp->pduSession[i].tlAddress,
           sizeof(in_addr_t));
    create_tunnel_resp.gnb_addr.length = sizeof(in_addr_t); // IPv4 byte length
  }

  nr_rrc_gNB_process_GTPV1U_CREATE_TUNNEL_RESP(&ctxt, &create_tunnel_resp, 0);

  // TODO: SV: combine e1ap_bearer_setup_req_t and e1ap_bearer_setup_resp_t and minimize assignments
  prepare_and_send_ue_context_modification_f1(ue_context_p, resp);
}

static void rrc_CU_process_f1_lost_connection(gNB_RRC_INST *rrc, f1ap_lost_connection_t *lc, sctp_assoc_t assoc_id)
{
  AssertFatal(rrc->du != NULL, "no DU connected, cannot received F1 lost connection\n");
  AssertFatal(rrc->du->assoc_id == assoc_id,
              "previously connected DU (%d) does not match DU for which connection has been lost (%d)\n",
              rrc->du->assoc_id,
              assoc_id);
  (void) lc; // unused for the moment
  ASN_STRUCT_FREE(asn_DEF_NR_MIB, rrc->du->mib);
  ASN_STRUCT_FREE(asn_DEF_NR_SIB1, rrc->du->sib1);
  free(rrc->du);
  rrc->du = NULL;
  LOG_I(RRC, "dropping DU with assoc_id %d (UE connections remain, if any)\n", assoc_id);
}

static void print_rrc_meas(FILE *f, const NR_MeasResults_t *measresults)
{
  DevAssert(measresults->measResultServingMOList.list.count >= 1);
  if (measresults->measResultServingMOList.list.count > 1)
    LOG_W(RRC, "Received %d MeasResultServMO, but handling only 1!\n", measresults->measResultServingMOList.list.count);

  NR_MeasResultServMO_t *measresultservmo = measresults->measResultServingMOList.list.array[0];
  NR_MeasResultNR_t *measresultnr = &measresultservmo->measResultServingCell;
  NR_MeasQuantityResults_t *mqr = measresultnr->measResult.cellResults.resultsSSB_Cell;

  fprintf(f, "    servingCellId %ld MeasResultNR for phyCellId %ld:\n      resultSSB:", measresultservmo->servCellId, *measresultnr->physCellId);
  if (mqr != NULL) {
    const long rrsrp = *mqr->rsrp - 156;
    const float rrsrq = (float) (*mqr->rsrq - 87) / 2.0f;
    const float rsinr = (float) (*mqr->sinr - 46) / 2.0f;
    fprintf(f, "RSRP %ld dBm RSRQ %.1f dB SINR %.1f dB\n", rrsrp, rrsrq, rsinr);
  } else {
    fprintf(f, "NOT PROVIDED\n");
  }
}

static const char *get_rrc_connection_status_text(NR_UE_STATE_t state)
{
  switch (state) {
    case NR_RRC_INACTIVE: return "inactive";
    case NR_RRC_IDLE: return "idle";
    case NR_RRC_SI_RECEIVED: return "SI-received";
    case NR_RRC_CONNECTED: return "connected";
    case NR_RRC_RECONFIGURED: return "reconfigured";
    case NR_RRC_HO_EXECUTION: return "HO-execution";
    default: AssertFatal(false, "illegal RRC state %d\n", state); return "illegal";
  }
  return "illegal";
}

static const char *get_pdusession_status_text(pdu_session_status_t status)
{
  switch (status) {
    case PDU_SESSION_STATUS_NEW: return "new";
    case PDU_SESSION_STATUS_DONE: return "done";
    case PDU_SESSION_STATUS_ESTABLISHED: return "established";
    case PDU_SESSION_STATUS_REESTABLISHED: return "reestablished";
    case PDU_SESSION_STATUS_TOMODIFY: return "to-modify";
    case PDU_SESSION_STATUS_FAILED: return "failed";
    case PDU_SESSION_STATUS_TORELEASE: return "to-release";
    case PDU_SESSION_STATUS_RELEASED: return "released";
    default: AssertFatal(false, "illegal PDU status code %d\n", status); return "illegal";
  }
  return "illegal";
}

static void write_rrc_stats(const gNB_RRC_INST *rrc)
{
  const char *filename = "nrRRC_stats.log";
  FILE *f = fopen(filename, "w");
  if (f == NULL) {
    LOG_E(NR_RRC, "cannot open %s for writing\n", filename);
    return;
  }

  int i = 0;
  rrc_gNB_ue_context_t *ue_context_p = NULL;
  /* cast is necessary to eliminate warning "discards ‘const’ qualifier" */
  RB_FOREACH(ue_context_p, rrc_nr_ue_tree_s, &((gNB_RRC_INST *)rrc)->rrc_ue_head)
  {
    const gNB_RRC_UE_t *ue_ctxt = &ue_context_p->ue_context;
    f1_ue_data_t ue_data = cu_get_f1_ue_data(ue_ctxt->rnti);  //TODO for later, one day, we will replace rnti with ue_id as OAI
    /* currently, we support only one DU. If we support multiple, need to
     * search for the DU corresponding to this UE here */
    const nr_rrc_du_container_t *du = rrc->du;
    DevAssert(du != NULL);

    // f1_ue_data_t ue_data = cu_get_f1_ue_data(ue_ctxt->rnti);//TODO W28
    // const int CC_id = ue_ctxt->primaryCC_id;
    fprintf(f,
            "UE %d CU UE ID %d DU UE ID %d RNTI %04x random identity %016lx",
            i,
            ue_ctxt->rrc_ue_id,
            ue_data.secondary_ue,
            ue_ctxt->rnti,
            ue_ctxt->random_ue_identity);
    if (ue_ctxt->Initialue_identity_5g_s_TMSI.presence)
      fprintf(f, " S-TMSI %x", ue_ctxt->Initialue_identity_5g_s_TMSI.fiveg_tmsi);
    fprintf(f, ":\n");

    fprintf(f, "    RRC status %s\n", get_rrc_connection_status_text(ue_ctxt->StatusRrc));

    if (ue_ctxt->nb_of_pdusessions == 0)
      fprintf(f, "    (no PDU sessions)\n");
    for (int nb_pdu = 0; nb_pdu < ue_ctxt->nb_of_pdusessions; ++nb_pdu) {
      const rrc_pdu_session_param_t *pdu = &ue_ctxt->pduSession[nb_pdu];
      fprintf(f, "    PDU session %d ID %d status %s\n", nb_pdu, pdu->param.pdusession_id, get_pdusession_status_text(pdu->status));
    }

    if (ue_ctxt->UE_Capability_nr) {
      AssertFatal(du->setup_req->num_cells_available == 1, "only one cell supported at the moment\n");
      const f1ap_served_cell_info_t *cell_info = &du->setup_req->cell[0].info;
      fprintf(f,
              "    UE cap: BW DL %x. BW UL %x, DL MIMO Layers %d UL MIMO Layers (CB) %d UL MIMO Layers (nonCB) %d\n",
              get_dl_bw_mask(cell_info, ue_ctxt->UE_Capability_nr),
              get_ul_bw_mask(cell_info, ue_ctxt->UE_Capability_nr),
              get_dl_mimo_layers(cell_info, ue_ctxt->UE_Capability_nr),
              get_ul_mimo_layersCB(cell_info, ue_ctxt->UE_Capability_nr),
              get_ul_mimo_layers(cell_info, ue_ctxt->UE_Capability_nr));
              // get_dl_bw_mask(rrc, ue_ctxt->UE_Capability_nr, CC_id), //TODO W38
              // get_ul_bw_mask(rrc, ue_ctxt->UE_Capability_nr, CC_id),
              // get_dl_mimo_layers(rrc, ue_ctxt->UE_Capability_nr, CC_id),
              // get_ul_mimo_layersCB(rrc, ue_ctxt->UE_Capability_nr, CC_id),
              // get_ul_mimo_layers(rrc, ue_ctxt->UE_Capability_nr, CC_id));
    }

    if (ue_ctxt->measResults)
      print_rrc_meas(f, ue_ctxt->measResults);
    ++i;
  }

  fclose(f);
}

///---------------------------------------------------------------------------------------------------------------///
///---------------------------------------------------------------------------------------------------------------///
void *rrc_gnb_task(void *args_p) {
  MessageDef *msg_p;
  instance_t                         instance;
  int                                result;
  protocol_ctxt_t ctxt = {.module_id = 0, .enb_flag = 1, .instance = 0, .rntiMaybeUEid = 0, .frame = -1, .subframe = -1, .eNB_index = 0, .brOption = false};

  long stats_timer_id = 1;
  if (!IS_SOFTMODEM_NOSTATS_BIT) {
    /* timer to write stats to file */
    timer_setup(1, 0, TASK_RRC_GNB, 0, TIMER_PERIODIC, NULL, &stats_timer_id);
  }
  
  itti_mark_task_ready(TASK_RRC_GNB);
  LOG_I(NR_RRC,"Entering main loop of NR_RRC message task\n");

  while (1) {
    // Wait for a message
    itti_receive_msg(TASK_RRC_GNB, &msg_p);
    const char *msg_name_p = ITTI_MSG_NAME(msg_p);
    instance = ITTI_MSG_DESTINATION_INSTANCE(msg_p);

    LOG_D(NR_RRC, "Received Msg %s\n", msg_name_p);
    switch (ITTI_MSG_ID(msg_p)) {
      case TERMINATE_MESSAGE:
        LOG_W(NR_RRC, " *** Exiting NR_RRC thread\n");
        itti_exit_task();
        break;

      case MESSAGE_TEST:
        LOG_I(NR_RRC, "[gNB %ld] Received %s\n", instance, msg_name_p);
        break;

      case TIMER_HAS_EXPIRED:
        /* only this one handled for now */
        DevAssert(TIMER_HAS_EXPIRED(msg_p).timer_id == stats_timer_id);
        write_rrc_stats(RC.nrrrc[0]);
        break;

      case RRC_SUBFRAME_PROCESS:
        nr_rrc_ss_subframe_process(&RRC_SUBFRAME_PROCESS(msg_p).ctxt);
        break;

      case F1AP_INITIAL_UL_RRC_MESSAGE:
        AssertFatal(NODE_IS_CU(RC.nrrrc[instance]->node_type) || NODE_IS_MONOLITHIC(RC.nrrrc[instance]->node_type),
                    "should not receive F1AP_INITIAL_UL_RRC_MESSAGE, need call by CU!\n");
        rrc_gNB_process_initial_ul_rrc_message(&F1AP_INITIAL_UL_RRC_MESSAGE(msg_p));
        break;

      /* Messages from PDCP */
      case F1AP_UL_RRC_MESSAGE:
        PROTOCOL_CTXT_SET_BY_INSTANCE(&ctxt,
                                      instance,
                                      GNB_FLAG_YES,
                                      F1AP_UL_RRC_MESSAGE(msg_p).gNB_CU_ue_id,
                                      0,
                                      0);
        LOG_D(NR_RRC,
              "Decoding DCCH %d: ue %04lx, inst %ld, ctxt %p, size %d\n",
              F1AP_UL_RRC_MESSAGE(msg_p).srb_id,
              ctxt.rntiMaybeUEid,
              instance,
              &ctxt,
              F1AP_UL_RRC_MESSAGE(msg_p).rrc_container_length);
        rrc_gNB_decode_dcch(&ctxt,
                            F1AP_UL_RRC_MESSAGE(msg_p).srb_id,
                            F1AP_UL_RRC_MESSAGE(msg_p).rrc_container,
                            F1AP_UL_RRC_MESSAGE(msg_p).rrc_container_length);
        free(F1AP_UL_RRC_MESSAGE(msg_p).rrc_container);
        break;

      case NGAP_DOWNLINK_NAS:
        rrc_gNB_process_NGAP_DOWNLINK_NAS(msg_p, instance, &rrc_gNB_mui);
        break;

      case NGAP_PDUSESSION_SETUP_REQ:
        rrc_gNB_process_NGAP_PDUSESSION_SETUP_REQ(msg_p, instance);
        break;

      case NGAP_PDUSESSION_MODIFY_REQ:
        rrc_gNB_process_NGAP_PDUSESSION_MODIFY_REQ(msg_p, instance);
        break;

      case NGAP_PDUSESSION_RELEASE_COMMAND:
        rrc_gNB_process_NGAP_PDUSESSION_RELEASE_COMMAND(msg_p, instance);
        break;

      /* Messages from gNB app */ //TODO W38
      case NRRRC_CONFIGURATION_REQ:
        LOG_I(NR_RRC, "[gNB %ld] Received %s : %p\n", instance, msg_name_p,&NRRRC_CONFIGURATION_REQ(msg_p));
        gNB_RRC_INST         *rrc=RC.nrrrc[instance];
        openair_rrc_gNB_configuration(rrc, &NRRRC_CONFIGURATION_REQ(msg_p));
        break;

      case NRRRC_RBLIST_CFG_REQ: //TODO W38
        LOG_I(NR_RRC, "[gNB %ld] Received %s : %p, RB Count:%d cc_id %d\n", instance, msg_name_p, &NRRRC_RBLIST_CFG_REQ(msg_p),NRRRC_RBLIST_CFG_REQ(msg_p).rb_count,NRRRC_RBLIST_CFG_REQ(msg_p).cell_index);
        rrc_gNB_rblist_configuration(instance, &NRRRC_RBLIST_CFG_REQ(msg_p));
        break;

      /* Messages from F1AP task */
      case F1AP_SETUP_REQ:
        AssertFatal(!NODE_IS_DU(RC.nrrrc[instance]->node_type), "should not receive F1AP_SETUP_REQUEST in DU!\n");
        rrc_gNB_process_f1_setup_req(&F1AP_SETUP_REQ(msg_p), msg_p->ittiMsgHeader.originInstance);
        break;

      case F1AP_UE_CONTEXT_SETUP_RESP:
        rrc_CU_process_ue_context_setup_response(msg_p, instance);
        break;

      case F1AP_UE_CONTEXT_MODIFICATION_RESP:
        rrc_CU_process_ue_context_modification_response(msg_p, instance);
        break;

      case F1AP_UE_CONTEXT_MODIFICATION_REQUIRED:
        rrc_CU_process_ue_modification_required(msg_p);
        break;

      case F1AP_UE_CONTEXT_RELEASE_REQ:
        rrc_CU_process_ue_context_release_request(msg_p);
        break;

      case F1AP_UE_CONTEXT_RELEASE_COMPLETE:
        rrc_CU_process_ue_context_release_complete(msg_p);
        break;

      case F1AP_LOST_CONNECTION:
        rrc_CU_process_f1_lost_connection(RC.nrrrc[0], &F1AP_LOST_CONNECTION(msg_p), msg_p->ittiMsgHeader.originInstance);
        break;

      /* Messages from X2AP */
      case X2AP_ENDC_SGNB_ADDITION_REQ:
        LOG_I(NR_RRC, "Received ENDC sgNB addition request from X2AP \n");
        rrc_gNB_process_AdditionRequestInformation(instance, &X2AP_ENDC_SGNB_ADDITION_REQ(msg_p));
        break;

      case X2AP_ENDC_SGNB_RECONF_COMPLETE:
        LOG_A(NR_RRC, "Handling of reconfiguration complete message at RRC gNB is pending \n");
        break;

      case NGAP_INITIAL_CONTEXT_SETUP_REQ:
        rrc_gNB_process_NGAP_INITIAL_CONTEXT_SETUP_REQ(msg_p, instance);
        break;

      case X2AP_ENDC_SGNB_RELEASE_REQUEST:
        LOG_I(NR_RRC, "Received ENDC sgNB release request from X2AP \n");
        rrc_gNB_process_release_request(instance, &X2AP_ENDC_SGNB_RELEASE_REQUEST(msg_p));
        break;

      case X2AP_ENDC_DC_OVERALL_TIMEOUT:
        rrc_gNB_process_dc_overall_timeout(instance, &X2AP_ENDC_DC_OVERALL_TIMEOUT(msg_p));
        break;

      case NGAP_UE_CONTEXT_RELEASE_REQ:
        rrc_gNB_process_NGAP_UE_CONTEXT_RELEASE_REQ(msg_p, instance);
        break;

      case NGAP_UE_CONTEXT_RELEASE_COMMAND:
        rrc_gNB_process_NGAP_UE_CONTEXT_RELEASE_COMMAND(msg_p, instance);
        break;

      case E1AP_SETUP_REQ:
        rrc_gNB_process_e1_setup_req(&E1AP_SETUP_REQ(msg_p), instance);
        break;

      case E1AP_BEARER_CONTEXT_SETUP_RESP:
        rrc_gNB_process_e1_bearer_context_setup_resp(&E1AP_BEARER_CONTEXT_SETUP_RESP(msg_p), instance);

      case NGAP_PAGING_IND:
        rrc_gNB_process_PAGING_IND(msg_p, instance);
        break;

      case SS_SS_NR_PAGING_IND:
        LOG_A(NR_RRC, "Received Paging message from SS: %s\n", msg_name_p);
        rrc_gNB_process_SS_PAGING_IND(msg_p, msg_name_p, instance);
        break;

      case RRC_PCCH_DATA_REQ:
        LOG_A(NR_RRC, "Received RRC_PCCH_DATA_REQ CC_id %d length %d \n", RRC_PCCH_DATA_REQ(msg_p).CC_id, RRC_PCCH_DATA_REQ(msg_p).sdu_size);
        int CC_id = RRC_PCCH_DATA_REQ(msg_p).CC_id;
        RC.nrrrc[instance]->carrier[CC_id].sizeof_paging = RRC_PCCH_DATA_REQ(msg_p).sdu_size;
        memcpy(RC.nrrrc[instance]->carrier[CC_id].paging, RRC_PCCH_DATA_REQ(msg_p).sdu_p, RRC_PCCH_DATA_REQ(msg_p).sdu_size);
        result = itti_free(ITTI_MSG_ORIGIN_ID(msg_p), RRC_PCCH_DATA_REQ(msg_p).sdu_p);
        AssertFatal(result == EXIT_SUCCESS, "Failed to free memory (%d)!\n", result);
        break;

      case SS_NRRRC_PDU_REQ:
        LOG_A(NR_RRC,"NR RRC received SS_NRRRC_PDU_REQ SRB_ID:%d SDU_SIZE:%d\n", SS_NRRRC_PDU_REQ (msg_p).srb_id, SS_NRRRC_PDU_REQ (msg_p).sdu_size);

        PROTOCOL_CTXT_SET_BY_INSTANCE(&ctxt,
                                      instance,
                                      GNB_FLAG_YES,
                                      SS_NRRRC_PDU_REQ(msg_p).rnti,
                                      msg_p->ittiMsgHeader.lte_time.frame,
                                      msg_p->ittiMsgHeader.lte_time.slot);
        if ((SS_NRRRC_PDU_REQ(msg_p).srb_id) != 0)
        {
          NR_DL_DCCH_Message_t *dl_dcch_msg = NULL;
          LOG_A(NR_RRC, "Sending NR RRC PDU by nr_rrc_data_req function \n");

          uper_decode(NULL,
                      &asn_DEF_NR_DL_DCCH_Message,
                      (void **)&dl_dcch_msg,
                      (uint8_t *)SS_NRRRC_PDU_REQ(msg_p).sdu,
                      SS_NRRRC_PDU_REQ(msg_p).sdu_size, 0, 0);

          LOG_A(NR_RRC, "DL DCCH size: %d \n", SS_NRRRC_PDU_REQ(msg_p).sdu_size);

          if (LOG_DEBUGFLAG(DEBUG_ASN1))
          {
          }
            xer_fprint(stdout, &asn_DEF_NR_DL_DCCH_Message, (void *)dl_dcch_msg);


          if(dl_dcch_msg->message.choice.c1) {
            if (dl_dcch_msg->message.choice.c1->present == NR_DL_DCCH_MessageType__c1_PR_rrcReconfiguration)
            {
              struct rrc_gNB_ue_context_s * ue_context_p = rrc_gNB_get_ue_context_by_rnti(RC.nrrrc[ctxt.module_id], ctxt.rntiMaybeUEid);
              LOG_A(NR_RRC, "[GNB %ld] SRB %d rnti %d rrcReconfiguration\n", instance, SS_NRRRC_PDU_REQ(msg_p).srb_id, SS_NRRRC_PDU_REQ(msg_p).rnti);
              if(ue_context_p){
                NR_RRCReconfiguration_t * rrcReconfiguration = dl_dcch_msg->message.choice.c1->choice.rrcReconfiguration;

                /*---------------------------------------------------------------------------*/
                /* The cellGroupConfig inside rrcReconfiguration from TTCN doesn't have rlc_config for some srb, upate it from bearList config */
                if(rrcReconfiguration->criticalExtensions.present == NR_RRCReconfiguration__criticalExtensions_PR_rrcReconfiguration && rrcReconfiguration->criticalExtensions.choice.rrcReconfiguration){
                  NR_RRCReconfiguration_IEs_t * ie = rrcReconfiguration->criticalExtensions.choice.rrcReconfiguration;
                  if(ie->nonCriticalExtension && ie->nonCriticalExtension->masterCellGroup){
                    NR_CellGroupConfig_t * cellGroupConfig = NULL;
                    uper_decode(NULL,
                                        &asn_DEF_NR_CellGroupConfig,
                                        (void **)&cellGroupConfig,
                                        (uint8_t *)ie->nonCriticalExtension->masterCellGroup->buf,
                                        ie->nonCriticalExtension->masterCellGroup->size, 0, 0);

                    if (LOG_DEBUGFLAG(DEBUG_ASN1) ) {
                      xer_fprint(stdout, &asn_DEF_NR_CellGroupConfig, (const void *)cellGroupConfig);
                    }

                    if(update_rrcReconfig_cellGroupConfig(&ctxt,ue_context_p,cellGroupConfig)){
                      //we need to update the SDU as cellGroupConfig updated
                      LOG_A(NR_RRC, "[GNB] update rrcReconfiguration DL_DCCH_Message message\n");
                      uint8_t *buff = calloc(128,sizeof(uint8_t));
                      asn_enc_rval_t  enc_rval = uper_encode_to_buffer(&asn_DEF_NR_CellGroupConfig,
                                                                                            NULL,
                                                                                            (void *)cellGroupConfig,
                                                                                            buff,
                                                                                            128);
                      ASN_STRUCT_FREE(asn_DEF_NR_CellGroupConfig,cellGroupConfig);
                      if(enc_rval.encoded == -1) {
                        LOG_E(NR_RRC, "ASN1 message CellGroupConfig encoding failed (%s)!\n",
                        enc_rval.failed_type->name);
                      }
                      AssertFatal(enc_rval.encoded != -1, "[gNB AssertFatal]ASN1 message encoding CellGroupConfig failed (%s)!\n",enc_rval.failed_type->name);

                      if (OCTET_STRING_fromBuf(ie->nonCriticalExtension->masterCellGroup, (const char*) buff, (enc_rval.encoded+7)/8) == -1) {
                        LOG_E(NR_RRC, "fatal: OCTET_STRING_fromBuf failed\n");
                        AssertFatal(0, "OCTET_STRING_fromBuf failed\n");
                      }
                      free(buff);

                      if (LOG_DEBUGFLAG(DEBUG_ASN1))
                      {
                        xer_fprint(stdout, &asn_DEF_NR_DL_DCCH_Message, (void *)dl_dcch_msg);
                      }

                      enc_rval = uper_encode_to_buffer(&asn_DEF_NR_DL_DCCH_Message,
                                      NULL,
                                      (void *)dl_dcch_msg,
                                      SS_NRRRC_PDU_REQ (msg_p).sdu,
                                      SDU_SIZE);
                      if(enc_rval.encoded == -1) {
                        LOG_E(NR_RRC, "[gNB AssertFatal]ASN1 message DL_DCCH_Message encoding failed (%s)!\n",enc_rval.failed_type->name);
                      }
                      AssertFatal(enc_rval.encoded != -1, "[gNB AssertFatal]ASN1 message encoding DL_DCCH_Message failed (%s)!\n",enc_rval.failed_type->name);

                      SS_NRRRC_PDU_REQ (msg_p).sdu_size = ((enc_rval.encoded+7)/8);
                      LOG_A(NR_RRC, "DL DCCH updated size: %d \n", SS_NRRRC_PDU_REQ(msg_p).sdu_size);
                    }
                  }
                }
                /*---------------------------------------------------------------------------*/
                rrc_gNB_store_RRCReconfiguration(&ctxt,ue_context_p,rrcReconfiguration);
              }
            }
            else if(dl_dcch_msg->message.choice.c1->present == NR_DL_DCCH_MessageType__c1_PR_securityModeCommand)
            {
              NR_SecurityModeCommand_t * securityModeCommand = dl_dcch_msg->message.choice.c1->choice.securityModeCommand;
              if(securityModeCommand->criticalExtensions.present == NR_SecurityModeCommand__criticalExtensions_PR_securityModeCommand){
                NR_SecurityModeCommand_IEs_t * ie = securityModeCommand->criticalExtensions.choice.securityModeCommand;
                if(ie){
                  NR_CipheringAlgorithm_t cipher_algo = ie->securityConfigSMC.securityAlgorithmConfig.cipheringAlgorithm;
                  NR_IntegrityProtAlgorithm_t integrity_algo = ie->securityConfigSMC.securityAlgorithmConfig.integrityProtAlgorithm? *(ie->securityConfigSMC.securityAlgorithmConfig.integrityProtAlgorithm): 0;
                  if(cipher_algo == _cip_algo && integrity_algo == _int_algo){
                    /* Set security to PDCP for the created SRB1 */
                    nr_pdcp_config_set_security(ctxt.rntiMaybeUEid, DCCH, _cip_algo | _int_algo << 4,
                                                              _nr_control_plane_cip_key,
                                                              _nr_control_plane_int_key,
                                                              NULL);
                  }else {
                    LOG_E(NR_RRC, "SS configure secuirty not matched with SecurityModeCommand\n");
                  }
                }
              }
            }
            else if(dl_dcch_msg->message.choice.c1->present == NR_DL_DCCH_MessageType__c1_PR_rrcRelease)
            {
              rrc_gNB_ue_context_t *ue_context_p = rrc_gNB_get_ue_context_by_rnti(RC.nrrrc[ctxt.module_id], ctxt.rntiMaybeUEid);
              if (ue_context_p) {
                LOG_I(NR_RRC, "rrcRelease UE rnti: %lx \n", ctxt.rntiMaybeUEid);
                ue_context_p->ue_context.ue_release_timer_rrc = 1;
                ue_context_p->ue_context.ue_release_timer_thres_rrc = 10; /* give enough time for low layer to transmit the rrcRelease message */
                ue_context_p->ue_context.ue_rrc_inactivity_timer = 0;
              }
              else
              {
                LOG_W(NR_RRC, "ue_context_p is already NULL\n");
              }
            }
          }

          ASN_STRUCT_FREE(asn_DEF_NR_DL_DCCH_Message,dl_dcch_msg);

          nr_pdcp_data_req_srb(ctxt.rntiMaybeUEid,
            SS_NRRRC_PDU_REQ (msg_p).srb_id,
            rrc_gNB_mui++,
            SS_NRRRC_PDU_REQ (msg_p).sdu_size,
            SS_NRRRC_PDU_REQ (msg_p).sdu,
            rrc_deliver_dl_rrc_message,
            RC.nrrrc[ctxt.module_id]);
        }
        break;

      case SS_DRB_PDU_REQ:
        {
          LOG_A(NR_RRC, "Received SS_DRB_PDU_REQ DRB_ID:%d SDU_SIZE:%d\n", SS_DRB_PDU_REQ(msg_p).drb_id, SS_DRB_PDU_REQ(msg_p).sdu_size);

          PROTOCOL_CTXT_SET_BY_INSTANCE(&ctxt,
                                        instance,
                                        GNB_FLAG_YES,
                                        SS_DRB_PDU_REQ(msg_p).rnti,
                                        msg_p->ittiMsgHeader.lte_time.frame,
                                        msg_p->ittiMsgHeader.lte_time.slot);

          if (SS_DRB_PDU_REQ(msg_p).data_type == DRB_RlcPdu || SS_DRB_PDU_REQ(msg_p).data_type == DRB_RlcSdu) {
            mem_block_t *sdu = get_free_mem_block(SS_DRB_PDU_REQ(msg_p).sdu_size, __func__);
            memcpy(sdu->data, SS_DRB_PDU_REQ(msg_p).sdu, SS_DRB_PDU_REQ(msg_p).sdu_size);
            enqueue_mac_rlc_data_req(&ctxt,
                                     SRB_FLAG_NO,
                                     MBMS_FLAG_NO,
                                     SS_DRB_PDU_REQ(msg_p).drb_id,
                                     0,
                                     0,
                                     SS_DRB_PDU_REQ(msg_p).sdu_size,
                                     sdu,
                                     NULL,
                                     NULL);
          } else if (SS_DRB_PDU_REQ(msg_p).data_type == DRB_PdcpSdu) {
            nr_pdcp_data_req_drb(&ctxt,
                                 SRB_FLAG_NO,
                                 SS_DRB_PDU_REQ(msg_p).drb_id,
                                 RLC_MUI_UNDEFINED,
                                 SDU_CONFIRM_NO,
                                 SS_DRB_PDU_REQ(msg_p).sdu_size,
                                 SS_DRB_PDU_REQ(msg_p).sdu,
                                 PDCP_TRANSMISSION_MODE_UNKNOWN,
                                 NULL,
                                 NULL);
          } else if (SS_DRB_PDU_REQ(msg_p).data_type == DRB_SdapSdu){
            sdap_data_req(&ctxt,
                        SS_DRB_PDU_REQ(msg_p).rnti,
                        SRB_FLAG_NO,
                        SS_DRB_PDU_REQ(msg_p).drb_id,
                        RLC_MUI_UNDEFINED,
                        RLC_SDU_CONFIRM_NO,
                        SS_DRB_PDU_REQ(msg_p).sdu_size,
                        SS_DRB_PDU_REQ(msg_p).sdu,
                        PDCP_TRANSMISSION_MODE_DATA, NULL, NULL,
                        SS_DRB_PDU_REQ(msg_p).qfi,
                        0,
                        SS_DRB_PDU_REQ(msg_p).pdu_sessionId);
          } else {
            AssertFatal(RC.nr_drb_data_type != RC.nr_drb_data_type, "Invalid DRB data type (%d)!\n", RC.nr_drb_data_type);
          }
        }
        break;

      case RRC_AS_SECURITY_CONFIG_REQ:
      {
        _int_algo = (e_NR_IntegrityProtAlgorithm)RRC_AS_SECURITY_CONFIG_REQ(msg_p).Integrity.integrity_algorithm;
        _cip_algo = (NR_CipheringAlgorithm_t)RRC_AS_SECURITY_CONFIG_REQ(msg_p).Ciphering.ciphering_algorithm;

        LOG_I(NR_RRC,"[gNB %ld] Received %s: Integrity algo: %d, Ciphering algo: %ld \n", instance, msg_name_p, _int_algo, _cip_algo);

        PROTOCOL_CTXT_SET_BY_INSTANCE(&ctxt, instance, GNB_FLAG_YES, RRC_AS_SECURITY_CONFIG_REQ(msg_p).rnti,
          msg_p->ittiMsgHeader.lte_time.frame, msg_p->ittiMsgHeader.lte_time.slot);

        if (_int_algo > 0) {
          memcpy(&(_nr_control_plane_int_key[0]), &(RRC_AS_SECURITY_CONFIG_REQ(msg_p).Integrity.kRRCint[0]), 16);
          memcpy(&(_nr_data_plane_int_key[0]), &(RRC_AS_SECURITY_CONFIG_REQ(msg_p).Integrity.kUPint[0]), 16);
        } else {
          memset(&(_nr_control_plane_int_key[0]), 0, 16);
          memset(&(_nr_data_plane_int_key[0]), 0, 16);
        }

        if (_cip_algo > 0) {
          memcpy(&(_nr_control_plane_cip_key[0]), &(RRC_AS_SECURITY_CONFIG_REQ(msg_p).Ciphering.kRRCenc[0]), 16);
          memcpy(&(_nr_data_plane_cip_key[0]), &(RRC_AS_SECURITY_CONFIG_REQ(msg_p).Ciphering.kUPenc[0]), 16);
        } else {
          memset(&(_nr_control_plane_cip_key[0]), 0, 16);
          memset(&(_nr_data_plane_cip_key[0]), 0, 16);
        }
      }
      break;

      default:
        LOG_E(NR_RRC, "[gNB %ld] Received unexpected message %s\n", instance, msg_name_p);
        break;
    }

    result = itti_free(ITTI_MSG_ORIGIN_ID(msg_p), msg_p);
    AssertFatal(result == EXIT_SUCCESS, "Failed to free memory (%d)!\n", result);
    msg_p = NULL;
  }
}

typedef struct deliver_ue_ctxt_setup_data_t {
  gNB_RRC_INST *rrc;
  f1ap_ue_context_setup_t *setup_req;
} deliver_ue_ctxt_setup_data_t;
static void rrc_deliver_ue_ctxt_setup_req(void *deliver_pdu_data, ue_id_t ue_id, int srb_id, char *buf, int size, int sdu_id)
{
  DevAssert(deliver_pdu_data != NULL);
  deliver_ue_ctxt_setup_data_t *data = deliver_pdu_data;
  data->setup_req->rrc_container = (uint8_t*)buf;
  data->setup_req->rrc_container_length = size;
  data->rrc->mac_rrc.ue_context_setup_request(data->setup_req);
}

//-----------------------------------------------------------------------------
void
rrc_gNB_generate_SecurityModeCommand(
  const protocol_ctxt_t *const ctxt_pP,
  rrc_gNB_ue_context_t  *const ue_context_pP
)
//-----------------------------------------------------------------------------
{
  uint8_t                             buffer[100];
  uint8_t                             size;
  gNB_RRC_UE_t *ue_p = &ue_context_pP->ue_context;

  T(T_ENB_RRC_SECURITY_MODE_COMMAND, T_INT(ctxt_pP->module_id), T_INT(ctxt_pP->frame), T_INT(ctxt_pP->subframe), T_INT(ctxt_pP->rntiMaybeUEid));
  NR_IntegrityProtAlgorithm_t integrity_algorithm = (NR_IntegrityProtAlgorithm_t)ue_p->integrity_algorithm;
  size = do_NR_SecurityModeCommand(ctxt_pP, buffer, rrc_gNB_get_next_transaction_identifier(ctxt_pP->module_id), ue_p->ciphering_algorithm, integrity_algorithm);
  LOG_DUMPMSG(NR_RRC,DEBUG_RRC,(char *)buffer,size,"[MSG] RRC Security Mode Command\n");
  LOG_I(NR_RRC, "UE %04x Logical Channel DL-DCCH, Generate SecurityModeCommand (bytes %d)\n", ue_p->rnti, size);

  gNB_RRC_INST *rrc = RC.nrrrc[ctxt_pP->module_id];
  AssertFatal(!NODE_IS_DU(rrc->node_type), "illegal node type DU!\n");

  cu_to_du_rrc_information_t cu2du = {0};
  cu_to_du_rrc_information_t *cu2du_p = NULL;
  if (ue_p->ue_cap_buffer.len > 0 && ue_p->ue_cap_buffer.buf != NULL) {
    cu2du_p = &cu2du;
    cu2du.uE_CapabilityRAT_ContainerList = ue_p->ue_cap_buffer.buf;
    cu2du.uE_CapabilityRAT_ContainerList_length = ue_p->ue_cap_buffer.len;
  }

  const nr_rrc_du_container_t *du = rrc->du;
  DevAssert(du != NULL);

  /* the callback will fill the UE context setup request and forward it */
  f1_ue_data_t ue_data = cu_get_f1_ue_data(ue_p->rnti);
  f1ap_ue_context_setup_t ue_context_setup_req = {
      .gNB_CU_ue_id = ue_p->rrc_ue_id,
      .gNB_DU_ue_id = ue_data.secondary_ue,
      .plmn.mcc = rrc->configuration[0].mcc[0],
      .plmn.mnc = rrc->configuration[0].mnc[0],
      .plmn.mnc_digit_length = rrc->configuration[0].mnc_digit_length[0],
      .nr_cellid = rrc->nr_cellid,
      .servCellId = 0, /* TODO: correct value? */
      .srbs_to_be_setup = 0, /* no new SRBs */
      .drbs_to_be_setup = 0, /* no new DRBs */
      .cu_to_du_rrc_information = cu2du_p,
  };
  deliver_ue_ctxt_setup_data_t data = {.rrc = rrc, .setup_req = &ue_context_setup_req};
  nr_pdcp_data_req_srb(ctxt_pP->rntiMaybeUEid, DCCH, rrc_gNB_mui++, size, buffer, rrc_deliver_ue_ctxt_setup_req, &data);
}

void
rrc_gNB_generate_UECapabilityEnquiry(
  const protocol_ctxt_t *const ctxt_pP,
  rrc_gNB_ue_context_t          *const ue_context_pP
)
//-----------------------------------------------------------------------------
{
  uint8_t                             buffer[100];
  uint8_t                             size;

  T(T_ENB_RRC_UE_CAPABILITY_ENQUIRY, T_INT(ctxt_pP->module_id), T_INT(ctxt_pP->frame), T_INT(ctxt_pP->subframe), T_INT(ctxt_pP->rntiMaybeUEid));
  size = do_NR_SA_UECapabilityEnquiry(
           ctxt_pP,
           buffer,
           rrc_gNB_get_next_transaction_identifier(ctxt_pP->module_id));
  LOG_I(NR_RRC,
        PROTOCOL_NR_RRC_CTXT_UE_FMT" Logical Channel DL-DCCH, Generate NR UECapabilityEnquiry (bytes %d)\n",
        PROTOCOL_NR_RRC_CTXT_UE_ARGS(ctxt_pP),
        size);

  gNB_RRC_INST *rrc = RC.nrrrc[ctxt_pP->module_id];
  AssertFatal(!NODE_IS_DU(rrc->node_type), "illegal node type DU!\n");

  const gNB_RRC_UE_t *ue = &ue_context_pP->ue_context;
  nr_rrc_transfer_protected_rrc_message(rrc, ue, DCCH, buffer, size);
}

typedef struct deliver_ue_ctxt_release_data_t {
  gNB_RRC_INST *rrc;
  f1ap_ue_context_release_cmd_t *release_cmd;
} deliver_ue_ctxt_release_data_t;
static void rrc_deliver_ue_ctxt_release_cmd(void *deliver_pdu_data, ue_id_t ue_id, int srb_id, char *buf, int size, int sdu_id)
{
  DevAssert(deliver_pdu_data != NULL);
  deliver_ue_ctxt_release_data_t *data = deliver_pdu_data;
  data->release_cmd->rrc_container = (uint8_t*) buf;
  data->release_cmd->rrc_container_length = size;
  data->rrc->mac_rrc.ue_context_release_command(data->release_cmd);
}

//-----------------------------------------------------------------------------
/*
* Generate the RRC Connection Release to UE.
* If received, UE should switch to RRC_IDLE mode.
*/
void
rrc_gNB_generate_RRCRelease(
  const protocol_ctxt_t *const ctxt_pP,
  rrc_gNB_ue_context_t  *const ue_context_pP
)
//-----------------------------------------------------------------------------
{
  uint8_t buffer[RRC_BUF_SIZE] = {0};
  int size = do_NR_RRCRelease(buffer, RRC_BUF_SIZE, rrc_gNB_get_next_transaction_identifier(ctxt_pP->module_id));

  LOG_I(NR_RRC,
        PROTOCOL_NR_RRC_CTXT_UE_FMT" Logical Channel DL-DCCH, Generate RRCRelease (bytes %d)\n",
        PROTOCOL_NR_RRC_CTXT_UE_ARGS(ctxt_pP),
        size);

  gNB_RRC_INST *rrc = RC.nrrrc[ctxt_pP->module_id];
  const gNB_RRC_UE_t *UE = &ue_context_pP->ue_context;
  f1_ue_data_t ue_data = cu_get_f1_ue_data(UE->rnti);
  f1ap_ue_context_release_cmd_t ue_context_release_cmd = {
    .gNB_CU_ue_id = UE->rrc_ue_id,
    .gNB_DU_ue_id = ue_data.secondary_ue,
    .cause = F1AP_CAUSE_RADIO_NETWORK,
    .cause_value = 10, // 10 = F1AP_CauseRadioNetwork_normal_release
    .srb_id = DCCH,
  };
  deliver_ue_ctxt_release_data_t data = {.rrc = rrc, .release_cmd = &ue_context_release_cmd};
  nr_pdcp_data_req_srb(ctxt_pP->rntiMaybeUEid, DCCH, rrc_gNB_mui++, size, buffer, rrc_deliver_ue_ctxt_release_cmd, &data);

  /* UE will be freed after UE context release complete */
}

void rrc_gNB_trigger_new_bearer(int rnti)
{
  /* get RRC and UE */
  gNB_RRC_INST *rrc = RC.nrrrc[0];
  rrc_gNB_ue_context_t *ue_context_p = rrc_gNB_get_ue_context_by_rnti(rrc, rnti);
  if (ue_context_p == NULL) {
    LOG_E(RRC, "unknown UE RNTI %04x\n", rnti);
    return;
  }
  gNB_RRC_UE_t *ue = &ue_context_p->ue_context;

  /* get the existing PDU sessoin */
  if (ue->nb_of_pdusessions < 1) {
    LOG_E(RRC, "no PDU session set up yet, cannot create additional bearer\n");
    return;
  }

  if (ue->established_drbs[0].status != DRB_INACTIVE
      && ue->established_drbs[1].status != DRB_INACTIVE) {
    LOG_E(RRC, "already have two established bearers, aborting\n");
    return;
  }

  e1ap_bearer_setup_req_t bearer_req = {0};
  bearer_req.gNB_cu_cp_ue_id = ue->rrc_ue_id;
  bearer_req.cipheringAlgorithm = ue->ciphering_algorithm;
  memcpy(bearer_req.encryptionKey, ue->kgnb, sizeof(ue->kgnb));
  bearer_req.integrityProtectionAlgorithm = ue->integrity_algorithm;
  memcpy(bearer_req.integrityProtectionKey, ue->kgnb, sizeof(ue->kgnb));
  bearer_req.ueDlAggMaxBitRate = 10000; /* probably does not matter */

  pdu_session_to_setup_t *pdu = &bearer_req.pduSession[0];
  //bearer_req.numPDUSessions++;
  bearer_req.numPDUSessions = 1;
  //pdu->sessionId = session->pdusession_id;
  //pdu->sst = msg->allowed_nssai[i].sST;
  //pdu->integrityProtectionIndication = rrc->security.do_drb_integrity ? E1AP_IntegrityProtectionIndication_required : E1AP_IntegrityProtectionIndication_not_needed;

  //pdu->confidentialityProtectionIndication = rrc->security.do_drb_ciphering ? E1AP_ConfidentialityProtectionIndication_required : E1AP_ConfidentialityProtectionIndication_not_needed;
  //pdu->teId = session->gtp_teid;
  pdu->numDRB2Setup = 1; // One DRB per PDU Session. TODO: Remove hardcoding
  DRB_nGRAN_to_setup_t *drb = &pdu->DRBnGRanList[0];
  int drb_id = 2;
  drb->id = drb_id;

  drb->defaultDRB = E1AP_DefaultDRB_false;
  drb->sDAP_Header_UL = !(rrc->configuration[ue->primaryCC_id].enable_sdap);
  drb->sDAP_Header_DL = !(rrc->configuration[ue->primaryCC_id].enable_sdap);

  drb->pDCP_SN_Size_UL = E1AP_PDCP_SN_Size_s_18;
  drb->pDCP_SN_Size_DL = E1AP_PDCP_SN_Size_s_18;

  drb->discardTimer = E1AP_DiscardTimer_infinity;
  drb->reorderingTimer = E1AP_T_Reordering_ms0;

  drb->rLC_Mode = E1AP_RLC_Mode_rlc_am;

  drb->numCellGroups = 1; // assume one cell group associated with a DRB

  for (int k=0; k < drb->numCellGroups; k++) {
    cell_group_t *cellGroup = drb->cellGroupList + k;
    cellGroup->id = 0; // MCG
  }

  int xid = rrc_gNB_get_next_transaction_identifier(0);
  /* generate a new bearer, it will be put into internal RRC state and picked
   * up later */
  generateDRB(ue,
              drb_id,
              &ue->pduSession[0],
              rrc->configuration[ue->primaryCC_id].enable_sdap,
              rrc->security.do_drb_integrity,
              rrc->security.do_drb_ciphering);

  /* associate the new bearer to it */
  ue->xids[xid] = RRC_PDUSESSION_MODIFY;
  ue->pduSession[0].xid = xid; // hack: fake xid for ongoing PDU session
  LOG_W(RRC, "trigger new bearer %ld for UE %04x xid %d\n", drb->id, ue->rnti, xid);
  rrc->cucp_cuup.bearer_context_setup(&bearer_req, 0);
}

void rrc_gNB_trigger_release_bearer(int rnti)
{
  /* get RRC and UE */
  gNB_RRC_INST *rrc = RC.nrrrc[0];
  rrc_gNB_ue_context_t *ue_context_p = rrc_gNB_get_ue_context_by_rnti(rrc, rnti);
  if (ue_context_p == NULL) {
    LOG_E(RRC, "unknown UE RNTI %04x\n", rnti);
    return;
  }
  gNB_RRC_UE_t *ue = &ue_context_p->ue_context;

  if (ue->established_drbs[1].status == DRB_INACTIVE) {
    LOG_E(RRC, "no second bearer, aborting\n");
    return;
  }

  // don't use E1: bearer release is not implemented, call directly
  // into PDCP/SDAP and then send corresponding message via F1

  int drb_id = 2;
  ue->established_drbs[1].status = DRB_INACTIVE;
  ue->DRB_ReleaseList = calloc(1, sizeof(*ue->DRB_ReleaseList));
  AssertFatal(ue->DRB_ReleaseList != NULL, "out of memory\n");
  NR_DRB_Identity_t *asn1_drb = malloc(sizeof(*asn1_drb));
  AssertFatal(asn1_drb != NULL, "out of memory\n");
  int idx = 0;
  NR_DRB_ToAddModList_t *drb_list = createDRBlist(ue, false);
  while (idx < drb_list->list.count) {
    const NR_DRB_ToAddMod_t *drbc = drb_list->list.array[idx];
    if (drbc->drb_Identity == drb_id)
      break;
    ++idx;
  }
  if (idx < drb_list->list.count) {
    nr_pdcp_release_drb(rnti, drb_id);
    asn_sequence_del(&drb_list->list, idx, 1);
  }
  *asn1_drb = drb_id;
  asn1cSeqAdd(&ue->DRB_ReleaseList->list, asn1_drb);

  f1ap_drb_to_be_released_t drbs_to_be_released[1] = {{.rb_id = drb_id}};
  f1_ue_data_t ue_data = cu_get_f1_ue_data(ue->rrc_ue_id);
  f1ap_ue_context_modif_req_t ue_context_modif_req = {
    .gNB_CU_ue_id = ue->rrc_ue_id,
    .gNB_DU_ue_id = ue_data.secondary_ue,
    .plmn.mcc = rrc->configuration[ue->primaryCC_id].mcc[0],
    .plmn.mnc = rrc->configuration[ue->primaryCC_id].mnc[0],
    .plmn.mnc_digit_length = rrc->configuration[ue->primaryCC_id].mnc_digit_length[0],
    .nr_cellid = rrc->nr_cellid,
    .servCellId = 0, /* TODO: correct value? */
    .srbs_to_be_setup_length = 0,
    .srbs_to_be_setup = NULL,
    .drbs_to_be_setup_length = 0,
    .drbs_to_be_setup = NULL,
    .drbs_to_be_released_length = 1,
    .drbs_to_be_released = drbs_to_be_released,
  };
  rrc->mac_rrc.ue_context_modification_request(&ue_context_modif_req);
}

int rrc_gNB_generate_pcch_msg(uint32_t tmsi, uint8_t paging_drx, instance_t instance, uint8_t CC_id){
  const unsigned int Ttab[4] = {32,64,128,256};
  uint8_t Tc;
  uint8_t Tue;
  uint32_t pfoffset;
  uint32_t N;  /* N: min(T,nB). total count of PF in one DRX cycle */
  uint32_t Ns = 0;  /* Ns: max(1,nB/T) */
  uint8_t i_s;  /* i_s = floor(UE_ID/N) mod Ns */
  uint32_t T;  /* DRX cycle */
  uint32_t length;
  uint8_t buffer[RRC_BUF_SIZE];
  const nr_rrc_du_container_t *du = RC.nrrrc[0]->du;
  DevAssert(du != NULL);
  struct NR_SIB1 *sib1 = du->sib1; //TODO W38
  // struct NR_SIB1 *sib1 = RC.nrrrc[instance]->carrier[CC_id].siblock1->message.choice.c1->choice.systemInformationBlockType1;

  /* get default DRX cycle from configuration */
  Tc = sib1->servingCellConfigCommon->downlinkConfigCommon.pcch_Config.defaultPagingCycle;

  Tue = paging_drx;
  /* set T = min(Tc,Tue) */
  T = Tc < Tue ? Ttab[Tc] : Ttab[Tue];
  /* set N = PCCH-Config->nAndPagingFrameOffset */
  switch (sib1->servingCellConfigCommon->downlinkConfigCommon.pcch_Config.nAndPagingFrameOffset.present) {
    case NR_PCCH_Config__nAndPagingFrameOffset_PR_oneT:
      N = T;
      pfoffset = 0;
      break;
    case NR_PCCH_Config__nAndPagingFrameOffset_PR_halfT:
      N = T/2;
      pfoffset = 1;
      break;
    case NR_PCCH_Config__nAndPagingFrameOffset_PR_quarterT:
      N = T/4;
      pfoffset = 3;
      break;
    case NR_PCCH_Config__nAndPagingFrameOffset_PR_oneEighthT:
      N = T/8;
      pfoffset = 7;
      break;
    case NR_PCCH_Config__nAndPagingFrameOffset_PR_oneSixteenthT:
      N = T/16;
      pfoffset = 15;
      break;
    default:
      LOG_E(RRC, "[gNB %ld] In rrc_gNB_generate_pcch_msg:  pfoffset error (pfoffset %d)\n",
            instance, sib1->servingCellConfigCommon->downlinkConfigCommon.pcch_Config.nAndPagingFrameOffset.present);
      return (-1);

  }

  switch (sib1->servingCellConfigCommon->downlinkConfigCommon.pcch_Config.ns) {
    case NR_PCCH_Config__ns_four:
      if(*sib1->servingCellConfigCommon->downlinkConfigCommon.initialDownlinkBWP.pdcch_ConfigCommon->choice.setup->pagingSearchSpace == 0){
        LOG_E(RRC, "[gNB %ld] In rrc_gNB_generate_pcch_msg:  ns error only 1 or 2 is allowed when pagingSearchSpace is 0\n",
              instance);
        return (-1);
      } else {
        Ns = 4;
      }
      break;
    case NR_PCCH_Config__ns_two:
      Ns = 2;
      break;
    case NR_PCCH_Config__ns_one:
      Ns = 1;
      break;
    default:
      LOG_E(RRC, "[gNB %ld] In rrc_gNB_generate_pcch_msg: ns error (ns %ld)\n",
            instance, sib1->servingCellConfigCommon->downlinkConfigCommon.pcch_Config.ns);
      return (-1);
  }

  /* insert data to UE_PF_PO or update data in UE_PF_PO */
  pthread_mutex_lock(&ue_pf_po_mutex);
  uint8_t i = 0;

  for (i = 0; i < MAX_MOBILES_PER_ENB; i++) {
    if ((UE_PF_PO[CC_id][i].enable_flag == true && UE_PF_PO[CC_id][i].ue_index_value == (uint16_t)(tmsi%1024))
        || (UE_PF_PO[CC_id][i].enable_flag != true)) {
      /* set T = min(Tc,Tue) */
      UE_PF_PO[CC_id][i].T = T;
      /* set UE_ID */
      UE_PF_PO[CC_id][i].ue_index_value = (uint16_t)(tmsi%1024);
      /* calculate PF and PO */
      /* set PF_min and PF_offset: (SFN + PF_offset) mod T = (T div N)*(UE_ID mod N) */
      UE_PF_PO[CC_id][i].PF_min = (T / N) * (UE_PF_PO[CC_id][i].ue_index_value % N);
      UE_PF_PO[CC_id][i].PF_offset = pfoffset;
      /* set i_s */
      /* i_s = floor(UE_ID/N) mod Ns */
      i_s = (uint8_t)((UE_PF_PO[CC_id][i].ue_index_value / N) % Ns);
      UE_PF_PO[CC_id][i].i_s = i_s;

      // TODO,set PO

      if (UE_PF_PO[CC_id][i].enable_flag == true) {
        //paging exist UE log
        LOG_D(NR_RRC,"[gNB %ld] CC_id %d In rrc_gNB_generate_pcch_msg: Update exist UE %d, T %d, N %d, PF %d, i_s %d, PF_offset %d\n", instance, CC_id, UE_PF_PO[CC_id][i].ue_index_value,
              T, N, UE_PF_PO[CC_id][i].PF_min, UE_PF_PO[CC_id][i].i_s, UE_PF_PO[CC_id][i].PF_offset);
      } else {
        /* set enable_flag */
        UE_PF_PO[CC_id][i].enable_flag = true;
        //paging new UE log
        LOG_D(NR_RRC,"[gNB %ld] CC_id %d In rrc_gNB_generate_pcch_msg: Insert a new UE %d, T %d, N %d, PF %d, i_s %d, PF_offset %d\n", instance, CC_id, UE_PF_PO[CC_id][i].ue_index_value,
              T, N, UE_PF_PO[CC_id][i].PF_min, UE_PF_PO[CC_id][i].i_s, UE_PF_PO[CC_id][i].PF_offset);
      }
      break;
    }
  }

  pthread_mutex_unlock(&ue_pf_po_mutex);

  /* Create message for PDCP (DLInformationTransfer_t) */
  length = do_NR_Paging(instance, buffer, &tmsi, 0, NULL);

  if (length == -1) {
    LOG_I(NR_RRC, "do_NR_Paging error\n");
    return -1;
  }
  // TODO, send message to pdcp

  return 0;
}

void nr_rrc_trigger(protocol_ctxt_t *ctxt, int frame, int subframe)
{
  MessageDef *message_p;
  message_p = itti_alloc_new_message(TASK_RRC_GNB, 0, RRC_SUBFRAME_PROCESS);
  RRC_SUBFRAME_PROCESS(message_p).ctxt  = *ctxt;
  RRC_SUBFRAME_PROCESS(message_p).CC_id = 0;
  LOG_T(NR_RRC, "Time in RRC: %u/ %u \n", frame, subframe);
  itti_send_msg_to_task(TASK_RRC_GNB, ctxt->module_id, message_p);
}
