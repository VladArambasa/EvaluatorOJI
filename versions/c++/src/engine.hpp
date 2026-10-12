// ENGINE.HPP - EVALUATION ENGINE OF THE C++ VERSION (SEE DOCS/SPECIFICATION.MD)
#pragma once
#include <string>
#include <vector>
#include "json.hpp"

namespace eng {

extern const char *VARIANT;
extern std::string PROBE_C_SOURCE;   // OLIEVAL_PROBE.C
extern std::string PROBE_PY_SOURCE;  // SITECUSTOMIZE.PY

int natcmp(const std::string &a, const std::string &b);

const js::Value *findProblem(const js::Value &ws, const std::string &name);
js::Value scan(const js::Value &ws, const std::string &problem, std::string &err);
js::Value structure(const js::Value &ws, bool create);
js::Value rescore(const js::Value &ws, const js::Value &prob, js::Value res);

bool startJob(const js::Value &ws, const std::vector<std::string> &problems, const std::vector<std::string> &onlyStudents,
              const std::vector<std::string> &onlyTests, std::string &err);
void stopJob();
js::Value status(long since, long rv);

}  // NAMESPACE ENG
