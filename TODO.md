
# Possible Bugs

* I added an entry for our DNS2 and it looks like it viewed it as a subdomain
  name.

      [ns2]
      subdomains=ns2
      ips=1.2.3.4

  Got this error:

  > 2023/06/03 20:00:12 do20 ipmgr[432802]: error: each nameserver subdomain
  > must have a unique IP address, found x.x.x.x twice, check subdomain
  > "ns2.m2osw.com". (in function "generate_zone_file()") (ipmgr.cpp:1713)

  when I renamed the section as `[dns2]`, it worked.

* The first serial number is 2 instead of 1.

* Handle the multiple default entries for IPs and nameservers (we only take
  the first string at the moment).

* The ipmgr tool will stop the bind9 service while doing updates and then
  restart it. If the restart fails, the service remains inactive. If later
  on you apply a fix, ipmgr does not detect bind9 as active so it does
  nothing on that end, leaving the service inactive. I'm thinking that we
  may want to have a flag as in a file somewhere that tells us the status
  (i.e. tells ipmgr that it is the one that stopped bind9 in the first
  place and thus it has to restart it on further changes).

  We shall use the raise-flag feature found in the communicator.

# Missing Features

* Add many many many configuration files to the existing dns-options test
* Fix the setup (i.e. don't run dns-setup on users).
* Implement a version that runs on a slave system.
  - On a slave, allow for emptying the cache on a restart.
    (this may not be what we want, instead have a command line tool which
    stops, delete the cache, restarts once the primary DNS is working as
    expected)
* Implement a "local DNS" which generates "IP name1 name2 ..." in /etc/hosts
  which is useful for the ipload tool to have access to said names on boot
  (i.e. the firewall starts before the DNS)
* Think about having a manager allowing us to edit the configuration files
  from a website (however, if you were to create a package to install said
  files, this is not that practical--i.e. you could not easily move from one
  server to another unless the files are part of your build process)
* Look into what I've done for m2osw to allow for TLS and postfix entries
  and make it easier here to handle those cases.

