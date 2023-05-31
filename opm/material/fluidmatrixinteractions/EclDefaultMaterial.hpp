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
/*!
 * \file
 * \copydoc Opm::EclDefaultMaterial
 */
#ifndef OPM_ECL_DEFAULT_MATERIAL_HPP
#define OPM_ECL_DEFAULT_MATERIAL_HPP

#include "EclDefaultMaterialParams.hpp"

#include <opm/material/common/MathToolbox.hpp>
#include <opm/material/common/Valgrind.hpp>

#include <algorithm>
#include <stdexcept>
#include <type_traits>

namespace Opm {

/*!
 * \ingroup material
 *
 * \brief Implements the default three phase capillary pressure law
 *        used by the ECLipse simulator.
 *
 * This material law is valid for three fluid phases and only depends
 * on the saturations.
 *
 * The required two-phase relations are supplied by means of template
 * arguments and can be an arbitrary other material laws. (Provided
 * that these only depend on saturation.)
 */
template <class TraitsT,
          class GasOilMaterialLawT,
          class OilWaterMaterialLawT,
          class ParamsT = EclDefaultMaterialParams<TraitsT,
                                                   typename GasOilMaterialLawT::Params,
                                                   typename OilWaterMaterialLawT::Params> >
class EclDefaultMaterial : public TraitsT
{
public:
    using GasOilMaterialLaw = GasOilMaterialLawT;
    using OilWaterMaterialLaw = OilWaterMaterialLawT;

    // some safety checks
    static_assert(TraitsT::numPhases == 3,
                  "The number of phases considered by this capillary pressure "
                  "law is always three!");
    static_assert(GasOilMaterialLaw::numPhases == 2,
                  "The number of phases considered by the gas-oil capillary "
                  "pressure law must be two!");
    static_assert(OilWaterMaterialLaw::numPhases == 2,
                  "The number of phases considered by the oil-water capillary "
                  "pressure law must be two!");
    static_assert(std::is_same<typename GasOilMaterialLaw::Scalar,
                               typename OilWaterMaterialLaw::Scalar>::value,
                  "The two two-phase capillary pressure laws must use the same "
                  "type of floating point values.");

    static_assert(GasOilMaterialLaw::implementsTwoPhaseSatApi,
                  "The gas-oil material law must implement the two-phase saturation "
                  "only API to for the default Ecl capillary pressure law!");
    static_assert(OilWaterMaterialLaw::implementsTwoPhaseSatApi,
                  "The oil-water material law must implement the two-phase saturation "
                  "only API to for the default Ecl capillary pressure law!");

    using Traits = TraitsT;
    using Params = ParamsT;
    using Scalar = typename Traits::Scalar;

    static constexpr int numPhases = 3;
    static constexpr int waterPhaseIdx = Traits::wettingPhaseIdx;
    static constexpr int oilPhaseIdx = Traits::nonWettingPhaseIdx;
    static constexpr int gasPhaseIdx = Traits::gasPhaseIdx;

    //! Specify whether this material law implements the two-phase
    //! convenience API
    static constexpr bool implementsTwoPhaseApi = false;

    //! Specify whether this material law implements the two-phase
    //! convenience API which only depends on the phase saturations
    static constexpr bool implementsTwoPhaseSatApi = false;

    //! Specify whether the quantities defined by this material law
    //! are saturation dependent
    static constexpr bool isSaturationDependent = true;

    //! Specify whether the quantities defined by this material law
    //! are dependent on the absolute pressure
    static constexpr bool isPressureDependent = false;

    //! Specify whether the quantities defined by this material law
    //! are temperature dependent
    static constexpr bool isTemperatureDependent = false;

    //! Specify whether the quantities defined by this material law
    //! are dependent on the phase composition
    static constexpr bool isCompositionDependent = false;

    /*!
     * \brief Implements the default three phase capillary pressure law
     *        used by the ECLipse simulator.
     *
     * This material law is valid for three fluid phases and only
     * depends on the saturations.
     *
     * The required two-phase relations are supplied by means of template
     * arguments and can be an arbitrary other material laws.
     *
     * \param values Container for the return values
     * \param params Parameters
     * \param state The fluid state
     */
    template <class ContainerT, class FluidState>
    static void capillaryPressures(ContainerT& values,
                                   const Params& params,
                                   const FluidState& state)
    {
        using Evaluation = typename std::remove_reference<decltype(values[0])>::type;
        values[gasPhaseIdx] = pcgn<FluidState, Evaluation>(params, state);
        values[oilPhaseIdx] = 0;
        values[waterPhaseIdx] = - pcnw<FluidState, Evaluation>(params, state);

        Valgrind::CheckDefined(values[gasPhaseIdx]);
        Valgrind::CheckDefined(values[oilPhaseIdx]);
        Valgrind::CheckDefined(values[waterPhaseIdx]);
    }

    /*
     * Hysteresis parameters for oil-water
     * @see EclHysteresisTwoPhaseLawParams::pcSwMdc(...)
     * @see EclHysteresisTwoPhaseLawParams::krnSwMdc(...)
     * \param params Parameters
     */
    static void oilWaterHysteresisParams(Scalar& pcSwMdc,
                                         Scalar& krnSwMdc,
                                         const Params& params)
    {
        pcSwMdc = params.oilWaterParams().pcSwMdc();
        krnSwMdc = params.oilWaterParams().krnSwMdc();

        Valgrind::CheckDefined(pcSwMdc);
        Valgrind::CheckDefined(krnSwMdc);
    }

    /*
     * Hysteresis parameters for oil-water
     * @see EclHysteresisTwoPhaseLawParams::pcSwMdc(...)
     * @see EclHysteresisTwoPhaseLawParams::krnSwMdc(...)
     * \param params Parameters
     */
    static void setOilWaterHysteresisParams(const Scalar& pcSwMdc,
                                            const Scalar& krnSwMdc,
                                            Params& params)
    {
        constexpr const double krwSw = 2.0; //Should not be used
        params.oilWaterParams().update(pcSwMdc, krwSw, krnSwMdc);
    }

    /*
     * Hysteresis parameters for gas-oil
     * @see EclHysteresisTwoPhaseLawParams::pcSwMdc(...)
     * @see EclHysteresisTwoPhaseLawParams::krnSwMdc(...)
     * \param params Parameters
     */
    static void gasOilHysteresisParams(Scalar& pcSwMdc,
                                       Scalar& krnSwMdc,
                                       const Params& params)
    {
        const auto Swco = params.Swl();

        // Pretend oil saturation is 'Swco' larger than it really is in
        // order to infer correct maximum Sg values in output layer.
        pcSwMdc = std::min(params.gasOilParams().pcSwMdc() + Swco, Scalar{2.0});
        krnSwMdc = std::min(params.gasOilParams().krnSwMdc() + Swco, Scalar{2.0});

        Valgrind::CheckDefined(pcSwMdc);
        Valgrind::CheckDefined(krnSwMdc);
    }

    /*
     * Hysteresis parameters for gas-oil
     * @see EclHysteresisTwoPhaseLawParams::pcSwMdc(...)
     * @see EclHysteresisTwoPhaseLawParams::krnSwMdc(...)
     * \param params Parameters
     */
    static void setGasOilHysteresisParams(const Scalar& pcSwMdc,
                                          const Scalar& krnSwMdc,
                                          Params& params)
    {
        // Maximum attainable oil saturation is 1-SWL
        const auto Swco = params.Swl();
        constexpr const double krwSw = 2.0; //Should not be used
        params.gasOilParams().update(pcSwMdc - Swco, krwSw, krnSwMdc - Swco);
    }

    /*!
     * \brief Capillary pressure between the gas and the non-wetting
     *        liquid (i.e., oil) phase.
     *
     * This is defined as
     * \f[
     * p_{c,gn} = p_g - p_n
     * \f]
     */
    template <class FluidState, class Evaluation = typename FluidState::Scalar>
    static Evaluation pcgn(const Params& params,
                           const FluidState& fs)
    {
        // Maximum attainable oil saturation is 1-SWL.
        const auto Sw = 1.0 - params.Swl() - decay<Evaluation>(fs.saturation(gasPhaseIdx));
        return GasOilMaterialLaw::twoPhaseSatPcnw(params.gasOilParams(), Sw);
    }

    /*!
     * \brief Capillary pressure between the non-wetting liquid (i.e.,
     *        oil) and the wetting liquid (i.e., water) phase.
     *
     * This is defined as
     * \f[
     * p_{c,nw} = p_n - p_w
     * \f]
     */
    template <class FluidState, class Evaluation = typename FluidState::Scalar>
    static Evaluation pcnw(const Params& params,
                           const FluidState& fs)
    {
        const auto Sw = decay<Evaluation>(fs.saturation(waterPhaseIdx));
        return OilWaterMaterialLaw::twoPhaseSatPcnw(params.oilWaterParams(), Sw);
    }

    /*!
     * \brief The inverse of the capillary pressure
     */
    template <class ContainerT, class FluidState>
    static void saturations(ContainerT& /*values*/,
                            const Params& /*params*/,
                            const FluidState& /*fluidState*/)
    {
        throw std::logic_error("Not implemented: saturations()");
    }

    /*!
     * \brief The saturation of the gas phase.
     */
    template <class FluidState, class Evaluation = typename FluidState::Scalar>
    static Evaluation Sg(const Params& /*params*/,
                         const FluidState& /*fluidState*/)
    {
        throw std::logic_error("Not implemented: Sg()");
    }

    /*!
     * \brief The saturation of the non-wetting (i.e., oil) phase.
     */
    template <class FluidState, class Evaluation = typename FluidState::Scalar>
    static Evaluation Sn(const Params& /*params*/,
                         const FluidState& /*fluidState*/)
    {
        throw std::logic_error("Not implemented: Sn()");
    }

    /*!
     * \brief The saturation of the wetting (i.e., water) phase.
     */
    template <class FluidState, class Evaluation = typename FluidState::Scalar>
    static Evaluation Sw(const Params& /*params*/,
                         const FluidState& /*fluidState*/)
    {
        throw std::logic_error("Not implemented: Sw()");
    }

    /*!
     * \brief The relative permeability of all phases.
     *
     * The relative permeability of the water phase it uses the same
     * value as the relative permeability for water in the water-oil
     * law with \f$S_o = 1 - S_w\f$. The gas relative permebility is
     * taken from the gas-oil material law, but with \f$S_o = 1 -
     * S_g\f$.  The relative permeability of the oil phase is
     * calculated using the relative permeabilities of the oil phase
     * in the two two-phase systems.
     *
     * A more detailed description can be found in the "Three phase
     * oil relative permeability models" section of the ECLipse
     * technical description.
     */
    template <class ContainerT, class FluidState>
    static void relativePermeabilities(ContainerT& values,
                                       const Params& params,
                                       const FluidState& fluidState)
    {
        using Evaluation = typename std::remove_reference<decltype(values[0])>::type;

        values[waterPhaseIdx] = krw<FluidState, Evaluation>(params, fluidState);
        values[oilPhaseIdx] = krn<FluidState, Evaluation>(params, fluidState);
        values[gasPhaseIdx] = krg<FluidState, Evaluation>(params, fluidState);
    }

    /*!
     * \brief The relative permeability of the gas phase.
     */
    template <class FluidState, class Evaluation = typename FluidState::Scalar>
    static Evaluation krg(const Params& params,
                          const FluidState& fluidState)
    {
        // Maximum attainable oil saturation is 1-SWL.
        const Evaluation Sw = 1.0 - params.Swl() - decay<Evaluation>(fluidState.saturation(gasPhaseIdx));
        return GasOilMaterialLaw::twoPhaseSatKrn(params.gasOilParams(), Sw);
    }

    /*!
     * \brief The relative permeability of the wetting phase.
     */
    template <class FluidState, class Evaluation = typename FluidState::Scalar>
    static Evaluation krw(const Params& params,
                          const FluidState& fluidState)
    {
        const Evaluation Sw = decay<Evaluation>(fluidState.saturation(waterPhaseIdx));
        return OilWaterMaterialLaw::twoPhaseSatKrw(params.oilWaterParams(), Sw);
    }

    /*!
     * \brief The relative permeability of the non-wetting (i.e., oil) phase.
     */
    template <class FluidState, class Evaluation = typename FluidState::Scalar>
    static Evaluation krn(const Params& params,
                          const FluidState& fluidState)
    {
        const Scalar Swco = params.Swl();

        const Evaluation Sw =
            max(Evaluation(Swco),
                     decay<Evaluation>(fluidState.saturation(waterPhaseIdx)));

        const Evaluation Sg = decay<Evaluation>(fluidState.saturation(gasPhaseIdx));

        const Evaluation Sw_ow = Sg + Sw;
        const Evaluation kro_ow = relpermOilInOilWaterSystem<Evaluation>(params, fluidState);
        const Evaluation kro_go = relpermOilInOilGasSystem<Evaluation>(params, fluidState);

        // avoid the division by zero: chose a regularized kro which is used if Sw - Swco
        // < epsilon/2 and interpolate between the oridinary and the regularized kro between
        // epsilon and epsilon/2
        constexpr const Scalar epsilon = 1e-5;
        if (scalarValue(Sw_ow) - Swco < epsilon) {
            const Evaluation kro2 = (kro_ow + kro_go)/2;
            if (scalarValue(Sw_ow) - Swco > epsilon/2) {
                const Evaluation kro1 = (Sg*kro_go + (Sw - Swco)*kro_ow)/(Sw_ow - Swco);
                const Evaluation alpha = (epsilon - (Sw_ow - Swco))/(epsilon/2);

                return kro2*alpha + kro1*(1 - alpha);
            }

            return kro2;
        }

        return (Sg*kro_go + (Sw - Swco)*kro_ow) / (Sw_ow - Swco);
    }

    /*!
     * \brief The relative permeability of oil in oil/gas system.
     */
    template <class Evaluation, class FluidState>
    static Evaluation relpermOilInOilGasSystem(const Params& params,
                                               const FluidState& fluidState)
    {
        const Evaluation Sw =
            max(Evaluation{ params.Swl() },
                     decay<Evaluation>(fluidState.saturation(waterPhaseIdx)));

        const Evaluation Sg = decay<Evaluation>(fluidState.saturation(gasPhaseIdx));
        const Evaluation So_go = 1.0 - (Sg + Sw);

        return GasOilMaterialLaw::twoPhaseSatKrw(params.gasOilParams(), So_go);
    }

    /*!
     * \brief The relative permeability of oil in oil/water system.
     */
    template <class Evaluation, class FluidState>
    static Evaluation relpermOilInOilWaterSystem(const Params& params,
                                                 const FluidState& fluidState)
    {
        const Evaluation Sw =
            max(Evaluation{ params.Swl() },
                     decay<Evaluation>(fluidState.saturation(waterPhaseIdx)));

        const Evaluation Sg = decay<Evaluation>(fluidState.saturation(gasPhaseIdx));
        const Evaluation Sw_ow = Sg + Sw;

        return OilWaterMaterialLaw::twoPhaseSatKrn(params.oilWaterParams(), Sw_ow);
    }

    /*!
     * \brief Update the hysteresis parameters after a time step.
     *
     * This assumes that the nested two-phase material laws are parameters for
     * EclHysteresisLaw. If they are not, calling this methid will cause a compiler
     * error. (But not calling it will still work.)
     */
    template <class FluidState>
    static void updateHysteresis(Params& params, const FluidState& fluidState)
    {
        const Scalar Swco = params.Swl();

        const Scalar Sw = clampSaturation(fluidState, waterPhaseIdx);
        const Scalar So = clampSaturation(fluidState, oilPhaseIdx);
        const Scalar Sg = clampSaturation(fluidState, gasPhaseIdx);

        if (params.inconsistentHysteresisUpdate()) {
            // NOTE: the saturations which are passed to update the hysteresis curves are
            // inconsistent with the ones used to calculate the relative permabilities. We do
            // it like this anyway because (a) the saturation functions of opm-core do it
            // this way (b) the simulations seem to converge better (which is not too much
            // surprising actually, because the time step does not start on a kink in the
            // solution) and (c) the Eclipse 100 simulator may do the same.
            //
            // Though be aware that from a physical perspective this is definitively
            // incorrect!
            params.oilWaterParams().update(/*pcSw=*/  Sw, //1.0 - So, (Effect is significant vs benchmark.)
                                           /*krwSw=*/ 1.0 - So,
                                           /*krnSw=*/ 1.0 - So);

            params.gasOilParams().update(/*pcSw=*/  1.0 - Swco - Sg,
                                         /*krwSw=*/ 1.0 - Swco - Sg,
                                         /*krnSw=*/ 1.0 - Swco - Sg);
        }
        else {
            const Scalar Sw_ow = Sg + std::max(Swco, Sw);
            const Scalar So_go = 1.0 - Sw_ow;

            params.oilWaterParams().update(/*pcSw=*/  Sw,
                                           /*krwSw=*/ 1 - Sg,
                                           /*krnSw=*/ Sw_ow);

            params.gasOilParams().update(/*pcSw=*/  1.0 - Swco - Sg,
                                         /*krwSw=*/ So_go,
                                         /*krnSw=*/ 1.0 - Swco - Sg);
        }
    }

    template <class FluidState>
    static Scalar clampSaturation(const FluidState& fluidState, const int phaseIndex)
    {
        const auto sat = scalarValue(fluidState.saturation(phaseIndex));
        return std::clamp(sat, Scalar{0.0}, Scalar{1.0});
    }
};
} // namespace Opm

#endif
