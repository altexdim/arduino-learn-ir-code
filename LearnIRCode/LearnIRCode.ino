/*
Пример программы для управления светодиодом с ИК-пульта.
https://github.com/altexdim/arduino-learn-ir-code

Коды команд кнопок пульта сохраняются в памяти Arduino после сброса.
Для обучения устройства новой команде (кнопке пульта) используется 
тактовая кнопка на Arduino.

У устройства два режима работы.

1) В основном режиме работы устройство ждёт команд с ИК-пульта. 
Если пришла команда, то сравнивает её с тем значением, которое храниться в памяти.
Если пришла команда, которая совападает со значением из памяти, то тогда рабочий светодиод
переключается - загорается или гаснет - от каждого нажатия кнопки ИК-пульта.

2) Второй режим - режим обучения. В этот режим устройство переводится нажатием 
и удержанием тактовой кнопки на Arduino. Если включен этот режим, то постоянно горит второй
светодиод, предназначенный только для отображения режима работы. В этом режиме устройство
ожидает от ИК-пульта нажатия любой кнопки. Если на ИК-пульте нажать на кнопку, то полученная
команда сохранится в постоянной памяти Arduino, и устройство будет переведено обратно в основной
режим работы. Если отпустить тактовую кнопку до команды с ИК-пульта, то устройство вернётся обратно
в основной режим работы, при этом никаких изменений с прежней командой не произойдёт.
Если устройство научилось новой команде, тогда рабочий светодиод можно переключать только 
с помощью этой новой команды, а предыдущая команда стирается.

License: MIT http://opensource.org/licenses/mit-license.php
Copyright (c) 2014 Ananyev Dmitry
*/

/*
Подключаем библиотеку IRremote https://github.com/shirriff/Arduino-IRremote (LGPL)
Install: 
  cd C:\Users\<USERNAME>\Documents\Arduino\libraries
  git clone https://github.com/shirriff/Arduino-IRremote.git IRremote
*/
#include <IRremote.h>

// Стандартная библиотека для работы с EEPROM. Нужна для сохранения кодов кнопок пульта между перезагрузками.
#include <EEPROM.h>

// Дополнительные функции для перевода long -> int и назад.
#define makeLong(hi, low)  (((long) hi) << 16 | (low))
#define highWord(w) ((w) >> 16)
#define lowWord(w) ((w) & 0xffff)

// Режим отладки
// В этом режиме отладочная информация выводится в последовательный порт.
#define DEBUG 0

/*
  Пин, к которому подключен ИК приёмник. 
  У ИК приёмника один информационный выход, подключается напрямую к этому пину. Ещё у ИК приёмника обычно есть
  два контакта - Vcc и Ground. Они соответственно подключаются к питанию и земле.
  Используется ИК приёмник TSOP22 на 38 кГц.
*/
#define RECV_PIN 8
/*
  Пин, к которому подключена тактовая кнопка включения режима обучения командам пульта.
  Другим концом тактовая кнопка подключается к земле. Так что при замыкании кнопки на этом пине будет низкий уровень.
  Так же к пину подключен подтягивающий резистор 10кОм на Vcc.
  Используется обычная двухконтактная кнопка нормально-разомкнутая.
*/
#define LEARN_BUTTON_PIN 10
/*
  Пин, к которому подключен светодиод, показывающий, что включен режим обучения.
  Светодиод другим концом подключен к земле через резистор 200 Ом. 
  Загорается от высокого уровня на пине.
  Используется обычный светодиод 20 мА на 2.3 В.
*/
#define LEARN_LED_PIN 12
/*
  Пин, который управляется командой с пульта. Здесь используется светодиод. По кнопке пульта он включается и выключается.
  Светодиод другим концом подключен к земле через резистор 200 Ом. 
  Загорается от высокого уровня на пине.
  Используется обычный светодиод 20 мА на 2.3 В.
  Вместо светодиода можно подключить любую нагрузку.
*/
#define WORK_LED_PIN 7
// адрес, по которому сохраняются команды в постоянной памяти. Используется 4 байта. Указывается начальный адрес.
#define IR_CODE_DATA_ADDRESS 0

// состояние кнопки режима обучения - нажата(1) или нет(0)
boolean learnButtonState = 0;
// режим обучения - включен(1) или нет(0)
boolean learnModeEnabled = 0;
// управлеямый с пульта светодиод - включен(1) или нет(0)
boolean workLedState = 0;
// код команды пульта, по которому происходит включение управляемого светодиода
long irCodeData = 0;

// объект приёмника ИК-команд
IRrecv irrecv(RECV_PIN);
// объект декодированных результатов из приёмника ИК-команд
decode_results irresult;

// Стандартная процедура первоначальной настройки Arduino при запуске
void setup() {
  #if DEBUG
  Serial.begin(9600);
  #endif
  
  pinMode(LEARN_LED_PIN, OUTPUT);
  pinMode(WORK_LED_PIN, OUTPUT);
  irCodeData = loadFromEeprom(IR_CODE_DATA_ADDRESS); // Загрузка кода команды пульта из постоянной памяти
  irrecv.enableIRIn(); // Запуск приёмника
}

// Основаная процедура цикла работы Arduino
void loop() {
  processLearnButton();
  processLearnLed();
  processWorkLed();
  processIr();  
}

// Процедура обработки нажатий на ИК-пульте
void processIr() {
  if (irrecv.decode(&irresult)) { // Если распознана команда с пульта
    long value = irresult.value;

    #if DEBUG
    Serial.print("Raw: ");
    Serial.println(value, HEX);
    #endif  

    if (0xFFFFFFFF != value) { // Если пришла команда 0xFFFFFFFF, то её нужно игнорировать
      if (irresult.decode_type == RC6) { // Если протокол пульта RC6 (и возможно RC5), то нужно избавиться от toggle бита (0x8000)
        value &= ~0x8000LL;
      }
      processCode(value);
    }

    irrecv.resume(); // Прочитать следующее значение команды с ИК-пульта
  }
}

/*
  Функция возвращает состояние кнопки, с программной защитой от дребезга контактов.
 
  @param int pin - Номер пина, к которому подключена кнопка.
  @param boolean pullUp - Подтянута ли кнопка к Vcc или к земле.
    true = кнопка подтянута к Vcc
    false = кнопка подтянута к земле
  @return int - Состояние кнопки, нажата или нет.
    0 = кнопка отпущена
    1 = кнопка нажата
    -1 = неопределённое состояние, дребезг контактов
*/
int getButtonState(int pin, boolean pullUp) {
  int pinState = digitalRead(pin);

  for (int i = 0; i < 3; i++) {
    delay(10);
    if (digitalRead(pin) != pinState) {
      return -1;
    }
  }
  
  /*
    Если кнопка с подтягивающим резистором, то нажатие на кнопку инвертировано относительно уровня сигнала на пине.
    Кнопка нажата - уровень на пине низкий. Кнопка отжата - уровень на пине высокий.
  */
  return pullUp ^ pinState;
}

// Процедура обработки нажатий на тактовую кнопку Arduino для запуска режима обучения командам пульта
void processLearnButton() {
  int buttonState = getButtonState(LEARN_BUTTON_PIN, true);
  if (buttonState == -1) { // Неопределённое состояние кнопки
    return;
  }
  if (buttonState == learnButtonState) { // Состояние кнопки не поменялось
    return;
  }

  learnButtonState = buttonState;
  learnModeEnabled = buttonState;
}

// Процедура управления светодиодом, показывающим режим обучения. 
// Если светодиод горит - значит работает режим обучения.
void processLearnLed() {
  digitalWrite(LEARN_LED_PIN, learnModeEnabled);
}

// Процедура управления светодиодом с пульта. 
// Кнопка с пульта включает или отключает светодиод.
void processWorkLed() {
  digitalWrite(WORK_LED_PIN, workLedState);
}

// Процедура обработки кода пульта
void processCode(long value) {
    #if DEBUG
    Serial.println(value, HEX); 
    #endif  
    
    if (learnModeEnabled) { // Если включен режим обучения
      irCodeData = value; // Запомним код
      saveToEeprom(irCodeData, IR_CODE_DATA_ADDRESS); // Сохраним его в постоянной памяти
      learnModeEnabled = false; // Выключим режим обучения
      delay(100); // Задержка, чтобы не включить случайно рабочий светодиод во время режима обучения, если пульт повторяет команды слишком быстро
    } else if (irCodeData == value) { // Если не включен режим обучения, и если команда совпала с той, которой обучился наш Arduino
      workLedState = !workLedState; // То меняем состояние рабочего светодиода
      delay(100); // Задержка, чтобы не было ложных срабатываний рабочего светодиода, если пульт повторяет команды слишком быстро
    }
}

// Процедура сохранения 4-байтного целого в постоянную память
void saveToEeprom(long value, int address) {
  int data[2];
  data[0] = highWord(value);
  data[1] = lowWord(value);
  for (int i = 0; i < 2; i++){
    EEPROM.write(address + i * 2, highByte(data[i]));
    EEPROM.write(address + i * 2 + 1, lowByte(data[i]));
  }  

  #if DEBUG
  Serial.print("Saved to Eeprom value = "); 
  Serial.print(value); 
  Serial.print(" address = "); 
  Serial.println(address); 
  #endif      
}

// Процедура чтения 4-байтного целого из постоянной памяти
long loadFromEeprom(int address) {
  byte data[4];
  for (int i = 0; i < 4; i++){
    data[i] = EEPROM.read(address + i);
  }
  int high = word(data[0], data[1]);
  int low = word(data[2], data[3]);
  long result = makeLong(high, low);

  #if DEBUG
  Serial.print("Loaded from Eeprom value = "); 
  Serial.print(result); 
  Serial.print(" address = "); 
  Serial.println(address); 
  #endif      

  return result;
}
