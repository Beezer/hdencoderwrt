# Introduction 
I wanted to be able to stream live HDTV from my living room to Samsung smart TV's elsewhere in the house. I experimented with various devices but none worked very well or were way too expensive. The solution that I've settled on is a combination of the [Hauppauge HD-PVR](http://www.mythtv.org/wiki/Hauppauge_HD-PVR) and the [TPLink TL-WR703N](http://wiki.openwrt.org/toh/tp-link/tl-wr703n). Neither of these devices are very expensive if purchased from ebay.

The aim of this project is to provide a plug and play solution.

# Hauppauge HD-PVR 
![](https://www.wheep.co.uk/images/180px-Hd_pvr_small.jpg)

This is a self-contained device that accepts HD component video, encodes it to a h264 video stream and makes it available to a computer via USB. The computer does not have to do any encoding or stream packaging itself. 

Note, you must use either the Hauppauge HD-PVR models 1212 or 1445. Please check the [MythTV](http://www.mythtv.org/wiki/Hauppauge_HD-PVR) page for more information.


If your video source only has HDMI, you can use a converter like this [Portta HDMI to component video converter](http://www.amazon.co.uk/gp/product/B00A8FIQXA)

# TPLink TL-WR703N 
![](https://www.wheep.co.uk/images/tl-wr703n_1.jpg)

This device is popular amongst hobbyists because it's small, cheap and capable of running custom firmware. It has a both a wired network port and wireless, and a usb 2 port for connecting the HD-PVR.

This project will provide a custom OpenWRT firmware that you can flash to your device & provide a plug & play video streaming solution.

# Samsung Tv App 
I have created an app that I can install on my Samsung Smart TV that will render the video stream. I will release this once I've cleaned it up a bit.

# TODO 
 * LuCI plugin for hdpvr so we can configure it more easily
 * Allow hdpvrd to remove/reinsert the hdpvr driver module in case it gets stuck
 * Allow hdpvrd to reboot the device if it gets real stuck
 * Sky TV channel change web service
 * Improve the Samsung plugin to include a configuration screen (it's hard coded at the moment) and to allow channel changes

# Installing 

Generic installation instructions for OpenWRT are available at 
http://wiki.openwrt.org/doc/howto/generic.flashing

##Still got factory firmware?
Then you'll want to use the "-factory.bin" image if you still have the standard
TPLink firmware installed. Upload this using the web interface. When you reboot,
you'll be running the custom firmware. Note, there is no web interface at present.
Follow the first login instructions http://wiki.openwrt.org/doc/howto/firstlogin
to set up a password

##Already running OpenWRT?
Then you'll want to use the "-sysupgrade.bin" image. Either ssh into your box and
run "sysupgrade -v <imagename>" or use the web interface if you have one. Generic
instructions at http://wiki.openwrt.org/doc/howto/generic.sysupgrade

##How to use?
There are no configurable options on this firmware. Flash it, plug your HDPVR into
the USB port, then point your video player at the video streaming URL:-

http://<IP OF BOX>:1101/video

There is also a status page at "http://<IP OF BOX>:1101/status"


If you really want to change encoder settings, please read the page at:-

http://www.mythtv.org/wiki/Hauppauge_HD-PVR

You then need to log in via SSH and add the v4l2-ctl commands into the startup script
at /etc/init.d/hdpvrd
