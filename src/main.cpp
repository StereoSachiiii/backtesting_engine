#include "core/engine.hpp"
#include "core/strategy/market_maker.hpp"
#include <iostream>
#include <iomanip>
#include <chrono>
#include <vector>

int main(int argc, char* argv[]) {
    bool raw = false;
    std::string path;

    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--raw") raw = true;
        else path = argv[i];
    }

    if (path.empty()) {
        std::cout << "Usage: hft_engine [--raw] <itch_file_path>\n";
        return 0;
    }

    std::vector<std::string> tickers = {
        "SPY",  "AAPL", "MSFT", "AMZN", "GOOGL", "GOOG", "META", "BRK.B",
        "JNJ",  "V",    "PG",   "UNH",  "HD",    "MA",   "DIS",  "NVDA",
        "PYPL", "BAC",  "VZ",   "ADBE", "CMCSA", "NFLX", "KO",   "NKE",
        "MRK",  "PEP",  "T",    "PFE",  "INTC",  "WMT",  "CSCO", "XOM",
        "ABT",  "CRM",  "CVX",  "ABBV", "ACN",   "AVGO", "MDT",  "COST",
        "TMO",  "MCD",  "LLY",  "DHR",  "TXN",   "NEE",  "BMY",  "AMGN",
        "LIN",  "PM",   "UNP",  "HON",  "IBM",   "QCOM", "LOW",  "ORCL",
        "RTX",  "SBUX", "GS",   "BLK",  "CAT",   "INTU", "ISRG", "GE",
        "AMD",  "NOW",  "BKNG", "GILD", "MMM",   "AXP",  "MDLZ", "CHTR",
        "TGT",  "MO",   "CVS",  "LRCX", "SYK",   "CI",   "ZTS",  "TMUS",
        "SPGI", "FIS",  "USB",  "BDX",  "TJX",   "ANTM", "CB",   "CME",
        "SO",   "D",    "CL",   "BSX",  "APD",   "ATVI", "REGN", "DE",
        "QQQ",  "IWM",  "EEM",  "XLF",  "TSLA",  "BA",   "JPM",  "WFC"
    };

    SimpleMarketMaker strategy(tickers);
    HFTEngine<SimpleMarketMaker> engine(strategy);
    engine.set_raw_mode(raw);

    for (const auto& t : tickers)
        engine.get_manager().add_to_whitelist(t);

    printf("[Main] Tracking %zu symbols | File: %s\n", tickers.size(), path.c_str());

    auto start = std::chrono::steady_clock::now();
    try {
        engine.run_file(path);
    } catch (const std::exception& e) {
        fprintf(stderr, "[Main] Exception: %s\n", e.what());
    }
    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    auto pnl = strategy.get_pnl(engine.get_manager());
    std::cout << "\n--- Engine Statistics ---\n"
              << "Processing time: " << duration << " ms\n"
              << std::fixed << std::setprecision(2)
              << "Realized PnL:   $" << pnl.realized << "\n"
              << "Unrealized PnL: $" << pnl.unrealized << "\n"
              << "Total PnL:      $" << pnl.total << "\n"
              << "Symbols Active: " << tickers.size() << "\n"
              << "----------------------------------------\n";

    return 0;
}
