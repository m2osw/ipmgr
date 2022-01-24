
# Zone Files

This directory is created when installing the ipmgr package.

It holds a list of static zones that the `ipmgr` command generates from
your configuration files found under `/etc/ipmgr/zones/...` and
`/var/lib/ipmgr/zones/...`.

For dynamic zones, we instead make use of the rndc and nsupdate commands
since files found under `/var/lib/bind/...` are not expected to be modified
by us at any one time.

-- vim: ts=4 sw=4 et
