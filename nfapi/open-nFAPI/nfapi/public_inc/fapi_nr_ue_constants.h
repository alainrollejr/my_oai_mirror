#ifndef _FAPI_NR_UE_CONSTANTS_H_
#define _FAPI_NR_UE_CONSTANTS_H_

//  constants defined by specification 38.331
#define FAPI_NR_MAX_NUM_DL_ALLOCATIONS             16
#define FAPI_NR_MAX_NUM_UL_ALLOCATIONS             16
#define FAPI_NR_MAX_NUM_SERVING_CELLS              32
#define FAPI_NR_MAX_NUM_ZP_CSI_RS_RESOURCE_PER_SET 16
#define FAPI_NR_MAX_NUM_CANDIDATE_BEAMS            16
#define FAPI_NR_MAX_RA_OCCASION_PER_CSIRS          64
// Constants Defined in 38.213
#define FAPI_NR_MAX_CORESET_PER_BWP                3
#define FAPI_NR_MAX_SS_PER_BWP                     10
#define FAPI_NR_MAX_SS                             FAPI_NR_MAX_SS_PER_BWP*NR_MAX_NUM_BWP


/// RX_IND
#define FAPI_NR_RX_PDU_TYPE_SSB 0x01
#define FAPI_NR_RX_PDU_TYPE_SIB 0x02 
#define FAPI_NR_RX_PDU_TYPE_DLSCH 0x03 
#define FAPI_NR_DCI_IND 0x04
#define FAPI_NR_RX_PDU_TYPE_RAR 0x05
#define FAPI_NR_CSIRS_IND 0x06
#define FAPI_NR_RX_PDU_TYPE_PCH 0x07

#define FAPI_NR_SIBS_MASK_SIB1 0x1


/// DCI_IND
#define FAPI_NR_DCI_TYPE_0_0 0x01
#define FAPI_NR_DCI_TYPE_0_1 0x02
#define FAPI_NR_DCI_TYPE_1_0 0x03
#define FAPI_NR_DCI_TYPE_1_1 0x04
#define FAPI_NR_DCI_TYPE_2_0 0x05
#define FAPI_NR_DCI_TYPE_2_1 0x06
#define FAPI_NR_DCI_TYPE_2_2 0x07
#define FAPI_NR_DCI_TYPE_2_3 0x08


/// TX_REQ


/// DL_CONFIG_REQ

#define FAPI_NR_DL_CONFIG_LIST_NUM 10

#define FAPI_NR_DL_CONFIG_TYPE_DCI 0x01
#define FAPI_NR_DL_CONFIG_TYPE_DLSCH 0x02
#define FAPI_NR_DL_CONFIG_TYPE_RA_DLSCH 0x03
#define FAPI_NR_DL_CONFIG_TYPE_SI_DLSCH 0x04
#define FAPI_NR_DL_CONFIG_TYPE_P_DLSCH 0x05
#define FAPI_NR_DL_CONFIG_TYPE_CSI_RS 0x06
#define FAPI_NR_DL_CONFIG_TYPE_CSI_IM 0x07
#define FAPI_NR_CONFIG_TA_COMMAND 0x08
#define FAPI_NR_DL_CONFIG_TYPES 0x08

#define FAPI_NR_CCE_REG_MAPPING_TYPE_INTERLEAVED 0x01
#define FAPI_NR_CCE_REG_MAPPING_TYPE_NON_INTERLEAVED 0x02

#define PRECODER_GRANULARITY_SAME_AS_REG_BUNDLE 0x01
#define PRECODER_GRANULARITY_ALL_CONTIGUOUS_RBS 0x02

/// UL_CONFIG_REQ
#define FAPI_NR_UL_CONFIG_LIST_NUM 10

#define FAPI_NR_UL_CONFIG_TYPE_DONE  0x00
#define FAPI_NR_UL_CONFIG_TYPE_PRACH 0x01
#define FAPI_NR_UL_CONFIG_TYPE_PUCCH 0x02
#define FAPI_NR_UL_CONFIG_TYPE_PUSCH 0x03
#define FAPI_NR_UL_CONFIG_TYPE_SRS   0x04
#define FAPI_NR_UL_CONFIG_TYPES      0x04

#endif
