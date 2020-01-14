#include "esp_http_client.h"
#include "esp_camera.h"
#include "driver/rtc_io.h"
#include <HTTPClient.h>
#include "Arduino.h"
#include <BlynkSimpleEsp32.h>
#include <TimeLib.h>
#include <WidgetRTC.h>
#include "driver/adc.h"
#include <Ticker.h>
#include "FS.h"     // SD Card ESP32
#include "SD_MMC.h" // SD Card ESP32
#include "../src/settings.cpp"

// Disable brownout problems
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// https://randomnerdtutorials.com/esp32-cam-video-streaming-face-recognition-arduino-ide/
// https://loboris.eu/ESP32/ESP32-CAM%20Product%20Specification.pdf

// https://robotzero.one/time-lapse-esp32-cameras/
// https://robotzero.one/wp-content/uploads/2019/04/Esp32CamTimelapsePost.ino

// https://randomnerdtutorials.com/esp32-cam-take-photo-save-microsd-card/

Settings settings;
WiFiClient client;

// CAMERA_MODEL_AI_THINKER
#define PWDN_GPIO_NUM 32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 0
#define SIOD_GPIO_NUM 26
#define SIOC_GPIO_NUM 27
#define Y9_GPIO_NUM 35
#define Y8_GPIO_NUM 34
#define Y7_GPIO_NUM 39
#define Y6_GPIO_NUM 36
#define Y5_GPIO_NUM 21
#define Y4_GPIO_NUM 19
#define Y3_GPIO_NUM 18
#define Y2_GPIO_NUM 5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM 23
#define PCLK_GPIO_NUM 22

// You can use all GPIOs of the header on the button side as analog inputs (12-15,2,4).
// You can define any GPIO pair as your I2C pins, just specify them in the Wire.begin() call.
// BUT analog pins doesn't works after using  WiFi.Begin() !!!

// V0 - send photo to Cloudinary/Blynk

// V1 - Blynk Image gallery, sended image
// V2 - Terminal

// V4 - Local IP
// V5 - WIFi signal
// V6 - version
// V7 - current time
// V8 - SD card information

// Attach Blynk virtual serial terminal
WidgetTerminal terminal(V2);

// Get real time
WidgetRTC rtc;

// folder with photos from camera on the SD card
String photo_dir = "/photo";

void sendDataToBlynk();
void takePhoto();

// Synchronize settings from Blynk server with device when internet is connected
BLYNK_CONNECTED()
{
  Serial.println("Blynk synchronized");
  rtc.begin();
  Blynk.syncAll();
}

// Use flash
bool send_photo = false;

bool delete_sd_card = false;

// Send one photo to Cloudinary
BLYNK_WRITE(V0)
{
  if (param.asInt() == 1)
  {
    send_photo = true;
    Serial.println("Send photo to server.");
    Blynk.virtualWrite(V0, false);
  }
}

// Terminal input
BLYNK_WRITE(V2)
{
  String valueFromTerminal = param.asStr();

  if (String("clear") == valueFromTerminal)
  {
    terminal.clear();
    terminal.println("CLEARED");
    terminal.flush();
  }
  else if (String("restart") == valueFromTerminal)
  {
    terminal.clear();
    terminal.println("Restart, bye");
    terminal.flush();
    ESP.restart();
  }
  else if (String("delete") == valueFromTerminal)
  {
    terminal.clear();
    terminal.println("DELETE SD CARD - wait max 5 minutes to timer");
    terminal.flush();
    delete_sd_card = true;
  }
  else if (valueFromTerminal != "\n" || valueFromTerminal != "\r" || valueFromTerminal != "")
  {
    terminal.println(String("unknown command: ") + valueFromTerminal);
    terminal.flush();
  }
}

bool init_wifi()
{
  int connAttempts = 0;
  Serial.println("\r\nConnecting to: " + String(settings.wifiSSID));

  WiFi.begin(settings.wifiSSID, settings.wifiPassword);

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
    if (connAttempts > 10)
      return false;
    connAttempts++;
  }
  return true;
}

bool init_blynk()
{
  Blynk.config(settings.blynkAuth);
  // timeout v milisekundach * 3
  Blynk.connect(3000);
  return Blynk.connected();
}

bool init_camera()
{
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  //init with high specs to pre-allocate larger buffers
  if (psramFound())
  {
    config.frame_size = FRAMESIZE_UXGA;
    // 0 is best, 63 lowest
    config.jpeg_quality = 8;
    config.fb_count = 2;
  }
  else
  {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

  // camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK)
  {
    Serial.printf("Camera init failed with error 0x%x", err);
    return false;
  }
  else
  {
    return true;
  }
}

esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
  switch (evt->event_id)
  {
  case HTTP_EVENT_ERROR:
    Serial.println("HTTP_EVENT_ERROR");
    break;
  case HTTP_EVENT_ON_CONNECTED:
    Serial.println("HTTP_EVENT_ON_CONNECTED");
    break;
  case HTTP_EVENT_HEADER_SENT:
    Serial.println("HTTP_EVENT_HEADER_SENT");
    break;
  case HTTP_EVENT_ON_HEADER:
    Serial.println();
    Serial.printf("HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
    break;
  case HTTP_EVENT_ON_DATA:
    Serial.println();
    Serial.printf("HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
    if (!esp_http_client_is_chunked_response(evt->client))
    {
      // Write out data
      // printf("%.*s", evt->data_len, (char*)evt->data);
    }
    break;
  case HTTP_EVENT_ON_FINISH:
    Serial.println("");
    Serial.println("HTTP_EVENT_ON_FINISH");
    break;
  case HTTP_EVENT_DISCONNECTED:
    Serial.println("HTTP_EVENT_DISCONNECTED");
    break;
  }
  return ESP_OK;
}

void take_send_photo()
{
  Serial.println("Taking picture...");
  camera_fb_t *fb = NULL;

  digitalWrite(GPIO_NUM_4, LOW);

  fb = esp_camera_fb_get();
  if (!fb)
  {
    Serial.println("Camera capture failed");
    return;
  }

  // Path where new picture will be saved in SD Card
  String date = String(day()) + "_" + String(month()) + "_" + String(year()) + "_" + String(hour()) + "-" + String(minute()) + "-" + String(second());
  String path = photo_dir + "/" + date + ".jpg";

  fs::FS &fs = SD_MMC;
  Serial.printf("Picture file name: %s\n", path.c_str());

  File file = fs.open(path.c_str(), FILE_WRITE);
  if (!file)
  {
    Serial.println("Failed to open file in writing mode");
  }
  else
  {
    file.write(fb->buf, fb->len); // payload (image), payload length
    Serial.printf("Saved file to path: %s\n", path.c_str());
  }
  file.close();

  if (send_photo)
  {
    send_photo = false;
    esp_http_client_handle_t http_client;

    esp_http_client_config_t config_client = {0};
    String url = String(settings.imageUploadScriptUrl);
    config_client.url = url.c_str();
    Serial.println("Upload URL: ") + url;
    config_client.event_handler = _http_event_handler;
    config_client.method = HTTP_METHOD_POST;

    http_client = esp_http_client_init(&config_client);

    esp_http_client_set_post_field(http_client, (const char *)fb->buf, fb->len);
    esp_http_client_set_header(http_client, "Content-Type", "image/jpg");

    Serial.println("Sending picture to the server...");
    esp_err_t err = esp_http_client_perform(http_client);
    if (err == ESP_OK)
    {
      Serial.print("HTTP status code OK: ");
      Serial.println(esp_http_client_get_status_code(http_client));
    }
    else
    {
      Serial.print("HTTP status error: ");
      Serial.println(err);
    }

    esp_http_client_cleanup(http_client);

    sendDataToBlynk();
  }

  esp_camera_fb_return(fb);
  Serial.println("OK, done");
}

bool init_SD_Card()
{
  Serial.println("Starting SD Card");
  if (!SD_MMC.begin())
  {
    Serial.println("SD Card Mount Failed");
    return false;
  }

  uint8_t cardType = SD_MMC.cardType();
  if (cardType == CARD_NONE)
  {
    Serial.println("No SD Card attached");
    return false;
  }

  if (!SD_MMC.exists(photo_dir))
  {
    Serial.println("MAKE DIR: ");
    Serial.println(SD_MMC.mkdir(photo_dir));
  }

  return true;
}

Ticker timerSendDataToBlynk(sendDataToBlynk, 288000); // 4.8 min 288000
Ticker timerTakePhoto(takePhoto, 5000);

bool setup_done = false;

bool deleting_in_progress = false;

File root;
int deleted_count = 0;
int fail_count = 0;
String rootpath = "/";
void rm(File, String);

void setup()
{
  //disable brownout detector
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  pinMode(4, INPUT);
  digitalWrite(4, LOW);
  rtc_gpio_hold_dis(GPIO_NUM_4);

  Serial.begin(115200);

  // TODO: refactor..
  if (init_SD_Card())
  {
    if (init_camera())
    {
      Serial.println("Camera OK");

      if (init_wifi())
      {
        Serial.println("Internet connected, connect to Blynk");
        if (init_blynk())
        {
          Serial.println("Blynk connected OK, wait to sync");
          Blynk.run();
          // delay for Blynk sync
          delay(1000);
          Serial.println("Setup done");
          setup_done = true;

          // send first data to internet
          sendDataToBlynk();

          timerSendDataToBlynk.start();
          timerTakePhoto.start();
        }
        else
        {
          Serial.println("Blynk failed");
        }
      }
      else
      {
        Serial.println("No WiFi");
      }
    }
    else
    {
      Serial.println("Camera init failed.");
    }
  }
  else
  {
    Serial.println("SD card init failed.");
  }

  if (!setup_done)
  {
    Serial.println("Setup failed, restart ESP");
    delay(5000);
    ESP.restart();
  }
}

void loop()
{
  timerSendDataToBlynk.update();
  timerTakePhoto.update();
  Blynk.run();
}

void deleteAllData()
{
  delete_sd_card = false;
  deleted_count = 0;
  fail_count = 0;
  deleting_in_progress = true;

  Serial.println("REMOVE");
  root = SD_MMC.open(photo_dir);

  timerTakePhoto.pause();

  terminal.println("CLEAR START IN " + String(hour()) + ":" + minute());
  terminal.flush();

  rm(root, rootpath);
  SD_MMC.rmdir(photo_dir);
  SD_MMC.mkdir(photo_dir);

  terminal.println("CLEAR ENDED IN " + String(hour()) + ":" + minute());
  terminal.println("Deleted files " + String(deleted_count));
  terminal.println("Failed files " + String(fail_count));
  terminal.println();
  terminal.flush();

  timerTakePhoto.resume();
  deleting_in_progress = false;
}

void sendDataToBlynk()
{
  Serial.println("Set values to Blynk");
  Blynk.virtualWrite(V4, "IP: " + WiFi.localIP().toString() + "|G: " + WiFi.gatewayIP().toString() + "|S: " + WiFi.subnetMask().toString() + "|DNS: " + WiFi.dnsIP().toString());
  Blynk.virtualWrite(V5, WiFi.RSSI());
  Blynk.virtualWrite(V6, settings.version);

  String currentTime = String(hour()) + ":" + minute();
  Blynk.virtualWrite(V7, currentTime);

  uint64_t usedBytes = SD_MMC.usedBytes();
  uint64_t totalBytes = SD_MMC.totalBytes();

  double percent = ((usedBytes / 1024.0) / (totalBytes / 1024.0)) * 100.0;
  String used = " used: " + String(usedBytes / 1024.0 / 1024.0) + "MB from " + String(totalBytes / 1024.0 / 1024.0) + "MB";
  Blynk.virtualWrite(V9, used);
  Blynk.virtualWrite(V10, percent);

  if (!deleting_in_progress && (percent > 5.0 || delete_sd_card))
  {
    deleteAllData();
  }
}

void takePhoto()
{
  take_send_photo();
}

void rm(File dir, String tempPath)
{
  while (true)
  {
    // keep connection alive, send data
    timerSendDataToBlynk.update();
    Blynk.run();

    // delete all..
    File entry = dir.openNextFile();
    String localPath;

    if (entry)
    {
      localPath = tempPath + entry.name() + '\0';
      char charBuf[localPath.length()];
      localPath.toCharArray(charBuf, localPath.length());

      if (SD_MMC.remove(charBuf))
      {
        Serial.print(".");
        //Serial.println(localPath);
        deleted_count++;
      }
      else
      {
       // Serial.print("Failed to delete ");
        //Serial.println(localPath);
        fail_count++;
      }
    }
    else
    {
      // break out of recursion
      break;
    }
  }
}
