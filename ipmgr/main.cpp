// Copyright (c) 2007-2025  Made to Order Software Corp.  All Rights Reserved.
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


// self
//
#include    "ipmgr.h"


// advgetopt
//
#include    <advgetopt/exception.h>


// eventdispatcher
//
#include    <eventdispatcher/signal_handler.h>


// C++
//
#include    <iostream>


// snapdev
//
#include    <snapdev/poison.h>


/** \mainpage
 *
 * \image html ipmgr-logo.png "IP Manager" width=250
 *
 * The ipmgr tool is used to generate DNS zone files dynamically and with
 * simple definitions (compared to a zone file, at least).
 */



int main(int argc, char * argv[])
{
    ed::signal_handler::create_instance();

    try
    {
        ipmgr l(argc, argv);
        return l.run();
    }
    catch(advgetopt::getopt_exit const & e)
    {
        return e.code();
    }
    catch(std::exception const & e)
    {
        std::cerr << "error:ipmgr: an exception occurred: " << e.what() << std::endl;
    }
    catch(...)
    {
        std::cerr << "error:ipmgr: an unknown exception occurred." << std::endl;
    }

    return 1;
}


// vim: ts=4 sw=4 et
