#include "public.sdk/source/main/pluginfactory.h"

#include "controller.h"
#include "metadata.h"
#include "processor.h"

#define stringSubCategory "Fx|Tools"

BEGIN_FACTORY_DEF(stringCompanyName, stringCompanyWeb, stringCompanyEmail)

DEF_CLASS2(INLINE_UID_FROM_FUID(audio_bridge::vst::kProcessorUID),
           PClassInfo::kManyInstances, kVstAudioEffectClass, stringPluginName,
           Vst::kDistributable, stringSubCategory, FULL_VERSION_STR,
           kVstVersionString, audio_bridge::vst::Processor::createInstance)

DEF_CLASS2(INLINE_UID_FROM_FUID(audio_bridge::vst::kControllerUID),
           PClassInfo::kManyInstances, kVstComponentControllerClass,
           stringPluginName " Controller", 0, "", FULL_VERSION_STR,
           kVstVersionString, audio_bridge::vst::Controller::createInstance)

END_FACTORY
