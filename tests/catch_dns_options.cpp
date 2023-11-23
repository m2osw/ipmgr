// Copyright (c) 2023  Made to Order Software Corp.  All Rights Reserved
//
// https://snapwebsites.org/project/ipmgr
// contact@m2osw.com
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

// self
//
#include    "catch_main.h"


// ipmgr
//
#include    <ipmgr/version.h>


// snapdev
//
#include    <snapdev/file_contents.h>
#include    <snapdev/glob_to_list.h>
#include    <snapdev/not_reached.h>
#include    <snapdev/string_replace_many.h>
#include    <snapdev/trim_string.h>


// C++
//
#include    <list>



namespace
{



class test_data
{
public:
    test_data(std::string const & filename)
        : f_filename(filename)
    {
        load_data();
    }

    void create_input(std::string const & filename)
    {
        std::ofstream out(filename);
        out << f_input;
        CATCH_REQUIRE(out.good());
    }

    void execute_command(std::string const & filename)
    {
        std::string cmd(SNAP_CATCH2_NAMESPACE::g_binary_dir());
        cmd += "/tools/dns-options --stdout ";

        for(auto const & e : f_execute)
        {
            cmd += " -e \"";
            cmd += snapdev::string_replace_many(e, { { "\"", "\\\"" } }); // escape quotes if any were used
            cmd += '"';
        }

        cmd += ' ';
        cmd += filename;
        cmd += " >";
        cmd += filename;
        cmd += ".output";

        int const r(system(cmd.c_str()));
        CATCH_REQUIRE(r == 0);
    }

    void verify_output(std::string const & filename)
    {
        std::string const output_filename(filename + ".output");

        snapdev::file_contents contents(output_filename);
        CATCH_REQUIRE(contents.exists());
        CATCH_REQUIRE(contents.read_all());
        std::string const output(contents.contents());

        CATCH_REQUIRE_LONG_STRING(f_output, output);
    }

private:
    enum class state_t {
        STATE_START,    // nothing found just yet
        STATE_EXECUTE,
        STATE_INPUT,
        STATE_OUTPUT,
    };

    void load_data()
    {
        state_t state(state_t::STATE_START);
        int line(0);
        std::ifstream in(f_filename);
        for(;;)
        {
            ++line;
            std::string l;
            std::getline(in, l);
            l = snapdev::trim_string(l);
            if(l.empty())
            {
                if(!in.good())
                {
                    // EOF
                    //
                    break;
                }
                continue;
            }
            if(l[0] == '#')
            {
                // comment
                //
                continue;
            }
            if(l[0] == '[')
            {
                // found new section, must end with ']'
                //
                if(l.back() != ']')
                {
                    std::cerr
                        << "error:"
                        << f_filename
                        << ':'
                        << line
                        << ": missing ']' after section name.\n";
                    CATCH_REQUIRE(!"missing ']'");
                    snapdev::NOT_REACHED();
                }

                l = snapdev::trim_string(l.substr(1, l.length() - 2));
                if(l.empty())
                {
                    std::cerr
                        << "error:"
                        << f_filename
                        << ':'
                        << line
                        << ": section name missing between '[...]'.\n";
                    CATCH_REQUIRE(!"section name missing");
                    snapdev::NOT_REACHED();
                }

                if(l == "execute")
                {
                    if(!f_execute.empty())
                    {
                        std::cerr
                            << "error:"
                            << f_filename
                            << ':'
                            << line
                            << ": found multiple definitions of the [execute] section.\n";
                        CATCH_REQUIRE(!"multiple [execute] section");
                        snapdev::NOT_REACHED();
                    }
                    state = state_t::STATE_EXECUTE;
                }
                else if(l == "input")
                {
                    if(!f_input.empty())
                    {
                        std::cerr
                            << "error:"
                            << f_filename
                            << ':'
                            << line
                            << ": found multiple definitions of the [input] section.\n";
                        CATCH_REQUIRE(!"multiple [input] section");
                        snapdev::NOT_REACHED();
                    }
                    state = state_t::STATE_INPUT;
                }
                else if(l == "output")
                {
                    if(!f_output.empty())
                    {
                        std::cerr
                            << "error:"
                            << f_filename
                            << ':'
                            << line
                            << ": found multiple definitions of the [output] section.\n";
                        CATCH_REQUIRE(!"multiple [output] section");
                        snapdev::NOT_REACHED();
                    }
                    state = state_t::STATE_OUTPUT;
                }
                else
                {
                    std::cerr
                        << "error:"
                        << f_filename
                        << ':'
                        << line
                        << ": unsupported section '["
                        << l
                        << "]'.\n";
                    CATCH_REQUIRE(!"unsupported section name");
                    snapdev::NOT_REACHED();
                }
                continue;
            }
            switch(state)
            {
            case state_t::STATE_START:
                std::cerr
                    << "error:"
                    << f_filename
                    << ':'
                    << line
                    << ": missing section name before data.\n";
                CATCH_REQUIRE(!"section name missing");
                snapdev::NOT_REACHED();
                break;

            case state_t::STATE_EXECUTE:
                f_execute.push_back(l);
                break;

            case state_t::STATE_INPUT:
                f_input += l;
                f_input += '\n';
                break;

            case state_t::STATE_OUTPUT:
                f_output += l;
                f_output += '\n';
                break;

            }
        }
    }

    std::string             f_filename = std::string();
    std::list<std::string>  f_execute = std::list<std::string>();
    std::string             f_input = std::string();
    std::string             f_output = std::string();
};



} // no name namespace


CATCH_TEST_CASE("dns_options", "[options]")
{
    CATCH_START_SECTION("verify dns-options editing")
    {
        std::string const input_filename(SNAP_CATCH2_NAMESPACE::g_tmp_dir() + "/named.conf");
        std::string const test_files(SNAP_CATCH2_NAMESPACE::g_source_dir() + "/tests/scripts");

        snapdev::glob_to_list<std::vector<std::string>> glob;
        bool const list_success(glob.read_path<
                 snapdev::glob_to_list_flag_t::GLOB_FLAG_IGNORE_ERRORS,
                 snapdev::glob_to_list_flag_t::GLOB_FLAG_PERIOD>(test_files + "/*.conf"));
        CATCH_REQUIRE(list_success);

        for(auto tf : glob)
        {
            std::cout << "--- working on \"" << tf << "\"...\n";
            test_data data(tf);
            data.create_input(input_filename);
            data.execute_command(input_filename);
            data.verify_output(input_filename);
        }
    }
    CATCH_END_SECTION()
}


// vim: ts=4 sw=4 et
