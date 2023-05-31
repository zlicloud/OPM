#include <fmt/format.h>

#include <opm/input/eclipse/Units/UnitSystem.hpp>

#include <opm/input/eclipse/Parser/ParserKeyword.hpp>

#include <opm/input/eclipse/Deck/DeckValue.hpp>
#include <opm/input/eclipse/Deck/DeckItem.hpp>
#include <opm/input/eclipse/Deck/UDAValue.hpp>
#include <opm/input/eclipse/Deck/DeckKeyword.hpp>
#include <opm/input/eclipse/Deck/DeckRecord.hpp>
#include <opm/input/eclipse/Utility/Typetools.hpp>

#include "export.hpp"
#include "converters.hpp"

#include <iostream>


namespace {

/* DeckKeyword */
const DeckRecord& (DeckKeyword::*getRecord)(size_t index) const = &DeckKeyword::getRecord;


const DeckItem& getItem(const DeckRecord& record, size_t index) {
  return record.getItem(index);
}

py::list item_to_pylist( const DeckItem& item )
{
    switch (item.getType())
    {
    case type_tag::integer:
        return iterable_to_pylist( item.getData< int >() );
        break;
    case type_tag::fdouble:
        throw py::type_error("Double list access must be specified by either 'get_raw_data_list' or 'get_SI_data_list'.");
        break;
    case type_tag::string:
        return iterable_to_pylist( item.getData< std::string >() );
        break;
    default:
        throw std::logic_error( "Type not set." );
        break;
    }
}


py::list raw_data_to_pylist( const DeckItem& item) {
    return iterable_to_pylist( item.getData<double>() );
}


py::list SI_data_to_pylist( const DeckItem& item) {
    return iterable_to_pylist( item.getSIDoubleData() );
}


bool is_int(const std::string& s)
{
    return !s.empty() && std::find_if(s.begin(),
        s.end(), [](char c) { return !std::isdigit(c); }) == s.end();
}


void push_string_as_deck_value(
    const ParserItem& parser_item,
    std::vector<DeckValue>& record,
    const std::string str)
{

    std::size_t star_pos = str.find('*');
    if (star_pos != std::string::npos) {
        int multiplier = 1;

        std::string mult_str = str.substr(0, star_pos);

        if (mult_str.length() > 0) {
            if (is_int(mult_str))
                multiplier = std::stoi( mult_str );
            else
                throw py::type_error();
        }

        std::string value_str = str.substr(star_pos + 1, str.length());
        DeckValue value;
        if (parser_item.dataType() == type_tag::uda) {
            if (value_str.length() > 0) {
                if (is_int(value_str))
                    value = DeckValue( UDAValue(stoi(value_str)) );
                else
                    value = DeckValue( UDAValue(stod(value_str)) );
            }
            else {
                value = DeckValue( UDAValue( parser_item.getDefault<UDAValue>() ) );
            }
        }
        else {
            if (value_str.length() > 0) {
                if (is_int(value_str))
                    value = DeckValue( stoi(value_str) );
                else
                    value = DeckValue( stod(value_str) );
            }
        }

        for (int i = 0; i < multiplier; i++)
            record.push_back( value );

    }
    else
        record.push_back( DeckValue(str) );

}

py::array_t<int> get_int_array(const DeckKeyword& kw) {
    return convert::numpy_array( kw.getIntData() );
}

py::array_t<double> get_raw_array(const DeckKeyword& kw) {
    return convert::numpy_array( kw.getRawDoubleData() );
}

py::array_t<double> get_SI_array(const DeckKeyword& kw) {
    return convert::numpy_array( kw.getSIDoubleData() );
}

bool uda_item_is_numeric(DeckItem *  item)
{
    if( !item->is_uda() )
        throw std::logic_error("deck item doesn't support user defined quantities");

    UDAValue uda = item->get_uda();

    return uda.is_numeric();
}

double get_uda_double(DeckItem *  item)
{
    UDAValue uda = item->get_uda();
    return uda.get<double>();
}

std::string get_uda_str(DeckItem *  item)
{
    UDAValue uda = item->get_uda();
    return uda.get<std::string>();
}

/*
  When exporting values to Python RawString and std::string are treated
  identically.
*/
std::string get_string(DeckItem * item, std::size_t index) {
    if (item->is_string())
        return item->get<std::string>(index);
    else if (item->is_raw_string())
        return item->get<RawString>(index);
    else
        throw std::logic_error("Tried to get string from item which is not string");
}


}
void python::common::export_DeckKeyword(py::module& module) {
    py::class_< DeckKeyword >( module, "DeckKeyword")
        .def(py::init<const ParserKeyword& >())

        .def(py::init([](const ParserKeyword& parser_keyword, py::list record_list, UnitSystem& active_system, UnitSystem& default_system) {

            std::vector< std::vector<DeckValue> > value_record_list;
            int i = 0;
            for (py::handle record_obj : record_list) {
                 py::list record = record_obj.cast<py::list>();
                 std::vector<DeckValue> value_record;
                 const ParserRecord& parser_record = parser_keyword.getRecord(i++);
                 int j = 0;
                 for (const py::handle& value_obj : record) {
                     const ParserItem& parser_item = parser_record.get(j++);
                     try {
                         int val_int = value_obj.cast<int>();
                         if (parser_item.dataType() == type_tag::uda) {
                             auto dim = active_system.parse(parser_item.dimensions()[0]);
                             value_record.push_back( DeckValue(UDAValue(static_cast<double>(val_int), dim)));
                         }
                         else {
                             value_record.push_back( DeckValue( val_int) );
                         }
                         continue;
                     }
                     catch (const std::exception& e_int) {}

                     try {
                         double val_double = value_obj.cast<double>();
                         if (parser_item.dataType() == type_tag::uda) {
                             auto dim = active_system.parse(parser_item.dimensions()[0]);
                             value_record.push_back( DeckValue(UDAValue(val_double, dim)));
                         }
                         else {
                             value_record.push_back( DeckValue(val_double) );
                         }
                         continue;
                     }
                     catch (const std::exception& e_double) {}

                     try {
                         std::string val_string = value_obj.cast<std::string>();
                         push_string_as_deck_value(
                             parser_item, value_record, val_string);
                         continue;
                     }
                     catch (const std::exception& e_string) {}

                     throw py::type_error("DeckKeyword: tried to add unknown type to record.");

                 }
                 value_record_list.push_back( value_record );
             }
             return DeckKeyword(parser_keyword, value_record_list, active_system, default_system);
         }  )  )

        .def( "__repr__", &DeckKeyword::name )
        .def( "__str__", &str<DeckKeyword> )
        .def("__iter__",  [] (const DeckKeyword &keyword) { return py::make_iterator(keyword.begin(), keyword.end()); }, py::keep_alive<0,1>())
        .def( "__getitem__", getRecord, ref_internal)
        .def( "__len__", &DeckKeyword::size )
        .def_property_readonly("name", &DeckKeyword::name )

    .def(py::init([](const ParserKeyword& parser_keyword, py::array_t<int> py_data) {
            return DeckKeyword(parser_keyword, convert::vector(py_data));
        } ) )

    .def(py::init([](const ParserKeyword& parser_keyword, py::array_t<double> py_data, UnitSystem& active_system, UnitSystem& default_system) {
            return DeckKeyword(parser_keyword, convert::vector(py_data), active_system, default_system);
        } ) )

    .def("get_int_array", &get_int_array)
    .def("get_raw_array", &get_raw_array)
    .def("get_SI_array", &get_SI_array)
         ;


    py::class_< DeckRecord >( module, "DeckRecord")
        .def( "__repr__", &str<DeckRecord> )
        .def( "__iter__", +[] (const DeckRecord& record) { return py::make_iterator(record.begin(), record.end()); }, py::keep_alive<0,1>())
        .def( "__getitem__", &getItem, ref_internal)
        .def( "__len__", &DeckRecord::size )
        ;


    py::class_< DeckItem >(module, "DeckItem")
        .def( "__len__", &DeckItem::data_size )
        .def("is_uda", &DeckItem::is_uda)
        .def("is_double", &DeckItem::is_double)
        .def("is_int", &DeckItem::is_int)
        .def("is_string", &DeckItem::is_string)
        .def("get_str", &get_string)
        .def("get_int", &DeckItem::get<int>)
        .def("get_raw", &DeckItem::get<double>)
        .def("get_uda", &DeckItem::get<UDAValue>)
        .def("get_SI", &DeckItem::getSIDouble)
        .def("get_data_list", &item_to_pylist)
        .def("get_raw_data_list", &raw_data_to_pylist)
        .def("get_SI_data_list", &SI_data_to_pylist)
        .def("__has_value", &DeckItem::hasValue)
        .def("__defaulted", &DeckItem::defaultApplied)
        .def("__is_numeric", &uda_item_is_numeric)
        .def("__uda_double", &get_uda_double)
        .def("__uda_str", &get_uda_str)
        .def("name", &DeckItem::name)
        ;


    py::class_< UDAValue >(module, "UDAValue")
        .def(py::init<double, const Dimension& >())
        .def(py::init<const std::string&, const Dimension&>())
        .def("dimension", &UDAValue::get_dim)
        .def("is_double", &UDAValue::is<double>)
        .def("is_string", &UDAValue::is<std::string>)
        .def("get_string", &UDAValue::get<std::string>)
        .def("get_double", &UDAValue::get<double>)
        .def("__repr__", [](const UDAValue& value) {
            if (value.is<double>())
                return fmt::format("UDAValue(value = {})", value.get<double>());
            else
                return fmt::format("UDAValue(value = {})", value.get<std::string>());
        })
        ;



}
