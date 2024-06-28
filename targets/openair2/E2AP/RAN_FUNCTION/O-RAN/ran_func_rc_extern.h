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

#ifndef RAN_FUNC_SM_RAN_CTRL_EXTERN_AGENT_H
#define RAN_FUNC_SM_RAN_CTRL_EXTERN_AGENT_H

#include "openair2/RRC/NR/nr_rrc_defs.h"

typedef enum { RC_SM_RRC_CONNECTED, RC_SM_RRC_INACTIVE, RC_SM_RRC_IDLE, RC_SM_RRC_ANY } rc_sm_rrc_state_e;
void signal_rrc_state_changed_to(const gNB_RRC_UE_t *rrc_ue_context, const rc_sm_rrc_state_e rrc_state);

typedef enum {
  RRC_SETUP_COMPLETE_MSG,
  XN_NG_HANDOVER_REQUEST,  // not supported in OAI
  F1_UE_CONTEXT_SETUP_REQUEST,

  END_EVENT_TRIGGER_MSG,
} message_type_e;

typedef enum{
  RRC_MEASUREMENT_REPORT,

  END_SUPPORTED_UL_DCCH_RRC_MSG_ID

} supported_ul_dcch_rrc_msg_id_e; 

typedef enum {
  Mobility_Management_7_3_3 = 3,
} call_process_breakpoint_e;


typedef enum {
  Handover_Preparation_7_3_3 = 1,
} call_breakpoint_e;
void signal_ue_id_to_ric(const gNB_RRC_UE_t *rrc_ue_context, const message_type_e type);

void signal_rrc_msg_to_ric(byte_array_t rrc_ba, supported_ul_dcch_rrc_msg_id_e type);

void create_indication_for_handover(uint64_t nr_cellid);

#endif
