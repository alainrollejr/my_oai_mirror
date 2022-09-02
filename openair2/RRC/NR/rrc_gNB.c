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
#include "COMMON/mac_rrc_primitives.h"
#include "RRC/NR/MESSAGES/asn1_msg.h"

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

#include "BIT_STRING.h"
#include "assertions.h"

//#define XER_PRINT

extern RAN_CONTEXT_t RC;

static inline uint64_t bitStr_to_uint64(BIT_STRING_t *asn);

mui_t rrc_gNB_mui = 0;

///---------------------------------------------------------------------------------------------------------------///
///---------------------------------------------------------------------------------------------------------------///

NR_DRB_ToAddModList_t *fill_DRB_configList(gNB_RRC_UE_t *ue)
{
  gNB_RRC_INST *rrc = RC.nrrrc[0];
  if (ue->nb_of_pdusessions == 0)
    return NULL;
  int nb_drb_to_setup = rrc->configuration.drbs;
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
            if (rrc->configuration.drbs > 1) { /* Force the creation from gNB Conf file */
              LOG_W(NR_RRC, "Adding %d DRBs, from gNB config file (not decided by 5GC\n", rrc->configuration.drbs);
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
                      rrc->configuration.enable_sdap,
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

typedef struct deliver_dl_rrc_message_data_s {
  const gNB_RRC_INST *rrc;
  f1ap_dl_rrc_message_t *dl_rrc;
} deliver_dl_rrc_message_data_t;
static void rrc_deliver_dl_rrc_message(void *deliver_pdu_data, ue_id_t ue_id, int srb_id, char *buf, int size, int sdu_id)
{
  DevAssert(deliver_pdu_data != NULL);
  deliver_dl_rrc_message_data_t *data = (deliver_dl_rrc_message_data_t *)deliver_pdu_data;
  data->dl_rrc->rrc_container = (uint8_t *)buf;
  data->dl_rrc->rrc_container_length = size;
  DevAssert(data->dl_rrc->srb_id == srb_id);
  data->rrc->mac_rrc.dl_rrc_message_transfer(0, data->dl_rrc);
}


void nr_rrc_transfer_protected_rrc_message(const gNB_RRC_INST *rrc, const gNB_RRC_UE_t *ue_p, uint8_t srb_id, const uint8_t* buffer, int size)
{
  DevAssert(size > 0);
  f1_ue_data_t ue_data = cu_get_f1_ue_data(ue_p->rrc_ue_id);
  f1ap_dl_rrc_message_t dl_rrc = {.gNB_CU_ue_id = ue_p->rrc_ue_id, .gNB_DU_ue_id = ue_data.secondary_ue, .srb_id = srb_id};
  deliver_dl_rrc_message_data_t data = {.rrc = rrc, .dl_rrc = &dl_rrc};
  nr_pdcp_data_req_srb(ue_p->rrc_ue_id, srb_id, rrc_gNB_mui++, size, (unsigned char *const)buffer, rrc_deliver_dl_rrc_message, &data);
}

///---------------------------------------------------------------------------------------------------------------///
///---------------------------------------------------------------------------------------------------------------///

static instance_t get_instance_by_NR_cellid(uint64_t nr_cellid)
{
  for (int i = 0; i < RC.nb_nr_inst; i++) {
    if (RC.nrrrc[i] && RC.nrrrc[i]->nr_cellid == nr_cellid) {
      return i;
    }
  }
  return -1;
}

static instance_t get_instance_by_F1_instance(instance_t f1_instance)
{
  for (int i = 0; i < RC.nb_nr_inst; i++) {
    if (RC.nrrrc[i] && RC.nrrrc[i]->f1_instance == f1_instance) {
      return i;
    }
  }
  return -1;
}

static void init_NR_SI(gNB_RRC_INST *rrc, gNB_RrcConfigurationReq *configuration)
{

  LOG_D(RRC,"%s()\n\n\n\n",__FUNCTION__);
  if (NODE_IS_DU(rrc->node_type) || NODE_IS_MONOLITHIC(rrc->node_type))
    rrc->carrier.mib = get_new_MIB_NR(rrc->carrier.servingcellconfigcommon);

  if((get_softmodem_params()->sa) && ( (NODE_IS_DU(rrc->node_type) || NODE_IS_MONOLITHIC(rrc->node_type)))) {
    NR_BCCH_DL_SCH_Message_t *sib1 = get_SIB1_NR(configuration);
    rrc->carrier.SIB1 = calloc(NR_MAX_SIB_LENGTH / 8, sizeof(*rrc->carrier.SIB1));
    AssertFatal(rrc->carrier.SIB1 != NULL, "out of memory\n");
    rrc->carrier.sizeof_SIB1 = encode_SIB1_NR(sib1, rrc->carrier.SIB1, NR_MAX_SIB_LENGTH / 8);
    rrc->carrier.siblock1 = sib1;
    nr_mac_config_sib1(RC.nrmac[rrc->module_id], sib1);
  }

  if (!NODE_IS_DU(rrc->node_type)) {
    rrc->carrier.SIB23 = (uint8_t *) malloc16(100);
    AssertFatal(rrc->carrier.SIB23 != NULL, "cannot allocate memory for SIB");
    rrc->carrier.sizeof_SIB23 = do_SIB23_NR(&rrc->carrier, configuration);
    LOG_I(NR_RRC,"do_SIB23_NR, size %d \n ", rrc->carrier.sizeof_SIB23);
    AssertFatal(rrc->carrier.sizeof_SIB23 != 255,"FATAL, RC.nrrrc[mod].carrier[CC_id].sizeof_SIB23 == 255");
  }

  LOG_I(NR_RRC,"Done init_NR_SI\n");

  if (NODE_IS_MONOLITHIC(rrc->node_type) || NODE_IS_DU(rrc->node_type)){
    // update SI info
    nr_mac_config_scc(RC.nrmac[rrc->module_id],
                      rrc->configuration.pdsch_AntennaPorts,
                      rrc->configuration.pusch_AntennaPorts,
                      rrc->configuration.sib1_tda,
                      rrc->configuration.minRXTXTIME,
                      rrc->carrier.servingcellconfigcommon);
    nr_mac_config_mib(RC.nrmac[rrc->module_id], rrc->carrier.mib);
  }

  /* set flag to indicate that cell information is configured. This is required
   * in DU to trigger F1AP_SETUP procedure */
  pthread_mutex_lock(&rrc->cell_info_mutex);
  rrc->cell_info_configured=1;
  pthread_mutex_unlock(&rrc->cell_info_mutex);

  if (get_softmodem_params()->phy_test > 0 || get_softmodem_params()->do_ra > 0) {
    rrc_gNB_ue_context_t *ue_context_p = rrc_gNB_allocate_new_ue_context(rrc);
    gNB_RRC_UE_t *UE = &ue_context_p->ue_context;
    UE->spCellConfig = calloc(1, sizeof(struct NR_SpCellConfig));
    UE->spCellConfig->spCellConfigDedicated = configuration->scd;
    LOG_I(NR_RRC,"Adding new user (%p)\n",ue_context_p);
    if (!NODE_IS_CU(RC.nrrrc[0]->node_type)) {
      rrc_add_nsa_user(rrc,ue_context_p,NULL);
    }
  }
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

static void openair_rrc_gNB_configuration(const module_id_t gnb_mod_idP, gNB_RrcConfigurationReq *configuration)
{
  protocol_ctxt_t      ctxt = { 0 };
  gNB_RRC_INST         *rrc=RC.nrrrc[gnb_mod_idP];
  PROTOCOL_CTXT_SET_BY_MODULE_ID(&ctxt, gnb_mod_idP, GNB_FLAG_YES, NOT_A_RNTI, 0, 0,gnb_mod_idP);
  LOG_I(NR_RRC,
        PROTOCOL_NR_RRC_CTXT_FMT" Init...\n",
        PROTOCOL_NR_RRC_CTXT_ARGS(&ctxt));
  AssertFatal(rrc != NULL, "RC.nrrrc not initialized!");
  AssertFatal(NUMBER_OF_UE_MAX < (module_id_t)0xFFFFFFFFFFFFFFFF, " variable overflow");
  AssertFatal(configuration!=NULL,"configuration input is null\n");
  rrc->module_id = gnb_mod_idP;
  rrc_gNB_CU_DU_init(rrc);
  uid_linear_allocator_init(&rrc->uid_allocator);
  RB_INIT(&rrc->rrc_ue_head);
  rrc->configuration = *configuration;
  rrc->carrier.servingcellconfigcommon = configuration->scc;
  nr_rrc_config_ul_tda(configuration->scc,configuration->minRXTXTIME);
   /// System Information INIT
  pthread_mutex_init(&rrc->cell_info_mutex,NULL);
  rrc->cell_info_configured = 0;
  LOG_I(NR_RRC, PROTOCOL_NR_RRC_CTXT_FMT" Checking release \n",PROTOCOL_NR_RRC_CTXT_ARGS(&ctxt));
  init_NR_SI(rrc, configuration);
  return;
} // END openair_rrc_gNB_configuration

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
unsigned int rrc_gNB_get_next_transaction_identifier(module_id_t gnb_mod_idP)
//-----------------------------------------------------------------------------
{
  static unsigned int transaction_id[NUMBER_OF_gNB_MAX] = {0};
  // used also in NGAP thread, so need thread safe operation
  unsigned int tmp = __atomic_add_fetch(&transaction_id[gnb_mod_idP], 1, __ATOMIC_SEQ_CST);
  tmp %= NR_RRC_TRANSACTION_IDENTIFIER_NUMBER;
  LOG_T(NR_RRC, "generated xid is %d\n", tmp);
  return tmp;
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

//-----------------------------------------------------------------------------
static void rrc_gNB_generate_RRCSetup(instance_t instance,
                                      rnti_t rnti,
                                      rrc_gNB_ue_context_t *const ue_context_pP,
                                      const uint8_t *masterCellGroup,
                                      int masterCellGroup_len)
//-----------------------------------------------------------------------------
{
  LOG_I(NR_RRC, "rrc_gNB_generate_RRCSetup for RNTI %04x\n", rnti);

  gNB_RRC_UE_t *ue_p = &ue_context_pP->ue_context;
  gNB_RRC_INST *rrc = RC.nrrrc[instance];
  unsigned char buf[1024];
  uint8_t xid = rrc_gNB_get_next_transaction_identifier(instance);
  ue_p->xids[xid] = RRC_SETUP;
  NR_SRB_ToAddModList_t *SRBs = createSRBlist(ue_p, false);

  int size = do_RRCSetup(ue_context_pP, buf, xid, masterCellGroup, masterCellGroup_len, &rrc->configuration, SRBs);
  AssertFatal(size > 0, "do_RRCSetup failed\n");
  AssertFatal(size <= 1024, "memory corruption\n");

  LOG_DUMPMSG(NR_RRC, DEBUG_RRC,
              (char *)buf,
              size,
              "[MSG] RRC Setup\n");
  nr_pdcp_add_srbs(true, ue_p->rrc_ue_id, SRBs, 0, NULL, NULL);

  freeSRBlist(SRBs);
  f1_ue_data_t ue_data = cu_get_f1_ue_data(ue_p->rrc_ue_id);
  f1ap_dl_rrc_message_t dl_rrc = {
      .gNB_CU_ue_id = ue_p->rrc_ue_id,
      .gNB_DU_ue_id = ue_data.secondary_ue,
      .rrc_container = buf,
      .rrc_container_length = size,
      .srb_id = CCCH
  };

  int i = instance;
  if (NODE_IS_CU(RC.nrrrc[instance]->node_type)) {
    i = RC.nrrrc[instance]->f1_instance;
  }
  rrc->mac_rrc.dl_rrc_message_transfer(i, &dl_rrc);
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

  f1_ue_data_t ue_data = cu_get_f1_ue_data(ue_p->rrc_ue_id);
  f1ap_dl_rrc_message_t dl_rrc = {
    .gNB_CU_ue_id = ue_p->rrc_ue_id,
    .gNB_DU_ue_id = ue_data.secondary_ue,
    .rrc_container = buf,
    .rrc_container_length = size,
    .srb_id = CCCH,
    .execute_duplication  = 1,
    .RAT_frequency_priority_information.en_dc = 0
  };
  rrc->mac_rrc.dl_rrc_message_transfer(0, &dl_rrc);
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
  AssertFatal(ue_p->nb_of_pdusessions == 0, "logic bug: PDU sessions present before RRC Connection established\n");
  uint8_t xid = rrc_gNB_get_next_transaction_identifier(ctxt_pP->module_id);
  ue_p->xids[xid] = RRC_DEFAULT_RECONF;

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

  NR_MeasConfig_t *measconfig = get_defaultMeasConfig(ctxt_pP->module_id);
  ue_p->measConfig = measconfig;

  uint8_t buffer[RRC_BUF_SIZE] = {0};
  int size = do_RRCReconfiguration(ctxt_pP,
                                   buffer,
                                   RRC_BUF_SIZE,
                                   xid,
                                   NULL, //*SRB_configList2,
                                   NULL, //*DRB_configList,
                                   NULL,
                                   NULL,
                                   NULL,
                                   measconfig,
                                   dedicatedNAS_MessageList,
                                   ue_context_pP,
                                   &rrc->carrier,
                                   &rrc->configuration,
                                   NULL,
                                   ue_p->masterCellGroup);
  AssertFatal(size > 0, "cannot encode RRCReconfiguration in %s()\n", __func__);
  LOG_W(NR_RRC, "do_RRCReconfiguration(): size %d\n", size);

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

  if (NODE_IS_DU(rrc->node_type) || NODE_IS_MONOLITHIC(rrc->node_type)) {
    gNB_RRC_UE_t *ue_p = &ue_context_pP->ue_context;
    nr_rrc_mac_update_cellgroup(ue_p->rnti, ue_p->masterCellGroup);

    uint32_t delay_ms = ue_p->masterCellGroup && ue_p->masterCellGroup->spCellConfig && ue_p->masterCellGroup->spCellConfig->spCellConfigDedicated
                                && ue_p->masterCellGroup->spCellConfig->spCellConfigDedicated->downlinkBWP_ToAddModList
                            ? NR_RRC_RECONFIGURATION_DELAY_MS + NR_RRC_BWP_SWITCHING_DELAY_MS
                            : NR_RRC_RECONFIGURATION_DELAY_MS;

    nr_mac_enable_ue_rrc_processing_timer(ctxt_pP->module_id, ue_p->rnti, *rrc->carrier.servingcellconfigcommon->ssbSubcarrierSpacing, delay_ms);
  }
}

//-----------------------------------------------------------------------------
void rrc_gNB_generate_dedicatedRRCReconfiguration(const protocol_ctxt_t *const ctxt_pP, rrc_gNB_ue_context_t *ue_context_pP)
//-----------------------------------------------------------------------------
{
  gNB_RRC_INST *rrc = RC.nrrrc[ctxt_pP->module_id];

  uint8_t xid = rrc_gNB_get_next_transaction_identifier(ctxt_pP->module_id);
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

  int size = do_RRCReconfiguration(ctxt_pP,
                                   buffer,
                                   RRC_BUF_SIZE,
                                   xid,
                                   SRBs,
                                   DRBs,
                                   NULL,
                                   NULL,
                                   NULL,
                                   NULL,
                                   dedicatedNAS_MessageList,
                                   ue_context_pP,
                                   &rrc->carrier,
                                   &rrc->configuration,
                                   NULL,
                                   cellGroupConfig);
  LOG_DUMPMSG(NR_RRC,DEBUG_RRC,(char *)buffer,size,"[MSG] RRC Reconfiguration\n");
  freeSRBlist(SRBs);
  freeDRBlist(DRBs);
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

  if (NODE_IS_DU(rrc->node_type) || NODE_IS_MONOLITHIC(rrc->node_type)) {
    nr_rrc_mac_update_cellgroup(ue_context_pP->ue_context.rnti, ue_context_pP->ue_context.masterCellGroup);

    uint32_t delay_ms = ue_p->masterCellGroup && ue_p->masterCellGroup->spCellConfig && ue_p->masterCellGroup->spCellConfig->spCellConfigDedicated
                                && ue_p->masterCellGroup->spCellConfig->spCellConfigDedicated->downlinkBWP_ToAddModList
                            ? NR_RRC_RECONFIGURATION_DELAY_MS + NR_RRC_BWP_SWITCHING_DELAY_MS
                            : NR_RRC_RECONFIGURATION_DELAY_MS;

    nr_mac_enable_ue_rrc_processing_timer(ctxt_pP->module_id, ue_p->rnti, *rrc->carrier.servingcellconfigcommon->ssbSubcarrierSpacing, delay_ms);
  }
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
  int size = do_RRCReconfiguration(ctxt_pP,
                                   buffer,
                                   RRC_BUF_SIZE,
                                   xid,
                                   NULL,
                                   DRB_configList,
                                   NULL,
                                   NULL,
                                   NULL,
                                   NULL,
                                   dedicatedNAS_MessageList,
                                   NULL,
                                   NULL,
                                   NULL,
                                   NULL,
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

  if (NODE_IS_DU(rrc->node_type) || NODE_IS_MONOLITHIC(rrc->node_type)) {
    uint32_t delay_ms = ue_p->masterCellGroup && ue_p->masterCellGroup->spCellConfig && ue_p->masterCellGroup->spCellConfig->spCellConfigDedicated
                                && ue_p->masterCellGroup->spCellConfig->spCellConfigDedicated->downlinkBWP_ToAddModList
                            ? NR_RRC_RECONFIGURATION_DELAY_MS + NR_RRC_BWP_SWITCHING_DELAY_MS
                            : NR_RRC_RECONFIGURATION_DELAY_MS;

    nr_mac_enable_ue_rrc_processing_timer(ctxt_pP->module_id, ue_p->rnti, *rrc->carrier.servingcellconfigcommon->ssbSubcarrierSpacing, delay_ms);
  }
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
  int size = do_RRCReconfiguration(ctxt_pP,
                                   buffer,
                                   RRC_BUF_SIZE,
                                   xid,
                                   NULL,
                                   NULL,
                                   DRB_Release_configList2,
                                   NULL,
                                   NULL,
                                   NULL,
                                   dedicatedNAS_MessageList,
                                   NULL,
                                   NULL,
                                   NULL,
                                   NULL,
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

  if (NODE_IS_DU(rrc->node_type) || NODE_IS_MONOLITHIC(rrc->node_type)) {
    uint32_t delay_ms = ue_p->masterCellGroup && ue_p->masterCellGroup->spCellConfig && ue_p->masterCellGroup->spCellConfig->spCellConfigDedicated
                                && ue_p->masterCellGroup->spCellConfig->spCellConfigDedicated->downlinkBWP_ToAddModList
                            ? NR_RRC_RECONFIGURATION_DELAY_MS + NR_RRC_BWP_SWITCHING_DELAY_MS
                            : NR_RRC_RECONFIGURATION_DELAY_MS;

    nr_mac_enable_ue_rrc_processing_timer(ctxt_pP->module_id, ue_p->rnti, *rrc->carrier.servingcellconfigcommon->ssbSubcarrierSpacing, delay_ms);
  }
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

  /* Derive the keys from kgnb */
  if (DRB_configList != NULL) {
    nr_derive_key(UP_ENC_ALG, ue_p->ciphering_algorithm, ue_p->kgnb, kUPenc);
    nr_derive_key(UP_INT_ALG, ue_p->integrity_algorithm, ue_p->kgnb, kUPint);
  }

  nr_derive_key(RRC_ENC_ALG, ue_p->ciphering_algorithm, ue_p->kgnb, kRRCenc);
  nr_derive_key(RRC_INT_ALG, ue_p->integrity_algorithm, ue_p->kgnb, kRRCint);

  /* Refresh SRBs/DRBs */
  LOG_D(NR_RRC, "Configuring PDCP DRBs/SRBs for UE %04x\n", ue_p->rnti);
  ue_context_pP->ue_context.ue_reconfiguration_after_reestablishment_counter++;

  NR_SRB_ToAddModList_t *SRBs = createSRBlist(ue_p, false);
  nr_pdcp_add_srbs(ctxt_pP->enb_flag,
                   ue_p->rrc_ue_id,
                   SRBs,
                   (ue_p->integrity_algorithm << 4) | ue_p->ciphering_algorithm,
                   kRRCenc,
                   kRRCint);
  freeSRBlist(SRBs);
  nr_pdcp_add_drbs(ctxt_pP->enb_flag,
                   ue_p->rrc_ue_id,
                   DRB_configList,
                   (ue_p->integrity_algorithm << 4) | ue_p->ciphering_algorithm,
                   kUPenc,
                   kUPint,
                   get_softmodem_params()->sa ? ue_p->masterCellGroup->rlc_BearerToAddModList : NULL);

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
                                                NR_ServingCellConfigCommon_t *scc,
                                                const rnti_t old_rnti)
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
  int size = do_RRCReestablishment(ue_context_pP,
                                   0 /* CC_id */,
                                   buffer,
                                   RRC_BUF_SIZE,
                                   xid,
                                   SRBs,
                                   masterCellGroup_from_DU,
                                   scc,
                                   &rrc->carrier);

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

  f1_ue_data_t ue_data = cu_get_f1_ue_data(ue_p->rrc_ue_id);
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
  int size = do_RRCReconfiguration(ctxt_pP,
                                   buffer,
                                   RRC_BUF_SIZE,
                                   new_xid,
                                   SRBs,
                                   DRBs,
                                   NULL,
                                   NULL,
                                   NULL,
                                   NULL, // MeasObj_list,
                                   NULL,
                                   ue_context_pP,
                                   &rrc->carrier,
                                   NULL,
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

  if (NODE_IS_DU(RC.nrrrc[ctxt_pP->module_id]->node_type) || NODE_IS_MONOLITHIC(RC.nrrrc[ctxt_pP->module_id]->node_type)) {
    uint32_t delay_ms = ue_p->masterCellGroup && ue_p->masterCellGroup->spCellConfig
                                && ue_p->masterCellGroup->spCellConfig->spCellConfigDedicated
                                && ue_p->masterCellGroup->spCellConfig->spCellConfigDedicated->downlinkBWP_ToAddModList
                            ? NR_RRC_RECONFIGURATION_DELAY_MS + NR_RRC_BWP_SWITCHING_DELAY_MS
                            : NR_RRC_RECONFIGURATION_DELAY_MS;

    nr_mac_enable_ue_rrc_processing_timer(ctxt_pP->module_id,
                                          ue_p->rnti,
                                          *RC.nrrrc[ctxt_pP->module_id]->carrier.servingcellconfigcommon->ssbSubcarrierSpacing,
                                          delay_ms);
  }
}
//-----------------------------------------------------------------------------

int nr_rrc_reconfiguration_req(rrc_gNB_ue_context_t         *const ue_context_pP,
                               protocol_ctxt_t              *const ctxt_pP,
                               const int                    dl_bwp_id,
                               const int                    ul_bwp_id) {

  uint8_t xid = rrc_gNB_get_next_transaction_identifier(ctxt_pP->module_id);
  gNB_RRC_UE_t *ue_p = &ue_context_pP->ue_context;
  ue_p->xids[xid] = RRC_DEDICATED_RECONF;

  NR_CellGroupConfig_t *masterCellGroup = ue_p->masterCellGroup;
  if (dl_bwp_id > 0) {
    *masterCellGroup->spCellConfig->spCellConfigDedicated->firstActiveDownlinkBWP_Id = dl_bwp_id;
    *masterCellGroup->spCellConfig->spCellConfigDedicated->defaultDownlinkBWP_Id = dl_bwp_id;
  }
  if (ul_bwp_id > 0) {
    *masterCellGroup->spCellConfig->spCellConfigDedicated->uplinkConfig->firstActiveUplinkBWP_Id = ul_bwp_id;
  }

  uint8_t buffer[RRC_BUF_SIZE];
  int size = do_RRCReconfiguration(ctxt_pP,
                                       buffer,
                                       RRC_BUF_SIZE,
                                       xid,
                                       NULL,
                                       NULL,
                                       NULL,
                                       NULL,
                                       NULL,
                                       NULL,
                                       NULL,
                                       ue_context_pP,
                                       NULL,
                                       NULL,
                                       NULL,
                                       masterCellGroup);

  gNB_RRC_INST *rrc = RC.nrrrc[ctxt_pP->module_id];
  nr_rrc_transfer_protected_rrc_message(rrc, ue_p, DCCH, buffer, size);

  if (NODE_IS_DU(rrc->node_type) || NODE_IS_MONOLITHIC(rrc->node_type)) {
    nr_rrc_mac_update_cellgroup(ue_context_pP->ue_context.rnti, masterCellGroup);

    uint32_t delay_ms = ue_p->masterCellGroup && ue_p->masterCellGroup->spCellConfig
                                && ue_p->masterCellGroup->spCellConfig->spCellConfigDedicated
                                && ue_p->masterCellGroup->spCellConfig->spCellConfigDedicated->downlinkBWP_ToAddModList
                            ? NR_RRC_RECONFIGURATION_DELAY_MS + NR_RRC_BWP_SWITCHING_DELAY_MS
                            : NR_RRC_RECONFIGURATION_DELAY_MS;

    nr_mac_enable_ue_rrc_processing_timer(ctxt_pP->module_id,
                                          ue_p->rnti,
                                          *rrc->carrier.servingcellconfigcommon->ssbSubcarrierSpacing,
                                          delay_ms);
  }

  return 0;
}

/*------------------------------------------------------------------------------*/
static int nr_rrc_gNB_decode_ccch(module_id_t module_id, const f1ap_initial_ul_rrc_message_t *msg)
{
  asn_dec_rval_t                                    dec_rval;
  NR_UL_CCCH_Message_t *ul_ccch_msg = NULL;
  gNB_RRC_INST *gnb_rrc_inst = RC.nrrrc[module_id];
  NR_RRCSetupRequest_IEs_t                         *rrcSetupRequest = NULL;
  NR_RRCReestablishmentRequest_IEs_t rrcReestablishmentRequest;
  rnti_t rnti = msg->crnti;

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
            /* randomValue                         BIT STRING (SIZE (39)) */
            if (rrcSetupRequest->ue_Identity.choice.randomValue.size != 5) { // 39-bit random value
              LOG_E(NR_RRC, "wrong InitialUE-Identity randomValue size, expected 5, provided %lu", (long unsigned int)rrcSetupRequest->ue_Identity.choice.randomValue.size);
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
            /* TODO */
            /* <5G-S-TMSI> = <AMF Set ID><AMF Pointer><5G-TMSI> 48-bit */
            /* ng-5G-S-TMSI-Part1                  BIT STRING (SIZE (39)) */
            if (rrcSetupRequest->ue_Identity.choice.ng_5G_S_TMSI_Part1.size != 5) {
              LOG_E(NR_RRC, "wrong ng_5G_S_TMSI_Part1 size, expected 5, provided %lu \n", (long unsigned int)rrcSetupRequest->ue_Identity.choice.ng_5G_S_TMSI_Part1.size);
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
              if (ue_context_p == NULL) {
                LOG_E(NR_RRC, "rrc_gNB_get_next_free_ue_context returned NULL\n");
                return -1;
              }
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
          UE = &ue_context_p->ue_context;
          UE->establishment_cause = rrcSetupRequest->establishmentCause;
          UE->Srb[1].Active = 1;
          rrc_gNB_generate_RRCSetup(module_id,
                                    rnti,
                                    ue_context_p,
                                    msg->du2cu_rrc_container,
                                    msg->du2cu_rrc_container_length);
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
        if (physCellId != gnb_rrc_inst->carrier.physCellId) {
          /* UE was moving from previous cell so quickly that RRCReestablishment for previous cell was received in this cell */
          LOG_E(NR_RRC,
                " NR_RRCReestablishmentRequest ue_Identity.physCellId(%ld) is not equal to current physCellId(%ld), fallback to RRC establishment\n",
                physCellId,
                gnb_rrc_inst->carrier.physCellId);
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
          rrc_gNB_generate_RRCSetup(module_id, rnti, ue_context_p, msg->du2cu_rrc_container, msg->du2cu_rrc_container_length);
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
          rrc_gNB_generate_RRCSetup(module_id, rnti, ue_context_p, msg->du2cu_rrc_container, msg->du2cu_rrc_container_length);
          break;
        }

        /* TODO: start timer in ITTI and drop UE if it does not come back */

        // update with new RNTI, and update secondary UE association
        UE->rnti = rnti;
        cu_remove_f1_ue_data(UE->rrc_ue_id);
        f1_ue_data_t ue_data = {.secondary_ue = rnti};
        cu_add_f1_ue_data(UE->rrc_ue_id, &ue_data);

        UE->reestablishment_cause = cause;
        LOG_D(NR_RRC, "Accept RRCReestablishmentRequest from UE physCellId %ld cause %ld\n", physCellId, cause);

        rrc_gNB_generate_RRCReestablishment(ue_context_p,
                                            msg->du2cu_rrc_container,
                                            gnb_rrc_inst->carrier.servingcellconfigcommon,
                                            old_rnti);
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

static void rrc_gNB_process_MeasurementReport(const protocol_ctxt_t *ctxt_p, rrc_gNB_ue_context_t *ue_context, const NR_MeasurementReport_t *measurementReport)
{
  LOG_I(NR_RRC, "Process measurementReport\n");

  if (LOG_DEBUGFLAG(DEBUG_ASN1))
    xer_fprint(stdout, &asn_DEF_NR_MeasurementReport, (void *)measurementReport);

  DevAssert(measurementReport->criticalExtensions.present == NR_MeasurementReport__criticalExtensions_PR_measurementReport
            && measurementReport->criticalExtensions.choice.measurementReport != NULL);

  NR_MeasurementReport_IEs_t *measurementReport_IEs = measurementReport->criticalExtensions.choice.measurementReport;
  const NR_MeasId_t id = measurementReport_IEs->measResults.measId;
  gNB_RRC_UE_t *ue_ctxt = &ue_context->ue_context;

  AssertFatal(id, "unexpected MeasResult for MeasurementId %ld received\n", id);
  asn1cCallocOne(ue_ctxt->measResults, measurementReport_IEs->measResults);

  NR_MeasConfig_t *meas_config = ue_ctxt->measConfig;
  if (meas_config == NULL) {
    LOG_W(NR_RRC, "%s: %i - meas_config = %p\n", __FUNCTION__, __LINE__, meas_config);
    return;
  }

  NR_MeasResults_t *meas_results = &measurementReport_IEs->measResults;
  if (ue_ctxt->measResults != NULL) {
    // ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_NR_MeasResults, ue_ctxt->measResults);
    free(ue_ctxt->measResults);
  }
  ue_ctxt->measResults = meas_results;

  // Get the meas_id_s structure which contains the measId, measObjectId and reportConfigId
  NR_MeasIdToAddMod_t *meas_id_s = NULL;
  for (int meas_idx = 0; meas_idx < meas_config->measIdToAddModList->list.count; meas_idx++) {
    if (id == meas_config->measIdToAddModList->list.array[meas_idx]->measId) {
      meas_id_s = meas_config->measIdToAddModList->list.array[meas_idx];
      break;
    }
  }

  // Get the report_config
  struct NR_ReportConfigToAddMod__reportConfig *report_config = NULL;
  for (int rep_id = 0; rep_id < meas_config->reportConfigToAddModList->list.count; rep_id++) {
    if (meas_id_s->reportConfigId == meas_config->reportConfigToAddModList->list.array[rep_id]->reportConfigId) {
      report_config = &meas_config->reportConfigToAddModList->list.array[rep_id]->reportConfig;
    }
  }

  NR_EventTriggerConfig_t *event_triggered = report_config->choice.reportConfigNR->reportType.choice.eventTriggered;
  bool trigger_ho = false;

  switch (event_triggered->eventId.present) {
    case NR_EventTriggerConfig__eventId_PR_eventA1:
      LOG_I(NR_RRC, "Event A1 (Serving becomes better than threshold)\n");
      break;

    case NR_EventTriggerConfig__eventId_PR_eventA2:
      LOG_I(NR_RRC, "Event A2 (Serving becomes worse than threshold)\n");
      break;

    case NR_EventTriggerConfig__eventId_PR_eventA3:

      LOG_I(NR_RRC, "Event A3 (Neighbour becomes offset better than SpCell)\n");

      for (int res_mo_idx = 0; res_mo_idx < meas_results->measResultServingMOList.list.count; res_mo_idx++) {
        NR_MeasResultServMO_t *meas_result_serv_MO = meas_results->measResultServingMOList.list.array[res_mo_idx];

        struct NR_MeasQuantityResults *active_mq_res = NULL;
        if (meas_result_serv_MO->measResultServingCell.measResult.cellResults.resultsSSB_Cell) {
          active_mq_res = meas_result_serv_MO->measResultServingCell.measResult.cellResults.resultsSSB_Cell;
        } else {
          active_mq_res = meas_result_serv_MO->measResultServingCell.measResult.cellResults.resultsCSI_RS_Cell;
        }
        AssertFatal(active_mq_res, "No active cell measurement results!\n");

        long rsrp_s = (*active_mq_res->rsrp) - 157;
        LOG_I(NR_RRC, "[Active gNB %d] UE %04lx (Measurement Id %li): RSRP = %li dBm\n", ctxt_p->module_id, ctxt_p->rntiMaybeUEid, id, rsrp_s);

        struct NR_MeasQuantityResults *neighboring_mq_res = NULL;
        if (meas_result_serv_MO->measResultServingCell.measResult.cellResults.resultsSSB_Cell) {
          neighboring_mq_res = meas_result_serv_MO->measResultBestNeighCell->measResult.cellResults.resultsSSB_Cell;
        } else {
          neighboring_mq_res = meas_result_serv_MO->measResultBestNeighCell->measResult.cellResults.resultsCSI_RS_Cell;
        }
        AssertFatal(neighboring_mq_res, "No neighboring cell measurement results!\n");

        long rsrp_t = (*neighboring_mq_res->rsrp) - 157;
        LOG_I(NR_RRC, "[BestNeighCell] UE %04lx (Measurement Id %li): RSRP = %li dBm\n", ctxt_p->rntiMaybeUEid, id, rsrp_t);

        long rsrp_offset = event_triggered->eventId.choice.eventA3 ? (event_triggered->eventId.choice.eventA3->a3_Offset.choice.rsrp) >> 1 : 0;

        if (rsrp_t > (rsrp_s + rsrp_offset)) {
          trigger_ho = true;
        }
      }
      break;

    case NR_EventTriggerConfig__eventId_PR_eventA4:
      LOG_I(NR_RRC, "Event A4 (Neighbour becomes better than threshold)\n");
      break;

    case NR_EventTriggerConfig__eventId_PR_eventA5:
      LOG_I(NR_RRC, "Event A5 (SpCell becomes worse than threshold1 and neighbour becomes better than threshold2)\n");
      break;

    case NR_EventTriggerConfig__eventId_PR_eventA6:
      LOG_I(NR_RRC, "Event A6 (Neighbour becomes offset better than SCell)\n");
      break;

    default:
      LOG_I(NR_RRC, "NR_EventTriggerConfig__eventId_PR_NOTHING or Other event report\n");
      break;
  }

  if (trigger_ho == true && ue_ctxt->StatusRrc != NR_RRC_HO_EXECUTION) {
    LOG_I(NR_RRC, "Handover triggered\n");

    // The gNB-CU sends an UE CONTEXT SETUP REQUEST message to the target gNB-DU to create an UE context and setup
    // SRBs and DRBs bearers

    ue_ctxt->StatusRrc = NR_RRC_HO_EXECUTION;
    NR_PhysCellId_t physCellId = *meas_results->measResultServingMOList.list.array[0]->measResultBestNeighCell->physCellId;

    MessageDef *message_p;
    message_p = itti_alloc_new_message(TASK_RRC_GNB, 0, F1AP_UE_CONTEXT_SETUP_REQ);
    f1ap_ue_context_setup_t *req = &F1AP_UE_CONTEXT_SETUP_REQ(message_p);

    // UE_IDs will be extracted from F1AP layer
    gNB_RRC_INST *rrc = RC.nrrrc[ctxt_p->module_id];
    req->mcc = rrc->configuration.mcc[0];
    req->mnc = rrc->configuration.mnc[0];
    req->mnc_digit_length = rrc->configuration.mnc_digit_length[0];
    req->nr_cellid = rrc->nr_cellid;

    // Instruction towards the DU for SRBs configuration
    req->srbs_to_be_setup_length = 2;
    req->srbs_to_be_setup = calloc(req->srbs_to_be_setup_length, sizeof(f1ap_srb_to_be_setup_t));
    f1ap_srb_to_be_setup_t *SRBs = req->srbs_to_be_setup;
    SRBs[0].srb_id = 1;
    SRBs[0].lcid = 1;
    SRBs[1].srb_id = 2;
    SRBs[1].lcid = 2;

    // Instruction towards the DU for DRB configuration and tunnel creation
    req->drbs_to_be_setup = malloc(1 * sizeof(f1ap_drb_to_be_setup_t));
    req->drbs_to_be_setup_length = 1;
    f1ap_drb_to_be_setup_t *DRBs = req->drbs_to_be_setup;
    DRBs[0].drb_id = 1;
    DRBs[0].rlc_mode = RLC_MODE_AM;
    DRBs[0].up_ul_tnl[0].tl_address = inet_addr(rrc->eth_params_s.my_addr);
    DRBs[0].up_ul_tnl[0].port = rrc->eth_params_s.my_portd;
    DRBs[0].up_ul_tnl_length = 1;
    DRBs[0].up_dl_tnl[0].tl_address = inet_addr(rrc->eth_params_s.remote_addr);
    DRBs[0].up_dl_tnl[0].port = rrc->eth_params_s.remote_portd;
    DRBs[0].up_dl_tnl_length = 1;

    // CellGroupConfig
    instance_t instance = get_instance_by_NR_physCellId(physCellId);
    NR_ServingCellConfigCommon_t *scc = RC.nrrrc[instance]->carrier.servingcellconfigcommon;
    if (ue_ctxt->masterCellGroup->spCellConfig->reconfigurationWithSync == NULL) {
      ue_ctxt->masterCellGroup->spCellConfig->reconfigurationWithSync = calloc(1, sizeof(*ue_ctxt->masterCellGroup->spCellConfig->reconfigurationWithSync));
      NR_ReconfigurationWithSync_t *reconfigurationWithSync = ue_ctxt->masterCellGroup->spCellConfig->reconfigurationWithSync;
      reconfigurationWithSync->spCellConfigCommon = scc;
      reconfigurationWithSync->spCellConfigCommon->downlinkConfigCommon->initialDownlinkBWP->pdcch_ConfigCommon =
          rrc->carrier.sib1->servingCellConfigCommon->downlinkConfigCommon.initialDownlinkBWP.pdcch_ConfigCommon;
      reconfigurationWithSync->newUE_Identity = get_new_rnti(rrc);
      reconfigurationWithSync->t304 = NR_ReconfigurationWithSync__t304_ms2000;
      reconfigurationWithSync->rach_ConfigDedicated = NULL;
      reconfigurationWithSync->ext1 = NULL;
    }
    if (ue_ctxt->masterCellGroup->spCellConfig->reconfigurationWithSync->rach_ConfigDedicated == NULL) {
      ue_ctxt->masterCellGroup->spCellConfig->reconfigurationWithSync->rach_ConfigDedicated = calloc(1, sizeof(struct NR_ReconfigurationWithSync__rach_ConfigDedicated));
      struct NR_ReconfigurationWithSync__rach_ConfigDedicated *rach_ConfigDedicated = ue_ctxt->masterCellGroup->spCellConfig->reconfigurationWithSync->rach_ConfigDedicated;
      rach_ConfigDedicated->present = NR_ReconfigurationWithSync__rach_ConfigDedicated_PR_uplink;
      rach_ConfigDedicated->choice.uplink = calloc(1, sizeof(struct NR_RACH_ConfigDedicated));
      rach_ConfigDedicated->choice.uplink->ra_Prioritization = NULL;
      rach_ConfigDedicated->choice.uplink->ext1 = NULL;
      rach_ConfigDedicated->choice.uplink->cfra = calloc(1, sizeof(struct NR_CFRA));
      struct NR_CFRA *cfra = rach_ConfigDedicated->choice.uplink->cfra;
      cfra->occasions = calloc(1, sizeof(struct NR_CFRA__occasions));
      memcpy(&cfra->occasions->rach_ConfigGeneric, &scc->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup->rach_ConfigGeneric, sizeof(NR_RACH_ConfigGeneric_t));
      cfra->occasions->ssb_perRACH_Occasion = calloc(1, sizeof(long));
      *cfra->occasions->ssb_perRACH_Occasion = NR_CFRA__occasions__ssb_perRACH_Occasion_one;
      cfra->resources.present = NR_CFRA__resources_PR_ssb;
      cfra->resources.choice.ssb = calloc(1, sizeof(struct NR_CFRA__resources__ssb));
      cfra->resources.choice.ssb->ra_ssb_OccasionMaskIndex = 0;
      int n_ssb = 0;
      uint64_t bitmap = get_ssb_bitmap(scc);
      struct NR_CFRA_SSB_Resource *ssbElem[64];
      for (int i = 0; i < 64; i++) {
        if ((bitmap >> (63 - i)) & 0x01) {
          ssbElem[n_ssb] = calloc(1, sizeof(struct NR_CFRA_SSB_Resource));
          ssbElem[n_ssb]->ssb = i;
          ssbElem[n_ssb]->ra_PreambleIndex = 63 - (ue_ctxt->rrc_ue_id % 64);
          ASN_SEQUENCE_ADD(&cfra->resources.choice.ssb->ssb_ResourceList.list, ssbElem[n_ssb]);
          n_ssb++;
        }
      }
    }
    req->cu_to_du_rrc_information = calloc(1, sizeof(cu_to_du_rrc_information_t));
    req->cu_to_du_rrc_information->ie_extensions = calloc(1, sizeof(protocol_extension_container_t));
    req->cu_to_du_rrc_information->ie_extensions->cell_group_config = calloc(1, RRC_BUF_SIZE);
    asn_enc_rval_t enc_rval = uper_encode_to_buffer(&asn_DEF_NR_CellGroupConfig,
                                                    NULL,
                                                    ue_ctxt->masterCellGroup,
                                                    req->cu_to_du_rrc_information->ie_extensions->cell_group_config,
                                                    RRC_BUF_SIZE);

    AssertFatal(enc_rval.encoded > 0, "ASN1 NR_CellGroupConfig encoding failed (%s, %jd)!\n", enc_rval.failed_type->name, enc_rval.encoded);

    req->cu_to_du_rrc_information->ie_extensions->cell_group_config_length = (enc_rval.encoded + 7) >> 3;

    if (ue_ctxt->handover_info == NULL) {
      ue_ctxt->handover_info = calloc(1, sizeof(NR_HANDOVER_INFO));
    }
    ue_ctxt->handover_info->modid_s = ctxt_p->module_id;
    ue_ctxt->handover_info->modid_t = instance;

    req->gNB_CU_ue_id = ue_ctxt->rrc_ue_id;
    req->gNB_DU_ue_id = 0xFFFF;

    for (int ho_idx = 0; ho_idx < NUMBER_OF_DU_PER_CU_MAX; ho_idx++) {
      if (ho_rnti_map[ho_idx][0] == 0) {
        ho_rnti_map[ho_idx][0] = ue_ctxt->rnti;
        ho_rnti_map[ho_idx][1] = ue_ctxt->handover_info->modid_s;
        ho_rnti_map[ho_idx][2] = 0;
        ho_rnti_map[ho_idx][3] = ue_ctxt->handover_info->modid_t;
      }
    }

    itti_send_msg_to_task(TASK_CU_F1, RC.nrrrc[instance]->f1_instance, message_p);
  }
}

void nr_update_ngu_tunnel_after_handover(gNB_RRC_INST *rrc_instance_source, const long rnti_s, struct rrc_gNB_ue_context_s *ue_context_target, const long rnti_t)
{
  struct rrc_gNB_ue_context_s *ue_context_source = rrc_gNB_get_ue_context_by_rnti(rrc_instance_source, rnti_s);

  gtpv1u_gnb_create_tunnel_req_t create_tunnel_req = {0};

  int j = 0;
  for (int i = 0; i < NB_RB_MAX; i++) {
    if (ue_context_source->ue_context.pduSession[i].status == PDU_SESSION_STATUS_ESTABLISHED || ue_context_source->ue_context.pduSession[i].status == PDU_SESSION_STATUS_DONE) {
      create_tunnel_req.pdusession_id[j] = ue_context_source->ue_context.pduSession[i].param.pdusession_id;
      create_tunnel_req.incoming_rb_id[j] = i + 1;
      create_tunnel_req.outgoing_teid[j] = ue_context_source->ue_context.pduSession[i].param.gtp_teid;
      create_tunnel_req.outgoing_qfi[j] = ue_context_source->ue_context.pduSession[i].param.qos[0].qfi;
      memcpy(create_tunnel_req.dst_addr[j].buffer, ue_context_source->ue_context.pduSession[i].param.upf_addr.buffer, sizeof(uint8_t) * 20);
      create_tunnel_req.dst_addr[j].length = ue_context_source->ue_context.pduSession[i].param.upf_addr.length;
      j++;
    }
  }

  create_tunnel_req.ue_id = rnti_t;
  create_tunnel_req.num_tunnels = j;

  gtpv1u_update_ngu_tunnel(0, &create_tunnel_req, rnti_s);
}

rnti_t find_ho_rnti_by_modid_s(const uint8_t modid_s)
{
  for (int ho_id = 0; ho_id < NUMBER_OF_DU_PER_CU_MAX; ho_id++) {
    if (ho_rnti_map[ho_id][1] == modid_s) {
      return ho_rnti_map[ho_id][0];
    }
  }
  return -1;
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
  rrc_gNB_generate_defaultRRCReconfiguration(ctxt_pP, ue_context_p);

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
  f1_ue_data_t ue_data = cu_get_f1_ue_data(UE->rrc_ue_id);
  f1ap_ue_context_modif_req_t ue_context_modif_req = {
    .gNB_CU_ue_id = UE->rrc_ue_id,
    .gNB_DU_ue_id = ue_data.secondary_ue,
    .mcc = rrc->configuration.mcc[0],
    .mnc = rrc->configuration.mnc[0],
    .mnc_digit_length = rrc->configuration.mnc_digit_length[0],
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

  /* we look up by CU UE ID! Do NOT change back to RNTI! */
  rrc_gNB_ue_context_t *ue_context_p = rrc_gNB_get_ue_context(gnb_rrc_inst, ctxt_pP->rntiMaybeUEid);
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
        LOG_A(NR_RRC, "Received measurementReport\n");
        rrc_gNB_process_MeasurementReport(ctxt_pP, ue_context_p, ul_dcch_msg->message.choice.c1->choice.measurementReport);
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
        nr_rrc_pdcp_config_security(ctxt_pP, ue_context_p, 1);

        rrc_gNB_generate_UECapabilityEnquiry(ctxt_pP, ue_context_p);
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
  return 0;
}

void rrc_gNB_process_f1_setup_req(f1ap_setup_req_t *f1_setup_req, instance_t instance) {

  LOG_I(NR_RRC,"Received F1 Setup Request from gNB_DU %llu (%s)\n",(unsigned long long int)f1_setup_req->gNB_DU_id,f1_setup_req->gNB_DU_name);

  int cu_cell_ind = 0;
  MessageDef *msg_p =itti_alloc_new_message (TASK_RRC_GNB, 0, F1AP_SETUP_RESP);
  F1AP_SETUP_RESP (msg_p).num_cells_to_activate = 0;
  MessageDef *msg_p2=itti_alloc_new_message (TASK_RRC_GNB, 0, F1AP_GNB_CU_CONFIGURATION_UPDATE);

  for (int i = 0; i < f1_setup_req->num_cells_available; i++) {

    gNB_RRC_INST *rrc = RC.nrrrc[instance];

      if (rrc->configuration.mcc[0] == f1_setup_req->cell[i].mcc &&
          rrc->configuration.mnc[0] == f1_setup_req->cell[i].mnc &&
          rrc->nr_cellid == f1_setup_req->cell[i].nr_cellid) {
      //fixme: multi instance is not consistent here
      F1AP_SETUP_RESP (msg_p).gNB_CU_name  = rrc->node_name;
      // check that CU rrc instance corresponds to mcc/mnc/cgi (normally cgi should be enough, but just in case)
      LOG_W(NR_RRC, "instance %d sib1 length %d\n", i, f1_setup_req->sib1_length[i]);
      AssertFatal(rrc->carrier.mib == NULL, "CU MIB is already initialized: double F1 setup request?\n");
      asn_dec_rval_t dec_rval = uper_decode_complete(NULL,
                                                     &asn_DEF_NR_BCCH_BCH_Message,
                                                     (void **)&rrc->carrier.mib,
                                                     f1_setup_req->mib[i],
                                                     f1_setup_req->mib_length[i]);
      AssertFatal(dec_rval.code == RC_OK,
                  "[gNB_CU %"PRIu8"] Failed to decode NR_BCCH_BCH_MESSAGE (%zu bits)\n",
                  (uint8_t)instance,
                  dec_rval.consumed );

      dec_rval = uper_decode_complete(NULL,
                                      &asn_DEF_NR_SIB1,
                                      (void **)&rrc->carrier.siblock1_DU,
                                      f1_setup_req->sib1[i],
                                      f1_setup_req->sib1_length[i]);

      AssertFatal(dec_rval.code == RC_OK,
                  "[gNB_DU %"PRIu8"] Failed to decode NR_BCCH_DLSCH_MESSAGE (%zu bits)\n",
                  (uint8_t)instance,
                  dec_rval.consumed );

      // Parse message and extract SystemInformationBlockType1 field
      rrc->carrier.sib1 = rrc->carrier.siblock1_DU;
      if (LOG_DEBUGFLAG(DEBUG_ASN1)){
        LOG_I(NR_RRC, "Printing received SIB1 container inside F1 setup request message:\n");
        xer_fprint(stdout, &asn_DEF_NR_SIB1,(void *)rrc->carrier.sib1);
      }

      rrc->carrier.physCellId = f1_setup_req->cell[i].nr_pci;

      F1AP_GNB_CU_CONFIGURATION_UPDATE (msg_p2).gNB_CU_name = rrc->node_name;
      F1AP_GNB_CU_CONFIGURATION_UPDATE (msg_p2).cells_to_activate[cu_cell_ind].mcc = rrc->configuration.mcc[0];
      F1AP_GNB_CU_CONFIGURATION_UPDATE (msg_p2).cells_to_activate[cu_cell_ind].mnc = rrc->configuration.mnc[0];
      F1AP_GNB_CU_CONFIGURATION_UPDATE (msg_p2).cells_to_activate[cu_cell_ind].mnc_digit_length = rrc->configuration.mnc_digit_length[0];
      F1AP_GNB_CU_CONFIGURATION_UPDATE (msg_p2).cells_to_activate[cu_cell_ind].nr_cellid = rrc->nr_cellid;
      F1AP_GNB_CU_CONFIGURATION_UPDATE (msg_p2).cells_to_activate[cu_cell_ind].nrpci = f1_setup_req->cell[i].nr_pci;

      int num_SI= 0;
      if (rrc->carrier.SIB23) {
        F1AP_GNB_CU_CONFIGURATION_UPDATE (msg_p2).cells_to_activate[cu_cell_ind].SI_container[2]        = rrc->carrier.SIB23;
        F1AP_GNB_CU_CONFIGURATION_UPDATE (msg_p2).cells_to_activate[cu_cell_ind].SI_container_length[2] = rrc->carrier.sizeof_SIB23;
        num_SI++;
      }
      F1AP_GNB_CU_CONFIGURATION_UPDATE (msg_p2).cells_to_activate[cu_cell_ind].num_SI = num_SI;

      cu_cell_ind++;
      F1AP_GNB_CU_CONFIGURATION_UPDATE (msg_p2).num_cells_to_activate = cu_cell_ind;

    } else { // setup_req mcc/mnc match rrc internal list element
      LOG_W(NR_RRC, "[Inst %d] No matching MCC/MNC: rrc->mcc/f1_setup_req->mcc %d/%d rrc->mnc/f1_setup_req->mnc %d/%d rrc->nr_cellid/f1_setup_req->nr_cellid %ld/%ld \n",
            (uint8_t)instance, rrc->configuration.mcc[0], f1_setup_req->cell[i].mcc,
            rrc->configuration.mnc[0], f1_setup_req->cell[i].mnc,
            rrc->nr_cellid, f1_setup_req->cell[i].nr_cellid);
    }

    if (cu_cell_ind == 0) {
      AssertFatal(1 == 0, "No cell found\n");
    } else {
      // send ITTI message to F1AP-CU task
      itti_send_msg_to_task (TASK_CU_F1, rrc->f1_instance, msg_p);
      itti_send_msg_to_task (TASK_CU_F1, rrc->f1_instance, msg_p2);
    }

    // handle other failure cases
  }//for (int i=0;i<f1_setup_req->num_cells_available;i++)
}

void rrc_gNB_process_initial_ul_rrc_message(const f1ap_initial_ul_rrc_message_t *ul_rrc)
{
  // first get RRC instance (note, no the ITTI instance)
  module_id_t module_id = get_instance_by_NR_cellid(ul_rrc->nr_cellid);

  //AssertFatal(i != RC.nb_nr_inst, "Cell_id not found\n");
  // TODO REMOVE_DU_RRC in monolithic mode, the MAC does not have the
  // nr_cellid. Thus, the above check would fail. For the time being, just put
  // a warning, as we handle one DU only anyway
  if (module_id >= RC.nb_nr_inst) {
    module_id = 0;
    LOG_W(RRC, "initial UL RRC message nr_cellid %ld does not match RRC's %ld\n", ul_rrc->nr_cellid, RC.nrrrc[0]->nr_cellid);
  }
  nr_rrc_gNB_decode_ccch(module_id, ul_rrc);

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

struct rrc_gNB_ue_context_s *find_ho_ue_context_in_source(uint8_t modid_t, uint8_t *modid_s, uint8_t *ho_idx)
{
  for (int ho_id = 0; ho_id < NUMBER_OF_DU_PER_CU_MAX; ho_id++) {
    if (ho_rnti_map[ho_id][3] == modid_t) {
      if (modid_s != NULL) {
        *modid_s = ho_rnti_map[ho_id][1];
      }
      if (ho_idx != NULL) {
        *ho_idx = ho_id;
      }
      return rrc_gNB_get_ue_context_by_rnti(RC.nrrrc[ho_rnti_map[ho_id][1]], ho_rnti_map[ho_id][0]);
    }
  }
  return NULL;
}

static void rrc_CU_process_ue_context_setup_response(MessageDef *msg_p, instance_t instance)
{
  f1ap_ue_context_setup_t *resp = &F1AP_UE_CONTEXT_SETUP_RESP(msg_p);
  gNB_RRC_INST *rrc = RC.nrrrc[instance];
  uint8_t modid_s;
  uint8_t ho_idx;
  rrc_gNB_ue_context_t *ho_ue_context_in_source = NULL;
  rrc_gNB_ue_context_t *ue_context_p = rrc_gNB_get_ue_context(rrc, resp->gNB_CU_ue_id);
  rrc_gNB_ue_context_t *ue_context_old = NULL;
  if (ue_context_p == NULL && resp->crnti != NULL) {
    uint64_t random_value;
    fill_random(&random_value, sizeof(random_value));
    random_value = random_value & 0x7fffffffff;
    ue_context_p = rrc_gNB_create_ue_context(*resp->crnti, rrc, random_value, resp->gNB_DU_ue_id);
    ho_ue_context_in_source = find_ho_ue_context_in_source(instance, &modid_s, &ho_idx);
    ho_rnti_map[ho_idx][2] = resp->gNB_DU_ue_id;
    ue_context_old = rrc_gNB_get_ue_context_by_rnti(RC.nrrrc[modid_s], ho_rnti_map[ho_idx][0]);
    ue_context_p->ue_context.handover_info = ue_context_old->ue_context.handover_info;
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

  if (ho_ue_context_in_source != NULL) {

    MessageDef *message_p;
    message_p = itti_alloc_new_message(TASK_RRC_GNB, 0, F1AP_UE_CONTEXT_MODIFICATION_REQ);
    f1ap_ue_context_setup_t *req = &F1AP_UE_CONTEXT_MODIFICATION_REQ(message_p);
    req->gNB_CU_ue_id = UE->rrc_ue_id;
    req->gNB_DU_ue_id = ho_ue_context_in_source->ue_context.rnti;
    req->mcc = RC.nrrrc[modid_s]->configuration.mcc[0];
    req->mnc = RC.nrrrc[modid_s]->configuration.mnc[0];
    req->mnc_digit_length = RC.nrrrc[modid_s]->configuration.mnc_digit_length[0];
    req->nr_cellid = RC.nrrrc[modid_s]->nr_cellid;

    protocol_ctxt_t ctxt = {.rntiMaybeUEid = ho_rnti_map[ho_idx][0], .module_id = instance, .instance = RC.nrrrc[instance]->f1_instance, .enb_flag = 1, .eNB_index = instance};
    req->rrc_container = calloc(1, RRC_BUF_SIZE*sizeof(uint8_t));
    req->rrc_container_length = rrc_gNB_generate_HO_RRCReconfiguration(&ctxt,
                                                                       modid_s,
                                                                       instance,
                                                                       ue_context_p,
                                                                       ue_context_old,
                                                                       req->rrc_container,
                                                                       RRC_BUF_SIZE,
                                                                       cellGroupConfig);

    // To DU send the Downlink Data Delivery Status
    req->ReconfigComplOutcome = RRCreconf_success;

    // To stop the data transmission
    req->transmission_action_indicator = calloc(1, sizeof(uint8_t));
    *req->transmission_action_indicator = 0;

    itti_send_msg_to_task(TASK_CU_F1, RC.nrrrc[modid_s]->f1_instance, message_p);
  }
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
  rrc_gNB_remove_ue_context(rrc, ue_context_p);
}

static void rrc_CU_process_ue_context_modification_response(MessageDef *msg_p, instance_t instance)
{
  f1ap_ue_context_modif_resp_t *resp = &F1AP_UE_CONTEXT_MODIFICATION_RESP(msg_p);
  protocol_ctxt_t ctxt = {.rntiMaybeUEid = resp->gNB_CU_ue_id, .module_id = instance, .instance = RC.nrrrc[instance]->f1_instance, .enb_flag = 1, .eNB_index = instance};
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

static unsigned int get_dl_bw_mask(const gNB_RRC_INST *rrc, const NR_UE_NR_Capability_t *cap)
{
  int common_band = *rrc->carrier.servingcellconfigcommon->downlinkConfigCommon->frequencyInfoDL->frequencyBandList.list.array[0];
  int common_scs  = rrc->carrier.servingcellconfigcommon->downlinkConfigCommon->frequencyInfoDL->scs_SpecificCarrierList.list.array[0]->subcarrierSpacing;
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

static unsigned int get_ul_bw_mask(const gNB_RRC_INST *rrc, const NR_UE_NR_Capability_t *cap)
{
  int common_band = *rrc->carrier.servingcellconfigcommon->uplinkConfigCommon->frequencyInfoUL->frequencyBandList->list.array[0];
  int common_scs  = rrc->carrier.servingcellconfigcommon->uplinkConfigCommon->frequencyInfoUL->scs_SpecificCarrierList.list.array[0]->subcarrierSpacing;
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

static int get_ul_mimo_layersCB(const gNB_RRC_INST *rrc, const NR_UE_NR_Capability_t *cap)
{
  int common_scs  = rrc->carrier.servingcellconfigcommon->uplinkConfigCommon->frequencyInfoUL->scs_SpecificCarrierList.list.array[0]->subcarrierSpacing;

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

static int get_ul_mimo_layers(const gNB_RRC_INST *rrc, const NR_UE_NR_Capability_t *cap)
{
  int common_scs  = rrc->carrier.servingcellconfigcommon->uplinkConfigCommon->frequencyInfoUL->scs_SpecificCarrierList.list.array[0]->subcarrierSpacing;

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

static int get_dl_mimo_layers(const gNB_RRC_INST *rrc, const NR_UE_NR_Capability_t *cap)
{
  int common_scs  = rrc->carrier.servingcellconfigcommon->downlinkConfigCommon->frequencyInfoDL->scs_SpecificCarrierList.list.array[0]->subcarrierSpacing;

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

int rrc_gNB_process_e1_setup_req(e1ap_setup_req_t *req, instance_t instance) {

  AssertFatal(req->supported_plmns <= PLMN_LIST_MAX_SIZE, "Supported PLMNs is more than PLMN_LIST_MAX_SIZE\n");
  gNB_RRC_INST *rrc = RC.nrrrc[0]; //TODO: remove hardcoding of RC index here
  MessageDef *msg_p = itti_alloc_new_message(TASK_RRC_GNB, instance, E1AP_SETUP_RESP);

  e1ap_setup_resp_t *resp = &E1AP_SETUP_RESP(msg_p);
  resp->transac_id = req->transac_id;

  for (int i=0; i < req->supported_plmns; i++) {
    if (rrc->configuration.mcc[i] == req->plmns[i].mcc &&
        rrc->configuration.mnc[i] == req->plmns[i].mnc) {
      LOG_E(NR_RRC, "PLMNs received from CUUP (mcc:%d, mnc:%d) did not match with PLMNs in RRC (mcc:%d, mnc:%d)\n",
            req->plmns[i].mcc, req->plmns[i].mnc, rrc->configuration.mcc[i], rrc->configuration.mnc[i]);
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

  f1_ue_data_t ue_data = cu_get_f1_ue_data(UE->rrc_ue_id);
  f1ap_ue_context_modif_req_t ue_context_modif_req = {
    .gNB_CU_ue_id = UE->rrc_ue_id,
    .gNB_DU_ue_id = ue_data.secondary_ue,
    .mcc = rrc->configuration.mcc[0],
    .mnc = rrc->configuration.mnc[0],
    .mnc_digit_length = rrc->configuration.mnc_digit_length[0],
    .nr_cellid = rrc->nr_cellid,
    .servCellId = 0, /* TODO: correct value? */
    .srbs_to_be_setup_length = nb_srb,
    .srbs_to_be_setup = srbs,
    .drbs_to_be_setup_length = nb_drb,
    .drbs_to_be_setup = drbs,
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
    if (mqr->rsrp) {
      const long rrsrp = *mqr->rsrp - 156;
      fprintf(f, "RSRP %ld dBm ", rrsrp);
    }
    if (mqr->rsrq) {
      const float rrsrq = (float)(*mqr->rsrq - 87) / 2.0f;
      fprintf(f, "RSRQ %.1f dB ", rrsrq);
    }
    if (mqr->sinr) {
      const float rsinr = (float)(*mqr->sinr - 46) / 2.0f;
      fprintf(f, "SINR %.1f dB ", rsinr);
    }
    fprintf(f, "\n");
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
    f1_ue_data_t ue_data = cu_get_f1_ue_data(ue_ctxt->rrc_ue_id);

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
      fprintf(f,
              "    UE cap: BW DL %x. BW UL %x, DL MIMO Layers %d UL MIMO Layers (CB) %d UL MIMO Layers (nonCB) %d\n",
              get_dl_bw_mask(rrc, ue_ctxt->UE_Capability_nr),
              get_ul_bw_mask(rrc, ue_ctxt->UE_Capability_nr),
              get_dl_mimo_layers(rrc, ue_ctxt->UE_Capability_nr),
              get_ul_mimo_layersCB(rrc, ue_ctxt->UE_Capability_nr),
              get_ul_mimo_layers(rrc, ue_ctxt->UE_Capability_nr));
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

      case F1AP_INITIAL_UL_RRC_MESSAGE:
        AssertFatal(NODE_IS_CU(RC.nrrrc[instance]->node_type) || NODE_IS_MONOLITHIC(RC.nrrrc[instance]->node_type),
                    "should not receive F1AP_INITIAL_UL_RRC_MESSAGE, need call by CU!\n");
        rrc_gNB_process_initial_ul_rrc_message(&F1AP_INITIAL_UL_RRC_MESSAGE(msg_p));
        break;

      /* Messages from PDCP */
      case F1AP_UL_RRC_MESSAGE:
        if (NODE_IS_CU(RC.nrrrc[instance]->node_type)) {
          instance = get_instance_by_F1_instance(instance);
        }
        PROTOCOL_CTXT_SET_BY_INSTANCE(&ctxt,
                                      instance,
                                      GNB_FLAG_YES,
                                      F1AP_UL_RRC_MESSAGE(msg_p).gNB_CU_ue_id,
                                      0,
                                      0);
        if (NODE_IS_CU(RC.nrrrc[ctxt.module_id]->node_type)) {
          ctxt.instance = RC.nrrrc[ctxt.module_id]->f1_instance;
        }
        LOG_D(NR_RRC,"Decoding DCCH %d: ue %04lx, inst %ld, ctxt %p, size %d\n",
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
        if (NODE_IS_CU(RC.nrrrc[instance]->node_type)) {
          instance = get_instance_by_F1_instance(instance);
        }
        rrc_gNB_process_NGAP_DOWNLINK_NAS(msg_p, instance, &rrc_gNB_mui);
        break;

      case NGAP_PDUSESSION_SETUP_REQ:
        if (NODE_IS_CU(RC.nrrrc[instance]->node_type)) {
          instance = get_instance_by_F1_instance(instance);
        }
        rrc_gNB_process_NGAP_PDUSESSION_SETUP_REQ(msg_p, instance);
        break;

      case NGAP_PDUSESSION_MODIFY_REQ:
        rrc_gNB_process_NGAP_PDUSESSION_MODIFY_REQ(msg_p, instance);
        break;

      case NGAP_PDUSESSION_RELEASE_COMMAND:
        rrc_gNB_process_NGAP_PDUSESSION_RELEASE_COMMAND(msg_p, instance);
        break;

      /* Messages from gNB app */
      case NRRRC_CONFIGURATION_REQ:
        openair_rrc_gNB_configuration(instance, &NRRRC_CONFIGURATION_REQ(msg_p));
        break;

      /* Messages from F1AP task */
      case F1AP_SETUP_REQ:
        ctxt.module_id = get_instance_by_NR_cellid(F1AP_SETUP_REQ(msg_p).cell[0].nr_cellid);
        RC.nrrrc[ctxt.module_id]->f1_instance = instance;
        AssertFatal(NODE_IS_CU(RC.nrrrc[instance]->node_type), "should not receive F1AP_SETUP_REQUEST, need call by CU!\n");
        rrc_gNB_process_f1_setup_req(&F1AP_SETUP_REQ(msg_p), ctxt.module_id);
        break;

      case F1AP_UE_CONTEXT_SETUP_RESP:
        if (NODE_IS_CU(RC.nrrrc[instance]->node_type)) {
          instance = get_instance_by_F1_instance(instance);
        }
        rrc_CU_process_ue_context_setup_response(msg_p, instance);
        break;

      case F1AP_UE_CONTEXT_MODIFICATION_RESP:
        if (NODE_IS_CU(RC.nrrrc[instance]->node_type)) {
          instance = get_instance_by_F1_instance(instance);
        }
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

      /* Messages from X2AP */
      case X2AP_ENDC_SGNB_ADDITION_REQ:
        LOG_I(NR_RRC, "Received ENDC sgNB addition request from X2AP \n");
        rrc_gNB_process_AdditionRequestInformation(instance, &X2AP_ENDC_SGNB_ADDITION_REQ(msg_p));
        break;

      case X2AP_ENDC_SGNB_RECONF_COMPLETE:
        LOG_A(NR_RRC, "Handling of reconfiguration complete message at RRC gNB is pending \n");
        break;

      case NGAP_INITIAL_CONTEXT_SETUP_REQ:
        if (NODE_IS_CU(RC.nrrrc[instance]->node_type)) {
          instance = get_instance_by_F1_instance(instance);
        }
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
  data->rrc->mac_rrc.ue_context_setup_request(0, data->setup_req);
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

  /* the callback will fill the UE context setup request and forward it */
  f1_ue_data_t ue_data = cu_get_f1_ue_data(ue_p->rrc_ue_id);
  f1ap_ue_context_setup_t ue_context_setup_req = {
    .gNB_CU_ue_id = ue_p->rrc_ue_id,
    .gNB_DU_ue_id = ue_data.secondary_ue,
    .mcc = rrc->configuration.mcc[0],
    .mnc = rrc->configuration.mnc[0],
    .mnc_digit_length = rrc->configuration.mnc_digit_length[0],
    .nr_cellid = rrc->nr_cellid,
    .servCellId = 0, /* TODO: correct value? */
    .srbs_to_be_setup = 0, /* no new SRBs */
    .drbs_to_be_setup = 0, /* no new DRBs */
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
  f1_ue_data_t ue_data = cu_get_f1_ue_data(UE->rrc_ue_id);
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
  struct NR_SIB1 *sib1 = RC.nrrrc[instance]->carrier.siblock1->message.choice.c1->choice.systemInformationBlockType1;

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
  length = do_NR_Paging (instance,
                         buffer,
                         tmsi);

  if (length == -1) {
    LOG_I(NR_RRC, "do_Paging error\n");
    return -1;
  }
  // TODO, send message to pdcp

  return 0;
}
