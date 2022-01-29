// Copyright (c) 2022  Made to Order Software Corp.  All Rights Reserved.
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
//
#pragma once

/** \file
 * \brief Various definitions of the ipmgr tool.
 *
 * The ipmgr class implements the functionality of the ipmgr tool.
 */


// advgetopt lib
//
#include    <advgetopt/advgetopt.h>
#include    <advgetopt/conf_file.h>



class ipmgr
{
public:
    struct zone_files_t
    {
        // indexed by domain names
        typedef std::map<std::string, zone_files_t>             map_t;
        typedef std::vector<advgetopt::conf_file::pointer_t>    config_array_t;

        // the order matters; when searching for a parameter, the
        // first file from the end of the vector must be checked
        // first; the first instance of a defined parameter must be
        // used; prior instances were overridden
        //
        config_array_t          f_config = config_array_t();
    };

                            ipmgr(int argc, char * argv[]);

    int                     run();

private:
    bool                    dry_run() const;
    bool                    verbose() const;
    int                     make_root();
    int                     read_zones();
    std::string             get_zone_param(
                                  zone_files_t const & zone
                                , std::string const & name
                                , std::string const & default_name = std::string()
                                , std::string const & default_value = std::string());
    bool                    get_zone_bool(
                                  zone_files_t const & zone
                                , std::string const & name
                                , std::string const & default_name = std::string()
                                , std::string const & default_value = std::string());
    std::int32_t            get_zone_integer(
                                  zone_files_t const & zone
                                , std::string const & name
                                , std::string const & default_name = std::string()
                                , std::int32_t default_value = 0);
    std::int64_t            get_zone_duration(
                                  zone_files_t const & zone
                                , std::string const & name
                                , std::string const & default_name = std::string()
                                , std::string const & default_value = std::string());
    std::string             get_zone_email(
                                  zone_files_t const & zone
                                , std::string const & name
                                , std::string const & default_name
                                , std::string const & default_value);
    std::uint32_t           get_zone_serial(std::string const & domain, bool next = false);
    int                     generate_zone(zone_files_t const & zone);
    int                     process_zones();
    int                     restart_bind9();

    advgetopt::getopt       f_opt;
    zone_files_t::map_t     f_zone_files = zone_files_t::map_t();
    bool                    f_bind_restart_required = false;
    bool                    f_dry_run = false;
    bool                    f_verbose = false;
    bool                    f_config_warnings = false;
};



// vim: ts=4 sw=4 et
