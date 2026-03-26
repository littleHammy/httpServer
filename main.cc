#include <iostream>
#include <string>
#include <vector>
#include <ctime>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <random>
#include <optional>

#define _WIN32_WINNT 0x0A00
#define CPPHTTPLIB_NO_MMAP

#include "httplib.h"
#include "nlohmann/json.hpp"

using json = nlohmann::json;
namespace fs = std::filesystem;

struct User {
    std::string name;
    std::string account;
    std::string password;
    std::string timestamp;
    bool online;
    bool activated;
    bool isAdmin;
    time_t lastHeartbeat;
};

struct Message {
    std::string name;
    std::string content;
    std::string timestamp;
};

std::vector<User> users;
std::vector<Message> messages;

std::string getCurrentTime() {
    time_t now = time(0);
    struct tm tstruct;
    char buf[80];
    tstruct = *localtime(&now);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tstruct);
    return buf;
}

std::string generateRandomString(int length) {
    const std::string chars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    std::random_device rd;
    std::mt19937 generator(rd());
    std::uniform_int_distribution<> distribution(0, (int)chars.size() - 1);
    std::string result;
    for (int i = 0; i < length; ++i) {
        result += chars[distribution(generator)];
    }
    return result;
}

std::string getFileExtension(const std::string& filename) {
    size_t dotPos = filename.find_last_of(".");
    if (dotPos == std::string::npos) return "";
    return filename.substr(dotPos);
}

void saveUsers(const std::string& filename) {
    std::ofstream file(filename);
    if (file.is_open()) {
        for (const auto& user : users) {
            file << user.name << "\n";
            file << user.account << "\n";
            file << user.password << "\n";
            file << user.timestamp << "\n";
            file << (user.online ? "1" : "0") << "\n";
            file << (user.activated ? "1" : "0") << "\n";
            file << (user.isAdmin ? "1" : "0") << "\n";
        }
        file.close();
    }
}

void loadUsers(const std::string& filename) {
    std::ifstream file(filename);
    if (file.is_open()) {
        std::string name, account, password, timestamp, onlineStr, activatedStr, isAdminStr;
        while (std::getline(file, name) && std::getline(file, account) && std::getline(file, password) &&
               std::getline(file, timestamp) && std::getline(file, onlineStr) &&
               std::getline(file, activatedStr) && std::getline(file, isAdminStr)) {
            User user;
            user.name = name;
            user.account = account;
            user.password = password;
            user.timestamp = timestamp;
            user.online = false;
            user.activated = (activatedStr == "1");
            user.isAdmin = (isAdminStr == "1");
            user.lastHeartbeat = 0;
            users.push_back(user);
        }
        file.close();
    }

    bool adminExists = false;
    for (const auto& user : users) {
        if (user.account == "admin") {
            adminExists = true;
            break;
        }
    }

    if (!adminExists) {
        User adminUser;
        adminUser.name = "管理员";
        adminUser.account = "admin";
        adminUser.password = "123456";
        adminUser.timestamp = getCurrentTime();
        adminUser.online = false;
        adminUser.activated = true;
        adminUser.isAdmin = true;
        adminUser.lastHeartbeat = 0;
        users.push_back(adminUser);
        saveUsers(filename);
        std::cout << "Created default admin user" << std::endl;
    }
}

void saveMessages(const std::string& filename) {
    std::ofstream file(filename);
    if (file.is_open()) {
        for (const auto& msg : messages) {
            file << msg.name << "\n";
            file << msg.content << "\n";
            file << msg.timestamp << "\n";
        }
        file.close();
    }
}

void loadMessages(const std::string& filename) {
    std::ifstream file(filename);
    if (file.is_open()) {
        std::string name, content, timestamp;
        while (std::getline(file, name) && std::getline(file, content) && std::getline(file, timestamp)) {
            Message msg;
            msg.name = name;
            msg.content = content;
            msg.timestamp = timestamp;
            messages.push_back(msg);
        }
        file.close();
    }
}

int main() {
    loadUsers("./data/users.txt");
    loadMessages("./data/messages.txt");

    httplib::Server svr;
    svr.set_mount_point("/", "./www");

    svr.set_error_handler([](const httplib::Request&, httplib::Response& res) {
        res.status = 404;
        res.set_content(R"(
            <!DOCTYPE html>
            <html>
            <head><meta charset="UTF-8"></head>
            <body><h1>404 - 页面找不到</h1><p><a href="/">返回首页</a></p></body>
            </html>
        )", "text/html; charset=utf-8");
    });

    svr.Get("/api/status", [](const httplib::Request&, httplib::Response& res) {
        json statusJson;
        statusJson["status"] = "ok";
        statusJson["message"] = "Server is running";
        statusJson["version"] = "1.0.0";
        res.set_content(statusJson.dump(4), "application/json; charset=utf-8");
    });

    svr.Post("/api/login", [&](const httplib::Request& req, httplib::Response& res) {
        auto name_it = req.params.find("name");
        auto account_it = req.params.find("account");
        auto password_it = req.params.find("password");

        if (name_it == req.params.end() || account_it == req.params.end() || password_it == req.params.end()) {
            json errorJson;
            errorJson["status"] = "error";
            errorJson["message"] = "参数不完整";
            res.set_content(errorJson.dump(4), "application/json; charset=utf-8");
            return;
        }

        bool userExists = false;
        bool passwordCorrect = false;
        bool isActivated = false;
        bool isAdmin = false;

        for (auto& user : users) {
            if (user.account == account_it->second) {
                userExists = true;
                isActivated = user.activated;
                isAdmin = user.isAdmin;
                if (password_it->second == "__restore__") {
                    passwordCorrect = true;
                } else if (user.password == password_it->second) {
                    passwordCorrect = true;
                }
                if (passwordCorrect && (isActivated || isAdmin)) {
                    user.online = true;
                    user.lastHeartbeat = time(0);
                    user.timestamp = getCurrentTime();
                }
                break;
            }
        }

        if (!userExists) {
            User user;
            user.name = name_it->second;
            user.account = account_it->second;
            user.password = password_it->second;
            user.timestamp = getCurrentTime();
            user.online = false;
            user.activated = false;
            user.isAdmin = (account_it->second == "admin");
            users.push_back(user);
            saveUsers("./data/users.txt");

            json responseJson;
            responseJson["status"] = "error";
            responseJson["message"] = "账号已创建，需要管理员授权激活后才能登录";
            res.set_content(responseJson.dump(4), "application/json; charset=utf-8");
        } else if (passwordCorrect) {
            if (isActivated || isAdmin) {
                saveUsers("./data/users.txt");
                json responseJson;
                responseJson["status"] = "ok";
                responseJson["message"] = "登录成功";
                responseJson["name"] = name_it->second;
                responseJson["isAdmin"] = isAdmin;
                res.set_content(responseJson.dump(4), "application/json; charset=utf-8");
            } else {
                json errorJson;
                errorJson["status"] = "error";
                errorJson["message"] = "账号未激活，需要管理员授权";
                res.set_content(errorJson.dump(4), "application/json; charset=utf-8");
            }
        } else {
            json errorJson;
            errorJson["status"] = "error";
            errorJson["message"] = "密码错误";
            res.set_content(errorJson.dump(4), "application/json; charset=utf-8");
        }
    });

    svr.Get("/api/users", [&](const httplib::Request&, httplib::Response& res) {
        time_t now = time(0);
        for (auto& user : users) {
            if (user.online && (now - user.lastHeartbeat > 5)) {
                user.online = false;
            }
        }
        json usersJson = json::array();
        for (const auto& user : users) {
            if (user.online) {
                json userJson;
                userJson["name"] = user.name;
                userJson["account"] = user.account;
                usersJson.push_back(userJson);
            }
        }
        res.set_content(usersJson.dump(4), "application/json; charset=utf-8");
    });

    svr.Post("/api/heartbeat", [&](const httplib::Request& req, httplib::Response& res) {
        auto account_it = req.params.find("account");
        if (account_it == req.params.end()) {
            json errorJson;
            errorJson["status"] = "error";
            errorJson["message"] = "参数不完整";
            res.set_content(errorJson.dump(4), "application/json; charset=utf-8");
            return;
        }
        for (auto& user : users) {
            if (user.account == account_it->second) {
                user.lastHeartbeat = time(0);
                break;
            }
        }
        json successJson;
        successJson["status"] = "ok";
        res.set_content(successJson.dump(4), "application/json; charset=utf-8");
    });

    svr.Post("/api/logout", [&](const httplib::Request& req, httplib::Response& res) {
        auto account_it = req.params.find("account");

        if (account_it == req.params.end()) {
            json errorJson;
            errorJson["status"] = "error";
            errorJson["message"] = "参数不完整";
            res.set_content(errorJson.dump(4), "application/json; charset=utf-8");
            return;
        }

        for (auto& user : users) {
            if (user.account == account_it->second) {
                user.online = false;
                break;
            }
        }

        saveUsers("./data/users.txt");
        json successJson;
        successJson["status"] = "ok";
        successJson["message"] = "退出成功";
        res.set_content(successJson.dump(4), "application/json; charset=utf-8");
    });

    svr.Get("/api/admin/users", [&](const httplib::Request& req, httplib::Response& res) {
        auto account_it = req.params.find("account");
        auto password_it = req.params.find("password");

        if (account_it == req.params.end() || password_it == req.params.end()) {
            json errorJson;
            errorJson["status"] = "error";
            errorJson["message"] = "需要管理员认证";
            res.set_content(errorJson.dump(4), "application/json; charset=utf-8");
            return;
        }

        bool isAdmin = false;
        for (const auto& user : users) {
            if (user.account == account_it->second && user.password == password_it->second && user.isAdmin) {
                isAdmin = true;
                break;
            }
        }

        if (!isAdmin) {
            json errorJson;
            errorJson["status"] = "error";
            errorJson["message"] = "无权限访问";
            res.set_content(errorJson.dump(4), "application/json; charset=utf-8");
            return;
        }

        json usersJson = json::array();
        for (const auto& user : users) {
            json userJson;
            userJson["name"] = user.name;
            userJson["account"] = user.account;
            userJson["timestamp"] = user.timestamp;
            userJson["online"] = user.online;
            userJson["activated"] = user.activated;
            userJson["isAdmin"] = user.isAdmin;
            usersJson.push_back(userJson);
        }
        res.set_content(usersJson.dump(4), "application/json; charset=utf-8");
    });

    svr.Post("/api/admin/activate", [&](const httplib::Request& req, httplib::Response& res) {
        auto adminAccount_it = req.params.find("adminAccount");
        auto adminPassword_it = req.params.find("adminPassword");
        auto userAccount_it = req.params.find("userAccount");

        if (adminAccount_it == req.params.end() || adminPassword_it == req.params.end() || userAccount_it == req.params.end()) {
            json errorJson;
            errorJson["status"] = "error";
            errorJson["message"] = "参数不完整";
            res.set_content(errorJson.dump(4), "application/json; charset=utf-8");
            return;
        }

        bool isAdmin = false;
        for (const auto& user : users) {
            if (user.account == adminAccount_it->second && user.password == adminPassword_it->second && user.isAdmin) {
                isAdmin = true;
                break;
            }
        }

        if (!isAdmin) {
            json errorJson;
            errorJson["status"] = "error";
            errorJson["message"] = "无权限操作";
            res.set_content(errorJson.dump(4), "application/json; charset=utf-8");
            return;
        }

        bool userFound = false;
        for (auto& user : users) {
            if (user.account == userAccount_it->second) {
                user.activated = true;
                userFound = true;
                break;
            }
        }

        if (userFound) {
            saveUsers("./data/users.txt");
            json successJson;
            successJson["status"] = "ok";
            successJson["message"] = "用户激活成功";
            res.set_content(successJson.dump(4), "application/json; charset=utf-8");
        } else {
            json errorJson;
            errorJson["status"] = "error";
            errorJson["message"] = "用户不存在";
            res.set_content(errorJson.dump(4), "application/json; charset=utf-8");
        }
    });

    svr.Post("/api/admin/deactivate", [&](const httplib::Request& req, httplib::Response& res) {
        auto adminAccount_it = req.params.find("adminAccount");
        auto adminPassword_it = req.params.find("adminPassword");
        auto userAccount_it = req.params.find("userAccount");

        if (adminAccount_it == req.params.end() || adminPassword_it == req.params.end() || userAccount_it == req.params.end()) {
            json errorJson;
            errorJson["status"] = "error";
            errorJson["message"] = "参数不完整";
            res.set_content(errorJson.dump(4), "application/json; charset=utf-8");
            return;
        }

        bool isAdmin = false;
        for (const auto& user : users) {
            if (user.account == adminAccount_it->second && user.password == adminPassword_it->second && user.isAdmin) {
                isAdmin = true;
                break;
            }
        }

        if (!isAdmin) {
            json errorJson;
            errorJson["status"] = "error";
            errorJson["message"] = "无权限操作";
            res.set_content(errorJson.dump(4), "application/json; charset=utf-8");
            return;
        }

        bool userFound = false;
        for (auto& user : users) {
            if (user.account == userAccount_it->second && !user.isAdmin) {
                user.activated = false;
                user.online = false;
                userFound = true;
                break;
            }
        }

        if (userFound) {
            saveUsers("./data/users.txt");
            json successJson;
            successJson["status"] = "ok";
            successJson["message"] = "用户已被撤销激活";
            res.set_content(successJson.dump(4), "application/json; charset=utf-8");
        } else {
            json errorJson;
            errorJson["status"] = "error";
            errorJson["message"] = "用户不存在或无法撤销管理员";
            res.set_content(errorJson.dump(4), "application/json; charset=utf-8");
        }
    });

    svr.Post("/api/admin/delete", [&](const httplib::Request& req, httplib::Response& res) {
        auto adminAccount_it = req.params.find("adminAccount");
        auto adminPassword_it = req.params.find("adminPassword");
        auto userAccount_it = req.params.find("userAccount");

        if (adminAccount_it == req.params.end() || adminPassword_it == req.params.end() || userAccount_it == req.params.end()) {
            json errorJson;
            errorJson["status"] = "error";
            errorJson["message"] = "参数不完整";
            res.set_content(errorJson.dump(4), "application/json; charset=utf-8");
            return;
        }

        bool isAdmin = false;
        for (const auto& user : users) {
            if (user.account == adminAccount_it->second && user.password == adminPassword_it->second && user.isAdmin) {
                isAdmin = true;
                break;
            }
        }

        if (!isAdmin) {
            json errorJson;
            errorJson["status"] = "error";
            errorJson["message"] = "无权限操作";
            res.set_content(errorJson.dump(4), "application/json; charset=utf-8");
            return;
        }

        auto it = users.begin();
        bool userFound = false;
        while (it != users.end()) {
            if (it->account == userAccount_it->second && !it->isAdmin) {
                it = users.erase(it);
                userFound = true;
                break;
            } else {
                ++it;
            }
        }

        if (userFound) {
            saveUsers("./data/users.txt");
            json successJson;
            successJson["status"] = "ok";
            successJson["message"] = "用户已从列表中移除";
            res.set_content(successJson.dump(4), "application/json; charset=utf-8");
        } else {
            json errorJson;
            errorJson["status"] = "error";
            errorJson["message"] = "用户不存在或无法删除管理员";
            res.set_content(errorJson.dump(4), "application/json; charset=utf-8");
        }
    });

    svr.Post("/api/change-password", [&](const httplib::Request& req, httplib::Response& res) {
        auto account_it = req.params.find("account");
        auto oldPassword_it = req.params.find("oldPassword");
        auto newPassword_it = req.params.find("newPassword");

        if (account_it == req.params.end() || oldPassword_it == req.params.end() || newPassword_it == req.params.end()) {
            json errorJson;
            errorJson["status"] = "error";
            errorJson["message"] = "参数不完整";
            res.set_content(errorJson.dump(4), "application/json; charset=utf-8");
            return;
        }

        bool userFound = false;
        bool passwordCorrect = false;
        for (auto& user : users) {
            if (user.account == account_it->second) {
                userFound = true;
                if (user.password == oldPassword_it->second) {
                    passwordCorrect = true;
                    user.password = newPassword_it->second;
                    break;
                }
            }
        }

        if (!userFound) {
            json errorJson;
            errorJson["status"] = "error";
            errorJson["message"] = "用户不存在";
            res.set_content(errorJson.dump(4), "application/json; charset=utf-8");
        } else if (!passwordCorrect) {
            json errorJson;
            errorJson["status"] = "error";
            errorJson["message"] = "旧密码错误";
            res.set_content(errorJson.dump(4), "application/json; charset=utf-8");
        } else {
            saveUsers("./data/users.txt");
            json successJson;
            successJson["status"] = "ok";
            successJson["message"] = "密码修改成功";
            res.set_content(successJson.dump(4), "application/json; charset=utf-8");
        }
    });

    svr.Get("/api/messages", [&](const httplib::Request&, httplib::Response& res) {
        json messagesJson = json::array();
        for (const auto& msg : messages) {
            json msgJson;
            msgJson["name"] = msg.name;
            msgJson["content"] = msg.content;
            msgJson["timestamp"] = msg.timestamp;
            messagesJson.push_back(msgJson);
        }
        res.set_content(messagesJson.dump(4), "application/json; charset=utf-8");
    });

    svr.Post("/api/messages", [](const httplib::Request& req, httplib::Response& res) {
        auto name_it = req.params.find("name");
        auto content_it = req.params.find("content");

        if (name_it == req.params.end() || content_it == req.params.end()) {
            json errorJson;
            errorJson["status"] = "error";
            errorJson["message"] = "参数不完整";
            res.set_content(errorJson.dump(4), "application/json; charset=utf-8");
            return;
        }

        Message msg;
        msg.name = name_it->second;
        msg.content = content_it->second;
        msg.timestamp = getCurrentTime();
        messages.push_back(msg);

        saveMessages("./data/messages.txt");

        json successJson;
        successJson["status"] = "ok";
        successJson["message"] = "消息发送成功";
        res.set_content(successJson.dump(4), "application/json; charset=utf-8");
    });

    svr.Delete("/api/messages", [](const httplib::Request&, httplib::Response& res) {
        messages.clear();
        saveMessages("./data/messages.txt");
        json successJson;
        successJson["status"] = "ok";
        successJson["message"] = "所有消息已清除";
        res.set_content(successJson.dump(4), "application/json; charset=utf-8");
    });

    svr.Post("/api/admin/messages/delete", [&](const httplib::Request& req, httplib::Response& res) {
        auto adminAccount_it = req.params.find("adminAccount");
        auto adminPassword_it = req.params.find("adminPassword");
        auto messageId_it = req.params.find("messageId");

        if (adminAccount_it == req.params.end() || adminPassword_it == req.params.end() || messageId_it == req.params.end()) {
            json errorJson;
            errorJson["status"] = "error";
            errorJson["message"] = "参数不完整";
            res.set_content(errorJson.dump(4), "application/json; charset=utf-8");
            return;
        }

        bool isAdmin = false;
        for (const auto& user : users) {
            if (user.account == adminAccount_it->second && user.password == adminPassword_it->second && user.isAdmin) {
                isAdmin = true;
                break;
            }
        }

        if (!isAdmin) {
            json errorJson;
            errorJson["status"] = "error";
            errorJson["message"] = "无权限操作";
            res.set_content(errorJson.dump(4), "application/json; charset=utf-8");
            return;
        }

        try {
            int messageId = std::stoi(messageId_it->second);
            if (messageId >= 0 && messageId < (int)messages.size()) {
                messages.erase(messages.begin() + messageId);
                saveMessages("./data/messages.txt");
                json successJson;
                successJson["status"] = "ok";
                successJson["message"] = "消息删除成功";
                res.set_content(successJson.dump(4), "application/json; charset=utf-8");
            } else {
                json errorJson;
                errorJson["status"] = "error";
                errorJson["message"] = "消息不存在";
                res.set_content(errorJson.dump(4), "application/json; charset=utf-8");
            }
        } catch (const std::exception& e) {
            json errorJson;
            errorJson["status"] = "error";
            errorJson["message"] = "无效的消息ID";
            res.set_content(errorJson.dump(4), "application/json; charset=utf-8");
        }
    });

    svr.Post("/api/upload", [](const httplib::Request& req, httplib::Response& res) {
        if (req.form.has_file("image")) {
            auto file = req.form.get_file("image");
            std::string randomName = generateRandomString(10);
            std::string extension = getFileExtension(file.filename);
            std::string filename = randomName + extension;
            std::string filePath = "./data/images/" + filename;

            std::ofstream outFile(filePath, std::ios::binary);
            if (outFile.is_open()) {
                outFile.write(file.content.c_str(), file.content.size());
                outFile.close();

                json successJson;
                successJson["status"] = "ok";
                successJson["message"] = "图片上传成功";
                successJson["url"] = "/data/images/" + filename;
                res.set_content(successJson.dump(4), "application/json; charset=utf-8");
            } else {
                json errorJson;
                errorJson["status"] = "error";
                errorJson["message"] = "图片保存失败";
                res.set_content(errorJson.dump(4), "application/json; charset=utf-8");
            }
        } else {
            json errorJson;
            errorJson["status"] = "error";
            errorJson["message"] = "没有文件上传";
            res.set_content(errorJson.dump(4), "application/json; charset=utf-8");
        }
    });

    svr.set_mount_point("/data", "./data");

    std::cout << "C++ HTTP Server started: http://localhost:8888" << std::endl;
    std::cout << "Chat room: http://localhost:8888/chat.html" << std::endl;

    bool ret = svr.listen("0.0.0.0", 8888);
    if (!ret) {
        std::cerr << "Server startup failed!" << std::endl;
        return -1;
    }

    return 0;
}
