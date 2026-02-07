# BoilerManager

This is a server and client for monitoring the temperatures on an ancient boiler in my apartment.

Solution consists of one RaspberryPi Pico W with code written in C++, 3xDS18B20 (DFR0198) sensors, 1x Kingbrite LF5WAEMBGMBW LED and the webserver running somewhere in Azure.

It's not utilizing any DB so for persistance layer uses just a text file.
![image](https://user-images.githubusercontent.com/11662240/228686476-4be4d379-54f2-4340-b7be-8e8ae4800312.png)

# About the client code

The code in this repo is designed to work with the SDK 1.5, with a copy of the "TLS Client" example. 

As of 2.0, with the current 2.2, RPi significantly changed the SDK architecture.

I updated the code to 2.2 and it is now available here https://github.com/AgentMC/BoilerClient

The code here will remain for the time being, in case I need to revert back to 1.5 which I have still available. Notably, the code itself does not change much, but the CMakeLists.txt does.
