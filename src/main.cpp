#include <Arduino.h>
#include "LoraMesher.h"
#include <driver/i2s.h>

// Cấu hình SX1262
#define LORA_CS    4
#define LORA_IRQ   1
#define LORA_RST   3
#define LORA_IO1   1

// Cấu hình SPI
#define LORA_SCK   7
#define LORA_MISO  8
#define LORA_MOSI  9

// Cấu hình LED và Nút
#define BOARD_LED 2
#define STOP_BTN  5
#define LED_ON    LOW
#define LED_OFF   HIGH

// Cấu hình I2S Mic (GPIO6, GPIO43, GPIO44)
#define I2S_WS        6
#define I2S_SD        44
#define I2S_SCK       43
#define I2S_PORT      I2S_NUM_0
#define I2S_BUF_LEN   512

// Cấu hình LoRa
#define NETWORK_FREQ   869.9f
#define NETWORK_BW     125.0f
#define NETWORK_SF     7
#define NETWORK_POWER  10

// Gói tin E2E
struct E2EPacket {
    uint8_t audioData[I2S_BUF_LEN];
    uint16_t dataSize;
    uint32_t timestamp;
};

LoraMesher& radio = LoraMesher::getInstance();
TaskHandle_t receiveTaskHandle = NULL;

bool isGateway = false;
bool isRecording = false;
uint16_t localAddress = 0;
uint16_t gatewayAddress = 0x0001;
uint8_t i2sBuffer[I2S_BUF_LEN];

void processReceivedPackets(void* parameters) {
    for (;;) {
        ulTaskNotifyTake(pdPASS, portMAX_DELAY);
        while (radio.getReceivedQueueSize() > 0) {
            AppPacket<E2EPacket>* packet = radio.getNextAppPacket<E2EPacket>();
            if (packet) {
                E2EPacket* e2e = packet->payload;
                Serial.printf("Nhận E2E từ %04X - Size: %d - Time: %u\n", 
                              packet->src, e2e->dataSize, e2e->timestamp);
                radio.deletePacket(packet);
            }
        }
    }
}

void setupI2S() {
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = 16000,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = 64,
        .use_apll = false,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0
    };
    i2s_pin_config_t pin_config = {
        .bck_io_num = I2S_SCK,
        .ws_io_num = I2S_WS,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = I2S_SD
    };
    i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_PORT, &pin_config);
    i2s_zero_dma_buffer(I2S_PORT);
}

size_t readI2S(uint8_t* buffer, size_t size) {
    size_t bytesRead = 0;
    i2s_read(I2S_PORT, buffer, size, &bytesRead, portMAX_DELAY);
    return bytesRead;
}

void setupLoRa() {
    LoraMesher::LoraMesherConfig config;
    config.module = LoraMesher::LoraModules::SX1262_MOD;
    config.loraCs = LORA_CS;
    config.loraIrq = LORA_IRQ;
    config.loraRst = LORA_RST;
    config.loraIo1 = LORA_IO1;
    config.freq = NETWORK_FREQ;
    config.bw = NETWORK_BW;
    config.sf = NETWORK_SF;
    config.power = NETWORK_POWER;
    config.max_packet_size = 240;
    
    radio.begin(config);
    
    if (isGateway) {
        radio.addGatewayRole();
    }
    
    xTaskCreate(processReceivedPackets, "Receive", 4096, NULL, 2, &receiveTaskHandle);
    radio.setReceiveAppDataTaskHandle(receiveTaskHandle);
    radio.start();
    localAddress = radio.getLocalAddress();
}

void setup() {
    Serial.begin(115200);
    pinMode(BOARD_LED, OUTPUT);
    pinMode(STOP_BTN, INPUT_PULLUP);
    
    uint8_t nodeId = 2;
    isGateway = (nodeId == 1);
    
    setupLoRa();
    setupI2S();
    
    Serial.printf("Node %04X - %s\n", localAddress, isGateway ? "GATEWAY" : "SENSOR");
}

void loop() {
    static uint8_t audioBuffer[I2S_BUF_LEN];
    static uint16_t audioIndex = 0;
    
    if (digitalRead(STOP_BTN) == LOW) {
        if (!isRecording) {
            isRecording = true;
            audioIndex = 0;
            Serial.println("Đang ghi âm...");
            digitalWrite(BOARD_LED, LED_ON);
        }
        
        size_t bytesRead = readI2S(i2sBuffer, I2S_BUF_LEN);
        if (bytesRead > 0 && audioIndex + bytesRead < I2S_BUF_LEN) {
            memcpy(&audioBuffer[audioIndex], i2sBuffer, bytesRead);
            audioIndex += bytesRead;
        }
    } else {
        if (isRecording) {
            isRecording = false;
            digitalWrite(BOARD_LED, LED_OFF);
            
            if (audioIndex > 0 && !isGateway) {
                E2EPacket packet;
                memcpy(packet.audioData, audioBuffer, audioIndex);
                packet.dataSize = audioIndex;
                packet.timestamp = millis();
                
                Serial.printf("Gửi E2E - Size: %d\n", audioIndex);
                radio.sendReliable(gatewayAddress, reinterpret_cast<uint8_t*>(&packet), sizeof(packet));
            }
            Serial.println("Đã gửi!");
        }
    }
    
    delay(10);
}