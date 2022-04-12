/*
 *****************************************************************
 *
 * Module : ACP Debug
 * Purpose: Debug logging
 *
 *****************************************************************
 *
 *  Copyright (c) 2019-2021 SEQUANS Communications.
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

// Internal includes
#include "adbgMsg.h"
#include "acpCtx.h"
#include "adbg.h"
#include "adbgMsgMap.h"

void adbgMsgLog(acpCtx_t ctx, enum adbgMsgLogDir dir, size_t size, const unsigned char* buffer)
{
	if (!acpCtxIsValid(ctx)) {
		SIDL_ASSERT(ctx != ctx);
		ACP_DEBUG_LOG("invalid context");
		return;
	}

	bool isServer = ACP_CTX_CAST(ctx)->isServer;

	enum acpMsgLocalId localId;

	if (acpGetMsgLocalId(size, buffer, &localId) < -1) {
		adbgPrintLog(ctx, "invalid buffer");
		adbgPrintLog(ctx, NULL);
		return;
	}

	if ((int)localId == (int)ACP_SERVICE_PUSH_TYPE) {
		adbgPrintLog(ctx, "ACP_SERVICE_PUSH_TYPE");
		adbgPrintLog(ctx, NULL);
		return;
	}

	// Service kind (0 - NTF, 1 - ONEWAY, 2 - CMD)
	int kind = acpCtxGetMsgKindFromId(localId);
	SIDL_ASSERT(kind != -1);
	if (kind < -1) {
		adbgPrintLog(ctx, "cannot find service");
		adbgPrintLog(ctx, NULL);
		return;
	}

	if ((kind == 1 || kind == 2) && dir == ADBG_MSG_LOG_RECV_DIR && isServer) {
		adbgMsgLogInArgs(ctx, localId, size - ACP_HEADER_SIZE, buffer + ACP_HEADER_SIZE);
	} else if ((kind == 0 || kind == 2) && dir == ADBG_MSG_LOG_SEND_DIR && isServer) {
		adbgMsgLogOutArgs(ctx, localId, size - ACP_HEADER_SIZE, buffer + ACP_HEADER_SIZE);
	} else if ((kind == 1 || kind == 2) && dir == ADBG_MSG_LOG_SEND_DIR && !isServer) {
		adbgMsgLogInArgs(ctx, localId, size - ACP_HEADER_SIZE, buffer + ACP_HEADER_SIZE);
	} else if ((kind == 0 || kind == 2) && dir == ADBG_MSG_LOG_RECV_DIR && !isServer) {
		adbgMsgLogOutArgs(ctx, localId, size - ACP_HEADER_SIZE, buffer + ACP_HEADER_SIZE);
	} else {
		SIDL_ASSERT(0);
	}
}
