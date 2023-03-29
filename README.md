# BoilerManager

This is a server and client for monitoring the temperatures on an ancient boiler in my apartment.

Solution consists of one RaspberryPi Pico W with code written in C++, 3xDS18B20 (DFR0198) sensors, 1x Kingbrite LF5WAEMBGMBW LED and the webserver running somewhere in Azure.

It's not utilizing any DB so for persistance layer uses just a text file.
![image](https://user-images.githubusercontent.com/11662240/228686476-4be4d379-54f2-4340-b7be-8e8ae4800312.png)
