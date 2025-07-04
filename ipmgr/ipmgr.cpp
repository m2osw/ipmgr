// Copyright (c) 2022-2025  Made to Order Software Corp.  All Rights Reserved.
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
#include    "exception.h"
#include    "version.h"


// advgetopt
//
#include    <advgetopt/validator_duration.h>
#include    <advgetopt/validator_integer.h>


// libaddr
//
#include    <libaddr/addr_parser.h>


// libtld
//
#include    <libtld/tld.h>


// cppprocess
//
#include    <cppprocess/io_capture_pipe.h>
#include    <cppprocess/process.h>


// snaplogger
//
#include    <snaplogger/message.h>
#include    <snaplogger/options.h>


// snapdev
//
#include    <snapdev/chownnm.h>
#include    <snapdev/pathinfo.h>
#include    <snapdev/file_contents.h>
#include    <snapdev/trim_string.h>


// boost
//
#include    <boost/preprocessor/stringize.hpp>


// C++
//
#include    <iostream>
#include    <fstream>
#include    <set>


// snapdev
//
#include    <snapdev/poison.h>



namespace
{


constexpr char const * const g_zone_directories_separator[] =
{
    " ",

    nullptr
};


constexpr char const * const g_ip_separator[] =
{
    " ",

    nullptr
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
                    , advgetopt::GETOPT_FLAG_PROCESS_VARIABLES>())
        , advgetopt::DefaultValue("2w")
        , advgetopt::Help("Define the amount of time to remember a value for, even if stale.")
    ),
    advgetopt::define_option(
          advgetopt::Name("default-group")
        , advgetopt::Flags(advgetopt::all_flags<
                      advgetopt::GETOPT_FLAG_GROUP_OPTIONS
                    , advgetopt::GETOPT_FLAG_REQUIRED
                    , advgetopt::GETOPT_FLAG_PROCESS_VARIABLES>())
        , advgetopt::Help("Default group name.")
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
                    , advgetopt::GETOPT_FLAG_PROCESS_VARIABLES>())
        , advgetopt::DefaultValue("5m")
        , advgetopt::Help("Define the amount of time to between retries to refresh the cache.")
    ),
    advgetopt::define_option(
          advgetopt::Name("default-refresh")
        , advgetopt::Flags(advgetopt::all_flags<
                      advgetopt::GETOPT_FLAG_GROUP_OPTIONS
                    , advgetopt::GETOPT_FLAG_REQUIRED
                    , advgetopt::GETOPT_FLAG_PROCESS_VARIABLES>())
        , advgetopt::DefaultValue("3h")
        , advgetopt::Help("Define the refresh rate of your secondary servers.")
    ),
    advgetopt::define_option(
          advgetopt::Name("default-retry")
        , advgetopt::Flags(advgetopt::all_flags<
                      advgetopt::GETOPT_FLAG_GROUP_OPTIONS
                    , advgetopt::GETOPT_FLAG_REQUIRED
                    , advgetopt::GETOPT_FLAG_PROCESS_VARIABLES>())
        , advgetopt::DefaultValue("3m")
        , advgetopt::Help("Define the retry rate in case the main server does not reply to your secondary servers.")
    ),
    advgetopt::define_option(
          advgetopt::Name("default-ttl")
        , advgetopt::Flags(advgetopt::all_flags<
                      advgetopt::GETOPT_FLAG_GROUP_OPTIONS
                    , advgetopt::GETOPT_FLAG_REQUIRED
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
        , advgetopt::DefaultValue("127.0.0.1")
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
          advgetopt::Name("force")
        , advgetopt::Flags(advgetopt::standalone_all_flags<
                      advgetopt::GETOPT_FLAG_GROUP_OPTIONS>())
        , advgetopt::Help("Force updates even if the files did not change.")
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
    .f_environment_variable_intro = nullptr,
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
    .f_groups = g_group_descriptions,
};
#pragma GCC diagnostic pop




char const * const g_bind9_need_restart = "/run/ipmgr/bind9-need-restart";
char const * const g_opendkim_need_restart = "/run/ipmgr/opendkim-need-restart";
char const * const g_opendmarc_need_restart = "/run/ipmgr/opendmarc-need-restart";



bool validate_domain(std::string const & domain)
{
    if(domain.empty())
    {
        SNAP_LOG_ERROR
            << "a domain name cannot be an empty string."
            << SNAP_LOG_SEND;
        return false;
    }

    if(domain.back() == '.')
    {
        SNAP_LOG_ERROR
            << "domain name \""
            << domain
            << "\" cannot end with a period; the ipmgr adds periods are required."
            << SNAP_LOG_SEND;
        return false;
    }

    struct tld_info info = {};
    enum tld_result const domain_validity(tld(domain.c_str(), &info));
    if(domain_validity != TLD_RESULT_SUCCESS)
    {
        SNAP_LOG_ERROR
            << "domain \""
            << domain
            << "\" does not seem to have a valid TLD."
            << SNAP_LOG_SEND;
        return false;
    }

    return true;
}


/** \brief Validate a list of IPs using the libaddr library.
 *
 * This function goes through a list of IPs to verify that they are
 * all either IPv4 or IPv6. If all the IPs are valid, then the function
 * returns true.
 *
 * This function is expected to be called only when numeric IP addresses
 * are expected. It supports a mix of IPv4 and IPv6 without issue.
 * Since we are setting up a domain name IP address, allowing an
 * named domain name wouldn't work (i.e. we would have to query ourselves).
 *
 * \warning
 * An empty list of IPs is considered valid by this function.
 *
 * \param[in] ip_list  A list of IP addresses.
 *
 * \return true if the list of IPs is considered valid.
 */
bool validate_ips(advgetopt::string_list_t & ip_list)
{
    std::size_t const max(ip_list.size());
    for(std::size_t idx(0); idx < max; ++idx)
    {
        std::string ip(ip_list[idx]);
        if(ip.empty())
        {
            // I don't think this can happen
            //
            continue;
        }

        // transform the IP to the IPv6 syntax supported by the addr parser
        // if it looks like it includes colons (i.e. we do not support ports)
        //
        if(ip[0] != '['
        && ip.find(':') != std::string::npos)
        {
            ip = '[' + ip;
            if(ip.back() != ']')
            {
                ip += ']';
            }
            ip_list[idx] = ip;
        }

        addr::addr_parser parser;
        parser.set_allow(addr::allow_t::ALLOW_ADDRESS, true);
        parser.set_allow(addr::allow_t::ALLOW_REQUIRED_ADDRESS, true);
        parser.set_allow(addr::allow_t::ALLOW_ADDRESS_LOOKUP, false);
        parser.set_allow(addr::allow_t::ALLOW_PORT, false);
        snapdev::NOT_USED(parser.parse(ip));
        if(parser.has_errors())
        {
            SNAP_LOG_ERROR
                << "could not parse IP address \""
                << ip_list[idx]
                << "\" (\""
                << ip
                << "\"); please verify that it is a valid IPv4 or IPv6 numeric address; error: "
                << parser.error_messages()
                << SNAP_LOG_SEND;
            return false;
        }
    }

    return true;
}



}
// no name namespace




ipmgr::zone_files::zone_files(advgetopt::getopt::pointer_t opt, bool verbose)
    : f_opt(opt)
    , f_dry_run(f_opt->is_defined("dry-run"))
    , f_verbose(verbose)
{
}


void ipmgr::zone_files::add(advgetopt::conf_file::pointer_t zone)
{
    f_configs.push_back(zone);
}


std::string ipmgr::zone_files::get_zone_param(
          std::string const & name
        , std::string const & default_name
        , std::string const & default_value) const
{
    // we have to go in reverse and this is a vector so we simply use the
    // index going backward
    //
    std::size_t idx(f_configs.size());
    while(idx > 0)
    {
        --idx;

        if(f_configs[idx]->has_parameter(name))
        {
            return f_configs[idx]->get_parameter(name);
        }
    }

    if(!default_name.empty())
    {
        if(f_opt->is_defined(default_name))
        {
            return f_opt->get_string(default_name);
        }
    }

    return default_value;
}


bool ipmgr::zone_files::get_zone_bool(
          std::string const & name
        , std::string const & default_name
        , std::string const & default_value) const
{
    std::string const value(get_zone_param(name, default_name, default_value));

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


std::int32_t ipmgr::zone_files::get_zone_integer(
          std::string const & name
        , std::string const & default_name
        , std::int32_t default_value) const
{
    std::string const value(get_zone_param(name, default_name));

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


std::int64_t ipmgr::zone_files::get_zone_duration(
          std::string const & name
        , std::string const & default_name
        , std::string const & default_value) const
{
    std::string const value(get_zone_param(
                                      name
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


std::string ipmgr::zone_files::get_zone_email(
          std::string const & name
        , std::string const & default_name
        , std::string const & default_value
        , bool allow_empty) const
{
    std::string const value(get_zone_param(
                                      name
                                    , default_name
                                    , default_value));
    if(allow_empty
    && value.empty())
    {
        return std::string();
    }

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

    tld_email_list::tld_email_t e;
    emails.next(e);
    return e.f_canonicalized_email;
}


/** \brief Retrieve the serial number of a zone.
 *
 * This function retrieves the serial number of a zone. If the zone is
 * dynamic, it reads the serial number from the SOA definition. This is
 * important since another tool may increase that number under our feet
 * (i.e. letsencrypt). For static zones, it reads the serial number from
 * a counter file.
 *
 * The counter files are saved under:
 *
 * \code
 *     /var/lib/ipmgr/serial/\<domain-name>.counter
 * \endcode
 *
 * Note that even when dynamic zones are used, a counter file is created.
 * This file is used in case the zone somehow disappears.
 *
 * \todo
 * Avoid hard coded path to serial counters and zone files to read the SOA.
 *
 * \param[in] next  Retrieve the next counter (i.e. if the current counter
 * is 5, then the function returns 6).
 *
 * \return The current or next serial number for this zone, or zero if the
 * retrieval failed.
 */
std::uint32_t ipmgr::zone_files::get_zone_serial(bool next)
{
    std::uint32_t serial(0);

#ifdef _DEBUG
    // the domain must be retrieved before the nameservers
    //
    if(f_domain.empty())
    {
        throw ipmgr_logic_error("get_zone_serial() called without a domain name defined");
    }
#endif

    // zones make use of the serial number found in our serial cache
    // note, however, that we make use of the serial from the SOA
    // when updating a dynamic file
    //
    std::string const path("/var/lib/ipmgr/serial/" + f_domain + ".counter");

    if(f_dynamic != dynamic_t::DYNAMIC_STATIC)
    {
        // this is a dynamic zone and each update to the zone imply an
        // increment to the SOA serial number and so we have to go get
        // that number "by hand" to make sure we are up to date when
        // regenerating the files
        //
        serial = 0;
        std::string const dynamic_filename("/var/lib/bind/" + f_domain + ".zone");
        std::ifstream in(dynamic_filename);
        std::string line;
        bool found(false);
        while(std::getline(in, line))
        {
            std::string ln(snapdev::trim_string(line));
            bool quoted(false);
            std::size_t len(ln.size());
            for(std::size_t i(0); i < len; ++i)
            {
                // TBD: do string support backslashes for escaping?
                //
                if(ln[i] == '"')
                {
                    quoted = !quoted;
                }
                else if(!quoted
                     && ln[i] == ';')
                {
                    ln = ln.substr(0, i);
                    break;
                }
            }

            if(!found)
            {
                advgetopt::string_list_t tokens;
                advgetopt::split_string(
                          ln
                        , tokens
                        , {" ", "\t"});
                if(tokens.size() >= 3
                && tokens[2] == "SOA")
                {
                    found = true;

                    // we found the SOA entry now we need to search for the
                    // parenthesis and then read the first number; the
                    // parenthesis is expected on the same line, but the
                    // number can be further down
                    //
                    std::string::size_type const n(ln.find('('));
                    if(n != std::string::npos)
                    {
                        ln = ln.substr(n + 1);
                    }
                }
            }

            if(found
            && !ln.empty())
            {
                // ln must include that number, it may be followed by
                // another number, though
                //
                advgetopt::string_list_t tokens;
                advgetopt::split_string(
                          ln
                        , tokens
                        , {" ", "\t"});
                if(tokens.size() > 0)
                {
                    std::int64_t s(0);
                    if(advgetopt::validator_integer::convert_string(tokens[0], s))
                    {
                        if(s > 0
                        && s < 65536)
                        {
                            serial = s;
                        }
                    }
                }

                break;
            }
        }
    }

    // allow for reading our static serial number if it failed reading
    // the SOA (maybe the file was deleted or it does not exist yet)
    //
    if(f_dynamic == dynamic_t::DYNAMIC_STATIC
    || serial == 0)
    {
        struct stat s = {};
        if(stat(path.c_str(), &s) != 0
        || s.st_size != sizeof(uint32_t))
        {
            if(!next)
            {
                if(serial == 0)
                {
                    serial = 1;
                }
                std::ofstream out(path);
                out.write(reinterpret_cast<char *>(&serial), sizeof(std::uint32_t));
                if(!out.good())
                {
                    SNAP_LOG_ERROR
                        << "could not create new serial number file \""
                        << path
                        << "\" for zone of \""
                        << f_domain
                        << "\" domain."
                        << SNAP_LOG_SEND;
                    return 0;
                }
            }
        }
        else
        {
            std::ifstream in(path);
            in.read(reinterpret_cast<char *>(&serial), sizeof(std::uint32_t));
            if(!in.good())
            {
                SNAP_LOG_ERROR
                    << "could not read serial number from file \""
                    << path
                    << "\" for zone of \""
                    << f_domain
                    << "\" static domain."
                    << SNAP_LOG_SEND;
                return 0;
            }

            if(serial == 0)
            {
                if(f_dynamic == dynamic_t::DYNAMIC_STATIC)
                {
                    SNAP_LOG_RECOVERABLE_ERROR
                        << "serial for \""
                        << f_domain
                        << "\" could not be read from our serial counter file."
                        << SNAP_LOG_SEND;
                }
                else
                {
                    SNAP_LOG_RECOVERABLE_ERROR
                        << "serial for \""
                        << f_domain
                        << "\" could not be read from the zone SOA."
                        << SNAP_LOG_SEND;
                }
                if(!next)
                {
                    serial = 1;
                }
            }
        }
    }

    if(next)
    {
        ++serial;
        if(serial == 0)
        {
            serial = 1;
        }

        std::ofstream out(path);
        out.write(reinterpret_cast<char *>(&serial), sizeof(std::uint32_t));
        if(!out.good())
        {
            SNAP_LOG_ERROR
                << "could not write serial number to file \""
                << path
                << "\" for zone of \""
                << f_domain
                << "\" domain."
                << SNAP_LOG_SEND;
            return 0;
        }
    }

    f_serial = serial;

    return f_serial;
}


std::string ipmgr::zone_files::get_zone_mail_subdomain() const
{
    if(f_mail_subdomains.empty())
    {
        return std::string();
    }

    return f_mail_subdomains[0] + '.' + f_domain;
}


bool ipmgr::zone_files::is_auth_server() const
{
    return f_auth_server;
}


std::string ipmgr::zone_files::get_ptr() const
{
    return f_ptr;
}


std::string ipmgr::zone_files::get_ptr_arpa() const
{
    // TBD: what happens for IPv6?
    //
    advgetopt::string_list_t list;
    advgetopt::split_string(
          f_ptr
        , list
        , {"."});
    if(list.size() != 4)
    {
        SNAP_LOG_ERROR
            << "right now we only support IPv4 PTRs."
            << SNAP_LOG_SEND;
    }
    return list[2] + '.' + list[1] + '.' + list[0] + ".in-addr.arpa";
}


bool ipmgr::zone_files::retrieve_fields()
{
    typedef bool (ipmgr::zone_files::*retrieve_func_t)();

    retrieve_func_t func_list[] =
    {
        // WARNING: for some fields, the order matters
        //
        &ipmgr::zone_files::retrieve_group,
        &ipmgr::zone_files::retrieve_domain,
        &ipmgr::zone_files::retrieve_ttl,
        &ipmgr::zone_files::retrieve_ptr,
        &ipmgr::zone_files::retrieve_ips,
        &ipmgr::zone_files::retrieve_nameservers,
        &ipmgr::zone_files::retrieve_hostmaster,
        &ipmgr::zone_files::retrieve_dynamic,
        &ipmgr::zone_files::retrieve_serial,
        &ipmgr::zone_files::retrieve_refresh,
        &ipmgr::zone_files::retrieve_retry,
        &ipmgr::zone_files::retrieve_expire,
        &ipmgr::zone_files::retrieve_minimum_cache_failures,
        &ipmgr::zone_files::retrieve_mail_fields,
        &ipmgr::zone_files::retrieve_all_sections,
    };

    for(auto f : func_list)
    {
        if(!(this->*f)())
        {
            return false;
        }
    }

    return true;
}


bool ipmgr::zone_files::retrieve_group()
{
    // the group is optional; if not defined, use a default
    //
    f_group = get_zone_param("group", "default_group", "domains");

    return true;
}


bool ipmgr::zone_files::retrieve_domain()
{
    f_domain = get_zone_param("domain");

    if(!validate_domain(f_domain))
    {
        SNAP_LOG_ERROR
            << "Domain \""
            << f_domain
            << "\" doesn't look valid."
            << SNAP_LOG_SEND;
        return false;
    }

    return true;
}


bool ipmgr::zone_files::retrieve_ttl()
{
    f_ttl = get_zone_duration("ttl", "default_ttl", "1d");
    if(f_ttl > 0 && f_ttl < 60)
    {
        f_ttl = 60; // 1m minimum
    }
    if(f_ttl < 0)
    {
        SNAP_LOG_ERROR
            << "Domain \""
            << f_domain
            << "\" has an invalid TTL definition."
            << SNAP_LOG_SEND;
        return false;
    }

    return true;
}


bool ipmgr::zone_files::retrieve_ptr()
{
    f_ptr = get_zone_param("ptr");
    if(f_ptr.empty())
    {
        return true;
    }

    addr::addr_parser parser;
    parser.set_allow(addr::allow_t::ALLOW_ADDRESS, true);
    parser.set_allow(addr::allow_t::ALLOW_REQUIRED_ADDRESS, true);
    parser.set_allow(addr::allow_t::ALLOW_ADDRESS_LOOKUP, false);
    parser.set_allow(addr::allow_t::ALLOW_PORT, false);
    addr::addr_range::vector_t r(parser.parse(f_ptr));
    addr::addr a(r[0].get_from());
    if(!a.is_ipv4())
    {
        SNAP_LOG_ERROR
            << "The ptr=... variable is currently limited to IPv4 addresses."
            << SNAP_LOG_SEND;
        return false;
    }

    f_ptr_ttl = get_zone_duration("ptr_ttl", std::string(), "12h");

    if(f_ptr_ttl < 0)
    {
        SNAP_LOG_ERROR
            << "Invalid PTR TTL for \""
            << f_domain
            << "\"."
            << SNAP_LOG_SEND;
        return false;
    }

    return true;
}


bool ipmgr::zone_files::retrieve_ips()
{
    advgetopt::split_string(
          get_zone_param("ips", "default_ips")
        , f_ips
        , {" ", ",", ";"});
    if(f_ips.size() == 0)
    {
        SNAP_LOG_ERROR
            << "You must defined at least one IP address defined in a zone."
            << SNAP_LOG_SEND;
        return false;
    }

    return validate_ips(f_ips);
}


bool ipmgr::zone_files::retrieve_nameservers()
{
#ifdef _DEBUG
    // the domain must be retrieved before the nameservers
    //
    if(f_domain.empty())
    {
        throw ipmgr_logic_error("retrieve_nameservers() called without a domain name defined");
    }
#endif

    std::string const default_nameservers("ns1." + f_domain + " ns2." + f_domain);
    std::string const nameserver_list(get_zone_param("nameservers", "default_nameservers", default_nameservers));
    advgetopt::string_list_t list;
    advgetopt::split_string(
          nameserver_list
        , list
        , {" ", ",", ":", ";"});
    if(list.size() < 2)
    {
        SNAP_LOG_ERROR
            << "You must defined at least two nameservers (found "
            << list.size()
            << " instead)."
            << SNAP_LOG_SEND;
        return false;
    }

    for(auto const & ns : list)
    {
        if(!validate_domain(ns))
        {
            SNAP_LOG_ERROR
                << "Validation of nameserver domain name \""
                << ns
                << "\" failed. Please verify that it is valid."
                << SNAP_LOG_SEND;
            return false;
        }
        if(f_nameservers.find(ns) != f_nameservers.end())
        {
            SNAP_LOG_ERROR
                << "Nameserver \""
                << ns
                << "\" found twice in zone \""
                << f_domain
                << "\"."
                << SNAP_LOG_SEND;
            return false;
        }
        f_nameservers[ns] = std::string();
    }

    return true;
}


bool ipmgr::zone_files::retrieve_hostmaster()
{
#ifdef _DEBUG
    // the domain must be retrieved before the nameservers
    //
    if(f_domain.empty())
    {
        throw ipmgr_logic_error("retrieve_nameservers() called without a domain name defined");
    }
#endif

    std::string const default_hostmaster("hostmaster@" + f_domain);
    f_hostmaster = get_zone_email(
                              "hostmaster"
                            , "default_hostmaster"
                            , default_hostmaster);
    if(f_hostmaster.empty())
    {
        // this should never happen since we have a default
        //
        SNAP_LOG_ERROR
            << "Host master email address for \""
            << f_domain
            << "\" is empty."
            << SNAP_LOG_SEND;
        return false;
    }

    // replace the '@' with a period
    // periods before the '@' get escaped with a backslash
    //
    for(std::size_t idx(0); idx < f_hostmaster.length(); ++idx)
    {
        if(f_hostmaster[idx] == '.')
        {
            f_hostmaster.insert(idx, 1, '\\');
            ++idx;
        }
        else if(f_hostmaster[idx] == '@')
        {
            f_hostmaster[idx] = '.';

            // and done!
            //
            break;
        }
    }

    return true;
}


bool ipmgr::zone_files::retrieve_serial()
{
    return get_zone_serial() != 0;
}


bool ipmgr::zone_files::retrieve_refresh()
{
    f_refresh = get_zone_duration("refresh", "default_refresh", "3h");

    if(f_refresh < 0)
    {
        SNAP_LOG_ERROR
            << "Invalid SOA refresh time for \""
            << f_domain
            << "\"."
            << SNAP_LOG_SEND;
        return false;
    }

    return true;
}


bool ipmgr::zone_files::retrieve_retry()
{
    f_retry = get_zone_duration("retry", "default_retry", "3m");

    if(f_retry < 0)
    {
        SNAP_LOG_ERROR
            << "Invalid SOA retry time for \""
            << f_domain
            << "\"."
            << SNAP_LOG_SEND;
        return false;
    }

    return true;
}


bool ipmgr::zone_files::retrieve_expire()
{
    f_expire = get_zone_duration("expire", "default_expire", "2w");

    if(f_expire < 0)
    {
        SNAP_LOG_ERROR
            << "Invalid SOA expire time for \""
            << f_domain
            << "\"."
            << SNAP_LOG_SEND;
        return false;
    }

    return true;
}


bool ipmgr::zone_files::retrieve_minimum_cache_failures()
{
    f_minimum_cache_failures = get_zone_duration(
                                  "minimum_cache_failures"
                                , "default_minimum_cache_failures"
                                , "5m");

    if(f_minimum_cache_failures < 0)
    {
        SNAP_LOG_ERROR
            << "Invalid SOA minimum cache failures time for \""
            << f_domain
            << "\"."
            << SNAP_LOG_SEND;
        return false;
    }

    return true;
}


// TODO: each subdomain needs its own priority, TTL, etc.
//       with the current scheme we do not support such; for now
//       we really only support one mail service per domain;
//       you can still use a round robbin (multiple IPs), or even better,
//       a proxy which properly balances your mail servers
//
bool ipmgr::zone_files::retrieve_mail_fields()
{
#ifdef _DEBUG
    // the domain must be retrieved before the nameservers
    //
    if(f_domain.empty())
    {
        throw ipmgr_logic_error("retrieve_nameservers() called without a domain name defined");
    }
#endif

    std::string const mail_section(get_zone_param("mail"));
    if(mail_section.empty())
    {
        // no MX for this domain; this is a valid case so return true
        //
        return true;
    }

    // verify that this section doesn't use CNAME which is illegal for
    // an MX entry
    //
    if(!get_zone_param(mail_section + "::cname").empty())
    {
        SNAP_LOG_ERROR
            << "Mail server MX must be given IP addresses, not a cname; please fix \""
            << f_domain
            << "\"."
            << SNAP_LOG_SEND;
        return false;
    }

    // list of subdomains (default to "mail")
    //
    advgetopt::split_string(
          get_zone_param(mail_section + "::subdomains", std::string(), "mail")
        , f_mail_subdomains
        , {" ", ",", ";"});

    for(auto const & subdomain : f_mail_subdomains)
    {
        if(!validate_domain(subdomain + '.' + f_domain))
        {
            SNAP_LOG_ERROR
                << "Validation of mail subdomain name \""
                << subdomain
                << "\" failed. Please verify that it is valid."
                << SNAP_LOG_SEND;
            return false;
        }
    }

    // the MX priority
    //
    f_mail_priority = get_zone_integer(mail_section + "::mail_priority", std::string(), 10);
    if(f_mail_priority < 0)
    {
        return false;
    }

    // the MX default TTL in case the `mail_ttl` field is not defined
    //
    f_mail_default_ttl = get_zone_duration(mail_section + "::ttl", "default_ttl", "0");
    if(f_mail_default_ttl < 0)
    {
        return false;
    }
    if(f_mail_default_ttl > 0 && f_mail_default_ttl < 60)
    {
        f_mail_default_ttl = 60;
    }

    // the MX TTL
    //
    f_mail_ttl = get_zone_duration(mail_section + "::mail_ttl", std::string(), "0");
    if(f_mail_ttl < 0)
    {
        return false;
    }
    if(f_mail_ttl > 0 && f_mail_ttl < 60)
    {
        f_mail_ttl = 60;
    }

    // the corresponding key TTL
    //
    f_key_ttl = get_zone_duration(mail_section + "::key_ttl", std::string(), "1800");
    if(f_key_ttl < 0)
    {
        return false;
    }
    if(f_key_ttl < 60)
    {
        f_key_ttl = 60;
    }

    f_auth_server = get_zone_bool(mail_section + "::auth_server", std::string(), "false");

    f_dmarc_rua = get_zone_email(mail_section + "::dmarc_rua", std::string(), std::string(), true);
    f_dmarc_ruf = get_zone_email(mail_section + "::dmarc_ruf", std::string(), std::string(), true);

    return true;
}


bool ipmgr::zone_files::retrieve_dynamic()
{
    std::string const dynamic(get_zone_param("dynamic"));
    if(dynamic.empty() || dynamic == "static")
    {
        f_dynamic = dynamic_t::DYNAMIC_STATIC;
    }
    else if(dynamic == "letsencrypt")
    {
        f_dynamic = dynamic_t::DYNAMIC_LETSENCRYPT;
    }
    else if(dynamic == "local")
    {
        f_dynamic = dynamic_t::DYNAMIC_LOCAL;
    }
    else if(dynamic == "both")
    {
        f_dynamic = dynamic_t::DYNAMIC_BOTH;
    }
    else
    {
        SNAP_LOG_ERROR
            << "Validation of dynamic keywrod \""
            << dynamic
            << "\" failed. Please try with \"static\", \"letsencrypt\", or \"local\"."
            << SNAP_LOG_SEND;
        return false;
    }

    return true;
}


bool ipmgr::zone_files::retrieve_all_sections()
{
    for(auto const & c : f_configs)
    {
        advgetopt::conf_file::sections_t s(c->get_sections());
        f_sections.insert(s.begin(), s.end());
    }

    return true;
}


std::string ipmgr::zone_files::group() const
{
    return f_group;
}


std::string ipmgr::zone_files::domain() const
{
#ifdef _DEBUG
    // the domain must be retrieved before the nameservers
    //
    if(f_domain.empty())
    {
        throw ipmgr_logic_error("domain() called without a domain name defined");
    }
#endif

    return f_domain;
}


ipmgr::zone_files::dynamic_t ipmgr::zone_files::dynamic() const
{
    return f_dynamic;
}


std::string ipmgr::zone_files::generate_zone_file()
{
    std::stringstream zone_data;

    // warning
    zone_data << "; WARNING -- auto-generated file; see `man ipmgr` for details.\n";

    // ORIGIN
    zone_data << "$ORIGIN .\n";

    // TTL (global time to live)
    zone_data << "$TTL " << f_ttl << "\n";

    // SOA
    zone_data
        << f_domain
        << " IN SOA "
        << f_nameservers.begin()->first
        << ". "
        << f_hostmaster
        << ". ("
        << f_serial
        << " "
        << f_refresh
        << " "
        << f_retry
        << " "
        << f_expire
        << " "
        << f_minimum_cache_failures
        << ")\n";

    // list of nameservers
    //
    for(auto const & ns : f_nameservers)
    {
        zone_data << "\tNS " << ns.first << ".\n";
    }

    // MX entries if this domain supports mail
    //
    if(!f_mail_subdomains.empty())
    {
        for(auto const & subdomain : f_mail_subdomains)
        {
            zone_data << "\t";
            if(f_mail_ttl > 0)
            {
                if(f_mail_ttl != f_ttl)
                {
                    zone_data << f_mail_ttl << ' ';    // 1m minimum
                }
            }
            else if(f_mail_default_ttl > 0)
            {
                if(f_mail_default_ttl != f_ttl)
                {
                    zone_data << f_mail_default_ttl << ' ';    // 1m minimum
                }
            }
            zone_data << "MX";
            if(f_mail_priority > 0)
            {
                zone_data << ' ' << f_mail_priority;
            }
            zone_data << '\t' << subdomain << '.' << f_domain << ".\n";

            // TODO: look into automatically handling the mail server keys
            //
            // info about the SPF, DKIM, DMARC
            // https://support.google.com/a/answer/10583557
        }
    }

    // domain IP addresses
    //
    for(auto const & ip : f_ips)
    {
        addr::addr_parser parser;
        parser.set_allow(addr::allow_t::ALLOW_ADDRESS, true);
        parser.set_allow(addr::allow_t::ALLOW_REQUIRED_ADDRESS, true);
        parser.set_allow(addr::allow_t::ALLOW_ADDRESS_LOOKUP, false);
        parser.set_allow(addr::allow_t::ALLOW_PORT, false);
        addr::addr_range::vector_t r(parser.parse(ip));
        addr::addr a(r[0].get_from());

        if(a.is_ipv4())
        {
            zone_data << "\tA";
        }
        else
        {
            zone_data << "\tAAAA";
        }
        zone_data << '\t' << a.to_ipv4or6_string(addr::STRING_IP_ADDRESS) << '\n';
    }

    // we want all the subdomains sorted so we use this intermediate
    // std::stringstream to generate a string that we save in a set and
    // afterward we write the strings in the set to the zone_data
    //
    std::set<std::string> unique_nameserver_ips;
    std::set<std::string> sorted_domains;
    std::set<std::string> sorted_subdomains;
    for(auto const & s : f_sections)
    {
        if(s == g_iplock_options_environment.f_section_variables_name)
        {
            // this is a [variables] section, ignore
            //
            // TODO: in the advgetopt, we should handle this one for
            //       config files loaded at a later time
            //
            continue;
        }

        // the name of a section is just that, a name
        // we use the name to access the info of that section, which
        // describes one or more subdomains
        //
        std::int32_t const subdomain_ttl(get_zone_duration(s + "::ttl", std::string(), "0"));
        if(subdomain_ttl < 0)
        {
            return std::string();
        }

        advgetopt::string_list_t subdomain_txt;
        advgetopt::split_string(
              get_zone_param(s + "::txt")
            , subdomain_txt
            , {" +++ "});

        std::string const subdomains(get_zone_param(s + "::subdomains"));

        if(s[0] == 'g'      // section name starts with 'global-'
        && s[1] == 'l'
        && s[2] == 'o'
        && s[3] == 'b'
        && s[4] == 'a'
        && s[5] == 'l'
        && s[6] == '-')
        {
            if(!subdomains.empty())
            {
                SNAP_LOG_ERROR
                    << "global sections of a zone file definition must not include a list of subdomains."
                    << SNAP_LOG_SEND;
                return std::string();
            }
            if(!get_zone_param(s + "::ips").empty())
            {
                SNAP_LOG_ERROR
                    << "global sections of a zone file definition must not include a list of IP addresses."
                    << SNAP_LOG_SEND;
                return std::string();
            }
            if(!get_zone_param(s + "::cname").empty())
            {
                SNAP_LOG_ERROR
                    << "global sections of a zone file definition must not include a cname=... parameter."
                    << SNAP_LOG_SEND;
                return std::string();
            }
            if(subdomain_txt.empty())
            {
                SNAP_LOG_ERROR
                    << "a subdomain global section must have one  txt=... entry. To enter multiple TXT entries, use +++ delimited by spaces to separate each one as in: txt=one +++ two."
                    << SNAP_LOG_SEND;
                return std::string();
            }

            for(auto const & txt : subdomain_txt)
            {
                std::stringstream ss;

                ss << '\t';

                if(subdomain_ttl != 0
                && subdomain_ttl != f_ttl)
                {
                    ss << subdomain_ttl << ' ';
                }

                ss << "TXT\t\"" << txt << '"';

                sorted_domains.insert(ss.str());
            }
        }
        else
        {
            if(subdomains.empty())
            {
                SNAP_LOG_ERROR
                    << "non-global section \""
                    << s
                    << "\" of zone \""
                    << f_domain
                    << "\" must include a list of one or more subdomains."
                    << SNAP_LOG_SEND;
                return std::string();
            }

            std::string const cname(get_zone_param(s + "::cname"));

            advgetopt::string_list_t subdomain_names;
            advgetopt::split_string(
                  subdomains
                , subdomain_names
                , {" ", ",", ";"});

            advgetopt::string_list_t subdomain_ips;
            advgetopt::split_string(
                  get_zone_param(s + "::ips")
                , subdomain_ips
                , {" ", ",", ";"});
            if(subdomain_ips.empty()
            && subdomain_txt.empty()
            && cname.empty())
            {
                subdomain_ips = f_ips;
            }

            if(((subdomain_ips.empty() ? 0 : 1)
                 + (subdomain_txt.empty() ? 0 : 1)
                 + (cname.empty() ? 0 : 1)) > 1)
            {
                SNAP_LOG_ERROR
                    << "a subdomain must have only one of IP addresses, a cname=..., or a txt=... field defined simultaneously."
                    << SNAP_LOG_SEND;
                return std::string();
            }

            if(subdomain_ips.empty()
            && subdomain_txt.empty()
            && cname.empty())
            {
                SNAP_LOG_ERROR
                    << "a subdomain must have at least one IP address, a cname=..., or a txt=... field defined."
                    << SNAP_LOG_SEND;
                return std::string();
            }

            if(!subdomain_ips.empty())
            {
                if(!validate_ips(subdomain_ips))
                {
                    return std::string();
                }
            }

            for(auto const & d : subdomain_names)
            {
                if(!subdomain_txt.empty())
                {
                    for(auto const & txt : subdomain_txt)
                    {
                        std::stringstream ss;

                        ss << d << '\t';

                        if(subdomain_ttl != 0
                        && subdomain_ttl != f_ttl)
                        {
                            ss << subdomain_ttl << ' ';
                        }

                        ss << "TXT\t\"" << txt << '"';

                        sorted_subdomains.insert(ss.str());
                    }
                }

                if(!subdomain_ips.empty())
                {
                    for(auto const & ip : subdomain_ips)
                    {
                        std::stringstream ss;

                        addr::addr_parser parser;
                        parser.set_allow(addr::allow_t::ALLOW_ADDRESS, true);
                        parser.set_allow(addr::allow_t::ALLOW_REQUIRED_ADDRESS, true);
                        parser.set_allow(addr::allow_t::ALLOW_ADDRESS_LOOKUP, false);
                        parser.set_allow(addr::allow_t::ALLOW_PORT, false);
                        addr::addr_range::vector_t r(parser.parse(ip));
                        addr::addr a(r[0].get_from());
                        std::string const address(a.to_ipv4or6_string(addr::STRING_IP_ADDRESS));

                        auto it(f_nameservers.find(d + '.' + f_domain));
                        if(it != f_nameservers.end())
                        {
                            if(!it->second.empty()
                            && it->second != address)
                            {
                                SNAP_LOG_ERROR
                                    << "a subdomain nameserver can only be given one IP address, found "
                                    << it->second
                                    << " and "
                                    << address
                                    << " for "
                                    << it->first
                                    << "."
                                    << SNAP_LOG_SEND;
                                return std::string();
                            }
                            else
                            {
                                it->second = address;
                                auto unique(unique_nameserver_ips.find(address));
                                if(unique != unique_nameserver_ips.end())
                                {
                                    SNAP_LOG_ERROR
                                        << "each nameserver subdomain must have a unique IP address, found "
                                        << address
                                        << " twice, check subdomain \""
                                        << it->first
                                        << "\"."
                                        << SNAP_LOG_SEND;
                                    return std::string();
                                }
                                else
                                {
                                    unique_nameserver_ips.insert(address);
                                }
                            }
                        }

                        ss << d << '\t';

                        if(subdomain_ttl != 0
                        && subdomain_ttl != f_ttl)
                        {
                            ss << subdomain_ttl << ' ';
                        }

                        if(a.is_ipv4())
                        {
                            ss << 'A';
                        }
                        else
                        {
                            ss << "AAAA";
                        }
                        ss << "\t" << address;

                        sorted_subdomains.insert(ss.str());
                    }
                }

                if(!cname.empty())
                {
                    auto it(f_nameservers.find(d + '.' + f_domain));
                    if(it != f_nameservers.end())
                    {
                        SNAP_LOG_ERROR
                            << "nameserver \""
                            << d
                            << "\" can't be used with CNAME."
                            << SNAP_LOG_SEND;
                        return std::string();
                    }

                    std::stringstream ss;

                    ss << d << '\t';

                    if(subdomain_ttl != 0
                    && subdomain_ttl != f_ttl)
                    {
                        ss << subdomain_ttl << ' ';
                    }

                    ss << "CNAME\t";
                    if(cname == ".")
                    {
                        ss << f_domain << '.';
                    }
                    else if(cname.back() == '.')
                    {
                        if(!validate_domain(cname.substr(0, cname.length() - 1)))
                        {
                            return std::string();
                        }

                        // if it ends with a period we assume it's a full
                        // domain name and only output the `cname` content
                        //
                        ss << cname;
                    }
                    else
                    {
                        std::string const link(cname + '.' + f_domain);
                        if(!validate_domain(link))
                        {
                            return std::string();
                        }

                        // assume cname is a subdomain of this domain
                        //
                        ss << link << '.';
                    }

                    sorted_subdomains.insert(ss.str());
                }
            }
        }
    }

    for(auto const & d : sorted_domains)
    {
        zone_data << d << '\n';
    }

    if(!f_mail_subdomains.empty())
    {
        // spf v1
        //
        // https://en.wikipedia.org/wiki/Sender_Policy_Framework
        //
        zone_data
            << '\t'
            << f_key_ttl
            << " TXT\t\"v=spf1 a:"
            << f_mail_subdomains[0]
            << '.'
            << f_domain
            << " a:"
            << f_domain
            << " -all\"\n";
    }

    // switch to the subdomains now
    //
    zone_data << "$ORIGIN " << f_domain << ".\n";

    // if there is an MX, handle the special fields for that
    //
    if(!f_mail_subdomains.empty())
    {
        // opendkim
        //
        std::string opendkim_path("/etc/opendkim/");
        std::string path(opendkim_path);
        path += f_domain;
        path += ".key";
        if(snapdev::mkdir_p(path) != 0)
        {
            SNAP_LOG_ERROR
                << "failed creating \"/etc/opendkim\" for domain \""
                << f_domain
                << "\"."
                << SNAP_LOG_SEND;
            return std::string();
        }
        std::string mailtxt(path);
        mailtxt += "/mail.txt";
        struct stat s;
        if(stat(mailtxt.c_str(), &s) != 0)
        {
            if(errno != ENOENT)
            {
                SNAP_LOG_ERROR
                    << "access to \""
                    << mailtxt
                    << "\" failed for domain \""
                    << f_domain
                    << "\"."
                    << SNAP_LOG_SEND;
                return std::string();
            }

            // the key doesn't exist yet, create it now
            //
            std::string cmd("opendkim-genkey --directory=");
            cmd += path;
            cmd += " --selector=";
            cmd += f_mail_subdomains[0]; // TBD: I'm not so sure this is correct, but it works for us...
            cmd += " --domain='";
            cmd += f_domain;
            cmd += "'";

            if(f_verbose)
            {
                std::cout
                    << "info: "
                    << cmd
                    << std::endl;
            }
            if(!f_dry_run)
            {
                int const r(system(cmd.c_str()));
                if(r != 0)
                {
                    SNAP_LOG_FATAL
                        << "could not generate an OpenDKIM key for \""
                        << f_domain
                        << "\" (exit code = "
                        << r
                        << ")."
                        << SNAP_LOG_SEND;
                    return std::string();
                }

                if(stat(mailtxt.c_str(), &s) != 0)
                {
                    SNAP_LOG_FATAL
                        << "command \""
                        << cmd
                        << "\" did not generate expected file \""
                        << mailtxt
                        << "\" for \""
                        << f_domain
                        << "\"."
                        << SNAP_LOG_SEND;
                    return std::string();
                }

                // it worked, update the corresponding tables

                // signing_table
                //
                std::string signing_filename(opendkim_path);
                signing_filename += "/signing_table";
                snapdev::file_contents signing_file(signing_filename);
                signing_file.read_all();
                std::string contents(signing_file.contents());
                std::string::size_type pos(contents.find(f_domain));
                if(pos != std::string::npos)
                {
                    // remove that line if it exists
                    //
                    std::string::size_type end(contents.find('\n', pos));
                    if(end == std::string::npos)
                    {
                        end = contents.length();
                    }
                    else
                    {
                        ++end;
                    }
                    contents = contents.substr(0, pos)
                             + contents.substr(end);
                }
                std::string key_id(f_mail_subdomains[0]);
                key_id += "._domainkey.";
                key_id += f_domain;
                if(contents.empty())
                {
                    contents = "# WARNING: AUTO-GENERATED FILE, SEE ipmgr(1) FOR DETAILS\n";
                }
                contents += f_domain;
                contents += ' ';
                contents += key_id;
                contents += '\n';
                if(signing_file.contents() != contents)
                {
                    signing_file.contents(contents);
                    if(!signing_file.write_all())
                    {
                        SNAP_LOG_FATAL
                            << "an I/O error occurred trying to write to \""
                            << signing_filename
                            << "\" for \""
                            << f_domain
                            << "\"."
                            << SNAP_LOG_SEND;
                        return std::string();
                    }
                }

                // key_table
                //
                std::string key_filename(opendkim_path);
                key_filename += "/key_table";
                snapdev::file_contents key_file(key_filename);
                key_file.read_all();
                contents = key_file.contents();
                pos = contents.find(key_id);
                if(pos != std::string::npos)
                {
                    // remove that line if it exists
                    //
                    std::string::size_type end(contents.find('\n', pos));
                    if(end == std::string::npos)
                    {
                        end = contents.length();
                    }
                    else
                    {
                        ++end;
                    }
                    contents = contents.substr(0, pos)
                             + contents.substr(end);
                }
                if(contents.empty())
                {
                    contents = "# WARNING: AUTO-GENERATED FILE, SEE ipmgr(1) FOR DETAILS\n";
                }
                contents += key_id;
                contents += ' ';
                contents += f_domain;
                contents += ":mail:";
                contents += path;
                contents += "/mail.private\n";
                if(key_file.contents() != contents)
                {
                    key_file.contents(contents);
                    if(!key_file.write_all())
                    {
                        SNAP_LOG_FATAL
                            << "an I/O error occurred trying to write to \""
                            << key_filename
                            << "\" for \""
                            << f_domain
                            << "\"."
                            << SNAP_LOG_SEND;
                        return std::string();
                    }
                }

                snapdev::file_contents flag(g_opendkim_need_restart, true);
                flag.contents("*** opendkim restart required ***\n");
                if(!flag.write_all())
                {
                    SNAP_LOG_MINOR
                        << "could not write to file \""
                        << g_opendkim_need_restart
                        << "\": "
                        << flag.last_error()
                        << SNAP_LOG_SEND;
                }
            }
        }
        else if(!S_ISREG(s.st_mode))
        {
            SNAP_LOG_FATAL
                << "\""
                << mailtxt
                << "\" is not a regular file for \""
                << f_domain
                << "\"."
                << SNAP_LOG_SEND;
            return std::string();
        }
        snapdev::file_contents txt(mailtxt);
        if(!txt.read_all())
        {
            // this is normal in a dry-run, otherwise we should
            // have failed earlier anyway
            //
            SNAP_LOG_WARNING
                << "OpenDKIM key for \""
                << f_domain
                << "\" not available."
                << SNAP_LOG_SEND;
        }
        else
        {
            zone_data << "adsp._domainkey\t" << f_key_ttl << " TXT\t\"dkim=all\"\n";

            // the opendkim-genkey generates a key file, but the line does
            // not include a TTL so we do a little bit of work on it
            //
            std::string const & key(txt.contents());
            for(char const * k(key.c_str());; ++k)
            {
                if(*k == '\0')
                {
                    SNAP_LOG_FATAL
                        << "OpenDKIM key for \""
                        << f_domain
                        << "\" does not include any blanks."
                        << SNAP_LOG_SEND;
                    return std::string();
                }
                if(isspace(*k))
                {
                    zone_data << '\t';
                    zone_data << f_key_ttl;
                    zone_data << ' ';
                    do
                    {
                        ++k;
                    }
                    while(*k != '\0' && isspace(*k));
                    if(k[0] == 'I' && k[1] == 'N' && isspace(k[2]))
                    {
                        k += 3;
                        while(*k != '\0' && isspace(*k))
                        {
                            ++k;
                        }
                    }
                    zone_data << k;
                    break;
                }
                zone_data << *k;
            }
        }

        // opendmarc
        //
        zone_data
            << "_dmarc\t"
            << f_key_ttl
            << " TXT\t\"v=DMARC1; p=quarantine;";

        if(!f_dmarc_rua.empty())
        {
            zone_data
                << " rua:"
                << f_dmarc_rua
                << ';';
        }

        if(!f_dmarc_ruf.empty())
        {
            zone_data
                << " ruf:"
                << f_dmarc_ruf
                << ';';
        }

        zone_data
            << " fo=0; adkim=r; aspf=r; pct=100; rf=afrf; sp=quarantine\"\n";
    }

    for(auto const & d : sorted_subdomains)
    {
        zone_data << d << '\n';
    }

    zone_data << "; vim: ts=25\n";

    // verify the final zone
    //
    {
        std::string const zone_to_verify("/run/ipmgr/verify.zone");
        snapdev::file_contents temp(zone_to_verify, true, true);
        temp.contents(zone_data.str());
        if(!temp.write_all())
        {
            SNAP_LOG_FATAL
                << "the generated zone could not be saved in \""
                << zone_to_verify
                << "\" for verification."
                << SNAP_LOG_SEND;
            return std::string();
        }

        std::string verify_zone("named-checkzone ");
        verify_zone += f_domain;
        verify_zone += ' ';
        verify_zone += zone_to_verify;
        if(f_verbose)
        {
            std::cout
                << "info: "
                << verify_zone
                << std::endl;
        }

        cppprocess::process named_checkzone("named-verification");
        named_checkzone.set_command("named-checkzone");
        named_checkzone.add_argument(f_domain);
        named_checkzone.add_argument(zone_to_verify);

        cppprocess::io_capture_pipe::pointer_t output(std::make_shared<cppprocess::io_capture_pipe>());
        named_checkzone.set_output_io(output);

        cppprocess::io_capture_pipe::pointer_t error(std::make_shared<cppprocess::io_capture_pipe>());
        named_checkzone.set_error_io(error);

        if(f_verbose)
        {
            std::cout
                << "info: "
                << named_checkzone.get_command_line()
                << std::endl;
        }

        if(named_checkzone.start() != 0)
        {
            SNAP_LOG_FATAL
                << "could not start \""
                << named_checkzone.get_command_line()
                << "\"."
                << SNAP_LOG_SEND;
            return std::string();
        }

        int const r(named_checkzone.wait());
        if(r != 0)
        {
            std::string const results(snapdev::trim_string(output->get_output(true)));
            std::string const errmsg(snapdev::trim_string(error->get_output(true)));

            SNAP_LOG_FATAL
                << "command \""
                << named_checkzone.get_command_line()
                << "\" returned an error (exit code "
                << r
                << "): stdout \""
                << results
                << "\" -- stderr \""
                << errmsg
                << "\"."
                << SNAP_LOG_SEND;
            return std::string();
        }
    }

    return zone_data.str();
}


std::string ipmgr::zone_files::generate_ptr_file()
{
    // at the moment, I think this should not happen, but I'd have to test
    // to make 100% sure
    //
    if(f_nameservers.empty())
    {
        return std::string();
    }

    std::stringstream zone_data;

    // warning
    zone_data
        << "; WARNING -- auto-generated file; see `man ipmgr` for details.\n"
        << "; NOTE: it is very unlikely that this will be useful since in most\n"
        << ";       most likelihood your IP provider is in control of the PTR.\n";

    // TTL (global time to live)
    zone_data << "$TTL " << f_ptr_ttl << "\n";

    // SOA
    zone_data
        << "@\tIN SOA "
        << f_nameservers.begin()->first
        << ". "
        << f_hostmaster
        << ". ("
        << f_serial
        << " "
        << f_refresh
        << " "
        << f_retry
        << " "
        << f_expire
        << " "
        << f_minimum_cache_failures
        << ")\n";

    // list of nameservers
    //
    for(auto const & ns : f_nameservers)
    {
        zone_data << "\tIN NS\t" << ns.first << ".\n";
    }

    // TODO: add support for IPv6
    //
    std::string::size_type const pos(f_ptr.rfind('.'));
    if(pos == std::string::npos)
    {
        SNAP_LOG_FATAL
            << "no period found in PTR \""
            << f_ptr
            << "\" (IPv6 is not yet supported)."
            << SNAP_LOG_SEND;
        return std::string();
    }

    zone_data << f_ptr.substr(pos + 1) << "\tIN PTR\t" << f_domain << ".\n";

    return zone_data.str();
}





/** \brief Initialize the ipmgr object.
 *
 * This function parses the command line and sets up the logger.
 *
 * \param[in] argc  The number of arguments in argv.
 * \param[in] argv  The argument strings.
 */
ipmgr::ipmgr(int argc, char * argv[])
    : f_opt(std::make_shared<advgetopt::getopt>(g_iplock_options_environment))
{
    snaplogger::add_logger_options(*f_opt);
    f_opt->finish_parsing(argc, argv);
    snaplogger::process_logger_options(
                      *f_opt
                    , "/etc/ipmgr/logger"
                    , std::cout
                    , false);

    f_dry_run = f_opt->is_defined("dry-run");
    f_verbose = f_dry_run || f_opt->is_defined("verbose");
    f_force = f_opt->is_defined("force");
    f_config_warnings = f_opt->is_defined("config-warnings");
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
        return 1;
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
        return 1;
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
    // get a list of all the files
    //
    std::size_t max(f_opt->size("zone-directories"));
    if(max == 0)
    {
        advgetopt::option_info::pointer_t opt(f_opt->get_option("zone-directories"));
        if(!opt->set_multiple_values(f_opt->get_default("zone-directories")))
        {
            // no zone directories specified?!
            //
            SNAP_LOG_FATAL
                << "the default --zone-directories ("
                << f_opt->get_default("zone-directories")
                << ") could not be parsed properly."
                << SNAP_LOG_SEND;
            return 1;
        }
        max = f_opt->size("zone-directories");
    }

    int exit_code(0);
    for(std::size_t i(0); i < max; ++i)
    {
        std::string dir(f_opt->get_string("zone-directories", i));
        if(f_verbose)
        {
            std::cout
                << "info: checking directory \""
                << dir
                << "\" for zone files."
                << std::endl;
        }
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
            zone_file->set_variables(f_opt->get_variables());
            std::string const domain(zone_file->get_parameter("domain"));
            if(domain.empty())
            {
                // force the re-definition of the domain name at all the
                // levels to confirm that everything is as it has to be
                //
                SNAP_LOG_ERROR
                    << "a domain name cannot be an empty string in \""
                    << g
                    << "\"."
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
            // this is an early validation; it is done again when we
            // retrieve that field in ipmgr::zone_files::retrieve_domain()
            //
            if(!validate_domain(domain))
            {
                exit_code = 1;
                continue;
            }

            // further validation of the file will happen later
            //
            if(f_zone_files[domain] == nullptr)
            {
                f_zone_files[domain] = std::make_shared<zone_files>(f_opt, f_verbose);
            }
            f_zone_files[domain]->add(zone_file);

            if(f_verbose)
            {
                std::cout
                    << "info: found configuration file \""
                    << g
                    << "\"."
                    << std::endl;
            }
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


int ipmgr::prepare_includes()
{
    std::string const includes_filename("/etc/bind/ipmgr-options.conf");

    f_includes.open(includes_filename);
    if(!f_includes.is_open())
    {
        SNAP_LOG_ERROR
            << "could not create file \""
            << includes_filename
            << "\"."
            << SNAP_LOG_SEND;
        return 1;
    }

    f_includes
        << "// AUTO-GENERATED, DO NOT EDIT\n"
        << "\n";

    return 0;
}


/** \brief Generate one zone.
 *
 * This function generates one zone from the specified configuration
 * info.
 *
 * The input is a zone as read by the read_zones() function.
 *
 * \return 0 on success, 1 on errors.
 */
int ipmgr::generate_zone(zone_files::pointer_t & zone)
{
    int r(0);

    if(!zone->retrieve_fields())
    {
        return 1;
    }

    if(f_verbose)
    {
        std::cout
            << "info: generating zone for \""
            << zone->domain()
            << "\"."
            << std::endl;
    }

    if(f_zone_conf[zone->group()].str().empty())
    {
        f_includes
            << "include \"/etc/bind/zones/"
            << zone->group()
            << ".conf\";\n";
    }

    std::string z(zone->generate_zone_file());
    if(z.empty())
    {
        // generation failed
        //
        return 1;
    }

// TODO: use a template to generate the zone_conf files (instead of the
//       dynamic parameter, use a template=... where you can define
//       the name of the template which gives us the parameters to use
//       all in one place and especially editable by users and yo'd be
//       able to create any number of templates)

    // we must insert all the zones in the configuration file, even if we
    // do not regenerate some of them because they are already up to date
    //
    // otherwise the .conf file would be missing those entries and that
    // would be really bad!
    //
    if(f_zone_conf[zone->group()].str().empty())
    {
        f_zone_conf[zone->group()]
            << "// AUTO-GENERATED FILE, DO NOT EDIT\n"
            << "// see ipmgr(1) instead\n"
            << "\n";
    }

    f_zone_conf[zone->group()]
        << "zone \""
        << zone->domain()
        << "\" {\n"
        << "  type master;\n"
        << "  file \""
            << (zone->dynamic() != zone_files::dynamic_t::DYNAMIC_STATIC
                    ? std::string("/var/lib/bind")
                    : "/etc/bind/zones/" + zone->group())
            << '/'
            << zone->domain()
            << ".zone"
            << "\";\n"
        << "  allow-transfer { trusted-servers; };\n"

        // at this time, I only handle our very specific update-policy needs...
        // this needs a lot of help
        //
        << (zone->dynamic() == zone_files::dynamic_t::DYNAMIC_LETSENCRYPT
                ? "  check-names warn;\n"
                  "  update-policy {\n"
                  "    grant letsencrypt_wildcard. name _acme-challenge." + zone->domain() + ". txt;\n"
                  "  };\n"
                : std::string())
        << (zone->dynamic() == zone_files::dynamic_t::DYNAMIC_LOCAL
                ? "  update-policy local;\n"
                : std::string())
        << (zone->dynamic() == zone_files::dynamic_t::DYNAMIC_BOTH
                ? "  check-names warn;\n"
                  "  update-policy {\n"
                  "    grant local-ddns zonesub any;\n"
                  "    grant letsencrypt_wildcard. name _acme-challenge." + zone->domain() + ". txt;\n"
                  "  };\n"
                : std::string())
        << (zone->dynamic() != zone_files::dynamic_t::DYNAMIC_STATIC
                ? "  max-journal-size 2M;\n"
                : "")
        << "};\n"
        << "\n";

    // compare with existing file, if it changed, then we raise a flag
    // about that
    //
    std::string const zone_filename("/var/lib/ipmgr/generated/" + zone->group() + "/" + zone->domain() + ".zone");

    snapdev::file_contents file(zone_filename, true);
    if(!f_force)
    {
        if(file.exists()
        && file.read_all())
        {
            // got existing file contents, did it change?
            //
            if(file.contents() == z)
            {
                // no changes, we're done here
                //
                return 0;
            }
        }
    }

    if(zone->dynamic() != zone_files::dynamic_t::DYNAMIC_STATIC)
    {
        stop_bind9();
    }

    // the zone changed or is forcibly refreshed so increment the serial number
    //
    if(zone->get_zone_serial(true) == 0)
    {
        return 1;
    }

    z = zone->generate_zone_file();
    if(z.empty())
    {
        // generation failed
        //
        return 1;
    }

    // raise flag that something changed and a restart will be required
    //
    // this file goes under /run so we don't take the risk of restarting
    // again after a reboot
    //
    f_bind_restart_required = true;
    snapdev::file_contents flag(g_bind9_need_restart, true);
    flag.contents("*** bind9 restart required ***\n");
    if(!flag.write_all())
    {
        SNAP_LOG_MINOR
            << "could not write to file \""
            << g_bind9_need_restart
            << "\": "
            << flag.last_error()
            << SNAP_LOG_SEND;
    }

    // save the new content
    //
    file.contents(z);
    if(!file.write_all())
    {
        SNAP_LOG_ERROR
            << "could not write to file \""
            << zone_filename
            << "\": "
            << file.last_error()
            << SNAP_LOG_SEND;
        return 1;
    }

    std::string const bind_filename("/etc/bind/zones/" + zone->group() + "/" + zone->domain() + ".zone");
    std::string const dynamic_filename("/var/lib/bind/" + zone->domain() + ".zone");

    if(zone->dynamic() == zone_files::dynamic_t::DYNAMIC_STATIC)
    {
        // if static, make sure to remove the dynamic zone file
        //
        r = unlink(dynamic_filename.c_str());
        if(r != 0
        && errno != ENOENT)
        {
            int const e(errno);
            SNAP_LOG_WARNING
                << "could not delete file \""
                << dynamic_filename
                << "\": "
                << e
                << ", "
                << strerror(e)
                << SNAP_LOG_SEND;
        }

        // static zones also get saved under /etc/bind/zones/<group>/...
        //

        snapdev::file_contents bind(bind_filename, true);
        bind.contents(z);
        if(!bind.write_all())
        {
            SNAP_LOG_ERROR
                << "could not write to static file \""
                << bind_filename
                << "\": "
                << bind.last_error()
                << SNAP_LOG_SEND;
            return 1;
        }

        return 0;
    }

    // if dynamic, make sure to remove the static zone file
    //
    r = unlink(bind_filename.c_str());
    if(r != 0
    && errno != ENOENT)
    {
        int const e(errno);
        SNAP_LOG_WARNING
            << "could not delete file \""
            << bind_filename
            << "\": "
            << e
            << ", "
            << strerror(e)
            << SNAP_LOG_SEND;
    }

    // this is a dynamic zone
    //
    // ideally, we would want to dynamically update this zone but...
    //
    // (1) zones made dynamic to allow letsencrypt can get a TXT setup
    //     but nothing else, so it's not useful
    //
    // (2) zones made dynamic to allow subdomain updates may have other
    //     changes such as their SOA and at this point I don't have the
    //     time to implement such here! (rndc can be used to update the
    //     SOA, but I'm not too sure how you'd setup the NS and MX
    //     fields, etc.)
    //
    // 1. new or letsencrypt dynamism only
    //
    // if it is the first time we set this one up, we need to create the
    // file under /var/lib/bind/<domain>.zone
    //
    // 2. existing
    //
    // it already exists, we want to just make the changes with nsupdate
    //

    // as mentioned above, always refresh the whole file...
    // and for that to work safely, we need to turn off the
    // server first, otherwise it could try to update the file
    // under our feet
    //
    stop_bind9();

    //if(access(dynamic_filename, F_OK) != 0
    //|| zone->dynamic() != dynamic_t::DYNAMIC_LOCAL)
    {
        // case 1. file is new or we're not in LOCAL dynamism
        //
        snapdev::file_contents dynamic_zone(dynamic_filename, true);
        dynamic_zone.contents(z);
        if(!dynamic_zone.write_all())
        {
            SNAP_LOG_ERROR
                << "could not write to dynamic file \""
                << dynamic_filename
                << "\": "
                << dynamic_zone.last_error()
                << SNAP_LOG_SEND;
            return 1;
        }
        if(snapdev::chownnm(dynamic_filename, "bind", "bind") != 0)
        {
            SNAP_LOG_ERROR
                << "could not set dynamic file \""
                << dynamic_filename
                << "\" owner and/or group to bind:bind."
                << SNAP_LOG_SEND;
            return 1;
        }

        return 0;
    }

// at the moment this case is not handled (see comments above)

    return 0;
}


int ipmgr::generate_ptr_zone(zone_files::pointer_t & zone)
{
    if(f_verbose)
    {
        std::cout
            << "info: generating PTR zone for \""
            << zone->domain()
            << "\"."
            << std::endl;
    }

    f_includes
        << "include \"/etc/bind/zones/"
        << zone->get_ptr()
        << ".ptr\";\n";

    std::string z(zone->generate_ptr_file());
    if(z.empty())
    {
        // generation failed
        //
        return 1;
    }

// TODO: use a template to generate the zone_conf files (instead of the
//       dynamic parameter, use a template=... where you can define
//       the name of the template which gives us the parameters to use
//       all in one place and especially editable by users and yo'd be
//       able to create any number of templates)

    // we must insert all the zones in the configuration file, even if we
    // do not regenerate some of them because they are already up to date
    //
    // otherwise the .conf file would be missing those entries and that
    // would be really bad!
    //
    if(f_zone_conf[zone->get_ptr()].str().empty())
    {
        // this should not happen here
        //
        f_zone_conf[zone->get_ptr()]
            << "// AUTO-GENERATED FILE, DO NOT EDIT\n"
            << "// see ipmgr(1) instead\n"
            << "\n";
    }

    f_zone_conf[zone->get_ptr()]
        << "zone \""
        << zone->get_ptr_arpa()
        << "\" {\n"
        << "  type master;\n"
        << "  file \"/etc/bind/zones/"
        << zone->get_ptr()
        << ".ptr\";\n"
        << "};\n";

    // compare with existing file, if it changed, then we raise a flag
    // about that
    //
    std::string const zone_filename("/var/lib/ipmgr/generated/" + zone->get_ptr() + ".ptr");

    snapdev::file_contents file(zone_filename, true);
    if(!f_force)
    {
        if(file.exists()
        && file.read_all())
        {
            // got existing file contents, did it change?
            //
            if(file.contents() == z)
            {
                // no changes, we're done here
                //
                return 0;
            }
        }
    }

    // the zone changed or is forcibly refreshed so increment the serial number
    //
    if(zone->get_zone_serial(true) == 0)
    {
        return 1;
    }

    z = zone->generate_ptr_file();
    if(z.empty())
    {
        // generation failed
        //
        return 1;
    }

    // raise flag that something changed and a restart will be required
    //
    // this file goes under /run so we don't take the risk of restarting
    // again after a reboot
    //
    f_bind_restart_required = true;
    snapdev::file_contents flag(g_bind9_need_restart, true);
    flag.contents("*** bind9 restart required ***\n");
    if(!flag.write_all())
    {
        SNAP_LOG_MINOR
            << "could not write to file \""
            << g_bind9_need_restart
            << "\": "
            << flag.last_error()
            << SNAP_LOG_SEND;
    }

    // save the new content
    //
    file.contents(z);
    if(!file.write_all())
    {
        SNAP_LOG_ERROR
            << "could not write to file \""
            << zone_filename
            << "\": "
            << file.last_error()
            << SNAP_LOG_SEND;
        return 1;
    }

    std::string const bind_filename("/etc/bind/zones/" + zone->get_ptr() + ".ptr");

    snapdev::file_contents bind(bind_filename, true);
    bind.contents(z);
    if(!bind.write_all())
    {
        SNAP_LOG_ERROR
            << "could not write to static file \""
            << bind_filename
            << "\": "
            << bind.last_error()
            << SNAP_LOG_SEND;
        return 1;
    }

    return 0;
}


/** \brief Save the configuration files.
 *
 * Each group of zones is given a configuration file with the bind syntax
 * referencing the zone files included in that group.
 *
 * This function saves the resulting configuration files to disk under
 * the /etc/bind/zones/... directory.
 *
 * The files were generated in the generate_zone() function.
 */
int ipmgr::save_conf_files()
{
    for(auto & ss : f_zone_conf)
    {
        std::string conf_filename("/etc/bind/zones/");
        conf_filename += ss.first;
        conf_filename += ".conf";

        snapdev::file_contents conf(conf_filename, true);
        conf.contents(ss.second.str());
        if(!conf.write_all())
        {
            SNAP_LOG_ERROR
                << "could not write to file \""
                << conf_filename
                << "\": "
                << conf.last_error()
                << SNAP_LOG_SEND;
            return 1;
        }
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

    r = prepare_includes();
    if(r != 0)
    {
        return r;
    }

    for(auto & z : f_zone_files)
    {
        r = generate_zone(z.second);
        if(r != 0)
        {
            return r;
        }

        if(!z.second->get_ptr().empty())
        {
            generate_ptr_zone(z.second);
        }
    }

    r = save_conf_files();
    if(r != 0)
    {
        return r;
    }

    return 0;
}


int ipmgr::process_opendmarc()
{
    bool changed(false);

    std::string cmd("edit-config --no-warning --space /etc/opendmarc.conf TrustedAuthservIDs ");
    bool has_trusted_mail(false);
    std::string auth_server_id;
    for(auto & z : f_zone_files)
    {
        std::string const trusted(z.second->get_zone_mail_subdomain());
        if(!trusted.empty())
        {
            if(z.second->is_auth_server())
            {
                if(auth_server_id.empty())
                {
                    auth_server_id = trusted;
                }
                else
                {
                    SNAP_LOG_ERROR
                        << "found two authoritative mail servers: \""
                        << trusted
                        << "\" and \""
                        << auth_server_id
                        << "\" when you can only have one."
                        << SNAP_LOG_SEND;
                    return 1;
                }
            }
            if(has_trusted_mail)
            {
                cmd += ',';
            }
            else
            {
                has_trusted_mail = true;
            }
            cmd += trusted;
        }
    }
    if(has_trusted_mail)
    {
        if(f_verbose)
        {
            std::cout
                << "info: "
                << cmd
                << std::endl;
        }
        if(!f_dry_run)
        {
            int const r(system(cmd.c_str()));
            if(r != 0)
            {
                SNAP_LOG_ERROR
                    << "updating the opendmarc configuration file with the list of trusted mail servers failed \""
                    << cmd
                    << "\"."
                    << SNAP_LOG_SEND;
                return r;
            }
            changed = true;
        }
    }

    if(auth_server_id.empty())
    {
        SNAP_LOG_WARNING
            << "no authoritative mail server found; opendmarc may not work as expected."
            << SNAP_LOG_SEND;
    }
    else
    {
        // this should be the MTA name (i.e. we shouldn't have to have
        // the user define which entry is the authoritative one)
        //
        cmd = "edit-config --space /etc/opendmarc.conf AuthservID ";
        cmd += auth_server_id;
        if(f_verbose)
        {
            std::cout
                << "info: "
                << cmd
                << std::endl;
        }
        if(!f_dry_run)
        {
            int const r(system(cmd.c_str()));
            if(r != 0)
            {
                SNAP_LOG_ERROR
                    << "updating the opendmarc configuration file with the list of trusted mail servers failed: \""
                    << cmd
                    << "\"."
                    << SNAP_LOG_SEND;
                return r;
            }
            changed = true;
        }
    }

    if(changed)
    {
        snapdev::file_contents flag(g_opendmarc_need_restart, true);
        flag.contents("*** opendmarc restart required ***\n");
        if(!flag.write_all())
        {
            SNAP_LOG_MINOR
                << "could not write to file \""
                << g_opendmarc_need_restart
                << "\": "
                << flag.last_error()
                << SNAP_LOG_SEND;
        }
    }

    return 0;
}


int ipmgr::bind9_is_active()
{
    // we must check only once because we may get called more than once
    // and the stop_bind9() may get called in between...
    //
    if(f_bind9_is_active != active_t::ACTIVE_NOT_TESTED)
    {
        return 0;
    }

    // we do not want to force a stop & start if the process is not currently
    // active (i.e. it may have been stopped by the user for a while)
    //
    cppprocess::process is_active_process("bind9-is-active?");
    is_active_process.set_command("systemctl");
    is_active_process.add_argument("is-active");
    is_active_process.add_argument("bind9");

    cppprocess::io_capture_pipe::pointer_t output(std::make_shared<cppprocess::io_capture_pipe>());
    is_active_process.set_output_io(output);

    if(f_verbose)
    {
        std::cout
            << "info: "
            << is_active_process.get_command_line()
            << std::endl;
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
        if(r != 0
        && r != 3)  // 3 is returned if the unit is not active
        {
            SNAP_LOG_FATAL
                << "command \""
                << is_active_process.get_command_line()
                << "\" returned an error (exit code "
                << r
                << ")."
                << SNAP_LOG_SEND;
            return 1;
        }
    }

    std::string const active(snapdev::trim_string(output->get_output(true)));
    f_bind9_is_active = active == "active"
                            ? active_t::ACTIVE_YES
                            : active_t::ACTIVE_NO;

    // TODO: make this flag a file so we know whether we need to do a restart
    //       even if ipmgr fails

    return 0;
}


int ipmgr::stop_bind9()
{
    int r(0);

    // make sure we try to stop only once (it's rather slow to repeat this
    // call otherwise even if it's safe)
    //
    if(f_stopped_bind9)
    {
        return 0;
    }
    f_stopped_bind9 = true;

    r = bind9_is_active();
    if(r != 0)
    {
        return r;
    }
    if(f_bind9_is_active != active_t::ACTIVE_YES)
    {
        return 0;
    }

    // stop the DNS server
    //
    char const * cmd("systemctl stop bind9");
    if(f_verbose)
    {
        std::cout
            << "info: "
            << cmd
            << std::endl;
    }
    if(!f_dry_run)
    {
        r = system(cmd);
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

    return 0;
}


int ipmgr::start_bind9()
{
    // start the DNS server
    //
    char const * cmd("systemctl start bind9");
    if(f_verbose)
    {
        std::cout
            << "info: "
            << cmd
            << std::endl;
    }
    if(!f_dry_run)
    {
        int const r(system(cmd));
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
    int r(0);

    // restart necessary?
    //
    if(!f_bind_restart_required)
    {
        if(access(g_bind9_need_restart, F_OK) != 0)
        {
            return 0;
        }
    }

    r = stop_bind9();
    if(r != 0)
    {
        return r;
    }

    if(f_bind9_is_active == active_t::ACTIVE_NO)
    {
        // it was not active when we started ipmgr
        //
        if(!f_dry_run)
        {
            snapdev::NOT_USED(unlink(g_bind9_need_restart));
        }
        return 0;
    }

    // clear the journals
    //
    char const * clear_journals("rm -f /var/lib/bind/*.jnl");
    if(f_verbose)
    {
        std::cout
            << "info: "
            << clear_journals
            << std::endl;
    }
    if(!f_dry_run)
    {
        r = system(clear_journals);
        if(r != 0)
        {
            SNAP_LOG_WARNING
                << "could not delete the journal (.jnl) files."
                << SNAP_LOG_SEND;
        }
    }

    r = start_bind9();
    if(r != 0)
    {
        return r;
    }

    // remove the flag telling us that the restart we requested
    //
    if(f_verbose)
    {
        std::cout
            << "info: rm -f "
            << g_bind9_need_restart
            << std::endl;
    }
    if(!f_dry_run)
    {
        // ignore errors on this one
        //
        snapdev::NOT_USED(unlink(g_bind9_need_restart));
    }

    return 0;
}


int ipmgr::restart_opendkim()
{
    int r(0);

    // restart necessary?
    //
    if(access(g_opendkim_need_restart, F_OK) != 0)
    {
        return 0;
    }

    // we do not want to force a restart if opendkim is not currently
    // active (i.e. it may have been stopped by the user for a while)
    //
    cppprocess::process is_active_process("opendkim-is-active?");
    is_active_process.set_command("systemctl");
    is_active_process.add_argument("is-active");
    is_active_process.add_argument("opendkim");

    cppprocess::io_capture_pipe::pointer_t output(std::make_shared<cppprocess::io_capture_pipe>());
    is_active_process.set_output_io(output);

    if(f_verbose)
    {
        std::cout
            << "info: "
            << is_active_process.get_command_line()
            << std::endl;
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
        r = is_active_process.wait();
        if(r != 0
        && r != 3)  // 3 is returned if the unit is not active
        {
            SNAP_LOG_FATAL
                << "command \""
                << is_active_process.get_command_line()
                << "\" returned an error (exit code "
                << r
                << ")."
                << SNAP_LOG_SEND;
            return 1;
        }
    }

    std::string const active(snapdev::trim_string(output->get_output(true)));
    if(active != "active")
    {
        return 0;
    }

    // stop the DNS server
    //
    char const * cmd("systemctl restart opendkim");
    if(f_verbose)
    {
        std::cout
            << "info: "
            << cmd
            << std::endl;
    }
    if(!f_dry_run)
    {
        r = system(cmd);
        if(r != 0)
        {
            SNAP_LOG_FATAL
                << "could not restart the opendkim service (systemctl exit value: "
                << r
                << ")"
                << SNAP_LOG_SEND;
            return r;
        }
    }

    // remove the flag telling us that the restart we requested
    //
    if(f_verbose)
    {
        std::cout
            << "info: rm -f "
            << g_opendkim_need_restart
            << std::endl;
    }
    if(!f_dry_run)
    {
        // ignore errors on this one
        //
        snapdev::NOT_USED(unlink(g_opendkim_need_restart));
    }

    return 0;
}


int ipmgr::restart_opendmarc()
{
    int r(0);

    // restart necessary?
    //
    if(access(g_opendmarc_need_restart, F_OK) != 0)
    {
        return 0;
    }

    // we do not want to force a restart if opendmarc is not currently
    // active (i.e. it may have been stopped by the user for a while)
    //
    cppprocess::process is_active_process("opendmarc-is-active?");
    is_active_process.set_command("systemctl");
    is_active_process.add_argument("is-active");
    is_active_process.add_argument("opendmarc");

    cppprocess::io_capture_pipe::pointer_t output(std::make_shared<cppprocess::io_capture_pipe>());
    is_active_process.set_output_io(output);

    if(f_verbose)
    {
        std::cout
            << "info: "
            << is_active_process.get_command_line()
            << std::endl;
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
        r = is_active_process.wait();
        if(r != 0
        && r != 3)  // 3 is returned if the unit is not active
        {
            SNAP_LOG_FATAL
                << "command \""
                << is_active_process.get_command_line()
                << "\" returned an error (exit code "
                << r
                << ")."
                << SNAP_LOG_SEND;
            return 1;
        }
    }

    std::string const active(snapdev::trim_string(output->get_output(true)));
    if(active != "active")
    {
        return 0;
    }

    // stop the DNS server
    //
    char const * cmd("systemctl restart opendmarc");
    if(f_verbose)
    {
        std::cout
            << "info: "
            << cmd
            << std::endl;
    }
    if(!f_dry_run)
    {
        r = system(cmd);
        if(r != 0)
        {
            SNAP_LOG_FATAL
                << "could not restart the opendmarc service (systemctl exit value: "
                << r
                << ")"
                << SNAP_LOG_SEND;
            return r;
        }
    }

    // remove the flag telling us that the restart we requested
    //
    if(f_verbose)
    {
        std::cout
            << "info: rm -f "
            << g_opendmarc_need_restart
            << std::endl;
    }
    if(!f_dry_run)
    {
        // ignore errors on this one
        //
        snapdev::NOT_USED(unlink(g_opendmarc_need_restart));
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
        return r;
    }

    r = process_zones();
    if(r != 0)
    {
        return r;
    }

    r = process_opendmarc();
    if(r != 0)
    {
        return r;
    }

    r = restart_bind9();
    if(r != 0)
    {
        return r;
    }

    r = restart_opendkim();
    if(r != 0)
    {
        return r;
    }

    r = restart_opendmarc();
    if(r != 0)
    {
        return r;
    }

    return 0;
}



// vim: ts=4 sw=4 et
