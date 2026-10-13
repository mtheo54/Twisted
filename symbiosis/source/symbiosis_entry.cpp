//------------------------------------------------------------------------
// SYMBIOSIS — symbiosis_entry.cpp
// Plug-in factory. The module entry points (GetPluginFactory, InitDll /
// bundleEntry / ModuleEntry, exported with SMTG_EXPORT_SYMBOL) come from the
// SDK's platform main, linked in by smtg_add_vst3plugin.
//------------------------------------------------------------------------
#include "symbiosis_cids.h"
#include "symbiosis_controller.h"
#include "symbiosis_processor.h"
#include "version.h"

#include "public.sdk/source/main/pluginfactory.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

BEGIN_FACTORY_DEF (stringCompanyName, stringCompanyWeb, stringCompanyEmail)

DEF_CLASS2 (INLINE_UID_FROM_FUID (Symbiosis::kSymbiosisProcessorUID),
            PClassInfo::kManyInstances,
            kVstAudioEffectClass,
            stringPluginName,
            Vst::kDistributable,
            SymbiosisVST3Category,
            FULL_VERSION_STR,
            kVstVersionString,
            Symbiosis::SymbiosisProcessor::createInstance)

DEF_CLASS2 (INLINE_UID_FROM_FUID (Symbiosis::kSymbiosisControllerUID),
            PClassInfo::kManyInstances,
            kVstComponentControllerClass,
            stringPluginName "Controller",
            0,
            "",
            FULL_VERSION_STR,
            kVstVersionString,
            Symbiosis::SymbiosisController::createInstance)

END_FACTORY
