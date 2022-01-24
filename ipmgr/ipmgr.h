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
#include <advgetopt/advgetopt.h>



class ipmgr
{
public:
                            ipmgr(int argc, char * argv[]);

    int                     run();

private:
    int                     make_root();

    advgetopt::getopt       f_opt;
    bool                    f_bind_restart_required = false;
};



// vim: ts=4 sw=4 et
