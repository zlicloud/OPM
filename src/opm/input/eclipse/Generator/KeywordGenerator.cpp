/*
  Copyright 2015 Statoil ASA.

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

#include <fstream>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <cctype>
#include <fmt/format.h>

#include <opm/json/JsonObject.hpp>
#include <opm/input/eclipse/Generator/KeywordGenerator.hpp>
#include <opm/input/eclipse/Generator/KeywordLoader.hpp>
#include <opm/input/eclipse/Parser/ParserKeyword.hpp>


namespace {

const std::string sourceHeader = R"(
#include <opm/input/eclipse/Deck/UDAValue.hpp>
#include <opm/input/eclipse/Parser/ParserItem.hpp>
#include <opm/input/eclipse/Parser/ParserRecord.hpp>
#include <opm/input/eclipse/Parser/Parser.hpp>


)";
}

namespace Opm {

    KeywordGenerator::KeywordGenerator(bool verbose)
        : m_verbose( verbose )
    {
    }

    std::string KeywordGenerator::headerHeader(const std::string& suffix) {
        std::string header = fmt::format("#ifndef PARSER_KEYWORDS_{0}_HPP\n"
                                         "#define PARSER_KEYWORDS_{0}_HPP\n"
                                         "#include <opm/input/eclipse/Parser/ParserKeyword.hpp>\n"
                                         "namespace Opm {{\n"
                                         "namespace ParserKeywords {{\n\n", suffix);
        return header;
    }

    void KeywordGenerator::ensurePath( const std::string& file_name) {
        std::filesystem::path file(file_name);
        if (!std::filesystem::is_directory( file.parent_path()))
            std::filesystem::create_directories( file.parent_path());
    }

    void
    updateFile(const std::string& newContent, const std::string& filename)
    {
        KeywordGenerator::ensurePath(filename);
        std::ofstream outputStream(filename);
        outputStream << newContent;
    }
    void
    KeywordGenerator::updateFile(const std::stringstream& newContent, const std::string& filename)
    {
        Opm::updateFile(newContent.str(), filename);
    }

    void
    write_file(const std::string& content, const std::string& file, bool verbose, std::string desc = "source")
    {
        Opm::updateFile(content, file);
        if (verbose)
            fmt::print("Updated {} file written to {}\n", desc, file);
    }


    static void
    write_file(const std::stringstream& stream, const std::string& file, bool verbose, std::string desc = "source")
    {
        write_file(stream.str(), file, verbose, desc);
    }

    void KeywordGenerator::updateBuiltInHeader(const KeywordLoader& loader, const std::string& headerBuildPath, const std::string& headerPath,
                                               const std::string& sourcePath ) const {
        std::stringstream newHeader;
        std::map<char, std::stringstream> newSources;

        newHeader << R"(#ifndef PARSER_KEYWORDS_BUILTIN_HPP
#define PARSER_KEYWORDS_BUILTIN_HPP
#include <unordered_map>
#include <fmt/format.h>
#include <opm/input/eclipse/Parser/ParserKeyword.hpp>
)";

        for(const auto& kw_pair : loader) {
            const auto& first_char = kw_pair.first;
            newSources[first_char]  << fmt::format("#include <opm/input/eclipse/Parser/ParserKeywords/{}.hpp>\n", first_char)
                                    << fmt::format("#include <{}/Builtin.hpp>\n",  headerPath)
                                    << "namespace Opm { namespace ParserKeywords {\n";
        }

        newHeader << R"(
namespace Opm {
namespace ParserKeywords {
struct Builtin {
)";
        for(const auto& kw_pair : loader) {
            const auto& keywords = kw_pair.second;
            auto& source = newSources[kw_pair.first];
            for (const auto& kw: keywords)
            {
                newHeader << fmt::format("    const ::Opm::ParserKeyword get_{0}();\n",kw.className());
                source << fmt::format("const ::Opm::ParserKeyword Builtin::get_{0}() {{ return {0}(); }};\n",kw.className());
            }
        }

        newHeader << R"(
    const ::Opm::ParserKeyword& operator[](const std::string& keyword) const {
        if (this->keywords.empty()) {
)";
        std::stringstream declareEmplace;

        for(const auto& kw_pair : loader) {
            const auto& keywords = kw_pair.second;
            auto& source = newSources[kw_pair.first];
            newHeader << fmt::format("            emplace{}();\n", kw_pair.first);
            source << fmt::format(R"(
void Builtin::emplace{}() const {{
)",
                                  kw_pair.first);
            declareEmplace << fmt::format(R"(
    void emplace{}() const;
)",
                                          kw_pair.first);

            for (const auto& kw: keywords)
                source << fmt::format("    this->keywords.emplace(\"{0}\", {0}());\n", kw.className());
            source <<"}\n";
            source <<"} }\n";
        }

        newHeader << R"(        }
        const auto kw_iter = this->keywords.find(keyword);
        if (kw_iter == this->keywords.end())
            throw std::invalid_argument(fmt::format("No builtin keyword: {}", keyword));
        return kw_iter->second;
    }

    const ::Opm::ParserKeyword& getKeyword(const std::string& keyword) const { return this->operator[](keyword); }
)";

        newHeader << "private:\n";
        newHeader << declareEmplace.str();
        newHeader << R"(
    mutable std::unordered_map<std::string, ::Opm::ParserKeyword> keywords;
};
}
}
#endif
)";

        const auto final_path = headerBuildPath + headerPath+ "/Builtin.hpp";
        write_file( newHeader, final_path, m_verbose, "header" );
        for(auto&& [first_char, source]: newSources)
        {
            auto sourceFile = std::filesystem::path(sourcePath) / fmt::format("Builtin{}.cpp",
                                                                              first_char);
            write_file(source, sourceFile, m_verbose, fmt::format("builtin source for {}", first_char));
        }
    }

    void KeywordGenerator::updateInitSource(const KeywordLoader& loader , const std::string& sourceFile,
                                            const std::string& sourcePath ) const {
        std::filesystem::path parserInitSource(sourceFile);
        std::string stem = parserInitSource.stem();
        std::filesystem::path parentPath = parserInitSource.parent_path();
        std::stringstream newSource;
        newSource << R"(
#include <opm/input/eclipse/Parser/Parser.hpp>
#include <opm/input/eclipse/Parser/ParserKeywords/Builtin.hpp>
)";

        for(const auto& kw_pair : loader) {
            const auto& first_char = kw_pair.first;
            const std::string header = fmt::format(R"(
#ifndef OPM_PARSER_INIT_{0}_HH
#define OPM_PARSER_INIT_{0}_HH

namespace Opm {{
class Parser;
namespace ParserKeywords {{
void addDefaultKeywords{0}(Parser& p);
}}
}}
#endif
)",
                                                   first_char);
            auto charHeaderFile = parserInitSource;
            charHeaderFile.replace_filename(
                fmt::format("include/opm/input/eclipse/Parser/ParserKeywords/ParserInit{}.hpp", first_char));
            write_file(header, charHeaderFile, m_verbose, fmt::format("init header for {}", first_char));
            std::stringstream sourceStr;
            sourceStr << fmt::format(R"(
#include <opm/input/eclipse/Parser/Parser.hpp>
#include<opm/input/eclipse/Parser/ParserKeywords/ParserInit{0}.hpp>
#include <opm/input/eclipse/Parser/ParserKeywords/{0}.hpp>

namespace Opm {{
namespace ParserKeywords {{
void addDefaultKeywords{0}([[maybe_unused]] Parser& p){{
    //Builtin keywords;
)",
                                     first_char);
                const auto& keywords = kw_pair.second;
                for (const auto& kw : keywords)
                    sourceStr << fmt::format("    p.addParserKeyword( {}() );", kw.className()) << std::endl;
            sourceStr << R"(

}
}
}
)";
            auto charSourceFile = std::filesystem::path(sourcePath) / fmt::format("ParserInit{}.cpp", first_char);
            write_file(sourceStr, charSourceFile, m_verbose, fmt::format("init source for {}", first_char));

            newSource << fmt::format("#include <opm/input/eclipse/Parser/ParserKeywords/ParserInit{}.hpp>\n",
                                     first_char);
        }

        newSource << R"(
namespace Opm {
namespace ParserKeywords {
void addDefaultKeywords(Parser& p);
void addDefaultKeywords(Parser& p) {
)";

        for(const auto& [first_char, keywords] : loader) {
                newSource << fmt::format("    addDefaultKeywords{}(p);", first_char) << std::endl;
        }

        newSource << R"(
}
}
void Parser::addDefaultKeywords() {
    ParserKeywords::addDefaultKeywords(*this);
}
}
)";

        write_file(newSource, sourceFile, m_verbose, "init");
    }


    void KeywordGenerator::updatePybindSource(const KeywordLoader& loader , const std::string& sourceFile) const {
        std::stringstream newSource;
        newSource << R"(#include <string>
#include <exception>

#include <opm/json/JsonObject.hpp>
#include <opm/input/eclipse/Parser/Parser.hpp>
#include <opm/input/eclipse/Parser/ParserKeyword.hpp>
#include <opm/input/eclipse/Parser/ParserKeywords/Builtin.hpp>
#include <opm/input/eclipse/Deck/Deck.hpp>
#include <opm/input/eclipse/Parser/ErrorGuard.hpp>

#include "export.hpp"

void python::common::export_ParserKeywords(py::module& module) {

    py::class_<ParserKeywords::Builtin>(module, "Builtin")
        .def(py::init<>())
)";

        for(const auto& kw_pair : loader) {
            const auto& keywords = kw_pair.second;
            for (const auto& kw: keywords)
                newSource << fmt::format("        .def_property_readonly(\"{0}\", &ParserKeywords::Builtin::get_{0})\n", kw.className());
        }
        newSource << R"(        .def("__getitem__", &ParserKeywords::Builtin::operator[], ref_internal);
}
)";

        fmt::print("Writing file: {}\n", sourceFile);
        write_file( newSource, sourceFile, m_verbose, "source");
    }


    void KeywordGenerator::updateKeywordSource(const KeywordLoader& loader , const std::string& sourcePath ) const {

        for(const auto& kw_pair : loader) {
            const auto& first_char = kw_pair.first;
            const auto& keywords = kw_pair.second;
            std::stringstream newSource;
            newSource << sourceHeader << std::endl;
            newSource << std::endl << std::endl << "#include <opm/input/eclipse/Parser/ParserKeywords/" << first_char << ".hpp>" << std::endl;
            newSource << "namespace Opm {" << std::endl;
            newSource << "namespace ParserKeywords {" << std::endl;
            for (const auto& kw: keywords)
                newSource << kw.createCode() << std::endl;
            newSource << "}\n}" << std::endl;
            write_file( newSource, sourcePath + "/" + first_char + ".cpp", m_verbose, "source" );
        }

    }

    void KeywordGenerator::updateHeader(const KeywordLoader& loader, const std::string& headerBuildPath, const std::string& headerPath) const {
        for( const auto& kw_pair : loader) {
            std::stringstream stream;
            const auto& first_char = kw_pair.first;
            const auto& keywords = kw_pair.second;

            stream << headerHeader( std::string( 1, std::toupper( first_char ) ) );
            for( auto& kw : keywords )
                stream << kw.createDeclaration("   ") << std::endl;

            stream << "}" << std::endl << "}" << std::endl;
            stream << "#endif" << std::endl;

            const auto final_path = headerBuildPath + headerPath+ "/" + std::string( 1, first_char ) + ".hpp";
            write_file( stream, final_path, m_verbose, "header" );
        }
    }


    std::string KeywordGenerator::startTest(const std::string& keyword_name) {
        return std::string("BOOST_AUTO_TEST_CASE(TEST") + keyword_name + std::string("Keyword) {\n");
    }


    std::string KeywordGenerator::endTest() {
        return "}\n\n";
    }



    void KeywordGenerator::updateTest(const KeywordLoader& loader , const std::string& testFile) const {
        std::stringstream stream;

        for(const auto& kw_pair : loader) {
            const auto& first_char = kw_pair.first;
            stream << "#include <opm/input/eclipse/Parser/ParserKeywords/" << first_char << ".hpp>" << std::endl;
        }

        stream << R"(

#define BOOST_TEST_MODULE GeneratedKeywordTest
#include <filesystem>
#include <boost/test/unit_test.hpp>
#include <memory>
#include <opm/json/JsonObject.hpp>
#include <opm/input/eclipse/Parser/ParserKeyword.hpp>
#include <opm/input/eclipse/Parser/ParserItem.hpp>
#include <opm/input/eclipse/Parser/ParserRecord.hpp>
#include <opm/input/eclipse/Units/UnitSystem.hpp>
auto unitSystem =  Opm::UnitSystem::newMETRIC();

namespace Opm {
void test_keyword(const ParserKeyword& inline_keyword, const std::string& json_file) {
    std::filesystem::path jsonPath( json_file );
    Json::JsonObject jsonConfig( jsonPath );
    ParserKeyword json_keyword(jsonConfig);
    BOOST_CHECK_EQUAL( json_keyword, inline_keyword);
    if (json_keyword.hasDimension()) {
        const auto& parserRecord = json_keyword.getRecord(0);
        for (size_t i=0; i < parserRecord.size(); i++){
            const auto& item = parserRecord.get( i );
            for (const auto& dim : item.dimensions())
                BOOST_CHECK_NO_THROW( unitSystem.getNewDimension( dim ));
        }
    }
}


)";

        for(const auto& kw_pair : loader) {
            stream << std::endl << "BOOST_AUTO_TEST_CASE(TestKeywords" << kw_pair.first << ") {" << std::endl;
            const auto& keywords = kw_pair.second;
            for (const auto& kw: keywords)
                stream << "    test_keyword( ParserKeywords::" << kw.getName() << "(),\"" << loader.getJsonFile( kw.getName()) << "\");" << std::endl;
            stream << "}" << std::endl;
        }
        stream << "}" << std::endl;
        updateFile( stream , testFile );
    }
}


