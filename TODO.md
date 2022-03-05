
# Missing Features

* Fix the serial number issue; if a zone gets updated dynamically (say by
  letsencrypt), then the serial number gets increased _under out feet_
  (i.e. our own serial file is not going to be valid and we can end up with
  the wrong serial number on the next update).
* Add many many many tests for the dns-options code; what we can do is
  a script which comes with many BIND9 options like files and run with the
  `--stdout` command line option and test the results against another file
  of what is expected; that way at least we can be more sure we do not
  regress as we fix existing issues
* Handle the multiple default entries for IPs and nameservers (we only take
  the first string at the moment).
* Fix bug: ipmgr does not detect dynamic/not dynamic change to a zone.
  (For now you can use --force to make sure you get the changes applied.)
* Fix the setup (i.e. don't run dns-setup on users).
* Implement a version that runs on a slave system.
  * On a slave, make sure to empty the cache on a restart.

gdb --args ../../BUILD/Debug/contrib/ipmgr/tools/dns-options -e acl[trusted-servers]+=192.168.55.123 named.conf.options


