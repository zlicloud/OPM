/*
  Copyright (C) 2020 Equinor

  This file is part of the Open Porous Media project (OPM).

  OPM is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  OPM is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with OPM.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef OPM_AUQIFER_CONFIG_HPP
#define OPM_AUQIFER_CONFIG_HPP

#include <opm/input/eclipse/EclipseState/Aquifer/Aquancon.hpp>
#include <opm/input/eclipse/EclipseState/Aquifer/Aquifetp.hpp>
#include <opm/input/eclipse/EclipseState/Aquifer/AquiferCT.hpp>
#include <opm/input/eclipse/EclipseState/Aquifer/AquiferFlux.hpp>
#include <opm/input/eclipse/EclipseState/Aquifer/NumericalAquifer/NumericalAquifers.hpp>

#include <cstddef>
#include <vector>
#include <unordered_set>

namespace Opm {
    class TableManager;
    class EclipseGrid;
    class Deck;
    class FieldPropsManager;
} // namespace Opm

namespace Opm { namespace RestartIO {
    class RstAquifer;
}} // namespace Opm::RestartIO

namespace Opm {

class AquiferConfig {
public:

    AquiferConfig() = default;
    AquiferConfig(const TableManager& tables, const EclipseGrid& grid,
                  const Deck& deck, const FieldPropsManager& field_props);
    AquiferConfig(const Aquifetp& fetp, const AquiferCT& ct, const AquiferFlux& aqufluxs, const Aquancon& conn);
    void load_connections(const Deck& deck, const EclipseGrid& grid);

    void pruneDeactivatedAquiferConnections(const std::vector<std::size_t>& deactivated_cells);
    void loadFromRestart(const RestartIO::RstAquifer& aquifers,
                         const TableManager&          tables);

    // there might be some aquifers (AQUFLUX only for now) are opened through
    // SCHEDULE section while not specified in the SOLUTION section.
    // We create dummy aquifers in the AquiferConfig to make sure we are aware of them
    // when we handle the SUMMARY section.
    // Since those aquifers are not active, basically we only need the id information
    void appendAqufluxSchedule(const std::unordered_set<int>& ids);

    static AquiferConfig serializationTestObject();

    bool active() const;
    const AquiferCT& ct() const;
    const Aquifetp& fetp() const;
    const AquiferFlux& aquflux() const;
    const Aquancon& connections() const;
    bool operator==(const AquiferConfig& other) const;
    bool hasAquifer(const int aquID) const;
    bool hasAnalyticalAquifer(const int aquID) const;

    bool hasNumericalAquifer() const;
    bool hasAnalyticalAquifer() const;
    const NumericalAquifers& numericalAquifers() const;
    NumericalAquifers& mutableNumericalAquifers() const;

    template<class Serializer>
    void serializeOp(Serializer& serializer)
    {
        serializer(aquifetp);
        serializer(aquiferct);
        serializer(aqconn);
        serializer(aquiferflux);
        serializer(numerical_aquifers);
    }

private:
    Aquifetp aquifetp{};
    AquiferCT aquiferct{};
    AquiferFlux aquiferflux{};
    mutable NumericalAquifers numerical_aquifers{};
    Aquancon aqconn{};
};

std::vector<int> analyticAquiferIDs(const AquiferConfig& cfg);
std::vector<int> numericAquiferIDs(const AquiferConfig& cfg);
}

#endif
