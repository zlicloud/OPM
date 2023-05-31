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
 * \copydoc Opm::EclEpsTwoPhaseLaw
 */
#ifndef OPM_ECL_EPS_TWO_PHASE_LAW_HPP
#define OPM_ECL_EPS_TWO_PHASE_LAW_HPP

#include "EclEpsTwoPhaseLawParams.hpp"

#include <algorithm>
#include <cstddef>
#include <type_traits>

namespace Opm {
/*!
 * \ingroup FluidMatrixInteractions
 *
 * \brief This material law takes a material law defined for unscaled saturation and
 *        converts it to a material law defined on scaled saturations.
 *
 * In ECL, simulations "live" in scaled space, while the saturation functions operate on
 * and produce unscaled quantities. This class implements the "impedance adaption" layer
 * between the two worlds. The basic purpose of it is thus the same as the one of \a
 * EffToAbsLaw, but it is quite a bit more complex.
 */
template <class EffLawT,
          class ParamsT = EclEpsTwoPhaseLawParams<EffLawT> >
class EclEpsTwoPhaseLaw : public EffLawT::Traits
{
    typedef EffLawT EffLaw;

public:
    typedef typename EffLaw::Traits Traits;
    typedef ParamsT Params;
    typedef typename EffLaw::Scalar Scalar;

    enum { wettingPhaseIdx = Traits::wettingPhaseIdx };
    enum { nonWettingPhaseIdx = Traits::nonWettingPhaseIdx };

    //! The number of fluid phases
    static const int numPhases = EffLaw::numPhases;
    static_assert(numPhases == 2,
                  "The endpoint scaling applies to the nested twophase laws, not to "
                  "the threephase one!");

    //! Specify whether this material law implements the two-phase
    //! convenience API
    static const bool implementsTwoPhaseApi = true;

    static_assert(EffLaw::implementsTwoPhaseApi,
                  "The material laws put into EclEpsTwoPhaseLaw must implement the "
                  "two-phase material law API!");

    //! Specify whether this material law implements the two-phase
    //! convenience API which only depends on the phase saturations
    static const bool implementsTwoPhaseSatApi = true;

    static_assert(EffLaw::implementsTwoPhaseSatApi,
                  "The material laws put into EclEpsTwoPhaseLaw must implement the "
                  "two-phase material law saturation API!");

    //! Specify whether the quantities defined by this material law
    //! are saturation dependent
    static const bool isSaturationDependent = true;

    //! Specify whether the quantities defined by this material law
    //! are dependent on the absolute pressure
    static const bool isPressureDependent = false;

    //! Specify whether the quantities defined by this material law
    //! are temperature dependent
    static const bool isTemperatureDependent = false;

    //! Specify whether the quantities defined by this material law
    //! are dependent on the phase composition
    static const bool isCompositionDependent = false;

    /*!
     * \brief The capillary pressure-saturation curves depending on absolute saturations.
     *
     * \param values A random access container which stores the
     *               relative pressure of each fluid phase.
     * \param params The parameter object expressing the coefficients
     *               required by the van Genuchten law.
     * \param fs The fluid state for which the capillary pressure
     *           ought to be calculated
     */
    template <class Container, class FluidState>
    static void capillaryPressures(Container& /*values*/, const Params& /*params*/, const FluidState& /*fluidState*/)
    {
        throw std::invalid_argument("The capillaryPressures(fs) method is not yet implemented");
    }

    /*!
     * \brief The relative permeability-saturation curves depending on absolute saturations.
     *
     * \param values A random access container which stores the
     *               relative permeability of each fluid phase.
     * \param params The parameter object expressing the coefficients
     *               required by the van Genuchten law.
     * \param fs The fluid state for which the relative permeabilities
     *           ought to be calculated
     */
    template <class Container, class FluidState>
    static void relativePermeabilities(Container& /*values*/, const Params& /*params*/, const FluidState& /*fluidState*/)
    {
        throw std::invalid_argument("The pcnw(fs) method is not yet implemented");
    }

    /*!
     * \brief The capillary pressure-saturation curve.
     *
     *
     * \param params A object that stores the appropriate coefficients
     *                for the respective law.
     *
     * \return Capillary pressure [Pa] calculated by specific
     *         constitutive relation (e.g. Brooks & Corey, van
     *         Genuchten, linear...)
     */
    template <class FluidState, class Evaluation = typename FluidState::Scalar>
    static Evaluation pcnw(const Params& /*params*/, const FluidState& /*fluidState*/)
    {
        throw std::invalid_argument("The pcnw(fs) method is not yet implemented");
    }

    template <class Evaluation>
    static Evaluation twoPhaseSatPcnw(const Params& params, const Evaluation& SwScaled)
    {
        const Evaluation SwUnscaled = scaledToUnscaledSatPc(params, SwScaled);
        const Evaluation pcUnscaled = EffLaw::twoPhaseSatPcnw(params.effectiveLawParams(), SwUnscaled);
        return unscaledToScaledPcnw_(params, pcUnscaled);
    }

    template <class Evaluation>
    static Evaluation twoPhaseSatPcnwInv(const Params& params, const Evaluation& pcnwScaled)
    {
        const Evaluation pcnwUnscaled = scaledToUnscaledPcnw_(params, pcnwScaled);
        const Evaluation SwUnscaled = EffLaw::twoPhaseSatPcnwInv(params.effectiveLawParams(), pcnwUnscaled);
        return unscaledToScaledSatPc(params, SwUnscaled);
    }

    /*!
     * \brief The saturation-capillary pressure curves.
     */
    template <class Container, class FluidState>
    static void saturations(Container& /*values*/, const Params& /*params*/, const FluidState& /*fluidState*/)
    {
        throw std::invalid_argument("The saturations(fs) method is not yet implemented");
    }

    /*!
     * \brief Calculate wetting liquid phase saturation given that
     *        the rest of the fluid state has been initialized
     */
    template <class FluidState, class Evaluation = typename FluidState::Scalar>
    static Evaluation Sw(const Params& /*params*/, const FluidState& /*fluidState*/)
    {
        throw std::invalid_argument("The Sw(fs) method is not yet implemented");
    }

    template <class Evaluation>
    static Evaluation twoPhaseSatSw(const Params& /*params*/, const Evaluation& /*pc*/)
    {
        throw std::invalid_argument("The twoPhaseSatSw(pc) method is not yet implemented");
    }

    /*!
     * \brief Calculate non-wetting liquid phase saturation given that
     *        the rest of the fluid state has been initialized
     */
    template <class FluidState, class Evaluation = typename FluidState::Scalar>
    static Evaluation Sn(const Params& /*params*/, const FluidState& /*fluidState*/)
    {
        throw std::invalid_argument("The Sn(pc) method is not yet implemented");
    }

    template <class Evaluation>
    static Evaluation twoPhaseSatSn(const Params& /*params*/, const Evaluation& /*pc*/)
    {
        throw std::invalid_argument("The twoPhaseSatSn(pc) method is not yet implemented");
    }

    /*!
     * \brief The relative permeability for the wetting phase.
     *
     * \param params    A container object that is populated with the appropriate coefficients for the respective law.
     *                  Therefore, in the (problem specific) spatialParameters  first, the material law is chosen, and then the params container
     *                  is constructed accordingly. Afterwards the values are set there, too.
     * \return          Relative permeability of the wetting phase calculated as implied by EffLaw e.g. Brooks & Corey, van Genuchten, linear... .
     *
     */
    template <class FluidState, class Evaluation = typename FluidState::Scalar>
    static Evaluation krw(const Params& /*params*/, const FluidState& /*fluidState*/)
    {
        throw std::invalid_argument("The krw(fs) method is not yet implemented");
    }

    template <class Evaluation>
    static Evaluation twoPhaseSatKrw(const Params& params, const Evaluation& SwScaled)
    {
        const Evaluation SwUnscaled = scaledToUnscaledSatKrw(params, SwScaled);
        const Evaluation krwUnscaled = EffLaw::twoPhaseSatKrw(params.effectiveLawParams(), SwUnscaled);
        return unscaledToScaledKrw_(SwScaled, params, krwUnscaled);
    }

    template <class Evaluation>
    static Evaluation twoPhaseSatKrwInv(const Params& params, const Evaluation& krwScaled)
    {
        const Evaluation krwUnscaled = scaledToUnscaledKrw_(params, krwScaled);
        const Evaluation SwUnscaled = EffLaw::twoPhaseSatKrwInv(params.effectiveLawParams(), krwUnscaled);
        return unscaledToScaledSatKrw(params, SwUnscaled);
    }

    /*!
     * \brief The relative permeability of the non-wetting phase.
     */
    template <class FluidState, class Evaluation = typename FluidState::Scalar>
    static Evaluation krn(const Params& /*params*/, const FluidState& /*fluidState*/)
    {
        throw std::invalid_argument("The krn(fs) method is not yet implemented");
    }

    template <class Evaluation>
    static Evaluation twoPhaseSatKrn(const Params& params, const Evaluation& SwScaled)
    {
        const Evaluation SwUnscaled = scaledToUnscaledSatKrn(params, SwScaled);
        const Evaluation krnUnscaled = EffLaw::twoPhaseSatKrn(params.effectiveLawParams(), SwUnscaled);
        return unscaledToScaledKrn_(SwScaled, params, krnUnscaled);
    }

    template <class Evaluation>
    static Evaluation twoPhaseSatKrnInv(const Params& params, const Evaluation& krnScaled)
    {
        const Evaluation krnUnscaled = scaledToUnscaledKrn_(params, krnScaled);
        const Evaluation SwUnscaled = EffLaw::twoPhaseSatKrnInv(params.effectiveLawParams(), krnUnscaled);
        return unscaledToScaledSatKrn(params, SwUnscaled);
    }

    /*!
     * \brief Convert an absolute saturation to an effective one for capillary pressure.
     *
     * The effective saturation is then feed into the "raw" capillary pressure law.
     */
    template <class Evaluation>
    static Evaluation scaledToUnscaledSatPc(const Params& params, const Evaluation& SwScaled)
    {
        if (!params.config().enableSatScaling())
            return SwScaled;

        // the saturations of capillary pressure are always scaled using two-point
        // scaling
        return scaledToUnscaledSatTwoPoint_(SwScaled,
                                            params.unscaledPoints().saturationPcPoints(),
                                            params.scaledPoints().saturationPcPoints());
    }

    template <class Evaluation>
    static Evaluation unscaledToScaledSatPc(const Params& params, const Evaluation& SwUnscaled)
    {
        if (!params.config().enableSatScaling())
            return SwUnscaled;

        // the saturations of capillary pressure are always scaled using two-point
        // scaling
        return unscaledToScaledSatTwoPoint_(SwUnscaled,
                                            params.unscaledPoints().saturationPcPoints(),
                                            params.scaledPoints().saturationPcPoints());
    }

    /*!
     * \brief Convert an absolute saturation to an effective one for the scaling of the
     *        relperm of the wetting phase.
     */
    template <class Evaluation>
    static Evaluation scaledToUnscaledSatKrw(const Params& params, const Evaluation& SwScaled)
    {
        if (!params.config().enableSatScaling())
            return SwScaled;

        if (params.config().enableThreePointKrSatScaling()) {
            return scaledToUnscaledSatThreePoint_(SwScaled,
                                                  params.unscaledPoints().saturationKrwPoints(),
                                                  params.scaledPoints().saturationKrwPoints());
        }
        else { // two-point relperm saturation scaling
            return scaledToUnscaledSatTwoPoint_(SwScaled,
                                                params.unscaledPoints().saturationKrwPoints(),
                                                params.scaledPoints().saturationKrwPoints());
        }
    }

    template <class Evaluation>
    static Evaluation unscaledToScaledSatKrw(const Params& params, const Evaluation& SwUnscaled)
    {
        if (!params.config().enableSatScaling())
            return SwUnscaled;

        if (params.config().enableThreePointKrSatScaling()) {
            return unscaledToScaledSatThreePoint_(SwUnscaled,
                                                  params.unscaledPoints().saturationKrwPoints(),
                                                  params.scaledPoints().saturationKrwPoints());
        }
        else { // two-point relperm saturation scaling
            return unscaledToScaledSatTwoPoint_(SwUnscaled,
                                                params.unscaledPoints().saturationKrwPoints(),
                                                params.scaledPoints().saturationKrwPoints());
        }
    }

    /*!
     * \brief Convert an absolute saturation to an effective one for the scaling of the
     *        relperm of the non-wetting phase.
     */
    template <class Evaluation>
    static Evaluation scaledToUnscaledSatKrn(const Params& params, const Evaluation& SwScaled)
    {
        if (!params.config().enableSatScaling())
            return SwScaled;

        if (params.config().enableThreePointKrSatScaling())
            return scaledToUnscaledSatThreePoint_(SwScaled,
                                                  params.unscaledPoints().saturationKrnPoints(),
                                                  params.scaledPoints().saturationKrnPoints());
        else // two-point relperm saturation scaling
            return scaledToUnscaledSatTwoPoint_(SwScaled,
                                                params.unscaledPoints().saturationKrnPoints(),
                                                params.scaledPoints().saturationKrnPoints());
    }


    template <class Evaluation>
    static Evaluation unscaledToScaledSatKrn(const Params& params, const Evaluation& SwUnscaled)
    {
        if (!params.config().enableSatScaling())
            return SwUnscaled;

        if (params.config().enableThreePointKrSatScaling()) {
            return unscaledToScaledSatThreePoint_(SwUnscaled,
                                                  params.unscaledPoints().saturationKrnPoints(),
                                                  params.scaledPoints().saturationKrnPoints());
        }
        else { // two-point relperm saturation scaling
            return unscaledToScaledSatTwoPoint_(SwUnscaled,
                                                params.unscaledPoints().saturationKrnPoints(),
                                                params.scaledPoints().saturationKrnPoints());
        }
    }

private:
    template <class Evaluation, class PointsContainer>
    static Evaluation scaledToUnscaledSatTwoPoint_(const Evaluation& scaledSat,
                                                   const PointsContainer& unscaledSats,
                                                   const PointsContainer& scaledSats)
    {
        return
            unscaledSats[0]
            +
            (scaledSat - scaledSats[0])*((unscaledSats[2] - unscaledSats[0])/(scaledSats[2] - scaledSats[0]));
    }

    template <class Evaluation, class PointsContainer>
    static Evaluation unscaledToScaledSatTwoPoint_(const Evaluation& unscaledSat,
                                                   const PointsContainer& unscaledSats,
                                                   const PointsContainer& scaledSats)
    {
        return
            scaledSats[0]
            +
            (unscaledSat - unscaledSats[0])*((scaledSats[2] - scaledSats[0])/(unscaledSats[2] - unscaledSats[0]));
    }

    template <class Evaluation, class PointsContainer>
    static Evaluation scaledToUnscaledSatThreePoint_(const Evaluation& scaledSat,
                                                     const PointsContainer& unscaledSats,
                                                     const PointsContainer& scaledSats)
    {
        using UnscaledSat = std::remove_cv_t<std::remove_reference_t<decltype(unscaledSats[0])>>;

        auto map = [&scaledSat, &unscaledSats, &scaledSats](const std::size_t i)
        {
            const auto distance = (scaledSat         - scaledSats[i])
                                / (scaledSats[i + 1] - scaledSats[i]);

            const auto displacement =
                std::max(unscaledSats[i + 1] - unscaledSats[i], UnscaledSat{ 0 });

            return std::min(unscaledSats[i] + distance*displacement,
                            Evaluation { unscaledSats[i + 1] });
        };

        if (! (scaledSat > scaledSats[0])) {
            // s <= sL
            return unscaledSats[0];
        }
        else if (scaledSat < std::min(scaledSats[1], scaledSats[2])) {
            // Scaled saturation in interval [sL, sR).
            // Map to tabulated saturation in [unscaledSats[0], unscaledSats[1]).
            return map(0);
        }
        else if (scaledSat < scaledSats[2]) {
            // Scaled saturation in interval [sR, sU); sR guaranteed to be less
            // than sU from previous condition.  Map to tabulated saturation in
            // [unscaledSats[1], unscaledSats[2]).
            return map(1);
        }
        else {
            // s >= sU
            return unscaledSats[2];
        }
    }

    template <class Evaluation, class PointsContainer>
    static Evaluation unscaledToScaledSatThreePoint_(const Evaluation& unscaledSat,
                                                     const PointsContainer& unscaledSats,
                                                     const PointsContainer& scaledSats)
    {
        using ScaledSat = std::remove_cv_t<std::remove_reference_t<decltype(scaledSats[0])>>;

        auto map = [&unscaledSat, &unscaledSats, &scaledSats](const std::size_t i)
        {
            const auto distance = (unscaledSat         - unscaledSats[i])
                                / (unscaledSats[i + 1] - unscaledSats[i]);

            const auto displacement =
                std::max(scaledSats[i + 1] - scaledSats[i], ScaledSat{ 0 });

            return std::min(scaledSats[i] + distance*displacement,
                            Evaluation { scaledSats[i + 1] });
        };

        if (! (unscaledSat > unscaledSats[0])) {
            return scaledSats[0];
        }
        else if (unscaledSat < unscaledSats[1]) {
            // Tabulated saturation in interval [unscaledSats[0], unscaledSats[1]).
            // Map to scaled saturation in [sL, sR).
            return map(0);
        }
        else if (unscaledSat < unscaledSats[2]) {
            // Tabulated saturation in interval [unscaledSats[1], unscaledSats[2]).
            // Map to scaled saturation in [sR, sU).
            return map(1);
        }
        else {
            return scaledSats[2];
        }
    }

    /*!
     * \brief Scale the capillary pressure according to the given parameters
     */
    template <class Evaluation>
    static Evaluation unscaledToScaledPcnw_(const Params& params, const Evaluation& unscaledPcnw)
    {
        if (params.config().enableLeverettScaling()) {
            Scalar alpha = params.scaledPoints().leverettFactor();
            return unscaledPcnw*alpha;
        }
        else if (params.config().enablePcScaling()) {
            const auto& scaled_maxPcnw = params.scaledPoints().maxPcnw();
            const auto& unscaled_maxPcnw = params.unscaledPoints().maxPcnw();

            Scalar alpha;
            if (scaled_maxPcnw == unscaled_maxPcnw)
                alpha = 1.0;
            else
                alpha = params.scaledPoints().maxPcnw()/params.unscaledPoints().maxPcnw();

            return unscaledPcnw*alpha;
        }

        return unscaledPcnw;
    }

    template <class Evaluation>
    static Evaluation scaledToUnscaledPcnw_(const Params& params, const Evaluation& scaledPcnw)
    {
        if (params.config().enableLeverettScaling()) {
            Scalar alpha = params.scaledPoints().leverettFactor();
            return scaledPcnw/alpha;
        }
        else if (params.config().enablePcScaling()) {
            const auto& scaled_maxPcnw = params.scaledPoints().maxPcnw();
            const auto& unscaled_maxPcnw = params.unscaledPoints().maxPcnw();

            Scalar alpha;
            if (scaled_maxPcnw == unscaled_maxPcnw)
                alpha = 1.0;
            else
                alpha = params.scaledPoints().maxPcnw()/params.unscaledPoints().maxPcnw();

            return scaledPcnw/alpha;
        }

        return scaledPcnw;
    }

    /*!
     * \brief Scale the wetting phase relative permeability of a phase according to the given parameters
     */
    template <class Evaluation>
    static Evaluation unscaledToScaledKrw_(const Evaluation& SwScaled,
                                           const Params& params,
                                           const Evaluation& unscaledKrw)
    {
        const auto& cfg = params.config();

        if (! cfg.enableKrwScaling())
            return unscaledKrw;

        const auto& scaled   = params.scaledPoints();
        const auto& unscaled = params.unscaledPoints();

        if (! cfg.enableThreePointKrwScaling()) {
            // Simple case: Run uses pure vertical scaling of water relperm (keyword KRW)
            const Scalar alpha = scaled.maxKrw() / unscaled.maxKrw();
            return unscaledKrw * alpha;
        }

        // Otherwise, run uses three-point vertical scaling (keywords KRWR and KRW)
        const auto fdisp = unscaled.krwr();
        const auto fmax  = unscaled.maxKrw();

        const auto sm = scaled.saturationKrwPoints()[2];
        const auto sr = std::min(scaled.saturationKrwPoints()[1], sm);
        const auto fr = scaled.krwr();
        const auto fm = scaled.maxKrw();

        if (! (SwScaled > sr)) {
            // Pure vertical scaling in left interval ([SWL, SR])
            return unscaledKrw * (fr / fdisp);
        }
        else if (fmax > fdisp) {
            // s \in [sr, sm), sm > sr; normal case: Kr(Smax) > Kr(Sr).
            //
            // Linear function between (sr,fr) and (sm,fm) in terms of
            // function value 'unscaledKrw'.  This usually alters the shape
            // of the relative permeability function in this interval (e.g.,
            // roughly quadratic to linear).
            const auto t = (unscaledKrw - fdisp) / (fmax - fdisp);

            return fr + t*(fm - fr);
        }
        else if (sr < sm) {
            // s \in [sr, sm), sm > sr; special case: Kr(Smax) == Kr(Sr).
            //
            // Linear function between (sr,fr) and (sm,fm) in terms of
            // saturation value 'SwScaled'.  This usually alters the shape
            // of the relative permeability function in this interval (e.g.,
            // roughly quadratic to linear).
            const auto t = (SwScaled - sr) / (sm - sr);

            return fr + t*(fm - fr);
        }
        else {
            // sm == sr (pure scaling).  Almost arbitrarily pick 'fm'.
            return fm;
        }
    }

    template <class Evaluation>
    static Evaluation scaledToUnscaledKrw_(const Params& params, const Evaluation& scaledKrw)
    {
        if (!params.config().enableKrwScaling())
            return scaledKrw;

        Scalar alpha = params.unscaledPoints().maxKrw()/params.scaledPoints().maxKrw();
        return scaledKrw*alpha;
    }

    /*!
     * \brief Scale the non-wetting phase relative permeability of a phase according to the given parameters
     */
    template <class Evaluation>
    static Evaluation unscaledToScaledKrn_(const Evaluation& SwScaled,
                                           const Params& params,
                                           const Evaluation& unscaledKrn)
    {
        const auto& cfg = params.config();

        if (! cfg.enableKrnScaling())
            return unscaledKrn;

        const auto& scaled = params.scaledPoints();
        const auto& unscaled = params.unscaledPoints();

        if (! cfg.enableThreePointKrnScaling()) {
            // Simple case: Run uses pure vertical scaling of non-wetting
            // phase's relative permeability (e.g., KRG)
            const Scalar alpha = scaled.maxKrn() / unscaled.maxKrn();
            return unscaledKrn * alpha;
        }

        // Otherwise, run uses three-point vertical scaling (e.g., keywords KRGR and KRG)
        const auto fdisp = unscaled.krnr();
        const auto fmax  = unscaled.maxKrn();

        const auto sl = scaled.saturationKrnPoints()[0];
        const auto sr = std::max(scaled.saturationKrnPoints()[1], sl);
        const auto fr = scaled.krnr();
        const auto fm = scaled.maxKrn();

        // Note logic here.  Krn is a decreasing function of Sw (dKrn/dSw <=
        // 0) so the roles of left and right intervals are reversed viz
        // unscaledToScaledKrw_().

        if (! (SwScaled < sr)) {
            // Pure vertical scaling in right-hand interval ([SR, SWU])
            return unscaledKrn * (fr / fdisp);
        }
        else if (fmax > fdisp) {
            // s \in [SWL, SR), SWL < SR; normal case: Kr(Swl) > Kr(Sr).
            //
            // Linear function between (sr,fr) and (sl,fm) in terms of
            // function value 'unscaledKrn'.  This usually alters the shape
            // of the relative permeability function in this interval (e.g.,
            // roughly quadratic to linear).
            const auto t = (unscaledKrn - fdisp) / (fmax - fdisp);

            return fr + t*(fm - fr);
        }
        else if (sr > sl) {
            // s \in [SWL, SR), SWL < SR; special case: Kr(Swl) == Kr(Sr).
            //
            // Linear function between (sr,fr) and (sl,fm) in terms of
            // saturation value 'SwScaled'.  This usually alters the shape
            // of the relative permeability function in this interval (e.g.,
            // roughly quadratic to linear).
            const auto t = (sr - SwScaled) / (sr - sl);

            return fr + t*(fm - fr);
        }
        else {
            // sl == sr (pure scaling).  Almost arbitrarily pick 'fm'.
            return fm;
        }
    }

    template <class Evaluation>
    static Evaluation scaledToUnscaledKrn_(const Params& params, const Evaluation& scaledKrn)
    {
        if (!params.config().enableKrnScaling())
            return scaledKrn;

        Scalar alpha = params.unscaledPoints().maxKrn()/params.scaledPoints().maxKrn();
        return scaledKrn*alpha;
    }
};
} // namespace Opm

#endif
