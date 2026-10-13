//------------------------------------------------------------------------
// SYMBIOSIS — version.h
//------------------------------------------------------------------------
#pragma once

#include "pluginterfaces/base/fplatform.h"

#define SYMBIOSIS_STR_(x) #x
#define SYMBIOSIS_STR(x) SYMBIOSIS_STR_ (x)

#define MAJOR_VERSION_INT 1
#define SUB_VERSION_INT 0
#define RELEASE_NUMBER_INT 0
#define BUILD_NUMBER_INT 1

#define MAJOR_VERSION_STR SYMBIOSIS_STR (MAJOR_VERSION_INT)
#define SUB_VERSION_STR SYMBIOSIS_STR (SUB_VERSION_INT)
#define RELEASE_NUMBER_STR SYMBIOSIS_STR (RELEASE_NUMBER_INT)
#define BUILD_NUMBER_STR SYMBIOSIS_STR (BUILD_NUMBER_INT)

#define FULL_VERSION_STR MAJOR_VERSION_STR "." SUB_VERSION_STR "." RELEASE_NUMBER_STR "." BUILD_NUMBER_STR
#define VERSION_STR MAJOR_VERSION_STR "." SUB_VERSION_STR "." RELEASE_NUMBER_STR

#define stringPluginName "Symbiosis"
#define stringOriginalFilename "Symbiosis.vst3"
#if SMTG_PLATFORM_64
#define stringFileDescription "Symbiosis VST3 (64 bit)"
#else
#define stringFileDescription "Symbiosis VST3"
#endif
#define stringCompanyName "abrasion.dev"
#define stringCompanyWeb "https://abrasion.dev"
#define stringCompanyEmail ""
#define stringLegalCopyright "Copyright (c) 2026 abrasion.dev"
#define stringLegalTrademarks "VST is a trademark of Steinberg Media Technologies GmbH"
