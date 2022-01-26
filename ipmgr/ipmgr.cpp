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


// libaddr lib
//
#include    <libaddr/addr_parser.h>


// cppprocess lib
//
#include    <cppprocess/io_capture_pipe.h>
#include    <cppprocess/process.h>


// snaplogger lib
//
#include    <snaplogger/message.h>
#include    <snaplogger/options.h>


// boost lib
//
#include    <boost/preprocessor/stringize.hpp>


// C++ lib
//
#include    <iostream>
#include    <fstream>
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
          advgetopt::Name("verbose")
        , advgetopt::ShortName('v')
        , advgetopt::Flags(advgetopt::standalone_all_flags<
                      advgetopt::GETOPT_FLAG_GROUP_OPTIONS>())
        , advgetopt::Help("Show the various steps the ipmgr takes to generate the zones.")
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
          advgetopt::Name("zone-directories")
        , advgetopt::Flags(advgetopt::all_flags<
                      advgetopt::GETOPT_FLAG_GROUP_OPTIONS
                    , advgetopt::GETOPT_FLAG_REQUIRED
                    , advgetopt::GETOPT_FLAG_MULTIPLE>())
        , advgetopt::Separators(g_zone_directories_separator)
        , advgetopt::DefaultValue("/usr/share/ipmgr/zones /etc/ipmgr/zones /var/lib/ipmgr/zones")
        , advgetopt::Help("List of directories to scan for zone definitions.")
    ),
    advgetopt::define_option(
          advgetopt::Name("hostmaster")
        , advgetopt::Flags(advgetopt::all_flags<
                      advgetopt::GETOPT_FLAG_GROUP_OPTIONS
                    , advgetopt::GETOPT_FLAG_REQUIRED
                    , advgetopt::GETOPT_FLAG_PROCESS_VARIABLES>())
        , advgetopt::Help("Email address of the host master.")
    ),
    advgetopt::define_option(
          advgetopt::Name("slave")
        , advgetopt::Flags(advgetopt::all_flags<
                      advgetopt::GETOPT_FLAG_GROUP_OPTIONS>())
        , advgetopt::DefaultValue("true")
        , advgetopt::Help("Mark this server as a slave DNS.")
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
          advgetopt::Name("default-nameservers")
        , advgetopt::Flags(advgetopt::all_flags<
                      advgetopt::GETOPT_FLAG_GROUP_OPTIONS
                    , advgetopt::GETOPT_FLAG_REQUIRED
                    , advgetopt::GETOPT_FLAG_MULTIPLE
                    , advgetopt::GETOPT_FLAG_PROCESS_VARIABLES>())
        , advgetopt::Help("Default domain names for all your nameservers. You must define at least two.")
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
    .f_section_variables_name = nullptr,
    .f_configuration_files = nullptr,
    .f_configuration_filename = nullptr,
    .f_configuration_directories = nullptr,
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
            << "could not become root (setuid(0)) to execute privileged commands (errno: "
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
            << "could not change group to root (setgid(0)) to execute privileged commands (errno: "
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
int ipmgr::list_zones()
{
    snap::glob_to_list<std::vector<std::string>> glob;

    std::size_t const max(f_opt.size("zone-directories"));
    for(std::size_t i(0); i < max; ++i)
    {
        std::string dir(f_opt.get_string("zone-directories", i));
        std::string pattern(dir + "/*.conf");
        if(glob.read_path<
                snap::glob_to_list_flag_t::GLOB_FLAG_IGNORE_ERRORS>(pattern))
        {
        }
    }

    return 0;
}


/** \brief Process the input files one at a time.
 *
 * This function reads the list of zone files to be processed using
 * a glob and then it processes them one by one.
 *
 * \return 0 if no error occurred.
 */
int ipmgr::process_zones()
{

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
