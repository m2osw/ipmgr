
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

# Missing Features

* Add many many many configuration files to the existing dns-options test
* Handle the multiple default entries for IPs and nameservers (we only take
  the first string at the moment).
* Fix bug: ipmgr does not detect dynamic/not dynamic change to a zone.
  (For now you can use --force to make sure you get the changes applied.)
* Fix the setup (i.e. don't run dns-setup on users).
* Implement a version that runs on a slave system.
  - On a slave, make sure to empty the cache on a restart.
* Implement a "local DNS" which generates "IP name1 name2 ..." in /etc/hosts
  which is useful for the ipload tool to have access to said names on boot
  (i.e. the firewall starts before the DNS)

gdb --args ../../BUILD/Debug/contrib/ipmgr/tools/dns-options -e acl[trusted-servers]+=192.168.55.123 named.conf.options


