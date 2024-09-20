#include "esp_camera.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "FS.h"      // SD Card ESP32
#include "SD_MMC.h"  // SD Card ESP32
#define EEPROM_SIZE 1
#define SD_MMC_CMD 38             //Please do not modify it.
#define SD_MMC_CLK 39             //Please do not modify it.
#define SD_MMC_D0 40              //Please do not modify it.
#define CAMERA_MODEL_ESP32S3_EYE  // Has PSRAM
#include "camera_pins.h"
TaskHandle_t pirgetHandle = NULL;
void pirget(void *parameter);
const char *server = "7043-36-232-191-66.ngrok-free.app";
WiFiClientSecure client_tcp;


// ===========================
// Enter your WiFi credentials
// ===========================
const char *ssid = "Tab";
const char *password = "T77777777";

void startCameraServer();
void setupLedFlash(int pin);
int inPin = 42;
int val;
int pictureNumber = 0;
void setup() {

  Serial.begin(115200);

  Serial.setDebugOutput(true);
  Serial.println();
  pinMode(inPin, INPUT);
  SD_MMC.setPins(SD_MMC_CLK, SD_MMC_CMD, SD_MMC_D0);
  if (!SD_MMC.begin("/sdcard", true, true, SDMMC_FREQ_DEFAULT, 5)) {
    Serial.println("Card Mount Failed");
    return;
  }
  uint8_t cardType = SD_MMC.cardType();
  if (cardType == CARD_NONE) {
    Serial.println("No SD_MMC card attached");
    return;
  }


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
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.frame_size = FRAMESIZE_UXGA;
  config.pixel_format = PIXFORMAT_JPEG;  // for streaming
  //config.pixel_format = PIXFORMAT_RGB565; // for face detection/recognition
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 12;
  config.fb_count = 1;

  // if PSRAM IC present, init with UXGA resolution and higher JPEG quality
  //                      for larger pre-allocated frame buffer.
  if (config.pixel_format == PIXFORMAT_JPEG) {
    if (psramFound()) {
      config.jpeg_quality = 10;
      config.fb_count = 2;
      config.grab_mode = CAMERA_GRAB_LATEST;
    } else {
      // Limit the frame size when PSRAM is not available
      config.frame_size = FRAMESIZE_SVGA;
      config.fb_location = CAMERA_FB_IN_DRAM;
    }
  } else {
    // Best option for face detection/recognition
    config.frame_size = FRAMESIZE_240X240;
#if CONFIG_IDF_TARGET_ESP32S3
    config.fb_count = 2;
#endif
  }

#if defined(CAMERA_MODEL_ESP_EYE)
  pinMode(13, INPUT_PULLUP);
  pinMode(14, INPUT_PULLUP);
#endif

  // camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }

  sensor_t *s = esp_camera_sensor_get();
  // initial sensors are flipped vertically and colors are a bit saturated
  if (s->id.PID == OV3660_PID) {
    s->set_vflip(s, 1);        // flip it back
    s->set_brightness(s, 1);   // up the brightness just a bit
    s->set_saturation(s, -2);  // lower the saturation
  }
  // drop down frame size for higher initial frame rate
  if (config.pixel_format == PIXFORMAT_JPEG) {
    s->set_framesize(s, FRAMESIZE_QVGA);
  }

#if defined(CAMERA_MODEL_M5STACK_WIDE) || defined(CAMERA_MODEL_M5STACK_ESP32CAM)
  s->set_vflip(s, 1);
  s->set_hmirror(s, 1);
#endif

#if defined(CAMERA_MODEL_ESP32S3_EYE)
  s->set_vflip(s, 1);
#endif

// Setup LED FLash if LED pin is defined in camera_pins.h
#if defined(LED_GPIO_NUM)
  setupLedFlash(LED_GPIO_NUM);
#endif

WiFi.begin(ssid, password);
  WiFi.setSleep(false);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  startCameraServer();

  Serial.print("Camera Ready! Use 'http://");
  Serial.print(WiFi.localIP());
  Serial.println("' to connect");

  xTaskCreatePinnedToCore(
    pirget,
    "pirget",
    10000,
    NULL,
    1,
    &pirgetHandle,
    0);
}

void loop() {
  // Do nothing. Everything is done in another task by the web server
}

void pirget(void *parameter) {
  while (1) {
    val = digitalRead(inPin);
    Serial.print(val);
    delay(500);
    while (val) {
      client_tcp.setInsecure();
      pictureNumber++;
      if (val == 1) {
        camera_fb_t *fb = esp_camera_fb_get();  //擷取影像
        if (!fb) {
          Serial.println("拍照失敗，請檢查");
        } else {
          //儲存到SD卡:SavePictoSD(檔名,影像);
          SavePictoSD("/pic" + String(pictureNumber) + ".jpg", fb);
          val = 0;
          String payload = SendImageLine(fb);
          Serial.println(payload);
          delay(1000);
          esp_camera_fb_return(fb);
          val = 0;
          client_tcp.println("Connection: close");  //清除影像緩衝區
        }
      }
    }
  }
}

void SavePictoSD(String filename, camera_fb_t *fb) {
  Serial.print("寫入檔案:" + filename + ",檔案大小=");
  Serial.println(String(fb->len) + "bytes");
  fs::FS &fs = SD_MMC;                        //設定SD卡裝置
  File file = fs.open(filename, FILE_WRITE);  //開啟檔案
  if (!file) {
    Serial.println("存檔失敗，請檢查SD卡");
  } else {
    file.write(fb->buf, fb->len);
    Serial.println("存檔成功");
  }
}
String SendImageLine(camera_fb_t *fb) {
  client_tcp.setInsecure();
  if (client_tcp.connect("7043-36-232-191-66.ngrok-free.app", 443)) {

    //組成HTTP POST表頭
    String head = "--Cusboundary\r\nContent-Disposition: form-data;";
    head += "--Cusboundary\r\nContent-Disposition: json; ";
    head += "name=\"imageFile\"; filename=\"esp32-cam.jpg\"";
    head += "\r\nContent-Type: image/jpeg\r\n\r\n";
    String tail = "\r\n--Cusboundary--\r\n";

    client_tcp.println();
    client_tcp.print(head);
    uint16_t imageLen = fb->len;
    uint16_t extraLen = head.length() + tail.length();
    uint16_t totalLen = imageLen + extraLen;

    //開始POST傳送
    client_tcp.println("GET https://7043-36-232-191-66.ngrok-free.app/work/cat/ HTTP/1.1");
    client_tcp.println("Host: 7043-36-232-191-66.ngrok-free.app");
    client_tcp.println("Connection: close");
    client_tcp.println("Authorization: Bearer ");
    client_tcp.println("Content-Length: " + String(totalLen));
    client_tcp.println("Content-Type: multipart/form-data; boundary=Cusboundary");
    client_tcp.println();
    uint8_t *fbBuf = fb->buf;
    size_t fbLen = fb->len;
    Serial.println("傳送影像檔...");
    for (size_t n = 0; n < fbLen; n = n + 2048) {
      if (n + 2048 < fbLen) {
        client_tcp.write(fbBuf, 2048);
        fbBuf += 2048;
      } else if (fbLen % 2048 > 0) {
        size_t remainder = fbLen % 2048;
        client_tcp.write(fbBuf, remainder);
      }
    }
    client_tcp.print(tail);
    client_tcp.println();
    String Feedback = "";
    boolean state = false;
    int waitTime = 3000;  //等候時間3秒鐘
    long startTime = millis();
    delay(1000);
    Serial.print("等候回應...");
    while ((startTime + waitTime) > millis()) {
      Serial.print(".");
      delay(1000);
      while (client_tcp.available()) {
        //已收到回覆，依序讀取內容
        char c = client_tcp.read();
        Feedback += c;
      }
    }
    client_tcp.stop();
    return Feedback;
  } else {
    return "傳送失敗，請檢查網路設定";
  }
}
