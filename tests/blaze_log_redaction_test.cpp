// Behavioral test: Blaze::Log (darkspore_server/source/blaze/tdf.cpp) must not
// print credentials, session secrets or e-mail addresses from packet dumps.
// Build (see gameplay-guardrails.yml): g++ -std=c++23 -DRAPIDJSON_HAS_STDSTRING
//   -I<rapidjson>/include -Idarkspore_server/source tests/blaze_log_redaction_test.cpp
//   darkspore_server/source/blaze/tdf.cpp darkspore_server/source/databuffer.cpp
#include "blaze/tdf.h"

#include <iostream>
#include <sstream>
#include <string>

int main() {
	rapidjson::Document document;
	document.Parse(R"({
		"MAIL": "player@example.invalid",
		"PASS": "hunter2-secret",
		"DVID": 0,
		"SESS": { "SKEY": "session-key-secret", "BUID": 42 },
		"LIST": [ { "TOKN": "token-secret" }, { "AUTH": "auth-secret" } ],
		"PNAM": "VisibleName"
	})");
	if (document.HasParseError()) {
		std::cerr << "fixture parse error\n";
		return 2;
	}

	std::ostringstream captured;
	auto* previous = std::cout.rdbuf(captured.rdbuf());
	Blaze::Log(document);
	std::cout.rdbuf(previous);
	const std::string out = captured.str();

	int failures = 0;
	for (const char* secret : { "player@example.invalid", "hunter2-secret", "session-key-secret", "token-secret", "auth-secret" }) {
		if (out.find(secret) != std::string::npos) {
			std::cerr << "FAIL: logged secret value " << secret << '\n';
			++failures;
		}
	}
	for (const char* kept : { "\"PASS\"", "<redacted>", "VisibleName", "\"BUID\": 42" }) {
		if (out.find(kept) == std::string::npos) {
			std::cerr << "FAIL: expected " << kept << " in log output\n";
			++failures;
		}
	}
	if (document["PASS"].GetString() != std::string("hunter2-secret")) {
		std::cerr << "FAIL: logging modified the original document\n";
		++failures;
	}
	if (failures != 0) {
		std::cerr << out;
		return 1;
	}
	std::cout << "blaze_log_redaction_test: all checks passed\n";
	return 0;
}
