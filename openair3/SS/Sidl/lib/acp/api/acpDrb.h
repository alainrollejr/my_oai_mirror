/*
 *****************************************************************
 *
 * Module  : ACP - Asynchronous Communication Protocol Generated Services Code
 *
 * Purpose : THIS FILE IS AUTOMATICALLY GENERATED !
 *
 *****************************************************************
 *
 *  Copyright (c) 2014-2021 SEQUANS Communications.
 *  All rights reserved.
 *
 *  This is confidential and proprietary source code of SEQUANS
 *  Communications. The use of the present source code and all
 *  its derived forms is exclusively governed by the restricted
 *  terms and conditions set forth in the SEQUANS
 *  Communications' EARLY ADOPTER AGREEMENT and/or LICENCE
 *  AGREEMENT. The present source code and all its derived
 *  forms can ONLY and EXCLUSIVELY be used with SEQUANS
 *  Communications' products. The distribution/sale of the
 *  present source code and all its derived forms is EXCLUSIVELY
 *  RESERVED to regular LICENCE holder and otherwise STRICTLY
 *  PROHIBITED.
 *
 *****************************************************************
 */

#pragma once

#include "SIDL_EUTRA_DRB_PORT.h"
#include "acp.h"

SIDL_BEGIN_C_INTERFACE

int acpDrbProcessFromSSEncClt(acpCtx_t _ctx, unsigned char* _buffer, size_t* _size, const struct DRB_COMMON_REQ* FromSS);

int acpDrbProcessFromSSDecSrv(acpCtx_t _ctx, const unsigned char* _buffer, size_t _size, struct DRB_COMMON_REQ** FromSS);

void acpDrbProcessFromSSFreeSrv(struct DRB_COMMON_REQ* FromSS);

int acpDrbProcessToSSEncSrv(acpCtx_t _ctx, unsigned char* _buffer, size_t* _size, const struct DRB_COMMON_IND* ToSS);

int acpDrbProcessToSSDecClt(acpCtx_t _ctx, const unsigned char* _buffer, size_t _size, struct DRB_COMMON_IND** ToSS);

void acpDrbProcessToSSFreeClt(struct DRB_COMMON_IND* ToSS);

SIDL_END_C_INTERFACE
