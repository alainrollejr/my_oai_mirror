/*
 * Copyright 2022 Sequans Communications.
 *
 * Licensed to the OpenAirInterface (OAI) Software Alliance under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The OpenAirInterface Software Alliance licenses this file to You under
 * the OAI Public License, Version 1.0  (the "License"); you may not use this file
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
 * For more information about the OpenAirInterface (OAI) Software Alliance:
 *      contact@openairinterface.org
 */

#pragma once

#include "SidlCompiler.h"
#include "SidlASN1.h"
#include "SidlASN1Base.h"
#include "SidlBase.h"
#include "SidlCommon.h"
#include "SidlCommonBase.h"

SIDL_BEGIN_C_INTERFACE

typedef int16_t AbsoluteCellPower_Type;

typedef uint8_t AttenuationValue_Type;

enum Attenuation_Type_Sel {
	Attenuation_Type_UNBOUND_VALUE = 0,
	Attenuation_Type_Value = 1,
	Attenuation_Type_Off = 2,
};

union Attenuation_Type_Value {
	AttenuationValue_Type Value;
	Null_Type Off;
};

struct Attenuation_Type {
	enum Attenuation_Type_Sel d;
	union Attenuation_Type_Value v;
};

typedef struct Attenuation_Type InitialAttenuation_Type;

struct InitialCellPower_Type {
	AbsoluteCellPower_Type MaxReferencePower;
	InitialAttenuation_Type Attenuation;
};

SIDL_END_C_INTERFACE