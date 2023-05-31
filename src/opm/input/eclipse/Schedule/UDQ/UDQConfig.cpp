/*
  Copyright 2019 Equinor ASA.

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

#include <opm/input/eclipse/Schedule/UDQ/UDQConfig.hpp>

#include <opm/io/eclipse/rst/state.hpp>

#include <opm/common/OpmLog/KeywordLocation.hpp>
#include <opm/common/utility/OpmInputError.hpp>

#include <opm/input/eclipse/Schedule/SummaryState.hpp>
#include <opm/input/eclipse/Schedule/UDQ/UDQEnums.hpp>
#include <opm/input/eclipse/Schedule/UDQ/UDQInput.hpp>
#include <opm/input/eclipse/Schedule/UDQ/UDQState.hpp>

#include <opm/input/eclipse/Parser/ParserKeywords/U.hpp> // UDQ

#include <opm/input/eclipse/Deck/DeckRecord.hpp>

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <vector>

#include <fmt/format.h>

namespace {
    std::string strip_quotes(const std::string& s)
    {
        if (s[0] == '\'') {
            return s.substr(1, s.size() - 2);
        }
        else {
            return s;
        }
    }
} // Anonymous namespace

namespace Opm {

    UDQConfig::UDQConfig(const UDQParams& params)
        : udq_params(params)
        , udqft(this->udq_params)
    {}

    UDQConfig::UDQConfig(const UDQParams& params, const RestartIO::RstState& rst_state)
        : UDQConfig(params)
    {
        for (const auto& rst_udq : rst_state.udqs) {
            if (rst_udq.is_define()) {
                KeywordLocation location("UDQ", "Restart file", 0);
                this->add_define(rst_udq.name,
                                 location,
                                 { rst_udq.expression() },
                                 rst_state.header.report_step);
            }
            else {
                this->add_assign(rst_udq.name,
                                 rst_udq.assign_selector(),
                                 rst_udq.assign_value(),
                                 rst_state.header.report_step);
            }

            this->add_unit(rst_udq.name, rst_udq.unit);
        }
    }

    UDQConfig UDQConfig::serializationTestObject()
    {
        UDQConfig result;
        result.udq_params = UDQParams::serializationTestObject();
        result.udqft = UDQFunctionTable(result.udq_params);
        result.m_definitions = {{"test1", UDQDefine::serializationTestObject()}};
        result.m_assignments = {{"test2", UDQAssign::serializationTestObject()}};
        result.units = {{"test3", "test4"}};
        result.input_index.insert({"test5", UDQIndex::serializationTestObject()});
        result.type_count = {{UDQVarType::SCALAR, 5}};

        return result;
    }

    const UDQParams& UDQConfig::params() const
    {
        return this->udq_params;
    }

    void UDQConfig::add_node(const std::string& quantity, const UDQAction action)
    {
        auto index_iter = this->input_index.find(quantity);
        if (this->input_index.find(quantity) == this->input_index.end()) {
            auto var_type = UDQ::varType(quantity);
            auto insert_index = this->input_index.size();

            this->type_count[var_type] += 1;
            this->input_index[quantity] = UDQIndex(insert_index, this->type_count[var_type], action, var_type);
        }
        else {
            index_iter->second.action = action;
        }
    }

    void UDQConfig::add_assign(const std::string&              quantity,
                               const std::vector<std::string>& selector,
                               const double                    value,
                               const std::size_t               report_step)
    {
        this->add_node(quantity, UDQAction::ASSIGN);

        auto assignment = this->m_assignments.find(quantity);
        if (assignment == this->m_assignments.end()) {
            this->m_assignments.emplace(std::piecewise_construct,
                                        std::forward_as_tuple(quantity),
                                        std::forward_as_tuple(quantity, selector,
                                                              value, report_step));
        }
        else {
            assignment->second.add_record(selector, value, report_step);
        }
    }

    void UDQConfig::add_assign(const std::string&                     quantity,
                               const std::unordered_set<std::string>& selector,
                               const double                           value,
                               const std::size_t                      report_step)
    {
        this->add_node(quantity, UDQAction::ASSIGN);

        auto assignment = this->m_assignments.find(quantity);
        if (assignment == this->m_assignments.end()) {
            this->m_assignments.emplace(std::piecewise_construct,
                                        std::forward_as_tuple(quantity),
                                        std::forward_as_tuple(quantity, selector,
                                                              value, report_step));
        }
        else {
            assignment->second.add_record(selector, value, report_step);
        }
    }

    void UDQConfig::add_define(const std::string&              quantity,
                               const KeywordLocation&          location,
                               const std::vector<std::string>& expression,
                               const std::size_t               report_step)
    {
        this->add_node(quantity, UDQAction::DEFINE);

        this->m_definitions.insert_or_assign(quantity,
                                             UDQDefine {
                                                 this->udq_params,
                                                 quantity,
                                                 report_step,
                                                 location,
                                                 expression
                                             });

        this->define_order.insert(quantity);
    }

    void UDQConfig::add_unit(const std::string& keyword, const std::string& quoted_unit)
    {
        const auto unit = strip_quotes(quoted_unit);
        const auto pair_ptr = this->units.find(keyword);
        if (pair_ptr != this->units.end()) {
            if (pair_ptr->second != unit) {
                throw std::invalid_argument("Illegal to change unit of UDQ keyword runtime");
            }

            return;
        }

        this->units[keyword] = unit;
    }

    void UDQConfig::add_update(const std::string&              keyword,
                               const std::size_t               report_step,
                               const KeywordLocation&          location,
                               const std::vector<std::string>& data)
    {
        if (data.empty()) {
            throw OpmInputError {
                fmt::format("Missing third item: ON|OFF|NEXT for UDQ update of {}", keyword),
                location
            };
        }

        if (this->m_definitions.count(keyword) == 0) {
            throw OpmInputError {
                fmt::format("UDQ variable: {} must be defined before you can use UPDATE", keyword),
                location
            };
        }

        const auto update_status = UDQ::updateType(data[0]);

        auto& define = this->m_definitions[keyword];
        define.update_status(update_status, report_step);
    }

    void UDQConfig::add_record(const DeckRecord&      record,
                               const KeywordLocation& location,
                               const std::size_t      report_step)
    {
        using KW = ParserKeywords::UDQ;

        const auto action = UDQ::actionType(record.getItem<KW::ACTION>().get<RawString>(0));
        const auto& quantity = record.getItem<KW::QUANTITY>().get<std::string>(0);
        const auto data = RawString::strings(record.getItem<KW::DATA>().getData<RawString>());

        if (action == UDQAction::UPDATE) {
            this->add_update(quantity, report_step, location, data);
        }
        else if (action == UDQAction::UNITS) {
            this->add_unit(quantity, data.front());
        }
        else if (action == UDQAction::ASSIGN) {
            const auto selector = std::vector<std::string>(data.begin(), data.end() - 1);
            const auto value = std::stod(data.back());
            this->add_assign(quantity, selector, value, report_step);
        }
        else if (action == UDQAction::DEFINE) {
            this->add_define(quantity, location, data, report_step);
        }
        else {
            throw std::runtime_error {
                "Unknown UDQ Operation " + std::to_string(static_cast<int>(action))
            };
        }
    }

    const UDQAssign& UDQConfig::assign(const std::string& key) const
    {
        return this->m_assignments.at(key);
    }

    const UDQDefine& UDQConfig::define(const std::string& key) const
    {
        return this->m_definitions.at(key);
    }

    UDQAction UDQConfig::action_type(const std::string& udq_key) const
    {
        auto action_iter = this->input_index.find(udq_key);
        return action_iter->second.action;
    }

    std::vector<UDQDefine> UDQConfig::definitions() const
    {
        std::vector<UDQDefine> ret;

        for (const auto& [key, index] : this->input_index) {
            if (index.action == UDQAction::DEFINE) {
                ret.push_back(this->m_definitions.at(key));
            }
        }

        return ret;
    }

    std::vector<UDQDefine> UDQConfig::definitions(const UDQVarType var_type) const
    {
        std::vector<UDQDefine> filtered_defines;

        for (const auto& [key, index] : this->input_index) {
            if (index.action == UDQAction::DEFINE) {
                const auto& udq_define = this->m_definitions.at(key);
                if (udq_define.var_type() == var_type) {
                    filtered_defines.push_back(udq_define);
                }
            }
        }

        return filtered_defines;
    }

    std::vector<UDQInput> UDQConfig::input() const
    {
        std::vector<UDQInput> res;

        for (const auto& [key, index] : this->input_index) {
            const auto u = this->has_unit(key)
                ? this->unit(key) : std::string{};

            if (index.action == UDQAction::DEFINE) {
                res.push_back(UDQInput(index, this->m_definitions.at(key), u));
            }
            else if (index.action == UDQAction::ASSIGN) {
                res.push_back(UDQInput(index, this->m_assignments.at(key), u));
            }
        }

        return res;
    }

    std::size_t UDQConfig::size() const
    {
        return std::count_if(this->input_index.begin(), this->input_index.end(),
                             [](const auto& index_pair)
                             {
                                 const auto action = index_pair.second.action;

                                 return (action == UDQAction::DEFINE)
                                     || (action == UDQAction::ASSIGN);
                             });
    }

    std::vector<UDQAssign> UDQConfig::assignments() const
    {
        std::vector<UDQAssign> ret;

        for (const auto& [key, input] : this->input_index) {
            if (input.action == UDQAction::ASSIGN) {
                ret.push_back(this->m_assignments.at(key));
            }
        }

        return ret;
    }

    std::vector<UDQAssign> UDQConfig::assignments(const UDQVarType var_type) const
    {
        std::vector<UDQAssign> filtered_assigns;

        for (const auto& index_pair : this->input_index) {
            auto assign_iter = this->m_assignments.find(index_pair.first);
            if ((assign_iter != this->m_assignments.end()) &&
                (assign_iter->second.var_type() == var_type))
            {
                filtered_assigns.push_back(assign_iter->second);
            }
        }

        return filtered_assigns;
    }

    const std::string& UDQConfig::unit(const std::string& key) const
    {
        const auto pair_ptr = this->units.find(key);
        if (pair_ptr == this->units.end()) {
            throw std::invalid_argument("No such UDQ quantity: " + key);
        }

        return pair_ptr->second;
    }

    bool UDQConfig::has_unit(const std::string& keyword) const
    {
        return this->units.find(keyword) != this->units.end();
    }

    bool UDQConfig::has_keyword(const std::string& keyword) const
    {
        return (this->m_assignments.find(keyword) != this->m_assignments.end())
            || (this->m_definitions.find(keyword) != this->m_definitions.end());
    }

    UDQInput UDQConfig::operator[](const std::string& keyword) const
    {
        const auto index_iter = this->input_index.find(keyword);
        if (index_iter == this->input_index.end()) {
            throw std::invalid_argument("Keyword: '" + keyword +
                                        "' not recognized as ASSIGN/DEFINE UDQ");
        }

        const auto u = this->has_unit(keyword)
            ? this->unit(keyword) : std::string{};

        if (index_iter->second.action == UDQAction::ASSIGN) {
            return UDQInput(this->input_index.at(keyword), this->m_assignments.at(keyword), u);
        }

        if (index_iter->second.action == UDQAction::DEFINE) {
            return UDQInput(this->input_index.at(keyword), this->m_definitions.at(keyword), u);
        }

        throw std::logic_error("Internal error - should not be here");
    }

    UDQInput UDQConfig::operator[](const std::size_t insert_index) const
    {
        auto index_iter = std::find_if(this->input_index.begin(), this->input_index.end(),
            [insert_index](const auto& name_index)
        {
            return name_index.second.insert_index == insert_index;
        });

        if (index_iter == this->input_index.end()) {
            throw std::invalid_argument("Insert index not recognized");
        }

        const auto& [keyword, index] = *index_iter;
        const auto u = this->has_unit(keyword)
            ? this->unit(keyword) : std::string{};

        if (index.action == UDQAction::ASSIGN) {
            return UDQInput(index, this->m_assignments.at(keyword), u);
        }

        if (index.action == UDQAction::DEFINE) {
            return UDQInput(index, this->m_definitions.at(keyword), u);
        }

        throw std::logic_error("Internal error - should not be here");
    }

    const UDQFunctionTable& UDQConfig::function_table() const
    {
        return this->udqft;
    }

    bool UDQConfig::operator==(const UDQConfig& data) const
    {
        return (this->params() == data.params())
            && (this->function_table() == data.function_table())
            && (this->m_definitions == data.m_definitions)
            && (this->m_assignments == data.m_assignments)
            && (this->units == data.units)
            && (this->input_index == data.input_index)
            && (this->type_count == data.type_count)
            ;
    }

    void UDQConfig::eval_assign(const std::size_t report_step,
                                SummaryState&     st,
                                UDQState&         udq_state,
                                UDQContext&       context) const
    {
        for (const auto& assign : this->assignments(UDQVarType::WELL_VAR)) {
            if (udq_state.assign(report_step, assign.keyword())) {
                auto ws = assign.eval(st.wells());
                context.update_assign(report_step, assign.keyword(), ws);
            }
        }

        for (const auto& assign : this->assignments(UDQVarType::GROUP_VAR)) {
            if (udq_state.assign(report_step, assign.keyword())) {
                auto ws = assign.eval(st.groups());
                context.update_assign(report_step, assign.keyword(), ws);
            }
        }

        for (const auto& assign : this->assignments(UDQVarType::FIELD_VAR)) {
            if (udq_state.assign(assign.report_step(), assign.keyword())) {
                auto ws = assign.eval();
                context.update_assign(report_step, assign.keyword(), ws);
            }
        }
    }

    void UDQConfig::eval_define(const std::size_t report_step,
                                UDQState&         udq_state,
                                UDQContext&       context) const
    {
        auto var_type_bit = [](const UDQVarType var_type)
        {
            return 1ul << static_cast<std::size_t>(var_type);
        };

        auto select_var_type = std::size_t{0};
        select_var_type |= var_type_bit(UDQVarType::WELL_VAR);
        select_var_type |= var_type_bit(UDQVarType::GROUP_VAR);
        select_var_type |= var_type_bit(UDQVarType::FIELD_VAR);

        for (const auto& [keyword, index] : this->input_index) {
            if (index.action != UDQAction::DEFINE) {
                continue;
            }

            auto def_pos = this->m_definitions.find(keyword);
            if (def_pos == this->m_definitions.end()) { // No such def
                throw std::logic_error {
                    fmt::format("Internal error: UDQ '{}' is not among "
                                "those DEFINEd for numerical evaluation", keyword)
                };
            }

            const auto& def = def_pos->second;
            if (((select_var_type & var_type_bit(def.var_type())) == 0) || // Unwanted Var Type
                ! udq_state.define(keyword, def.status())) // UDQ def not applicable now
            {
                continue;
            }

            context.update_define(report_step, keyword, def.eval(context));
        }
    }

    void UDQConfig::eval(const std::size_t  report_step,
                         const WellMatcher& wm,
                         SummaryState&      st,
                         UDQState&          udq_state) const
    {
        UDQContext context(this->function_table(), wm, st, udq_state);
        this->eval_assign(report_step, st, udq_state, context);
        this->eval_define(report_step, udq_state, context);
    }

    void UDQConfig::eval_assign(const std::size_t  report_step,
                                const WellMatcher& wm,
                                SummaryState&      st,
                                UDQState&          udq_state) const
    {
        UDQContext context(this->function_table(), wm, st, udq_state);
        this->eval_assign(report_step, st, udq_state, context);
    }

    void UDQConfig::required_summary(std::unordered_set<std::string>& summary_keys) const
    {
        for (const auto& def_pair : this->m_definitions) {
            def_pair.second.required_summary(summary_keys);
        }
    }

} // namespace Opm
