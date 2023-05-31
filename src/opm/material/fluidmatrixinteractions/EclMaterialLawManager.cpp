// -*- mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
// vi: set et ts=4 sw=4 sts=4:
/*
  This file is part of the Open Porous Media project (OPM).

  OPM is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 2 of the License, or
  (at your option) any later version.

  OPM is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with OPM.  If not, see <http://www.gnu.org/licenses/>.

  Consult the COPYING file in the top-level source directory of this
  module for the precise wording of the license and the list of
  copyright holders.
*/

#include <config.h>
#include <opm/material/fluidmatrixinteractions/EclMaterialLawManager.hpp>

#include <opm/common/TimingMacros.hpp>
#include <opm/common/OpmLog/OpmLog.hpp>

#include <opm/input/eclipse/EclipseState/Grid/SatfuncPropertyInitializers.hpp>

#include <opm/material/fluidmatrixinteractions/EclEpsGridProperties.hpp>
#include <opm/material/fluidstates/SimpleModularFluidState.hpp>

#include <algorithm>

namespace Opm {

template<class TraitsT>
EclMaterialLawManager<TraitsT>::EclMaterialLawManager() = default;

template<class TraitsT>
EclMaterialLawManager<TraitsT>::~EclMaterialLawManager() = default;

template<class TraitsT>
void EclMaterialLawManager<TraitsT>::
initFromState(const EclipseState& eclState)
{
    // get the number of saturation regions and the number of cells in the deck
    const auto&  runspec       = eclState.runspec();
    const size_t numSatRegions = runspec.tabdims().getNumSatTables();

    const auto& ph = runspec.phases();
    this->hasGas = ph.active(Phase::GAS);
    this->hasOil = ph.active(Phase::OIL);
    this->hasWater = ph.active(Phase::WATER);

    readGlobalEpsOptions_(eclState);
    readGlobalHysteresisOptions_(eclState);
    readGlobalThreePhaseOptions_(runspec);

    // Read the end point scaling configuration (once per run).
    gasOilConfig_ = std::make_shared<EclEpsConfig>();
    oilWaterConfig_ = std::make_shared<EclEpsConfig>();
    gasWaterConfig_ = std::make_shared<EclEpsConfig>();
    gasOilConfig_->initFromState(eclState, EclTwoPhaseSystemType::GasOil);
    oilWaterConfig_->initFromState(eclState, EclTwoPhaseSystemType::OilWater);
    gasWaterConfig_->initFromState(eclState, EclTwoPhaseSystemType::GasWater);


    const auto& tables = eclState.getTableManager();

    {
        const auto& stone1exTables = tables.getStone1exTable();

        if (! stone1exTables.empty()) {
            stoneEtas_.clear();
            stoneEtas_.reserve(numSatRegions);

            for (const auto& table : stone1exTables) {
                stoneEtas_.push_back(table.eta);
            }
        }
    }

    this->unscaledEpsInfo_.resize(numSatRegions);

    if (this->hasGas + this->hasOil + this->hasWater == 1) {
        // Single-phase simulation.  Special case.  Nothing to do here.
        return;
    }

    // Multiphase simulation.  Common case.
    const auto tolcrit = runspec.saturationFunctionControls()
        .minimumRelpermMobilityThreshold();

    const auto rtep  = satfunc::getRawTableEndpoints(tables, ph, tolcrit);
    const auto rfunc = satfunc::getRawFunctionValues(tables, ph, rtep);

    for (unsigned satRegionIdx = 0; satRegionIdx < numSatRegions; ++satRegionIdx) {
        this->unscaledEpsInfo_[satRegionIdx]
            .extractUnscaled(rtep, rfunc, satRegionIdx);
    }
}

template<class TraitsT>
void EclMaterialLawManager<TraitsT>::
initParamsForElements(const EclipseState& eclState, size_t numCompressedElems)
{
    InitParams initParams {*this, eclState, numCompressedElems};
    initParams.run();
}

template<class TraitsT>
typename TraitsT::Scalar EclMaterialLawManager<TraitsT>::
applySwatinit(unsigned elemIdx,
              Scalar pcow,
              Scalar Sw)
{
    auto& elemScaledEpsInfo = oilWaterScaledEpsInfoDrainage_[elemIdx];

    // TODO: Mixed wettability systems - see ecl kw OPTIONS switch 74

    if (pcow < 0.0)
        Sw = elemScaledEpsInfo.Swu;
    else {

        if (Sw <= elemScaledEpsInfo.Swl)
            Sw = elemScaledEpsInfo.Swl;

        // specify a fluid state which only stores the saturations
        using FluidState = SimpleModularFluidState<Scalar,
                                                   numPhases,
                                                   /*numComponents=*/0,
                                                   /*FluidSystem=*/void, /* -> don't care */
                                                   /*storePressure=*/false,
                                                   /*storeTemperature=*/false,
                                                   /*storeComposition=*/false,
                                                   /*storeFugacity=*/false,
                                                   /*storeSaturation=*/true,
                                                   /*storeDensity=*/false,
                                                   /*storeViscosity=*/false,
                                                   /*storeEnthalpy=*/false>;
        FluidState fs;
        fs.setSaturation(waterPhaseIdx, Sw);
        fs.setSaturation(gasPhaseIdx, 0);
        fs.setSaturation(oilPhaseIdx, 0);
        std::array<Scalar, numPhases> pc = { 0 };
        MaterialLaw::capillaryPressures(pc, materialLawParams(elemIdx), fs);

        Scalar pcowAtSw = pc[oilPhaseIdx] - pc[waterPhaseIdx];
        constexpr const Scalar pcowAtSwThreshold = 1.0; //Pascal
        // avoid divison by very small number
        if (std::abs(pcowAtSw) > pcowAtSwThreshold) {
            elemScaledEpsInfo.maxPcow *= pcow/pcowAtSw;
            auto& elemEclEpsScalingPoints = oilWaterScaledEpsPointsDrainage(elemIdx);
            elemEclEpsScalingPoints.init(elemScaledEpsInfo,
                                         *oilWaterEclEpsConfig_,
                                         EclTwoPhaseSystemType::OilWater);
        }
    }

    return Sw;
}

template<class TraitsT>
const typename EclMaterialLawManager<TraitsT>::MaterialLawParams&
EclMaterialLawManager<TraitsT>::
connectionMaterialLawParams(unsigned satRegionIdx, unsigned elemIdx) const
{
    MaterialLawParams& mlp = const_cast<MaterialLawParams&>(materialLawParams_[elemIdx]);

    if (enableHysteresis())
        OpmLog::warning("Warning: Using non-default satnum regions for connection is not tested in combination with hysteresis");
    // Currently we don't support COMPIMP. I.e. use the same table lookup for the hysteresis curves.
    // unsigned impRegionIdx = satRegionIdx;

    // change the sat table it points to.
    switch (mlp.approach()) {
    case EclMultiplexerApproach::Stone1: {
        auto& realParams = mlp.template getRealParams<EclMultiplexerApproach::Stone1>();

        realParams.oilWaterParams().drainageParams().setUnscaledPoints(oilWaterUnscaledPointsVector_[satRegionIdx]);
        realParams.oilWaterParams().drainageParams().setEffectiveLawParams(oilWaterEffectiveParamVector_[satRegionIdx]);
        realParams.gasOilParams().drainageParams().setUnscaledPoints(gasOilUnscaledPointsVector_[satRegionIdx]);
        realParams.gasOilParams().drainageParams().setEffectiveLawParams(gasOilEffectiveParamVector_[satRegionIdx]);
//            if (enableHysteresis()) {
//                realParams.oilWaterParams().imbibitionParams().setUnscaledPoints(oilWaterUnscaledPointsVector_[impRegionIdx]);
//                realParams.oilWaterParams().imbibitionParams().setEffectiveLawParams(oilWaterEffectiveParamVector_[impRegionIdx]);
//                realParams.gasOilParams().imbibitionParams().setUnscaledPoints(gasOilUnscaledPointsVector_[impRegionIdx]);
//                realParams.gasOilParams().imbibitionParams().setEffectiveLawParams(gasOilEffectiveParamVector_[impRegionIdx]);
//            }
    }
        break;

    case EclMultiplexerApproach::Stone2: {
        auto& realParams = mlp.template getRealParams<EclMultiplexerApproach::Stone2>();
        realParams.oilWaterParams().drainageParams().setUnscaledPoints(oilWaterUnscaledPointsVector_[satRegionIdx]);
        realParams.oilWaterParams().drainageParams().setEffectiveLawParams(oilWaterEffectiveParamVector_[satRegionIdx]);
        realParams.gasOilParams().drainageParams().setUnscaledPoints(gasOilUnscaledPointsVector_[satRegionIdx]);
        realParams.gasOilParams().drainageParams().setEffectiveLawParams(gasOilEffectiveParamVector_[satRegionIdx]);
//            if (enableHysteresis()) {
//                realParams.oilWaterParams().imbibitionParams().setUnscaledPoints(oilWaterUnscaledPointsVector_[impRegionIdx]);
//                realParams.oilWaterParams().imbibitionParams().setEffectiveLawParams(oilWaterEffectiveParamVector_[impRegionIdx]);
//                realParams.gasOilParams().imbibitionParams().setUnscaledPoints(gasOilUnscaledPointsVector_[impRegionIdx]);
//                realParams.gasOilParams().imbibitionParams().setEffectiveLawParams(gasOilEffectiveParamVector_[impRegionIdx]);
//            }
    }
        break;

    case EclMultiplexerApproach::Default: {
        auto& realParams = mlp.template getRealParams<EclMultiplexerApproach::Default>();
        realParams.oilWaterParams().drainageParams().setUnscaledPoints(oilWaterUnscaledPointsVector_[satRegionIdx]);
        realParams.oilWaterParams().drainageParams().setEffectiveLawParams(oilWaterEffectiveParamVector_[satRegionIdx]);
        realParams.gasOilParams().drainageParams().setUnscaledPoints(gasOilUnscaledPointsVector_[satRegionIdx]);
        realParams.gasOilParams().drainageParams().setEffectiveLawParams(gasOilEffectiveParamVector_[satRegionIdx]);
//            if (enableHysteresis()) {
//                realParams.oilWaterParams().imbibitionParams().setUnscaledPoints(oilWaterUnscaledPointsVector_[impRegionIdx]);
//                realParams.oilWaterParams().imbibitionParams().setEffectiveLawParams(oilWaterEffectiveParamVector_[impRegionIdx]);
//                realParams.gasOilParams().imbibitionParams().setUnscaledPoints(gasOilUnscaledPointsVector_[impRegionIdx]);
//                realParams.gasOilParams().imbibitionParams().setEffectiveLawParams(gasOilEffectiveParamVector_[impRegionIdx]);
//            }
    }
        break;

    case EclMultiplexerApproach::TwoPhase: {
        auto& realParams = mlp.template getRealParams<EclMultiplexerApproach::TwoPhase>();
        realParams.oilWaterParams().drainageParams().setUnscaledPoints(oilWaterUnscaledPointsVector_[satRegionIdx]);
        realParams.oilWaterParams().drainageParams().setEffectiveLawParams(oilWaterEffectiveParamVector_[satRegionIdx]);
        realParams.gasOilParams().drainageParams().setUnscaledPoints(gasOilUnscaledPointsVector_[satRegionIdx]);
        realParams.gasOilParams().drainageParams().setEffectiveLawParams(gasOilEffectiveParamVector_[satRegionIdx]);
//            if (enableHysteresis()) {
//                realParams.oilWaterParams().imbibitionParams().setUnscaledPoints(oilWaterUnscaledPointsVector_[impRegionIdx]);
//                realParams.oilWaterParams().imbibitionParams().setEffectiveLawParams(oilWaterEffectiveParamVector_[impRegionIdx]);
//                realParams.gasOilParams().imbibitionParams().setUnscaledPoints(gasOilUnscaledPointsVector_[impRegionIdx]);
//                realParams.gasOilParams().imbibitionParams().setEffectiveLawParams(gasOilEffectiveParamVector_[impRegionIdx]);
//            }
    }
        break;

    default:
        throw std::logic_error("Enum value for material approach unknown!");
    }

    return mlp;
}

template<class TraitsT>
int EclMaterialLawManager<TraitsT>::
getKrnumSatIdx(unsigned elemIdx, FaceDir::DirEnum facedir) const
{
    using Dir = FaceDir::DirEnum;
    const std::vector<int>* array = nullptr;
    switch(facedir) {
    case Dir::XPlus:
      array = &krnumXArray_;
      break;
    case Dir::YPlus:
      array = &krnumYArray_;
      break;
    case Dir::ZPlus:
      array = &krnumZArray_;
      break;
    default:
      throw std::runtime_error("Unknown face direction");
    }
    if (array->size() > 0) {
      return (*array)[elemIdx];
    }
    else {
      return satnumRegionArray_[elemIdx];
    }
}

template<class TraitsT>
void EclMaterialLawManager<TraitsT>::
oilWaterHysteresisParams(Scalar& pcSwMdc,
                         Scalar& krnSwMdc,
                         unsigned elemIdx) const
{
    if (!enableHysteresis())
        throw std::runtime_error("Cannot get hysteresis parameters if hysteresis not enabled.");

    const auto& params = materialLawParams(elemIdx);
    MaterialLaw::oilWaterHysteresisParams(pcSwMdc, krnSwMdc, params);
}

template<class TraitsT>
void EclMaterialLawManager<TraitsT>::
setOilWaterHysteresisParams(const Scalar& pcSwMdc,
                            const Scalar& krnSwMdc,
                            unsigned elemIdx)
{
    if (!enableHysteresis())
        throw std::runtime_error("Cannot set hysteresis parameters if hysteresis not enabled.");

    auto& params = materialLawParams(elemIdx);
    MaterialLaw::setOilWaterHysteresisParams(pcSwMdc, krnSwMdc, params);
}

template<class TraitsT>
void EclMaterialLawManager<TraitsT>::
gasOilHysteresisParams(Scalar& pcSwMdc,
                       Scalar& krnSwMdc,
                       unsigned elemIdx) const
{
    if (!enableHysteresis())
        throw std::runtime_error("Cannot get hysteresis parameters if hysteresis not enabled.");

    const auto& params = materialLawParams(elemIdx);
    MaterialLaw::gasOilHysteresisParams(pcSwMdc, krnSwMdc, params);
}

template<class TraitsT>
void EclMaterialLawManager<TraitsT>::
setGasOilHysteresisParams(const Scalar& pcSwMdc,
                          const Scalar& krnSwMdc,
                          unsigned elemIdx)
{
    if (!enableHysteresis())
        throw std::runtime_error("Cannot set hysteresis parameters if hysteresis not enabled.");

    auto& params = materialLawParams(elemIdx);
    MaterialLaw::setGasOilHysteresisParams(pcSwMdc, krnSwMdc, params);
}

template<class TraitsT>
EclEpsScalingPoints<typename TraitsT::Scalar>&
EclMaterialLawManager<TraitsT>::
oilWaterScaledEpsPointsDrainage(unsigned elemIdx)
{
    auto& materialParams = materialLawParams_[elemIdx];
    switch (materialParams.approach()) {
    case EclMultiplexerApproach::Stone1: {
        auto& realParams = materialParams.template getRealParams<EclMultiplexerApproach::Stone1>();
        return realParams.oilWaterParams().drainageParams().scaledPoints();
    }

    case EclMultiplexerApproach::Stone2: {
        auto& realParams = materialParams.template getRealParams<EclMultiplexerApproach::Stone2>();
        return realParams.oilWaterParams().drainageParams().scaledPoints();
    }

    case EclMultiplexerApproach::Default: {
        auto& realParams = materialParams.template getRealParams<EclMultiplexerApproach::Default>();
        return realParams.oilWaterParams().drainageParams().scaledPoints();
    }

    case EclMultiplexerApproach::TwoPhase: {
        auto& realParams = materialParams.template getRealParams<EclMultiplexerApproach::TwoPhase>();
        return realParams.oilWaterParams().drainageParams().scaledPoints();
    }
    default:
        throw std::logic_error("Enum value for material approach unknown!");
    }
}

template<class TraitsT>
const typename EclMaterialLawManager<TraitsT>::MaterialLawParams& EclMaterialLawManager<TraitsT>::
materialLawParamsFunc_(unsigned elemIdx, FaceDir::DirEnum facedir) const
{
    using Dir = FaceDir::DirEnum;
    if (dirMaterialLawParams_) {
        switch(facedir) {
            case Dir::XPlus:
                return dirMaterialLawParams_->materialLawParamsX_[elemIdx];
            case Dir::YPlus:
                return dirMaterialLawParams_->materialLawParamsY_[elemIdx];
            case Dir::ZPlus:
                return dirMaterialLawParams_->materialLawParamsZ_[elemIdx];
            default:
                throw std::runtime_error("Unexpected face direction");
        }
    }
    else {
        return materialLawParams_[elemIdx];
    }
}

template<class TraitsT>
void EclMaterialLawManager<TraitsT>::
readGlobalEpsOptions_(const EclipseState& eclState)
{
    oilWaterEclEpsConfig_ = std::make_shared<EclEpsConfig>();
    oilWaterEclEpsConfig_->initFromState(eclState, EclTwoPhaseSystemType::OilWater);

    enableEndPointScaling_ = eclState.getTableManager().hasTables("ENKRVD");
}

template<class TraitsT>
void EclMaterialLawManager<TraitsT>::
readGlobalHysteresisOptions_(const EclipseState& state)
{
    hysteresisConfig_ = std::make_shared<EclHysteresisConfig>();
    hysteresisConfig_->initFromState(state.runspec());
}

template<class TraitsT>
void EclMaterialLawManager<TraitsT>::
readGlobalThreePhaseOptions_(const Runspec& runspec)
{
    bool gasEnabled = runspec.phases().active(Phase::GAS);
    bool oilEnabled = runspec.phases().active(Phase::OIL);
    bool waterEnabled = runspec.phases().active(Phase::WATER);

    int numEnabled =
        (gasEnabled?1:0)
        + (oilEnabled?1:0)
        + (waterEnabled?1:0);

    if (numEnabled == 0) {
        throw std::runtime_error("At least one fluid phase must be enabled. (Is: "+std::to_string(numEnabled)+")");
    } else if (numEnabled == 1) {
        threePhaseApproach_ = EclMultiplexerApproach::OnePhase;
    } else if ( numEnabled == 2) {
        threePhaseApproach_ = EclMultiplexerApproach::TwoPhase;
        if (!gasEnabled)
            twoPhaseApproach_ = EclTwoPhaseApproach::OilWater;
        else if (!oilEnabled)
            twoPhaseApproach_ = EclTwoPhaseApproach::GasWater;
        else if (!waterEnabled)
            twoPhaseApproach_ = EclTwoPhaseApproach::GasOil;
    }
    else {
        assert(numEnabled == 3);

        threePhaseApproach_ = EclMultiplexerApproach::Default;
        const auto& satctrls = runspec.saturationFunctionControls();
        if (satctrls.krModel() == SatFuncControls::ThreePhaseOilKrModel::Stone2)
            threePhaseApproach_ = EclMultiplexerApproach::Stone2;
        else if (satctrls.krModel() == SatFuncControls::ThreePhaseOilKrModel::Stone1)
            threePhaseApproach_ = EclMultiplexerApproach::Stone1;
    }
}


template class EclMaterialLawManager<ThreePhaseMaterialTraits<double,0,1,2>>;
template class EclMaterialLawManager<ThreePhaseMaterialTraits<float,0,1,2>>;

} // namespace Opm
