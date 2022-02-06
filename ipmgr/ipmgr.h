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
    class zone_files
    {
    public:
        // indexed by domain names
        typedef std::shared_ptr<zone_files>                     pointer_t;
        typedef std::map<std::string, pointer_t>                map_t;
        typedef std::vector<advgetopt::conf_file::pointer_t>    config_array_t;

                                zone_files(
                                      advgetopt::getopt::pointer_t opt
                                    , bool verbose);

        void                    add(advgetopt::conf_file::pointer_t zone);

        std::string             get_zone_param(
                                      std::string const & name
                                    , std::string const & default_name = std::string()
                                    , std::string const & default_value = std::string());
        bool                    get_zone_bool(
                                      std::string const & name
                                    , std::string const & default_name = std::string()
                                    , std::string const & default_value = std::string());
        std::int32_t            get_zone_integer(
                                      std::string const & name
                                    , std::string const & default_name = std::string()
                                    , std::int32_t default_value = 0);
        std::int64_t            get_zone_duration(
                                      std::string const & name
                                    , std::string const & default_name = std::string()
                                    , std::string const & default_value = std::string());
        std::string             get_zone_email(
                                      std::string const & name
                                    , std::string const & default_name
                                    , std::string const & default_value);

        bool                    retrieve_fields();
        std::string             domain() const;
        bool                    is_dynamic() const;
        std::string             generate_zone_file();

    private:
        bool                    retrieve_domain();
        bool                    retrieve_ttl();
        bool                    retrieve_ips();
        bool                    retrieve_nameservers();
        bool                    retrieve_hostmaster();
        bool                    retrieve_serial();
        bool                    retrieve_refresh();
        bool                    retrieve_retry();
        bool                    retrieve_expire();
        bool                    retrieve_minimum_cache_failures();
        bool                    retrieve_mail_fields();
        bool                    retrieve_dynamic();
        bool                    retrieve_all_sections();
        std::uint32_t           get_zone_serial(bool next = false);

        advgetopt::getopt::pointer_t        f_opt = advgetopt::getopt::pointer_t();
        bool                                f_verbose = false;

        // the order matters; when searching for a parameter, the
        // first file from the end of the vector must be checked
        // first; the first instance of a defined parameter must be
        // used; prior instances were overridden
        //
        config_array_t                      f_configs = config_array_t();

        // these get defined when we call the retrive_fields() function
        //
        std::string                         f_domain = std::string();
        std::int32_t                        f_ttl = 0;
        advgetopt::string_list_t            f_ips = advgetopt::string_list_t();
        advgetopt::string_list_t            f_nameservers = advgetopt::string_list_t();
        std::string                         f_hostmaster = std::string();
        std::uint32_t                       f_serial = 0;
        std::int64_t                        f_refresh = 0;
        std::int64_t                        f_retry = 0;
        std::int64_t                        f_expire = 0;
        std::int64_t                        f_minimum_cache_failures = 0;
        advgetopt::string_list_t            f_mail_subdomains = advgetopt::string_list_t();
        std::int32_t                        f_mail_priority = -1;
        std::int32_t                        f_mail_ttl = 0;
        std::int32_t                        f_mail_default_ttl = 0;
        bool                                f_dynamic = false;
        advgetopt::conf_file::sections_t    f_sections = advgetopt::conf_file::sections_t();
    };

                            ipmgr(int argc, char * argv[]);

    int                     run();

private:
    bool                    dry_run() const;
    bool                    verbose() const;
    int                     make_root();
    int                     read_zones();
    int                     generate_zone(zone_files::pointer_t & zone);
    int                     process_zones();
    int                     restart_bind9();

    advgetopt::getopt::pointer_t
                            f_opt = advgetopt::getopt::pointer_t();
    zone_files::map_t       f_zone_files = zone_files::map_t();
    bool                    f_bind_restart_required = false;
    bool                    f_dry_run = false;
    bool                    f_verbose = false;
    bool                    f_config_warnings = false;
};



// vim: ts=4 sw=4 et
