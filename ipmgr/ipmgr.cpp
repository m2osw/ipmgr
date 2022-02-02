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
#include    "exception.h"
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
#include    <snapdev/file_contents.h>
#include    <snapdev/trim_string.h>


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
        parser.set_allow(addr::addr_parser::flag_t::ADDRESS, true);
        parser.set_allow(addr::addr_parser::flag_t::REQUIRED_ADDRESS, true);
        parser.set_allow(addr::addr_parser::flag_t::ADDRESS_LOOKUP, false);
        parser.set_allow(addr::addr_parser::flag_t::PORT, false);
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
        , std::string const & default_value)
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
        , std::string const & default_value)
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
        , std::int32_t default_value)
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
        , std::string const & default_value)
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
        , std::string const & default_value)
{
    std::string const value(get_zone_param(
                                      name
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


std::uint32_t ipmgr::zone_files::get_zone_serial(bool next)
{
#ifdef _DEBUG
    // the domain must be retrieved before the nameservers
    //
    if(f_domain.empty())
    {
        throw ipmgr_logic_error("get_zone_serial() called without a domain name defined");
    }
#endif

    std::string path("/var/lib/ipmgr/serial/" + f_domain + ".counter");

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
                << f_domain
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
            << f_domain
            << "\" domain."
            << SNAP_LOG_SEND;
        return 0;
    }

    return serial;
}


bool ipmgr::zone_files::retrieve_fields()
{
    typedef bool (ipmgr::zone_files::*retrieve_func_t)();

    retrieve_func_t func_list[] =
    {
        // WARNING: for some fields, the order matters
        //
        &ipmgr::zone_files::retrieve_domain,
        &ipmgr::zone_files::retrieve_ttl,
        &ipmgr::zone_files::retrieve_ips,
        &ipmgr::zone_files::retrieve_nameservers,
        &ipmgr::zone_files::retrieve_hostmaster,
        &ipmgr::zone_files::retrieve_serial,
        &ipmgr::zone_files::retrieve_refresh,
        &ipmgr::zone_files::retrieve_retry,
        &ipmgr::zone_files::retrieve_expire,
        &ipmgr::zone_files::retrieve_minimum_cache_failures,
        &ipmgr::zone_files::retrieve_mail_fields,
        &ipmgr::zone_files::retrieve_dynamic,
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
    return f_ttl >= 0;
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
    advgetopt::split_string(
          nameserver_list
        , f_nameservers
        , {" ", ",", ":", ";"});
    if(f_nameservers.size() < 2)
    {
        SNAP_LOG_ERROR
            << "You must defined at least two nameservers (found "
            << f_nameservers.size()
            << " instead)."
            << SNAP_LOG_SEND;
        return false;
    }

    for(auto const & ns : f_nameservers)
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
    return !f_hostmaster.empty();
}


bool ipmgr::zone_files::retrieve_serial()
{
    f_serial = get_zone_serial();
    return f_serial != 0;
}


bool ipmgr::zone_files::retrieve_refresh()
{
    f_refresh = get_zone_duration("refresh", "default_refresh", "3h");
    return f_refresh >= 0;
}


bool ipmgr::zone_files::retrieve_retry()
{
    f_retry = get_zone_duration("retry", "default_retry", "3m");
    return f_retry >= 0;
}


bool ipmgr::zone_files::retrieve_expire()
{
    f_expire = get_zone_duration("expire", "default_expire", "2w");
    return f_expire >= 0;
}


bool ipmgr::zone_files::retrieve_minimum_cache_failures()
{
    f_minimum_cache_failures = get_zone_duration(
                                  "minimum_cache_failures"
                                , "default_minimum_cache_failures"
                                , "5m");
    return f_minimum_cache_failures >= 0;
}


// TODO: each sub-domain needs its own priority, TTL, etc.
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

    // list of sub-domains (default to "mail")
    //
    advgetopt::split_string(
          get_zone_param(mail_section + "::sub_domains", std::string(), "mail")
        , f_mail_sub_domains
        , {" ", ",", ";"});

    for(auto const & sub_domain : f_mail_sub_domains)
    {
        if(!validate_domain(sub_domain + '.' + f_domain))
        {
            SNAP_LOG_ERROR
                << "Validation of mail sub-domain name \""
                << sub_domain
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

    return true;
}


bool ipmgr::zone_files::retrieve_dynamic()
{
    f_dynamic = get_zone_bool("dynamic", std::string(), "false");
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


bool ipmgr::zone_files::is_dynamic() const
{
    return f_dynamic;
}


std::string ipmgr::zone_files::generate_zone_file()
{
    std::stringstream zone_data;

    // warning
    zone_data << "; WARNING -- auto-generated file; see `man ipmgr` for details.\n";
    if(f_dynamic)
    {
        zone_data << ";\n";
        zone_data << ";            this file represents our original but it is not used\n";
        zone_data << ";            by bind9 since we instead dynamically update the zone\n";
    }

    // ORIGIN
    zone_data << "$ORIGIN .\n";

    // TTL (global time to live)
    zone_data << "$TTL " << f_ttl << "\n";

    // SOA
    zone_data
        << f_domain
        << " IN SOA "
        << f_nameservers[0]
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
        zone_data << "\tNS " << ns << ".\n";
    }

    // MX entries if this domain supports mail
    //
    if(!f_mail_sub_domains.empty())
    {
        for(auto const & sub_domain : f_mail_sub_domains)
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
            zone_data << '\t' << sub_domain << '.' << f_domain << ".\n";

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
        parser.set_allow(addr::addr_parser::flag_t::ADDRESS, true);
        parser.set_allow(addr::addr_parser::flag_t::REQUIRED_ADDRESS, true);
        parser.set_allow(addr::addr_parser::flag_t::ADDRESS_LOOKUP, false);
        parser.set_allow(addr::addr_parser::flag_t::PORT, false);
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
        zone_data << '\t' << a.to_ipv4or6_string(addr::addr::string_ip_t::STRING_IP_ONLY) << '\n';
    }

    // we want all the sub-domains sorted so we use this intermediate
    // std::stringstream to generate a string that we save in a set and
    // afterward we write the strings in the set to the zone_data
    //
    std::set<std::string> sorted_domains;
    std::set<std::string> sorted_sub_domains;
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
        // describes one or more sub-domains
        //
        std::int32_t const sub_domain_ttl(get_zone_duration(s + "::ttl", std::string(), "0"));
        if(sub_domain_ttl < 0)
        {
            return std::string();
        }

        advgetopt::string_list_t sub_domain_txt;
        advgetopt::split_string(
              get_zone_param(s + "::txt")
            , sub_domain_txt
            , {" +++ "});

        std::string const sub_domains(get_zone_param(s + "::sub_domains"));

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
            if(sub_domain_txt.empty())
            {
                SNAP_LOG_ERROR
                    << "a sub-domain global section must have one  txt=... entry."
                    << SNAP_LOG_SEND;
                return std::string();
            }

            for(auto const & txt : sub_domain_txt)
            {
                std::stringstream ss;

                ss << '\t';

                if(sub_domain_ttl != 0
                && sub_domain_ttl != f_ttl)
                {
                    ss << sub_domain_ttl << ' ';
                }

                ss << "TXT\t\"" << txt << '"';

                sorted_domains.insert(ss.str());
            }
        }
        else
        {
            if(sub_domains.empty())
            {
                SNAP_LOG_ERROR
                    << "non-global sections of a zone file definition must include a list of one or more sub-domains."
                    << SNAP_LOG_SEND;
                return std::string();
            }

            std::string const cname(get_zone_param(s + "::cname"));

            advgetopt::string_list_t sub_domain_names;
            advgetopt::split_string(
                  sub_domains
                , sub_domain_names
                , {" ", ",", ";"});

            advgetopt::string_list_t sub_domain_ips;
            advgetopt::split_string(
                  get_zone_param(s + "::ips")
                , sub_domain_ips
                , {" ", ",", ";"});
            if(sub_domain_ips.empty()
            && sub_domain_txt.empty()
            && cname.empty())
            {
                sub_domain_ips = f_ips;
            }

            if(((sub_domain_ips.empty() ? 0 : 1)
                 + (sub_domain_txt.empty() ? 0 : 1)
                 + (cname.empty() ? 0 : 1)) > 1)
            {
                SNAP_LOG_ERROR
                    << "a sub-domain must have only one of IP addresses, a cname=..., or a txt=... field defined simultaneously."
                    << SNAP_LOG_SEND;
                return std::string();
            }

            if(sub_domain_ips.empty()
            && sub_domain_txt.empty()
            && cname.empty())
            {
                SNAP_LOG_ERROR
                    << "a sub-domain must have at least one IP address, a cname=..., or a txt=... field defined."
                    << SNAP_LOG_SEND;
                return std::string();
            }

            if(!sub_domain_ips.empty())
            {
                if(!validate_ips(sub_domain_ips))
                {
                    return std::string();
                }
            }

            for(auto const & d : sub_domain_names)
            {
                if(!sub_domain_txt.empty())
                {
                    for(auto const & txt : sub_domain_txt)
                    {
                        std::stringstream ss;

                        ss << d << '\t';

                        if(sub_domain_ttl != 0
                        && sub_domain_ttl != f_ttl)
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
                        && sub_domain_ttl != f_ttl)
                        {
                            ss << sub_domain_ttl << ' ';
                        }

                        addr::addr_parser parser;
                        parser.set_allow(addr::addr_parser::flag_t::ADDRESS, true);
                        parser.set_allow(addr::addr_parser::flag_t::REQUIRED_ADDRESS, true);
                        parser.set_allow(addr::addr_parser::flag_t::ADDRESS_LOOKUP, false);
                        parser.set_allow(addr::addr_parser::flag_t::PORT, false);
                        addr::addr_range::vector_t r(parser.parse(ip));
                        addr::addr a(r[0].get_from());

                        if(a.is_ipv4())
                        {
                            ss << 'A';
                        }
                        else
                        {
                            ss << "AAAA";
                        }
                        ss << "\t" << a.to_ipv4or6_string(addr::addr::string_ip_t::STRING_IP_ONLY);

                        sorted_sub_domains.insert(ss.str());
                    }
                }

                if(!cname.empty())
                {
                    std::stringstream ss;

                    ss << d << '\t';

                    if(sub_domain_ttl != 0
                    && sub_domain_ttl != f_ttl)
                    {
                        ss << sub_domain_ttl << ' ';
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

                        // assume cname is a sub-domain of this domain
                        //
                        ss << link << '.';
                    }

                    sorted_sub_domains.insert(ss.str());
                }
            }
        }
    }

    for(auto const & d : sorted_domains)
    {
        zone_data << d << '\n';
    }

    // switch to the sub-domains now
    //
    zone_data << "$ORIGIN " << f_domain << ".\n";

    for(auto const & d : sorted_sub_domains)
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
        temp.write_all();

        std::string verify_zone("named-checkzone ");
        verify_zone += f_domain;
        verify_zone += ' ';
        verify_zone += zone_to_verify;
        if(f_verbose)
        {
            std::cout << verify_zone << std::endl;
        }
        int const r(system(verify_zone.c_str()));
        if(r != 0)
        {
            SNAP_LOG_FATAL
                << "the generated zone did not validate with named-checkzone; exit code: "
                << r
                << " (command: \""
                << verify_zone
                << "\")."
                << SNAP_LOG_SEND;
            return std::string();
        }
    }

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
    int exit_code(0);

    // get a list of all the files
    //
    std::size_t const max(f_opt->size("zone-directories"));
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
        std::string dir(f_opt->get_string("zone-directories", i));
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
    if(!zone->retrieve_fields())
    {
        return 1;
    }

    std::string z(zone->generate_zone_file());
    if(z.empty())
    {
        // generation failed
        //
        return 1;
    }

    // compare with existing file, if it changed, then we raise a flag
    // about that
    //
    std::string const zone_filename("/var/lib/ipmgr/generated/" + zone->domain() + "/" + zone->domain() + ".zone");

    snapdev::file_contents file(zone_filename, true);
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

    if(!zone->is_dynamic())
    {
        // static file, we're done
        //
        return 0;
    }

    // this is a dynamic zone, so we have to actually manually update
    // it instead of just saving a static file and restarting bind9
    //

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

    for(auto & z : f_zone_files)
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
    if(!f_bind_restart_required)
    {
        if(access(g_bind9_need_restart, F_OK) != 0)
        {
            return 0;
        }
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

    std::string const active(snapdev::trim_string(output->get_output(true)));
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