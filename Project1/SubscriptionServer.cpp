#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>  
#include <ws2tcpip.h>   
#include <windows.h>    
#include <winhttp.h>
#include <string>
#include <iostream>
#include <vector>
#include <sstream>
#include <iomanip>
#include <thread>
#include <mutex>
#include <set>
#pragma comment(lib, "ws2_32.lib")  // Лінкування з бібліотекою Winsock
#pragma comment(lib, "winhttp.lib") // Лінкування з бібліотекою для HTTP




std::mutex clientMutex; //М'ютекс забезпечує синхронний доступ до спільних ресурсів між потоками.

// Структура для інформації про клієнта
struct ClientInfo {
    SOCKET socket;       // Сокет клієнта
    std::string type;    // Тип клієнта (напр., "HTTP", "FTP")
    bool operator<(const ClientInfo& other) const {
        return socket < other.socket; // Порівняння за сокетами
    }
    bool operator==(const ClientInfo& other) const {
        return socket == other.socket && type == other.type; // Порівняння за сокетом і типом
    }
};

SOCKET accept(SOCKET s, struct sockaddr* addr, int* addrlen);



std::set<ClientInfo> clients;

// ------------------- HTTP-запити -------------------
std::string HttpGetRequest(const std::wstring& server, const std::wstring& url) { //ім'я сервера , шлях до ресурсу на сервері
    HINTERNET hSession = NULL, hConnect = NULL, hRequest = NULL; // Дескриптори для сесії, підключення та запиту 
    std::string response; // Рядок для збереження відповіді сервера

    // Ініціалізація WinHTTP: відкривається сесія для виконання HTTP-запитів
    hSession = WinHttpOpen(
        L"MyApp/1.0", // Ім'я агента (User-Agent) клієнта
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, // Використання системного проксі (якщо є)
        WINHTTP_NO_PROXY_NAME, // Не використовується спеціальний проксі-сервер
        WINHTTP_NO_PROXY_BYPASS, // Не обходити проксі для жодних адрес
        0 // Параметри (за замовчуванням)
    );

    if (hSession) { // Перевіряємо, чи вдалося створити сесію
        hConnect = WinHttpConnect(
            hSession, // Ідентифікатор відкритої сесії
            server.c_str(), // Ім'я сервера
            INTERNET_DEFAULT_HTTPS_PORT, // Порт HTTPS (443)
            0 // Резервний параметр
        );
        if (hConnect) { // Перевіряємо, чи вдалося підключитися до сервера
            hRequest = WinHttpOpenRequest(
                hConnect, // Ідентифікатор підключення
                L"GET", // Тип запиту: GET
                url.c_str(), // Шлях до ресурсу на сервері
                NULL, // Версія протоколу (за замовчуванням)
                WINHTTP_NO_REFERER, // Відсутній заголовок Referer
                WINHTTP_DEFAULT_ACCEPT_TYPES, // Стандартні типи контенту, які приймаються
                WINHTTP_FLAG_SECURE // Використовується безпечне з'єднання (HTTPS)
            );
            if (hRequest) { // Перевіряємо, чи вдалося створити запит
                BOOL sent = WinHttpSendRequest(
                    hRequest, // Ідентифікатор запиту
                    WINHTTP_NO_ADDITIONAL_HEADERS, // Додаткові заголовки не передаються
                    0, // Довжина заголовків (0, якщо їх немає)
                    WINHTTP_NO_REQUEST_DATA, // Додаткові дані в запиті (відсутні)
                    0, // Довжина даних запиту
                    0, // Загальна довжина запиту (0, якщо відсутня)
                    0 // Резервний параметр
                );
                // Перевіряємо, чи вдалося відправити запит і отримати відповідь
                if (sent && WinHttpReceiveResponse(hRequest, NULL)) {
                    DWORD bytesRead = 0; // Кількість байтів, прочитаних із відповіді
                    do {
                        char buffer[4096]; // Буфер для читання даних (4 КБ)
                        WinHttpReadData(
                            hRequest, // Ідентифікатор запиту
                            buffer, // Буфер для зберігання даних
                            sizeof(buffer), // Розмір буфера
                            &bytesRead // Фактична кількість прочитаних байтів
                        );
                        // Додаємо прочитані дані до відповіді, якщо щось прочитано
                        if (bytesRead > 0) {
                            response.append(buffer, bytesRead);
                        }
                    } while (bytesRead > 0); // Повторюємо, поки є дані для читання
                }
            }
        }
    }

    // Закриття відкритих дескрипторів для запобігання витокам пам'яті
    if (hRequest) WinHttpCloseHandle(hRequest); // Закриваємо дескриптор запиту
    if (hConnect) WinHttpCloseHandle(hConnect); // Закриваємо дескриптор підключення
    if (hSession) WinHttpCloseHandle(hSession); // Закриваємо дескриптор сесії
    return response;
}

// ------------------- Парсинг JSON -------------------
// Функція для пошуку значення ключа у JSON-рядку
// json - JSON-рядок, в якому виконується пошук
// key - ключ, значення якого потрібно знайти
std::string FindJsonValue(const std::string& json, const std::string& key) {
    // Знаходимо перше входження ключа в JSON-рядку
    // Додаємо лапки навколо ключа, щоб уникнути збігів з іншими подібними словами
    size_t start = json.find("\"" + key + "\":");
    // Якщо ключ не знайдено, повертаємо порожній рядок
    if (start == std::string::npos) return "";
    // Знаходимо двокрапку (":") після ключа
    start = json.find(":", start) + 1;
    // Шукаємо кінець значення, припускаючи, що воно закінчується комою (",")
    size_t end = json.find(",", start);
    // Якщо значення обгорнуте в лапки, змінюємо межі для їх ігнорування
    if (json[start] == '"') {
        start++; // Пропускаємо початкову лапку
        end = json.find("\"", start); // Знаходимо кінцеву лапку
    }

    // Витягуємо підрядок, який відповідає значенню ключа
    return json.substr(start, end - start);
}




// Функція для отримання інформації про погоду через API OpenWeatherMap
std::vector<std::string> GetWeatherInfo() {
    std::vector<std::string> weatherInfo; // Масив для зберігання погодної інформації
    // Адреса сервера та шлях до ресурсу з параметрами запиту
    std::wstring server = L"api.openweathermap.org"; // Домен API OpenWeatherMap
    std::wstring url = L"/data/2.5/weather?q=Lviv&appid=e4a2c38dfe1b15e27796cb1e4b559c35";  // Місто - Львів, ключ API

    // Виконуємо HTTP-запит до сервера
    std::string response = HttpGetRequest(server, url);

    // Перевірка, чи отримано відповідь
    if (!response.empty()) {
        try {
            // Парсимо дані з JSON-відповіді
            std::string temp = FindJsonValue(response, "temp");         // Температура
            std::string pressure = FindJsonValue(response, "pressure"); // Тиск
            std::string humidity = FindJsonValue(response, "humidity"); // Вологість
            std::string weatherType = FindJsonValue(response, "main");  // Тип погоди (ясно, хмарно тощо)


            // Конвертуємо температуру з Кельвінів у Цельсії
            double tempCelsius = std::stod(temp) - 273.15;

            // Форматуємо температуру з двома знаками після коми
            std::ostringstream tempStream;
            tempStream << std::fixed << std::setprecision(2) << tempCelsius << "°C";

            // Додаємо дані до списку погодної інформації
            weatherInfo.push_back(tempStream.str());     // Температура у Цельсіях
            weatherInfo.push_back(pressure + " hPa");    // Тиск у гПа
            weatherInfo.push_back(humidity + "%");       // Вологість у відсотках
            weatherInfo.push_back(weatherType);          // Тип погоди
        }
        // У разі помилки (наприклад, неправильний формат відповіді)
        catch (...) {
            weatherInfo = { "-", "-", "-", "Error fetching data" };
        }
    }
    else {
        // Якщо відповідь сервера відсутня
        weatherInfo = { "-", "-", "-", "No response from server" };
    }
    // Повертаємо масив з інформацією про погоду
    return weatherInfo;
}

// Функція для отримання курсів валют через API Національного банку України
std::vector<std::string> GetCurrencyInfo() {
    std::vector<std::string> currencyInfo;// Вектор для зберігання курсів валют

    // Адреса сервера та шлях до API
    std::wstring server = L"bank.gov.ua"; // Домен серверу Національного банку України
    std::wstring url = L"/NBUStatService/v1/statdirectory/exchange?json";

    // Виконуємо HTTP-запит до сервера
    std::string response = HttpGetRequest(server, url);

    // Перевіряємо, чи отримали відповідь від сервера
    if (!response.empty()) {
        // Змінні для зберігання курсів валют
        std::string usdRate, eurRate;

        // Пошук позиції, де вказано курс долара США (USD) у відповіді
        size_t usdPos = response.find("\"cc\":\"USD\""); // Знаходимо JSON-елемент для валюти USD
        if (usdPos != std::string::npos) {
            // Якщо знайдено елемент USD, витягуємо його курс за допомогою функції FindJsonValue
            usdRate = FindJsonValue(response.substr(usdPos - 50, 100), "rate");
        }
        // Пошук позиції, де вказано курс євро (EUR) у відповіді
        size_t eurPos = response.find("\"cc\":\"EUR\""); // Знаходимо JSON-елемент для валюти EUR
        if (eurPos != std::string::npos) {
            // Якщо знайдено елемент EUR, витягуємо його курс за допомогою функції FindJsonValue
            eurRate = FindJsonValue(response.substr(eurPos - 50, 100), "rate");
        }

        // Додаємо курси валют у вектор
        currencyInfo.push_back(usdRate + " UAH");
        currencyInfo.push_back(eurRate + " UAH");
    }
    else {
        // Якщо відповідь від сервера відсутня, додаємо дефолтні значення
        currencyInfo = { "-", "-" };
    }

    return currencyInfo;
}

// Функція для отримання інформації про акції тесли
std::vector<std::string> GetStockInfo() {
    std::vector<std::string> stockInfo; // Вектор для зберігання інформації про акції
    // Домен сервера (адреса API Finnhub)
    std::wstring server = L"finnhub.io";
    // Шлях до ресурсу, що включає символ TSLA (Tesla) та токен авторизації
    std::wstring url = L"/api/v1/quote?symbol=TSLA&token=ct2tn8pr01qkff70vo3gct2tn8pr01qkff70vo40";
    // Виконання HTTP-запиту до вказаного ресурсу
    std::string response = HttpGetRequest(server, url);

    // Перевірка, чи була отримана відповідь
    if (!response.empty()) {
        try {
            // Парсинг JSON-відповіді для отримання поточної ціни акцій
            std::string currentPrice = FindJsonValue(response, "c");
            // Парсинг для отримання найвищої ціни за день
            std::string highPrice = FindJsonValue(response, "h");
            // Парсинг для отримання найнижчої ціни за день
            std::string lowPrice = FindJsonValue(response, "l");

            // Додавання отриманих даних до вектора
            stockInfo.push_back("$" + currentPrice);  // Поточна ціна
            stockInfo.push_back("$" + highPrice);    // Найвища ціна
            stockInfo.push_back("$" + lowPrice);     // Найнижча ціна
        }
        catch (...) {
            // У разі виникнення помилки повертаємо дефолтні значення
            stockInfo = { "-", "-", "-" };
        }
    }
    else {
        // Якщо відповідь сервера була порожньою, встановлюємо дефолтні значення
        stockInfo = { "-", "-", "-" };
    }

    return stockInfo;
}



// Оновлення даних 
void BroadcastToClients(const std::string& message, const std::string& subscriptionType) {
    // Захист списку клієнтів за допомогою м'ютекса
    std::lock_guard<std::mutex> lock(clientMutex);
    // Проходимо по списку клієнтів
    for (auto it = clients.begin(); it != clients.end(); ) {
        // Перевіряємо, чи підписаний клієнт на вказаний тип
        if (it->type == subscriptionType) {
            // Надсилаємо повідомлення через сокет
            int result = send(it->socket, message.c_str(), static_cast<int>(message.size()), 0);

            // Якщо виникла помилка або клієнт відключився
            if (result == SOCKET_ERROR) {
                closesocket(it->socket); // Закриваємо сокет
                it = clients.erase(it); // Видаляємо клієнта зі списку
                continue;                // Переходимо до наступного клієнта
            }
        }
        ++it;// Перехід до наступного клієнта
    }
}


//Служба оновлення погоди
void WeatherUpdateService() {
    // Бескінечний цикл для постійного оновлення інформації про погоду
    while (true) {
        // Отримуємо актуальну інформацію про погоду
        auto weatherInfo = GetWeatherInfo();
        std::ostringstream oss;
        oss << "Weather in Lviv:\n"
            << weatherInfo[0] << "°\n"
            << weatherInfo[1] << "\n"
            << weatherInfo[2] << "\n"
            << weatherInfo[3] << "\n";
        // Надсилаємо повідомлення всім клієнтам, підписаним на тип "weather"
        BroadcastToClients(oss.str(), "weather");
        // Затримка на 1 годину (3600 секунд) перед наступним оновленням
        std::this_thread::sleep_for(std::chrono::seconds(3600)); // Затримка для тестування
    }
}


// Служба оновлення валютних курсів
void CurrencyUpdateService() {
    // Бескінечний цикл для постійного оновлення валютних курсів
    while (true) {
        // Отримуємо актуальні курси валют
        auto currencyInfo = GetCurrencyInfo();
        std::ostringstream oss;
        oss << "Currency Rates:\n"
            << currencyInfo[0] << "\n"
            << currencyInfo[1] << "\n";
        // Надсилаємо повідомлення всім клієнтам, підписаним на тип "currency"
        BroadcastToClients(oss.str(), "currency");
        // Затримка на 24 години перед наступним оновленням
        std::this_thread::sleep_for(std::chrono::hours(24)); // Затримка для тестування
    }
}


// Служба оновлення інформації про акції
void StockUpdateService() {
    // Бескінечний цикл для постійного оновлення інформації про акції
    while (true) {
        // Отримуємо актуальну інформацію про акції Tesla Inc.
        auto stockInfo = GetStockInfo();

        std::ostringstream oss;
        oss << "Tesla Inc.(TSLA):\n"
            << stockInfo[0] << "\n"
            << stockInfo[1] << "\n"
            << stockInfo[2] << "\n";
        // Надсилаємо повідомлення всім клієнтам, підписаним на тип "stock"
        BroadcastToClients(oss.str(), "stock");
        // Затримка на 1 хвилину (60 секунд) перед наступним оновленням
        std::this_thread::sleep_for(std::chrono::seconds(60)); // Затримка для тестування
    }
}

// ------------------- Основний сервер -------------------
void HandleClient(SOCKET clientSocket) {
    char buffer[256];
    int bytesRead;

    // Читання даних від клієнта
    bytesRead = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);

    if (bytesRead > 0) {
        buffer[bytesRead] = '\0';
        std::string command(buffer);

        std::lock_guard<std::mutex> lock(clientMutex);

        // Видалення клієнта з попередньої підписки, якщо вона існує
        auto it = std::find(clients.begin(), clients.end(), ClientInfo{ clientSocket, "" });
        if (it != clients.end()) {
            clients.erase(it); // Видаляємо з попереднього типу
        }

        // Розділення підписок, якщо є кома
        std::stringstream ss(command);
        std::string singleCommand;
        while (std::getline(ss, singleCommand, ',')) {
            if (singleCommand == "subscribe:weather") {
                clients.insert(ClientInfo{ clientSocket, "weather" });
                std::cout << "Client subscribed to weather updates." << std::endl;

                auto weatherInfo = GetWeatherInfo();
                std::ostringstream oss;
                oss << "Weather in Lviv:\n"
                    << weatherInfo[0] << "\n"
                    << weatherInfo[1] << "\n"
                    << weatherInfo[2] << "\n"
                    << weatherInfo[3] << "\n";
                send(clientSocket, oss.str().c_str(), oss.str().size(), 0);
            }
            else if (singleCommand == "subscribe:currency") {
                clients.insert(ClientInfo{ clientSocket, "currency" });
                std::cout << "Client subscribed to currency updates." << std::endl;

                auto currencyInfo = GetCurrencyInfo();
                std::ostringstream oss;
                oss << "Currency Rates:\n"
                    << currencyInfo[0] << "\n"
                    << currencyInfo[1] << "\n";
                send(clientSocket, oss.str().c_str(), oss.str().size(), 0);
            }
            else if (singleCommand == "subscribe:stock") {
                clients.insert(ClientInfo{ clientSocket, "stock" });
                std::cout << "Client subscribed to stock updates." << std::endl;

                auto stockInfo = GetStockInfo();
                std::ostringstream oss;
                oss << "TSLA Inc. (TSLA):\n"
                    << stockInfo[0] << "\n"
                    << stockInfo[1] << "\n"
                    << stockInfo[2] << "\n";
                send(clientSocket, oss.str().c_str(), oss.str().size(), 0);
            }
            else {
                std::cout << "Invalid subscription type: " << singleCommand << std::endl;
            }
        }
    }
    else {
        closesocket(clientSocket);
        std::cout << "Client disconnected (read error)." << std::endl;
    }
}


int main() {
    // Ініціалізація Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed." << std::endl;
        return 1;
    }

    const std::string ipAddress = "127.0.0.1";
    const int port = 12345;

    // Створення сокету
    SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (serverSocket == INVALID_SOCKET) {
        std::cerr << "Error creating socket." << std::endl;
        WSACleanup();
        return 1;
    }

    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    serverAddr.sin_addr.s_addr = inet_addr(ipAddress.c_str());

    if (bind(serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cerr << "Binding failed." << std::endl;
        closesocket(serverSocket);
        WSACleanup();
        return 1;
    }

    // Починаємо слухати на сокеті
    if (listen(serverSocket, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "Listen failed." << std::endl;
        closesocket(serverSocket);
        WSACleanup();
        return 1;
    }

    std::cout << "Server started on " << ipAddress << ":" << port << std::endl;

    std::thread weatherThread(WeatherUpdateService);
    std::thread currencyThread(CurrencyUpdateService);
    std::thread stockThread(StockUpdateService);
    weatherThread.detach();
    currencyThread.detach();
    stockThread.detach();

    while (true) {
        // Прийом вхідних з'єднань
        struct sockaddr_in clientAddr;
        int clientAddrSize = sizeof(clientAddr);
        SOCKET clientSocket = accept(serverSocket, (struct sockaddr*)&clientAddr, &clientAddrSize);
        if (clientSocket == INVALID_SOCKET) {
            std::cerr << "Accept failed." << std::endl;
            continue;
        }

        std::cout << "Client connected." << std::endl;

        // Обробляємо клієнта в окремому потоці
        std::thread clientThread(HandleClient, clientSocket);
        clientThread.detach();
    }

    closesocket(serverSocket);
    WSACleanup();
    return 0;
}