import serial
import time

# --- Configurações ---
SERIAL_PORT = 'COM3'  # MUDE PARA A SUA PORTA SERIAL CORRETA
BAUD_RATE_ARDUINO = 9600
# ARDUINO_INIT_TIMEOUT = 5 # Tempo para o Arduino inicializar e enviar SENSOR_READY (após INIT_SENSOR)
ARDUINO_SENSOR_INIT_TIMEOUT = 10 # Tempo maior para o Arduino inicializar o sensor
RESPONSE_TIMEOUT = 15    # Timeout geral para respostas do Arduino

arduino_serial = None # Variável global para a conexão serial

# Delimitadores
START_MARKER = '<'
END_MARKER = '>'

# --- Constantes para Comandos (Python para Arduino) ---
CMD_INIT_SENSOR = "INIT_SENSOR" # Novo comando
CMD_ENROLL = "ENROLL"
CMD_IDENTIFY = "IDENTIFY"
CMD_DELETE = "DELETE" # Não implementado no Arduino ainda
CMD_COUNT = "COUNT"
CMD_EMPTY = "EMPTY"   # Não implementado no Arduino ainda
CMD_GET_IMAGE = "GET_IMAGE"
CMD_IMAGE_TO_TZ1 = "IMAGE_TO_TZ1"
CMD_IMAGE_TO_TZ2 = "IMAGE_TO_TZ2"
CMD_CREATE_MODEL = "CREATE_MODEL"
CMD_STORE_MODEL = "STORE_MODEL"
CMD_REMOVE_FINGER_ACK = "REMOVE_FINGER_ACK"
CMD_DOWNLOAD_TEMPLATE_B1 = "DOWNLOAD_TPL_B1"

# --- Constantes para Respostas (Arduino para Python) ---
RESP_PREFIX = "RESP:" # O Python irá remover isso ao ler
RESP_ARDUINO_READY_FOR_INIT = "ARDUINO_READY_FOR_INIT" # Arduino pronto para receber INIT_SENSOR
RESP_SENSOR_READY = "SENSOR_READY"
RESP_SENSOR_ERROR = "SENSOR_ERROR"
RESP_UNKNOWN_COMMAND = "UNKNOWN_COMMAND"
RESP_OK = "OK"
RESP_FAIL = "FAIL"
RESP_COMM_ERROR = "COMM_ERROR"
RESP_IMAGE_FAIL = "IMAGE_FAIL"
RESP_IMAGE_MESSY = "IMAGE_MESSY"
RESP_FEATURE_FAIL = "FEATURE_FAIL"
RESP_NO_FINGER = "NO_FINGER"
RESP_ENROLL_MISMATCH = "ENROLL_MISMATCH"
RESP_BAD_LOCATION = "BAD_LOCATION"
RESP_FLASH_ERROR = "FLASH_ERROR"
RESP_NOT_FOUND = "NOT_FOUND"
RESP_ASK_PLACE_FINGER = "ASK_PLACE_FINGER"
RESP_ASK_REMOVE_FINGER = "ASK_REMOVE_FINGER"
RESP_ASK_PLACE_AGAIN = "ASK_PLACE_AGAIN"
RESP_FINGER_REMOVED = "FINGER_REMOVED"
RESP_ID_FOUND = "ID_FOUND"
RESP_COUNT_RESULT = "COUNT_RESULT"
# Adicione outras respostas conforme necessário

def connect_arduino():
    global arduino_serial
    try:
        print(f"Tentando conectar ao Arduino na porta {SERIAL_PORT} a {BAUD_RATE_ARDUINO} bps...")
        arduino_serial = serial.Serial(SERIAL_PORT, BAUD_RATE_ARDUINO, timeout=0.1) 
        
        print("Aguardando Arduino reiniciar e estabilizar...")
        time.sleep(2.5) # Aumentar um pouco para garantir que o Arduino esteja no loop()
        arduino_serial.reset_input_buffer()
        arduino_serial.reset_output_buffer()
        print("Buffers de serial limpos.")

        # Enviar comando para o Arduino inicializar o sensor
        print(f"Enviando comando {CMD_INIT_SENSOR} para o Arduino...")
        if not send_to_arduino(CMD_INIT_SENSOR):
            print("Falha ao enviar comando de inicialização do sensor para o Arduino.")
            if arduino_serial and arduino_serial.is_open:
                arduino_serial.close()
            arduino_serial = None
            return False

        # Esperar pela mensagem SENSOR_READY ou SENSOR_ERROR do Arduino
        print(f"Aguardando confirmação de inicialização do sensor do Arduino (timeout: {ARDUINO_SENSOR_INIT_TIMEOUT}s)...")
        full_response_content = read_arduino_response(timeout_seconds=ARDUINO_SENSOR_INIT_TIMEOUT)

        if full_response_content and RESP_SENSOR_READY in full_response_content:
            print(f"Arduino (inicialização do sensor): {full_response_content}")
            if ",CAP:" in full_response_content:
                try:
                    capacity_str = full_response_content.split(",CAP:")[1].split(END_MARKER)[0].split('>')[0] # Limpa qualquer resquício
                    print(f"Capacidade do sensor detectada: {capacity_str}")
                except IndexError:
                    print("Não foi possível extrair a capacidade, mas o sensor está pronto.")
            print("Conexão com Arduino e inicialização do sensor bem-sucedidas!")
            return True
        elif full_response_content and RESP_SENSOR_ERROR in full_response_content:
            print(f"Erro na inicialização do sensor pelo Arduino: {full_response_content}")
            if arduino_serial and arduino_serial.is_open:
                arduino_serial.close()
            arduino_serial = None
            return False
        else:
            print(f"Falha ao receber confirmação/erro do sensor do Arduino. Resposta: '{full_response_content}'")
            if arduino_serial and arduino_serial.is_open:
                arduino_serial.close()
            arduino_serial = None
            return False

    except serial.SerialException as e:
        print(f"Erro ao abrir porta serial {SERIAL_PORT}: {e}")
        arduino_serial = None
        return False
    except Exception as e:
        print(f"Erro inesperado na conexão: {e}")
        if arduino_serial and arduino_serial.is_open:
            arduino_serial.close()
        arduino_serial = None
        return False

def send_to_arduino(command_payload):
    if arduino_serial and arduino_serial.is_open:
        full_command = f"{START_MARKER}{command_payload}{END_MARKER}\n"
        print(f"PYTHON -> ARDUINO: {full_command.strip()}")
        try:
            arduino_serial.write(full_command.encode('utf-8'))
            arduino_serial.flush() # Espera que todos os dados sejam escritos
            return True
        except serial.SerialTimeoutException:
            print("Erro: Timeout ao escrever na porta serial.")
            return False
        except Exception as e:
            print(f"Erro ao enviar para o Arduino: {e}")
            return False
    else:
        print("Erro: Arduino não conectado para enviar comando.")
        return False


def read_arduino_response(timeout_seconds=RESPONSE_TIMEOUT):
    if arduino_serial and arduino_serial.is_open:
        raw_message_inside_markers = ""
        message_started = False
        buffer_debug = "" # Para ver o que está chegando cru
        
        start_time = time.time()
        while time.time() - start_time < timeout_seconds:
            if arduino_serial.in_waiting > 0:
                try:
                    byte = arduino_serial.read(1)
                    char = byte.decode('utf-8', errors='ignore') # Usar 'ignore' para caracteres problemáticos
                    buffer_debug += char # Acumula tudo para debug se necessário

                    if char == START_MARKER:
                        raw_message_inside_markers = ""
                        message_started = True
                        continue 
                    
                    if message_started:
                        if char == END_MARKER:
                            # Mensagem completa recebida
                            print(f"ARDUINO -> PYTHON (RAW): <{raw_message_inside_markers}>")
                            if raw_message_inside_markers.startswith(RESP_PREFIX):
                                clean_response = raw_message_inside_markers[len(RESP_PREFIX):]
                                print(f"ARDUINO -> PYTHON (CONTEÚDO): {clean_response}")
                                return clean_response
                            else:
                                # Pode ser uma mensagem de debug do Arduino não formatada
                                print(f"ARDUINO -> PYTHON (SEM PREFIXO RESP): {raw_message_inside_markers}")
                                return raw_message_inside_markers 
                        
                        # Adiciona à mensagem APENAS se não for newline/cr (a menos que sejam parte válida da mensagem)
                        # O Arduino envia <RESP:MSG>\n. O \n é o terminador da linha, não da mensagem encapsulada.
                        # Esta lógica deve capturar apenas o que está entre < e >.
                        # O \n após o END_MARKER é consumido pela próxima iteração do read(1) e ignorado
                        # se message_started for resetado ou se o char for \n.
                        # Não precisamos mais do `char not in ['\r', '\n']` aqui se o Arduino
                        # só envia \n APÓS o END_MARKER.
                        raw_message_inside_markers += char

                except UnicodeDecodeError:
                    # print(f"UnicodeDecodeError: byte problemático: {byte}") # Debug
                    pass # Ignora bytes que não podem ser decodificados
                except Exception as e:
                    print(f"Erro durante leitura da serial: {e}")
                    # return f"{RESP_FAIL}:READ_ERROR_PY" # Pode ser melhor continuar tentando até o timeout
            
            time.sleep(0.005) # Pequena pausa para não sobrecarregar CPU e dar tempo para mais bytes chegarem

        # Timeout ocorreu
        print(f"ARDUINO -> PYTHON: TIMEOUT (após {timeout_seconds}s ao esperar por '{START_MARKER}...{END_MARKER}')")
        if buffer_debug: print(f"   Buffer parcial recebido durante timeout: '{buffer_debug.strip()}'")
        if raw_message_inside_markers: print(f"   Mensagem parcial entre marcadores: '{raw_message_inside_markers}'")
        return f"{RESP_FAIL}:TIMEOUT_PY" # Retorna um erro padrão de timeout do Python
    
    print("Erro: Arduino não conectado para leitura.")
    return f"{RESP_FAIL}:NO_CONNECTION_PY"


# --- Funções de interação (enroll_finger_interactive, identify_current_finger, etc.) ---
# As funções de interação (enroll, identify, count) permanecem praticamente as mesmas,
# pois a lógica de enviar comandos e esperar respostas específicas é mantida.
# Apenas certifique-se que as strings de resposta esperadas (RESP_OK:IMAGE1_TAKEN, etc.)
# correspondem exatamente ao que o Arduino envia.

def enroll_finger_interactive():
    if not arduino_serial:
        print("Arduino não conectado.")
        return

    # Obter ID do usuário
    user_id_str = ""
    user_id = -1
    while True:
        try:
            user_id_str = input("Digite o ID para o novo cadastro (1-127, ou 0 para cancelar): ")
            user_id = int(user_id_str)
            # A capacidade real será verificada pelo Arduino, mas uma checagem básica aqui é boa.
            if 0 <= user_id <= 127: 
                break
            else:
                print("ID inválido. Deve ser entre 1 e 127 (ou 0 para cancelar).")
        except ValueError:
            print("Por favor, digite um número.")
    
    if user_id == 0:
        print("Cadastro cancelado.")
        return

    print(f"\nIniciando cadastro para ID: {user_id}")
    # O comando ENROLL agora inclui o ID: <ENROLL,ID>
    if not send_to_arduino(f"{CMD_ENROLL},{user_id}"): return

    # Fluxo de cadastro:
    # 1. Arduino pede para colocar o dedo
    response = read_arduino_response()
    if RESP_ASK_PLACE_FINGER not in response:
        print(f"Falha no cadastro (etapa 1 - esperar {RESP_ASK_PLACE_FINGER}): Resposta foi '{response}'")
        return
    input("Coloque o dedo no sensor e pressione Enter...")
    # Python agora envia o comando para o Arduino prosseguir
    if not send_to_arduino(CMD_GET_IMAGE): return 

    # 2. Arduino confirma imagem 1
    response = read_arduino_response() 
    if f"{RESP_OK}:IMAGE1_TAKEN" not in response: # Arduino envia OK:IMAGE1_TAKEN
        print(f"Falha ao capturar imagem 1: {response}")
        return
    
    # Python envia comando para converter imagem 1
    if not send_to_arduino(CMD_IMAGE_TO_TZ1): return
    response = read_arduino_response() 
    if f"{RESP_OK}:CONVERT1_DONE" not in response: # Arduino envia OK:CONVERT1_DONE
        print(f"Falha ao converter imagem 1: {response}")
        return

    # 3. Arduino pede para remover o dedo
    response = read_arduino_response() # Arduino envia ASK_REMOVE_FINGER automaticamente
    if RESP_ASK_REMOVE_FINGER not in response:
        print(f"Falha no cadastro (etapa 2 - esperar {RESP_ASK_REMOVE_FINGER}): {response}")
        return
    input("Retire o dedo do sensor e pressione Enter...")
    # Python envia confirmação de que usuário foi notificado para remover
    if not send_to_arduino(CMD_REMOVE_FINGER_ACK): return 

    # 4. Arduino confirma dedo removido
    response = read_arduino_response() # Arduino envia FINGER_REMOVED
    if RESP_FINGER_REMOVED not in response:
        print(f"Falha ao confirmar remoção do dedo: {response}")
        return

    # 5. Arduino pede para colocar o dedo novamente
    response = read_arduino_response() # Arduino envia ASK_PLACE_AGAIN
    if RESP_ASK_PLACE_AGAIN not in response:
        print(f"Falha no cadastro (etapa 3 - esperar {RESP_ASK_PLACE_AGAIN}): {response}")
        return
    input("Coloque o MESMO dedo novamente no sensor e pressione Enter...")
    if not send_to_arduino(CMD_GET_IMAGE): return

    # 6. Arduino confirma imagem 2 e conversão 2
    response = read_arduino_response() 
    if f"{RESP_OK}:IMAGE2_TAKEN" not in response: # Arduino envia OK:IMAGE2_TAKEN
        print(f"Falha ao capturar imagem 2: {response}")
        return

    if not send_to_arduino(CMD_IMAGE_TO_TZ2): return
    response = read_arduino_response() 
    if f"{RESP_OK}:CONVERT2_DONE" not in response: # Arduino envia OK:CONVERT2_DONE
        print(f"Falha ao converter imagem 2: {response}")
        return

    # 7. Arduino cria modelo (Python envia comando para criar)
    if not send_to_arduino(CMD_CREATE_MODEL): return
    response = read_arduino_response() 
    if f"{RESP_OK}:MODEL_CREATED" not in response: # Arduino envia OK:MODEL_CREATED
        if RESP_ENROLL_MISMATCH in response: # Checa se o erro específico foi mismatch
            print("As digitais não correspondem. Tente novamente.")
        else:
            print(f"Falha ao criar modelo: {response}")
        return
    full_template_bytes = None # Inicializa
    choice_download = input("Deseja fazer o download do template para o PC? (s/N): ").strip().lower()
    
    if choice_download == 's':
        print("Solicitando download do template do sensor (do CharBuffer1)...")
        if send_to_arduino(CMD_DOWNLOAD_TEMPLATE_B1): # Comando para o Arduino
            template_hex_parts = []
            bytes_downloaded_count_from_arduino = 0
            transfer_successful = False

            # Loop para receber os chunks do template
            # A primeira resposta deve ser TEMPLATE_UPLOAD_CMD_ACKNOWLEDGED
            ack_response = read_arduino_response(timeout_seconds=5)
            if RESP_OK in ack_response and "TEMPLATE_UPLOAD_CMD_ACKNOWLEDGED" in ack_response:
                print("Arduino confirmou início da transferência do template...")
                
                while True: # Loop para os chunks e mensagem final
                    arduino_reply = read_arduino_response(timeout_seconds=10) # Timeout para cada chunk/msg final

                    if arduino_reply.startswith("TEMPLATE_CHUNK:"):
                        hex_chunk = arduino_reply.split(":", 1)[1]
                        template_hex_parts.append(hex_chunk)
                        # print(f"   Recebido chunk: {len(hex_chunk)//2} bytes")
                    elif RESP_OK in arduino_reply and "TEMPLATE_DOWNLOAD_COMPLETE" in arduino_reply:
                        try:
                            bytes_downloaded_count_from_arduino = int(arduino_reply.split(":")[-1])
                            print(f"Arduino reportou download completo: {bytes_downloaded_count_from_arduino} bytes.")
                            full_hex_string = "".join(template_hex_parts)
                            full_template_bytes = bytes.fromhex(full_hex_string)
                            if len(full_template_bytes) == bytes_downloaded_count_from_arduino:
                                print(f"Template reconstruído com sucesso ({len(full_template_bytes)} bytes).")
                                transfer_successful = True
                            else:
                                print(f"ERRO: Discrepância no tamanho! Python reconstruiu {len(full_template_bytes)}, Arduino reportou {bytes_downloaded_count_from_arduino}")
                        except ValueError as e:
                            print(f"Erro ao converter template de HEX para bytes: {e}")
                        except Exception as e:
                            print(f"Erro inesperado ao finalizar template: {e}")
                        break # Sai do loop de chunks
                    elif RESP_FAIL in arduino_reply:
                        print(f"Falha no download do template reportada pelo Arduino: {arduino_reply}")
                        break # Sai do loop de chunks
                    elif "TIMEOUT_PY" in arduino_reply:
                        print("Timeout esperando mais dados do template do Arduino.")
                        break # Sai do loop de chunks
                    else:
                        print(f"Resposta inesperada durante download do template: {arduino_reply}")
                        # Continuar esperando pode ser arriscado aqui
            else:
                print(f"Arduino não confirmou o início do download do template. Resposta: {ack_response}")
            
            if transfer_successful and full_template_bytes:
                print(f"Template ({len(full_template_bytes)} bytes) pronto para ser salvo no banco de dados.")
                # Aqui você chamaria sua função para salvar no banco de dados:
                # db_save_template(user_id, full_template_bytes)
                print(f"Template para ID {user_id} salvo externamente!")
            else:
                print("Download do template falhou ou template inválido.")
        else:
            print("Falha ao enviar comando de download para o Arduino.")

    # 8. Arduino armazena modelo (Python envia comando para armazenar)
    choice_store_sensor = input(f"Deseja armazenar este modelo no flash do sensor com ID {user_id}? (S/n): ").strip().lower()
    if choice_store_sensor != 'n':
        print(f"Solicitando armazenamento do modelo no flash do sensor para ID {user_id}...")
        if not send_to_arduino(CMD_STORE_MODEL): return # Arduino espera este comando
        response = read_arduino_response() 
        if f"{RESP_OK}:STORED:{user_id}" in response:
            print(f"Digital armazenada com sucesso no flash do sensor para o ID {user_id}!")
        else:
            print(f"Falha ao armazenar modelo no flash do sensor: {response}")
    else:
        print("Modelo não será armazenado no flash do sensor.")



def identify_current_finger():
    if not arduino_serial:
        print("Arduino não conectado.")
        return

    print("\nIniciando identificação...")
    # Python envia o comando principal para iniciar a identificação
    if not send_to_arduino(CMD_IDENTIFY): return

    # Arduino responde pedindo para colocar o dedo
    response = read_arduino_response()
    if RESP_ASK_PLACE_FINGER not in response:
        print(f"Erro no fluxo de identificação (esperava {RESP_ASK_PLACE_FINGER}): {response}")
        return
    
    input("Coloque o dedo no sensor para identificação e pressione Enter...")
    # Python envia comando para Arduino capturar a imagem
    if not send_to_arduino(CMD_GET_IMAGE): return

    # Arduino responde se a imagem foi capturada ou não
    response = read_arduino_response() 
    if f"{RESP_OK}:IMAGE_TAKEN" not in response: # Arduino envia OK:IMAGE_TAKEN
        if RESP_NO_FINGER in response:
            print("Nenhum dedo detectado.")
        else:
            print(f"Falha ao capturar imagem para identificação: {response}")
        return

    # Python envia comando para Arduino converter a imagem
    if not send_to_arduino(CMD_IMAGE_TO_TZ1): return # Usar buffer 1
    response = read_arduino_response() 
    if f"{RESP_OK}:CONVERT_DONE" not in response: # Arduino envia OK:CONVERT_DONE
        print(f"Falha ao converter imagem para identificação: {response}")
        return
    
    # Arduino agora faz a busca automaticamente e envia o resultado.
    # Python apenas espera pela resposta final da busca.
    print("Aguardando resultado da busca do Arduino...")
    response = read_arduino_response(timeout_seconds=10) # A busca pode demorar um pouco
    
    if RESP_ID_FOUND in response: # Ex: ID_FOUND:12,CONFIDENCE:150
        try:
            # A resposta já vem limpa do RESP_PREFIX por read_arduino_response
            # Exemplo: "ID_FOUND:12,CONFIDENCE:150"
            parts_id = response.split(f"{RESP_ID_FOUND}:")[1] # "12,CONFIDENCE:150"
            sensor_id = parts_id.split(",CONFIDENCE:")[0]    # "12"
            confidence = parts_id.split(",CONFIDENCE:")[1]   # "150"
            print(f"Digital encontrada! ID do Sensor: {sensor_id}, Confiança: {confidence}")
        except IndexError:
            print(f"Resposta de ID_FOUND mal formatada do Arduino: {response}")
    elif RESP_NOT_FOUND in response:
        print("Digital não encontrada no banco de dados do sensor.")
    elif RESP_FAIL in response: # Se houve alguma falha genérica no processo
        print(f"Falha na busca da digital: {response}")
    else:
        print(f"Resposta inesperada do Arduino durante a busca: {response}")


def get_sensor_template_count():
    if not arduino_serial:
        print("Arduino não conectado.")
        return
    print("\nSolicitando contagem de templates...")
    if not send_to_arduino(CMD_COUNT): return
    
    response = read_arduino_response() # Espera COUNT_RESULT:N
    if RESP_COUNT_RESULT in response: # Ex: COUNT_RESULT:5
        try:
            count = response.split(f"{RESP_COUNT_RESULT}:")[1] 
            print(f"Templates armazenados no sensor: {count}")
        except IndexError:
            print(f"Resposta de contagem mal formatada: {response}")
    elif RESP_FAIL in response:
        print(f"Falha ao obter contagem: {response}")
    else:
        print(f"Resposta inesperada para contagem de templates: {response}")


def main_menu():
    global arduino_serial
    if not connect_arduino(): # Tenta conectar e inicializar o sensor
        print("Encerrando programa devido à falha na conexão inicial com o Arduino/sensor.")
        return

    while True:
        print("\n--- Menu Principal ---")
        print("1. Cadastrar nova digital")
        print("2. Identificar digital")
        print("3. Obter contagem de templates no sensor")
        print("4. Sair")
        choice = input("Escolha uma opção: ").strip()

        if choice == '1':
            enroll_finger_interactive()
        elif choice == '2':
            identify_current_finger()
        elif choice == '3':
            get_sensor_template_count()
        elif choice == '4':
            print("Saindo...")
            break
        else:
            print("Opção inválida. Tente novamente.")

    if arduino_serial and arduino_serial.is_open:
        arduino_serial.close()
        print("Porta serial fechada.")

if __name__ == '__main__':
    main_menu()