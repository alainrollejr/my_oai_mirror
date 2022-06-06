/*
 * Copyright 2022 Sequans Communications.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include "SidlCompiler.h"
#include "SidlASN1.h"
#include "SidlASN1Base.h"
#include "SidlBase.h"
#include "SidlCommon.h"
#include "SidlCommonBase.h"
#include "SidlParts.h"
#include "SidlVals.h"
#include "TtcnCommon.h"
#include "SidlCommon_BcchConfig.h"
#include "SidlCommon_Bcch_BRConfig.h"
#include "SidlCommon_CcchDcchDtchConfig.h"
#include "SidlCommon_InitialCellPower.h"
#include "SidlCommon_PhysicalLayerConfigDL.h"
#include "SidlCommon_PhysicalLayerConfigUL.h"
#include "SidlCommon_RachProcedureConfig.h"
#include "SidlCommon_SciSchedulingConfig.h"
#include "SidlCommon_ServingCellConfig.h"
#include "SidlCommon_StaticCellInfo.h"
#include "SidlCommon_CellConfigRequest.h"

SIDL_BEGIN_C_INTERFACE

typedef Null_Type EUTRA_VngConfigConfirm_Type;

typedef int32_t NocLevel_Type;

struct EUTRA_VngConfigInfo_Type {
	Dl_Bandwidth_Type Bandwidth;
	NocLevel_Type NocLevel;
};

enum EUTRA_VngConfigRequest_Type_Sel {
	EUTRA_VngConfigRequest_Type_UNBOUND_VALUE = 0,
	EUTRA_VngConfigRequest_Type_Configure = 1,
	EUTRA_VngConfigRequest_Type_Activate = 2,
	EUTRA_VngConfigRequest_Type_Deactivate = 3,
};

union EUTRA_VngConfigRequest_Type_Value {
	struct EUTRA_VngConfigInfo_Type Configure;
	Null_Type Activate;
	Null_Type Deactivate;
};

struct EUTRA_VngConfigRequest_Type {
	enum EUTRA_VngConfigRequest_Type_Sel d;
	union EUTRA_VngConfigRequest_Type_Value v;
};

struct EUTRA_VNG_CTRL_REQ {
	struct ReqAspCommonPart_Type Common;
	struct EUTRA_VngConfigRequest_Type Request;
};

struct EUTRA_VNG_CTRL_CNF {
	struct CnfAspCommonPart_Type Common;
	EUTRA_VngConfigConfirm_Type Confirm;
};

SIDL_END_C_INTERFACE
