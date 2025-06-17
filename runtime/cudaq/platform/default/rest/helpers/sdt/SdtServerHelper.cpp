/*******************************************************************************
 * Copyright (c) 2022 - 2025 NVIDIA Corporation & Affiliates.                  *
 * All rights reserved.                                                        *
 *                                                                             *
 * This source code and the accompanying materials are made available under    *
 * the terms of the Apache License 2.0 which accompanies this distribution.    *
 ******************************************************************************/
#include "common/Logger.h"
#include "common/RestClient.h"
#include "common/ServerHelper.h"
#include "cudaq/utils/cudaq_utils.h"
#include <fstream>
#include <iostream>
#include <thread>
using nlohmann::json;
#include "llvm/Support/Base64.h"
#include <regex>

namespace cudaq {

/// @brief Find and set the API and refresh tokens, and the time string.
void findApiKeyInFile1(std::string &apiKey, const std::string &path,
                      std::string &refreshKey, std::string &timeStr,
                      std::string &credentials);

/// Search for the API key, invokes findApiKeyInFile1
std::string searchAPIKey1(std::string &key, std::string &refreshKey,
                         std::string &credentials, std::string &timeStr,
                         std::string userSpecifiedConfig = "");

/// @brief The implements the ServerHelper interface
/// to map Job requests and Job result retrievals actions from the calling
/// Executor to the specific schema required by the remote REST
/// server.
class SdtServerHelper : public ServerHelper {
protected:
  /// @brief The base URL
  std::string baseUrl = "https://api.anyon.cloud:5000/";  // hjpark: 주소 수정 필요
  /// @brief The machine we are targeting.
  std::string machine = "telegraph-8q"; //"berkeley-25q";//
  /// @brief Time string, when the last tokens were retrieved
  std::string timeStr = "";
  /// @brief The refresh token
  std::string refreshKey = "";
  /// @brief The API token for the remote server
  std::string apiKey = "";
  std::string credentials = "";

  std::string userSpecifiedCredentials = "";
  std::string credentialsPath = "";
  std::string configHjpark = "";

  // hjpark
  // std::string QUREA_BASE_URL_DEFAULT = "https://qurea-api-local.sdt.services";      // dev-server
  std::string QUREA_BASE_URL_DEFAULT = "http://qurea-api-local.sdt.services:30001"; // local-server
  std::string QUREA_API_GATEWAY_PATH = "/services/job";
  std::string QUREA_ENGINE_JOB_PATH = "/api/quantum-jobs";
  std::string qureaBaseUrl = "";
  std::string qureaApiKey = "";

  /// @brief The ServerHelper requires the API token be updated every so often,
  /// using the provided refresh token. This function will do that.
  void refreshTokens(bool force_refresh = false);

  /// @brief Return the headers required for the REST calls
  RestHeaders generateRequestHeader() const;
  RestHeaders generateRequestHeader(std::string) const;

  void postMessage(ServerMessage body);

public:
  /// @brief Return the name of this server helper, must be the
  /// same as the qpu config file.
  const std::string name() const override { return "sdt"; }
  RestHeaders getHeaders() override;

  // hjpark: 예를 들어 .py 에서 아래와 같이 호출되었을 때,
  // cudaq.set_target("sdt", machine="qubesim", url="localhost", credentials="/sdt/config/.sdt_config")
  // "sdt" 뒤에 전달되는 key-value 설정값들을 받을 수 있는 메서드다. 
  // key 값은 정하기 나름이라, 어떻게 쓸지 필요한 대로 수정만 하면 됌.
  void initialize(BackendConfig config) override {
    // hjpark: Get system environments
    char* envQureaApiKey = getenv("QUREA_API_KEY");
    if (envQureaApiKey == NULL) {
      std::cout << "[SDT] envQureaApiKey is NULL" << std::endl;
      qureaApiKey = "";
      // return;
    } else {
      qureaApiKey = std::string(envQureaApiKey);
    }
    std::cout << "[SDT] QUREA_API_KEY: " << qureaApiKey << std::endl;

    char* envQureaBaseUrl = getenv("QUREA_BASE_URL");
    if (envQureaBaseUrl == NULL) {
      std::cout << "[SDT] envQureaBaseUrl is NULL" << std::endl;
      qureaBaseUrl = QUREA_BASE_URL_DEFAULT;
    } else {
      qureaBaseUrl = std::string(envQureaBaseUrl);
    }
    std::cout << "[SDT] QUREA_BASE_URL: " << qureaBaseUrl << std::endl;

    // hjpark: Get user configurations
    backendConfig = config;
    std::cout << "[SDT] backendConfig: \n";
    for (const auto& [key, value] : backendConfig) {
        std::cout << "  " << key << ": " << value << std::endl;
    }

    // Set the machine
    auto iter = backendConfig.find("machine");
    if (iter != backendConfig.end())
      machine = iter->second;

    // Set an alternate base URL if provided
    iter = backendConfig.find("url");
    if (iter != backendConfig.end()) {
      baseUrl = iter->second;
      if (!baseUrl.ends_with("/"))
        baseUrl += "/";
    }

    // Set credentials
    iter = backendConfig.find("credentials");
    if (iter != backendConfig.end())
      userSpecifiedCredentials = iter->second;

    // Set user config
    iter = backendConfig.find("configHjpark");
    if (iter != backendConfig.end())
      configHjpark = iter->second;

    parseConfigForCommonParams(config);
  }

  /// @brief Create a job payload for the provided quantum codes
  ServerJobPayload
  createJob(std::vector<KernelExecution> &circuitCodes) override;

  /// @brief Return the job id from the previous job post
  std::string extractJobId(ServerMessage &postResponse) override;

  /// @brief Return the URL for retrieving job results
  std::string constructGetJobPath(ServerMessage &postResponse) override;
  std::string constructGetJobPath(std::string &jobId) override;

  /// @brief Return true if the job is done
  bool jobIsDone(ServerMessage &getJobResponse) override;

  /// @brief Given a completed job response, map back to the sample_result
  cudaq::sample_result processResults(ServerMessage &postJobResponse,
                                      std::string &jobID) override;

  /// @brief Update `passPipeline` with architecture-specific pass options
  void updatePassPipeline(const std::filesystem::path &platformPath,
                          std::string &passPipeline) override;
};

// hjpark: Executor.cpp에서 호출되며, Executor.cpp에서 API 서버로 호출함. response로 task_id를 받고 일단 끝. 
//         이후에 아래에 있는 jobIsDone 메서드를 통해 polling으로 종료를 체크함.
ServerJobPayload
SdtServerHelper::createJob(std::vector<KernelExecution> &circuitCodes) {
  std::vector<ServerMessage> messages;
  for (auto &circuitCode : circuitCodes) {
    // Construct the job itself
    ServerMessage j;
    j["machine"] = machine;
    j["language"] = "QIR 1.0";
    j["program"] = circuitCode.code;    // QIR1.0 을 base64로 인코딩한 값이 저장됨.
    j["priority"] = "normal";
    j["count"] = shots;
    j["options"] = nullptr;
    j["name"] = circuitCode.name;
    messages.push_back(j);

    std::cout << "[SDT] Message: \n" << j << std::endl;
    /* 예시
      {
        "count": 1000,
        "language": "QIR 1.0",
        "machine": "telegraph-8q",
        "name": "kernel",
        "options": null,
        "priority": "normal",
        "program": "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aXRfcmVzZXR0aW5nY2xhc3NpY2FsX2ludHNjbGFzc2ljYWxfZmxvYXRzY2xhc3NpY2FsX2ZpeGVkX3BvaW50c3VzZXJfZnVuY3Rpb25zZHluYW1pY19mbG9hdF9hcmdzZXh0ZXJuX2Z1bmN0aW9uc2JhY2t3YXJkc19icmFuY2hpbmcAIwiXMoJwLSMIFzOCcDUjCJczgjAhIwhTMsPgBM8MAyRAMwzRIM0wQMQ0wwAV0wwDZFAzDNAxzTBAyDTDACXTDAOkTDMM0DLNMEDMNMMANZOMBCYoIzY2uzaXtjeyOrYyFzO2sLO5URqqsi4s0zau8z4wAACpGAAAJwAAAAsKciiHd4AHelhwmEM9uMM4sEM50MOC5hzGoQ3oQR7CwR3mIR3oIR3ewR0WNONgDudQD+EgD+RAD+EgD+dQDvSwgIEHeSiHcGAHdniHcQgHeigHclhwnMM4tAE7pIM9lMMCaxzYIRzc4RzcIBzkYRzcIBzogR7CYRzQoRzIYRzCgR3YYcEBD/QgD+FQD/SADguIdRgHc0gHAAAAANEQAAAGAAAAB8w8pIM7nAM7lAM9oIM8lEM4kMMBAAAAYSAAACMAAAATBEEsEAAAAAkAAACERwBKgM4IgCmWoDUCYIolyA11BAKARVg01BEIARZhEQAAAAAjBgUQgmCAWMaIgTGEIBhEk1GMGBhECIIBUxnEiIExhCAYPBQhjBgUQAiCASURIwZGEYJgwFgGMWJgGCEIBk9FCCMGxRGCYEBNhIYDAQAAAAEAAAAHENAAAAAAAHEgAAADAAAAMg4QIoQCsQQAAAAAAAAAAGUMAABJAAAAEgOUOAIAAAADAAAAywAAAC8AAABMAAAAAQAAAFgAAAAAAAAAWAAAAAgAAAAYAQAAAAAAAPoAAAAZAAAAEwEAABEAAAATAAAAAAAAABgBAAAAAAAAAAAAAAgAAAAAAAAAJgAAABgAAAAmAAAAGAAAAP////8AJAAAPgAAABcAAAA+AAAAFwAAAP////8IJAAAVQAAABgAAABVAAAAGAAAAP////8IJAAAbQAAACMAAABtAAAAIwAAAP////8IJAAAkAAAABoAAACQAAAAGgAAAP////8IJAAAqgAAACEAAACqAAAAIQAAAP////8IJAAAJAEAABUAAAAAAAAAEwAAAP////8ACAAAOQEAABUAAAATAAAAEwAAAP////8ACAAAAAAAAF0MAABXAAAAEgOUrgIAAABjc3RyLjcyMzAzMDMwMzAzMDAwY3N0ci43MjMwMzAzMDMwMzEwMF9fbnZxcHBfX21saXJnZW5fX2tlcm5lbF9fcXVhbnR1bV9fcWlzX19oX19ib2R5X19xdWFudHVtX19xaXNfX216X19ib2R5X19xdWFudHVtX19ydF9fcmVzdWx0X3JlY29yZF9vdXRwdXRfX3F1YW50dW1fX3Fpc19fY25vdF9fYm9keV9fcXVhbnR1bV9fcWlzX19yZWFkX3Jlc3VsdF9fYm9keTE2LjAuNiA3Y2JmMWEyNTkxNTIwYzI0OTFhYTM1MzM5ZjIyNzc3NWY0ZDNhZGY2YWFyY2g2NC11bmtub3duLWxpbnV4LWdudUxMVk1EaWFsZWN0TW9kdWxlLkxjc3RyLjcyMzAzMDMwMzAzMDAwLkxjc3RyLjcyMzAzMDMwMzAzMTAwAAAAAAAA"
      }
    */

    // hjpark
    // if (token) {
      postMessage(j);
      std::cout << "[SDT] API Request Completed" << std::endl;
    // }
  }

  /*
  // Get the tokens we need
  credentialsPath = searchAPIKey1(apiKey, refreshKey, credentials, timeStr, userSpecifiedCredentials);
  refreshTokens();
  
  // Get the headers
  RestHeaders headers = generateRequestHeader();
  */

  // Remote Backend 로 전송하기 위한 RestHeaders
  RestHeaders headers{        // RestHeaders => map<string, string>
    {"Authorization", qureaApiKey},
    {"Content-Type", "application/json"},
    // {"Connection", "keep-alive"},
    // {"Accept", "*/*"}
  };

  cudaq::info("Created job payload for sdt, language is QIR 1.0, targeting {}", machine);

  // return the payload
  return std::make_tuple(baseUrl + "job", headers, messages);
}

std::string SdtServerHelper::extractJobId(ServerMessage &postResponse) {
  // printf("Extracting ID\n");
  std::string jobToken =
      postResponse[0]["job_token"]
          .get<std::string>(); // The post response is an array [json_data,
                               // http_status_code]
  // printf("Extracted ID %s\n",jobToken.c_str());
  return jobToken;
}

std::string
SdtServerHelper::constructGetJobPath(ServerMessage &postResponse) {
  return baseUrl + "job/" + extractJobId(postResponse);
}

std::string SdtServerHelper::constructGetJobPath(std::string &jobId) {
  return baseUrl + "job/" + jobId;
}

// hjpark: 여기서 작업이 끝났는지 polling으로 체크. future.cpp에서 호출 중
bool SdtServerHelper::jobIsDone(ServerMessage &getJobResponse) {
  auto status = getJobResponse[0]["status"]
                    .get<std::string>(); // All job get and post responses at an
                                         // array of [resdata, httpstatuscode]
  if (status == "failed") {
    std::string msg = "";
    if (getJobResponse[0].count("error"))
      msg = getJobResponse[0]["error"]["text"].get<std::string>();
    throw std::runtime_error("Job failed to execute msg = [" + msg + "]");
  } else if (status == "waiting") {
    return false;
  } else if (status == "executing") {
    return false;
  } else
    return status == "done";
}

// hjpark: future.cpp에서 호출하는 jobIsDone이 true가 되면 이 메서드가 실행되서 결과 받아옴
cudaq::sample_result
SdtServerHelper::processResults(ServerMessage &postJobResponse,
                                  std::string &jobId) {
  // Results come back as a map of vectors. Each map key corresponds to a qubit
  // and its corresponding vector holds the measurement results in each shot:
  //      { "results" : { "r0" : ["0", "0", ...],
  //                      "r1" : ["1", "0", ...]  } }
  auto results = postJobResponse[0]["results"];

  cudaq::info("Results message: {}", results.dump());

  std::vector<ExecutionResult> srs;

  // Populate individual registers' results into srs
  for (auto &[registerName, result] : results.items()) {
    auto bitResults = result.get<std::vector<std::string>>();
    CountsDictionary thisRegCounts;
    for (auto &b : bitResults)
      thisRegCounts[b]++;
    srs.emplace_back(thisRegCounts, registerName);
    srs.back().sequentialData = bitResults;
  }

  // The global register needs to have results sorted by qubit number.
  // Sort output_names by qubit first and then result number. If there are
  // duplicate measurements for a qubit, only save the last one.
  if (outputNames.find(jobId) == outputNames.end())
    throw std::runtime_error("Could not find output names for job " + jobId);

  auto &output_names = outputNames[jobId];
  for (auto &[result, info] : output_names) {
    cudaq::info("Qubit {} Result {} Name {}", info.qubitNum, result,
                info.registerName);
  }

  // The local mock server tests don't work the same way as the true Sdt
  // QPU. They do not support the full named QIR output recording functions.
  // Detect for the that difference here.
  bool mockServer = false;
  if (results.begin().key() == "MOCK_SERVER_RESULTS") {
    // printf("this is mock server");
    mockServer = true;
  }

  if (!mockServer)
    for (auto &[_, val] : output_names)
      if (!results.contains(val.registerName))
        throw std::runtime_error("Expected to see " + val.registerName +
                                 " in the results, but did not see it.");

  // Construct idx[] such that output_names[idx[:]] is sorted by QIR qubit
  // number. There may initially be duplicate qubit numbers if that qubit was
  // measured multiple times. If that's true, make the lower-numbered result
  // occur first. (Dups will be removed in the next step below.)
  std::vector<std::size_t> idx;
  if (!mockServer) {
    idx.resize(output_names.size());
    std::iota(idx.begin(), idx.end(), 0);
    std::sort(idx.begin(), idx.end(), [&](std::size_t i1, std::size_t i2) {
      if (output_names[i1].qubitNum == output_names[i2].qubitNum)
        return i1 < i2; // choose lower result number
      return output_names[i1].qubitNum < output_names[i2].qubitNum;
    });

    // The global register only contains the *final* measurement of each
    // requested qubit, so eliminate lower-numbered results from idx array.
    for (auto it = idx.begin(); it != idx.end();) {
      if (std::next(it) != idx.end()) {
        if (output_names[*it].qubitNum ==
            output_names[*std::next(it)].qubitNum) {
          it = idx.erase(it);
          continue;
        }
      }
      ++it;
    }
  } else {
    idx.resize(1); // local mock server tests
  }

  // For each shot, we concatenate the measurements results of all qubits.
  auto begin = results.begin();
  auto nShots = begin.value().get<std::vector<std::string>>().size();
  std::vector<std::string> bitstrings(nShots);
  for (auto r : idx) {
    // If allNamesPresent == false, that means we are running local mock server
    // tests which don't support the full QIR output recording functions. Just
    // use the first key in that case.
    auto bitResults =
        mockServer ? results.at(begin.key()).get<std::vector<std::string>>()
                   : results.at(output_names[r].registerName)
                         .get<std::vector<std::string>>();
    for (size_t i = 0; auto &bit : bitResults)
      bitstrings[i++] += bit;
  }

  cudaq::CountsDictionary counts;
  for (auto &b : bitstrings)
    counts[b]++;

  // Store the combined results into the global register
  srs.emplace_back(counts, GlobalRegisterName);
  srs.back().sequentialData = bitstrings;
  sample_result sampleResult(srs);

  // Now reorder according to reorderIdx[]. This sorts the global bitstring in
  // original user qubit allocation order.
  auto thisJobReorderIdxIt = reorderIdx.find(jobId);
  if (thisJobReorderIdxIt != reorderIdx.end()) {
    auto &thisJobReorderIdx = thisJobReorderIdxIt->second;
    if (!thisJobReorderIdx.empty())
      sampleResult.reorder(thisJobReorderIdx);
  }

  return sampleResult;
}

std::map<std::string, std::string>
SdtServerHelper::generateRequestHeader() const {
  std::string apiKey, refreshKey, credentials, timeStr;
  searchAPIKey1(apiKey, refreshKey, credentials, timeStr,
               userSpecifiedCredentials);
  std::map<std::string, std::string> headers{
      {"Authorization", apiKey},
      {"Content-Type", "application/json"},
      {"Connection", "keep-alive"},
      {"Accept", "*/*"}};
  return headers;
}

std::map<std::string, std::string>
SdtServerHelper::generateRequestHeader(std::string authKey) const {
  std::map<std::string, std::string> headers{
      {"Authorization", authKey},
      {"Content-Type", "application/json"},
      {"Connection", "keep-alive"},
      {"Accept", "*/*"}};
  return headers;
}

// hjpark
void SdtServerHelper::postMessage(ServerMessage body) { // ServerMessage => nlohmann::json
  RestClient client;
  RestHeaders headers{        // RestHeaders => map<string, string>
    {"Authorization", qureaApiKey},
    {"Content-Type", "application/json"},
    {"Connection", "keep-alive"},
    {"Accept", "*/*"}
  };

  std::string url = qureaBaseUrl;
  std::string path = QUREA_API_GATEWAY_PATH + QUREA_ENGINE_JOB_PATH;
  std::cout << "[SDT] -------------------------- \n" << std::endl;
  std::cout << "[SDT] QUREA API url: \n" << url << std::endl;
  std::cout << "[SDT] QUREA API path: \n" << path << std::endl;

  ServerMessage response = client.post(url, path, body, headers);
  std::cout << "[SDT] Job API POST response: \n" << response << std::endl;
  // ServerMessage response1 = client.get(url, path, headers);
  // std::cout << "[SDT] Job API GET response: \n" << response1 << std::endl;
}

RestHeaders SdtServerHelper::getHeaders() { return generateRequestHeader(); }

/// Refresh the api key and refresh-token
void SdtServerHelper::refreshTokens(bool force_refresh) {
  std::mutex m;
  std::lock_guard<std::mutex> l(m);
  RestClient client;
  auto now = std::chrono::high_resolution_clock::now();

  if (apiKey.empty()) {
    force_refresh = true;
    if (refreshKey.empty())
      refreshKey = credentials;
  }
  if (timeStr.empty()) {
    timeStr = std::to_string(now.time_since_epoch().count());
  }

  // We first check how much time has elapsed since the
  // existing refresh key was created
  std::int64_t timeAsLong = std::stol(timeStr);
  std::chrono::high_resolution_clock::duration d(timeAsLong);
  std::chrono::high_resolution_clock::time_point oldTime(d);
  auto secondsDuration =
      1e-3 *
      std::chrono::duration_cast<std::chrono::milliseconds>(now - oldTime);

  // If we are getting close to an 30 min, then we will refresh
  bool needsRefresh = secondsDuration.count() * (1. / 1800.) > .85;
  if (needsRefresh || force_refresh) {
    cudaq::info("Refreshing id_token");
    std::stringstream ss;
    ss << "\"refresh_token\":\"" << refreshKey << "\"";
    auto headers = generateRequestHeader(refreshKey);
    nlohmann::json j;
    j["refresh_token"] = refreshKey;
    auto response_json = client.post(baseUrl, "login", j, headers);
    apiKey = response_json["id_token"].get<std::string>();
    refreshKey = response_json["refresh_token"].get<std::string>();
    std::ofstream out(credentialsPath);
    out << "key:" << apiKey << '\n';
    out << "refresh:" << refreshKey << '\n';
    out << "time:" << now.time_since_epoch().count() << '\n';
    timeStr = std::to_string(now.time_since_epoch().count());
  }
  // If the time string is empty, let's add it
  if (timeStr.empty()) {
    timeStr = std::to_string(now.time_since_epoch().count());
    std::ofstream out(credentialsPath);
    out << "key:" << apiKey << '\n';
    out << "refresh:" << refreshKey << '\n';
    out << "time:" << timeStr << '\n';
  }
}

void findApiKeyInFile1(std::string &apiKey, const std::string &path,
                      std::string &refreshKey, std::string &timeStr,
                      std::string &credentials) {
  std::ifstream stream(path);
  std::string contents((std::istreambuf_iterator<char>(stream)),
                       std::istreambuf_iterator<char>());

  std::vector<std::string> lines;
  lines = cudaq::split(contents, '\n');
  nlohmann::json jsoncreds;
  for (const std::string &l : lines) {
    std::vector<std::string> keyAndValue = cudaq::split(l, ':');
    if ((keyAndValue.size() != 2) &&
        ((keyAndValue[0] != "credentials") || (keyAndValue.size() != 4)))
      throw std::runtime_error("Ill-formed configuration file (" + path +
                               "). Key-value pairs must be in `<key> : "
                               "<value>` or `<key> : {username:<username>, "
                               "password:<password>}` format. (One per line)");
    cudaq::trim(keyAndValue[0]);
    cudaq::trim(keyAndValue[1]);
    if (keyAndValue[0] == "key")
      apiKey = keyAndValue[1];
    else if (keyAndValue[0] == "refresh")
      refreshKey = keyAndValue[1];
    else if (keyAndValue[0] == "time")
      timeStr = keyAndValue[1];
    else if (keyAndValue[0] ==
             "credentials") { // If the config file doesn't contain key and
                              // refresh token, we will add the username
                              // password to apikey for BasicHttpAuthentication
                              // and generation of tokens
      std::string linecontent =
          keyAndValue[1] + ":" + keyAndValue[2] + ":" + keyAndValue[3];
      // printf("The credentials read from the .config file is: %s",
      // linecontent.c_str());
      jsoncreds = json::parse(linecontent);
      std::string delim(":");
      std::string username = jsoncreds.at("username");
      std::string passwd = jsoncreds.at("password");
      std::string authInfo = username + delim + passwd;
      // authInfo = base64::to_base64(authInfo);
      authInfo = llvm::encodeBase64(authInfo);
      credentials = "Basic " + authInfo;
    } else
      throw std::runtime_error(
          "Unknown key in configuration file: " + keyAndValue[0] + ".");
  }

  if (credentials.empty() && refreshKey.empty())
    throw std::runtime_error("Empty credentials in configuration file (" +
                             path + ").");
  // The `time` key is not required.
}

/// Search for the API key
std::string searchAPIKey1(std::string &key, std::string &refreshKey,
                         std::string &credentials, std::string &timeStr,
                         std::string userSpecifiedConfig) {
  std::string hwConfig;
  // Allow someone to tweak this with an environment variable
  if (auto creds = std::getenv("CUDAQ_SDT_CREDENTIALS"))
    hwConfig = std::string(creds);
  else if (!userSpecifiedConfig.empty())
    hwConfig = userSpecifiedConfig;
  else
    hwConfig = std::string(getenv("HOME")) + std::string("/.sdt_config");
  if (cudaq::fileExists(hwConfig)) {
    findApiKeyInFile1(key, hwConfig, refreshKey, timeStr, credentials);
  } else {
    throw std::runtime_error("Cannot find Sdt Config file with credentials "
                             "(~/.sdt_config).");
  }

  return hwConfig;
}

void SdtServerHelper::updatePassPipeline(
    const std::filesystem::path &platformPath, std::string &passPipeline) {
  std::string qgate_type = "cgate";
  if (machine.starts_with("berkeley")) {
    qgate_type = "pgate";
    printf("Compiling gates for berkeley\n");
  } else if (machine.starts_with("telegraph")) {
    qgate_type = "cgate";
    printf("Compiling gates for telegraph\n");
  } else {
    printf("Unidentified machine type %s\n", machine.c_str());
  }
  passPipeline =
      std::regex_replace(passPipeline, std::regex("%Q_GATE%"), qgate_type);

  std::string pathToFile = platformPath / std::string("mapping/sdt") /
                           (machine + std::string(".txt"));
  passPipeline =
      std::regex_replace(passPipeline, std::regex("%QPU_ARCH%"), pathToFile);
}

} // namespace cudaq

CUDAQ_REGISTER_TYPE(cudaq::ServerHelper, cudaq::SdtServerHelper, sdt)
