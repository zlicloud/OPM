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
 * \copydoc Opm::NullSolidEnergyLaw
 */
#ifndef OPM_NULL_SOLID_ENERGY_LAW_HPP
#define OPM_NULL_SOLID_ENERGY_LAW_HPP

namespace Opm
{
/*!
 * \ingroup material
 *
 * \brief Implements a solid energy storage law which just returns 0.
 */
template <class ScalarT>
class NullSolidEnergyLaw
{
public:
    using Params = int;
    using Scalar = ScalarT;

    /*!
     * \brief Given a fluid state, compute the volumetric internal energy of the solid
     *        matrix [W/m^3].
     *
     * This solid energy law simply returns 0.
     */
    template <class FluidState, class Evaluation = typename FluidState::Scalar>
    static Evaluation solidInternalEnergy(const Params&, const FluidState&)
    { return 0.0; }
};

} // namespace Opm

#endif
