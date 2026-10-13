//------------------------------------------------------------------------
// SYMBIOSIS — symbiosis_cids.h
// Class IDs, parameter IDs and ranges, shared by processor, controller and editor.
//------------------------------------------------------------------------
#pragma once

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/ivstdataexchange.h"
#include "pluginterfaces/vst/vsttypes.h"

#include <algorithm>
#include <cmath>

namespace Symbiosis {

static const Steinberg::FUID kSymbiosisProcessorUID (0x3AE38AC6, 0x342748CF, 0xA82FFDB9, 0xC2970BC7);
static const Steinberg::FUID kSymbiosisControllerUID (0xE5AE4309, 0x45674073, 0x9509E9EB, 0x12266675);

#define SymbiosisVST3Category "Fx|Dynamics|Spectral"

//------------------------------------------------------------------------
enum ParamIds : Steinberg::Vst::ParamID
{
	kParamPullId = 0,
	kParamHarmonicsId,
	kParamInertiaId,
	kParamReactionId,
	kParamTrackingId,
	kParamTransShieldId,
	kParamPhaseLinkId,
	kParamDeltaSoloId,
	kParamTargetModeId,
	kParamOversamplingId,
	kParamBypassId,

	kNumParams
};

/** Continuous parameters are linear RangeParameters; lists are StringListParameters. */
struct ParamSpec
{
	double minPlain;
	double maxPlain;
	double defaultPlain;
	int numSteps; // 0 = continuous, otherwise number of list entries - 1
};

inline constexpr ParamSpec kParamSpecs[kNumParams] = {
    {0.0, 1.0, 0.68, 0},    // Pull
    {0.0, 1.0, 0.42, 0},    // Harmonics
    {1.0, 150.0, 35.0, 0},  // Inertia (ms)
    {5.0, 500.0, 80.0, 0},  // Reaction (ms)
    {0.0, 1.0, 0.75, 0},    // Tracking
    {0.0, 1.0, 0.20, 0},    // Trans-Shield
    {0.0, 1.0, 1.0, 1},     // Phase Link: Off, On
    {0.0, 1.0, 0.0, 1},     // Delta Solo: Off, On
    {0.0, 1.0, 0.0, 1},     // Target Mode: Exact, Harmonic
    {0.0, 2.0, 2.0, 2},     // Oversampling: Off, 2x, 4x
    {0.0, 1.0, 0.0, 1},     // Bypass
};

inline double toPlain (Steinberg::Vst::ParamID id, double normalized)
{
	const ParamSpec& s = kParamSpecs[id];
	normalized = std::clamp (normalized, 0.0, 1.0);
	if (s.numSteps > 0)
		return std::min<double> (s.numSteps, std::floor (normalized * (s.numSteps + 1)));
	return s.minPlain + normalized * (s.maxPlain - s.minPlain);
}

inline double toNormalized (Steinberg::Vst::ParamID id, double plain)
{
	const ParamSpec& s = kParamSpecs[id];
	if (s.numSteps > 0)
		return std::clamp (plain / s.numSteps, 0.0, 1.0);
	return std::clamp ((plain - s.minPlain) / (s.maxPlain - s.minPlain), 0.0, 1.0);
}

inline double defaultNormalized (Steinberg::Vst::ParamID id)
{
	return toNormalized (id, kParamSpecs[id].defaultPlain);
}

/** Factory presets (SYSTEME menu). Bypass and Delta Solo are never touched by a preset. */
struct FactoryPreset
{
	const char* name;
	double plain[kNumParams]; // plain values, in ParamIds order
};

inline constexpr FactoryPreset kFactoryPresets[] = {
    //            Pull  Harm  Inert React Track Shield PLink Delta Target OS  Bypass
    {"DEFAULT",    {0.68, 0.42, 35.0, 80.0,  0.75, 0.20,  1.0,  0.0,  0.0,   2.0, 0.0}},
    {"KICK / 808", {0.80, 0.60, 10.0, 120.0, 0.70, 0.35,  1.0,  0.0,  1.0,   2.0, 0.0}},
    {"VOCAL / BEAT", {0.55, 0.45, 25.0, 150.0, 0.85, 0.15, 1.0, 0.0,  0.0,   2.0, 0.0}},
    {"GENTLE",     {0.35, 0.30, 60.0, 250.0, 0.75, 0.20,  1.0,  0.0,  0.0,   1.0, 0.0}},
    {"INTENSE",    {0.95, 0.80, 5.0,  60.0,  0.60, 0.40,  1.0,  0.0,  1.0,   2.0, 0.0}},
};

inline constexpr bool isPresetParam (Steinberg::Vst::ParamID id)
{
	return id != kParamDeltaSoloId && id != kParamBypassId;
}

/** Data-exchange queue carrying the Orbit HUD frames (processor -> controller). */
inline constexpr Steinberg::Vst::DataExchangeUserContextID kHudExchangeContextId = 0x5359;

/** State chunk version written by the processor. */
inline constexpr Steinberg::int32 kStateVersion = 1;

} // namespace Symbiosis
