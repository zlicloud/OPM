/*
  Copyright 2016 Statoil ASA.

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

#include <opm/input/eclipse/Schedule/SummaryState.hpp>

#include <opm/input/eclipse/Schedule/UDQ/UDQSet.hpp>

#include <opm/common/utility/TimeService.hpp>

#include <cstddef>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <ostream>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

    bool is_total(const std::string& key) {
        static const std::vector<std::string> totals = {
            "OPT"  , "GPT"  , "WPT" , "GIT", "WIT", "OPTF" , "OPTS" , "OIT"  , "OVPT" , "OVIT" , "MWT" ,
            "WVPT" , "WVIT" , "GMT"  , "GPTF" , "SGT"  , "GST" , "FGT" , "GCT" , "GIMT" ,
            "WGPT" , "WGIT" , "EGT"  , "EXGT" , "GVPT" , "GVIT" , "LPT" , "VPT" , "VIT" , "NPT" , "NIT",
            "TPT", "TIT", "CPT", "CIT", "SPT", "SIT", "EPT", "EIT", "TPTHEA", "TITHEA",
            "OFT", "OFT+", "OFT-", "OFTG", "OFTL",
            "GFT", "GFT+", "GFT-", "GFTG", "GFTL",
            "WFT", "WFT+", "WFT-",
        };

        auto sep_pos = key.find(':');

        // Starting with ':' - that is probably broken?!
        if (sep_pos == 0)
            return false;

        if (sep_pos == std::string::npos) {
            for (const auto& total : totals) {
                if (key.compare(1, total.size(), total) == 0)
                    return true;
            }

            return false;
        } else
            return is_total(key.substr(0,sep_pos));
    }

    template <class T>
    using map2 = std::unordered_map<std::string, std::unordered_map<std::string, T>>;

    template <class T>
    bool has_var(const map2<T>& values, const std::string& var1, const std::string& var2) {
        const auto& var1_iter = values.find(var1);
        if (var1_iter == values.end())
            return false;

        const auto& var2_iter = var1_iter->second.find(var2);
        if (var2_iter == var1_iter->second.end())
            return false;

        return true;
    }

    template <class T>
    void erase_var(map2<T>& values, std::set<std::string>& var2_set, const std::string& var1, const std::string& var2) {
        const auto& var1_iter = values.find(var1);
        if (var1_iter == values.end())
            return;

        var1_iter->second.erase(var2);
        var2_set.clear();
        for (const auto& [_, var2_map] : values) {
            (void)_;
            for (const auto& [v2, __] : var2_map) {
                (void)__;
                var2_set.insert(v2);
            }
        }
    }

    template <class T>
    std::vector<std::string> var2_list(const map2<T>& values, const std::string& var1) {
        const auto& var1_iter = values.find(var1);
        if (var1_iter == values.end())
            return {};

        std::vector<std::string> l;
        for (const auto& pair : var1_iter->second)
            l.push_back(pair.first);
        return l;
    }

} // Anonymous namespace

namespace Opm
{

    SummaryState::SummaryState(time_point sim_start_arg)
        : sim_start(sim_start_arg)
    {
        this->update_elapsed(0);
    }

    SummaryState::SummaryState(std::time_t sim_start_arg)
        : SummaryState { TimeService::from_time_t(sim_start_arg) }
    {}

    void SummaryState::set(const std::string& key, double value)
    {
        this->values.insert_or_assign(key, value);
    }

    bool SummaryState::erase(const std::string& key) {
        return (this->values.erase(key) > 0);
    }

    bool SummaryState::erase_well_var(const std::string& well, const std::string& var)
    {
        std::string key = var + ":" + well;
        if (!this->erase(key))
            return false;

        erase_var(this->well_values, this->m_wells, var, well);
        this->well_names.reset();
        return true;
    }

    bool SummaryState::erase_group_var(const std::string& group, const std::string& var)
    {
        std::string key = var + ":" + group;
        if (!this->erase(key))
            return false;

        erase_var(this->group_values, this->m_groups, var, group);
        this->group_names.reset();
        return true;
    }

    bool SummaryState::has(const std::string& key) const
    {
        return this->values.find(key) != this->values.end();
    }

    bool SummaryState::has_well_var(const std::string& well, const std::string& var) const
    {
        return has_var(this->well_values, var, well);
    }

    bool SummaryState::has_well_var(const std::string& var) const
    {
        return this->well_values.count(var) != 0;
    }

    bool SummaryState::has_group_var(const std::string& group, const std::string& var) const
    {
        return has_var(this->group_values, var, group);
    }

    bool SummaryState::has_group_var(const std::string& var) const
    {
        return this->group_values.count(var) != 0;
    }

    bool SummaryState::has_conn_var(const std::string& well, const std::string& var, std::size_t global_index) const
    {
        if (!has_var(this->conn_values, var, well))
            return false;

        const auto& index_map = this->conn_values.at(var).at(well);
        return (index_map.count(global_index) > 0);
    }

    bool SummaryState::has_segment_var(const std::string& well,
                                       const std::string& var,
                                       const std::size_t  segment) const
    {
        // Segment Values = [var][well][segment] -> double

        auto varPos = this->segment_values.find(var);
        if (varPos == this->segment_values.end()) {
            return false;
        }

        auto wellPos = varPos->second.find(well);
        if (wellPos == varPos->second.end()) {
            return false;
        }

        return wellPos->second.find(segment) != wellPos->second.end();
    }

    void SummaryState::update(const std::string& key, double value) {
        if (is_total(key))
            this->values[key] += value;
        else
            this->values[key] = value;
    }

    void SummaryState::update_well_var(const std::string& well, const std::string& var, double value) {
        std::string key = var + ":" + well;
        if (is_total(var)) {
            this->values[key] += value;
            this->well_values[var][well] += value;
        } else {
            this->values[key] = value;
            this->well_values[var][well] = value;
        }
        if (this->m_wells.count(well) == 0) {
            this->m_wells.insert(well);
            this->well_names.reset();
        }
    }

    void SummaryState::update_group_var(const std::string& group, const std::string& var, double value) {
        std::string key = var + ":" + group;
        if (is_total(var)) {
            this->values[key] += value;
            this->group_values[var][group] += value;
        } else {
            this->values[key] = value;
            this->group_values[var][group] = value;
        }
        if (this->m_groups.count(group) == 0) {
            this->m_groups.insert(group);
            this->group_names.reset();
        }
    }

    void SummaryState::update_elapsed(double delta)
    {
        this->elapsed += delta;
    }

    void SummaryState::update_udq(const UDQSet& udq_set, double undefined_value)
    {
        const auto var_type = udq_set.var_type();
        if (var_type == UDQVarType::WELL_VAR) {
            const std::vector<std::string> wells = this->wells(); // Intentional copy
            for (const auto& well : wells) {
                const auto& udq_value = udq_set[well].value();
                this->update_well_var(well, udq_set.name(), udq_value.value_or(undefined_value));
            }
        }
        else if (var_type == UDQVarType::GROUP_VAR) {
            const std::vector<std::string> groups = this->groups(); // Intentional copy
            for (const auto& group : groups) {
                const auto& udq_value = udq_set[group].value();
                this->update_group_var(group, udq_set.name(), udq_value.value_or(undefined_value));
            }
        }
        else {
            const auto& udq_var = udq_set[0].value();
            this->update(udq_set.name(), udq_var.value_or(undefined_value));
        }
    }

    void SummaryState::update_conn_var(const std::string& well, const std::string& var, std::size_t global_index, double value)
    {
        std::string key = var + ":" + well + ":" + std::to_string(global_index);
        if (is_total(var)) {
            this->values[key] += value;
            this->conn_values[var][well][global_index] += value;
        } else {
            this->values[key] = value;
            this->conn_values[var][well][global_index] = value;
        }
    }

    void SummaryState::update_segment_var(const std::string& well,
                                          const std::string& var,
                                          const std::size_t  segment,
                                          const double       value)
    {
        auto& val_ref  = this->values[var + ':' + well + ':' + std::to_string(segment)];
        auto& sval_ref = this->segment_values[var][well][segment];

        if (is_total(var)) {
            val_ref  += value;
            sval_ref += value;
        }
        else {
            val_ref = sval_ref = value;
        }
    }

    double SummaryState::get(const std::string& key) const
    {
        const auto iter = this->values.find(key);
        if (iter == this->values.end())
            throw std::out_of_range("No such key: " + key);

        return iter->second;
    }

    double SummaryState::get(const std::string& key, double default_value) const
    {
        const auto iter = this->values.find(key);
        if (iter == this->values.end())
            return default_value;

        return iter->second;
    }

    double SummaryState::get_elapsed() const
    {
        return this->elapsed;
    }

    double SummaryState::get_well_var(const std::string& well, const std::string& var) const
    {
        return this->well_values.at(var).at(well);
    }

    double SummaryState::get_group_var(const std::string& group, const std::string& var) const
    {
        return this->group_values.at(var).at(group);
    }

    double SummaryState::get_conn_var(const std::string& well, const std::string& var, std::size_t global_index) const
    {
        return this->conn_values.at(var).at(well).at(global_index);
    }

    double SummaryState::get_segment_var(const std::string& well,
                                         const std::string& var,
                                         const std::size_t  segment) const
    {
        return this->segment_values.at(var).at(well).at(segment);
    }

    double SummaryState::get_well_var(const std::string& well, const std::string& var, double default_value) const
    {
        if (this->has_well_var(well, var))
            return this->get_well_var(well, var);

        return default_value;
    }

    double SummaryState::get_group_var(const std::string& group, const std::string& var, double default_value) const
    {
        if (this->has_group_var(group, var))
            return this->get_group_var(group, var);

        return default_value;
    }

    double SummaryState::get_conn_var(const std::string& well, const std::string& var, std::size_t global_index, double default_value) const
    {
        if (this->has_conn_var(well, var, global_index))
            return this->get_conn_var(well, var, global_index);
        return default_value;
    }

    double SummaryState::get_segment_var(const std::string& well,
                                         const std::string& var,
                                         const std::size_t  segment,
                                         const double       default_value) const
    {
        auto varPos = this->segment_values.find(var);
        if (varPos == this->segment_values.end()) {
            return default_value;
        }

        auto wellPos = varPos->second.find(well);
        if (wellPos == varPos->second.end()) {
            return default_value;
        }

        auto valPos = wellPos->second.find(segment);
        return (valPos == wellPos->second.end())
            ? default_value
            : valPos->second;
    }

    const std::vector<std::string>& SummaryState::wells() const
    {
        if (!this->well_names) {
            this->well_names = std::vector<std::string>(this->m_wells.begin(), this->m_wells.end());
        }

        return *this->well_names;
    }

    std::vector<std::string> SummaryState::wells(const std::string& var) const
    {
        return var2_list(this->well_values, var);
    }

    const std::vector<std::string>& SummaryState::groups() const
    {
        if (!this->group_names) {
            this->group_names = std::vector<std::string>(this->m_groups.begin(), this->m_groups.end());
        }

        return *this->group_names;
    }

    std::vector<std::string> SummaryState::groups(const std::string& var) const
    {
        return var2_list(this->group_values, var);
    }

    void SummaryState::append(const SummaryState& buffer)
    {
        this->sim_start = buffer.sim_start;
        this->elapsed = buffer.elapsed;
        this->values = buffer.values;
        this->well_names.reset();
        this->group_names.reset();

        this->m_wells.insert(buffer.m_wells.begin(), buffer.m_wells.end());
        for (const auto& [var, vals] : buffer.well_values) {
            this->well_values.insert_or_assign(var, vals);
        }

        this->m_groups.insert(buffer.m_groups.begin(), buffer.m_groups.end());
        for (const auto& [var, vals] : buffer.group_values) {
            this->group_values.insert_or_assign(var, vals);
        }

        for (const auto& [var, vals] : buffer.conn_values) {
            this->conn_values.insert_or_assign(var, vals);
        }

        for (const auto& [var, vals] : buffer.segment_values) {
            this->segment_values.insert_or_assign(var, vals);
        }
    }

    SummaryState::const_iterator SummaryState::begin() const
    {
        return this->values.begin();
    }

    SummaryState::const_iterator SummaryState::end() const
    {
        return this->values.end();
    }

    std::size_t SummaryState::num_wells() const
    {
        return this->m_wells.size();
    }

    std::size_t SummaryState::size() const
    {
        return this->values.size();
    }

    bool SummaryState::operator==(const SummaryState& other) const
    {
        return (this->sim_start == other.sim_start)
            && (this->elapsed == other.elapsed)
            && (this->values == other.values)
            && (this->well_values == other.well_values)
            && (this->m_wells == other.m_wells)
            && (this->wells() == other.wells())
            && (this->group_values == other.group_values)
            && (this->m_groups == other.m_groups)
            && (this->groups() == other.groups())
            && (this->conn_values == other.conn_values)
            && (this->segment_values == other.segment_values);
    }

    SummaryState SummaryState::serializationTestObject()
    {
        auto st = SummaryState{TimeService::from_time_t(101)};

        st.elapsed = 1.0;
        st.values = {{"test1", 2.0}};
        st.well_values = {{"test2", {{"test3", 3.0}}}};
        st.m_wells = {"test4"};
        st.well_names = {"test5"};
        st.group_values = {{"test6", {{"test7", 4.0}}}},
        st.m_groups = {"test7"};
        st.group_names = {"test8"},
        st.conn_values = {{"test9", {{"test10", {{5, 6.0}}}}}};

        {
            auto& sval = st.segment_values["SU1"];
            sval.emplace("W1", std::unordered_map<std::size_t, double> {
                    { std::size_t{ 1},  123.456   },
                    { std::size_t{ 2},   17.29    },
                    { std::size_t{10}, -  2.71828 },
                });

            sval.emplace("W6", std::unordered_map<std::size_t, double> {
                    { std::size_t{ 7}, 3.1415926535 },
                });
        }

        {
            auto& sval = st.segment_values["SUVIS"];
            sval.emplace("I2", std::unordered_map<std::size_t, double> {
                    { std::size_t{17},  29.0   },
                    { std::size_t{42}, - 1.618 },
                });
        }

        return st;
    }

    std::ostream& operator<<(std::ostream& stream, const SummaryState& st)
    {
        stream << "Simulated seconds: " << st.get_elapsed() << std::endl;
        for (const auto& value_pair : st)
            stream << std::setw(17) << value_pair.first << ": " << value_pair.second << std::endl;

        return stream;
    }

} // namespace Opm
