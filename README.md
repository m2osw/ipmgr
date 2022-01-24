
<p align="center">
<img alt="ipmgr" title="IP Manager -- a command line to easily manage your BIND9 zone files."
src="https://snapwebsites.org/sites/snapwebsites.org/files/images/ipmgr-logo.png" width="200" height="200"/>
</p>

# IP Manager

The IP Manager (`ipmgr`) is a tool used to manage domain name definitions
in the BIND9 DNS system.


# Dynamic IPs with BIND9 Breaks

I've experience breakage with BIND9 when sending it too many updates in a row.
It looks like it will never delete (or at least recycle) the journal and as
a result the files grows until the whole thing crashes. After that, the
corresponding zone fails to load even on a full restart cycle.

When the IP Manager is asked to restart the BIND9 service, it will also make
sure to delete the `.jnl` files. This way, we start fresh and it will work as
expected.


# Changing BIND9 Settings

I created a tool a while back (started in 2018 and it was in snapmanager/dns)
in order to make edits to the BIND9 settings. Unfortunately, the BIND9
settings can't be overridden. Instead you have to fix whatever you need
to fix "by hand" or using our `dns-options` tool.


# Documentation

See the conf/README.md for details about the configuration of `ipmgr`

The IP Manager uses BIND9 to do the actual DNS serving part. You can find
the [BIND9 document](https://bind9.readthedocs.io/en/latest/index.html)
online. The IP Manager installs a few files and generates the necessary
commands to dynamically add the domain names to the server.


# License

The ipmgr project is covered by the GNU GENERAL PUBLIC LICENSE.

See the LICENSE.txt file for details. If you can't find the LICENCE.txt file,
check out the https://www.gnu.org/licenses/ web page.


# Logo

I created the Logo using a
[font created by Film Himmel - Jens R. Ziehn](https://www.1001freefonts.com/28-days-later.font).

It is limited to uppercase A to Z and roman digits 0 to 9.


# Bugs

Submit bug reports and patches on
[github](https://github.com/m2osw/snapwebsites/issues).


_This file is part of the [snapcpp project](https://snapwebsites.org/)._
