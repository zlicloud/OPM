/*
  Copyright 2020 Equinor ASA

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

#define BOOST_TEST_MODULE test_rst

#include <boost/test/unit_test.hpp>

#include <opm/io/eclipse/OutputStream.hpp>
#include <opm/io/eclipse/ERst.hpp>
#include <opm/io/eclipse/RestartFileView.hpp>

#include <opm/io/eclipse/rst/connection.hpp>
#include <opm/io/eclipse/rst/group.hpp>
#include <opm/io/eclipse/rst/header.hpp>
#include <opm/io/eclipse/rst/segment.hpp>
#include <opm/io/eclipse/rst/state.hpp>
#include <opm/io/eclipse/rst/well.hpp>

#include <opm/output/data/Wells.hpp>
#include <opm/output/eclipse/AggregateWellData.hpp>
#include <opm/output/eclipse/AggregateConnectionData.hpp>
#include <opm/output/eclipse/AggregateGroupData.hpp>
#include <opm/output/eclipse/VectorItems/intehead.hpp>
#include <opm/output/eclipse/VectorItems/well.hpp>
#include <opm/output/eclipse/WriteRestartHelpers.hpp>

#include <opm/input/eclipse/EclipseState/EclipseState.hpp>
#include <opm/input/eclipse/EclipseState/TracerConfig.hpp>
#include <opm/input/eclipse/Schedule/Action/State.hpp>
#include <opm/input/eclipse/Schedule/Schedule.hpp>
#include <opm/input/eclipse/Schedule/SummaryState.hpp>
#include <opm/input/eclipse/Schedule/Well/Well.hpp>
#include <opm/input/eclipse/Schedule/Well/WellEconProductionLimits.hpp>
#include <opm/input/eclipse/Schedule/Well/WellTestState.hpp>
#include <opm/input/eclipse/Schedule/Well/WVFPEXP.hpp>

#include <opm/input/eclipse/Units/UnitSystem.hpp>

#include <opm/input/eclipse/Deck/Deck.hpp>
#include <opm/input/eclipse/Parser/Parser.hpp>
#include <opm/input/eclipse/Python/Python.hpp>

#include <opm/common/utility/TimeService.hpp>

#include <tests/WorkArea.hpp>

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {
    struct SimulationCase
    {
        explicit SimulationCase(const Opm::Deck& deck)
            : es    { deck }
            , grid  { deck }
            , sched { deck, es, std::make_shared<Opm::Python>() }
        {}

        // Order requirement: 'es' must be declared/initialised before 'sched'.
        Opm::EclipseState es;
        Opm::EclipseGrid  grid;
        Opm::Schedule     sched;
        Opm::Parser       parser;
    };

    Opm::Deck first_sim()
    {
        // Mostly copy of tests/FIRST_SIM.DATA
        const auto input = std::string {
            R"~(
RUNSPEC
OIL
GAS
WATER
DISGAS
VAPOIL
UNIFOUT
UNIFIN
DIMENS
 10 10 10 /

GRID
DXV
10*0.25 /
DYV
10*0.25 /
DZV
10*0.25 /
TOPS
100*0.25 /

PORO
1000*0.2 /
PERMX
1000*1 /
PERMY
1000*0.1 /
PERMZ
1000*0.01 /

SOLUTION


START             -- 0
1 NOV 1979 /

SCHEDULE
RPTRST
BASIC=1
/
DATES             -- 1
 10  OKT 2008 /
/
WELSPECS
      'OP_1'       'OP'   9   9 1*     'OIL' 1*      1*  1*   1*  1*   1*  1*  /
      'OP_2'       'OP'   9   9 1*     'OIL' 1*      1*  1*   1*  1*   1*  1*  /
/
COMPDAT
      'OP_1'  9  9   1   1 'OPEN' 1*   32.948   0.311  3047.839 1*  1*  'X'  22.100 /
      'OP_2'  9  9   2   2 'OPEN' 1*   46.825   0.311  4332.346 1*  1*  'X'  22.123 /
      'OP_1'  9  9   3   3 'OPEN' 1*   32.948   0.311  3047.839 1*  1*  'X'  22.100 /
/
WCONPROD
      'OP_1' 'OPEN' 'ORAT' 20000  4* 1000 /
/
WCONINJE
      'OP_2' 'GAS' 'OPEN' 'RATE' 100 200 400 /
/

WECON
-- Adapted from opm-tests/wecon_test/3D_WECON.DATA
-- Well_name  minOrate  minGrate  maxWCT  maxGOR  maxWGR  WOprocedure  flag  open_well  minEco  2maxWCT WOaction maxGLR minLrate maxT
  'OP_1'      1.0       800       0.1     321.09  1.0e-3  CON          YES    1*         POTN    0.8     WELL     300.0   50      1* /
/

DATES             -- 2
 20  JAN 2011 /
/
WELSPECS
      'OP_3'       'OP'   9   9 1*     'OIL' 1*      1*  1*   1*  1*   1*  1*  /
/
COMPDAT
      'OP_3'  9  9   1   1 'OPEN' 1*   32.948   0.311  3047.839 1*  1*  'X'  22.100 /
/
WCONPROD
      'OP_3' 'OPEN' 'ORAT' 20000  4* 1000 /
/
WCONINJE
      'OP_2' 'WATER' 'OPEN' 'RATE' 100 200 400 /
/

DATES             -- 3
 15  JUN 2013 /
/
COMPDAT
      'OP_2'  9  9   3  9 'OPEN' 1*   32.948   0.311  3047.839 1*  1*  'X'  22.100 /
      'OP_1'  9  9   7  7 'SHUT' 1*   32.948   0.311  3047.839 1*  1*  'X'  22.100 /
/

WGRUPCON
 'OP_2'  YES   0.5  OIL  1.0 /
 'OP_3'  NO    1*   RES  0.625 /
/

DATES             -- 4
 22  APR 2014 /
/
WELSPECS
      'OP_4'       'OP'   9   9 1*     'OIL' 1*      1*  1*   1*  1*   1*  1*  /
/
COMPDAT
      'OP_4'  9  9   3  9 'OPEN' 1*   32.948   0.311  3047.839 1*  1*  'X'  22.100 /
      'OP_3'  9  9   3  9 'OPEN' 1*   32.948   0.311  3047.839 1*  1*  'X'  22.100 /
/
WCONPROD
      'OP_4' 'OPEN' 'ORAT' 20000  4* 1000 /
/

WVFPEXP
 'OP_1' 1*    'YES' /
 'OP_2' 'EXP' 'NO'  'YES1' /
 'OP_3' 'EXP' 'YES' 'YES2' /
/

DATES             -- 5
 30  AUG 2014 /
/
WELSPECS
      'OP_5'       'OP'   9   9 1*     'OIL' 1*      1*  1*   1*  1*   1*  1*  /
/
COMPDAT
      'OP_5'  9  9   3  9 'OPEN' 1*   32.948   0.311  3047.839 1*  1*  'X'  22.100 /
/
WCONPROD
      'OP_5' 'OPEN' 'ORAT' 20000  4* 1000 /
/

DATES             -- 6
 15  SEP 2014 /
/
WCONPROD
      'OP_3' 'SHUT' 'ORAT' 20000  4* 1000 /
/

DATES             -- 7
 9  OCT 2014 /
/
WELSPECS
      'OP_6'       'OP'   9   9 1*     'OIL' 1*      1*  1*   1*  1*   1*  1*  /
/
COMPDAT
      'OP_6'  9  9   3  9 'OPEN' 1*   32.948   0.311  3047.839 1*  1*  'X'  22.100 /
/
WCONPROD
      'OP_6' 'OPEN' 'ORAT' 20000  4* 1000 /
/
TSTEP            -- 8
10 /
)~" };

        return Opm::Parser{}.parseString(input);
    }

    void writeRstFile(const SimulationCase& simCase,
                      const std::string&    baseName,
                      const std::size_t     rptStep)
    {
        const auto& units    = simCase.es.getUnits();
        const auto  sim_step = rptStep - 1;

        const auto sumState     = Opm::SummaryState { Opm::TimeService::now() };
        const auto action_state = Opm::Action::State{};
        const auto wtest_state  = Opm::WellTestState{};

        const auto ih = Opm::RestartIO::Helpers::
            createInteHead(simCase.es, simCase.grid, simCase.sched,
                           0, sim_step, sim_step, sim_step);

        const auto lh = Opm::RestartIO::Helpers::createLogiHead(simCase.es);
        const auto dh = Opm::RestartIO::Helpers::
            createDoubHead(simCase.es, simCase.sched,
                           sim_step, sim_step + 1, 0, 0);

        auto wellData = Opm::RestartIO::Helpers::AggregateWellData(ih);
        wellData.captureDeclaredWellData(simCase.sched, simCase.es.tracer(),
                                         sim_step, action_state,
                                         wtest_state, sumState, ih);
        wellData.captureDynamicWellData(simCase.sched, simCase.es.tracer(),
                                        sim_step, {}, sumState);

        auto connectionData = Opm::RestartIO::Helpers::AggregateConnectionData(ih);
        connectionData.captureDeclaredConnData(simCase.sched, simCase.grid, units,
                                               {}, sumState, sim_step);

        auto groupData = Opm::RestartIO::Helpers::AggregateGroupData(ih);
        groupData.captureDeclaredGroupData(simCase.sched, units, sim_step, sumState, ih);

        const auto outputDir = std::string { "./" };

        Opm::EclIO::OutputStream::Restart rstFile {
            Opm::EclIO::OutputStream::ResultSet {outputDir, baseName},
            static_cast<int>(rptStep),
            Opm::EclIO::OutputStream::Formatted {false},
            Opm::EclIO::OutputStream::Unified {true}
        };

        rstFile.write("INTEHEAD", ih);
        rstFile.write("DOUBHEAD", dh);
        rstFile.write("LOGIHEAD", lh);

        rstFile.write("IGRP", groupData.getIGroup());
        rstFile.write("SGRP", groupData.getSGroup());
        rstFile.write("XGRP", groupData.getXGroup());
        rstFile.write("ZGRP", groupData.getZGroup());

        rstFile.write("IWEL", wellData.getIWell());
        rstFile.write("SWEL", wellData.getSWell());
        rstFile.write("XWEL", wellData.getXWell());
        rstFile.write("ZWEL", wellData.getZWell());

        rstFile.write("ICON", connectionData.getIConn());
        rstFile.write("SCON", connectionData.getSConn());
        rstFile.write("XCON", connectionData.getXConn());
    }

    Opm::RestartIO::RstState
    loadRestart(const SimulationCase& simCase,
                const std::string&    baseName,
                const std::size_t     rptStep)
    {
        auto rstFile = std::make_shared<Opm::EclIO::ERst>(baseName + ".UNRST");
        auto rstView = std::make_shared<Opm::EclIO::RestartFileView>
            (std::move(rstFile), rptStep);

        return Opm::RestartIO::RstState::
            load(std::move(rstView), simCase.es.runspec(), simCase.parser);
    }

    Opm::RestartIO::RstState
    makeRestartState(const SimulationCase& simCase,
                     const std::string&    baseName,
                     const std::size_t     rptStep,
                     const std::string&    workArea)
    {
        // Recall: Constructor changes working directory of current process,
        // destructor restores original working directory.  The non-trivial
        // destructor also means that this object will not be tagged as
        // "unused" in release builds.
        WorkArea work_area{workArea};

        writeRstFile(simCase, baseName, rptStep);
        return loadRestart(simCase, baseName, rptStep);
    }
} // Anonymous namespace

// =====================================================================

BOOST_AUTO_TEST_CASE(group_test)
{
    const auto simCase = SimulationCase{first_sim()};
    const auto& units = simCase.es.getUnits();
    // Report Step 2: 2011-01-20 --> 2013-06-15
    const auto rptStep = std::size_t{2};
    const auto sim_step = rptStep - 1;
    Opm::SummaryState sumState(Opm::TimeService::now());

    const auto ih = Opm::RestartIO::Helpers::createInteHead(simCase.es,
                                                            simCase.grid,
                                                            simCase.sched,
                                                            0,
                                                            sim_step,
                                                            sim_step,
                                                            sim_step);

    const auto lh = Opm::RestartIO::Helpers::createLogiHead(simCase.es);

    const auto dh = Opm::RestartIO::Helpers::createDoubHead(simCase.es,
                                                            simCase.sched,
                                                            sim_step,
                                                            sim_step+1,
                                                            0, 0);

    auto groupData = Opm::RestartIO::Helpers::AggregateGroupData(ih);
    groupData.captureDeclaredGroupData(simCase.sched, units, sim_step, sumState, ih);

    const auto& igrp = groupData.getIGroup();
    const auto& sgrp = groupData.getSGroup();
    const auto& xgrp = groupData.getXGroup();
    const auto& zgrp8 = groupData.getZGroup();

    Opm::UnitSystem unit_system(Opm::UnitSystem::UnitType::UNIT_TYPE_METRIC);
    std::vector<std::string> zgrp;
    for (const auto& s8: zgrp8)
        zgrp.push_back(s8.c_str());

    Opm::RestartIO::RstHeader header(simCase.es.runspec(), unit_system,ih,lh,dh);
    for (int ig=0; ig < header.ngroup; ig++) {
        std::size_t zgrp_offset = ig * header.nzgrpz;
        std::size_t igrp_offset = ig * header.nigrpz;
        std::size_t sgrp_offset = ig * header.nsgrpz;
        std::size_t xgrp_offset = ig * header.nxgrpz;

        Opm::RestartIO::RstGroup group(unit_system,
                                       header,
                                       zgrp.data() + zgrp_offset,
                                       igrp.data() + igrp_offset,
                                       sgrp.data() + sgrp_offset,
                                       xgrp.data() + xgrp_offset);
    }
}

BOOST_AUTO_TEST_CASE(State_test)
{
    const auto simCase = SimulationCase{first_sim()};

    // Report Step 2: 2011-01-20 --> 2013-06-15
    const auto rptStep  = std::size_t{4};
    const auto baseName = std::string { "TEST_UDQRST" };

    const auto state =
        makeRestartState(simCase, baseName, rptStep,
                         "test_rstate");

    const auto& well = state.get_well("OP_3");
    BOOST_CHECK_THROW(well.segment(10), std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(Well_Economic_Limits)
{
    const auto simCase = SimulationCase{first_sim()};

    // Report Step 2: 2011-01-20 --> 2013-06-15
    const auto rptStep  = std::size_t{4};
    const auto baseName = std::string { "TEST_RST_WECON" };

    const auto state =
        makeRestartState(simCase, baseName, rptStep,
                         "test_rst_wecon");

    const auto& op_1 = state.get_well("OP_1");

    namespace Limits = Opm::RestartIO::Helpers::VectorItems::
        IWell::Value::EconLimit;

    BOOST_CHECK_MESSAGE(op_1.econ_workover_procedure == Limits::WOProcedure::Con,
                        "Well '" << op_1.name << "' must have work-over procedure 'Con'");
    BOOST_CHECK_MESSAGE(op_1.econ_workover_procedure_2 == Limits::WOProcedure::StopOrShut,
                        "Well '" << op_1.name << "' must have secondary work-over "
                        "procedure 'StopOrShut' (WELL)");
    BOOST_CHECK_MESSAGE(op_1.econ_limit_end_run == Limits::EndRun::Yes,
                        "Well '" << op_1.name << "' must have end-run flag 'Yes'");
    BOOST_CHECK_MESSAGE(op_1.econ_limit_quantity == Limits::Quantity::Potential,
                        "Well '" << op_1.name << "' must have limiting "
                        "quantity 'Potential'");

    BOOST_CHECK_CLOSE(op_1.econ_limit_min_oil  ,   1.0f / 86400.0f, 1.0e-7f);
    BOOST_CHECK_CLOSE(op_1.econ_limit_min_gas  , 800.0f / 86400.0f, 1.0e-7f);
    BOOST_CHECK_CLOSE(op_1.econ_limit_max_wct  ,   0.1f           , 1.0e-7f);
    BOOST_CHECK_CLOSE(op_1.econ_limit_max_gor  , 321.09f          , 1.0e-7f);
    BOOST_CHECK_CLOSE(op_1.econ_limit_max_wgr  ,   1.0e-3f        , 1.0e-7f);
    BOOST_CHECK_CLOSE(op_1.econ_limit_max_wct_2,   0.8f           , 1.0e-7f);
    BOOST_CHECK_CLOSE(op_1.econ_limit_min_liq  ,  50.0f / 86400.0f, 1.0e-7f);

    const auto& op_2 = state.get_well("OP_2");

    BOOST_CHECK_MESSAGE(op_2.econ_workover_procedure == Limits::WOProcedure::None,
                        "Well '" << op_2.name << "' must have work-over procedure 'None'");
    BOOST_CHECK_MESSAGE(op_2.econ_workover_procedure_2 == Limits::WOProcedure::None,
                        "Well '" << op_2.name << "' must have secondary work-over "
                        "procedure 'None'");
    BOOST_CHECK_MESSAGE(op_2.econ_limit_end_run == Limits::EndRun::No,
                        "Well '" << op_2.name << "' must have end-run flag 'No'");
    BOOST_CHECK_MESSAGE(op_2.econ_limit_quantity == Limits::Quantity::Rate,
                        "Well '" << op_2.name << "' must have limiting "
                        "quantity 'Rate'");

    BOOST_CHECK_CLOSE(op_2.econ_limit_min_oil  ,   0.0f    , 1.0e-7f);
    BOOST_CHECK_CLOSE(op_2.econ_limit_min_gas  ,   0.0f    , 1.0e-7f);
    BOOST_CHECK_CLOSE(op_2.econ_limit_max_wct  ,   1.0e+20f, 1.0e-7f); // No limit => infinity
    BOOST_CHECK_CLOSE(op_2.econ_limit_max_gor  ,   1.0e+20f, 1.0e-7f);
    BOOST_CHECK_CLOSE(op_2.econ_limit_max_wgr  ,   1.0e+20f, 1.0e-7f);
    BOOST_CHECK_CLOSE(op_2.econ_limit_max_wct_2,   0.0f    , 1.0e-7f); // No limit => 0.0
    BOOST_CHECK_CLOSE(op_2.econ_limit_min_liq  ,   0.0f    , 1.0e-7f);
}

BOOST_AUTO_TEST_CASE(Construct_Well_Economic_Limits_Object)
{
    const auto simCase = SimulationCase{first_sim()};

    // Report Step 2: 2011-01-20 --> 2013-06-15
    const auto rptStep  = std::size_t{4};
    const auto baseName = std::string { "TEST_RST_WECON" };

    const auto state =
        makeRestartState(simCase, baseName, rptStep,
                         "test_rst_wecon");

    const auto op_1 = std::string { "OP_1" };
    const auto op_2 = std::string { "OP_2" };
    const auto limit_op_1 = Opm::WellEconProductionLimits{ state.get_well(op_1) };
    const auto limit_op_2 = Opm::WellEconProductionLimits{ state.get_well(op_2) };

    BOOST_CHECK_MESSAGE(limit_op_1.requireWorkover(),
                        "Well '" << op_1 << "' must have a primary work-over procedure");
    BOOST_CHECK_MESSAGE(limit_op_1.workover() == Opm::WellEconProductionLimits::EconWorkover::CON,
                        "Well '" << op_1 << "' must have work-over procedure 'CON'");

    BOOST_CHECK_MESSAGE(limit_op_1.requireSecondaryWorkover(),
                        "Well '" << op_1 << "' must have a secondary work-over procedure");
    BOOST_CHECK_MESSAGE(limit_op_1.workoverSecondary() == Opm::WellEconProductionLimits::EconWorkover::WELL,
                        "Well '" << op_1 << "' must have secondary work-over procedure 'WELL'");

    BOOST_CHECK_MESSAGE(limit_op_1.endRun(), "Well '" << op_1 << "' must have end-run flag 'true'");

    BOOST_CHECK_MESSAGE(limit_op_1.quantityLimit() == Opm::WellEconProductionLimits::QuantityLimit::POTN,
                        "Well '" << op_1 << "' must have limiting quantity 'POTN'");

    BOOST_CHECK_MESSAGE(limit_op_1.onAnyEffectiveLimit(),
                        "Well '" << op_1 << "' must have active economic limits");

    BOOST_CHECK_MESSAGE(limit_op_1.onAnyRatioLimit(),
                        "Well '" << op_1 << "' must have active economic limits on ratios");

    BOOST_CHECK_MESSAGE(limit_op_1.onAnyRateLimit(),
                        "Well '" << op_1 << "' must have active economic limits on rates");

    BOOST_CHECK_MESSAGE(limit_op_1.onMinOilRate(),
                        "Well '" << op_1 << "' must have active "
                        "economic limits on minimum oil rate");
    BOOST_CHECK_CLOSE(limit_op_1.minOilRate(), 1.0 / 86400.0, 1.0e-5);

    BOOST_CHECK_MESSAGE(limit_op_1.onMinGasRate(),
                        "Well '" << op_1 << "' must have active "
                        "economic limits on minimum gas rate");
    BOOST_CHECK_CLOSE(limit_op_1.minGasRate(), 800.0 / 86400.0, 1.0e-5);

    BOOST_CHECK_MESSAGE(limit_op_1.onMaxWaterCut(),
                        "Well '" << op_1 << "' must have active "
                        "economic limits on maximum water-cut");
    BOOST_CHECK_CLOSE(limit_op_1.maxWaterCut(), 0.1, 1.0e-5);

    BOOST_CHECK_MESSAGE(limit_op_1.onMaxGasOilRatio(),
                        "Well '" << op_1 << "' must have active "
                        "economic limits on maximum gas-oil ratio");
    BOOST_CHECK_CLOSE(limit_op_1.maxGasOilRatio(), 321.09, 1.0e-5);

    BOOST_CHECK_MESSAGE(limit_op_1.onMaxWaterGasRatio(),
                        "Well '" << op_1 << "' must have active "
                        "economic limits on maximum water-gas ratio");
    BOOST_CHECK_CLOSE(limit_op_1.maxWaterGasRatio(), 1.0e-3, 1.0e-5);

    BOOST_CHECK_MESSAGE(limit_op_1.onSecondaryMaxWaterCut(),
                        "Well '" << op_1 << "' must have active "
                        "economic limits on maximum secondary water-cut");
    BOOST_CHECK_CLOSE(limit_op_1.maxSecondaryMaxWaterCut(), 0.8, 1.0e-5);

    BOOST_CHECK_MESSAGE(limit_op_1.onMinLiquidRate(),
                        "Well '" << op_1 << "' must have active "
                        "economic limits on minimum liquid rate");
    BOOST_CHECK_CLOSE(limit_op_1.minLiquidRate(), 50.0 / 86400.0, 1.0e-5);

    BOOST_CHECK_MESSAGE(! limit_op_1.onMaxGasLiquidRatio(),
                        "Well '" << op_1 << "' must NOT have active "
                        "economic limits on maximum gas-liquid ratio");

    BOOST_CHECK_MESSAGE(! limit_op_1.onMaxTemperature(),
                        "Well '" << op_1 << "' must NOT have active "
                        "economic limits on maximum temperature");

    BOOST_CHECK_MESSAGE(! limit_op_1.onMinReservoirFluidRate(),
                        "Well '" << op_1 << "' must NOT have active "
                        "economic limits on minimum reservoir flow rate");

    BOOST_CHECK_MESSAGE(! limit_op_1.validFollowonWell(),
                        "Well '" << op_1 << "' must NOT have an "
                        "active follow-on well");

    // =======================================================================

    BOOST_CHECK_MESSAGE(! limit_op_2.requireWorkover(),
                        "Well '" << op_2 << "' must NOT have a primary work-over procedure");

    BOOST_CHECK_MESSAGE(! limit_op_2.requireSecondaryWorkover(),
                        "Well '" << op_2 << "' must NOT have a secondary work-over procedure");

    BOOST_CHECK_MESSAGE(! limit_op_2.endRun(), "Well '" << op_2 << "' must have end-run flag 'false'");

    BOOST_CHECK_MESSAGE(limit_op_2.quantityLimit() == Opm::WellEconProductionLimits::QuantityLimit::RATE,
                        "Well '" << op_2 << "' must have limiting quantity 'RATE'");

    BOOST_CHECK_MESSAGE(! limit_op_2.onAnyEffectiveLimit(),
                        "Well '" << op_2 << "' must NOT have active economic limits");

    BOOST_CHECK_MESSAGE(! limit_op_2.onAnyRatioLimit(),
                        "Well '" << op_2 << "' must NOT have active economic limits on ratios");

    BOOST_CHECK_MESSAGE(! limit_op_2.onAnyRateLimit(),
                        "Well '" << op_2 << "' must NOT have active economic limits on rates");

    BOOST_CHECK_MESSAGE(! limit_op_2.onMinOilRate(),
                        "Well '" << op_2 << "' must NOT have active "
                        "economic limits on minimum oil rate");

    BOOST_CHECK_MESSAGE(! limit_op_2.onMinGasRate(),
                        "Well '" << op_2 << "' must NOT have active "
                        "economic limits on minimum gas rate");

    BOOST_CHECK_MESSAGE(! limit_op_2.onMaxWaterCut(),
                        "Well '" << op_2 << "' must NOT have active "
                        "economic limits on maximum water-cut");

    BOOST_CHECK_MESSAGE(! limit_op_2.onMaxGasOilRatio(),
                        "Well '" << op_2 << "' must NOT have active "
                        "economic limits on maximum gas-oil ratio");

    BOOST_CHECK_MESSAGE(! limit_op_2.onMaxWaterGasRatio(),
                        "Well '" << op_2 << "' must NOT have active "
                        "economic limits on maximum water-gas ratio");

    BOOST_CHECK_MESSAGE(! limit_op_2.onSecondaryMaxWaterCut(),
                        "Well '" << op_2 << "' must NOT have active "
                        "economic limits on maximum secondary water-cut");

    BOOST_CHECK_MESSAGE(! limit_op_2.onMinLiquidRate(),
                        "Well '" << op_2 << "' must NOT have active "
                        "economic limits on minimum liquid rate");

    BOOST_CHECK_MESSAGE(! limit_op_2.onMaxGasLiquidRatio(),
                        "Well '" << op_2 << "' must NOT have active "
                        "economic limits on maximum gas-liquid ratio");

    BOOST_CHECK_MESSAGE(! limit_op_2.onMaxTemperature(),
                        "Well '" << op_2 << "' must NOT have active "
                        "economic limits on maximum temperature");

    BOOST_CHECK_MESSAGE(! limit_op_2.onMinReservoirFluidRate(),
                        "Well '" << op_2 << "' must NOT have active "
                        "economic limits on minimum reservoir flow rate");

    BOOST_CHECK_MESSAGE(! limit_op_2.validFollowonWell(),
                        "Well '" << op_2 << "' must NOT have an "
                        "active follow-on well");
}

BOOST_AUTO_TEST_CASE(Well_Guide_Rates_Group_Control)
{
    const auto simCase = SimulationCase{first_sim()};

    const auto rptStep  = std::size_t{4};
    const auto baseName = std::string { "TEST_RST_WGRUPCON" };

    const auto state =
        makeRestartState(simCase, baseName, rptStep, "test_rst_wgrupcon");

    const auto& op_2 = state.get_well("OP_2");
    const auto& op_3 = state.get_well("OP_3");

    namespace WGrupCon = Opm::RestartIO::Helpers::VectorItems::
        IWell::Value::WGrupCon;

    BOOST_CHECK_MESSAGE(op_2.group_controllable_flag == WGrupCon::Controllable::Yes,
                        "Well '" << op_2.name << "' must be group controllable");
    BOOST_CHECK_MESSAGE(op_2.grupcon_gr_phase == WGrupCon::GRPhase::Oil,
                        "Well '" << op_2.name << "' must have guiderate phase 'Oil'");
    BOOST_CHECK_CLOSE(op_2.grupcon_gr_value, 0.5f, 1.0e-7f);
    BOOST_CHECK_CLOSE(op_2.grupcon_gr_scaling, 1.0f, 1.0e-7f);

    BOOST_CHECK_MESSAGE(op_3.group_controllable_flag == WGrupCon::Controllable::No,
                        "Well '" << op_3.name << "' must NOT be group controllable");
    BOOST_CHECK_MESSAGE(op_3.grupcon_gr_phase == WGrupCon::GRPhase::ReservoirVolumeRate,
                        "Well '" << op_3.name << "' must have guiderate phase 'ReservoirVolumeRate'");
    BOOST_CHECK_CLOSE(op_3.grupcon_gr_value, -1.0e+20f, 1.0e-7f);
    BOOST_CHECK_CLOSE(op_3.grupcon_gr_scaling, 0.625f, 1.0e-7f);
}

BOOST_AUTO_TEST_CASE(Construct_Well_Guide_Rates_Group_Control_Object)
{
    const auto simCase = SimulationCase{first_sim()};

    const auto rptStep  = std::size_t{4};
    const auto baseName = std::string { "TEST_RST_WGRUPCON" };

    const auto state =
        makeRestartState(simCase, baseName, rptStep, "test_rst_wgrupcon");

    auto makeRestartWell = [&state](const std::string& well_name)
    {
        return Opm::Well {
            state.get_well(well_name),
            static_cast<int>(rptStep),
            Opm::TracerConfig{},
            Opm::UnitSystem::newMETRIC(),
            1.0e+20
        };
    };

    const auto op_2 = makeRestartWell("OP_2");
    const auto op_3 = makeRestartWell("OP_3");

    BOOST_CHECK_MESSAGE(op_2.isAvailableForGroupControl(),
                        "Well '" << op_2.name() << "' must be group controllable");
    BOOST_CHECK_MESSAGE(op_2.getRawGuideRatePhase() == Opm::Well::GuideRateTarget::OIL,
                        "Well '" << op_2.name() << "' must have guiderate phase 'OIL'");
    BOOST_CHECK_CLOSE(op_2.getGuideRate(), 0.5, 1.0e-7);
    BOOST_CHECK_CLOSE(op_2.getGuideRateScalingFactor(), 1.0, 1.0e-7);

    // =======================================================================

    BOOST_CHECK_MESSAGE(! op_3.isAvailableForGroupControl(),
                        "Well '" << op_3.name() << "' must NOT be group controllable");
    BOOST_CHECK_MESSAGE(op_3.getRawGuideRatePhase() == Opm::Well::GuideRateTarget::RES,
                        "Well '" << op_3.name() << "' must have guiderate phase 'RES'");
    BOOST_CHECK_CLOSE(op_3.getGuideRate(), -1.0, 1.0e-7);
    BOOST_CHECK_CLOSE(op_3.getGuideRateScalingFactor(), 0.625, 1.0e-7);
}

BOOST_AUTO_TEST_CASE(Explicit_THP_Control_Options)
{
    namespace WVfpExp = Opm::RestartIO::Helpers::VectorItems::
        IWell::Value::WVfpExp;

    const auto simCase = SimulationCase{first_sim()};

    const auto rptStep  = std::size_t{5};
    const auto baseName = std::string { "TEST_RST_WVFPEXP" };

    const auto state =
        makeRestartState(simCase, baseName, rptStep, "test_rst_wvfpexp");

    const auto& op_1 = state.get_well("OP_1");
    const auto& op_2 = state.get_well("OP_2");
    const auto& op_3 = state.get_well("OP_3");
    const auto& op_4 = state.get_well("OP_4");

    BOOST_CHECK_EQUAL(op_1.thp_lookup_procedure_vfptable, WVfpExp::Lookup::Implicit);
    BOOST_CHECK_EQUAL(op_1.close_if_thp_stabilised,
                      static_cast<int>(WVfpExp::CloseStabilised::Yes));
    BOOST_CHECK_EQUAL(op_1.prevent_thpctrl_if_unstable,
                      static_cast<int>(WVfpExp::PreventTHP::No));

    BOOST_CHECK_EQUAL(op_2.thp_lookup_procedure_vfptable, WVfpExp::Lookup::Explicit);
    BOOST_CHECK_EQUAL(op_2.close_if_thp_stabilised,
                      static_cast<int>(WVfpExp::CloseStabilised::No));
    BOOST_CHECK_EQUAL(op_2.prevent_thpctrl_if_unstable,
                      static_cast<int>(WVfpExp::PreventTHP::Yes1));

    BOOST_CHECK_EQUAL(op_3.thp_lookup_procedure_vfptable, WVfpExp::Lookup::Explicit);
    BOOST_CHECK_EQUAL(op_3.close_if_thp_stabilised,
                      static_cast<int>(WVfpExp::CloseStabilised::Yes));
    BOOST_CHECK_EQUAL(op_3.prevent_thpctrl_if_unstable,
                      static_cast<int>(WVfpExp::PreventTHP::Yes2));

    BOOST_CHECK_EQUAL(op_4.thp_lookup_procedure_vfptable, WVfpExp::Lookup::Implicit);
    BOOST_CHECK_EQUAL(op_4.close_if_thp_stabilised,
                      static_cast<int>(WVfpExp::CloseStabilised::No));
    BOOST_CHECK_EQUAL(op_4.prevent_thpctrl_if_unstable,
                      static_cast<int>(WVfpExp::PreventTHP::No));
}

BOOST_AUTO_TEST_CASE(Construct_Well_Explicit_THP_Control_Options_Object)
{
    const auto simCase = SimulationCase{first_sim()};

    const auto rptStep  = std::size_t{5};
    const auto baseName = std::string { "TEST_RST_WVFPEXP" };

    const auto state =
        makeRestartState(simCase, baseName, rptStep, "test_rst_wvfpexp");

    auto makeTHPOptions = [&state](const std::string& well_name)
    {
        return Opm::Well {
            state.get_well(well_name),
            static_cast<int>(rptStep),
            Opm::TracerConfig{},
            Opm::UnitSystem::newMETRIC(),
            1.0e+20
        }.getWVFPEXP();
    };

    const auto op_1 = makeTHPOptions("OP_1");
    const auto op_2 = makeTHPOptions("OP_2");
    const auto op_3 = makeTHPOptions("OP_3");
    const auto op_4 = makeTHPOptions("OP_4");

    // 1* YES /
    BOOST_CHECK_MESSAGE(! op_1.explicit_lookup(), "Well 'OP_1' must have IMPLICIT THP lookup");
    BOOST_CHECK_MESSAGE(  op_1.shut(),            "Well 'OP_1' must SHUT if operating in stabilised region");
    BOOST_CHECK_MESSAGE(! op_1.prevent(),         "Well 'OP_1' must NOT prevent switching to THP control when constrained to unstable VFP table region");
    BOOST_CHECK_MESSAGE(! op_1.report_first(),    "Well 'OP_1' must NOT report first time THP control switching prevented");
    BOOST_CHECK_MESSAGE(! op_1.report_every(),    "Well 'OP_1' must NOT report every time THP control switching prevented");

    // EXP NO YES1 /
    BOOST_CHECK_MESSAGE(  op_2.explicit_lookup(), "Well 'OP_2' must have EXPLICIT THP lookup");
    BOOST_CHECK_MESSAGE(! op_2.shut(),            "Well 'OP_2' must remain open if operating in stabilised region");
    BOOST_CHECK_MESSAGE(  op_2.prevent(),         "Well 'OP_2' must prevent switching to THP control when constrained to unstable VFP table region");
    BOOST_CHECK_MESSAGE(  op_2.report_first(),    "Well 'OP_2' must report first time THP control switching prevented");
    BOOST_CHECK_MESSAGE(! op_2.report_every(),    "Well 'OP_2' must NOT report every time THP control switching prevented");

    // EXP YES YES2 /
    BOOST_CHECK_MESSAGE(  op_3.explicit_lookup(), "Well 'OP_3' must have EXPLICIT THP lookup");
    BOOST_CHECK_MESSAGE(  op_3.shut(),            "Well 'OP_3' must SHUT if operating in stabilised region");
    BOOST_CHECK_MESSAGE(  op_3.prevent(),         "Well 'OP_3' must prevent switching to THP control when constrained to unstable VFP table region");
    BOOST_CHECK_MESSAGE(! op_3.report_first(),    "Well 'OP_3' must NOT report first time THP control switching prevented");
    BOOST_CHECK_MESSAGE(  op_3.report_every(),    "Well 'OP_3' must report every time THP control switching prevented");

    // All defaults.
    BOOST_CHECK_MESSAGE(! op_4.explicit_lookup(), "Well 'OP_4' must have IMPLICIT THP lookup");
    BOOST_CHECK_MESSAGE(! op_4.shut(),            "Well 'OP_4' must remain open if operating in stabilised region");
    BOOST_CHECK_MESSAGE(! op_4.prevent(),         "Well 'OP_4' must NOT prevent switching to THP control when constrained to unstable VFP table region");
    BOOST_CHECK_MESSAGE(! op_4.report_first(),    "Well 'OP_4' must NOT report first time THP control switching prevented");
    BOOST_CHECK_MESSAGE(! op_4.report_every(),    "Well 'OP_4' must NOT report every time THP control switching prevented");
}
