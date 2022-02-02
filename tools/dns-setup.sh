#!/bin/sh -e
#
# Setup a few parameters to make BIND9 settings a little safer (by ofuscation
# which is useless but required by PCI)

# Fixed a few parameters which are considered unsafe by PCI
# (these are really just ofuscation of the DNS)
#
NAMED_CONF=/etc/bind/named.conf.options
DNS_OPTIONS=dns-options
NAMED_LOG_PATH=/var/log/named

# Make a backup but only once (in case the .bak already exists, do not
# do a backup)
#
if test ! -f ${NAMED_CONF}.bak
then
    cp ${NAMED_CONF} ${NAMED_CONF}.bak
fi

# Setup the three most important values used to hide the DNS implementation
# (See SNAP-522)
#
${DNS_OPTIONS} -e 'options.version = none'   ${NAMED_CONF}
${DNS_OPTIONS} -e 'options.hostname = none'  ${NAMED_CONF}
${DNS_OPTIONS} -e 'options.server-id = none' ${NAMED_CONF}

# Setup the bogus network IP addresses
# (See SNAP-553)
#
${DNS_OPTIONS} -e 'acl[bogusnets]._ = 0.0.0.0/8;'      ${NAMED_CONF}
${DNS_OPTIONS} -e 'acl[bogusnets]._ = 192.0.2.0/24;'   ${NAMED_CONF}
${DNS_OPTIONS} -e 'acl[bogusnets]._ = 224.0.0.0/3;'    ${NAMED_CONF}

# The following may be used by the private network
#
#${DNS_OPTIONS} -e 'acl[bogusnets]._ = 10.0.0.0/8;'     ${NAMED_CONF}
#${DNS_OPTIONS} -e 'acl[bogusnets]._ = 172.16.0.0/12;'  ${NAMED_CONF}
#${DNS_OPTIONS} -e 'acl[bogusnets]._ = 192.168.0.0/16;' ${NAMED_CONF}

${DNS_OPTIONS} -e 'options.blackhole._ = bogusnets;' ${NAMED_CONF}

# Define a security log file
# (See SNAP-483)
#
${DNS_OPTIONS} -e 'logging.category[security]._ = security_file'      ${NAMED_CONF}
${DNS_OPTIONS} -e 'logging.channel[security_file].file = "'${NAMED_LOG_PATH}'/security.log" versions 3 size 30m' ${NAMED_CONF}
${DNS_OPTIONS} -e 'logging.channel[security_file].severity = dynamic' ${NAMED_CONF}
${DNS_OPTIONS} -e 'logging.channel[security_file].print-time = yes'   ${NAMED_CONF}

# Create the log folder, no rotation is required as the definition
# in the options already defines how many files and their size we
# accept (i.e. 3 files each of up to 30Mb)
#
if test ! -d ${NAMED_LOG_PATH}
then
    mkdir -p ${NAMED_LOG_PATH}
    chown bind:bind ${NAMED_LOG_PATH}
fi

# vim: ts=4 sw=4 et