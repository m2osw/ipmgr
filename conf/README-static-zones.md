
# Static Zones

The `/usr/share/ipmgr/zones` directory can be used to add static zones.
This is generally done by packages.

The content of the zone file definitions is found in file:

    /etc/ipmgr/zones/README-zones.md

# Dynamic zones

If you instead want to create a dynamic zone, please check out the
`/var/lib/ipmgr/zones`.

# Updates

Whenever you add a new zone (static or dynamic), you want to run the
compiler once:

    ipmgr

It will automatically generate the zones and configuration files as
expected by bind9.

