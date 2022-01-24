# IP Manager -- the easy way to generate zone files for BIND9
# Copyright (c) 2022  Made to Order Software Corp.  All Rights Reserved
#
# https://snapwebsites.org/project/ipmgr
# contact@m2osw.com
# 
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.
#
cmake_minimum_required(VERSION 3.10.2)

project(ipmrg_project)

find_package(AdvGetOpt        REQUIRED)
find_package(Boost            REQUIRED)
find_package(EventDispatcher  REQUIRED)
find_package(LibAddr          REQUIRED)
find_package(LibExcept        REQUIRED)
find_package(SnapCMakeModules REQUIRED)
find_package(SnapDev          REQUIRED)
find_package(SnapLogger       REQUIRED)

SnapGetVersion(IPMGR ${CMAKE_CURRENT_SOURCE_DIR})

enable_language(CXX)

# Sub-directories
add_subdirectory(ipmgr)
add_subdirectory(tools)
add_subdirectory(doc)


# vim: ts=4 sw=4 et
