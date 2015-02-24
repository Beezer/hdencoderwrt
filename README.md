Installing

Generic installation instructions for OpenWRT are available at 
http://wiki.openwrt.org/doc/howto/generic.flashing

Still got factory firmware?
Then you'll want to use the "-factory.bin" image if you still have the standard
TPLink firmware installed. Upload this using the web interface. When you reboot,
you'll be running the custom firmware. Note, there is no web interface at present.
Follow the first login instructions http://wiki.openwrt.org/doc/howto/firstlogin
to set up a password

Already running OpenWRT?
Then you'll want to use the "-sysupgrade.bin" image. Either ssh into your box and
run "sysupgrade -v <imagename>" or use the web interface if you have one. Generic
instructions at http://wiki.openwrt.org/doc/howto/generic.sysupgrade

How to use?
There are no configurable options on this firmware. Flash it, plug your HDPVR into
the USB port, then point your video player at the video streaming URL:-

http://<IP OF BOX>:1101/video

There is also a status page at "http://<IP OF BOX>:1101/status"


If you really want to change encoder settings, please read the page at:-

http://www.mythtv.org/wiki/Hauppauge_HD-PVR

You then need to log in via SSH and add the v4l2-ctl commands into the startup script
at /etc/init.d/hdpvrd
