# ESP32-CAM time lapse with sending picture to the Cloudinary / Blynk
Time-lapse image capture with ESP32 Camera. Camera makes picture in given interval, saves it to the micro SD card, sends it to the Cloudinary server and to the Blynk Image widget.

### Features:
* Save image in given time laps to the micro SD card
* Can save photo to the Blynk / Cloudinary

> To build a project, you need to download all the necessary libraries and create the *settings.cpp* file in the *src* folder:
```c++
#include "IPAddress.h"

// Project settings
struct Settings
{
    const char *blynkAuth = "TODO";

    const char *wifiSSID = "TODO";
    const char *wifiPassword = "TODO";
    const char *imageUploadScriptUrl = "TODO"; // se Server scripts folder
    const char *version = "1.0.0";
};
```

### Currents list:

* [ESP32-CAM](https://www.aliexpress.com/item/32992663411.html)
* 16GB micro SD card

### Sending picture from ESP32-CAM to Cloudinary / Blynk
ESP32-CAM makes picture and send it by HTTP POST request to the server via simple PHP script. I followed [this tutorial](https://robotzero.one/time-lapse-esp32-cameras/).

On the server, PHP script:
* inserts date and time to the picture and saves picture to the disk
* sends picture to the [Cloudinary API](https://cloudinary.com/documentation/upload_images#uploading_with_a_direct_call_to_the_api)
* sets picture public url from Cloudinary to the Blynk Image Widget
* removes picture from server disk

PHP script is located in the *Server scripts folder*. It's needed to set up your *upload_preset*, cloudinary URL, and *blynkAuthToken*. 