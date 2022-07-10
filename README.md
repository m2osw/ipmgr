
<p align="center">
<img alt="ipmgr" title="IP Manager -- a command line to easily manage your BIND9 zone files."
src="https://snapwebsites.org/sites/snapwebsites.org/files/images/ipmgr-logo.png" width="200" height="200"/>
</p>

# IP Manager

The IP Manager (`ipmgr`) is a tool used to manage domain name definitions
in the BIND9 DNS system.


# Dynamic IPs with BIND9 Breaks

I've experience breakage with BIND9 when sending it too many updates in a row.
It looks like it will never delete (or at least recycle) the journals and as
a result the files grow until the whole thing crashes. After that, the
corresponding zone fails to load even on a full restart cycle.

When the IP Manager is asked to restart the BIND9 service, it will also make
sure to delete the `.jnl` files. This way, we start fresh and BIND9 work as
expected.

**Note:** This BIND9 bug is still present in Ubuntu 20.04.


# Changing BIND9 Settings

A while back, I had a need to edit BIND9 settings in my installation scripts.
The main issue with BIND9 is that you can't easily override settings by
creating a file such as:

    bind9.d/50-named.conf

Since that is not supported, you are left with having to edit the existing
`.conf` files manually. For me to me able to make such edits in installation
scripts, I decided to create a tool: `dns-options`. This tool is now part of
the IP Manager project and some bugs from the old version were fixed (it still
is very bogus, some options just can't be automatically edited just yet).


# Documentation

See the `conf/README-zones.md` for details about the configuration of `ipmgr`.
There are actually several files defined like so:

* `conf/README.md`

  Defines how to safely make changes to the `ipmgr.conf` file without the
  risk of having your changes smashed when upgrading ipmgr.

* `conf/README-ipmgr.md`

  Defines what goes in the `/etc/bind9/zones` directory. It gets installed
  in that directory.

* `conf/README-static-zones.md`

  IP Manager supports two types of zones: dynamic and static. Static zones
  are generally installed by packages and the only way to edit them is to
  update that package and then apply an update on your system.

  These zones are found under `/usr/share/ipmgr/zones/...`.

* `conf/README-zones.md`

  When a tool generates an ipmgr `.ini` file with a zone definition, it
  can save that definition under `/var/lib/ipmgr/zones/...`. These files
  are considered dynamic.

  This `README` file also includes details on how to write the `.ini`
  file with each parameter defined, how to use it, some examples, etc.

  Here is an example of configuration of a zone and as you can see, we
  use a simple .ini format: `<name>=<value>` and separate sections with
  `[<section>]`. A section is used to create and define a subdomain.

      domain=example.com
      hostmaster=alexis@example.com
      ips=10.0.0.1
      mail=mail
      ttl=60m
      refresh=3h
      retry=3m
      expire=2w
      minimum_cache_failures=5m
      dynamic=true
      nameservers="ns1.example.com ns2.example.com"

      [server]
      sub_domains=server

      [websites]
      sub_domains=${website_subdomains}
      ttl=5m
      cname=server

      [api]
      sub_domain=rest api graphql
      ips=${api_ip}

      [mail]
      sub_domains="mail"
      ips=10.0.2.1
      ttl=4w
      mail_priority=10
      mail_key=mail.example.com.key
      key_ttl=30m
      auth_server=true

      [info]
      sub_domains=info
      txt="description=this domain is the best"

* BIND9 Further Reading

  The IP Manager uses BIND9 to do the actual DNS serving part. You can find
  the [BIND9 document](https://bind9.readthedocs.io/en/latest/index.html)
  online. The IP Manager installs a few configuration files and generates
  the necessary commands to transform `ipmgr` settings found in `.ini` files
  to `.zone` and `.conf` file to add your domain names to the server. It
  also automatically restart BIND9 for you when changes were applied.


# License

The ipmgr project is covered by the GNU GENERAL PUBLIC LICENSE Version 3 or
higher.

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
