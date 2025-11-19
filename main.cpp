#include "crow.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <string>
#include <unistd.h>

namespace fs = std::filesystem;

// todo mutex
std::string serverLog;
enum ExitCodes {
    Ok,
    NonRoot
};

bool isRoot() {
    return geteuid() == 0;
}

std::string getCurrentTime() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t nowt = std::chrono::system_clock::to_time_t(now);
    std::string ans = std::ctime(&nowt);
    ans.resize(ans.size() - 1); // remove \n
    return ans;
}

// todo: log as middleware?
// todo: make log less verbose (print all staff to file not to stdout)
void addToLog(const std::string& message) {
    std::string logEntry = "[" + getCurrentTime() + "] " + message + "\n";
    serverLog += logEntry;
    std::cout << logEntry;
}

int main() {
    if (!isRoot()) {
        std::cerr << "Start server as root. Abort" << std::endl;
        return ExitCodes::NonRoot;
    }

    crow::SimpleApp app;

    CROW_ROUTE(app, "/info")([](){
        return crow::response(200, "Ok\n");
    });

    CROW_ROUTE(app, "/log")([](){
        return crow::response(200, serverLog);
    });

    // todo: move to separate file
    // todo: split this huge function on little several ones
    CROW_ROUTE(app, "/upload").methods("POST"_method)
    ([](const crow::request& req){
        try {
            auto body = req.body;

            std::string boundary;
            auto contentType = req.get_header_value("Content-Type");
            // todo: ugly manual body processing. maybe prefer to use pistachio lib
            if (contentType.find("multipart/form-data") != std::string::npos) {
                size_t pos = contentType.find("boundary=");
                if (pos != std::string::npos) {
                    boundary = contentType.substr(pos + 9);
                }
            }

            if (boundary.empty()) {
                std::string errMsg = "Bad boundary in Content-Type";
                addToLog(errMsg);
                return crow::response(400, errMsg + "\n");
            }

            std::string fileStart = "--" + boundary + "\r\n";
            fileStart += "Content-Disposition: form-data; name=\"file\"; filename=\"";
            
            auto filenamePos = body.find(fileStart);
            if (filenamePos == std::string::npos) {
                std::string errMsg = "Bad filename in Content-Disposition";
                addToLog(errMsg);
                return crow::response(400, errMsg + "\n");
            }

            filenamePos += fileStart.length();
            auto filenameEnd = body.find("\"", filenamePos);
            // todo: string_view?
            auto filename = body.substr(filenamePos, filenameEnd - filenamePos);

            std::string dataStart = "\r\n\r\n";
            auto dataPos = body.find(dataStart, filenameEnd);
            if (dataPos == std::string::npos) {
                std::string errMsg = "No filedata";
                addToLog(errMsg);
                return crow::response(400, errMsg + "\n");
            }
            
            dataPos += dataStart.length();

            std::string endBoundary = "\r\n--" + boundary + "--";
            auto dataEnd = body.find(endBoundary, dataPos);
            if (dataEnd == std::string::npos) {
                std::string errMsg = "No closing boundary";
                addToLog(errMsg);
                return crow::response(400, errMsg + "\n");
            }

            auto fileData = body.substr(dataPos, dataEnd - dataPos);
            
            std::string tmpFilename = "/tmp/" + fs::path(filename).filename().string() + ".XXXXXX";
            std::vector<char> fn;
            std::ranges::copy(tmpFilename, std::back_inserter(fn));
            fn.push_back('\0');
            auto fd = mkstemp(fn.data()); // todo use tmpfile?
            if (fd == -1) {
                std::string errMsg = "Can't open file '" + tmpFilename + "' for writing";
                addToLog(errMsg);
                return crow::response(500, errMsg + "\n");
            }
            
            write(fd, fileData.c_str(), fileData.size() + 1);
            close(fd);

            std::string okMsg = std::to_string(fileData.size()) + " bytes from uploaded '" + filename +
                    "' has been successfully saved on server as '" + tmpFilename + '\'';
            addToLog(okMsg);
            
            return crow::response(200, okMsg + "\n");
            
        } catch (const std::exception& e) {
            addToLog(std::string(e.what()));
            return crow::response(500, std::string(e.what()) + "\n");
        }
    });

    addToLog("Starting web server on 1616");
    app.port(1616).multithreaded().run();

    return 0;
}
