/*
 *****************************************************************
 *
 * Module : SIDL base - compiler support
 *
 * Purpose: Provide abstraction layers and feature defines to help
 *          using compiler specific features in a portable way.
 *
 *****************************************************************
 *
 *  Copyright (c) 2009-2021 SEQUANS Communications.
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

// WA for already defined None somewhere (which conflicts with SIDL code)
#ifdef None
#undef None /* in case IDL is compiled with OAI eNB None type is also used by OAI source code */
#endif

// WA for already define C_RNTI somewhere (which conflicts with SIDL code)
#ifdef C_RNTI
#undef C_RNTI /* in case IDL is compiled with OAI eNB None type is also used by OAI source code */
#endif

