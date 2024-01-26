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

/*! \file nr_nas_msg_sim.h
 * \brief simulator for nr nas message
 * \author Yoshio INOUE, Masayuki HARADA
 * \email yoshio.inoue@fujitsu.com,masayuki.harada@fujitsu.com
 * \date 2020
 * \version 0.1
 */



#ifndef __NR_NAS_MSG_SIM_H__
#define __NR_NAS_MSG_SIM_H__

#include "RegistrationRequest.h"
#include "FGSIdentityResponse.h"
#include "FGSAuthenticationResponse.h"
#include "FGSNASSecurityModeComplete.h"
#include "FGSDeregistrationRequestUEOriginating.h"
#include "RegistrationComplete.h"
#include "as_message.h"
#include "FGSUplinkNasTransport.h"
#include <openair3/UICC/usim_interface.h>
#include "FGSServiceRequest.h"
#include "ActivateTestModeComplete.h"
#include "CloseUeTestLoopComplete.h"
#include "OpenUeTestLoopComplete.h"
#include "DeactivateTestModeComplete.h"

#define PLAIN_5GS_MSG                                      0b0000
#define INTEGRITY_PROTECTED                                0b0001
#define INTEGRITY_PROTECTED_AND_CIPHERED                   0b0010
#define INTEGRITY_PROTECTED_WITH_NEW_SECU_CTX              0b0011          // only for SECURITY MODE COMMAND
#define INTEGRITY_PROTECTED_AND_CIPHERED_WITH_NEW_SECU_CTX 0b0100         // only for SECURITY MODE COMPLETE

#define REGISTRATION_REQUEST                               0b01000001 /* 65 = 0x41 */
#define REGISTRATION_ACCEPT                                0b01000010 /* 66 = 0x42 */
#define REGISTRATION_COMPLETE                              0b01000011 /* 67 = 0x43 */
#define FGS_DEREGISTRATION_REQUEST_UE_ORIGINATING          0b01000101 /* 69 = 0x44 */
#define FGS_DEREGISTRATION_ACCEPT                          0b01000110 /* 70 = 0x46 */
#define FGS_SERVICE_REQUEST                                0b01001100 /* 76 = 0x4c */
#define FGS_AUTHENTICATION_REQUEST                         0b01010110 /* 86 = 0x56 */
#define FGS_AUTHENTICATION_RESPONSE                        0b01010111 /* 87 = 0x57 */
#define FGS_IDENTITY_REQUEST                               0b01011011 /* 91 = 0x5b */
#define FGS_IDENTITY_RESPONSE                              0b01011100 /* 92 = 0x5c */
#define FGS_SECURITY_MODE_COMMAND                          0b01011101 /* 93 = 0x5d */
#define FGS_SECURITY_MODE_COMPLETE                         0b01011110 /* 94 = 0x5e */
#define FGS_UPLINK_NAS_TRANSPORT                           0b01100111 /* 103= 0x67 */
#define FGS_DOWNLINK_NAS_TRANSPORT                         0b01101000 /* 104= 0x68 */

#define TEST_PD                                             0b00001111 /* skipIndicator=0000, protocolDiscriminator=1111 */
#define ACTIVATE_TEST_MODE                                  0b10000100 /* 132 = 0x84 */
#define ACTIVATE_TEST_MODE_COMPLETE                         0b10000101 /* 133 = 0x85 */
#define NR_CLOSE_UE_TEST_LOOP                               0b10000000 /* 128 = 0x80 */
#define CLOSE_UE_TEST_LOOP_COMPLETE                         0b10000001 /* 129 = 0x81 */
#define OPEN_UE_TEST_LOOP                                   0b10000010 /* 130 = 0x82 */
#define OPEN_UE_TEST_LOOP_COMPLETE                          0b10000011 /* 131 = 0x83 */
#define DEACTIVATE_TEST_MODE                                0b10000110 /* 134 = 0x86 */
#define DEACTIVATE_TEST_MODE_COMPLETE                       0b10000111 /* 135 = 0x87 */

// message type for 5GS session management
#define FGS_PDU_SESSION_ESTABLISHMENT_REQ                  0b11000001 /* 193= 0xc1 */
#define FGS_PDU_SESSION_ESTABLISHMENT_ACC                  0b11000010 /* 194= 0xc2 */

#define INITIAL_REGISTRATION                               0b001
#define MOBILITY_REGISTRATION_UPDATING                     0b010
#define PLAIN_5GS_NAS_MESSAGE_HEADER_LENGTH                3
#define SECURITY_PROTECTED_5GS_NAS_MESSAGE_HEADER_LENGTH   7
#define PAYLOAD_CONTAINER_LENGTH_MIN                       3
#define PAYLOAD_CONTAINER_LENGTH_MAX                       65537

#define NR_NAS_CIP_INT_KEY_LEN_BYTES                       (16)



#define NIA0_ALG_ID     EIA0_ALG_ID
// 3GPP TS 33.501: D.3.1.2 128-NIA1 128-NIA1 is identical to 128-EIA1 as specified in Annex B of TS 33.401
#define NIA1_128_ALG_ID EIA1_128_ALG_ID
// 3GPP TS 33.501: D.3.1.3 128-NIA2 128-NIA2 is identical to 128-EIA2 as specified in Annex B of TS 33.401
#define NIA2_128_ALG_ID EIA2_128_ALG_ID

#define NEA0_ALG_ID     EEA0_ALG_ID
// 3GPP TS 33.501: D.2.1.2 128-NEA1 128-NEA1 is identical to 128-EEA1 as specified in Annex B of TS 33.401
#define NEA1_128_ALG_ID EEA1_128_ALG_ID
// 3GPP TS 33.501: D.2.1.3 128-NEA2 128-NEA2 is identical to 128-EEA2 as specified in Annex B of TS 33.401
#define NEA2_128_ALG_ID EEA2_128_ALG_ID

/* Security Key for SA UE */
typedef struct {
  uint8_t mk[256]; // EAP-AKA' master-key.
                   // PRF' produces as many bits of output as is needed,
                   // lets define 256 bytes MK.
  uint8_t kausf[32];
  uint8_t kseaf[32];
  uint8_t kamf[32];
  uint8_t knas_int[NR_NAS_CIP_INT_KEY_LEN_BYTES];
  uint8_t knas_enc[NR_NAS_CIP_INT_KEY_LEN_BYTES];
  uint8_t res[16];
  uint8_t rand[16];
  uint8_t kgnb[32];
  uint32_t mm_counter;
  uint32_t sm_counter;
} ue_sa_security_key_t;

typedef struct {
  uicc_t *uicc;
  ue_sa_security_key_t security;
  Guti5GSMobileIdentity_t *guti;
} nr_ue_nas_t;

typedef enum fgs_protocol_discriminator_e {
  /* Protocol discriminator identifier for 5GS Mobility Management */
  FGS_MOBILITY_MANAGEMENT_MESSAGE =   0x7E,

  /* Protocol discriminator identifier for 5GS Session Management */
  FGS_SESSION_MANAGEMENT_MESSAGE =    0x2E,
} fgs_protocol_discriminator_t;

typedef struct {
  uint8_t ex_protocol_discriminator;
  uint8_t security_header_type;
  uint8_t message_type;
} mm_msg_header_t;

/* Structure of security protected header */
typedef struct {
  fgs_protocol_discriminator_t    protocol_discriminator;
  uint8_t                         security_header_type;
  uint32_t                        message_authentication_code;
  uint8_t                         sequence_number;
} fgs_nas_message_security_header_t;

typedef union {
  mm_msg_header_t                        header;
  registration_request_msg               registration_request;
  fgs_identiy_response_msg               fgs_identity_response;
  fgs_authentication_response_msg        fgs_auth_response;
  fgs_deregistration_request_ue_originating_msg fgs_deregistration_request_ue_originating;
  fgs_security_mode_complete_msg         fgs_security_mode_complete;
  registration_complete_msg              registration_complete;
  fgs_uplink_nas_transport_msg           uplink_nas_transport;
  activate_test_mode_complete_msg        activate_test_mode_complete;
  close_ue_test_loop_complete_msg        close_ue_test_loop_complete;
  open_ue_test_loop_complete_msg         open_ue_test_loop_complete;
  deactivate_test_mode_complete_msg      deactivate_test_mode_complete;
  fgs_service_request_msg                fgs_service_request;
} MM_msg;

typedef struct {
  MM_msg mm_msg;    /* 5GS Mobility Management messages */
} fgs_nas_message_plain_t;

typedef struct {
  fgs_nas_message_security_header_t header;
  fgs_nas_message_plain_t plain;
} fgs_nas_message_security_protected_t;

typedef union {
  fgs_nas_message_security_header_t header;
  fgs_nas_message_security_protected_t security_protected;
  fgs_nas_message_plain_t plain;
} fgs_nas_message_t;

typedef struct {
  union {
    mm_msg_header_t plain_nas_msg_header;
    struct security_protected_nas_msg_header_s {
      uint8_t  ex_protocol_discriminator;
      uint8_t  security_header_type;
      uint16_t message_authentication_code1;
      uint16_t message_authentication_code2;
      uint8_t  sequence_number;
    } security_protected_nas_msg_header_t;
  } choice;
} nas_msg_header_t;

typedef struct {
  uint8_t ex_protocol_discriminator;
  uint8_t pdu_session_id;
  uint8_t PTI;
  uint8_t message_type;
} fgs_sm_nas_msg_header_t;

typedef struct {
    mm_msg_header_t         plain_nas_msg_header;
    uint8_t                 payload_container_type;
    uint16_t                payload_container_length;
    fgs_sm_nas_msg_header_t sm_nas_msg_header;
} dl_nas_transport_t;

nr_ue_nas_t *get_ue_nas_info(module_id_t module_id);
void generateRegistrationRequest(as_nas_info_t *initialNasMsg, nr_ue_nas_t *nas);
void generateServiceRequest(as_nas_info_t *initialNasMsg, nr_ue_nas_t *nas);
void *nas_nrue_task(void *args_p);

/**
 * The helper function to recalculate the kgnb, where kgnb=f(kamf, ulNasCount).
 * With current implementation, NAS calulates and reports the kgnb value to RRC
 * once, but UE need this key refreshed after the RRC Release for subsequent
 * calculations of AS Security keys. Note, the NAS keeps track of UlNASCount.
 */
void updateKgNB(nr_ue_nas_t *nas, uint8_t *kgnb);

#endif /* __NR_NAS_MSG_SIM_H__*/
