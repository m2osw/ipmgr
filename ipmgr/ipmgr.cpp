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


/** \file
 * \brief ipmgr tool.
 *
 * This implementation offers a way to easily manage the BIND9 zones without
 * having to edit those files manually all the time.
 *
 * Our configuration files are much easier than the standard DNS files and
 * allows us to define many of the parameters, such as your server IP
 * addresses, once for the entire system instead of once per domain.
 *
 * The current version generates the static zone files under
 * `/etc/bind/zones/...`. These files get loaded automatically when you
 * restart the `named` service.
 *
 * For dynamic zones, this implementation uses the `nsupdate` and `rndc`
 * tools since you are not expected to tweak the files under `/var/lib/bind/...`
 * directly (although you cloud (1) flush, (2) stop BIND9, (3) edit the files,
 * (4) restart BIND9; this is not 100% safe because you could receive new
 * requests which BIND9 is down and the re-generation would wipe out the
 * dynamic changes.
 */


// self
//
#include    "ipmgr.h"
#include    "version.h"


// advgetopt lib
//
#include    <advgetopt/validator_duration.h>
#include    <advgetopt/validator_integer.h>


// libaddr lib
//
#include    <libaddr/addr_parser.h>


// libtld lib
//
#include    <libtld/tld.h>


// cppprocess lib
//
#include    <cppprocess/io_capture_pipe.h>
#include    <cppprocess/process.h>


// snaplogger lib
//
#include    <snaplogger/message.h>
#include    <snaplogger/options.h>


// snapdev lib
//
#include    <snapdev/pathinfo.h>


// boost lib
//
#include    <boost/preprocessor/stringize.hpp>


// C++ lib
//
#include    <iostream>
#include    <fstream>
#include    <set>
#include    <sstream>


// snapdev lib
//
#include    <snapdev/poison.h>



namespace
{


char const * const g_zone_directories_separator[] =
{
    " ",
};


char const * const g_ip_separator[] =
{
    " ",
};



/** \brief Command line options.
 *
 * This table includes all the options supported by ipmgr on the
 * command line.
 */
advgetopt::option const g_ipmgr_options[] =
{
    // OPTIONS
    //
    advgetopt::define_option(
          advgetopt::Name("default-expire")
        , advgetopt::Flags(advgetopt::all_flags<
                      advgetopt::GETOPT_FLAG_GROUP_OPTIONS
                    , advgetopt::GETOPT_FLAG_REQUIRED
                    , advgetopt::GETOPT_FLAG_MULTIPLE
                    , advgetopt::GETOPT_FLAG_PROCESS_VARIABLES>())
        , advgetopt::DefaultValue("2w")
        , advgetopt::Help("Define the amount of time to remember a value for even if stale.")
    ),
    advgetopt::define_option(
          advgetopt::Name("default-hostmaster")
        , advgetopt::Flags(advgetopt::all_flags<
                      advgetopt::GETOPT_FLAG_GROUP_OPTIONS
                    , advgetopt::GETOPT_FLAG_REQUIRED
                    , advgetopt::GETOPT_FLAG_PROCESS_VARIABLES>())
        , advgetopt::Help("Default email address of the host master.")
    ),
    advgetopt::define_option(
          advgetopt::Name("default-ips")
        , advgetopt::Flags(advgetopt::all_flags<
                      advgetopt::GETOPT_FLAG_GROUP_OPTIONS
                    , advgetopt::GETOPT_FLAG_REQUIRED
                    , advgetopt::GETOPT_FLAG_MULTIPLE
                    , advgetopt::GETOPT_FLAG_PROCESS_VARIABLES>())
        , advgetopt::Separators(g_ip_separator)
        , advgetopt::Help("List of IP address to assign to a domain by default (if more than one, do a round robin).")
    ),
    advgetopt::define_option(
          advgetopt::Name("default-minimum-cache-failures")
        , advgetopt::Flags(advgetopt::all_flags<
                      advgetopt::GETOPT_FLAG_GROUP_OPTIONS
                    , advgetopt::GETOPT_FLAG_REQUIRED
                    , advgetopt::GETOPT_FLAG_MULTIPLE
                    , advgetopt::GETOPT_FLAG_PROCESS_VARIABLES>())
        , advgetopt::DefaultValue("5m")
        , advgetopt::Help("Define the amount of time to between retries to refresh the cache.")
    ),
    advgetopt::define_option(
          advgetopt::Name("default-refresh")
        , advgetopt::Flags(advgetopt::all_flags<
                      advgetopt::GETOPT_FLAG_GROUP_OPTIONS
                    , advgetopt::GETOPT_FLAG_REQUIRED
                    , advgetopt::GETOPT_FLAG_MULTIPLE
                    , advgetopt::GETOPT_FLAG_PROCESS_VARIABLES>())
        , advgetopt::DefaultValue("3h")
        , advgetopt::Help("Define the refresh rate of your secondary servers.")
    ),
    advgetopt::define_option(
          advgetopt::Name("default-retry")
        , advgetopt::Flags(advgetopt::all_flags<
                      advgetopt::GETOPT_FLAG_GROUP_OPTIONS
                    , advgetopt::GETOPT_FLAG_REQUIRED
                    , advgetopt::GETOPT_FLAG_MULTIPLE
                    , advgetopt::GETOPT_FLAG_PROCESS_VARIABLES>())
        , advgetopt::DefaultValue("3m")
        , advgetopt::Help("Define the retry rate in case the main server does not reply to your secondary servers.")
    ),
    advgetopt::define_option(
          advgetopt::Name("default-ttl")
        , advgetopt::Flags(advgetopt::all_flags<
                      advgetopt::GETOPT_FLAG_GROUP_OPTIONS
                    , advgetopt::GETOPT_FLAG_REQUIRED
                    , advgetopt::GETOPT_FLAG_MULTIPLE
                    , advgetopt::GETOPT_FLAG_PROCESS_VARIABLES>())
        , advgetopt::DefaultValue("1d")
        , advgetopt::Help("Define the default time to live for a domain name request.")
    ),
    advgetopt::define_option(
          advgetopt::Name("default-nameservers")
        , advgetopt::Flags(advgetopt::all_flags<
                      advgetopt::GETOPT_FLAG_GROUP_OPTIONS
                    , advgetopt::GETOPT_FLAG_REQUIRED
                    , advgetopt::GETOPT_FLAG_MULTIPLE
                    , advgetopt::GETOPT_FLAG_PROCESS_VARIABLES>())
        , advgetopt::Help("Default domain names for all your nameservers. You must define at least two.")
    ),
    advgetopt::define_option(
          advgetopt::Name("dns-ip")
        , advgetopt::Flags(advgetopt::all_flags<
                      advgetopt::GETOPT_FLAG_GROUP_OPTIONS
                    , advgetopt::GETOPT_FLAG_REQUIRED>())
        , advgetopt::DefaultValue("1270.01")
        , advgetopt::Help("Define the IP address to connect to the DNS service (BIND9).")
    ),
    advgetopt::define_option(
          advgetopt::Name("dry-run")
        , advgetopt::ShortName('d')
        , advgetopt::Flags(advgetopt::standalone_command_flags<
                      advgetopt::GETOPT_FLAG_GROUP_OPTIONS>())
        , advgetopt::Help("Run ipmgr to generate all the commands, but do not actually run those commands. This option implies `--verbose`.")
    ),
    advgetopt::define_option(
          advgetopt::Name("quiet")
        , advgetopt::ShortName('q')
        , advgetopt::Flags(advgetopt::standalone_all_flags<
                      advgetopt::GETOPT_FLAG_GROUP_OPTIONS>())
        , advgetopt::Help("Cancel the `--verbose` flags.")
    ),
    advgetopt::define_option(
          advgetopt::Name("config-warnings")
        , advgetopt::Flags(advgetopt::all_flags<
                      advgetopt::GETOPT_FLAG_GROUP_OPTIONS>())
        , advgetopt::DefaultValue("false")
        , advgetopt::Help("Show configuration file warnings.")
    ),
    advgetopt::define_option(
          advgetopt::Name("slave")
        , advgetopt::Flags(advgetopt::all_flags<
                      advgetopt::GETOPT_FLAG_GROUP_OPTIONS>())
        , advgetopt::DefaultValue("true")
        , advgetopt::Help("Mark this server as a slave DNS.")
    ),
    advgetopt::define_option(
          advgetopt::Name("verbose")
        , advgetopt::ShortName('v')
        , advgetopt::Flags(advgetopt::standalone_all_flags<
                      advgetopt::GETOPT_FLAG_GROUP_OPTIONS>())
        , advgetopt::Help("Show the various steps the ipmgr takes to generate the zones.")
    ),
    advgetopt::define_option(
          advgetopt::Name("zone-directories")
        , advgetopt::Flags(advgetopt::all_flags<
                      advgetopt::GETOPT_FLAG_GROUP_OPTIONS
                    , advgetopt::GETOPT_FLAG_REQUIRED
                    , advgetopt::GETOPT_FLAG_MULTIPLE>())
        , advgetopt::Separators(g_zone_directories_separator)
        , advgetopt::DefaultValue("/usr/share/ipmgr/zones /etc/ipmgr/zones /var/lib/ipmgr/zones")
        , advgetopt::Help("List of directories to scan for zone definitions.")
    ),
    advgetopt::end_options()
};



advgetopt::group_description const g_group_descriptions[] =
{
    advgetopt::define_group(
          advgetopt::GroupNumber(advgetopt::GETOPT_FLAG_GROUP_COMMANDS)
        , advgetopt::GroupName("command")
        , advgetopt::GroupDescription("Commands:")
    ),
    advgetopt::define_group(
          advgetopt::GroupNumber(advgetopt::GETOPT_FLAG_GROUP_OPTIONS)
        , advgetopt::GroupName("option")
        , advgetopt::GroupDescription("Options:")
    ),
    advgetopt::end_groups()
};


char const * const g_configuration_directories[] =
{
    "/usr/share/ipmgr",
    "/etc/ipmgr",
    nullptr
};




// TODO: once we have stdc++20, remove all defaults
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
advgetopt::options_environment const g_iplock_options_environment =
{
    .f_project_name = "ipmgr",
    .f_group_name = nullptr,
    .f_options = g_ipmgr_options,
    .f_options_files_directory = nullptr,
    .f_environment_variable_name = "IPMGR_OPTIONS",
    .f_section_variables_name = "variables",
    .f_configuration_files = nullptr,
    .f_configuration_filename = "ipmgr.conf",
    .f_configuration_directories = g_configuration_directories,
    .f_environment_flags = advgetopt::GETOPT_ENVIRONMENT_FLAG_SYSTEM_PARAMETERS
                         | advgetopt::GETOPT_ENVIRONMENT_FLAG_PROCESS_SYSTEM_PARAMETERS,
    .f_help_header = "Usage: %p [-<opt>] [ip]\n"
                     "where -<opt> is one or more of:",
    .f_help_footer = nullptr,
    .f_version = IPMGR_VERSION_STRING,
    .f_license = "This software is licenced under the GPL v3",
    .f_copyright = "Copyright (c) 2022-" BOOST_PP_STRINGIZE(UTC_BUILD_YEAR) " by Made to Order Software Corporation",
    .f_build_date = UTC_BUILD_DATE,
    .f_build_time = UTC_BUILD_TIME,
    .f_groups = g_group_descriptions
};
#pragma GCC diagnostic pop




char const * g_bind9_need_restart = "/run/ipmgr/bind9-need-restart";


}
// no name namespace




/** \brief Initialize the ipmgr object.
 *
 * This function parses the command line and sets up the logger.
 *
 * \param[in] argc  The number of arguments in argv.
 * \param[in] argv  The argument strings.
 */
ipmgr::ipmgr(int argc, char * argv[])
    : f_opt(g_iplock_options_environment)
{
    snaplogger::add_logger_options(f_opt);
    f_opt.finish_parsing(argc, argv);
    snaplogger::process_logger_options(f_opt, "/etc/ipmgr/logger");

    f_dry_run = f_opt.is_defined("dry-run");
    f_verbose = f_dry_run || f_opt.is_defined("verbose");
    f_config_warnings = f_opt.is_defined("config-warnings");
}


/** \brief Become root when required.
 *
 * Some of the work we do, such as generating the static zones under
 * `/etc/bind/zones`, require the ipmgr to be root.
 *
 * Also, we need the ipmgr to be run when restarting the named service.
 *
 * The nsupdate and rndc commands should not require you to be root
 * unless the keys are only accessible to the root user.
 *
 * \return 1 on error, 0 on success
 */
int ipmgr::make_root()
{
    if(setuid(0) != 0)
    {
        int const e(errno);
        SNAP_LOG_FATAL
            << "could not become root (`setuid(0)`) to execute privileged commands (errno: "
            << e
            << ", "
            << strerror(e)
            << ")"
            << SNAP_LOG_SEND;
        //return 1;
    }

    if(setgid(0) != 0)
    {
        int const e(errno);
        SNAP_LOG_FATAL
            << "could not change group to root (`setgid(0)`) to execute privileged commands (errno: "
            << e
            << ", "
            << strerror(e)
            << ")"
            << SNAP_LOG_SEND;
        //return 1;
    }

    return 0;
}


/** \brief Read all the files to process.
 *
 * This function reads all the zone files that are going to be processed.
 * It also merges the list items by name. The same zone can be defined in
 * three different locations (plus their standard sub-directories as per
 * advgetopt, so really some 303 files). The one with the lowest priority
 * is read first. The others can overwrite the values as required.
 */
int ipmgr::read_zones()
{
    int exit_code(0);

    // get a list of all the files
    //
    std::size_t const max(f_opt.size("zone-directories"));
    if(max == 0)
    {
        // no zone directories specified?!
        //
        SNAP_LOG_FATAL
            << "no zone directories specified (see --zone-directories)."
            << SNAP_LOG_SEND;
        return 0;
    }
    for(std::size_t i(0); i < max; ++i)
    {
        std::string dir(f_opt.get_string("zone-directories", i));
        std::string pattern(dir + "/*.conf");
        snapdev::glob_to_list<std::vector<std::string>> glob;
        if(!glob.read_path<
                  snapdev::glob_to_list_flag_t::GLOB_FLAG_RECURSIVE
                , snapdev::glob_to_list_flag_t::GLOB_FLAG_IGNORE_ERRORS>(pattern))
        {
            SNAP_LOG_WARNING
                << "could not read \""
                << dir
                << "\" for zone configuration files."
                << SNAP_LOG_SEND;
            continue;
        }

        // now group the list of zones by domain name
        //
        for(auto const & g : glob)
        {
            advgetopt::conf_file_setup zone_setup(g);
            advgetopt::conf_file::pointer_t zone_file(advgetopt::conf_file::get_conf_file(zone_setup));
            zone_file->set_variables(f_opt.get_variables());
            std::string const domain(zone_file->get_parameter("domain"));
            if(domain.empty())
            {
                // force the re-definition of the domain name at all the
                // levels to confirm that everything is as it has to be
                //
                SNAP_LOG_ERROR
                    << "a domain name cannot be an empty string."
                    << SNAP_LOG_SEND;
                exit_code = 1;
                continue;
            }

            if(f_config_warnings)
            {
                std::string const domain_filename(snapdev::pathinfo::basename(g, ".conf"));
                if(domain_filename != domain)
                {
                    SNAP_LOG_WARNING
                        << "domain filename ("
                        << g
                        << ") does not corresponding to the domain defined in that file ("
                        << domain
                        << ")."
                        << SNAP_LOG_SEND;
                }
            }

            // verify the TLD with the libtld
            //
            struct tld_info info = {};
            enum tld_result const domain_validity(tld(domain.c_str(), &info));
            if(domain_validity != TLD_RESULT_SUCCESS)
            {
                SNAP_LOG_ERROR
                    << "domain \""
                    << g
                    << "\" does not seem to have a valid TLD."
                    << SNAP_LOG_SEND;
                exit_code = 1;
                continue;
            }

            // further validation of the file will happen later
            //
            f_zone_files[domain].f_config.push_back(zone_file);
        }
    }
    if(f_zone_files.empty())
    {
        // nothing, just return
        //
        SNAP_LOG_MINOR
            << "no zones found, bind not setup with any TLD."
            << SNAP_LOG_SEND;
        return 0;
    }

    return exit_code;
}


std::string ipmgr::get_zone_param(
          zone_files_t const & zone
        , std::string const & name
        , std::string const & default_name
        , std::string const & default_value)
{
    // we have to go in reverse and this is a vector so we simply use the
    // index going backward
    //
    std::size_t idx(zone.f_config.size());
    while(idx > 0)
    {
        --idx;

        if(zone.f_config[idx]->has_parameter(name))
        {
            return zone.f_config[idx]->get_parameter(name);
        }
    }

    if(!default_name.empty())
    {
        if(f_opt.is_defined(default_name))
        {
            return f_opt.get_string(default_name);
        }
    }

    return default_value;
}


bool ipmgr::get_zone_bool(
          zone_files_t const & zone
        , std::string const & name
        , std::string const & default_name
        , std::string const & default_value)
{
    std::string const value(get_zone_param(zone, name, default_name, default_value));

    if(advgetopt::is_true(value))
    {
        return true;
    }

    if(advgetopt::is_false(value))
    {
        return false;
    }

    SNAP_LOG_ERROR
        << "expected \""
        << name
        << "\" to be set to a boolean value (true/on/1 or false/off/0)."
        << SNAP_LOG_SEND;

    return false;
}


std::int32_t ipmgr::get_zone_integer(
          zone_files_t const & zone
        , std::string const & name
        , std::string const & default_name
        , std::int32_t default_value)
{
    std::string const value(get_zone_param(zone, name, default_name, std::string()));

    if(value.empty())
    {
        return default_value;
    }

    std::int64_t result(0);
    if(!advgetopt::validator_integer::convert_string(
              value
            , result))
    {
        SNAP_LOG_ERROR
            << "expected \""
            << name
            << "\" to be set to a valid integer; not \""
            << value
            << "\"."
            << SNAP_LOG_SEND;
        return -1;
    }

    if(result < INT_MIN
    || result > INT_MAX)
    {
        SNAP_LOG_ERROR
            << "\""
            << name
            << "\" integer \""
            << value
            << "\" is out of bounds."
            << SNAP_LOG_SEND;
        return -1;
    }

    return result;
}


std::int64_t ipmgr::get_zone_duration(
          zone_files_t const & zone
        , std::string const & name
        , std::string const & default_name
        , std::string const & default_value)
{
    std::string const value(get_zone_param(
                                      zone
                                    , name
                                    , default_name
                                    , default_value));

    double result(0.0);
    if(!advgetopt::validator_duration::convert_string(
              value
            , advgetopt::validator_duration::VALIDATOR_DURATION_DEFAULT_FLAGS
            , result))
    {
        SNAP_LOG_ERROR
            << "expected \""
            << name
            << "\" to be set to a duration (1h, 3d, 2w, 1m, 3s, 1y, or long form 1 hour, 3 days, ...)."
            << SNAP_LOG_SEND;
        return -1;
    }

    // round to the next second up
    //
    return static_cast<std::int64_t>(result + 0.5);
}


std::string ipmgr::get_zone_email(
          zone_files_t const & zone
        , std::string const & name
        , std::string const & default_name
        , std::string const & default_value)
{
    std::string const value(get_zone_param(
                                      zone
                                    , name
                                    , default_name
                                    , default_value));

    tld_email_list emails;
    if(emails.parse(value, 0) != TLD_RESULT_SUCCESS)
    {
        SNAP_LOG_ERROR
            << "\""
            << value
            << "\" is an invalid email address. Please verify that it"
               " includes a label, the @ character, and a valid domain name."
            << SNAP_LOG_SEND;
        return std::string();
    }

    if(emails.count() != 1)
    {
        SNAP_LOG_ERROR
            << "invalid email in configuration parameter \""
            << name
            << "\" ("
            << value
            << "); expected exactly one valid email address."
            << SNAP_LOG_SEND;
        return std::string();
    }

    // replace the '@' with a period
    // periods before the '@' get escaped with a backslash
    //
    tld_email_list::tld_email_t e;
    emails.next(e);
    std::string hostmaster(e.f_canonicalized_email);
    for(std::size_t idx(0); idx < hostmaster.length(); ++idx)
    {
        if(hostmaster[idx] == '.')
        {
            hostmaster.insert(idx, 1, '\\');
            ++idx;
        }
        else if(hostmaster[idx] == '@')
        {
            hostmaster[idx] = '.';
            break;
        }
    }

    return hostmaster;
}


std::uint32_t ipmgr::get_zone_serial(std::string const & domain, bool next)
{
    std::string path("/var/lib/ipmgr/serial/" + domain + ".counter");

    std::uint32_t serial(0);

    struct stat s = {};
    if(stat(path.c_str(), &s) != 0
    || s.st_size != sizeof(uint32_t))
    {
        std::ofstream out(path);
        out.write(reinterpret_cast<char *>(&serial), sizeof(std::uint32_t));
        if(!out.good())
        {
            SNAP_LOG_ERROR
                << "could not create new serial number file \""
                << path
                << "\" for zone of \""
                << domain
                << "\" domain."
                << SNAP_LOG_SEND;
            return 0;
        }
    }

    std::fstream file(path);
    file.read(reinterpret_cast<char *>(&serial), sizeof(std::uint32_t));
    if(next)
    {
        ++serial;
        if(serial == 0)
        {
            serial = 1;
        }
        file.seekp(0, std::ios_base::beg);
        file.write(reinterpret_cast<char *>(&serial), sizeof(std::uint32_t));
    }

    if(!file.good())
    {
        SNAP_LOG_ERROR
            << "could not read and/or write serial number to file \""
            << path
            << "\" for zone of \""
            << domain
            << "\" domain."
            << SNAP_LOG_SEND;
        return 0;
    }

    return serial;
}


//    ips=10.0.0.1
//    mail=mail
//
//    [websites]
//    sub_domains=${website_subdomains}
//    ips=10.0.2.1
//    ttl=5m
//
//    [api]
//    sub_domain="rest api graphql"
//    ips=${api_ip}
//
//    [mail]
//    sub_domains="mail"
//    ttl=4w
//    mail_priority=10
//    mail_key=mail.example.com.key
//    mail_ttl=5h
//
//    [info]
//    sub_domains="info"
//    txt="description=this domain is the best"

/** \brief Generate one zone.
 *
 * This function generates one zone from the specified configuration
 * info.
 *
 * The input is a zone as read by the read_zones() function.
 *
 * \return 0 on success, 1 on errors.
 */
int ipmgr::generate_zone(zone_files_t const & zone)
{
    // default TTL for the domain
    //
    std::int64_t const ttl(get_zone_duration(zone, "ttl", "default_ttl", "1d"));
    if(ttl < 0)
    {
        return 1;
    }

    // domain
    //
    std::string const domain(get_zone_param(zone, "domain"));

    // ips
    //
    advgetopt::string_list_t ips;
    advgetopt::split_string(
          get_zone_param(zone, "ips", "default_ips")
        , ips
        , {" ", ",", ";"});
    if(ips.size() == 0)
    {
        SNAP_LOG_ERROR
            << "You must defined at least one IP address."
            << SNAP_LOG_SEND;
        return 1;
    }

    // nameservers
    //
    std::string const default_nameservers("ns1." + domain + " ns2." + domain);
    std::string const nameserver_list(get_zone_param(zone, "nameservers", "default_nameservers", default_nameservers));
    advgetopt::string_list_t nameservers;
    advgetopt::split_string(
          nameserver_list
        , nameservers
        , {" ", ",", ":", ";"});
    if(nameservers.size() < 2)
    {
        SNAP_LOG_ERROR
            << "You must defined at least two nameservers."
            << SNAP_LOG_SEND;
        return 1;
    }

    // hostmaster email address
    //
    std::string const default_hostmaster("hostmaster@" + domain);
    std::string const hostmaster(get_zone_email(zone, "hostmaster", "default_hostmaster", default_hostmaster));
    if(hostmaster.empty())
    {
        return 1;
    }

    // get the current serial
    //
    std::uint32_t const serial(get_zone_serial(domain));
    if(serial == 0)
    {
        return 1;
    }

    // refresh
    //
    std::int64_t const refresh(get_zone_duration(zone, "refresh", "default_refresh", "3h"));
    if(refresh < 0)
    {
        return 1;
    }

    // retry
    //
    std::int64_t const retry(get_zone_duration(zone, "retry", "default_retry", "3m"));
    if(retry < 0)
    {
        return 1;
    }

    // exprire
    //
    std::int64_t const expire(get_zone_duration(zone, "expire", "default_expire", "2w"));
    if(expire < 0)
    {
        return 1;
    }

    // minimum_cache_failures
    //
    std::int64_t const minimum_cache_failures(get_zone_duration(
                      zone
                    , "minimum_cache_failures"
                    , "default_minimum_cache_failures"
                    , "5m"));
    if(minimum_cache_failures < 0)
    {
        return 1;
    }

    // mail section name (if any)
    //
    advgetopt::string_list_t mail_sub_domains;
    std::int32_t mail_priority(-1);
    std::int32_t mail_ttl(0);
    std::int32_t mail_default_ttl(0);
    std::string const mail_section(get_zone_param(zone, "mail"));
    if(!mail_section.empty())
    {
        advgetopt::split_string(
              get_zone_param(zone, mail_section + "::sub_domains", std::string(), "mail")
            , mail_sub_domains
            , {" ", ",", ";"});
        mail_priority = get_zone_integer(zone, mail_section + "::mail_priority", std::string(), 10);
        mail_default_ttl = get_zone_duration(zone, mail_section + "::ttl", "default_ttl", "0");
        if(mail_ttl < 0)
        {
            return 1;
        }
        mail_ttl = get_zone_duration(zone, mail_section + "::mail_ttl", std::string(), "0");
        if(mail_ttl < 0)
        {
            return 1;
        }
    }

    // dynamic?
    //
    bool const dynamic(get_zone_bool(zone, "dynamic", std::string(), "false"));

    if(dynamic)
    {

std::cerr << "TODO: dynamic zone\n";
    }
    else
    {
std::cerr << "static zone\n";
        std::stringstream zone_data;

        // ORIGIN
        zone_data << "$ORIGIN .\n";

        // TTL
        zone_data << "$TTL " << ttl << "\n";

        // SOA
        zone_data
            << domain
            << " IN SOA "
            << nameservers[0]
            << ". "
            << hostmaster
            << ". ("
            << serial
            << " "
            << refresh
            << " "
            << retry
            << " "
            << expire
            << " "
            << minimum_cache_failures
            << ")\n";

        for(auto const & ns : nameservers)
        {
            zone_data << "\tNS " << ns << ".\n";
        }

        if(!mail_sub_domains.empty())
        {
            for(auto const & sub_domain : mail_sub_domains)
            {
                zone_data << "\t";
                if(mail_ttl > 0)
                {
                    if(mail_ttl != ttl)
                    {
                        zone_data << std::max(mail_ttl, 60) << ' ';    // 1m minimum
                    }
                }
                else if(mail_default_ttl > 0)
                {
                    if(mail_default_ttl != ttl)
                    {
                        zone_data << std::max(mail_default_ttl, 60) << ' ';    // 1m minimum
                    }
                }
                zone_data << "MX " << mail_priority << '\t'
                          << sub_domain << '.' << domain << ".\n";

                // info about the SPF, DKIM, DMARC
                // https://support.google.com/a/answer/10583557
            }
        }

        for(auto const & ip : ips)
        {
            zone_data << "\tA\t" << ip << "\n";
        }

        zone_data << '\n' << "$ORIGIN " << domain << ".\n";

        advgetopt::conf_file::sections_t all_sections;
        for(auto const & c : zone.f_config)
        {
            advgetopt::conf_file::sections_t s(c->get_sections());
            all_sections.insert(s.begin(), s.end());
        }

        // we want all the sub-domains sorted so we use this intermediate
        // std::stringstream to generate a string that we save in a set and
        // afterward we write the strings in the set to the zone_data
        //
        std::set<std::string> sorted_sub_domains;
        for(auto const & s : all_sections)
        {
            // the name of a section is just that, a name
            // we use it here to get the info about that one section and
            // process the info by converting them into sub-domain definitions
            //
            if(s == g_iplock_options_environment.f_section_variables_name)
            {
                continue;
            }

            std::int32_t const sub_domain_ttl(get_zone_duration(zone, s + "::ttl", std::string(), "0"));
            if(sub_domain_ttl < 0)
            {
                return 1;
            }

            advgetopt::string_list_t sub_domain_txt;
            advgetopt::split_string(
                  get_zone_param(zone, s + "::txt")
                , sub_domain_txt
                , {" +++ "});

            std::string const sub_domains(get_zone_param(zone, s + "::sub_domains"));

            if(s[0] == 'g'      // section name starts with 'global-'
            && s[1] == 'l'
            && s[2] == 'o'
            && s[3] == 'b'
            && s[4] == 'a'
            && s[5] == 'l'
            && s[6] == '-')
            {
                if(!sub_domains.empty())
                {
                    SNAP_LOG_ERROR
                        << "global sections of a zone file definition must not include a list of sub-domains."
                        << SNAP_LOG_SEND;
                    return 1;
                }
                if(!get_zone_param(zone, s + "::ips").empty())
                {
                    SNAP_LOG_ERROR
                        << "global sections of a zone file definition must not include a list of IP addresses."
                        << SNAP_LOG_SEND;
                    return 1;
                }
                if(sub_domain_txt.empty())
                {
                    SNAP_LOG_ERROR
                        << "a sub-domain global section must have one  txt=... entry."
                        << SNAP_LOG_SEND;
                    return 1;
                }

                for(auto const & txt : sub_domain_txt)
                {
                    std::stringstream ss;

                    ss << '\t';

                    if(sub_domain_ttl != 0
                    && sub_domain_ttl != ttl)
                    {
                        ss << sub_domain_ttl << ' ';
                    }

                    ss << "TXT\t\"" << txt << '"';

                    sorted_sub_domains.insert(ss.str());
                }
            }
            else
            {
                if(sub_domains.empty())
                {
                    SNAP_LOG_ERROR
                        << "non-global sections of a zone file definition must include a list of one or more sub-domains."
                        << SNAP_LOG_SEND;
                    return 1;
                }
                advgetopt::string_list_t sub_domain_names;
                advgetopt::split_string(
                      sub_domains
                    , sub_domain_names
                    , {" ", ",", ";"});

                advgetopt::string_list_t sub_domain_ips;
                advgetopt::split_string(
                      get_zone_param(zone, s + "::ips")
                    , sub_domain_ips
                    , {" ", ",", ";"});
                if(sub_domain_ips.empty()
                && sub_domain_txt.empty())
                {
                    sub_domain_ips = ips;
                }

                if(sub_domain_ips.empty()
                && sub_domain_txt.empty())
                {
                    SNAP_LOG_ERROR
                        << "a sub-domain must have at least one IP address or a txt=... field defined."
                        << SNAP_LOG_SEND;
                    return 1;
                }
                else
                {
                    for(auto const & d : sub_domain_names)
                    {
                        if(!sub_domain_txt.empty())
                        {
                            for(auto const & txt : sub_domain_txt)
                            {
                                std::stringstream ss;

                                ss << d << '\t';

                                if(sub_domain_ttl != 0
                                && sub_domain_ttl != ttl)
                                {
                                    ss << sub_domain_ttl << ' ';
                                }

                                ss << "TXT\t\"" << txt << '"';

                                sorted_sub_domains.insert(ss.str());
                            }
                        }

                        if(!sub_domain_ips.empty())
                        {
                            for(auto const & ip : sub_domain_ips)
                            {
                                std::stringstream ss;

                                ss << d << '\t';

                                if(sub_domain_ttl != 0
                                && sub_domain_ttl != ttl)
                                {
                                    ss << sub_domain_ttl << ' ';
                                }

                                ss << "A\t" << ip;

                                sorted_sub_domains.insert(ss.str());
                            }
                        }
                    }
                }
            }
        }

        for(auto const & d : sorted_sub_domains)
        {
            zone_data << d << '\n';
        }

std::cerr << "result: [" << zone_data.str() << "]\n";
    }

    return 0;
}


/** \brief Process the input files one at a time.
 *
 * This function reads the list of zone files to be processed using
 * a glob and then it processes them one by one.
 *
 * \return 0 if no error occurred, 1 otherwise.
 */
int ipmgr::process_zones()
{
    int r(0);

    r = read_zones();
    if(r != 0)
    {
        return r;
    }

    for(auto const & z : f_zone_files)
    {
        r = generate_zone(z.second);
        if(r != 0)
        {
            return r;
        }
    }

    return 0;
}


/** \brief Restart bind9.
 *
 * This function checks whether the bind9 service needs to be restarted.
 * If so, then it checks whether it is currently active. If a restart is
 * not necessary or the service is not currently active, nothing happens.
 * Otherwise, it stops the process, removes all the .jnl files, and
 * finally restarts the process.
 *
 * \return 0 or 1 as the main() function expects
 */
int ipmgr::restart_bind9()
{
    // restart necessary?
    //
    if(access(g_bind9_need_restart, F_OK) != 0)
    {
        return 0;
    }

    // we do not want to force a stop & start if the process is not currently
    // active (i.e. it may have been stopped by the user for a while)
    //
    cppprocess::process is_active_process("is-active?");
    is_active_process.set_command("systemctl");
    is_active_process.add_argument("is-active");
    is_active_process.add_argument("bind9");

    cppprocess::io_capture_pipe::pointer_t output(std::make_shared<cppprocess::io_capture_pipe>());
    is_active_process.set_output_io(output);

    if(f_verbose)
    {
        std::cout << is_active_process.get_command_line() << std::endl;
    }

    if(!f_dry_run)
    {
        if(is_active_process.start() != 0)
        {
            SNAP_LOG_FATAL
                << "could not start \""
                << is_active_process.get_command_line()
                << "\"."
                << SNAP_LOG_SEND;
            return 1;
        }
        int const r(is_active_process.wait());
        if(r != 0)
        {
            SNAP_LOG_FATAL
                << "command \""
                << is_active_process.get_command_line()
                << "\" return an error (exit code "
                << r
                << ")."
                << SNAP_LOG_SEND;
            return 1;
        }
    }

    std::string const active(output->get_output(true));
    if(active != "active")
    {
        return 0;
    }

    // stop the DNS server
    //
    char const * stop_bind9("systemctl stop bind9");
    if(f_verbose)
    {
        std::cout << stop_bind9 << std::endl;
    }
    if(!f_dry_run)
    {
        int const r(system(stop_bind9));
        if(r != 0)
        {
            SNAP_LOG_FATAL
                << "could not stop the bind9 process (systemctl exit value: "
                << r
                << ")"
                << SNAP_LOG_SEND;
            return r;
        }
    }

    // clear the journals
    //
    char const * clear_journals("rm -f /var/lib/bind9/*.jnl");
    if(f_verbose)
    {
        std::cout << clear_journals << std::endl;
    }
    if(!f_dry_run)
    {
        int const r(system(clear_journals));
        if(r != 0)
        {
            SNAP_LOG_WARNING
                << "could not delete the journal (.jnl) files."
                << SNAP_LOG_SEND;
        }
    }

    // start the DNS server
    //
    char const * start_bind9("systemctl start bind9");
    if(f_verbose)
    {
        std::cout << start_bind9 << std::endl;
    }
    if(!f_dry_run)
    {
        int const r(system(start_bind9));
        if(r != 0)
        {
            SNAP_LOG_FATAL
                << "could not start the bind9 process (systemctl exit value: "
                << r
                << ")."
                << SNAP_LOG_SEND;
            return r;
        }
    }

    // remove the flag telling us that the restart we requested
    //
    std::string done_restart("rm -f ");
    done_restart += g_bind9_need_restart;
    if(f_verbose)
    {
        std::cout << done_restart << std::endl;
    }
    if(!f_dry_run)
    {
        int const r(system(done_restart.c_str()));
        if(r != 0)
        {
            SNAP_LOG_WARNING
                << "could not delete the \""
                << g_bind9_need_restart
                << "\" flag."
                << SNAP_LOG_SEND;
        }
    }

    return 0;
}


/** \brief Run the IP Manager.
 *
 * This command runs the IP Manager. This means:
 *
 * 1. Read zone files and process it
 *
 * 2. Save static zones under `/etc/bind/zones/...` and mark that we will
 *    have to restart the `named` server
 *
 * 3. Run `rndc` and/or `nsupdate` as required to update dynamic zones
 *
 * 4. If necessary (step 2. saved files) then restart the `named` service
 *
 * For step 2, we generate the new file and compare it to the old file.
 * If it did not change, then we do nothing more. If no old file exists
 * or something changed, then we overwrite the old file with the new and
 * mark that we want to restart the server (using a file under
 * `/run/ipmgr/...` in case something happens and the restart doesn't
 * happen on this run).
 *
 * \return The function returns 1 on errors and 0 on success (like what main()
 * is expected to return).
 */
int ipmgr::run()
{
    // some functionality requires us to modify files own by root or bind
    //
    int r(make_root());
    if(r != 0)
    {
std::cerr << "--- r = " << r << " (make_root()\n";
        return r;
    }

    r = process_zones();
    if(r != 0)
    {
        return r;
    }

    r = restart_bind9();
    if(r != 0)
    {
        return r;
    }

    return 0;
}



// vim: ts=4 sw=4 et
