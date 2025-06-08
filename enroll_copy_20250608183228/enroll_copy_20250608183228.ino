#include <Adafruit_Fingerprint.h>
#include <SoftwareSerial.h>

SoftwareSerial mySensorSerial(2, 3); // RX, TX (Sensor TX -> D2, Sensor RX <- D3 via divisor)
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&mySensorSerial);

String inputString = "";      // String para armazenar dados recebidos
boolean stringComplete = false; // Flag para indicar se uma mensagem completa foi recebida
bool sensorInitialized = false; // Flag para controlar a inicialização do sensor

// Delimitadores
const char START_MARKER = '<';
const char END_MARKER = '>';

// --- Constantes para Comandos e Respostas (usando const char* para economizar SRAM) ---
// Comandos do Python para o Arduino
const char* CMD_INIT_SENSOR = "INIT_SENSOR";
const char* CMD_ENROLL = "ENROLL";
const char* CMD_IDENTIFY = "IDENTIFY";
const char* CMD_COUNT = "COUNT";
const char* CMD_GET_IMAGE = "GET_IMAGE";
const char* CMD_IMAGE_TO_TZ1 = "IMAGE_TO_TZ1";
const char* CMD_IMAGE_TO_TZ2 = "IMAGE_TO_TZ2";
const char* CMD_CREATE_MODEL = "CREATE_MODEL";
const char* CMD_STORE_MODEL = "STORE_MODEL";
const char* CMD_REMOVE_FINGER_ACK = "REMOVE_FINGER_ACK";
const char* CMD_DOWNLOAD_TEMPLATE_B1 = "DOWNLOAD_TPL_B1";
// const char* CMD_DOWNLOAD_TEMPLATE_B2 = "DOWNLOAD_TPL_B2";

// Respostas do Arduino para Python
const char* RESP_PREFIX = "RESP:"; // Este ainda é usado para construir a string de resposta
const char* RESP_ARDUINO_READY_FOR_INIT = "ARDUINO_READY_FOR_INIT";
const char* RESP_SENSOR_READY = "SENSOR_READY";
const char* RESP_SENSOR_ERROR = "SENSOR_ERROR";
const char* RESP_UNKNOWN_COMMAND = "UNKNOWN_COMMAND";
const char* RESP_OK = "OK";
const char* RESP_FAIL = "FAIL";
const char* RESP_COMM_ERROR = "COMM_ERROR";
const char* RESP_IMAGE_FAIL = "IMAGE_FAIL";
const char* RESP_IMAGE_MESSY = "IMAGE_MESSY";
const char* RESP_FEATURE_FAIL = "FEATURE_FAIL";
const char* RESP_NO_FINGER = "NO_FINGER";
const char* RESP_ENROLL_MISMATCH = "ENROLL_MISMATCH";
const char* RESP_BAD_LOCATION = "BAD_LOCATION";
const char* RESP_FLASH_ERROR = "FLASH_ERROR";
const char* RESP_NOT_FOUND = "NOT_FOUND";
const char* RESP_ASK_PLACE_FINGER = "ASK_PLACE_FINGER";
const char* RESP_ASK_REMOVE_FINGER = "ASK_REMOVE_FINGER";
const char* RESP_ASK_PLACE_AGAIN = "ASK_PLACE_AGAIN";
const char* RESP_FINGER_REMOVED = "FINGER_REMOVED";
const char* RESP_ID_FOUND = "ID_FOUND";
const char* RESP_COUNT_RESULT = "COUNT_RESULT";
const char* RESP_TIMEOUT_PY_CMD = "TIMEOUT_WAITING_FOR_CMD";

// Constantes baseadas no manual ZFM-20
const uint8_t ZFM_PID_COMMAND = 0x01;
const uint8_t ZFM_PID_DATA = 0x02;
const uint8_t ZFM_PID_ACK = 0x07;
const uint8_t ZFM_PID_ENDDATA = 0x08;
const int TEMPLATE_SIZE = 512; // bytes
const int DATA_PACKET_PAYLOAD_SIZE = 128; // bytes (conteúdo de dados por pacote)
int pendingEnrollID = -1; // Para armazenar o ID do usuário quando um modelo é criado mas ainda não armazenado


void setup() {
  Serial.begin(9600);
  while (!Serial && (millis() < 3000)); 
  
  inputString.reserve(100); 
  
  Serial.println(F("Arduino: Setup básico concluído. Aguardando comando de inicialização do sensor do Python."));
}

bool initializeSensor() {
  Serial.println(F("Arduino: Recebido comando para inicializar sensor."));
  Serial.println(F("Arduino: Chamando mySensorSerial.begin(57600)..."));
  mySensorSerial.begin(57600);
  delay(100); 

  Serial.println(F("Arduino: Tentando finger.verifyPassword()..."));
  if (finger.verifyPassword()) {
    Serial.println(F("Arduino: finger.verifyPassword() SUCESSO."));
    Serial.println(F("Arduino: Tentando finger.getParameters()..."));
    uint8_t p_status = finger.getParameters(); 
    if (p_status == FINGERPRINT_OK) {
        Serial.println(F("Arduino: finger.getParameters() SUCESSO."));
        Serial.print(F("Arduino: Capacidade do sensor lida: "));
        Serial.println(finger.capacity);
        
        String successMsg = String(RESP_SENSOR_READY) + F(",CAP:") + String(finger.capacity);
        sendResponse(successMsg);
        return true;
    } else {
        Serial.print(F("Arduino: finger.getParameters() FALHOU com código: 0x"));
        Serial.println(p_status, HEX);
        String errorMsg = String(RESP_SENSOR_ERROR) + F(":GET_PARAMS_FAIL_CODE_0x") + String(p_status, HEX);
        sendResponse(errorMsg);
        return false;
    }
  } else {
    Serial.println(F("Arduino: finger.verifyPassword() FALHOU."));
    String errorMsg = String(RESP_SENSOR_ERROR) + F(":VERIFY_PASS_FAIL");
    sendResponse(errorMsg);
    return false;
  }
}

void loop() {
  serialEvent(); 

  if (stringComplete) {
    String commandPart = inputString; // inputString é o payload
    String command;
    String valueStr;
    int value = -1;

    // Serial.print(F("Arduino Raw Received for processing: <"));
    // Serial.print(inputString); Serial.println(F(">"));   

    int commaIndex = commandPart.indexOf(',');
    if (commaIndex != -1) {
      command = commandPart.substring(0, commaIndex);
      valueStr = commandPart.substring(commaIndex + 1);
      if (valueStr.length() > 0) { // Checar se valueStr não é vazia antes de toInt()
          value = valueStr.toInt();
      }
    } else {
      command = commandPart;
    }

    // Serial.print(F("Arduino Parsed Command: [")); Serial.print(command); 
    // Serial.print(F("], ValueStr: [")); Serial.print(valueStr); 
    // Serial.print(F("] (int: ")); Serial.print(value); Serial.println(F(")"));


    if (!sensorInitialized) {
      if (command.equals(CMD_INIT_SENSOR)) {
        sensorInitialized = initializeSensor();
        if (!sensorInitialized) {
          Serial.println(F("Arduino: Falha na inicialização do sensor. Aguardando novo comando INIT_SENSOR ou reset."));
        } else {
           Serial.println(F("Arduino: Sensor inicializado com sucesso. Pronto para comandos."));
        }
      } else {
        sendResponse(String(RESP_SENSOR_ERROR) + F(":NOT_INITIALIZED_YET"));
      }
    } else { 
      if (command.equals(CMD_ENROLL)) {
        if (value > 0 && value <= finger.capacity) {
          enrollFingerProcess(value);
        } else {
          sendResponse(String(RESP_FAIL) + F(":INVALID_ID:") + String(value) + F(",CAP:") + String(finger.capacity));
        }
      } else if (command.equals(CMD_IDENTIFY)) {
        identifyFingerProcess();
      } else if (command.equals(CMD_COUNT)) {
        getTemplateCount();
      } else if (command.equals(CMD_DOWNLOAD_TEMPLATE_B1)) {
        handleTemplateDownload(0x01); // Passa o ID do buffer (0x01 para CharBuffer1)
      } else if (command.equals(CMD_STORE_MODEL)) {
        if (pendingEnrollID != -1) {
          // Serial.print(F("Arduino: Recebido comando para armazenar modelo para ID pendente: "));
          // Serial.println(pendingEnrollID);
          int p = finger.storeModel(pendingEnrollID); // Usa o ID que foi guardado
          if (p == FINGERPRINT_OK) {
            sendResponse(String(RESP_OK) + F(":STORED:") + String(pendingEnrollID));
          } else {
              handleFingerprintError(p, F("STORE_MODEL_FAIL"));
            }
            pendingEnrollID = -1; // Limpa o ID pendente após a tentativa de armazenar
        }else {
            sendResponse(String(RESP_FAIL) + F(":NO_PENDING_MODEL_TO_STORE"));
          }
      }else if (command.equals(CMD_GET_IMAGE) || 
               command.equals(CMD_IMAGE_TO_TZ1) ||
               command.equals(CMD_IMAGE_TO_TZ2) ||
               command.equals(CMD_CREATE_MODEL) ||
               command.equals(CMD_REMOVE_FINGER_ACK) ) {
        sendResponse(String(RESP_FAIL) + F(":UNEXPECTED_SUB_COMMAND:") + command);
      }
      else {
        sendResponse(String(RESP_UNKNOWN_COMMAND) + F(":") + command);
      }
    }
  
    inputString = "";
    stringComplete = false;
  }
}

// Passar por referência constante para economizar SRAM
void sendResponse(const String& message) {
  Serial.print(START_MARKER);
  Serial.print(RESP_PREFIX); 
  Serial.print(message);      
  Serial.print(END_MARKER); 
  Serial.print('\n'); 
  Serial.flush(); 
}

void serialEvent() {
  while (Serial.available()) {
    char inChar = (char)Serial.read();
    if (inChar == START_MARKER) {
      inputString = ""; 
      stringComplete = false;
    } else if (inChar == END_MARKER) {
      if (inputString.length() > 0) {
          stringComplete = true;
      }
    } else if (inChar == '\n' || inChar == '\r') {
      // Ignora
    }
    else {
      if (inputString.length() < 98 && !stringComplete) { 
        inputString += inChar;
      }
    }
  }
}


void getTemplateCount() {
  // Serial.println(F("Arduino: Executando getTemplateCount()..."));
  uint8_t p = finger.getTemplateCount();
  if (p == FINGERPRINT_OK) {
    sendResponse(String(RESP_COUNT_RESULT) + F(":") + String(finger.templateCount));
  } else {
    // Serial.print(F("Arduino: Falha ao obter contagem, código: 0x")); Serial.println(p, HEX);
    handleFingerprintError(p, F("COUNT_FAIL_LIB_CODE"));
  }
}

void enrollFingerProcess(int id) {
  // Serial.print(F("Arduino: Iniciando processo de cadastro para ID: ")); Serial.println(id);
  sendResponse(RESP_ASK_PLACE_FINGER); 
  if (!waitForPythonCommand(CMD_GET_IMAGE)) return; 
  
  int p = -1;
  unsigned long getImageStartTime = millis();
  bool fingerPresent = false;
  // Serial.println(F("Arduino (Enroll): Esperando dedo para imagem 1..."));
  while(millis() - getImageStartTime < 7000) { 
    p = finger.getImage();
    if (p == FINGERPRINT_OK) { fingerPresent = true; break; }
    if (p == FINGERPRINT_NOFINGER) { delay(50); } 
    else { handleFingerprintError(p, F("ENROLL_IMG1_ATTEMPT")); return; }
  }
  if (!fingerPresent) { sendResponse(String(RESP_NO_FINGER) + F(":ENROLL_IMG1")); return; }
  // Serial.println(F("Arduino (Enroll): Imagem 1 OK."));
  sendResponse(String(RESP_OK) + F(":IMAGE1_TAKEN"));

  if (!waitForPythonCommand(CMD_IMAGE_TO_TZ1)) return;
  // Serial.println(F("Arduino (Enroll): Convertendo imagem 1 para Tz1..."));
  p = finger.image2Tz(1);
  if (p != FINGERPRINT_OK) { handleFingerprintError(p, F("ENROLL_CONV1")); return; }
  // Serial.println(F("Arduino (Enroll): Conversão 1 OK."));
  sendResponse(String(RESP_OK) + F(":CONVERT1_DONE"));

  sendResponse(RESP_ASK_REMOVE_FINGER);
  if (!waitForPythonCommand(CMD_REMOVE_FINGER_ACK)) return;
  
  p = 0; 
  getImageStartTime = millis(); 
  // Serial.println(F("Arduino (Enroll): Esperando remoção do dedo..."));
  while (p != FINGERPRINT_NOFINGER) {
    p = finger.getImage(); 
    if (millis() - getImageStartTime > 7000) { 
        sendResponse(String(RESP_FAIL) + F(":TIMEOUT_REMOVE_FINGER"));
        return;
    }
    delay(50); 
  }
  // Serial.println(F("Arduino (Enroll): Dedo removido."));
  sendResponse(RESP_FINGER_REMOVED);

  sendResponse(RESP_ASK_PLACE_AGAIN);
  if (!waitForPythonCommand(CMD_GET_IMAGE)) return;
  
  p = -1; 
  getImageStartTime = millis(); 
  fingerPresent = false;
  // Serial.println(F("Arduino (Enroll): Esperando dedo para imagem 2..."));
  while(millis() - getImageStartTime < 7000) {
    p = finger.getImage();
    if (p == FINGERPRINT_OK) { fingerPresent = true; break; }
     if (p == FINGERPRINT_NOFINGER) { delay(50); } 
     else { handleFingerprintError(p, F("ENROLL_IMG2_ATTEMPT")); return; }
  }
  if (!fingerPresent) { sendResponse(String(RESP_NO_FINGER) + F(":ENROLL_IMG2")); return; }
  // Serial.println(F("Arduino (Enroll): Imagem 2 OK."));
  sendResponse(String(RESP_OK) + F(":IMAGE2_TAKEN"));
  
  if (!waitForPythonCommand(CMD_IMAGE_TO_TZ2)) return;
  // Serial.println(F("Arduino (Enroll): Convertendo imagem 2 para Tz2..."));
  p = finger.image2Tz(2);
  if (p != FINGERPRINT_OK) { handleFingerprintError(p, F("ENROLL_CONV2")); return; }
  // Serial.println(F("Arduino (Enroll): Conversão 2 OK."));
  sendResponse(String(RESP_OK) + F(":CONVERT2_DONE"));

  if (!waitForPythonCommand(CMD_CREATE_MODEL)) return;
  // Serial.println(F("Arduino (Enroll): Criando modelo..."));
  p = finger.createModel();
  if (p != FINGERPRINT_OK) { 
    handleFingerprintError(p, F("ENROLL_MODEL"));
    pendingEnrollID = -1; // Limpa se falhar
    return; 
  }
  // Serial.println(F("Arduino (Enroll): Modelo criado OK."));
  sendResponse(String(RESP_OK) + F(":MODEL_CREATED"));
  pendingEnrollID = id;
}

void identifyFingerProcess() {
  // Serial.println(F("Arduino: Iniciando processo de identificação..."));
  sendResponse(RESP_ASK_PLACE_FINGER);

  if (!waitForPythonCommand(CMD_GET_IMAGE)) return;
  
  int p = -1;
  unsigned long getImageStartTime = millis();
  bool fingerPresent = false;
  // Serial.println(F("Arduino (Identify): Esperando dedo para imagem..."));
  while(millis() - getImageStartTime < 7000) { 
    p = finger.getImage();
    if (p == FINGERPRINT_OK) { fingerPresent = true; break; }
    if (p == FINGERPRINT_NOFINGER) { delay(50); }
    else { handleFingerprintError(p, F("IDENTIFY_IMG_ATTEMPT")); return; }
  }

  if (!fingerPresent) { sendResponse(String(RESP_NO_FINGER) + F(":IDENTIFY_IMG")); return; }
  // Serial.println(F("Arduino (Identify): Imagem OK."));
  sendResponse(String(RESP_OK) + F(":IMAGE_TAKEN"));

  if (!waitForPythonCommand(CMD_IMAGE_TO_TZ1)) return; 
  // Serial.println(F("Arduino (Identify): Convertendo imagem para Tz1..."));
  p = finger.image2Tz(1); 
  if (p != FINGERPRINT_OK) { handleFingerprintError(p, F("IDENTIFY_CONV")); return; }
  // Serial.println(F("Arduino (Identify): Conversão OK."));
  sendResponse(String(RESP_OK) + F(":CONVERT_DONE"));

  // Serial.println(F("Arduino (Identify): Procurando digital..."));
  p = finger.fingerFastSearch(); 
  if (p == FINGERPRINT_OK) {
    // Serial.print(F("Arduino (Identify): Digital encontrada! ID: ")); Serial.print(finger.fingerID);
    // Serial.print(F(", Confiança: ")); Serial.println(finger.confidence);
    sendResponse(String(RESP_ID_FOUND) + F(":") + String(finger.fingerID) + F(",CONFIDENCE:") + String(finger.confidence));
  } else if (p == FINGERPRINT_NOTFOUND) {
    // Serial.println(F("Arduino (Identify): Digital não encontrada."));
    sendResponse(RESP_NOT_FOUND);
  } else {
    handleFingerprintError(p, F("IDENTIFY_SEARCH"));
  }
}

// Passar expectedCommand por referência constante
bool waitForPythonCommand(const String& expectedCommand) {
  String receivedCmdPayload = ""; 
  unsigned long startTime = millis();
  
  // Serial.print(F("Arduino: Esperando comando Python '")); 
  // Serial.print(expectedCommand); Serial.println(F("'...")); 
  
  while (millis() - startTime < 10000) { 
    serialEvent(); 
    
    if (stringComplete) {
      receivedCmdPayload = inputString; 
      inputString = "";       
      stringComplete = false; 

      // Serial.print(F("Arduino (waitForPythonCommand): Recebido payload '")); 
      // Serial.print(receivedCmdPayload); Serial.println(F("'")); 

      String actualCommandBase = receivedCmdPayload;
      int commaPos = receivedCmdPayload.indexOf(',');
      if (commaPos != -1) {
        actualCommandBase = receivedCmdPayload.substring(0, commaPos);
      }

      if (actualCommandBase.equals(expectedCommand)) {
        // Serial.print(F("Arduino (waitForPythonCommand): Comando '")); 
        // Serial.print(expectedCommand); Serial.println(F("' recebido e aceito.")); 
        return true; 
      } else {
        // Serial.print(F("Arduino (waitForPythonCommand): Comando inesperado '")); 
        // Serial.print(receivedCmdPayload); Serial.print(F("', esperando por '")); 
        // Serial.print(expectedCommand); Serial.println(F("'. Continuando a esperar...")); 
      }
    }
    delay(10); 
  }
  
  // Serial.print(F("Arduino: TIMEOUT esperando por comando Python '")); 
  // Serial.print(expectedCommand); Serial.println(F("'.")); 
  sendResponse(String(RESP_FAIL) + F(":") + RESP_TIMEOUT_PY_CMD + F(":") + expectedCommand);
  return false; 
}

// Passar context por referência constante (se for String) ou usar F() se for literal
void handleFingerprintError(int errorCode, const __FlashStringHelper* context) { // Para F() macro
  String errorMsg = String(RESP_FAIL) + ":" + String(context) + ":"; // Precisa converter __FlashStringHelper para String
  // Serial.print(F("Arduino: Erro no sensor - Contexto: ")); Serial.print(context); 
  // Serial.print(F(", Código: 0x")); Serial.println(errorCode, HEX); 

  switch (errorCode) {
    case FINGERPRINT_PACKETRECIEVEERR: errorMsg += RESP_COMM_ERROR; break;
    case FINGERPRINT_NOFINGER:         errorMsg += RESP_NO_FINGER; break; 
    case FINGERPRINT_IMAGEFAIL:        errorMsg += RESP_IMAGE_FAIL; break;
    case FINGERPRINT_IMAGEMESS:        errorMsg += RESP_IMAGE_MESSY; break;
    case FINGERPRINT_FEATUREFAIL:      errorMsg += RESP_FEATURE_FAIL; break;
    case FINGERPRINT_ENROLLMISMATCH:   errorMsg += RESP_ENROLL_MISMATCH; break;
    case FINGERPRINT_BADLOCATION:      errorMsg += RESP_BAD_LOCATION; break;
    case FINGERPRINT_FLASHERR:         errorMsg += RESP_FLASH_ERROR; break;
    case FINGERPRINT_NOTFOUND:         errorMsg += RESP_NOT_FOUND; break; 
    default:
      errorMsg += F("UNKNOWN_SENSOR_ERR_0x"); // Usar F() aqui também
      char hexErrorCode[3]; 
      sprintf(hexErrorCode, "%02X", errorCode); 
      errorMsg += hexErrorCode; // hexErrorCode já é char array, ok
      break;
  }
  sendResponse(errorMsg);
}

// Sobrecarga para aceitar String comum se necessário (embora F() seja preferível para constantes)
void handleFingerprintError(int errorCode, const String& context) {
  String errorMsg = String(RESP_FAIL) + ":" + context + ":";
  // ... (mesmo switch case, mas sem F() em errorMsg += ...)
   switch (errorCode) {
    case FINGERPRINT_PACKETRECIEVEERR: errorMsg += RESP_COMM_ERROR; break;
    case FINGERPRINT_NOFINGER:         errorMsg += RESP_NO_FINGER; break; 
    case FINGERPRINT_IMAGEFAIL:        errorMsg += RESP_IMAGE_FAIL; break;
    case FINGERPRINT_IMAGEMESS:        errorMsg += RESP_IMAGE_MESSY; break;
    case FINGERPRINT_FEATUREFAIL:      errorMsg += RESP_FEATURE_FAIL; break;
    case FINGERPRINT_ENROLLMISMATCH:   errorMsg += RESP_ENROLL_MISMATCH; break;
    case FINGERPRINT_BADLOCATION:      errorMsg += RESP_BAD_LOCATION; break;
    case FINGERPRINT_FLASHERR:         errorMsg += RESP_FLASH_ERROR; break;
    case FINGERPRINT_NOTFOUND:         errorMsg += RESP_NOT_FOUND; break; 
    default:
      errorMsg += "UNKNOWN_SENSOR_ERR_0x"; // Sem F() aqui se RESP_COMM_ERROR etc são const char*
      char hexErrorCode[3]; 
      sprintf(hexErrorCode, "%02X", errorCode); 
      errorMsg += hexErrorCode;
      break;
  }
  sendResponse(errorMsg);
}

void handleTemplateDownload(uint8_t bufferId_to_upload_from) { // bufferId pode ser 0x01 ou 0x02
    // Serial.println(F("Arduino: Solicitando upload de template do sensor..."));

    // 1. Enviar comando UpChar (0x08) para o sensor
    // A função finger.getModel() da lib Adafruit já faz isso para o CharBuffer1.
    // Se quisermos do CharBuffer2, teríamos que construir o pacote manualmente ou modificar getModel().
    // Por enquanto, vamos assumir que o modelo está no CharBuffer1 (padrão do getModel).
    uint8_t p = finger.getModel(); // Envia UpChar para CharBuffer1
    if (p != FINGERPRINT_OK) {
        sendResponse(String(RESP_FAIL) + F(":GETMODEL_CMD_FAIL_CODE_0x") + String(p, HEX));
        return;
    }
    // Se chegou aqui, o sensor respondeu OK e VAI começar a enviar pacotes de dados.
    sendResponse(String(RESP_OK) + F(":TEMPLATE_UPLOAD_CMD_ACKNOWLEDGED"));

    uint8_t sensorPacketBuffer[DATA_PACKET_PAYLOAD_SIZE + 12]; // Suficiente para cabeçalho + payload + checksum
    int totalTemplateBytesReceived = 0;
    bool downloadError = false;

    while (totalTemplateBytesReceived < TEMPLATE_SIZE && !downloadError) {
        unsigned long packetStartTime = millis();
        int availableBytes;
        // Esperar por dados suficientes para pelo menos o cabeçalho de um pacote (9 bytes)
        do {
            availableBytes = mySensorSerial.available();
            if (millis() - packetStartTime > 2000) { // Timeout para esperar o início de um pacote
                sendResponse(String(RESP_FAIL) + F(":TIMEOUT_WAITING_SENSOR_DATA_PACKET"));
                downloadError = true;
                break;
            }
            delay(1);
        } while (availableBytes < 9);

        if (downloadError) break;

        // Ler e validar o cabeçalho do pacote do sensor
        if ((uint8_t)mySensorSerial.read() != (FINGERPRINT_STARTCODE >> 8) ||
            (uint8_t)mySensorSerial.read() != (FINGERPRINT_STARTCODE & 0xFF)) {
            sendResponse(String(RESP_FAIL) + F(":BAD_TEMPLATE_PACKET_START_CODE"));
            downloadError = true; break;
        }
        // Ler endereço (4 bytes) - vamos apenas consumir
        for (int i = 0; i < 4; i++) { mySensorSerial.read(); }

        uint8_t pid = (uint8_t)mySensorSerial.read();
        uint16_t packetLength = ((uint8_t)mySensorSerial.read()) << 8;
        packetLength |= (uint8_t)mySensorSerial.read();

        if (pid != ZFM_PID_DATA && pid != ZFM_PID_ENDDATA) {
            sendResponse(String(RESP_FAIL) + F(":UNEXPECTED_PID_0x") + String(pid, HEX));
            downloadError = true; break;
        }

        // packetLength = N (payload) + 2 (checksum)
        uint16_t payloadLength = packetLength - 2;
        if (payloadLength > DATA_PACKET_PAYLOAD_SIZE || payloadLength == 0) {
            sendResponse(String(RESP_FAIL) + F(":INVALID_PAYLOAD_LENGTH_") + String(payloadLength));
            downloadError = true; break;
        }

        // Envia para o Python APENAS os dados do template em formato hexadecimal
        Serial.print(START_MARKER); Serial.print(RESP_PREFIX);
        Serial.print(F("TEMPLATE_CHUNK:"));

        uint16_t calculatedChecksum = pid + (packetLength >> 8) + (packetLength & 0xFF);

        for (int i = 0; i < payloadLength; i++) {
            unsigned long byteReadStartTime = millis();
            while (!mySensorSerial.available()) {
                if (millis() - byteReadStartTime > 100) { // Timeout para um byte individual
                    sendResponse(String(RESP_FAIL) + F(":TIMEOUT_READING_PAYLOAD_BYTE"));
                    downloadError = true; break;
                }
                delay(1);
            }
            if (downloadError) break;

            uint8_t templateByte = (uint8_t)mySensorSerial.read();
            if (templateByte < 0x10) Serial.print('0');
            Serial.print(templateByte, HEX);
            calculatedChecksum += templateByte;
        }
        if (downloadError) { // Se erro ao ler payload, fechar a tag e sair
            Serial.print(END_MARKER); Serial.print('\n'); Serial.flush();
            break;
        }

        Serial.print(END_MARKER); Serial.print('\n'); Serial.flush();
        totalTemplateBytesReceived += payloadLength;

        // Ler checksum do sensor (2 bytes)
        uint16_t sensorChecksum;
        unsigned long chkReadStartTime = millis();
        while(mySensorSerial.available() < 2) {
            if (millis() - chkReadStartTime > 100) {
                 sendResponse(String(RESP_FAIL) + F(":TIMEOUT_READING_CHECKSUM"));
                 downloadError = true; break;
            }
            delay(1);
        }
        if (downloadError) break;
        
        sensorChecksum = ((uint8_t)mySensorSerial.read()) << 8;
        sensorChecksum |= (uint8_t)mySensorSerial.read();

        if (calculatedChecksum != sensorChecksum) {
            sendResponse(String(RESP_FAIL) + F(":TEMPLATE_CHUNK_CHECKSUM_MISMATCH"));
            downloadError = true; break;
        }

        if (pid == ZFM_PID_ENDDATA) {
            if (totalTemplateBytesReceived != TEMPLATE_SIZE) {
                sendResponse(String(RESP_FAIL) + F(":END_PACKET_BUT_SIZE_MISMATCH_RECV_") + String(totalTemplateBytesReceived));
                downloadError = true;
            }
            break; // Fim da transmissão
        }
    } // Fim do while (totalTemplateBytesReceived < TEMPLATE_SIZE && !downloadError)

    if (!downloadError && totalTemplateBytesReceived == TEMPLATE_SIZE) {
        sendResponse(String(RESP_OK) + F(":TEMPLATE_DOWNLOAD_COMPLETE:") + String(totalTemplateBytesReceived));
    } else if (!downloadError && totalTemplateBytesReceived != TEMPLATE_SIZE) {
        // Chegou aqui se o loop terminou por timeout antes de receber todos os bytes, mas sem erro de checksum/pacote
        sendResponse(String(RESP_FAIL) + F(":DOWNLOAD_INCOMPLETE_TIMEOUT_RECV_") + String(totalTemplateBytesReceived));
    }
    // Se downloadError foi true, a mensagem de erro específica já foi enviada.
    // Limpar qualquer lixo restante na serial do sensor
    delay(50); // Dá um tempo para o sensor terminar de enviar, se for o caso
    while(mySensorSerial.available()) mySensorSerial.read();
}
