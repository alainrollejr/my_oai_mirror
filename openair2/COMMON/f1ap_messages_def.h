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

/* F1AP -> SCTP */
MESSAGE_DEF(F1AP_CU_SCTP_REQ, MESSAGE_PRIORITY_MED, f1ap_cu_setup_req_t)

/* eNB_DU application layer -> F1AP messages or CU F1AP -> RRC*/
MESSAGE_DEF(F1AP_SETUP_REQ, MESSAGE_PRIORITY_MED, f1ap_setup_req_t)
MESSAGE_DEF(F1AP_GNB_CU_CONFIGURATION_UPDATE_ACKNOWLEDGE, MESSAGE_PRIORITY_MED, f1ap_gnb_cu_configuration_update_acknowledge_t)
MESSAGE_DEF(F1AP_GNB_CU_CONFIGURATION_UPDATE_FAILURE, MESSAGE_PRIORITY_MED, f1ap_gnb_cu_configuration_update_failure_t)

/* F1AP -> eNB_DU or eNB_CU_RRC -> F1AP application layer messages */
MESSAGE_DEF(F1AP_SETUP_RESP, MESSAGE_PRIORITY_MED, f1ap_setup_resp_t)
MESSAGE_DEF(F1AP_SETUP_FAILURE, MESSAGE_PRIORITY_MED, f1ap_setup_failure_t)
MESSAGE_DEF(F1AP_GNB_CU_CONFIGURATION_UPDATE, MESSAGE_PRIORITY_MED, f1ap_gnb_cu_configuration_update_t)

/* MAC -> F1AP messages */
MESSAGE_DEF(F1AP_INITIAL_UL_RRC_MESSAGE           , MESSAGE_PRIORITY_MED, f1ap_initial_ul_rrc_message_t)
MESSAGE_DEF(F1AP_UL_RRC_MESSAGE                , MESSAGE_PRIORITY_MED, f1ap_ul_rrc_message_t)
MESSAGE_DEF(F1AP_UE_CONTEXT_RELEASE_REQ, MESSAGE_PRIORITY_MED, f1ap_ue_context_release_req_t)
MESSAGE_DEF(F1AP_UE_CONTEXT_RELEASE_CMD, MESSAGE_PRIORITY_MED, f1ap_ue_context_release_cmd_t)
MESSAGE_DEF(F1AP_UE_CONTEXT_RELEASE_COMPLETE, MESSAGE_PRIORITY_MED, f1ap_ue_context_release_complete_t)

/* RRC -> F1AP  messages */
MESSAGE_DEF(F1AP_DL_RRC_MESSAGE              , MESSAGE_PRIORITY_MED, f1ap_dl_rrc_message_t)
MESSAGE_DEF(F1AP_UE_CONTEXT_SETUP_REQ,  MESSAGE_PRIORITY_MED, f1ap_ue_context_setup_t)
MESSAGE_DEF(F1AP_UE_CONTEXT_SETUP_RESP, MESSAGE_PRIORITY_MED, f1ap_ue_context_setup_t)
MESSAGE_DEF(F1AP_UE_CONTEXT_MODIFICATION_REQ, MESSAGE_PRIORITY_MED, f1ap_ue_context_modif_req_t)
MESSAGE_DEF(F1AP_UE_CONTEXT_MODIFICATION_RESP, MESSAGE_PRIORITY_MED, f1ap_ue_context_modif_resp_t)

/* CU -> DU*/
MESSAGE_DEF(F1AP_PAGING_IND, MESSAGE_PRIORITY_MED, f1ap_paging_ind_t)
