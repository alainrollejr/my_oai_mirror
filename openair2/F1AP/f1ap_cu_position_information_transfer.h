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

/*! \file f1ap_cu_position_information_transfer.h
 * \brief F1AP tasks related to position information transfer , CU side
 * \author EURECOM
 * \date 2023
 * \version 0.1
 * \company Eurecom
 * \email: adeel.malik@eurecom.fr
 * \note
 * \warning
 */

#ifndef F1AP_CU_POSITION_INFORMATION_TRANSFER_H_
#define F1AP_CU_POSITION_INFORMATION_TRANSFER_H_

int CU_send_POSITIONING_INFORMATION_REQUEST(instance_t instance,
                                            f1ap_positioning_information_req_t *f1ap_positioning_information_req);

int CU_handle_POSITIONING_INFORMATION_RESPONSE(instance_t instance, sctp_assoc_t assoc_id, uint32_t stream, F1AP_F1AP_PDU_t *pdu);

int CU_handle_POSITIONING_INFORMATION_FAILURE(instance_t instance, sctp_assoc_t assoc_id, uint32_t stream, F1AP_F1AP_PDU_t *pdu);

#endif /* F1AP_CU_POSITION_INFORMATION_TRANSFER_H_ */
