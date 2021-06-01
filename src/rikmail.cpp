/*
// Copyright (c) 2019 Intel Corporation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
*/

#include "rikmail.hpp"
#include <phosphor-logging/log.hpp>
#include <phosphor-logging/elog-errors.hpp>
#include <boost/process/child.hpp>
#include <boost/process/io.hpp>
#include <vector>
#include <unordered_map>
#include <xyz/openbmc_project/Common/error.hpp>

#include <filesystem>
#include "mail.h"

namespace fs = std::filesystem;


template <typename... ArgTypes>
static std::vector<std::string> executeCmd(const char* path,
                                           ArgTypes&&... tArgs)
{
    std::vector<std::string> stdOutput;
    boost::process::ipstream stdOutStream;
    boost::process::child execProg(path, const_cast<char*>(tArgs)...,
                                   boost::process::std_out > stdOutStream);
    std::string stdOutLine;

    while (stdOutStream && std::getline(stdOutStream, stdOutLine) &&
           !stdOutLine.empty())
    {
        stdOutput.emplace_back(stdOutLine);
    }

    execProg.wait();

    int retCode = execProg.exit_code();
    if (retCode)
    {
        phosphor::logging::log<phosphor::logging::level::ERR>(
            "Command execution failed",
            phosphor::logging::entry("PATH=%d", path),
            phosphor::logging::entry("RETURN_CODE:%d", retCode));
        phosphor::logging::elog<
            sdbusplus::xyz::openbmc_project::Common::Error::InternalFailure>();
    }

    return stdOutput;
}

RikfanMgr::RikmailMgr(boost::asio::io_service& io_,
                     sdbusplus::asio::object_server& srv_,
                     std::shared_ptr<sdbusplus::asio::connection>& conn_) :
    io(io_),
    server(srv_), conn(conn_)
{
    iface = server.add_interface(RikmailPath, RikmailIface);
    iface->register_method("ReadMode", [this]() { return this->mode; });
    // iface->register_method("Read", [this](const std::string& key) {
    //     std::unordered_map<std::string, std::string> env = readAllVariable();
    //     auto it = env.find(key);
    //     if (it != env.end())
    //     {
    //         return it->second;
    //     }
    //     return std::string{};
    // });

    iface->register_method(
        "WriteMode", [this](const std::string& mode) {
            setMailMode(mode);
        });
    iface->initialize(true);

    this->mode = readConf();
    setFanMode(std::to_string(this->mode));
}

std::unordered_map<std::string, std::string> RikmailMgr::readAllVariable()
{
    std::unordered_map<std::string, std::string> env;
    std::vector<std::string> output = executeCmd("/sbin/fw_printenv");
    for (const auto& entry : output)
    {
        size_t pos = entry.find("=");
        if (pos + 1 >= entry.size())
        {
            phosphor::logging::log<phosphor::logging::level::ERR>(
                "Invalid U-Boot environment",
                phosphor::logging::entry("ENTRY=%s", entry.c_str()));
            continue;
        }
        // using string instead of string_view for null termination
        std::string key = entry.substr(0, pos);
        std::string value = entry.substr(pos + 1);
        env.emplace(key, value);
    }
    return env;
}

void RikmailMgr::setMailMode(const std::string& mode)
{
    phosphor::logging::log<phosphor::logging::level::ERR>(
        ("Rikmail set mode " + mode).c_str());

    try 
    {
       this->mode = std::stoi(mode);
    } 
    catch (const std::exception& e) 
    { 
         // std::cout << e.what();
        this->mode = 2;
    }
    setMailMode(this->mode);
    writeConf(this->mode);
    // executeCmd("/sbin/rikmail_setmode", mode.c_str());
    return;
}


int RikmailMgr::readConf()
{
    int mode = 2;
    fs::path conf_fname = "/etc/rikmail/rikmail.conf";
    try
    {
        std::ifstream conf_stream {conf_fname};
        conf_stream >> mode;
    }
    catch (const std::exception& e)
    {
        mode = 2;
        writeConf(mode);
    }
    return mode;
}


void RikfanMgr::writeConf(int mode)
{
    fs::path conf_fname = "/etc/rikmail/rikmail.conf";
    std::ofstream conf_stream {conf_fname};
    conf_stream << mode;
}

