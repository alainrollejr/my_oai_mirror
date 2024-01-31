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

#include "nr_pdcp_entity.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common/ran_context.h"
#include "nr_pdcp_security_nea1.h"
#include "nr_pdcp_security_nea2.h"
#include "nr_pdcp_integrity_nia2.h"
#include "nr_pdcp_integrity_nia1.h"
#include "nr_pdcp_sdu.h"
#include "ss_gNB_context.h"

#include "MAC/mac.h"  // for DCCH

#include "LOG/log.h"

#undef LOG_DUMPMSG
#define LOG_DUMPMSG(c, f, b, s, x...) do {uint8_t tmp_buf[4096]={0};        \
                    int tmp_count = snprintf(tmp_buf, 4095, x); \
                    tmp_count += snprintf(tmp_buf+tmp_count, 4095-tmp_count, "size %ld, '", s); \
                    for(int tmpi=0; tmpi<s; tmpi++) {tmp_count += snprintf(tmp_buf+tmp_count, 4095-tmp_count, "%02X", *(uint8_t*)(b + tmpi));} \
                    snprintf(tmp_buf+tmp_count, 4095-tmp_count, "'");\
                    LOG_I(c, "%s\n", tmp_buf); \
                  } while (0)

extern RAN_CONTEXT_t RC;

static void nr_pdcp_entity_recv_pdu(nr_pdcp_entity_t *entity,
                                    char *_buffer, int size)
{
  unsigned char    *buffer = (unsigned char *)_buffer;
  nr_pdcp_sdu_t    *sdu;
  int              rcvd_sn;
  uint32_t         rcvd_hfn;
  uint32_t         rcvd_count;
  int              header_size;
  int              integrity_size;
  int              rx_deliv_sn;
  uint32_t         rx_deliv_hfn;

  if (size < 1) {
    LOG_E(PDCP, "bad PDU received (size = %d)\n", size);
    return;
  }

  if (entity->type != NR_PDCP_SRB && !(buffer[0] & 0x80)) {
    LOG_E(PDCP, "%s:%d:%s: fatal\n", __FILE__, __LINE__, __FUNCTION__);
    /* TODO: This is something of a hack. The most significant bit
       in buffer[0] should be 1 if the packet is a data packet. We are
       processing malformed data packets if the most significant bit
       is 0. Rather than exit(1), this hack allows us to continue for now.
       We need to investigate why this hack is neccessary. */
    buffer[0] |= 128;
  }
  entity->stats.rxpdu_pkts++;
  entity->stats.rxpdu_bytes += size;


  if (entity->sn_size == 12) {
    rcvd_sn = ((buffer[0] & 0xf) <<  8) |
                buffer[1];
    header_size = 2;
  } else {
    rcvd_sn = ((buffer[0] & 0x3) << 16) |
               (buffer[1]        <<  8) |
                buffer[2];
    header_size = 3;
  }
  entity->stats.rxpdu_sn = rcvd_sn;

  /* SRBs always have MAC-I, even if integrity is not active */
  if (entity->has_integrity || entity->type == NR_PDCP_SRB) {
    integrity_size = 4;
  } else {
    integrity_size = 0;
  }

  if (size < header_size + integrity_size + 1) {
    LOG_E(PDCP, "bad PDU received (size = %d)\n", size);

    entity->stats.rxpdu_dd_pkts++;
    entity->stats.rxpdu_dd_bytes += size;

    return;
  }

  rx_deliv_sn  = entity->rx_deliv & entity->sn_max;
  rx_deliv_hfn = entity->rx_deliv >> entity->sn_size;

  if (rcvd_sn < rx_deliv_sn - entity->window_size) {
    rcvd_hfn = rx_deliv_hfn + 1;
  } else if (rcvd_sn >= rx_deliv_sn + entity->window_size) {
    rcvd_hfn = rx_deliv_hfn - 1;
  } else {
    rcvd_hfn = rx_deliv_hfn;
  }

  rcvd_count = (rcvd_hfn << entity->sn_size) | rcvd_sn;

  LOG_I(PDCP, "%s: Entity security status(%d): ciphering %d, integrity %d\n", __FUNCTION__, entity->has_ciphering,
        entity->has_ciphering?entity->ciphering_algorithm:-1, entity->has_integrity?entity->integrity_algorithm:-1);
  LOG_DUMPMSG(PDCP, DEBUG_PDCP, _buffer, size, "%s: RLC => PDCP PDU at %s:%d(rcvd_count=%zu rcvd_sn=%d): ",
              __FUNCTION__, entity->type == NR_PDCP_SRB ? "SRB":"DRB", entity->rb_id, rcvd_count, rcvd_sn);

  if (entity->has_ciphering)
  {
    // 3GPP TS 33.501 6.7.4, Security Mode Command is on SRB1
    if (entity->type == NR_PDCP_SRB && entity->rb_id == DCCH && entity->has_ciphering == NR_PDCP_ENTITY_CIPHERING_SMC) {
      LOG_I(PDCP, "%s: Skip deciphering during Security Mode Command\n", __FUNCTION__);
    } else {
      LOG_DUMPMSG(PDCP, DEBUG_PDCP, buffer+header_size, size-header_size, "%s: Deciphering rbid=%d count=%d dir=%d, buffer: ",
                  __FUNCTION__, entity->rb_id, rcvd_count, entity->is_gnb ? 0 : 1, size-header_size);
      LOG_DUMPMSG(PDCP, DEBUG_PDCP, entity->ciphering_key, 16, "%s: key: ", __FUNCTION__);

      entity->cipher(entity->security_context, buffer+header_size, size-header_size,
                      entity->rb_id, rcvd_count, entity->is_gnb ? 0 : 1);
      LOG_DUMPMSG(PDCP, DEBUG_PDCP, buffer+header_size, size-header_size, "%s: Deciphered: ", __FUNCTION__);
    }
  } else {
    LOG_I(PDCP, "%s: deciphering did not apply\n", __FUNCTION__);
  }

  if (entity->has_integrity)
  {
    LOG_DUMPMSG(PDCP, DEBUG_PDCP, buffer, size - integrity_size, "%s: Integrity check: rbid=%d count=%d dir=%d, buffer(%d): ",
                __FUNCTION__, entity->rb_id, rcvd_count, entity->is_gnb ? 0 : 1, size - integrity_size);
    LOG_DUMPMSG(PDCP, DEBUG_PDCP, entity->integrity_key, 16, "%s: integrity key: ", __FUNCTION__);

    unsigned char xmaci[4] = {0};
    unsigned char *const maci = buffer + size - integrity_size;

    entity->integrity(entity->integrity_context, xmaci, buffer, size - integrity_size,
                       entity->rb_id, rcvd_count, entity->is_gnb ? 0 : 1);
    LOG_DUMPMSG(PDCP, DEBUG_PDCP, entity->integrity_key, 16, "%s: integrity key: ", __FUNCTION__);
    LOG_DUMPMSG(PDCP, DEBUG_PDCP, maci,  4, "%s:  maci: ", __FUNCTION__);
    LOG_DUMPMSG(PDCP, DEBUG_PDCP, xmaci, 4, "%s: xmaci: ", __FUNCTION__);

    if (memcmp(xmaci, maci, 4) != 0) {
      LOG_E(PDCP, "%s: discard NR PDU, integrity failed\n", __FUNCTION__);
      entity->stats.rxpdu_dd_pkts++;
      entity->stats.rxpdu_dd_bytes += size;
      if (RC.ss.mode != SS_HWTMODEM) {
              exit(1);
      }
    }
  }

  if (rcvd_count < entity->rx_deliv
      || nr_pdcp_sdu_in_list(entity->rx_list, rcvd_count)) {
    LOG_W(PDCP, "discard NR PDU rcvd_count=%d, entity->rx_deliv %d,sdu_in_list %d\n", rcvd_count,entity->rx_deliv,nr_pdcp_sdu_in_list(entity->rx_list,rcvd_count));
    entity->stats.rxpdu_dd_pkts++;
    entity->stats.rxpdu_dd_bytes += size;

    return;
  }

  LOG_DUMPMSG(PDCP, DEBUG_PDCP, (char *)buffer + header_size, size - header_size - integrity_size, "%s: SDU PDCP => RRC: ", __FUNCTION__);
  sdu = nr_pdcp_new_sdu(rcvd_count,
                        (char *)buffer + header_size,
                        size - header_size - integrity_size);
  entity->rx_list = nr_pdcp_sdu_list_add(entity->rx_list, sdu);
  entity->rx_size += size-header_size;

  if (rcvd_count >= entity->rx_next) {
    entity->rx_next = rcvd_count + 1;
  }

  /* TODO(?): out of order delivery */

  if (rcvd_count == entity->rx_deliv) {
    /* deliver all SDUs starting from rx_deliv up to discontinuity or end of list */
    uint32_t count = entity->rx_deliv;
    while (entity->rx_list != NULL && count == entity->rx_list->count) {
      nr_pdcp_sdu_t *cur = entity->rx_list;
      entity->deliver_sdu(entity->deliver_sdu_data, entity,
                          cur->buffer, cur->size);
      entity->rx_list = cur->next;
      entity->rx_size -= cur->size;
      entity->stats.txsdu_pkts++;
      entity->stats.txsdu_bytes += cur->size;


      nr_pdcp_free_sdu(cur);
      count++;
    }
    entity->rx_deliv = count;
  }

  if (entity->t_reordering_start != 0 && entity->rx_deliv >= entity->rx_reord) {
    /* stop and reset t-Reordering */
    entity->t_reordering_start = 0;
  }

  if (entity->t_reordering_start == 0 && entity->rx_deliv < entity->rx_next) {
    entity->rx_reord = entity->rx_next;
    entity->t_reordering_start = entity->t_current;
  }
}

static int nr_pdcp_entity_process_sdu(nr_pdcp_entity_t *entity,
                                      char *buffer,
                                      int size,
                                      int sdu_id,
                                      char *pdu_buffer,
                                      int pdu_max_size)
{
  uint32_t count;
  int      sn;
  int      header_size;
  int      integrity_size;
  char    *buf = pdu_buffer;
  DevAssert(size + 3 + 4 <= pdu_max_size);
  int      dc_bit;
  entity->stats.rxsdu_pkts++;
  entity->stats.rxsdu_bytes += size;


  count = entity->tx_next;
  sn = entity->tx_next & entity->sn_max;

  LOG_DUMPMSG(PDCP, DEBUG_PDCP, buffer, size, "%s: RRC => PDCP: count=%zu, sn=%d: ", __FUNCTION__, count, sn);

  /* D/C bit is only to be set for DRBs */
  if (entity->type == NR_PDCP_DRB_AM || entity->type == NR_PDCP_DRB_UM) {
    dc_bit = 0x80;
  } else {
    dc_bit = 0;
  }

  if (entity->sn_size == 12) {
    buf[0] = dc_bit | ((sn >> 8) & 0xf);
    buf[1] = sn & 0xff;
    header_size = 2;
  } else {
    buf[0] = dc_bit | ((sn >> 16) & 0x3);
    buf[1] = (sn >> 8) & 0xff;
    buf[2] = sn & 0xff;
    header_size = 3;
  }

  /* SRBs always have MAC-I, even if integrity is not active */
  if (entity->has_integrity || entity->type == NR_PDCP_SRB) {
    integrity_size = 4;
  } else {
    integrity_size = 0;
  }

  memcpy(buf + header_size, buffer, size);

  LOG_I(PDCP, "Entity security status (%d): ciphering %d, integrity check %d\n", entity->has_ciphering,
        entity->has_ciphering?entity->ciphering_algorithm:-1, entity->has_integrity?entity->integrity_algorithm:-1);

  if (entity->has_integrity)
  {
    uint8_t integrity[4] = {0};
    LOG_DUMPMSG(PDCP, DEBUG_PDCP, buf, header_size + size, "%s: Integrity protection: rbid=%d count=%d dir=%d, buffer(%d): ",
                __FUNCTION__, entity->rb_id, count, entity->is_gnb ? 1 : 0, header_size + size);
    LOG_DUMPMSG(PDCP, DEBUG_PDCP, entity->integrity_key, 16, "%s: integrity key: ", __FUNCTION__);

    entity->integrity(entity->integrity_context,
                      integrity,
                      (unsigned char *)buf, header_size + size,
                      entity->rb_id, count, entity->is_gnb ? 1 : 0);
    LOG_DUMPMSG(PDCP, DEBUG_PDCP, integrity, 4, "%s: calculated MACI: ", __FUNCTION__);
    memcpy((unsigned char *)buf + header_size + size, integrity, 4);

  } else if (integrity_size == 4) {
   // set MAC-I to 0 for SRBs with integrity not active
    memset(buf + header_size + size, 0, 4);
  }

  if (entity->has_ciphering)
  {
     // 3GPP TS 33.501 6.7.4, Security Mode Command is on SRB1
     if (entity->type == NR_PDCP_SRB && entity->rb_id == DCCH && entity->has_ciphering == NR_PDCP_ENTITY_CIPHERING_SMC) {
      LOG_I(PDCP, "%s: Skip ciphering during Security Mode Command\n", __FUNCTION__);
      if (!entity->is_gnb && count > 0) {
        entity->has_ciphering = NR_PDCP_ENTITY_CIPHERING_ON;
      }
    } else {
      LOG_DUMPMSG(PDCP, DEBUG_PDCP, entity->ciphering_key, 16, "%s: Ciphering key: ", __FUNCTION__);
      LOG_DUMPMSG(PDCP, DEBUG_PDCP, (unsigned char *)buf + header_size, size + integrity_size,
             "%s: Ciphering: rbid=%d count=%d dir=%d, buffer(%d): ", __FUNCTION__, entity->rb_id, count, entity->is_gnb ? 1 : 0, size + integrity_size);

      entity->cipher(entity->security_context, (unsigned char *)buf + header_size,
                     size + integrity_size, entity->rb_id, count, entity->is_gnb ? 1 : 0);
      LOG_DUMPMSG(PDCP, DEBUG_PDCP, (unsigned char *)buf+header_size, size + integrity_size, "%s: Ciphered msg: ", __FUNCTION__);
    }
  }
  else
  {
    LOG_I(PDCP, "%s: Ciphering not applied\n", __FUNCTION__);
  }

  entity->tx_next++;

  entity->stats.txpdu_pkts++;
  entity->stats.txpdu_bytes += header_size + size + integrity_size;
  entity->stats.txpdu_sn = sn;
  
  LOG_DUMPMSG(PDCP, DEBUG_PDCP, (unsigned char *)pdu_buffer, header_size + size + integrity_size, "%s: PDU PDCP => RLC: ", __FUNCTION__);

  return header_size + size + integrity_size;
}

/* may be called several times, take care to clean previous settings */
static void nr_pdcp_entity_set_security(nr_pdcp_entity_t *entity,
                                        int integrity_algorithm,
                                        char *integrity_key,
                                        int ciphering_algorithm,
                                        char *ciphering_key)
{
  const size_t kKEY_LEN = 16;

  if (!entity) {
    LOG_E(PDCP, "%s: NULL entity, exiting\n", __FUNCTION__);
    return;
  }
  LOG_I(PDCP, "%s: Security algo for %s%d: Integrity %d, Ciphering %d\n", __FUNCTION__, entity->type == NR_PDCP_SRB ? "SRB":"DRB", entity->rb_id, integrity_algorithm, ciphering_algorithm);

  if (integrity_algorithm != -1) {
    entity->integrity_algorithm = integrity_algorithm;
  }
  if (ciphering_algorithm != -1) {
    entity->ciphering_algorithm = ciphering_algorithm;
  }

  if (integrity_key != NULL) {
    memcpy(entity->integrity_key, integrity_key, kKEY_LEN);
    LOG_DUMPMSG(PDCP, DEBUG_PDCP, entity->integrity_key, kKEY_LEN, "%s: Integrity key (algo %d) for %s%d: ",
                __FUNCTION__, integrity_algorithm, entity->type == NR_PDCP_SRB ? "SRB":"DRB", entity->rb_id);
  }
  if (ciphering_key != NULL) {
    memcpy(entity->ciphering_key, ciphering_key, kKEY_LEN);
    LOG_DUMPMSG(PDCP, DEBUG_PDCP, entity->ciphering_key, kKEY_LEN, "%s: Ciphering key (algo %d) for %s%d: ",
                __FUNCTION__, ciphering_algorithm, entity->type == NR_PDCP_SRB ? "SRB":"DRB", entity->rb_id);
  }

  if (integrity_algorithm == 0) {
    entity->has_integrity = 0;
    if (entity->free_integrity != NULL) {
      entity->free_integrity(entity->integrity_context);
      entity->free_integrity = NULL;
    }
  }

  if (integrity_algorithm != 0 && integrity_algorithm != -1) {
    entity->has_integrity = 1;
    if (entity->free_integrity != NULL) {
      entity->free_integrity(entity->integrity_context);
      entity->free_integrity = NULL;
    }
    if (integrity_algorithm == 2) {
      entity->integrity_context = nr_pdcp_integrity_nia2_init(entity->integrity_key);
      entity->integrity = nr_pdcp_integrity_nia2_integrity;
      entity->free_integrity = nr_pdcp_integrity_nia2_free_integrity;
    } else if (integrity_algorithm == 1) {
      entity->integrity_context = nr_pdcp_integrity_nia1_init(entity->integrity_key);
      entity->integrity = nr_pdcp_integrity_nia1_integrity;
      entity->free_integrity = nr_pdcp_integrity_nia1_free_integrity;
    } else {
      LOG_E(PDCP, "FATAL: only nia1 and nia2 supported for the moment\n");
      exit(1);
    }
  }

  if (ciphering_algorithm == 0) {
    entity->has_ciphering = NR_PDCP_ENTITY_CIPHERING_OFF;
    if (entity->free_security != NULL) {
      entity->free_security(entity->security_context);
      entity->free_security = NULL;
    }
  }

  if (ciphering_algorithm != 0 && ciphering_algorithm != -1) {
    if (entity->type == NR_PDCP_SRB && entity->rb_id == DCCH && entity->has_ciphering == NR_PDCP_ENTITY_CIPHERING_OFF) {
      entity->has_ciphering = NR_PDCP_ENTITY_CIPHERING_SMC;
    } else if (entity->type == NR_PDCP_SRB && entity->rb_id > DCCH || entity->type < NR_PDCP_SRB) {
      entity->has_ciphering = NR_PDCP_ENTITY_CIPHERING_ON;
    }
    LOG_I(PDCP, "%s: entity->has_ciphering %d\n", __FUNCTION__, entity->has_ciphering);
    if (entity->free_security != NULL) {
      entity->free_security(entity->security_context);
      entity->free_security = NULL;
    }
    if (ciphering_algorithm == 2) {
      entity->security_context = nr_pdcp_security_nea2_init(entity->ciphering_key);
      entity->cipher = nr_pdcp_security_nea2_cipher;
      entity->free_security = nr_pdcp_security_nea2_free_security;
    } else if (ciphering_algorithm == 1) {
      entity->security_context = nr_pdcp_security_nea1_init(entity->ciphering_key);
      entity->cipher = nr_pdcp_security_nea1_cipher;
      entity->free_security = nr_pdcp_security_nea1_free_security;
    } else {
      LOG_E(PDCP, "FATAL: only nea1 and nea2 supported for the moment\n");
      exit(1);
    }
  }
}

static void check_t_reordering(nr_pdcp_entity_t *entity)
{
  uint32_t count;

  /* if t_reordering is set to "infinity" (seen as -1) then do nothing */
  if (entity->t_reordering == -1)
    return;

  if (entity->t_reordering_start == 0
      || entity->t_current <= entity->t_reordering_start + entity->t_reordering)
    return;

  /* stop timer */
  entity->t_reordering_start = 0;

  /* deliver all SDUs with count < rx_reord */
  while (entity->rx_list != NULL && entity->rx_list->count < entity->rx_reord) {
    nr_pdcp_sdu_t *cur = entity->rx_list;
    entity->deliver_sdu(entity->deliver_sdu_data, entity,
                        cur->buffer, cur->size);
    entity->rx_list = cur->next;
    entity->rx_size -= cur->size;
    nr_pdcp_free_sdu(cur);
  }

  /* deliver all SDUs starting from rx_reord up to discontinuity or end of list */
  count = entity->rx_reord;
  while (entity->rx_list != NULL && count == entity->rx_list->count) {
    nr_pdcp_sdu_t *cur = entity->rx_list;
    entity->deliver_sdu(entity->deliver_sdu_data, entity,
                        cur->buffer, cur->size);
    entity->rx_list = cur->next;
    entity->rx_size -= cur->size;
    nr_pdcp_free_sdu(cur);
    count++;
  }

  entity->rx_deliv = count;

  if (entity->rx_deliv < entity->rx_next) {
    entity->rx_reord = entity->rx_next;
    entity->t_reordering_start = entity->t_current;
  }
}

void nr_pdcp_entity_set_time(struct nr_pdcp_entity_t *entity, uint64_t now)
{
  entity->t_current = now;

  check_t_reordering(entity);
}

static void deliver_all_sdus(nr_pdcp_entity_t *entity)
{
  // deliver the PDCP SDUs stored in the receiving PDCP entity to upper layers
  while (entity->rx_list != NULL) {
    nr_pdcp_sdu_t *cur = entity->rx_list;
    entity->deliver_sdu(entity->deliver_sdu_data, entity,
                        cur->buffer, cur->size);
    entity->rx_list = cur->next;
    entity->rx_size -= cur->size;
    entity->stats.txsdu_pkts++;
    entity->stats.txsdu_bytes += cur->size;
    nr_pdcp_free_sdu(cur);
  }
}

void nr_pdcp_entity_suspend(nr_pdcp_entity_t *entity)
{
  entity->tx_next = 0;
  if (entity->t_reordering_start != 0) {
    entity->t_reordering_start = 0;
    deliver_all_sdus(entity);
  }
  entity->rx_next = 0;
  entity->rx_deliv = 0;
}

void nr_pdcp_entity_release(nr_pdcp_entity_t *entity)
{
  deliver_all_sdus(entity);
}

void nr_pdcp_entity_delete(nr_pdcp_entity_t *entity)
{
  nr_pdcp_sdu_t *cur = entity->rx_list;
  while (cur != NULL) {
    nr_pdcp_sdu_t *next = cur->next;
    nr_pdcp_free_sdu(cur);
    cur = next;
  }
  if (entity->free_security != NULL)
    entity->free_security(entity->security_context);
  if (entity->free_integrity != NULL)
    entity->free_integrity(entity->integrity_context);
  free(entity);
}

static void nr_pdcp_entity_get_stats(nr_pdcp_entity_t *entity,
                                     nr_pdcp_statistics_t *out)
{
  *out = entity->stats;
}


nr_pdcp_entity_t *new_nr_pdcp_entity(
    nr_pdcp_entity_type_t type,
    int is_gnb,
    int rb_id,
    int pdusession_id,
    bool has_sdap_rx,
    bool has_sdap_tx,
    void (*deliver_sdu)(void *deliver_sdu_data, struct nr_pdcp_entity_t *entity,
                        char *buf, int size),
    void *deliver_sdu_data,
    void (*deliver_pdu)(void *deliver_pdu_data, ue_id_t ue_id, int rb_id,
                        char *buf, int size, int sdu_id),
    void *deliver_pdu_data,
    int sn_size,
    int t_reordering,
    int discard_timer,
    int ciphering_algorithm,
    int integrity_algorithm,
    unsigned char *ciphering_key,
    unsigned char *integrity_key)
{
  nr_pdcp_entity_t *ret;

  ret = calloc(1, sizeof(nr_pdcp_entity_t));
  if (ret == NULL) {
    LOG_E(PDCP, "%s:%d:%s: out of memory\n", __FILE__, __LINE__, __FUNCTION__);
    exit(1);
  }

  ret->type = type;

  ret->recv_pdu     = nr_pdcp_entity_recv_pdu;
  ret->process_sdu  = nr_pdcp_entity_process_sdu;
  ret->set_security = nr_pdcp_entity_set_security;
  ret->set_time     = nr_pdcp_entity_set_time;

  ret->delete_entity = nr_pdcp_entity_delete;
  ret->release_entity = nr_pdcp_entity_release;
  ret->suspend_entity = nr_pdcp_entity_suspend;
  
  ret->get_stats = nr_pdcp_entity_get_stats;
  ret->deliver_sdu = deliver_sdu;
  ret->deliver_sdu_data = deliver_sdu_data;

  ret->deliver_pdu = deliver_pdu;
  ret->deliver_pdu_data = deliver_pdu_data;

  ret->rb_id         = rb_id;
  ret->pdusession_id = pdusession_id;
  ret->has_sdap_rx   = has_sdap_rx;
  ret->has_sdap_tx   = has_sdap_tx;
  ret->sn_size       = sn_size;
  ret->t_reordering  = t_reordering;
  ret->discard_timer = discard_timer;

  ret->sn_max        = (1 << sn_size) - 1;
  ret->window_size   = 1 << (sn_size - 1);

  ret->is_gnb = is_gnb;

  nr_pdcp_entity_set_security(ret,
                              integrity_algorithm, (char *)integrity_key,
                              ciphering_algorithm, (char *)ciphering_key);

  return ret;
}
