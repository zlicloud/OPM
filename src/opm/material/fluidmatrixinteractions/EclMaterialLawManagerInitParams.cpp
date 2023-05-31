// -*- mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
// vi: set et ts=4 sw=4 sts=4:
/*
  Copyright 2022 Equinor ASA.

  This file is part of the Open Porous Media project (OPM).

  OPM is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or

  OPM is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with OPM.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <config.h>
#include <opm/material/fluidmatrixinteractions/EclMaterialLawManager.hpp>
#include <opm/material/fluidmatrixinteractions/EclEpsGridProperties.hpp>

namespace Opm {

/* constructors*/

template <class Traits>
EclMaterialLawManager<Traits>::InitParams::
InitParams(EclMaterialLawManager<Traits>& parent, const EclipseState& eclState, size_t numCompressedElems) :
    parent_{parent},
    eclState_{eclState},
    numCompressedElems_{numCompressedElems}
{
    // read end point scaling grid properties
    // TODO: these objects might require some memory, can this be simplified?
    if (this->parent_.enableHysteresis()) {
        this->epsImbGridProperties_
            = std::make_unique<EclEpsGridProperties>(this->eclState_, /*useImbibition=*/true);
    }
    this->epsGridProperties_
        = std::make_unique<EclEpsGridProperties>(this->eclState_, /*useImbibition=*/false);
}

/* public methods */

template <class Traits>
void
EclMaterialLawManager<Traits>::InitParams::
run() {
    readUnscaledEpsPointsVectors_();
    readEffectiveParameters_();
    initSatnumRegionArray_();
    copySatnumArrays_();
    initOilWaterScaledEpsInfo_();
    initMaterialLawParamVectors_();
    std::vector<std::vector<int>*> satnumArray;
    std::vector<std::vector<int>*> imbnumArray;
    std::vector<std::vector<MaterialLawParams>*> mlpArray;
    initArrays_(satnumArray, imbnumArray, mlpArray);
    auto num_arrays = mlpArray.size();
    for (unsigned i=0; i<num_arrays; i++) {
        for (unsigned elemIdx = 0; elemIdx < this->numCompressedElems_; ++elemIdx) {
            unsigned satRegionIdx = satRegion_(*satnumArray[i], elemIdx);
            //unsigned satNumCell = this->parent_.satnumRegionArray_[elemIdx];
            HystParams hystParams {*this};
            hystParams.setConfig();
            hystParams.setDrainageParamsOilGas(elemIdx, satRegionIdx);
            hystParams.setDrainageParamsOilWater(elemIdx, satRegionIdx);
            hystParams.setDrainageParamsGasWater(elemIdx, satRegionIdx);
            if (this->parent_.enableHysteresis()) {
                unsigned imbRegionIdx = imbRegion_(*imbnumArray[i], elemIdx);
                hystParams.setImbibitionParamsOilGas(elemIdx, imbRegionIdx);
                hystParams.setImbibitionParamsOilWater(elemIdx, imbRegionIdx);
                hystParams.setImbibitionParamsGasWater(elemIdx, imbRegionIdx);
            }
            hystParams.finalize();
            initThreePhaseParams_(hystParams, (*mlpArray[i])[elemIdx], satRegionIdx, elemIdx);
        }
    }
}

/* private methods alphabetically sorted*/

template <class Traits>
void
EclMaterialLawManager<Traits>::InitParams::
copySatnumArrays_()
{
    copyIntArray_(this->parent_.krnumXArray_, "KRNUMX");
    copyIntArray_(this->parent_.krnumYArray_, "KRNUMY");
    copyIntArray_(this->parent_.krnumZArray_, "KRNUMZ");
    copyIntArray_(this->parent_.imbnumXArray_, "IMBNUMX");
    copyIntArray_(this->parent_.imbnumYArray_, "IMBNUMY");
    copyIntArray_(this->parent_.imbnumZArray_, "IMBNUMZ");
    // create the information for the imbibition region (IMBNUM). By default this is
    // the same as the saturation region (SATNUM)
    this->parent_.imbnumRegionArray_ = this->parent_.satnumRegionArray_;
    copyIntArray_(this->parent_.imbnumRegionArray_, "IMBNUM");
    assert(this->numCompressedElems_ == this->parent_.satnumRegionArray_.size());
    assert(!this->parent_.enableHysteresis() || this->numCompressedElems_ == this->parent_.imbnumRegionArray_.size());
}

template <class Traits>
void
EclMaterialLawManager<Traits>::InitParams::
copyIntArray_(std::vector<int>& dest, const std::string keyword)
{
    if (this->eclState_.fieldProps().has_int(keyword)) {
        dest.resize(this->numCompressedElems_);
        const auto& satnumRawData = this->eclState_.fieldProps().get_int(keyword);
        for (unsigned elemIdx = 0; elemIdx < this->numCompressedElems_; ++elemIdx) {
            dest[elemIdx] = satnumRawData[elemIdx] - 1;
        }
    }
}

template <class Traits>
unsigned
EclMaterialLawManager<Traits>::InitParams::
imbRegion_(std::vector<int>& array, unsigned elemIdx)
{
    std::vector<int>& default_vec = this->parent_.imbnumRegionArray_;
    return satOrImbRegion_(array, default_vec, elemIdx);
}

template <class Traits>
void
EclMaterialLawManager<Traits>::InitParams::
initArrays_(
        std::vector<std::vector<int>*>& satnumArray,
        std::vector<std::vector<int>*>& imbnumArray, 
        std::vector<std::vector<MaterialLawParams>*>& mlpArray)
{
    satnumArray.push_back(&this->parent_.satnumRegionArray_);
    imbnumArray.push_back(&this->parent_.imbnumRegionArray_);
    mlpArray.push_back(&this->parent_.materialLawParams_);
    if (this->parent_.dirMaterialLawParams_) {
        if (this->parent_.hasDirectionalRelperms()) {
            satnumArray.push_back(&this->parent_.krnumXArray_);
            satnumArray.push_back(&this->parent_.krnumYArray_);
            satnumArray.push_back(&this->parent_.krnumZArray_);
        }
        if (this->parent_.hasDirectionalImbnum()) {
            imbnumArray.push_back(&this->parent_.imbnumXArray_);
            imbnumArray.push_back(&this->parent_.imbnumYArray_);
            imbnumArray.push_back(&this->parent_.imbnumZArray_);
        }
        mlpArray.push_back(&(this->parent_.dirMaterialLawParams_->materialLawParamsX_));
        mlpArray.push_back(&(this->parent_.dirMaterialLawParams_->materialLawParamsY_));
        mlpArray.push_back(&(this->parent_.dirMaterialLawParams_->materialLawParamsZ_));
    }
}

template <class Traits>
void
EclMaterialLawManager<Traits>::InitParams::
initMaterialLawParamVectors_()
{
    this->parent_.materialLawParams_.resize(this->numCompressedElems_);
    if (this->parent_.hasDirectionalImbnum() || this->parent_.hasDirectionalRelperms()) {
        this->parent_.dirMaterialLawParams_ 
            = std::make_unique<DirectionalMaterialLawParams<MaterialLawParams>>(this->numCompressedElems_);
    }
}

template <class Traits>
void
EclMaterialLawManager<Traits>::InitParams::
initOilWaterScaledEpsInfo_()
{
    // This vector will be updated in the hystParams.setDrainageOilWater() in the run() method
    this->parent_.oilWaterScaledEpsInfoDrainage_.resize(this->numCompressedElems_);
}

template <class Traits>
void
EclMaterialLawManager<Traits>::InitParams::
initSatnumRegionArray_()
{
    // copy the SATNUM grid property. in some cases this is not necessary, but it
    // should not require much memory anyway...
    auto &satnumArray = this->parent_.satnumRegionArray_;
    satnumArray.resize(this->numCompressedElems_);
    if (this->eclState_.fieldProps().has_int("SATNUM")) {
        const auto& satnumRawData = this->eclState_.fieldProps().get_int("SATNUM");
        for (unsigned elemIdx = 0; elemIdx < this->numCompressedElems_; ++elemIdx) {
            satnumArray[elemIdx] = satnumRawData[elemIdx] - 1;
        }
    }
    else {
        std::fill(satnumArray.begin(), satnumArray.end(), 0);
    }
}

template <class Traits>
void
EclMaterialLawManager<Traits>::InitParams::
initThreePhaseParams_(HystParams &hystParams,
                      MaterialLawParams& materialParams,
                      unsigned satRegionIdx,
                      unsigned elemIdx)
{
    const auto& epsInfo = this->parent_.oilWaterScaledEpsInfoDrainage_[elemIdx];

    auto oilWaterParams = hystParams.getOilWaterParams();
    auto gasOilParams = hystParams.getGasOilParams();
    auto gasWaterParams = hystParams.getGasWaterParams();
    materialParams.setApproach(this->parent_.threePhaseApproach_);
    switch (materialParams.approach()) {
        case EclMultiplexerApproach::Stone1: {
            auto& realParams = materialParams.template getRealParams<EclMultiplexerApproach::Stone1>();
            realParams.setGasOilParams(gasOilParams);
            realParams.setOilWaterParams(oilWaterParams);
            realParams.setSwl(epsInfo.Swl);

            if (!this->parent_.stoneEtas_.empty()) {
                realParams.setEta(this->parent_.stoneEtas_[satRegionIdx]);
            }
            else
                realParams.setEta(1.0);
            realParams.finalize();
            break;
        }

        case EclMultiplexerApproach::Stone2: {
            auto& realParams = materialParams.template getRealParams<EclMultiplexerApproach::Stone2>();
            realParams.setGasOilParams(gasOilParams);
            realParams.setOilWaterParams(oilWaterParams);
            realParams.setSwl(epsInfo.Swl);
            realParams.finalize();
            break;
        }

        case EclMultiplexerApproach::Default: {
            auto& realParams = materialParams.template getRealParams<EclMultiplexerApproach::Default>();
            realParams.setGasOilParams(gasOilParams);
            realParams.setOilWaterParams(oilWaterParams);
            realParams.setSwl(epsInfo.Swl);
            realParams.finalize();
            break;
        }

        case EclMultiplexerApproach::TwoPhase: {
            auto& realParams = materialParams.template getRealParams<EclMultiplexerApproach::TwoPhase>();
            realParams.setGasOilParams(gasOilParams);
            realParams.setOilWaterParams(oilWaterParams);
            realParams.setGasWaterParams(gasWaterParams);
            realParams.setApproach(this->parent_.twoPhaseApproach_);
            realParams.finalize();
            break;
        }

        case EclMultiplexerApproach::OnePhase: {
            // Nothing to do, no parameters.
            break;
        }
    } // end switch()
}

template <class Traits>
void
EclMaterialLawManager<Traits>::InitParams::
readEffectiveParameters_()
{
    ReadEffectiveParams effectiveReader {*this};
    // populates effective parameter vectors in the parent class (EclMaterialManager)
    effectiveReader.read();
}

template <class Traits>
void
EclMaterialLawManager<Traits>::InitParams::
readUnscaledEpsPointsVectors_()
{
    if (this->parent_.hasGas && this->parent_.hasOil) {
        readUnscaledEpsPoints_(
            this->parent_.gasOilUnscaledPointsVector_,
            this->parent_.gasOilConfig_,
            EclTwoPhaseSystemType::GasOil
        );
    }
    if (this->parent_.hasOil && this->parent_.hasWater) {
        readUnscaledEpsPoints_(
            this->parent_.oilWaterUnscaledPointsVector_,
            this->parent_.oilWaterConfig_,
            EclTwoPhaseSystemType::OilWater
        );
    }
    if (!this->parent_.hasOil) {
        readUnscaledEpsPoints_(
            this->parent_.gasWaterUnscaledPointsVector_,
            this->parent_.gasWaterConfig_,
            EclTwoPhaseSystemType::GasWater
        );
    }
}

template <class Traits>
template <class Container>
void
EclMaterialLawManager<Traits>::InitParams::
readUnscaledEpsPoints_(Container& dest, std::shared_ptr<EclEpsConfig> config, EclTwoPhaseSystemType system_type)
{
    const size_t numSatRegions = this->eclState_.runspec().tabdims().getNumSatTables();
    dest.resize(numSatRegions);
    for (unsigned satRegionIdx = 0; satRegionIdx < numSatRegions; ++satRegionIdx) {
        dest[satRegionIdx] = std::make_shared<EclEpsScalingPoints<Scalar> >();
        dest[satRegionIdx]->init(this->parent_.unscaledEpsInfo_[satRegionIdx], *config, system_type);
    }
}

template <class Traits>
unsigned
EclMaterialLawManager<Traits>::InitParams::
satRegion_(std::vector<int>& array, unsigned elemIdx)
{
    std::vector<int>& default_vec = this->parent_.satnumRegionArray_;
    return satOrImbRegion_(array, default_vec, elemIdx);
}

template <class Traits>
unsigned
EclMaterialLawManager<Traits>::InitParams::
satOrImbRegion_(std::vector<int>& array, std::vector<int>& default_vec, unsigned elemIdx)
{
    int value;
    if (array.size() > 0) {
        value = array[elemIdx];
    }
    else { // use default value
        value = default_vec[elemIdx];
    }
    return static_cast<unsigned>(value);
}

// Make some actual code, by realizing the previously defined templated class
template class EclMaterialLawManager<ThreePhaseMaterialTraits<double,0,1,2>>::InitParams;
template class EclMaterialLawManager<ThreePhaseMaterialTraits<float,0,1,2>>::InitParams;

} // namespace Opm
