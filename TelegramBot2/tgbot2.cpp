#include <stdio.h>
#include <tgbot/tgbot.h>
#define SQLITECPP_COMPILE_DLL
#include <SQLiteCpp/SQLiteCpp.h>
#include <regex>
#include <iostream>
#include <fstream>
#include <curl/curl.h>


struct UserData {
    std::string phoneNumber;
    std::string name;
    std::string link;
    std::string size;
    std::string cost;
    int step = 0;
};

std::map<long, UserData> sessions;

// Функция для записи данных, полученных с помощью curl
size_t write_data(void* ptr, size_t size, size_t nmemb, FILE* stream) {
    size_t written = fwrite(ptr, size, nmemb, stream);
    return written;
}
// Функция для скачивания фотографии с помощью curl
bool download_photo(const std::string& photo_url, const std::string& save_path) {
    CURL* curl;
    FILE* fp;
    CURLcode res;

    curl = curl_easy_init();
    if (curl) {
        // Устанавливаем URL для загрузки
        curl_easy_setopt(curl, CURLOPT_URL, photo_url.c_str());

        // Устанавливаем функцию для записи данных в файл
        fp = fopen(save_path.c_str(), "wb");
        if (fp) {
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);

            // Выполняем запрос
            res = curl_easy_perform(curl);

            // Проверяем результат выполнения
            if (res != CURLE_OK) {
                fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
                fclose(fp);
                curl_easy_cleanup(curl);
                return false;
            }

            // Закрываем файл
            fclose(fp);

            // Очищаем curl
            curl_easy_cleanup(curl);

            return true;
        }
        else {
            fprintf(stderr, "Не удалось открыть файл для записи.\n");
            curl_easy_cleanup(curl);
            return false;
        }
    }
    else {
        fprintf(stderr, "Не удалось инициализировать curl.\n");
        return false;
    }
}


int get_user_step(const SQLite::Database& db, int userId) {
    try {
        // Проверяем, существует ли пользователь в базе данных
        SQLite::Statement query(db, "SELECT EXISTS(SELECT 1 FROM users WHERE id = ?)");
        query.bind(1, userId);
        if (query.executeStep()) {
            // Если пользователь существует, получаем его шаг
            if (query.getColumn(0).getInt() == 1) {
                // Формируем SQL-запрос для получения шага
                SQLite::Statement stepQuery(db, "SELECT step FROM users WHERE id = ?");
                stepQuery.bind(1, userId);
                if (stepQuery.executeStep()) {
                    return stepQuery.getColumn(0).getInt();
                }
            }
        }
        // Если пользователь не найден, возвращаем 0
        return 0;
    }
    catch (std::exception& e) {
        std::cerr << "Exception occurred: " << e.what() << std::endl;
        return 0;
    }
}

bool isValidPhoneNumber(const std::string& phoneNumber) {
    // Регулярное выражение для проверки формата номера телефона
    std::regex phoneRegex("\\+?[0-9]{11,12}");

    // Проверка, соответствует ли введенная строка формату номера телефона
    return std::regex_match(phoneNumber, phoneRegex);
}

int main() {

    SQLite::Database db("DB.db", SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
    TgBot::Bot bot("YOUR BOT TOKEN");
    bot.getEvents().onCommand("start", [&bot, &db](TgBot::Message::Ptr message) {
        try {
            long chatId = message->chat->id;
            if (sessions.find(chatId) == sessions.end()) {
                // Если сессии нет, создаем новую
                sessions[chatId] = UserData();
            }
            UserData& userData = sessions[chatId];
            if (get_user_step(db, chatId) == 0) {
                bot.getApi().sendMessage(chatId, u8"прив. Введи имя");
                userData.step = 1;
                db.exec("BEGIN TRANSACTION");
                SQLite::Statement users(db, "INSERT INTO users (id, step) VALUES (?, 1)");
                users.bind(1, message->chat->id);
                users.exec();
                db.exec("COMMIT");
            }
            else {
                bot.getApi().sendMessage(chatId, u8"менеджеры скоро свяжутся с вами");
            }
        }
        catch (std::exception& e) {
            std::cerr << "Exception occurred: " << e.what() << std::endl;
        }
        });

    bot.getEvents().onAnyMessage([&bot, &db](TgBot::Message::Ptr message) {

        long chatId = message->chat->id;
        if (sessions.find(chatId) == sessions.end()) {
            // Если сессии нет, создаем новую
            sessions[chatId] = UserData();
        }
        UserData& userData = sessions[chatId];

        userData.step = get_user_step(db, message->chat->id);
        if ((StringTools::startsWith(message->text, "/start"))) {
            return;
        }
        else if (userData.step != 0) {
            try {
                if (userData.step == 1) {
                    userData.name = message->text;

                    db.exec("BEGIN TRANSACTION");
                    SQLite::Statement updateQuery(db, "UPDATE users SET step = 2, name = ? WHERE id = ?");
                    updateQuery.bind(1, userData.name);
                    updateQuery.bind(2, message->chat->id);
                    updateQuery.exec();
                    db.exec("COMMIT");

                    userData.step++;
                    bot.getApi().sendMessage(chatId, u8"введи номер");
                }
                else if (userData.step == 2) {
                    userData.phoneNumber = message->text;
                    if (!isValidPhoneNumber(userData.phoneNumber)) {
                        bot.getApi().sendMessage(chatId, u8"Неккоректный номер, введите еще раз.");
                        userData.phoneNumber.clear();
                    }
                    else {

                        db.exec("BEGIN TRANSACTION");
                        SQLite::Statement updateQuery(db, "UPDATE users SET step = 3, number = ? WHERE id = ?");
                        updateQuery.bind(1, userData.phoneNumber);
                        updateQuery.bind(2, message->chat->id);
                        updateQuery.exec();
                        db.exec("COMMIT");

                        bot.getApi().sendMessage(chatId, u8"Спасибо! Теперь скинь ссылку где тебе удобнее будет с нами связаться");
                        userData.step++;
                    }
                }
                else if (userData.step == 3) {
                    userData.link = message->text;

                    db.exec("BEGIN TRANSACTION");
                    SQLite::Statement updateQuery(db, "UPDATE users SET step = 4, link = ? WHERE id = ?");
                    updateQuery.bind(1, userData.link);
                    updateQuery.bind(2, message->chat->id);
                    updateQuery.exec();
                    db.exec("COMMIT");

                    userData.step++;
                    bot.getApi().sendMessage(chatId, u8"уточни размер тату (маленькая, средняя, большая, еще не знаю");

                }
                else if (userData.step == 4) {
                    userData.size = message->text;

                    db.exec("BEGIN TRANSACTION");
                    SQLite::Statement updateQuery(db, "UPDATE users SET step = 5, size = ? WHERE id = ?");
                    updateQuery.bind(1, userData.size);
                    updateQuery.bind(2, message->chat->id);
                    updateQuery.exec();
                    db.exec("COMMIT");

                    userData.step++;
                    bot.getApi().sendMessage(chatId, u8"на какую стоимость тату ты рассчитываешь?");
                }
                else if (userData.step == 5) {
                    userData.cost = message->text;

                    db.exec("BEGIN TRANSACTION");
                    SQLite::Statement updateQuery(db, "UPDATE users SET step = 6, cost = ? WHERE id = ?");
                    updateQuery.bind(1, userData.cost);
                    updateQuery.bind(2, message->chat->id);
                    updateQuery.exec();
                    db.exec("COMMIT");

                    bot.getApi().sendMessage(chatId, u8"отправь фото желаемой тату (при наличии)");
                }
                else if (userData.step == 6) {
                    if (message->photo.empty()) {
                        // Если фото не было отправлено, ничего не делаем
                        return;
                    }

                    // Получаем ID фотографии с максимальным разрешением
                    static const std::string photoId = message->photo.back()->fileId;

                    // Получаем объект TgBot::Api
                    TgBot::Api api = bot.getApi();

                    // Получаем объект TgBot::File с информацией о файле
                    TgBot::File::Ptr fileInfo = api.getFile(photoId);
                    std::string filePath = fileInfo->filePath;

                    std::string photoUrl = "https://api.telegram.org/file/bot" + bot.getToken() + "/" + filePath;

                    // Путь для сохранения фотографии
                    std::string save_path = "C:\c++\bot\testtatoo\photo" + userData.name + ".jpg";

                    // Скачиваем фотографию
                    if (download_photo(photoUrl, save_path)) {
                        std::cout << "Фотография успешно скачана!\n";
                    }
                    else {
                        std::cout << "Ошибка при скачивании фотографии.\n";
                    }

                }
                else if (userData.step == 7) {
                    bot.getApi().sendMessage(chatId, u8"наши менеджеры скоро свяжутся с тобой");
                }
            }
            catch (std::exception& e) {
                std::cerr << "Exception occurred: " << e.what() << std::endl;
            }
        }
        });
    try {
        printf("Bot username: %s\n", bot.getApi().getMe()->username.c_str());
        TgBot::TgLongPoll longPoll(bot);
        while (true) {
            printf("Long poll started\n");
            longPoll.start();
        }
    }
    catch (TgBot::TgException& e) {
        printf("error: %s\n", e.what());
    }
    return 0;
}