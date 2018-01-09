Sample DHCPv4 client application
################################

Overview
********

This application starts a DHCPv4 client, gets an IPv4 address from the
DHCPv4 server, and prints address, lease time, netmask and router
information to a serial console.

Requirements
************

- :ref:`networking with Qemu <networking_with_qemu>`

Building and Running
********************

QEMU x86
========

These are instructions for how to use this sample application using
QEMU on a Linux host to negotiate IP address from DHCPv4 server running
on Linux host.

Follow readme from:

    Run 'loop_socat' and 'loop-slip-tap' scripts from net-tools.

    https://gerrit.zephyrproject.org/r/gitweb?p=net-tools.git;a=blob;f=README

.. code-block:: console

    $ ./loop_socat.sh

In another window:

.. code-block:: console

    $ sudo ./loop-slip-tap.sh

Here's a sample server configuration file '/etc/dhcpd/dhcp.conf'
used to configure the DHCPv4 server:

.. code-block:: console

   log-facility local7;
   default-lease-time 600;
   max-lease-time 7200;

   subnet 192.0.2.0 netmask 255.255.255.0 {
     range 192.0.2.10 192.0.2.100;
   }

Use another terminal window to start up a DHCPv4 server on the Linux host,
using this conf file:

.. code-block:: console

    $ sudo dhcpd -d -4 -cf /etc/dhcp/dhcpd.conf -lf /var/lib/dhcp/dhcpd.leases tap0

Run Zephyr samples/net/dhcpv4_client application in QEMU:

.. code-block:: console

    $ cd $ZEPHYR_BASE/samples/net/dhcpv4_client
    $ make pristine && make qemu

Once DHCPv4 client address negotiation completed with server, details
are shown like this:

.. code-block:: console

    [dhcpv4] [INF] main: In main
    [dhcpv4] [INF] main_thread: Run dhcpv4 client
    [dhcpv4] [INF] handler: Your address: 192.0.2.10
    [dhcpv4] [INF] handler: Lease time: 600
    [dhcpv4] [INF] handler: Subnet: 255.255.255.0
    [dhcpv4] [INF] handler: Router: 0.0.0.0

To verify the Zephyr application client is running and has received
an ip address by typing:

.. code-block:: console

    $ ping -I tap0 192.0.2.10


FRDM_K64F
=========

These are instructions for how to use this sample application running on
:ref:`frdm_k64f` board to negotiate IP address from DHCPv4 server running on
Linux host.

Connect ethernet cable from :ref:`Freedom-K64F board <frdm_k64f>` to Linux host
machine and check for new interfaces:

.. code-block:: console

    $ ifconfig

Add ip address and routing information to interface:

.. code-block:: console

    $ sudo ip addr add 192.0.2.2 dev eth1
    $ sudo ip route add 192.0.2.0/24 dev eth1

Here's a sample server configuration file '/etc/dhcpd/dhcp.conf'
used to configure the DHCPv4 server:

.. code-block:: console

   log-facility local7;
   default-lease-time 600;
   max-lease-time 7200;

   subnet 192.0.2.0 netmask 255.255.255.0 {
     range 192.0.2.10 192.0.2.100;
   }

Use another terminal window to start up a DHCPv4 server on the Linux host,
using this conf file:

.. code-block:: console

    $ sudo dhcpd -d -4 -cf /etc/dhcp/dhcpd.conf -lf /var/lib/dhcp/dhcpd.leases eth1

Build Zephyr samples/net/dhcpv4_client application:

.. code-block:: console

    $ cd $ZEPHYR_BASE/samples/net/dhcpv4_client
    $ make pristine && make BOARD=frdm_k64f
    $ cp outdir/frdm_k64f/zephyr.bin /media/rveerama/MBED/

Once DHCPv4 client address negotiation completed with server, details
are shown like this:

.. code-block:: console

    $ sudo screen /dev/ttyACM0 115200
    [dhcpv4] [INF] main: In main
    [dhcpv4] [INF] main_thread: Run dhcpv4 client
    [dhcpv4] [INF] handler: Your address: 192.0.2.10
    [dhcpv4] [INF] handler: Lease time: 600
    [dhcpv4] [INF] handler: Subnet: 255.255.255.0
    [dhcpv4] [INF] handler: Router: 0.0.0.0

To verify the Zephyr application client is running and has received
an ip address by typing:

.. code-block:: console

    $ ping -I eth1 192.0.2.10
