#pragma once
// Text tables of frequency / dB / phase: microphone calibration files (the usual makers' formats, with or without a
// sensitivity header), FRD files, and CSV / TXT exports from the usual analysers.
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace la
{
struct FreqTable
{
    std::vector<double> freq, db, phase;   // phase is empty when the file has none
    std::vector<double> extra;             // optional 4th column (LiveAlign traces keep coherence here)
    std::vector<std::pair<std::string, std::string>> meta;   // "* key=value" comment lines
    bool hasSensFactor = false;
    double sensFactorDb = 0.0;

    bool empty() const { return freq.empty(); }
    bool hasPhase() const { return ! phase.empty() && phase.size() == freq.size(); }

    std::string metaValue (const std::string& key, const std::string& fallback = {}) const
    {
        for (const auto& kv : meta)
            if (kv.first == key) return kv.second;
        return fallback;
    }

    // dB at f: linear in log-frequency, holding the end values outside the table.
    double dbAt (double f) const { return interp (db, f); }
    double phaseAt (double f) const { return hasPhase() ? interp (phase, f) : 0.0; }

private:
    double interp (const std::vector<double>& v, double f) const
    {
        if (freq.empty() || v.size() != freq.size()) return 0.0;
        if (f <= freq.front()) return v.front();
        if (f >= freq.back()) return v.back();
        const auto it = std::upper_bound (freq.begin(), freq.end(), f);
        const size_t i = (size_t) (it - freq.begin());
        const double f0 = freq[i - 1], f1 = freq[i];
        const double t = (std::log (f) - std::log (f0)) / (std::log (f1) - std::log (f0));
        return v[i - 1] + t * (v[i] - v[i - 1]);
    }
};

namespace detail
{
inline std::string trim (const std::string& s)
{
    size_t a = 0, b = s.size();
    while (a < b && std::isspace ((unsigned char) s[a])) ++a;
    while (b > a && std::isspace ((unsigned char) s[b - 1])) --b;
    return s.substr (a, b - a);
}

inline std::string lower (std::string s)
{
    for (auto& c : s) c = (char) std::tolower ((unsigned char) c);
    return s;
}

// Pulls every number out of a data line. A line using ';' as the separator is treated as decimal-comma.
inline std::vector<double> numbers (std::string line)
{
    if (line.find (';') != std::string::npos)
    {
        for (auto& c : line)
            if (c == ',') c = '.';
            else if (c == ';') c = ' ';
    }
    else
    {
        for (auto& c : line)
            if (c == ',' || c == '\t') c = ' ';
    }

    std::vector<double> out;
    const char* p = line.c_str();
    while (*p != 0)
    {
        while (*p == ' ') ++p;
        if (*p == 0) break;
        char* end = nullptr;
        const double v = std::strtod (p, &end);
        if (end == p) return {};   // a word in the middle: not a data line
        out.push_back (v);
        p = end;
    }
    return out;
}
} // namespace detail

inline FreqTable parseFreqTable (const std::string& text)
{
    FreqTable t;
    std::istringstream in (text);
    std::string raw;
    struct Row { double f, db, ph, ex; int cols; };
    std::vector<Row> rows;

    while (std::getline (in, raw))
    {
        std::string line = detail::trim (raw);
        if (line.empty()) continue;

        const std::string low = detail::lower (line);
        const auto sens = low.find ("sens factor");
        if (sens != std::string::npos)
        {
            const auto eq = line.find ('=', sens);
            if (eq != std::string::npos)
            {
                t.sensFactorDb = std::strtod (line.c_str() + eq + 1, nullptr);
                t.hasSensFactor = true;
            }
        }

        const char c0 = line[0];
        if (c0 == '*' || c0 == '#' || c0 == '"' || c0 == ';' || c0 == '/' || std::isalpha ((unsigned char) c0))
        {
            // "* key=value" metadata (LiveAlign trace files)
            if (c0 == '*' || c0 == '#')
            {
                const std::string body = detail::trim (line.substr (1));
                const auto eq = body.find ('=');
                if (eq != std::string::npos && eq > 0 && body.find (' ') > eq)
                    t.meta.push_back ({ body.substr (0, eq), body.substr (eq + 1) });
            }
            continue;
        }

        const auto nums = detail::numbers (line);
        if (nums.size() < 2 || ! (nums[0] > 0.0) || ! std::isfinite (nums[1])) continue;
        Row r { nums[0], nums[1], nums.size() > 2 ? nums[2] : 0.0, nums.size() > 3 ? nums[3] : 0.0, (int) nums.size() };
        rows.push_back (r);
    }

    std::stable_sort (rows.begin(), rows.end(), [] (const Row& a, const Row& b) { return a.f < b.f; });
    bool allPhase = ! rows.empty(), allExtra = ! rows.empty();
    for (const auto& r : rows) { allPhase = allPhase && r.cols >= 3; allExtra = allExtra && r.cols >= 4; }

    for (const auto& r : rows)
    {
        if (! t.freq.empty() && r.f <= t.freq.back()) continue;   // drop duplicate frequencies
        t.freq.push_back (r.f);
        t.db.push_back (r.db);
        if (allPhase) t.phase.push_back (r.ph);
        if (allExtra) t.extra.push_back (r.ex);
    }
    return t;
}

// FRD text: "freq dB phase" per line, readable by most measurement, simulation and crossover tools.
inline std::string writeFrd (const FreqTable& t, const std::vector<std::string>& headerLines, bool includeExtra)
{
    std::ostringstream out;
    out.setf (std::ios::fixed);
    for (const auto& h : headerLines) out << "* " << h << "\n";
    for (const auto& kv : t.meta) out << "* " << kv.first << "=" << kv.second << "\n";
    for (size_t i = 0; i < t.freq.size(); ++i)
    {
        if (! std::isfinite (t.db[i])) continue;
        out.precision (4);
        out << t.freq[i] << " ";
        out.precision (3);
        out << t.db[i] << " " << (t.hasPhase() && std::isfinite (t.phase[i]) ? t.phase[i] : 0.0);
        if (includeExtra && i < t.extra.size()) out << " " << (std::isfinite (t.extra[i]) ? t.extra[i] : 0.0);
        out << "\n";
    }
    return out.str();
}
} // namespace la
