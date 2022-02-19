
# Missing Features

* Handle the multiple default entries for IPs and nameservers (we only take
  the first string at the moment)
* Added necessary code to generate a mail server key from scratch
* Fix bug: ipmgr does not detect dynamic/not dynamic change to a zone.
  (For now you can use --force to make sure you get the changes applied.)

