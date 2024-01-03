
# Setting Up a Zone

To setup a zone we want to define one file with the zone info.

By convention you create a file named after the corresponding domain. This
file uses one of the advgetopt supported file formats, which in this case
will look like a .ini file. A zone has a global area definition with the
global parameters of the zone and then sections for your domains.

The following describes this format in detail.

# Organization

The advgetopt library is capable of searching many directories and load
all the files it finds in all those folders. Especially, we use the `.d`
sub-directory feature to add files that the administrator is expected to
tweak.

## Tool Configuration

The `ipmgr` tool itself has configuration files. This is mainly to define
the target DNS (ip address, passwords/keys, etc.) and the location(s) of
the zone definitions.

The tool configuration file is `/etc/ipmgr/ipmgr.conf`.

The administrator can change values using the `/etc/ipmgr/ipmgr.d`
sub-directory with filenames such as `50-ipmgr.conf` (other projects may
do so using different numbers).

    dns_ip=127.0.0.1
    zone_directories="/etc/ipmgr/zones /var/lib/ipmgr/zones"
    hostmaster=alexis@example.com
    default_ips="10.0.0.1 10.0.0.2"
    default_ttl=1d
    default_refresh=3h
    default_retry=3m
    default_expire=2w
    default_minimum_cache_failures=5m
    default_nameservers="<ns1> <ns2>"

    [variables]
    <name>=<value>

The default timings defined here are used as the _Start of Authority_ (SOA)
timings. They can be overridden by a specific domain and by a specific
sub-domain. More information about these fields can be found on
(Wikipedia)[https://en.wikipedia.org/wiki/SOA_record].

### `dns_ip`

The IP address to connect to in order to do update on the BIND9 DNS. This
is generally the IP address necessary to connect to the server with
`nsupdate`.

### `zone_directories`

A list of space separated directories where the zones are stored.

By default, we expect `/etc/ipmgr/zones`, but for fully dynamic zones, you
may want to use a directory such as `/var/lib/ipmgr/zones`. The files under
`/etc/...` are not expected to be updated by your software after installation,
only by adminitrators which is why we suggest a folder under `/var`.

### `hostmaster` (SOA `RNAME` field)

An email address to use as the hostmaster email in the SOA definitions.
A specific domain can override this value.

### `default_ips`

The default IP addresses for each domain defined with the IP Manager.
In most cases, you create a DNS for one server and that server has one
IP address which you end up repeating in each zone file. This allows you
to define that IP address once.

In most cases, you are likely to have a single IP address in this variable.
You can put multiple in which case one A or AAAA record gets created per
IP. This allows for a cheap form of Round Robin mechanism. You must make
sure that the `REFRESH` (`refresh` and/or `default_refresh` parameters) is
pretty short to make it useful.

### `default_ttl` (`$TTL <value>`)

The time to live of the cache of a remote server.

### `default_refresh` (SOA `REFRESH` field)

How often secondary name servers should refresh their cache with the master.

This value is used when a specific domain doesn't overwrite it.

### `default_retry` (SOA `RETRY` field)

How quickly secondary name servers can retry requesting a domain name when
a previous request was unanswered.

The IP Manager makes sure that this value is less than the `REFRESH` field
for all the domains. If that is not the case, then the DNS zone fails to be
created.

This value is used when a specific domain doesn't overwrite it.

### `default_expire` (SOA `EXPIRE` field)

How long before this DNS information will be considered completely out of
date (unusable) on a secondary name server which is not answering that
secondary server requests to refresh the zone.

This is a form of upper limit for the time a domain can be cached on a
secondary name server (i.e. the data is considered _stalled_ after `RETRY`
seconds; this values allows the secondary server to use the value it has
in its cache up until the `EXPIRE` has also passed and the secondary server
was not able to `REFRESH` the data).

The IP Manager makes sure that this value is larger than `REFRESH` + `RETRY`.
If that is not the case, then the DNS zone fails to be created.

This value is used when a specific domain doesn't overwrite it.

### `default_minimum_cache_failures` (SOA `MINIMUM` field)

How long a secondary server has to wait when receiving a negative response
for a given request. This is for the secondadry servers to cache failures
and not re-request for an answer over and over again when it already knows
the answer is a failure.

The value used in negative responses is defined as the minimum for that
domain TTL (i.e. the value defined with `$TTL <ttl>`) and this `MINIMUM`
value.

This value is used when a specific domain doesn't overwrite it.

### `default_nameservers` (SOA MNAME)

The SOA includes one nameserver in its first line definition and it has to
defined at least two `NS` fields. These are the default. Chances are all you
zones use the exact same nameservers so it can be indicated once here.

    default_nameservers="ns1.example.com ns2.example.com"

### Variables (`<name>=<value>`)

The global `variables` section is a list of parameters and values that can be
reused in your other files.

For example, you could define the IP address of a secondary server that you
use to handle some API calls like so:

    [variables]
    api_ip=10.0.3.1
    website_subdomains="w ww www wwww"

Later you can reference that variable in another parameter like so:

    ips=${api_ip}

This simply means that the `ips` parameter is going to be set to the
value defined in the `[variables]` section. So, in other words, the first
statement is equivalent to:

    ips=10.0.3.1

This feature is actually a part of the advgetopt project.

TODO: add variables we find in the system such as the interface name and
      IP addresses.

## Keys Data-store

The DNS uses keys for DNSSEC and also for mail authentication by external
mail servers. We make use of a specific folder so that way we can make sure
it is secure (only accessible by root).

    /etc/ipmgr/keys

## Zones Configuration

The `ipmgr` tool looks for zone definitions in .ini like files defined inside
the `zones` sub-directory. The file must end with `.conf` as the `ipmgr` tool
searches using `*.conf` as the glob pattern.

You can further use sub-directories if you have many domains you can group in
some logical way:

    /etc/ipmgr/zones/company/...
    /etc/ipmgr/zones/private/...
    /etc/ipmgr/zones/customers/...

There is no default supplied by `ipmgr` since there is no default zone
requirements for DNS. This document can be used to create your first zone.

It is expected that each zone be defined in a file named after the domain
followed by the `.conf` extension:

    /etc/ipmgr/zones/example.com.conf

It is strongly adviced to include the full TLD since if you get two different
TLDs you need two different files to properly define each domain:

    /etc/ipmgr/zones/example.net.conf
    /etc/ipmgr/zones/example.org.conf

### Special Sections

The configuration files support a few special sections.

The `[variables]` can be used to define named values that you can then reuse
throughout your configuration files. See the documentation above for more
details.

A section that starts with "global-" such as `[global-txt-fields]` can be
used to manually defined a `TXT` field without a sub-domain name. This is
useful for special verification of your domain name (sp1, google, etc.)

    [global-txt-field]
    txt=google-site-verification=123
    ttl=30m

Note: You can only have one txt=... field in a section. To insert multiple
TXT field within one section, you can separate each field with the " +++ "
separator (including the spaces, but not the quotes). For example:

    [global-section]
    txt=first=field +++ second=field +++ third=field

defines three separate `TXT` fields: `first`, `second`, and `third`.

### Quick Review of Available Parameters

Here is an example file for a domain named `example.com`. Each variable
is documented below.

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

### `domain` (global)

The `domain` parameter is the full domain name, including the TLD, but
not any of the sub-domain names.

### `hostmaster` (global)

The email address of the user to contact in case the DNS doesn't work as
expected. This is pretty much never used now. I suppose it was used before
to contact people who are managing a DNS to ask them to fix it. Note that
if that email address is managed by that very specific DNS, changes are
that the email will be lost anyway.

If not defined within a domain, this address defaults to the one defined
in the `ipmgr.conf` file.

### `ips` (global, specialized)

The IP addresses this domain name or sub-domain name is assigned.

When multiple addresses are defined, round robin will be in effect so the
list of IPs will be rotated each time a new secondary server requests the
IP addresses.

The system supports IPv4 and IPv6 IP addresses. The IPv4 can be written
using the IPv6 syntax. In most cases, the IPv6 uses hexadecimal numbers
separated by colons and the IPv4 decimal numbers are separated by periods
as usual.

### `mail` (global)

The name of the section to use for the `MX` field. If not defined,
then the `mail_priority` and `mail_key` are ignored.

### `mail_priority` (global)

The priority to use with the `MX` field. This automatically defaults to 10.

### `mail_key` (global)

The filename with a key to use with the mail service. This is used to sign
emails so the receiving parties can verify that the origin is valid.

### `key_ttl` (specialized)

The mail section (i.e. see the `mail` global parameter) can include a
`key_ttl=<duration>` parameter. This is used to assign a specific TTL
for the `TXT` fields used by SPF, DMARC, DKIM.

The minimum value is 60 (1 minute). The default is 1800 (30 minutes).

### `auth_server` (specialized)

One of your domain can be set as the authoritative mail server. This is done
by setting this parameter to `true` in the `mail` section (i.e. the section
named in the `mail` global parameter). The default is `false`.

Only one mail server can be authoritative. If you setup more than one, then
an error ensues at the time we find the second one.

### `ttl` (global, specialized)

The TTL for this domain name or sub-domain.

If not specified, the `default_ttl` value is used.

### `refresh` (global)

The `REFRESH` parameter for this domain. In most cases, the default as defined
in the main `ipmgr.conf` file is enough and thus you do not redefine this
parameter here.

### `retry` (global)

The `RETRY` parameter for this domain. In most cases, the default as defined
in the main `ipmgr.conf` file is enough and thus you do not redefine this
parameter here.

### `expire` (global)

The `EXPIRE` parameter for this domain. In most cases, the default as defined
in the main `ipmgr.conf` file is enough and thus you do not redefine this
parameter here.

### `minimum_cache_failures` (global)

The `MINIMUM` parameter for this domain. In most cases, the default as defined
in the main `ipmgr.conf` file is enough and thus you do not redefine this
parameter here.

### `dynamic` (global)

Whether this zone is dynamic or not.

A dynamic zone is required when we use letsencrypt on that domain name. This
is important since letsencrypt will verify that you still own that domain
name. This means checking with your DNS and making some small modifications
to it.

Websites for which you do not need a certificate, or have a certificate in
a different way, can use the static definition which gets saved as a .zone
file inside the `/etc/bind/zones` directory.

### `sub_domains` (specialized)

Defines a set of sub-domain names to be attached to this domain.

You can have as many sub-domains defined in this list. All of these sub-domains
will be given the same set of IPs, TTL, etc. as defined within this specialized
sub-domains definition.

### `cname` (specialized)

The `cname` field gives the name of another sub-domain or the main domain.
When present, this sub-domain will be assigned a `CNAME` instead of an `A`
or `AAAA` field.

    cname=server    # use server.<domain>.
    cname=.         # use <domain>.

Note that a `CNAME` means one extra request to your DNS. If you have multiple
DNS in a large organization, then it certainly makes sense to make use of
`CNAME`. In a small organization (under 1,000 sub-domains total), you probably
want to use the `ips` field instead. After all, the syntax allows you to
reuse variables where you can save your IPs so there is no real need for a
`CNAME` in `ipmgr`.

### `[<name>]` (Start specialization)

Create a specialized section where one defines sub-domain names and their
corresponding TTL, IP addresses, etc.

The name of the section is just for your own benefit. It is not used
internally for anything other than referencing the input data (i.e. in error
messages).

Parameters supported in these sections are markedas _specialized_ above.
At this time, there is the `sub_domains`, `ttl`, and `ips`.





-- vim: ts=4 sw=4 et
