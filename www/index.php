<?php
  $selected="index";
  include("header.inc");
?>

<H2>Main Screen</H2>
Welcome to the home of SvxLink. 
The SvxLink project aim to develop a flexible general purpose voice services
system for ham radio use. The svxlink server consists of a core that handles
the connection to the tranceiver. The transceiver audio is connected to the PC
through the sound card and the PTT is controlled by a pin in the serial port.
The core can be configured to act as a repeater controller or to operate on a
simplex channel.
The voice services are loaded into the core as plugins called modules in
SvxLink lingo. Existing voice services are: Help - a help system,
Parrot - a module that plays back everything you say, EchoLink - connect
to other EchoLink stations and TclVoiceMail - a simple voice mail system.
The project also includes an EchoLink client GUI application (Qtel).
<P/>
EchoLink is an amateur radio invention (well actually it is just a
modified verison of IP telephony) to link radio transceivers together
over the Internet. You must have an amateur radio license to use
it. The original EchoLink software can be found at
<A href="http://www.echolink.org/">http://www.echolink.org/</A>. However,
this software only support the Windows operating system and it is
closed source. SvxLink is released under the GPL license. 
<P/>
Qtel is only an EchoLink client program. It does not have the
sysop mode. That is, it can not be connected to a transceiver
and act as a link. For the latter, use the svxlink server.
<P/>
SvxLink is known to work under these operating systems (I really hope that
this list will grow in time):
<UL>
  <LI>Red Hat Linux 9 (RPMS provided for older versions)</LI>
  <LI>Fedora Core 1 (only tested with older versions)</LI>
  <LI>Fedora Core 2 (RPMS provided for older versions)</LI>
  <LI>Fedora Core 3 (RPMS provided)</LI>
  <LI>Fedora Core 4 (RPMS provided)</LI>
  <LI>SuSE 9.1 (only tested with older versions)</LI>
  <LI>Slackware 10 (not tested by author)</LI>
  <LI>SuSE Linux 9.2 Pro (not tested by author)</LI>
</UL>
<P/>

<?php include("footer.inc"); ?>
